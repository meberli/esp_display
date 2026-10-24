// SenseCAP Indicator D1 panel driver (CONFIG_PANEL_BOARD_SENSECAP_D1).
// Same API as panel_hw.cpp (Guition); only one of the two is compiled.
// Hardware facts from Seeed's BSP (SenseCAP_Indicator_ESP32, components/bsp).
#include "panel_hw.h"

#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "diag.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "fonts.h"
#include "lvgl.h"
#include "panel_ui.h"
#include "sys.h"
#include "test_harness.h"
#include <esp_log.h>

static const char* const TAG = "panel_d1";

namespace {

// Seeed runs the GX screen at 18 MHz, but with PSRAM at 120 MHz (experimental) and no
// bounce buffers. Here PSRAM runs at 80 MHz octal and Wi-Fi/LVGL share it, so 16 MHz
// (~56 Hz refresh) leaves headroom for the bounce-buffer refills (36 -> 32 MB/s scan-out).
constexpr uint32_t kPclkHz = 16 * 1000 * 1000;
// 10 lines (~0.3 ms) were too little slack: every ~12 s a late bounce EOF ISR
// (Wi-Fi/flash critical sections) made the driver restart DMA mid-frame, which
// shows one torn/shifted frame ("flicker"). vsync - fb_complete counts those.
constexpr int kBounceLines = 20;

// ---------------------------------------------------------------------------
// Shared I2C bus (expander + touch).

i2c_master_bus_handle_t i2c_bus = nullptr;

bool bus_begin() {
  if (i2c_bus != nullptr) return true;
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = I2C_NUM_0;
  bc.sda_io_num = static_cast<gpio_num_t>(kPinTpSda);
  bc.scl_io_num = static_cast<gpio_num_t>(kPinTpScl);
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = 1;
  if (i2c_new_master_bus(&bc, &i2c_bus) != ESP_OK) {
    ESP_LOGE(TAG, "I2C bus init failed");
    i2c_bus = nullptr;
    return false;
  }
  return true;
}

i2c_master_dev_handle_t add_dev(uint8_t addr, uint32_t hz) {
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = hz;
  i2c_master_dev_handle_t h = nullptr;
  if (i2c_master_bus_add_device(i2c_bus, &dc, &h) != ESP_OK) return nullptr;
  return h;
}

// ---------------------------------------------------------------------------
// PCA9535 / TCA9535 16-bit I/O expander. Registers: 0/1 input, 2/3 output,
// 4/5 polarity, 6/7 config (1 = input). Only our own pins are changed;
// every other pin keeps its power-on (input) configuration.

i2c_master_dev_handle_t exp_dev = nullptr;
uint8_t exp_addr = 0;
uint16_t exp_out = 0xFFFF;
uint16_t exp_cfg = 0xFFFF;

bool exp_write16(uint8_t reg, uint16_t v) {
  uint8_t w[3] = {reg, static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
  return i2c_master_transmit(exp_dev, w, 3, 50) == ESP_OK;
}

bool exp_read16(uint8_t reg, uint16_t* v) {
  uint8_t r[2] = {};
  if (i2c_master_transmit_receive(exp_dev, &reg, 1, r, 2, 50) != ESP_OK) return false;
  *v = static_cast<uint16_t>(r[0] | (r[1] << 8));
  return true;
}

bool exp_begin() {
  if (exp_dev != nullptr) return true;
  if (!bus_begin()) return false;
  const uint8_t addrs[] = {kExpAddr1, kExpAddr2};
  for (uint8_t a : addrs) {
    if (i2c_master_probe(i2c_bus, a, 50) == ESP_OK) {
      exp_addr = a;
      break;
    }
  }
  if (exp_addr == 0) {
    console_puts("PCA9535 not found at 0x20 or 0x39");
    return false;
  }
  exp_dev = add_dev(exp_addr, 400000);
  if (exp_dev == nullptr) return false;
  if (!exp_read16(2, &exp_out) || !exp_read16(6, &exp_cfg)) {
    console_puts("PCA9535 read failed");
    return false;
  }
  console_printf("PCA9535 at 0x%02X (out %04X cfg %04X)\n", exp_addr, exp_out, exp_cfg);
  return true;
}

// Make `pin` an output with `level` (output register first, then direction).
bool exp_set(int pin, bool level) {
  uint16_t m = static_cast<uint16_t>(1u << pin);
  uint16_t out = level ? (exp_out | m) : (exp_out & ~m);
  if (out != exp_out || (exp_cfg & m) != 0) {
    if (!exp_write16(2, out)) return false;
    exp_out = out;
  }
  if ((exp_cfg & m) != 0) {
    uint16_t cfg = exp_cfg & ~m;
    if (!exp_write16(6, cfg)) return false;
    exp_cfg = cfg;
  }
  return true;
}

// ---------------------------------------------------------------------------
// ST7701S over 3-wire SPI (9-bit words), SCK/MOSI on GPIOs, CS on the expander.

void spi_word(bool data, uint8_t v) {
  uint16_t w = (data ? 0x100 : 0) | v;
  for (int bit = 8; bit >= 0; --bit) {
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdMosi), (w >> bit) & 1);
    esp_rom_delay_us(2);
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 1);
    esp_rom_delay_us(2);
  }
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
}

