// Panel driver for the Panlee ZX3D95CE01S-TR (GC9503V RGB + FT6336 touch).
// Same API as panel_hw.cpp (Guition); pins in board_pins_panlee.h.
#include "panel_hw.h"

#include <string.h>

#include "sys.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "fonts.h"
#include "lvgl.h"
#include "panel_ui.h"
#include "esp_rom_sys.h"
#include "test_harness.h"
#include "diag.h"
#include <esp_log.h>
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"

static const char* const TAG = "panel_hw";

namespace {

// ---------------------------------------------------------------------------
// GC9503V over 3-wire SPI (9-bit words: D/C bit, then 8 bits MSB first).
// SCLK/MOSI are RGB D0/D1 (GPIO45/48): they are switched to plain GPIO for the
// init and handed back to the LCD peripheral afterwards (LGFX does the same).

void spi_word(bool data, uint8_t v) {
  uint16_t w = (data ? 0x100 : 0) | v;
  for (int bit = 8; bit >= 0; --bit) {
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdMosi), (w >> bit) & 1);
    esp_rom_delay_us(1);
    gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 1);
    esp_rom_delay_us(1);
  }
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
}

void gc_cmd(uint8_t cmd, const uint8_t* data, size_t n) {
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 0);
  spi_word(false, cmd);
  for (size_t i = 0; i < n; ++i) spi_word(true, data[i]);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 1);
}

#define GC_GAMMA                                                                                              \
  52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x12, 0x00, 0x18, 0x00, 0x21, 0x00, 0x2a, 0x00, 0x35, 0x00, 0x47, 0x00,   \
      0x56, 0x00, 0x90, 0x00, 0xe5, 0x01, 0x68, 0x01, 0xd5, 0x01, 0xd7, 0x02, 0x36, 0x02, 0xa6, 0x02, 0xee,   \
      0x03, 0x48, 0x03, 0xa0, 0x03, 0xba, 0x03, 0xc5, 0x03, 0xd0, 0x03, 0xE0, 0x03, 0xea, 0x03, 0xFa, 0x03,   \
      0xFF

// LovyanGFX Panel_GC9503::getInitCommands (used by ha_deck on WT32S3-86S),
// as {command, length, data...}. Length 0xFF = delay in ms (next byte).
const uint8_t kInit[] = {
    0xF0, 5, 0x55, 0xAA, 0x52, 0x08, 0x00,
    0xF6, 2, 0x5A, 0x87,
    0xC1, 1, 0x3F,
    0xC2, 1, 0x0E,
    0xC6, 1, 0xF8,
    0xC9, 1, 0x10,
    0xCD, 1, 0x25,
    0xF8, 1, 0x8A,
    0xAC, 1, 0x45,
    0xA0, 1, 0xDD,
    0xA7, 1, 0x47,
    0xFA, 4, 0x00, 0x00, 0x00, 0x04,
    0xA3, 1, 0xEE,
    0xFD, 3, 0x28, 0x28, 0x00,
    0x71, 1, 0x48,
    0x72, 1, 0x48,
    0x73, 2, 0x00, 0x44,
    0x97, 1, 0xEE,
    0x83, 1, 0x93,
    0x9A, 1, 0x72,
    0x9B, 1, 0x5a,
    0x82, 2, 0x2c, 0x2c,
    0xB1, 1, 0x10,
    0x6D, 32, 0x00, 0x1F, 0x19, 0x1A, 0x10, 0x0e, 0x0c, 0x0a, 0x02, 0x07, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
              0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x08, 0x01, 0x09, 0x0b, 0x0D, 0x0F, 0x1a, 0x19, 0x1f, 0x00,
    0x64, 16, 0x38, 0x05, 0x01, 0xdb, 0x03, 0x03, 0x38, 0x04, 0x01, 0xdc, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A,
    0x65, 16, 0x38, 0x03, 0x01, 0xdd, 0x03, 0x03, 0x38, 0x02, 0x01, 0xde, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A,
    0x66, 16, 0x38, 0x01, 0x01, 0xdf, 0x03, 0x03, 0x38, 0x00, 0x01, 0xe0, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A,
    0x67, 16, 0x30, 0x01, 0x01, 0xe1, 0x03, 0x03, 0x30, 0x02, 0x01, 0xe2, 0x03, 0x03, 0x7A, 0x7A, 0x7A, 0x7A,
    0x68, 13, 0x00, 0x08, 0x15, 0x08, 0x15, 0x7A, 0x7A, 0x08, 0x15, 0x08, 0x15, 0x7A, 0x7A,
    0x60, 8, 0x38, 0x08, 0x7A, 0x7A, 0x38, 0x09, 0x7A, 0x7A,
    0x63, 8, 0x31, 0xe4, 0x7A, 0x7A, 0x31, 0xe5, 0x7A, 0x7A,
    0x6B, 1, 0x07,
    0x7A, 2, 0x08, 0x13,
    0x7B, 2, 0x08, 0x13,
    0xD1, GC_GAMMA,
    0xD2, GC_GAMMA,
    0xD3, GC_GAMMA,
    0xD4, GC_GAMMA,
    0xD5, GC_GAMMA,
    0xD6, GC_GAMMA,
    0x3A, 1, 0x66,
    0x11, 0,
    0x00, 0xFF, 200,  // delay after sleep out
    0x29, 0,
};

