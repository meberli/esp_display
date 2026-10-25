#include "espnow_play.h"
#include "energy_task.h"
#include "pv_task.h"
#include <time.h>
#include "sdkconfig.h"
#ifdef CONFIG_PANEL_TEST_HARNESS

#include "test_harness.h"
#include "settings.h"
#include "net.h"
#include "ota_check.h"
#include <string.h>

#include "sys.h"
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>
#include <esp_log.h>
#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "panel_hw.h"
#include "maint.h"
#include "panel_ui.h"
#include "diag.h"
#include <esp_image_format.h>
#include <esp_ota_ops.h>

bool app_test_net_busy(uint32_t ms);  // main.cpp

namespace {

enum class Sim { Idle, Hold, Move, ReleaseHold };

Sim sim = Sim::Idle;
int x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
uint32_t t0 = 0, dur = 0;
int cur_x = 0, cur_y = 0;
bool auto_release = false;

char line[320];  // long enough for TYPE <update URL>
size_t line_len = 0;

int clampc(int v, int max) { return v < 0 ? 0 : (v >= max ? max - 1 : v); }

void start_move(int ax, int ay, int bx, int by, uint32_t ms, bool release) {
  x0_ = clampc(ax, kPanelWidth);
  y0_ = clampc(ay, kPanelHeight);
  x1_ = clampc(bx, kPanelWidth);
  y1_ = clampc(by, kPanelHeight);
  cur_x = x0_;
  cur_y = y0_;
  dur = ms == 0 ? 1 : ms;
  t0 = now_ms();
  auto_release = release;
  sim = Sim::Move;
}

// Screenshot transfer. At 921600 the CH340 link drops short runs of bytes;
// 460800 (the default here) measured clean. To stay safe at any speed, the
// image goes out as base64 text lines, each with its own CRC32, and the host
// asks again for any line it lost:
//   SHOT            -> SHOT_BEGIN <w> <h> <len> <line_bytes>
//                      L <offset> <crc32 hex> <base64>   (one per line_bytes)
//                      SHOT_END
//   SHOTR <off> <n> -> the same L lines for that range, then SHOTR_END
//   SHOT TEST       -> byte pattern i & 0xFF instead of the screen
// The last capture stays in PSRAM until the next SHOT.
constexpr uint32_t kLineBytes = 192;
uint8_t* shot_buf = nullptr;
uint32_t shot_len = 0;

void send_line(uint32_t off) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t n = shot_len - off > kLineBytes ? kLineBytes : shot_len - off;
  const uint8_t* d = shot_buf + off;
  char out[24 + kLineBytes * 4 / 3 + 8];
  int pos = snprintf(out, 24, "L %u %08x ", static_cast<unsigned>(off),
                     static_cast<unsigned>(esp_rom_crc32_le(0, d, n)));
  for (uint32_t i = 0; i < n; i += 3) {
    uint32_t v = d[i] << 16;
    if (i + 1 < n) v |= d[i + 1] << 8;
    if (i + 2 < n) v |= d[i + 2];
    out[pos++] = tbl[(v >> 18) & 63];
    out[pos++] = tbl[(v >> 12) & 63];
    out[pos++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
    out[pos++] = i + 2 < n ? tbl[v & 63] : '=';
  }
  out[pos++] = '\n';
  const uint8_t* p = reinterpret_cast<const uint8_t*>(out);
  size_t left = pos;
  while (left > 0) {  // write() can return short when the TX ring is full
    size_t w = console_write(p, left);
    p += w;
    left -= w;
    if (w == 0) sleep_ms(1);
  }
}

void send_range(uint32_t off, uint32_t n) {
  if (shot_buf == nullptr) return;
  off -= off % kLineBytes;
  uint32_t end = off + n > shot_len ? shot_len : off + n;
  for (; off < end; off += kLineBytes) send_line(off);
}

void shot(bool test_pattern) {
  lv_obj_t* scr = ui_overlay() != nullptr ? ui_overlay() : lv_scr_act();  // off: the black layer
  uint32_t size = lv_snapshot_buf_size_needed(scr, LV_IMG_CF_TRUE_COLOR);
  if (shot_buf == nullptr) {
    shot_buf = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (shot_buf == nullptr) {
      console_puts("ERR nomem");
      return;
    }
  }
  lv_img_dsc_t dsc;
  if (test_pattern) {
    for (uint32_t i = 0; i < size; ++i) shot_buf[i] = static_cast<uint8_t>(i);
    dsc.header.w = kPanelWidth;
    dsc.header.h = kPanelHeight;
    dsc.data_size = size;
  } else if (lv_snapshot_take_to_buf(scr, LV_IMG_CF_TRUE_COLOR, &dsc, shot_buf, size) != LV_RES_OK) {
    console_puts("ERR snapshot");
    return;
  }
  shot_len = dsc.data_size;
  esp_log_level_set("*", ESP_LOG_NONE);
  console_printf("SHOT_BEGIN %u %u %u %u\n", static_cast<unsigned>(dsc.header.w),
                static_cast<unsigned>(dsc.header.h), static_cast<unsigned>(shot_len),
                static_cast<unsigned>(kLineBytes));
  send_range(0, shot_len);
  console_printf("%s", "SHOT_END\n");
  esp_log_level_set("*", ESP_LOG_ERROR);
  diag_quiet_http_logs();  // "*" clears the per-tag levels
}

void handle(char* cmd) {
  int a, b, c, d, e;
  if (strcmp(cmd, "PING") == 0) {
    console_puts("PONG");
  } else if (strcmp(cmd, "CO4ENQR") == 0) {
    play_print_qr_test_only();  // test builds only: the pairing string holds the device secret
  } else if (strncmp(cmd, "CO4ENRX ", 8) == 0) {
    // CO4ENRX <src mac 12 hex> <frame hex>: as if received over ESP-NOW
    uint8_t mac[6], f[CO4EN_MAX_FRAME];
    const char* p = cmd + 8;
    size_t n = 0;
    bool ok = strlen(p) > 13 && p[12] == ' ';
    for (int i = 0; ok && i < 6; ++i) ok = sscanf(p + 2 * i, "%2hhx", &mac[i]) == 1;
    for (const char* h = p + 13; ok && h[0] != '\0' && h[1] != '\0' && n < sizeof(f); h += 2)
      ok = sscanf(h, "%2hhx", &f[n++]) == 1;
    if (ok && n >= CO4EN_HDR_LEN) {
      play_cmd_inject(mac, f, n);
      console_printf("CO4ENRX queued %u\n", static_cast<unsigned>(n));
    } else {
      console_puts("CO4ENRX ERR");
    }
  } else if (strcmp(cmd, "CO4EN PAIR") == 0) {
    play_cmd_pair(true);
  } else if (strcmp(cmd, "CO4EN STOP") == 0) {
    play_cmd_pair(false);
  } else if (strcmp(cmd, "CO4EN RESET") == 0) {
    play_cmd_reset();
  } else if (strcmp(cmd, "CO4EN FWD 1") == 0 || strcmp(cmd, "CO4EN FWD 0") == 0) {
    play_cmd_fwd(cmd[10] == '1');
  } else if (strcmp(cmd, "CO4EN KVTEXT 1") == 0 || strcmp(cmd, "CO4EN KVTEXT 0") == 0) {
    play_cmd_kvtext(cmd[13] == '1');
  } else if (strncmp(cmd, "CO4EN SEND ", 11) == 0) {
    char nm[24] = "", v[24] = "", u[12] = "";
    if (sscanf(cmd + 11, "%23s %23s %11s", nm, v, u) >= 2) play_cmd_send(nm, v, u);
    else console_puts("CO4EN ERR");
  } else if (strcmp(cmd, "SCROLLCHK") == 0) {
    char text[120];
    ui_scroll_check(text, sizeof(text));
    console_printf("SCROLLCHK %s\n", text);
  } else if (strcmp(cmd, "ENERGY") == 0) {
    static EnergyView v;
    energy_get(&v);
    console_printf("ENERGY on=%d have=%d stale=%d http=%d p_in=%.0f p_out=%.0f today_in=%.3f today_out=%.3f water_l=%.1f "
                   "cost=%.2f credit=%.2f slot=%d days=%ld,%ld flags=%u water=%d/%d m3=%.3f water_y=%.1f err=%s\n",
                   v.enabled, v.have, v.stale, v.http_status, v.r.p_in_kw * 1000, v.r.p_out_kw * 1000,
                   energy_today_in(v.h), energy_today_out(v.h), energy_today_water_l(v.h), v.h.cost, v.h.credit,
                   (int)tariff_slot(v.tariff, time(nullptr)), (long)v.h.days[0].ymd, (long)v.h.days[1].ymd, v.h.flags,
                   v.r.has_water, v.h.have_water, v.r.water_m3, energy_yesterday_water_l(v.h), v.error);
  } else if (strcmp(cmd, "PV") == 0) {
    static PvView v;
    pv_get(&v);
    console_printf("PV on=%d conn=%d have=%d stale=%d asleep=%d down=%d polls=%lu ok=%lu errors=%lu rated=%lu "
                   "gw=%s:%u unit=%u dc=%.1f ac=%.1f hz=%.2f today=%.1f total=%.1f temp=%.1f v1=%.1f gv1=%.1f gv2=%.1f "
                   "gv3=%.1f status=%d hist_today=%.1f err=%s\n",
                   v.enabled, v.connected, v.have, v.stale, v.asleep, v.down, (unsigned long)v.polls,
                   (unsigned long)v.ok, (unsigned long)v.errors, (unsigned long)v.rated_w, v.host, v.port, v.unit,
                   v.r.pv_w, v.r.ac_w, v.r.grid_hz, v.r.today_kwh, v.r.total_kwh, v.r.temp_c, v.r.pv1_v, v.r.grid_v[0],
                   v.r.grid_v[1], v.r.grid_v[2], v.r.status, v.hist.today, v.error);
  } else if (strcmp(cmd, "STATE") == 0) {
    char text[64];
    ui_describe(text, sizeof(text));
    char pw[40];
    ui_power_describe(pw, sizeof(pw));
    console_printf("STATE %s relay=%d busy=%d %s\n", text, panel_relay_is_on() ? 1 : 0, sim != Sim::Idle ? 1 : 0, pw);
  } else if (sscanf(cmd, "BAUD %d", &a) == 1 && a >= 9600) {
    // Switch the link speed for this session (resets on reboot).
    console_puts("OK");
    console_flush();
    sleep_ms(20);
    console_set_baud(static_cast<unsigned long>(a));
  } else if (sscanf(cmd, "SHOTR %d %d", &a, &b) == 2) {
    send_range(static_cast<uint32_t>(a), static_cast<uint32_t>(b));
    console_printf("%s", "SHOTR_END\n");
  } else if (strcmp(cmd, "SHOT") == 0 || strcmp(cmd, "SHOT TEST") == 0) {
    shot(cmd[4] != '\0');
  } else if (sscanf(cmd, "TAP %d %d", &a, &b) == 2) {
    start_move(a, b, a, b, 120, true);
    console_puts("OK");
  } else if (sscanf(cmd, "PRESS %d %d", &a, &b) == 2) {
    start_move(a, b, a, b, 1, false);
    console_puts("OK");
  } else if (sscanf(cmd, "MOVE %d %d", &a, &b) == 2) {
    // Finger path step while held (after PRESS): lets the host replay real,
    // jittery multi-point touches instead of a straight SWIPE.
    if (sim == Sim::Hold || sim == Sim::Move) {
      cur_x = clampc(a, kPanelWidth);
      cur_y = clampc(b, kPanelHeight);
      sim = Sim::Hold;
      console_puts("OK");
    } else {
      console_puts("ERR not pressed");
    }
  } else if (strcmp(cmd, "RELEASE") == 0) {
    sim = sim == Sim::Idle ? Sim::Idle : Sim::ReleaseHold;
    t0 = now_ms();
    console_puts("OK");
  } else if (sscanf(cmd, "SWIPE %d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
    start_move(a, b, c, d, static_cast<uint32_t>(e < 1 ? 1 : e), true);
    console_puts("OK");
  } else if (strncmp(cmd, "POS ", 4) == 0) {
    int x = 0, y = 0;
    if (ui_find(cmd + 4, &x, &y)) {
      console_printf("POS %d %d\n", x, y);
    } else {
      console_puts("ERR not visible");
    }
  } else if (strcmp(cmd, "LIST") == 0) {
    ui_list_dump();
  } else if (strncmp(cmd, "TYPE ", 5) == 0) {
    console_puts(ui_type(cmd + 5) ? "OK" : "ERR no text field");
  } else if (strcmp(cmd, "ENTER") == 0) {
    console_puts(ui_enter() ? "OK" : "ERR no text field");
  } else if (strncmp(cmd, "SCRTO ", 6) == 0) {
    ui_set_screen_timeout(static_cast<uint16_t>(atoi(cmd + 6)));
    console_puts("OK");
  } else if (strcmp(cmd, "REBOOT") == 0) {
    ui_flush_settings();
    console_puts("OK");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
  } else if (sscanf(cmd, "REMTEST %d %d", &a, &b) == 2) {
    ui_rem_test(a, b);
    console_puts("OK");
  } else if (strncmp(cmd, "CALLEAD", 7) == 0) {
    if (cmd[7] == ' ') ui_set_cal_lead(static_cast<uint16_t>(atoi(cmd + 8)));
    console_printf("CALLEAD %u\n", ui_cal_lead());
  } else if (strncmp(cmd, "CALFROM", 7) == 0) {
    if (cmd[7] == ' ') ui_set_cal_from(static_cast<uint16_t>(atoi(cmd + 8)));
    console_printf("CALFROM %u\n", ui_cal_from());
  } else if (strcmp(cmd, "ACKDUMP") == 0) {
    // hex of the ack ring (save/restore real acknowledgements around tests)
    static uint8_t raw[256];
    size_t n = settings_reminder_acks_size();
    settings_reminder_acks_get(raw);
    static char hex[2 * sizeof(raw) + 1];
    for (size_t i = 0; i < n; ++i) snprintf(hex + 2 * i, 3, "%02x", raw[i]);
    console_printf("ACKS %s\n", hex);
  } else if (strncmp(cmd, "ACKLOAD ", 8) == 0) {
    // ACKLOAD <byte offset> <hex>: stage part of the ring; ACKLOAD END writes it.
    static uint8_t stage[256];
    size_t n = settings_reminder_acks_size();
    if (strcmp(cmd + 8, "END") == 0) {
      console_puts(settings_reminder_acks_set(stage) ? "OK" : "ERR nvs");
    } else {
      char* end = nullptr;
      long off = strtol(cmd + 8, &end, 10);
      const char* h = end != nullptr && *end == ' ' ? end + 1 : "";
      size_t k = 0;
      while (h[2 * k] != '\0' && h[2 * k + 1] != '\0' && off + k < n) {
        char b[3] = {h[2 * k], h[2 * k + 1], 0};
        stage[off + k] = static_cast<uint8_t>(strtoul(b, nullptr, 16));
        ++k;
      }
      console_printf("OK %u\n", static_cast<unsigned>(k));
    }
  } else if (strncmp(cmd, "WASTEFROM", 9) == 0) {
    if (cmd[9] == ' ') ui_set_waste_from(static_cast<uint16_t>(atoi(cmd + 10)));
    console_printf("WASTEFROM %u\n", ui_waste_from());
  } else if (strncmp(cmd, "WASTEUNTIL", 10) == 0) {
    if (cmd[10] == ' ') ui_set_waste_until(static_cast<uint16_t>(atoi(cmd + 11)));
    console_printf("WASTEUNTIL %u\n", ui_waste_until());
  } else if (strncmp(cmd, "WEBIDLE ", 8) == 0) {
    maint_web_set_idle_ms(static_cast<uint32_t>(atoi(cmd + 8)) * 1000u);
    console_puts("OK");
  } else if (strcmp(cmd, "BOOTOTHER") == 0 || strcmp(cmd, "BOOTOTHER NOVALID") == 0) {
    console_puts(maint_test_boot_other(cmd[9] == ' ') ? "OK" : "ERR");
  } else if (sscanf(cmd, "NETBUSY %d", &a) == 1) {
    console_puts(app_test_net_busy(static_cast<uint32_t>(a)) ? "OK" : "ERR busy");
  } else if (strncmp(cmd, "DIAG", 4) == 0) {
    diag_command(cmd);
  } else if (strcmp(cmd, "IMGVERIFY") == 0) {
    // Same check esp_ota_end() runs (incl. app signature) on the idle OTA slot.
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    esp_partition_pos_t pos = {part->address, part->size};
    esp_image_metadata_t md = {};
    esp_err_t e = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &md);
    console_printf("IMGVERIFY %s %s\n", part->label, esp_err_to_name(e));
  } else if (strcmp(cmd, "WASTETEST") == 0) {
    console_puts(ui_waste_test() ? "OK" : "ERR no calendar");
  } else if (strcmp(cmd, "REMDROP") == 0) {
    ui_rem_drop_tests();
    console_puts("OK");
  } else if (strcmp(cmd, "REMCLEAR") == 0) {
    ui_rem_clear();
    console_puts("OK");
  } else if (strcmp(cmd, "WIFIFORGET") == 0) {
    console_puts(settings_test_wifi(0, "") ? "OK" : "ERR nvs");
  } else if (strncmp(cmd, "WIFIPASS ", 9) == 0) {
    console_puts(settings_test_wifi(1, cmd + 9) ? "OK" : "ERR nvs");
  } else if (strcmp(cmd, "WIFIRELOAD") == 0) {
    // As if the saved network came back: restart the station with the NVS creds.
    WifiCreds c;
    if (settings_load_wifi(&c)) wifi_start(c.ssid, c.pass, "loxone-panel");
    memset(&c, 0, sizeof(c));
    console_puts("OK");
  } else if (strcmp(cmd, "WIFIRESTORE") == 0) {
    console_puts(settings_test_wifi(2, "") ? "OK" : "ERR nvs");
  } else if (strncmp(cmd, "VERSET", 6) == 0) {
    ota_set_version_override(cmd[6] == ' ' ? cmd + 7 : "");
    console_printf("VERSION %s\n", ota_running_version());
  } else if (strcmp(cmd, "LOCRESET") == 0) {
    ui_reset_location();
    console_puts("OK");
  } else if (cmd[0] != '\0') {
    console_puts("ERR unknown");
  }
}

}  // namespace