void st_cmd(uint8_t cmd, const uint8_t* data, size_t n) {
  exp_set(kExpLcdCs, false);
  spi_word(false, cmd);
  for (size_t i = 0; i < n; ++i) spi_word(true, data[i]);
  exp_set(kExpLcdCs, true);
}

// Seeed's ST7701S sequence (lcd_panel_config.c, "GX" screen) as
// {command, length, data...}; 0xFF length is a delay in ms. It is the
// Arduino_GFX type9 table plus C7 04, CD 08, MADCTL 10 and INVON.
const uint8_t kInit[] = {
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x0D, 0x02,
    0xC2, 2, 0x31, 0x05,
    0xC7, 1, 0x04,
    0xCD, 1, 0x08,
    0xB0, 16, 0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,
    0xB1, 16, 0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    0xB0, 1, 0x60,
    0xB1, 1, 0x32,
    0xB2, 1, 0x07,
    0xB3, 1, 0x80,
    0xB5, 1, 0x49,
    0xB7, 1, 0x85,
    0xB8, 1, 0x21,
    0xC1, 1, 0x78,
    0xC2, 1, 0x78,
    0x00, 0xFF, 20,  // Seeed: Delay(20) after bank-1 power settings
    0xE0, 3, 0x00, 0x1B, 0x02,
    0xE1, 11, 0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44,
    0xE2, 12, 0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00,
    0xE3, 4, 0x00, 0x00, 0x11, 0x11,
    0xE4, 2, 0x44, 0x44,
    0xE5, 16, 0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,
    0xE6, 4, 0x00, 0x00, 0x11, 0x11,
    0xE7, 2, 0x44, 0x44,
    0xE8, 16, 0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
    0xEB, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    0xEC, 2, 0x3C, 0x00,
    0xED, 16, 0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,
    0x36, 1, 0x10,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xE5, 1, 0xE4,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    0x3A, 1, 0x60,
    0x21, 0,
    0x11, 0,
    0x00, 0xFF, 120,  // delay
    0x29, 0,
    0x00, 0xFF, 120,
};

bool st7701_init() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << kPinLcdSck) | (1ULL << kPinLcdMosi);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
  if (!exp_begin()) return false;
  // Seeed drives the RP2040 reset line high (released); do the same so the
  // coprocessor state does not depend on a floating pin. It is never commanded.
  bool ok = exp_set(kExpRp2040Rst, true) && exp_set(kExpLcdCs, true) && exp_set(kExpLcdRst, false);
  sleep_ms(10);
  ok = ok && exp_set(kExpLcdRst, true);
  if (!ok) {
    console_puts("PCA9535 write failed");
    return false;
  }
  sleep_ms(120);
  size_t i = 0;
  while (i < sizeof(kInit)) {
    uint8_t cmd = kInit[i];
    uint8_t len = kInit[i + 1];
    if (len == 0xFF) {
      sleep_ms(kInit[i + 2]);
      i += 3;
      continue;
    }
    st_cmd(cmd, &kInit[i + 2], len);
    i += 2 + len;
  }
  console_puts("ST7701S init sent (expander CS/RST)");
  return true;
}

