#include "pv_task.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
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
constexpr size_t kBuf = 2048;

template <typename F>
void update(F f) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  f(*g_view);
  xSemaphoreGive(g_mu);
}

void log_hex(const char* what, const uint8_t* d, size_t n) {
  char hex[2 * 48 + 1];
  size_t m = n < 48 ? n : 48;
  for (size_t i = 0; i < m; ++i) snprintf(hex + 2 * i, 3, "%02x", d[i]);
  console_printf("PV: %s (%u bytes) %s%s\n", what, static_cast<unsigned>(n), hex, n > m ? "..." : "");
}

void serve(int c, uint8_t* buf) {
  size_t have = 0;
  bool timed = false;
  uint8_t out[128];
  int idle = 0;
  for (;;) {
    if (g_reload) return;
    int r = recv(c, buf + have, kBuf - have, 0);
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && ++idle > 450) {
      console_printf("PV: no data for 15 min, closing\n");  // the stick reconnects on a new socket
      return;
    }
    if (r > 0) idle = 0;
    if (r == 0) {
      console_printf("PV: stick closed the connection\n");
      return;
    }
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // 2 s receive timeout: check reload
      console_printf("PV: recv error %d\n", errno);
      return;
    }
    have += r;
    for (;;) {
      int len = pv_frame_length(buf, have);
      if (len == 0) {
        if (have == kBuf) have = 0;  // cannot complete: drop
        break;
      }
      if (len < 0) {
        log_hex("unexpected data, dropped", buf, have);
        have = 0;
        break;
      }
      PvFrame f;
      PvReading pr;
      int k = pv_handle(buf, len, &f, &pr, out, sizeof(out));
      const int64_t now = time(nullptr);
      if (k < 0) {
        log_hex("bad CRC, ignored", buf, len);
      } else {
        if (k > 0) send(c, out, k, 0);
        if (f.type == kPvData && pr.valid) {
          update([&](PvView& v) {
            if (v.fetched > 0 && now - v.fetched > 20) v.interval_s = static_cast<int32_t>(now - v.fetched);
            v.r = pr;
            v.have = true;
            v.fetched = now;
            v.proto = f.proto;
            v.records++;
            v.error[0] = '\0';
          });
          console_printf("PV: live record proto %u: %.0f W DC, %.0f W AC, today %.1f kWh, total %.1f kWh, %.1f C\n",
                         f.proto, pr.pv_w, pr.ac_w, pr.today_kwh, pr.total_kwh, pr.temp_c);
        } else if (f.type == kPvData) {
          log_hex("data record with unknown layout", buf, len);
          update([&](PvView& v) { snprintf(v.error, sizeof(v.error), "unbekanntes Datenformat"); });
        } else if (f.type == kPvAnnounce && !timed) {
          // Logger serial: plain bytes 8..17 (pv_handle descrambled the record).
          char logger[11];
          for (int i = 0; i < 10; ++i) logger[i] = static_cast<char>(buf[8 + i]);
          logger[10] = '\0';
          time_t tt = time(nullptr);
          struct tm lt;
          localtime_r(&tt, &lt);
          char when[24];
          strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &lt);
          size_t tl = pv_time_command(f.proto, logger, when, out, sizeof(out));
          vTaskDelay(pdMS_TO_TICKS(500));
          if (tl > 0) send(c, out, tl, 0);
          timed = true;
          console_printf("PV: announce (proto %u), time set to %s\n", f.proto, when);
        } else if (f.type != kPvPing && f.type != kPvBuffered && f.type != kPvAnnounce) {
          log_hex("other record", buf, len);
        }
      }
      memmove(buf, buf + len, have - len);
      have -= len;
    }
  }
}

void task(void*) {
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kBuf, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) vTaskDelete(nullptr);
  for (;;) {
    g_reload = false;
    char ip[16], port_s[8];
    bool on = settings_load_pv_on();
    settings_load_text(kTextPvIp, ip, sizeof(ip), nullptr);
    settings_load_text(kTextPvPort, port_s, sizeof(port_s), nullptr);
    int port = atoi(port_s);
    if (port <= 0 || port > 65535) port = 5279;
    update([&](PvView& v) {
      v.enabled = on && ip[0] != '\0';
      v.listening = false;
      v.connected = false;
      snprintf(v.peer, sizeof(v.peer), "%s", ip);
    });
    if (!on || ip[0] == '\0' || !wifi_connected()) {
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    int ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (ls < 0 || bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(ls, 1) != 0) {
      console_printf("PV: cannot listen on %d (errno %d)\n", port, errno);
      if (ls >= 0) close(ls);
      update([&](PvView& v) { snprintf(v.error, sizeof(v.error), "Port %d belegt", port); });
      vTaskDelay(pdMS_TO_TICKS(10000));
      continue;
    }
    console_printf("PV: listening on %d, accepting only %s\n", port, ip);
    update([&](PvView& v) { v.listening = true; });
    in_addr_t allowed = inet_addr(ip);
    while (!g_reload) {
      fd_set rs;
      FD_ZERO(&rs);
      FD_SET(ls, &rs);
      timeval tv = {2, 0};
      if (select(ls + 1, &rs, nullptr, nullptr, &tv) <= 0) continue;
      sockaddr_in peer = {};
      socklen_t pl = sizeof(peer);
      int c = accept(ls, reinterpret_cast<sockaddr*>(&peer), &pl);
      if (c < 0) continue;
      char pip[16];
      inet_ntoa_r(peer.sin_addr, pip, sizeof(pip));
      if (peer.sin_addr.s_addr != allowed) {
        console_printf("PV: refused connection from %s\n", pip);
        update([&](PvView& v) { v.rejected++; });
        close(c);
        continue;
      }
      console_printf("PV: stick %s connected\n", pip);
      timeval rt = {2, 0};
      setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
      timeval st = {5, 0};
      setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof(st));
      update([&](PvView& v) { v.connected = true; });
      serve(c, buf);
      close(c);
      update([&](PvView& v) { v.connected = false; });
    }
    close(ls);
  }
}

}  // namespace

void pv_start() {
  g_mu = xSemaphoreCreateMutex();
  g_view = static_cast<PvView*>(heap_caps_calloc(1, sizeof(PvView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_mu == nullptr || g_view == nullptr) return;
  // Core 0, priority 2 (< whatwatt 3 < net 4 < Loxone 5); stack in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(task, "pv", 6 * 1024, nullptr, 2, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void pv_get(PvView* out) {
  if (g_mu == nullptr || g_view == nullptr) {
    memset(out, 0, sizeof(*out));
    return;
  }
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *out = *g_view;
  xSemaphoreGive(g_mu);
  out->stale = pv_stale(time(nullptr), out->fetched, out->interval_s);
}

void pv_kick() { g_reload = true; }
