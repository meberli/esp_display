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

static const char* const TAG = "panel_hw";

#define GT911_ADDR1 0x5D
#define GT911_ADDR2 0x14

namespace {

// ---------------------------------------------------------------------------
// ST7701S over 3-wire SPI (9-bit words: D/C bit, then 8 bits MSB first).
// Bit-banged like Arduino_SWSPI did; it is only used for the init sequence.

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

void st_cmd(uint8_t cmd, const uint8_t* data, size_t n) {
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 0);
  spi_word(false, cmd);
  for (size_t i = 0; i < n; ++i) spi_word(true, data[i]);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 1);
}

// Arduino_GFX st7701_type9_init_operations (Guition ESP32-4848S040), as
// {command, length, data...}. 0xFF length marks a delay in ms.
const uint8_t kInit[] = {
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    0xC0, 2, 0x3B, 0x00,
    0xC1, 2, 0x0D, 0x02,
    0xC2, 2, 0x31, 0x05,
    0xCD, 1, 0x00,
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
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x13,
    0xE5, 1, 0xE4,
    0xFF, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    0x3A, 1, 0x60,
    0x11, 0,
    0x00, 0xFF, 120,  // delay
    0x29, 0,
};

void st7701_init() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ULL << kPinLcdCs) | (1ULL << kPinLcdSck) | (1ULL << kPinLcdMosi);
  io.mode = GPIO_MODE_OUTPUT;
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdCs), 1);
  gpio_set_level(static_cast<gpio_num_t>(kPinLcdSck), 0);
  // No reset line: software reset first, like Arduino_RGB_Display::begin.
  st_cmd(0x01, nullptr, 0);
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
}

// ---------------------------------------------------------------------------
// RGB panel. Timings from the Arduino_GFX ESP32_4848S040_86BOX_GUITION entry.

esp_lcd_panel_handle_t rgb = nullptr;
uint16_t* fb = nullptr;

bool rgb_begin() {
  esp_lcd_rgb_panel_config_t c = {};
  c.clk_src = LCD_CLK_SRC_DEFAULT;
  c.timings.pclk_hz = 12 * 1000 * 1000;
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
  c.num_fbs = 1;
  c.bounce_buffer_size_px = kPanelWidth * 10;  // 2 x 10 lines in internal DRAM
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
// GT911 on the new I2C master driver. INT/RST are not wired on this board.

i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t gt911 = nullptr;
bool touch_ok = false;
bool last_pressed = false;
int last_x = 0;
int last_y = 0;

bool gt_read(uint16_t reg, uint8_t* out, size_t n) {
  uint8_t r[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
  return i2c_master_transmit_receive(gt911, r, 2, out, n, 50) == ESP_OK;
}

bool gt_write8(uint16_t reg, uint8_t v) {
  uint8_t w[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), v};
  return i2c_master_transmit(gt911, w, 3, 50) == ESP_OK;
}

// GT911 raw (480x480) -> logical point. Same mapping as TAMC_GT911
// ROTATION_RIGHT (display rotation 1).
void gt_map(int raw_x, int raw_y) {
  bool was = last_pressed;
  last_x = raw_y;
  last_y = kPanelHeight - raw_x;
  last_pressed = true;
  if (!was) console_printf("GT911 press raw %d,%d -> %d,%d\n", raw_x, raw_y, last_x, last_y);
}

void gt_poll() {
  uint8_t status = 0;
  bool ok = gt_read(0x814E, &status, 1);
  diag_touch_poll(ok, ok && (status & 0x80) != 0, ok && (status & 0x80) != 0 && (status & 0x0F) != 0);
  int ix, iy;
  bool ip, iraw;
  if (diag_touch_inject(&ix, &iy, &ip, &iraw)) {  // driver-level injection (diag.cpp)
    if (ip && iraw) {
      gt_map(ix, iy);  // same decode path as a real GT911 report
    } else if (ip) {
      last_x = ix;
      last_y = iy;
    }
    last_pressed = ip;
    if (ok && (status & 0x80) != 0) gt_write8(0x814E, 0);
    return;
  }
  if (!ok) return;
  if ((status & 0x80) == 0) return;  // no new report: keep the last state
  int n = status & 0x0F;
  if (n > 0 && n <= 5) {
    // Point 1 record at 0x814F: track id, x lo/hi, y lo/hi, size lo/hi, reserved.
    // (v0.7.0/0.7.1 read from 0x8150, one byte late: x = x_hi | y_lo << 8, so
    // every real touch was clamped to the corner 479,0. Simulated touches
    // bypassed this decode, so the tests never saw it.)
    uint8_t p[8];
    if (gt_read(0x814F, p, sizeof(p))) {
      int raw_x = p[1] | (p[2] << 8);
      int raw_y = p[3] | (p[4] << 8);
      gt_map(raw_x, raw_y);
    }
  } else {
    last_pressed = false;
  }
  gt_write8(0x814E, 0);
}

constexpr uint32_t kBlMax = (1u << 10) - 1;
constexpr uint32_t kBlPwmHz = 150;
uint8_t bl_level = 0;

lv_disp_draw_buf_t draw_buf;
lv_disp_drv_t disp_drv;
lv_indev_drv_t indev_drv;

// Rotation 1 as Arduino_GFX did it: logical (x, y) -> framebuffer row x,
// column W-1-y. Then write the touched rows back from cache for the DMA.
void panel_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color) {
  const int32_t w = area->x2 - area->x1 + 1;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(color);
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    const uint16_t* row = src + (y - area->y1) * w;
    uint16_t* dst = fb + static_cast<size_t>(area->x1) * kPanelWidth + (kPanelWidth - 1 - y);
    for (int32_t x = 0; x < w; ++x) {
      *dst = row[x];
      dst += kPanelWidth;
    }
  }
  esp_cache_msync(fb + static_cast<size_t>(area->x1) * kPanelWidth, static_cast<size_t>(w) * kPanelWidth * 2,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  lv_disp_flush_ready(drv);
}

