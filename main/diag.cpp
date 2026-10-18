#include "esp_attr.h"
#include "diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "esp_heap_caps.h"
#include "lwip/sockets.h"
#include "net.h"
#include "nvs.h"
#include <fcntl.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "panel_hw.h"
#include "panel_ui.h"
#include "ota_check.h"
#include "sys.h"
#include "test_harness.h"

namespace {
std::atomic<uint32_t> t_reads{0}, t_err{0}, t_fresh{0}, t_pressed{0};
std::atomic<uint32_t> ui_loops{0}, ui_lv_max{0}, ui_poll_max{0}, ui_loop_max{0}, ui_slow{0};
std::atomic<uint32_t> interactive_ms{0};

// Injection (diag task writes, UI task reads).
std::atomic<bool> inj_active{false};  // set last (after the params) by the diag task, read by the UI task
volatile bool inj_raw = false;
volatile int inj_x0, inj_y0, inj_x1, inj_y1;
volatile uint32_t inj_t0, inj_dur, inj_hold;

void stats() {
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  EXT_RAM_BSS_ATTR static char buf[1536];
  vTaskGetRunTimeStats(buf);
  console_puts("DIAG tasks (name, abs time, % since boot):");
  console_write(buf, strlen(buf));
#endif
}

const char* const TAG = "diag";
// Warn once a task has less stack headroom than this (bytes; ESP-IDF high-water
// marks are in bytes) or internal RAM drops below kHeapWarn.
constexpr uint32_t kStackWarn = 768;
constexpr size_t kHeapWarn = 16 * 1024;

// Stack high-water marks of all tasks plus heap minima. warn_only: log only
// tasks/heap below the thresholds (periodic check), else print everything.
void stacks(bool warn_only) {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
  static TaskStatus_t st[32];
  static uint32_t warned = 0;  // bit per slot index of tasks already warned about (by name hash)
  UBaseType_t n = uxTaskGetSystemState(st, 32, nullptr);
  char line[480];
  size_t used = snprintf(line, sizeof(line), "DIAG stack free(B):");
  for (UBaseType_t i = 0; i < n; ++i) {
    uint32_t free_b = st[i].usStackHighWaterMark;
    // Only our own tasks: IDF system tasks (ipc, IDLE) run with small fixed stacks by design.
    const char* nm = st[i].pcTaskName;
    bool ours = !strcmp(nm, "ui") || !strcmp(nm, "net") || !strcmp(nm, "lox") || !strcmp(nm, "diag") || !strcmp(nm, "restart") ||
                !strcmp(nm, "httpd");
    if (ours && free_b < kStackWarn) {
      uint32_t bit = 1u << ((st[i].pcTaskName[0] + st[i].pcTaskName[1]) & 31);
      if (!(warned & bit)) {
        warned |= bit;
        ESP_LOGW(TAG, "low stack: task %s only %lu bytes free", st[i].pcTaskName, (unsigned long)free_b);
      }
    }
    if (!warn_only && used < sizeof(line) - 24)
      used += snprintf(line + used, sizeof(line) - used, " %s=%lu", st[i].pcTaskName, (unsigned long)free_b);
  }
  size_t int_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  static bool heap_warned = false;
  if (int_min < kHeapWarn && !heap_warned) {
    heap_warned = true;
    ESP_LOGW(TAG, "internal RAM minimum %u bytes", (unsigned)int_min);
  }
  if (!warn_only) {
    console_puts(line);
    console_printf("DIAG heap int free=%uKB min=%uKB block=%uKB | psram free=%uKB min=%uKB\n",
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), (unsigned)(int_min / 1024),
                   (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                   (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM) / 1024));
    // DMA-capable internal, internal minimum with the action that caused it,
    // open sockets, NVS usage, LVGL (system heap) attribution.
    heap_note("other");
    int socks = 0;
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; ++fd)
      if (fcntl(fd, F_GETFL, 0) >= 0) socks++;
    nvs_stats_t ns = {};
    nvs_get_stats(nullptr, &ns);
    size_t ns_used = 0;
    nvs_handle_t h;
    if (nvs_open("panel", NVS_READONLY, &h) == ESP_OK) {
      nvs_get_used_entry_count(h, &ns_used);
      nvs_close(h);
    }
    console_printf("DIAG mem dma free=%uKB block=%uKB | int min=%uB during %s | sockets=%d/%d | "
                   "nvs used=%u free=%u total=%u ns=%u panel=%u | lvgl ui int=%uKB psram=%uKB\n",
                   (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024),
                   (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024),
                   (unsigned)int_min, heap_min_label(), socks, CONFIG_LWIP_MAX_SOCKETS, (unsigned)ns.used_entries,
                   (unsigned)ns.free_entries, (unsigned)ns.total_entries, (unsigned)ns.namespace_count,
                   (unsigned)ns_used, (unsigned)(g_ui_build_int / 1024), (unsigned)(g_ui_build_psram / 1024));
  }