// ---------------------------------------------------------------------------
// RGB panel. Porches from Seeed (GX): hsync 8/50/10, vsync 8/20/10.

esp_lcd_panel_handle_t rgb = nullptr;
uint16_t* fb = nullptr;        // buffer currently scanned out (DIAGFB stats)
uint16_t* fbs[2] = {};         // both framebuffers = LVGL direct-mode buffers
SemaphoreHandle_t vsync_sem = nullptr;

// Frame timing for DIAGHW (no camera): vsync count/interval and the flip wait.
volatile uint32_t st_vsync = 0, st_bounce = 0;
volatile int64_t st_vsync_last = 0;
volatile uint32_t st_vsync_max_us = 0;
uint32_t st_flips = 0, st_flip_max_us = 0, st_copy_max_us = 0, st_flip_timeouts = 0;
volatile bool want_vsync = false;
// The bounce-buffer ISR latches the next scan-out FB (bb_fb_index = cur_fb_index) when it has
// fetched the last line of a frame, which is BEFORE that frame's VSYNC (prefetch). A swap
// requested between that latch and VSYNC is only picked up one frame later, so waiting for
// VSYNC alone let sync_dirty() write into the FB that was still being scanned -> one-frame
// flash at the end of scroll/swipe. We now wait for on_frame_buf_complete (= latch point).
volatile uint32_t st_early_vsync = 0;  // VSYNCs seen while a swap was pending but not latched

bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  const int64_t t = esp_timer_get_time();
  if (st_vsync_last != 0) {
    const uint32_t d = static_cast<uint32_t>(t - st_vsync_last);
    if (d > st_vsync_max_us) st_vsync_max_us = d;
  }
  st_vsync_last = t;
  st_vsync = st_vsync + 1;
  BaseType_t woken = pdFALSE;
  if (want_vsync) st_early_vsync = st_early_vsync + 1;
  return woken == pdTRUE;
}

bool IRAM_ATTR on_frame_buf_complete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  st_bounce = st_bounce + 1;
  BaseType_t woken = pdFALSE;
  if (want_vsync) {  // the driver has just latched cur_fb for the next frame
    want_vsync = false;
    xSemaphoreGiveFromISR(vsync_sem, &woken);
  }
  return woken == pdTRUE;
}

