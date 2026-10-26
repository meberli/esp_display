#include "pv_task.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "net.h"
#include "settings.h"
#include "sys.h"

namespace {

SemaphoreHandle_t g_mu = nullptr;
PvView* g_view = nullptr;  // PSRAM, guarded by g_mu
volatile bool g_reload = false;
constexpr int kTimeoutMs = 3500;
constexpr int kGapMs = 200;

template <typename F>
void update(F f) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  f(*g_view);
  xSemaphoreGive(g_mu);
}

int32_t local_ymd(int64_t now) {
  if (now < 1700000000) return 0;  // clock not set yet
  time_t t = static_cast<time_t>(now);
  struct tm lt;
  localtime_r(&t, &lt);
  return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

int local_sod(int64_t now) {
  time_t t = static_cast<time_t>(now);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
}

int connect_gw(const char* host, int port) {
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return -1;
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = inet_addr(host);
  // Non-blocking connect with the request timeout.
  int fl = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, fl | O_NONBLOCK);
  int r = connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
  if (r != 0 && errno == EINPROGRESS) {
    fd_set ws;
    FD_ZERO(&ws);
    FD_SET(s, &ws);
    timeval tv = {kTimeoutMs / 1000, (kTimeoutMs % 1000) * 1000};
    int err = 0;
    socklen_t el = sizeof(err);
    if (select(s + 1, nullptr, &ws, nullptr, &tv) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && err == 0)
      r = 0;
  }
  if (r != 0) {
    close(s);
    return -1;
  }
  fcntl(s, F_SETFL, fl);
  timeval rt = {0, 250 * 1000};  // short slices: the deadline is enforced below
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
  timeval st = {kTimeoutMs / 1000, 0};
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof(st));
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return s;
}

// One read transaction. 0 = ok, -1 = timeout, -2 = exception (*exc), -3 = socket error/garbage (reconnect).
int transact(int s, uint16_t tid, uint8_t unit, uint8_t fc, uint16_t start, uint16_t count, uint16_t* regs, uint8_t* exc) {
  uint8_t q[12];
  size_t ql = pv_mb_request(q, sizeof(q), tid, unit, fc, start, count);
  if (ql == 0) return -3;
  // Drop anything left over (a late answer to a timed-out request).
  uint8_t junk[64];
  while (recv(s, junk, sizeof(junk), MSG_DONTWAIT) > 0) {
  }
  if (send(s, q, ql, 0) != static_cast<int>(ql)) return -3;
  uint8_t buf[260];
  size_t have = 0;
  const uint32_t t0 = now_ms();
  while (now_ms() - t0 < static_cast<uint32_t>(kTimeoutMs)) {
    int r = recv(s, buf + have, sizeof(buf) - have, 0);
    if (r == 0) return -3;
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
      return -3;
    }
    have += r;
    int k = pv_mb_response(buf, have, tid, unit, fc, count, regs, exc);
    if (k > 0) return 0;
    if (k == -2) return -2;
    if (k == -1) return -3;
    if (have == sizeof(buf)) return -3;
  }
  return -1;
}