void panel_touch_read_raw(lv_indev_data_t* data) {
  if (harness_touch_override(data)) return;
  if (!touch_ok) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  gt_poll();
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

// Release debounce: the controller sometimes reports one empty sample in the
// middle of a drag. That split one scroll into two touches, and the short
// second one was taken as a tap. A release counts only after kRelSamples
// empty reads in a row; until then the last point is held.
constexpr int kRelSamples = 2;
int rel_n = 0;
bool held = false;
lv_coord_t held_x = 0, held_y = 0;

void panel_touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
  panel_touch_read_raw(data);
  if (data->state == LV_INDEV_STATE_PRESSED) {
    rel_n = 0;
    held = true;
    held_x = data->point.x;
    held_y = data->point.y;
  } else if (held && ++rel_n < kRelSamples) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = held_x;
    data->point.y = held_y;
  } else {
    held = false;
  }
  // Screen off: the waking touch is swallowed until release (no tap, no swipe).
  if (!ui_power_touch(data->state == LV_INDEV_STATE_PRESSED, data->point.x, data->point.y)) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  ui_touch_sample(data->state == LV_INDEV_STATE_PRESSED, data->point.x, data->point.y);
}

}  // namespace

bool panel_display_begin() {
  st7701_init();
  if (!rgb_begin()) return false;
  // Backlight GPIO38 on LEDC PWM (10 bit) so it can dim and fade. GPIO38 is
  // the EN pin of the AP3031 LED boost driver: at 20 kHz (up to v0.7.3) every
  // level below roughly 85 % left the backlight off, so the clock savers (40 %)
  // and the dim hint (4 %) were rendered but black. 150 Hz is the rate the
  // ESPHome configs for this board use for working dim levels.
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
  uint8_t found = 0;
  if (i2c_master_probe(i2c_bus, GT911_ADDR1, 50) == ESP_OK) {
    found = GT911_ADDR1;
  } else if (i2c_master_probe(i2c_bus, GT911_ADDR2, 50) == ESP_OK) {
    found = GT911_ADDR2;
  }
  if (found == 0) {
    console_puts("GT911 not found at 0x5D or 0x14");
    return 0;
  }
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = found;
  dc.scl_speed_hz = 100000;
  if (i2c_master_bus_add_device(i2c_bus, &dc, &gt911) != ESP_OK) return 0;
  // The GT911 keeps its factory configuration (TAMC rewrote it; not needed).
  touch_ok = true;
  console_printf("GT911 at 0x%02X\n", found);
  return found;
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

bool panel_gt911_read(uint16_t reg, uint8_t* out, size_t n) { return touch_ok && gt_read(reg, out, n); }

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
  indev_drv.scroll_limit = 14;  // > kTapSlop: a tap never starts a scroll
  indev_drv.gesture_limit = 40;
  lv_indev_drv_register(&indev_drv);
  return true;
}

void panel_relay_begin() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPinRelay1;
  io.mode = GPIO_MODE_INPUT_OUTPUT;  // readable back for panel_relay_is_on()
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinRelay1), kRelayOffLevel);
}

bool panel_relay_is_on() { return gpio_get_level(static_cast<gpio_num_t>(kPinRelay1)) == kRelayOnLevel; }

void panel_relay_set(bool on) {
  gpio_set_level(static_cast<gpio_num_t>(kPinRelay1), on ? kRelayOnLevel : kRelayOffLevel);
}

void panel_hw_dump() {}