bool rgb_begin() {
  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = kPclkHz;
  c.timings.h_res = kPanelWidth;
  c.timings.v_res = kPanelHeight;
  c.timings.hsync_pulse_width = 8;
  c.timings.hsync_back_porch = 50;
  c.timings.hsync_front_porch = 10;
  c.timings.vsync_pulse_width = 8;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 10;
  c.timings.flags.hsync_idle_low = 0;
  c.timings.flags.vsync_idle_low = 0;
  c.timings.flags.de_idle_high = 0;
  c.timings.flags.pclk_active_neg = 0;
  c.timings.flags.pclk_idle_high = 0;
  c.data_width = 16;
  c.in_color_format = LCD_COLOR_FMT_RGB565;
  c.out_color_format = LCD_COLOR_FMT_RGB565;
  // Two PSRAM framebuffers: LVGL renders into the hidden one (direct mode) and the
  // driver switches at vsync, so a scroll never shows a half-updated frame (with one
  // buffer the 40-line flushes were copied into the buffer while it was scanned out:
  // tearing/flicker). Bounce buffers stay: DMA reads internal RAM, the CPU refills
  // them from PSRAM, so PSRAM/flash stalls do not starve the panel directly.
  c.num_fbs = 2;
  c.bounce_buffer_size_px = kPanelWidth * kBounceLines;  // 2 x N lines in internal DRAM
  c.dma_burst_size = 64;
  c.hsync_gpio_num = static_cast<gpio_num_t>(kPinHsync);
  c.vsync_gpio_num = static_cast<gpio_num_t>(kPinVsync);
  c.de_gpio_num = static_cast<gpio_num_t>(kPinDe);
  c.pclk_gpio_num = static_cast<gpio_num_t>(kPinPclk);
  c.disp_gpio_num = GPIO_NUM_NC;
  const int pins[16] = {kPinB0, kPinB1, kPinB2, kPinB3, kPinB4, kPinG0, kPinG1, kPinG2,
                        kPinG3, kPinG4, kPinG5, kPinR0, kPinR1, kPinR2, kPinR3, kPinR4};
  for (int i = 0; i < 16; ++i) c.data_gpio_nums[i] = static_cast<gpio_num_t>(pins[i]);
  c.flags.fb_in_psram = 1;
  if (esp_lcd_new_rgb_panel(&c, &rgb) != ESP_OK) return false;
  vsync_sem = xSemaphoreCreateBinary();
  if (vsync_sem == nullptr) return false;
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = on_vsync;
  cbs.on_frame_buf_complete = on_frame_buf_complete;
  if (esp_lcd_rgb_panel_register_event_callbacks(rgb, &cbs, nullptr) != ESP_OK) return false;
  if (esp_lcd_panel_reset(rgb) != ESP_OK || esp_lcd_panel_init(rgb) != ESP_OK) return false;
  void* p0 = nullptr;
  void* p1 = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(rgb, 2, &p0, &p1) != ESP_OK || p0 == nullptr || p1 == nullptr) return false;
  fbs[0] = static_cast<uint16_t*>(p0);
  fbs[1] = static_cast<uint16_t*>(p1);
  fb = fbs[0];
  for (uint16_t* b : fbs) {
    memset(b, 0, kPanelWidth * kPanelHeight * 2);
    esp_cache_msync(b, kPanelWidth * kPanelHeight * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
  return true;
}

// ---------------------------------------------------------------------------
// FT6336U (FT5x06 register set). 0x02 = TD_STATUS (points), 0x03.. = P1 XH
// (event in bits 7..6, x[11:8]), XL, YH (id, y[11:8]), YL.

i2c_master_dev_handle_t ft = nullptr;
uint8_t ft_addr = 0;
bool touch_ok = false;
bool last_pressed = false;
int last_x = 0;
int last_y = 0;
int raw_min_x = 9999, raw_max_x = -1, raw_min_y = 9999, raw_max_y = -1;

bool ft_read(uint8_t reg, uint8_t* out, size_t n) {
  return i2c_master_transmit_receive(ft, &reg, 1, out, n, 50) == ESP_OK;
}

// The FT6336 reports in the opposite corner to the ST7701S scan origin: Seeed's
// indicator_basis lv_port.c maps x = 480 - x, y = 480 - y for ROTATION_0 (seen on the
// D1: a tap bottom-left hit the element top-right). Mirrored on both axes, no swap.
int ft_mirror(int v, int size) {
  int m = size - 1 - v;
  return m < 0 ? 0 : (m >= size ? size - 1 : m);
}

void ft_map(int raw_x, int raw_y) {
  bool was = last_pressed;
  last_x = ft_mirror(raw_x, kPanelWidth);
  last_y = ft_mirror(raw_y, kPanelHeight);
  last_pressed = true;
  if (!was) console_printf("FT6336 press raw %d,%d -> %d,%d\n", raw_x, raw_y, last_x, last_y);
}

void ft_poll() {
  uint8_t p[5] = {};
  bool ok = ft_read(0x02, p, sizeof(p));
  int n = ok ? (p[0] & 0x0F) : 0;
  diag_touch_poll(ok, ok, ok && n > 0 && n <= 2);
  int ix, iy;
  bool ip, iraw;
  if (diag_touch_inject(&ix, &iy, &ip, &iraw)) {
    if (ip && iraw) {
      ft_map(ix, iy);
    } else if (ip) {
      last_x = ix;
      last_y = iy;
    }
    last_pressed = ip;
    return;
  }
  if (!ok) return;
  if (n > 0 && n <= 2) {
    int raw_x = ((p[1] & 0x0F) << 8) | p[2];
    int raw_y = ((p[3] & 0x0F) << 8) | p[4];
    if (raw_x < raw_min_x) raw_min_x = raw_x;
    if (raw_x > raw_max_x) raw_max_x = raw_x;
    if (raw_y < raw_min_y) raw_min_y = raw_y;
    if (raw_y > raw_max_y) raw_max_y = raw_y;
    ft_map(raw_x, raw_y);
  } else {
    last_pressed = false;
  }
}

constexpr uint32_t kBlMax = (1u << 10) - 1;
constexpr uint32_t kBlPwmHz = 1000;
uint8_t bl_level = 0;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

// LVGL direct mode into the two RGB framebuffers (as Seeed's LCD_AVOID_TEAR +
// LCD_LVGL_DIRECT_MODE). LVGL has drawn the dirty areas straight into the hidden
// buffer `color`. On the last area: write the cache back, ask the driver to show that
// buffer (it switches at the next vsync), wait for the vsync, then copy the dirty areas
// into the other buffer so both stay identical for the next frame.
void sync_dirty(lv_disp_drv_t* drv, const uint16_t* from, uint16_t* to, bool writeback_from) {
  lv_disp_t* d = _lv_refr_get_disp_refreshing();
  for (int i = 0; i < d->inv_p; ++i) {
    if (d->inv_area_joined[i]) continue;
    const lv_area_t& a = d->inv_areas[i];
    const size_t off = static_cast<size_t>(a.y1) * kPanelWidth;
    const size_t bytes = static_cast<size_t>(a.y2 - a.y1 + 1) * kPanelWidth * 2;
    if (writeback_from) {
      esp_cache_msync(const_cast<uint16_t*>(from) + off, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
      continue;
    }
    const int w = a.x2 - a.x1 + 1;
    for (int y = a.y1; y <= a.y2; ++y) {
      const size_t o = static_cast<size_t>(y) * kPanelWidth + a.x1;
      memcpy(to + o, from + o, static_cast<size_t>(w) * 2);
    }
    esp_cache_msync(to + off, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
  (void)drv;
}

// Ring of the last flips for DIAGHW (time, area count, dirty rows, which FB).
struct FlipLog { uint32_t t_ms; uint8_t n; uint8_t idx; int16_t y1, y2; };
FlipLog flip_log[8];
uint32_t flip_log_pos = 0;

void panel_flush(lv_disp_drv_t* drv, const lv_area_t*, lv_color_t* color) {
  if (!lv_disp_flush_is_last(drv)) {
    lv_disp_flush_ready(drv);
    return;
  }
  uint16_t* shown = reinterpret_cast<uint16_t*>(color);
  uint16_t* other = shown == fbs[0] ? fbs[1] : fbs[0];
  sync_dirty(drv, shown, nullptr, true);
  const int64_t t0 = esp_timer_get_time();
  xSemaphoreTake(vsync_sem, 0);
  want_vsync = true;
  esp_lcd_panel_draw_bitmap(rgb, 0, 0, kPanelWidth, kPanelHeight, shown);  // latched at next frame end
  if (xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(60)) != pdTRUE) ++st_flip_timeouts;
  want_vsync = false;
  const int64_t t1 = esp_timer_get_time();
  fb = shown;
  sync_dirty(drv, shown, other, false);
  const int64_t t2 = esp_timer_get_time();
  ++st_flips;
  {
    lv_disp_t* d = _lv_refr_get_disp_refreshing();
    FlipLog& L = flip_log[flip_log_pos++ % 8];
    L.t_ms = static_cast<uint32_t>(t0 / 1000);
    L.n = static_cast<uint8_t>(d->inv_p);
    L.idx = shown == fbs[0] ? 0 : 1;
    L.y1 = 480; L.y2 = -1;
    for (int i = 0; i < d->inv_p; ++i) {
      if (d->inv_area_joined[i]) continue;
      if (d->inv_areas[i].y1 < L.y1) L.y1 = d->inv_areas[i].y1;
      if (d->inv_areas[i].y2 > L.y2) L.y2 = d->inv_areas[i].y2;
    }
  }
  if (t1 - t0 > st_flip_max_us) st_flip_max_us = static_cast<uint32_t>(t1 - t0);
  if (t2 - t1 > st_copy_max_us) st_copy_max_us = static_cast<uint32_t>(t2 - t1);
  lv_disp_flush_ready(drv);
}

void panel_touch_read_raw(lv_indev_data_t* data) {
  if (harness_touch_override(data)) return;
  if (!touch_ok) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  ft_poll();
  if (last_pressed) {
    int x = last_x < 0 ? 0 : (last_x >= kPanelWidth ? kPanelWidth - 1 : last_x);
    int y = last_y < 0 ? 0 : (last_y >= kPanelHeight ? kPanelHeight - 1 : last_y);
    data->point.x = static_cast<lv_coord_t>(x);
    data->point.y = static_cast<lv_coord_t>(y);
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void panel_touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
  panel_touch_read_raw(data);
  // Screen off: the waking touch is swallowed until release (no tap, no swipe).
  if (!ui_power_touch(data->state == LV_INDEV_STATE_PRESSED, data->point.x, data->point.y)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  ui_touch_sample(data->state == LV_INDEV_STATE_PRESSED, data->point.x, data->point.y);
}

}  // namespace

bool panel_display_begin() {
  if (!st7701_init()) return false;
  if (!rgb_begin()) return false;
  ledc_timer_config_t tc = {};
  tc.speed_mode = LEDC_LOW_SPEED_MODE;
  tc.duty_resolution = LEDC_TIMER_10_BIT;
  tc.timer_num = LEDC_TIMER_0;
  tc.freq_hz = kBlPwmHz;
  tc.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&tc) != ESP_OK) return false;
  ledc_channel_config_t cc = {};
  cc.gpio_num = kPinBacklight;
  cc.speed_mode = LEDC_LOW_SPEED_MODE;
  cc.channel = LEDC_CHANNEL_0;
  cc.timer_sel = LEDC_TIMER_0;
  cc.duty = kBlMax;
  if (ledc_channel_config(&cc) != ESP_OK) return false;
  ledc_fade_func_install(0);
  bl_level = 100;
  return true;
}

uint8_t panel_touch_begin() {
  touch_ok = false;
  if (!exp_begin()) return 0;
  // Touch reset through the expander (Seeed: low, 5 ms, high).
  exp_set(kExpTpRst, false);
  sleep_ms(5);
  exp_set(kExpTpRst, true);
  sleep_ms(200);
  const uint8_t addrs[] = {0x48, 0x38};
  for (uint8_t a : addrs) {
    if (i2c_master_probe(i2c_bus, a, 50) == ESP_OK) {
      ft_addr = a;
      break;
    }
  }
  if (ft_addr == 0) {
    console_puts("FT6336 not found at 0x48 or 0x38");
    return 0;
  }
  ft = add_dev(ft_addr, 400000);
  if (ft == nullptr) return 0;
  // Same register setup as Seeed's ft5x06_init (thresholds and timing).
  const uint8_t setup[][2] = {{0x80, 70}, {0x81, 60}, {0x82, 16}, {0x83, 60}, {0x84, 10},
                              {0x85, 20}, {0x87, 2},  {0x88, 12}, {0x89, 40}};
  for (const auto& r : setup) i2c_master_transmit(ft, r, 2, 50);
  touch_ok = true;
  console_printf("FT6336 at 0x%02X\n", ft_addr);
  return ft_addr;
}

void panel_backlight(uint8_t percent, uint32_t fade_ms) {
  if (percent > 100) percent = 100;
  bl_level = percent;
  uint32_t duty = kBlMax * percent / 100;
  ledc_fade_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  if (fade_ms == 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  } else {
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, static_cast<int>(fade_ms));
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
  }
}

uint8_t panel_backlight_level() { return bl_level; }

void panel_bl_describe(char* out, size_t n) {
  snprintf(out, n, "bl_set=%u%% duty=%lu/%lu freq=%luHz gpio%d", static_cast<unsigned>(bl_level),
           static_cast<unsigned long>(ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0)),
           static_cast<unsigned long>(kBlMax), static_cast<unsigned long>(ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0)),
           static_cast<int>(kPinBacklight));
}