void harness_poll() {
  while (console_available() > 0) {
    int ch = console_read();
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      line[line_len] = '\0';
      handle(line);
      line_len = 0;
    } else if (line_len < sizeof(line) - 1) {
      line[line_len++] = static_cast<char>(ch);
    }
  }
}

bool harness_touch_override(lv_indev_data_t* data) {
  uint32_t now = now_ms();
  switch (sim) {
    case Sim::Idle:
      return false;
    case Sim::Move: {
      uint32_t el = now - t0;
      if (el >= dur) {
        cur_x = x1_;
        cur_y = y1_;
        if (auto_release) {
          sim = Sim::ReleaseHold;
          t0 = now;
        } else {
          sim = Sim::Hold;
        }
      } else {
        cur_x = x0_ + static_cast<int>((x1_ - x0_) * static_cast<int32_t>(el) / static_cast<int32_t>(dur));
        cur_y = y0_ + static_cast<int>((y1_ - y0_) * static_cast<int32_t>(el) / static_cast<int32_t>(dur));
      }
      data->point.x = cur_x;
      data->point.y = cur_y;
      data->state = LV_INDEV_STATE_PRESSED;
      return true;
    }
    case Sim::Hold:
      data->point.x = cur_x;
      data->point.y = cur_y;
      data->state = LV_INDEV_STATE_PRESSED;
      return true;
    case Sim::ReleaseHold:
      // Report released at the last point for a moment, then hand back to GT911.
      data->point.x = cur_x;
      data->point.y = cur_y;
      data->state = LV_INDEV_STATE_RELEASED;
      if (now - t0 > 60) {
        sim = Sim::Idle;
      }
      return true;
  }
  return false;
}

#endif