#endif
}

bool read_only(const char* line) {
  return strcmp(line, "DIAGGT") == 0 || strcmp(line, "DIAGFB") == 0 || strcmp(line, "DIAGSTATS") == 0 ||
         strcmp(line, "DIAGSTACK") == 0;
}

void handle(const char* line) {
  int a, b, c, d, e;
#if !CONFIG_PANEL_DIAG_INJECT && !CONFIG_PANEL_TEST_HARNESS
  if (!read_only(line)) {
    if (strncmp(line, "DIAG", 4) == 0) console_puts("DIAG ERR read-only build (CONFIG_PANEL_DIAG_INJECT)");
    return;
  }
#endif
  if (sscanf(line, "DIAGRAW %d %d", &a, &b) == 2) {
    inj_x0 = a; inj_x1 = a; inj_y0 = b; inj_y1 = b; inj_dur = 120; inj_hold = 0;
    inj_raw = true;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
    return;
  }
  inj_raw = false;
  if (sscanf(line, "DIAGSWIPE %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
    inj_x0 = a; inj_y0 = b; inj_x1 = c; inj_y1 = d; inj_dur = e < 20 ? 20 : e; inj_hold = 0;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
  } else if (sscanf(line, "DIAGTAP %d %d", &a, &b) == 2) {
    inj_x0 = a; inj_x1 = a; inj_y0 = b; inj_y1 = b; inj_dur = 120; inj_hold = 0;
    inj_t0 = now_ms();
    inj_active = true;
    console_puts("DIAG OK");
  } else if (strcmp(line, "DIAGGT") == 0) {
    uint8_t id[14] = {}, cfg[16] = {}, cmd = 0xFF, st = 0xFF;
    bool a1 = panel_gt911_read(0x8140, id, sizeof(id));
    bool a2 = panel_gt911_read(0x8047, cfg, sizeof(cfg));
    bool a3 = panel_gt911_read(0x8040, &cmd, 1);
    bool a4 = panel_gt911_read(0x814E, &st, 1);
    console_printf("DIAG GT911 ok=%d%d%d%d product=%c%c%c%c fw=0x%02X%02X xres=%u yres=%u vendor=0x%02X\n", a1, a2, a3, a4,
                   id[0], id[1], id[2], id[3], id[5], id[4], id[6] | (id[7] << 8), id[8] | (id[9] << 8), id[10]);
    console_printf("DIAG GT911 cfg_ver=0x%02X xmax=%u ymax=%u touches=%u sw1=0x%02X sw2=0x%02X shake=0x%02X filter=0x%02X "
                   "large=0x%02X noise=0x%02X cmd=0x%02X status=0x%02X\n", cfg[0], cfg[1] | (cfg[2] << 8),
                   cfg[3] | (cfg[4] << 8), cfg[5], cfg[6], cfg[7], cfg[8], cfg[9], cfg[10], cfg[11], cmd, st);
  } else if (sscanf(line, "DIAGSCRTO %d", &a) == 1) {
    ui_set_screen_timeout(static_cast<uint16_t>(a));  // this boot only, not saved
    console_puts("DIAG OK");
  } else if (sscanf(line, "DIAGSAVER %d", &a) == 1 && a >= 0 && a <= 2) {
    ui_saver_runtime(a);  // this boot only: the saved setting is not touched
    console_puts("DIAG OK");
  } else if (strncmp(line, "DIAGVER", 7) == 0) {
    // Pretend to run another version for the update check (this boot only).
    const char* v = line[7] == ' ' ? line + 8 : "";
    ota_set_version_override(v);
    console_printf("DIAG version for update check: %s\n", ota_running_version());
  } else if (strcmp(line, "DIAGFB") == 0) {
    // What is really in the panel framebuffer (not an LVGL snapshot).
    uint32_t black = 0, white = 0, red = 0, other = 0;
    panel_fb_stats(&black, &white, &red, &other);
    char ui[160], bl[96];
    ui_saver_describe(ui, sizeof(ui));
    panel_bl_describe(bl, sizeof(bl));
    console_printf("DIAG FB black=%lu white=%lu red=%lu other=%lu | %s | %s\n", (unsigned long)black, (unsigned long)white,
                   (unsigned long)red, (unsigned long)other, ui, bl);
  } else if (sscanf(line, "DIAGBL %d", &a) == 1 && a >= 0 && a <= 100) {
    panel_backlight(static_cast<uint8_t>(a), 0);  // live brightness test (next screen on/off resets it)
    console_puts("DIAG OK");
  } else if (strcmp(line, "DIAGSTATS") == 0) {
    stats();
  } else if (strcmp(line, "DIAGSTACK") == 0) {
    stacks(false);
  }
}

void report(uint32_t t) {
  console_printf("DIAG t=%lus touch reads=%lu i2c_err=%lu reports=%lu pressed=%lu | ui loops=%lu lv_max=%lums "
                 "poll_max=%lums loop_max=%lums slow(>100ms)=%lu | int=%uKB psram=%uKB | interactive=%lums\n",
                 (unsigned long)t, (unsigned long)t_reads.load(), (unsigned long)t_err.load(), (unsigned long)t_fresh.load(),
                 (unsigned long)t_pressed.load(), (unsigned long)ui_loops.load(), (unsigned long)(ui_lv_max.load() / 1000),
                 (unsigned long)(ui_poll_max.load() / 1000), (unsigned long)(ui_loop_max.load() / 1000), (unsigned long)ui_slow.load(),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), (unsigned long)interactive_ms);
  ui_lv_max = ui_poll_max = ui_loop_max = 0;
}