void gc9503_init() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << kPinLcdCs) | (1ULL << kPinLcdSck) | (1ULL << kPinLcdMosi);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  esp_rom_gpio_connect_out_signal(kPinLcdSck, SIG_GPIO_OUT_IDX, false, false);
  esp_rom_gpio_connect_out_signal(kPinLcdMosi, SIG_GPIO_OUT_IDX, false, false);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 1);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdMosi), 0);
  size_t i = 0;
  while (i < sizeof(kInit)) {
    uint8_t cmd = kInit[i];
    uint8_t len = kInit[i + 1];
    if (len == 0xFF) {
      sleep_ms(kInit[i + 2]);
      i += 3;
      continue;
    }
    gc_cmd(cmd, &kInit[i + 2], len);
    i += 2 + len;
  }
  // Hand D0/D1 back to the LCD_CAM RGB peripheral (data_gpio_nums[0] / [1]).
  static_assert(kPinLcdSck == kPinB0 && kPinLcdMosi == kPinB1, "init bus must be RGB D0/D1");
  esp_rom_gpio_connect_out_signal(kPinLcdSck, LCD_DATA_OUT0_IDX, false, false);
  esp_rom_gpio_connect_out_signal(kPinLcdMosi, LCD_DATA_OUT1_IDX, false, false);
}

// ---------------------------------------------------------------------------
// RGB panel. Porches/polarity from ha_deck LGFX.hpp (hsync fp 8 / pw 10 /
// bp 50, vsync fp 8 / pw 10 / bp 20, idle high). PCLK 12 MHz like the Guition
// build (LGFX runs 15 MHz) to keep PSRAM bandwidth headroom.

esp_lcd_panel_handle_t rgb = nullptr;
uint16_t* fb = nullptr;

bool rgb_begin() {
  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 12 * 1000 * 1000;
  c.timings.h_res = kPanelWidth;
  c.timings.v_res = kPanelHeight;
  c.timings.hsync_pulse_width = 10;
  c.timings.hsync_back_porch = 50;
  c.timings.hsync_front_porch = 8;
  c.timings.vsync_pulse_width = 10;
  c.timings.vsync_back_porch = 20;
  c.timings.vsync_front_porch = 8;
  c.timings.flags.hsync_idle_low = 0;
  c.timings.flags.vsync_idle_low = 0;
  c.timings.flags.de_idle_high = 0;
  c.timings.flags.pclk_active_neg = 0;
  c.timings.flags.pclk_idle_high = 0;
  c.data_width = 16;
  c.in_color_format = LCD_COLOR_FMT_RGB565;
  c.out_color_format = LCD_COLOR_FMT_RGB565;
  c.num_fbs = 1;
  c.bounce_buffer_size_px = kPanelWidth * 20;  // 2 x 20 lines x 480 x 2 B = 38 KB internal DMA
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
  if (esp_lcd_panel_reset(rgb) != ESP_OK || esp_lcd_panel_init(rgb) != ESP_OK) return false;
  void* p = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(rgb, 1, &p) != ESP_OK || p == nullptr) return false;
  fb = static_cast<uint16_t*>(p);
  memset(fb, 0, kPanelWidth * kPanelHeight * 2);
  esp_cache_msync(fb, kPanelWidth * kPanelHeight * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  return true;
}

// ---------------------------------------------------------------------------
// FT6336 (FT5x06 register set) at 0x38. Polled; INT (GPIO4) unused.
// Raw coordinates are used as-is (LGFX offset_rotation 0); mirroring (S).

i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t ft = nullptr;
uint8_t ft_addr = 0;
bool sht_present = false;
bool touch_ok = false;
bool last_pressed = false;
int last_x = 0;
int last_y = 0;