void task(void*) {
  PvHist* hist = static_cast<PvHist*>(heap_caps_calloc(1, sizeof(PvHist), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (hist == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  bool loaded = settings_load_pv_hist(hist);
  console_printf("PV: history %s (%u bytes)\n", loaded ? "loaded" : "new", static_cast<unsigned>(sizeof(PvHist)));
  PvHours* hours = static_cast<PvHours*>(heap_caps_calloc(1, sizeof(PvHours), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (hours == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  bool hloaded = settings_load_pv_hours(hours);
  hours->last_t = 0;  // never integrate across a reboot
  console_printf("PV: hours %s (%u bytes)\n", hloaded ? "loaded" : "new", static_cast<unsigned>(sizeof(PvHours)));
  update([&](PvView& v) {
    v.hist = *hist;
    v.hours = *hours;
  });
  int s = -1;
  uint16_t tid = 0;
  bool rated_done = false;
  uint32_t last_save = 0;
  int timeouts = 0;
  struct { uint32_t polls, ok, timeouts, errors; } hr = {};
  uint32_t hr_t0 = now_ms();
  for (;;) {
    g_reload = false;
    char host[16], port_s[8], unit_s[8];
    bool on = settings_load_pv_on();
    settings_load_text(kTextPvGw, host, sizeof(host), nullptr);
    settings_load_text(kTextPvGwPort, port_s, sizeof(port_s), nullptr);
    settings_load_text(kTextPvUnit, unit_s, sizeof(unit_s), nullptr);
    int port = atoi(port_s);
    if (port <= 0 || port > 65535) port = 502;
    int unit = atoi(unit_s);
    if (unit < 0 || unit > 247) unit = 1;
    update([&](PvView& v) {
      v.enabled = on && host[0] != '\0';
      snprintf(v.host, sizeof(v.host), "%s", host);
      v.port = static_cast<uint16_t>(port);
      v.unit = static_cast<uint8_t>(unit);
    });
    console_printf("PV: Modbus TCP %s:%d unit %d (%s, read only FC04)\n", host, port, unit, on ? "on" : "off");
    while (!g_reload) {
      const uint32_t cycle_t0 = now_ms();
      if (now_ms() - hr_t0 >= 3600u * 1000u) {
        hr_t0 = now_ms();
        if (hr.polls > 0)
          console_printf("PV: last hour polls=%lu ok=%lu timeouts=%lu errors=%lu\n", (unsigned long)hr.polls,
                         (unsigned long)hr.ok, (unsigned long)hr.timeouts, (unsigned long)hr.errors);
        hr = {};
      }
      if (!on || host[0] == '\0' || !wifi_connected()) {
        if (s >= 0) close(s), s = -1;
        update([&](PvView& v) { v.connected = false; });
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (!on) break;  // re-read settings
        continue;
      }
      if (s < 0) {
        s = connect_gw(host, port);
        if (s < 0) {
          ++hr.errors;
          update([&](PvView& v) {
            v.connected = false;
            v.down = true;
            v.asleep = false;
            v.errors++;
            snprintf(v.error, sizeof(v.error), "Gateway nicht erreichbar");
          });
        } else {
          console_printf("PV: connected to gateway\n");
          update([&](PvView& v) { v.connected = true; v.down = false; });
          vTaskDelay(pdMS_TO_TICKS(kGapMs));
        }
      }
      if (s >= 0 && !rated_done) {
        uint16_t hregs[10];
        uint8_t exc = 0;
        int r = transact(s, ++tid, unit, kPvFcHolding, 0, 10, hregs, &exc);
        if (r == 0) {
          rated_done = true;
          update([&](PvView& v) { v.rated_w = hregs[8]; });
          console_printf("PV: rated power %u W\n", hregs[8]);
        } else if (r == -2) {
          rated_done = true;  // not offered: never ask again
        } else if (r == -3) {
          close(s), s = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(kGapMs));
      }
      if (s >= 0) {
        uint16_t regs[kPvRegCount];
        uint8_t exc = 0;
        ++hr.polls;
        int r = transact(s, ++tid, unit, kPvFcInput, kPvRegStart, kPvRegCount, regs, &exc);
        const int64_t now = time(nullptr);
        PvReading pr;
        if (r == 0 && pv_decode_input(regs, kPvRegCount, &pr)) {
          ++hr.ok;
          if (timeouts > 0) console_printf("PV: inverter answers again (after %d timeouts)\n", timeouts);
          timeouts = 0;
          const int32_t ymd = local_ymd(now);
          int hres = ymd != 0 ? pv_hist_update(hist, ymd, pr.today_kwh) : 0;
          // Persist on a new day, the first reading of the boot, otherwise at most hourly.
          if (hres != 0 && ((hres & 2) || last_save == 0 || now_ms() - last_save > 3600u * 1000u)) {
            bool ok = settings_save_pv_hist(*hist);
            last_save = now_ms() | 1;
            console_printf("PV: history saved=%d%s\n", ok ? 1 : 0, (hres & 2) ? " (new day)" : "");
          }
          // Hourly buckets: Eac only once the inverter's counter belongs to today (morning reset).
          const float eac = hist->ymd == ymd && hist->fresh && hist->today >= 0 ? hist->today : -1.0f;
          const int qres = ymd != 0 ? pv_hours_add(hours, ymd, now, local_sod(now), pr.ac_w, eac) : 0;
          // NVS wear: persist only when an hour (or day) is entered, <= 24 writes a day, none at night.
          if (qres & 2) {
            bool ok = settings_save_pv_hours(*hours);
            console_printf("PV: hours saved=%d\n", ok ? 1 : 0);
          }
          static bool first = true;
          if (first) {
            first = false;
            console_printf("PV: first reading status %d, %.0f W DC, %.0f W AC, today %.1f kWh, total %.1f kWh, %.1f C\n",
                           pr.status, pr.pv_w, pr.ac_w, pr.today_kwh, pr.total_kwh, pr.temp_c);
          }
          update([&](PvView& v) {
            v.r = pr;
            v.have = true;
            v.fetched = now;
            v.asleep = false;
            v.down = false;
            v.polls++;
            v.ok++;
            v.error[0] = '\0';
            v.hist = *hist;
            v.hours = *hours;
          });
        } else {
          const bool sleepish = r == -1 || r == -2;  // no RS485 answer (timeout / gateway exception 0x0B)
          if (r == -1) ++hr.timeouts, ++timeouts;
          else ++hr.errors;
          if (timeouts == 3) console_printf("PV: inverter not answering (asleep?)\n");
          if (r == -3) close(s), s = -1;
          update([&](PvView& v) {
            v.polls++;
            v.errors++;
            if (sleepish) v.asleep = true;
            if (r == -3) v.connected = false;
            if (r == -1) snprintf(v.error, sizeof(v.error), "Wechselrichter antwortet nicht");
            else if (r == -2) snprintf(v.error, sizeof(v.error), "Modbus-Fehler %u", exc);
            else snprintf(v.error, sizeof(v.error), "Verbindung unterbrochen");
          });
          // Many timeouts on one socket: start a fresh connection (still one at a time).
          if (r == -1 && timeouts % 6 == 0 && s >= 0) close(s), s = -1;
        }
      }
      // Poll every 10 s (from the start of this cycle).
      while (!g_reload && now_ms() - cycle_t0 < kPvPollS * 1000u) vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (s >= 0) close(s), s = -1;
    update([&](PvView& v) { v.connected = false; });
  }
}

}  // namespace

void pv_start() {
  g_mu = xSemaphoreCreateMutex();
  g_view = static_cast<PvView*>(heap_caps_calloc(1, sizeof(PvView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_mu == nullptr || g_view == nullptr) return;
  pv_hist_clear(&g_view->hist);
  pv_hours_clear(&g_view->hours);
  // Core 0, priority 2 (< whatwatt 3 < net 4 < Loxone 5); stack in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(task, "pv", 6 * 1024, nullptr, 2, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void pv_get(PvView* out) {
  if (g_mu == nullptr || g_view == nullptr) {
    memset(out, 0, sizeof(*out));
    pv_hist_clear(&out->hist);
    pv_hours_clear(&out->hours);
    return;
  }
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *out = *g_view;
  xSemaphoreGive(g_mu);
  out->stale = pv_stale(time(nullptr), out->fetched);
}

void pv_kick() { g_reload = true; }