void diag_task(void*) {
  char line[64];
  size_t len = 0;
  uint32_t start = now_ms(), last = start;
  bool stats30 = false, stats90 = false;
  for (;;) {
#ifndef CONFIG_PANEL_TEST_HARNESS
    // The release image has no harness: read the console here.
    while (console_available() > 0) {
      int ch = console_read();
      if (ch == '\r') continue;
      if (ch == '\n') {
        line[len] = '\0';
        handle(line);
        len = 0;
      } else if (len < sizeof(line) - 1) {
        line[len++] = static_cast<char>(ch);
      }
    }
#endif
    uint32_t now = now_ms();
    uint32_t up = (now - start) / 1000;
    uint32_t every = up < 120 ? 5000 : 60000;
    if (now - last >= every) {
      last = now;
      report(up);
      stacks(every == 5000);  // full table once a minute, warnings always
    }
    if (!stats30 && up >= 30) { stats30 = true; stats(); stacks(false); }
    if (!stats90 && up >= 90) { stats90 = true; stats(); }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}  // namespace

void diag_command(const char* line) { handle(line); }

void diag_start() { xTaskCreatePinnedToCore(diag_task, "diag", 4096, nullptr, 2, nullptr, 0); }

void diag_touch_poll(bool i2c_ok, bool fresh, bool pressed) {
  ++t_reads;
  if (!i2c_ok) ++t_err;
  if (fresh) ++t_fresh;
  if (pressed) ++t_pressed;
}

bool diag_touch_inject(int* x, int* y, bool* pressed, bool* raw) {
  if (!inj_active) return false;
  *raw = inj_raw;
  uint32_t el = now_ms() - inj_t0;
  if (el <= inj_dur) {
    *x = inj_x0 + (inj_x1 - inj_x0) * (int)el / (int)inj_dur;
    *y = inj_y0 + (inj_y1 - inj_y0) * (int)el / (int)inj_dur;
    *pressed = true;
  } else {
    *x = inj_x1;
    *y = inj_y1;
    *pressed = false;
    if (el > inj_dur + 60) inj_active = false;
  }
  return true;
}

void diag_ui_loop(uint32_t lv_us, uint32_t poll_us, uint32_t loop_us) {
  ++ui_loops;
  if (lv_us > ui_lv_max) ui_lv_max = lv_us;
  if (poll_us > ui_poll_max) ui_poll_max = poll_us;
  if (loop_us > ui_loop_max) ui_loop_max = loop_us;
  if (loop_us > 100000) ++ui_slow;
}

void diag_ui_first_interactive() {
  if (interactive_ms == 0) interactive_ms = now_ms();
}