void panel_fb_stats(uint32_t* black, uint32_t* white, uint32_t* red, uint32_t* other) {
  *black = *white = *red = *other = 0;
  if (fb == nullptr) return;
  for (size_t i = 0; i < static_cast<size_t>(kPanelWidth) * kPanelHeight; i += 4) {  // every 4th pixel
    uint16_t c = fb[i];
    int r = c >> 11, g = (c >> 5) & 0x3F, b = c & 0x1F;
    if (r < 3 && g < 6 && b < 3) ++*black;
    else if (r > 26 && g > 54 && b > 26) ++*white;
    else if (r > 24 && g < 10 && b < 6) ++*red;
    else ++*other;
  }
}

bool panel_touch_ok() { return touch_ok; }

// GT911 register access (DIAGGT) does not exist on this board.
bool panel_gt911_read(uint16_t, uint8_t*, size_t) { return false; }

bool panel_lvgl_start() {
  lv_init();
  // Direct mode: LVGL draws into the two RGB framebuffers themselves (see panel_flush).
  if (fbs[0] == nullptr || fbs[1] == nullptr) return false;
  const uint32_t pixels = static_cast<uint32_t>(kPanelWidth) * kPanelHeight;
  lv_disp_draw_buf_init(&draw_buf, fbs[0], fbs[1], pixels);
  lv_disp_drv_init(&disp_drv);
  disp_drv.direct_mode = 1;
  disp_drv.hor_res = kPanelWidth;
  disp_drv.ver_res = kPanelHeight;
  disp_drv.flush_cb = panel_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t* disp = lv_disp_drv_register(&disp_drv);
  // lv_conf.h had LV_FONT_DEFAULT (then DejaVu 20); LVGL's Kconfig cannot name
  // a custom font, so the default theme gets it here instead.
  lv_theme_t* th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                         true, &font_inter_20);
  lv_disp_set_theme(disp, th);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = panel_touch_read;
  indev_drv.scroll_limit = 12;
  indev_drv.gesture_limit = 40;
  lv_indev_drv_register(&indev_drv);
  return true;
}