bool ft_read(uint8_t reg, uint8_t* out, size_t n) {
  return i2c_master_transmit_receive(ft, &reg, 1, out, n, 50) == ESP_OK;
}

void ft_map(int raw_x, int raw_y) {
  bool was = last_pressed;
  last_x = raw_x;
  last_y = raw_y;
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
    ft_map(((p[1] & 0x0F) << 8) | p[2], ((p[3] & 0x0F) << 8) | p[4]);
  } else {
    last_pressed = false;
  }
}

constexpr uint32_t kBlMax = (1u << 10) - 1;
constexpr uint32_t kBlPwmHz = 1000;  // LGFX Light_PWM default on this board
uint8_t bl_level = 0;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

// Rotation 0: logical (x, y) = framebuffer (x, y). The panel orientation on
// the wall is not verified yet (S); rotate here if the UI comes out turned.
void panel_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color) {
  const int32_t w = area->x2 - area->x1 + 1;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(color);
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    memcpy(fb + static_cast<size_t>(y) * kPanelWidth + area->x1, src + (y - area->y1) * w, static_cast<size_t>(w) * 2);
  }
  esp_cache_msync(fb + static_cast<size_t>(area->y1) * kPanelWidth,
                  static_cast<size_t>(area->y2 - area->y1 + 1) * kPanelWidth * 2,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
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
  // "Reset": the GC9503V reset is an RC on VSYNC; pulse it like LGFX pin_rst.
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPinLcdRstVsync;
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdRstVsync), 0);
  sleep_ms(10);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdRstVsync), 1);
  sleep_ms(120);
  if (!rgb_begin()) return false;
  gc9503_init();  // after the RGB bus runs, as LGFX Panel_GC9503::init does
  // Backlight GPIO5 on LEDC PWM (10 bit).
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
  if (i2c_bus == nullptr) {
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = static_cast<gpio_num_t>(kPinTpSda);
    bc.scl_io_num = static_cast<gpio_num_t>(kPinTpScl);
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bc, &i2c_bus) != ESP_OK) {
      ESP_LOGE(TAG, "I2C bus init failed");
      return 0;
    }
  }
  sht_present = i2c_master_probe(i2c_bus, kShtAddr, 50) == ESP_OK;
  console_printf("SHT20 at 0x%02X: %s\n", kShtAddr, sht_present ? "present" : "not found");
  const uint8_t addrs[] = {0x38, 0x48};
  for (uint8_t a : addrs) {
    if (i2c_master_probe(i2c_bus, a, 50) == ESP_OK) {
      ft_addr = a;
      break;
    }
  }
  if (ft_addr == 0) {
    console_puts("FT6336 not found at 0x38 or 0x48");
    return 0;
  }
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = ft_addr;
  dc.scl_speed_hz = 400000;
  if (i2c_master_bus_add_device(i2c_bus, &dc, &ft) != ESP_OK) return 0;
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

bool panel_gt911_read(uint16_t, uint8_t*, size_t) { return false; }  // no GT911 on this board

bool panel_lvgl_start() {
  lv_init();
  // One PSRAM draw buffer on purpose: panel_flush rotates synchronously into
  // the RGB framebuffer, so a second buffer would not overlap rendering and
  // flushing, and 2 x 38 KB internal RAM is not available (DIAGSTACK minimum
  // ~18 KB). Revisit together with an async (DMA/PPA) flush.
  constexpr uint32_t kLines = 40;
  uint32_t pixels = static_cast<uint32_t>(kPanelWidth) * kLines;
  lv_color_t* buf =
      static_cast<lv_color_t*>(heap_caps_malloc(pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return false;
  lv_disp_draw_buf_init(&draw_buf, buf, nullptr, pixels);
  lv_disp_drv_init(&disp_drv);
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

// No relay on this board.
void panel_relay_begin() {}
bool panel_relay_is_on() { return false; }
void panel_relay_set(bool) {}

void panel_hw_dump() {
  uint8_t id[4] = {};
  bool a = touch_ok && ft_read(0xA3, id, 1) && ft_read(0xA6, id + 1, 1) && ft_read(0xA8, id + 2, 1) &&
           ft_read(0xA1, id + 3, 1);
  console_printf("DIAGHW panlee ft=0x%02X ok=%d chip=%02X fw=%02X vendor=%02X lib=%02X sht20=%d\n", ft_addr, a, id[0],
                 id[1], id[2], id[3], sht_present);
}