// No relay on this board (kBoardHasRelay = false): the UI hides the tile.
void panel_relay_begin() {}
bool panel_relay_is_on() { return false; }
void panel_relay_set(bool) {}

// DIAGHW (D1): read-only hardware readback for verification without a camera.
void panel_hw_dump() {
  uint8_t id[4] = {}, th[10] = {}, mode = 0xFF;
  bool a = touch_ok && ft_read(0xA3, id, 1) && ft_read(0xA6, id + 1, 1) && ft_read(0xA8, id + 2, 1) &&
           ft_read(0xA1, id + 3, 1);
  bool b = touch_ok && ft_read(0x80, th, sizeof(th));
  bool c = touch_ok && ft_read(0xA4, &mode, 1);
  console_printf("DIAG FT addr=0x%02X ok=%d%d%d chip=0x%02X fw=0x%02X vendor=0x%02X lib=0x%02X g_mode=0x%02X "
                 "th=%u/%u/%u/%u/%u/%u act=%u mon=%u raw_x=%d..%d raw_y=%d..%d\n",
                 ft_addr, a, b, c, id[0], id[1], id[2], id[3], mode, th[0], th[1], th[2], th[3], th[4], th[5], th[8],
                 th[9], raw_min_x, raw_max_x, raw_min_y, raw_max_y);
  uint16_t out = 0, cfg = 0;
  bool e = exp_dev && exp_read16(2, &out) && exp_read16(6, &cfg);
  console_printf("DIAG EXP addr=0x%02X ok=%d out=%04X cfg=%04X lcd_cs=%d lcd_rst=%d tp_rst=%d\n", exp_addr, e, out, cfg,
                 (out >> kExpLcdCs) & 1, (out >> kExpLcdRst) & 1, (out >> kExpTpRst) & 1);
  console_printf("DIAG BLPIN gpio%d out_sel=%u (ledc_ls0=%u) inv=%u duty=%lu\n", kPinBacklight,
                 (unsigned)(GPIO.func_out_sel_cfg[kPinBacklight].val & 0x1FF), (unsigned)LEDC_LS_SIG_OUT0_IDX,
                 (unsigned)((GPIO.func_out_sel_cfg[kPinBacklight].val >> 9) & 1),
                 (unsigned long)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
  const int pins[16] = {kPinB0, kPinB1, kPinB2, kPinB3, kPinB4, kPinG0, kPinG1, kPinG2,
                        kPinG3, kPinG4, kPinG5, kPinR0, kPinR1, kPinR2, kPinR3, kPinR4};
  char buf[200];
  int k = 0;
  bool order = true;
  for (int i = 0; i < 16; ++i) {
    unsigned sel = (GPIO.func_out_sel_cfg[pins[i]].val & 0x1FF);
    if (sel != (unsigned)(LCD_DATA_OUT0_IDX + i)) order = false;
    k += snprintf(buf + k, sizeof(buf) - k, "%d:%u ", pins[i], sel - LCD_DATA_OUT0_IDX);
  }
  const int64_t age = esp_timer_get_time() - st_vsync_last;
  console_printf("DIAG RGB pclk=%lu fbs=2 bounce=%d vsync=%lu fb_complete=%lu vsync_max_us=%lu last_vsync_age_us=%lld "
                 "flips=%lu flip_max_us=%lu copy_max_us=%lu flip_timeouts=%lu early_vsync=%lu\n",
                 (unsigned long)kPclkHz, kPanelWidth * kBounceLines, (unsigned long)st_vsync, (unsigned long)st_bounce,
                 (unsigned long)st_vsync_max_us, (long long)age, (unsigned long)st_flips, (unsigned long)st_flip_max_us,
                 (unsigned long)st_copy_max_us, (unsigned long)st_flip_timeouts,
                 (unsigned long)st_early_vsync);
  st_vsync_max_us = 0;  // maxima since the last DIAGHW
  st_flip_max_us = 0;
  st_copy_max_us = 0;
  {
    // Rows where the two FBs differ (CPU view). Outside a refresh they must be
    // identical, otherwise the next swap shows stale content for a frame.
    int diff = 0, first = -1, last = -1;
    for (int y = 0; y < kPanelHeight; ++y) {
      if (memcmp(fbs[0] + y * kPanelWidth, fbs[1] + y * kPanelWidth, kPanelWidth * 2) != 0) {
        ++diff;
        if (first < 0) first = y;
        last = y;
      }
    }
    console_printf("DIAG RGBFB shown=%d diff_rows=%d (%d..%d)\n", fb == fbs[0] ? 0 : 1, diff, first, last);
    char lb[220];
    int k = 0;
    for (int i = 0; i < 8; ++i) {
      const FlipLog& L = flip_log[(flip_log_pos + i) % 8];
      if (L.t_ms == 0) continue;
      k += snprintf(lb + k, sizeof(lb) - k, " %lu:fb%u n=%u y%d-%d", (unsigned long)L.t_ms, L.idx, L.n, L.y1, L.y2);
      if (k >= (int)sizeof(lb) - 1) break;
    }
    console_printf("DIAG RGBFLIPS%s\n", lb);
  }
  console_printf("DIAG RGBPINS order_ok=%d (gpio:data_bit) %s pclk_sel=%u de_sel=%u\n", order, buf,
                 (unsigned)(GPIO.func_out_sel_cfg[kPinPclk].val & 0x1FF), (unsigned)(GPIO.func_out_sel_cfg[kPinDe].val & 0x1FF));
}
