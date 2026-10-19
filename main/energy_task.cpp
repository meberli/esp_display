#include "energy_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clock_sync.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "settings.h"
#include "sys.h"

namespace {

SemaphoreHandle_t g_mu = nullptr;
EnergyView* g_view = nullptr;  // PSRAM, guarded by g_mu
TaskHandle_t g_task = nullptr;
constexpr size_t kBodyCap = 6 * 1024;

void publish(const EnergySettings& s, const EnergyReading* r, const EnergyHist& h, int status, const char* err,
             int64_t now) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_view->enabled = s.on && s.host[0] != '\0';
  g_view->tariff = s.tariff;
  g_view->show_exp = s.show_exp;
  g_view->water_on = s.water_on;
  g_view->http_status = status;
  snprintf(g_view->error, sizeof(g_view->error), "%s", err);
  if (r != nullptr) {
    g_view->r = *r;
    g_view->have = true;
    g_view->fetched = now;
  }
  g_view->h = h;
  xSemaphoreGive(g_mu);
}

void task(void*) {
  EnergyHist* hist = static_cast<EnergyHist*>(heap_caps_calloc(1, sizeof(EnergyHist), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  uint8_t* body = static_cast<uint8_t*>(heap_caps_malloc(kBodyCap + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (hist == nullptr || body == nullptr) {
    console_printf("Energie: out of memory\n");
    vTaskDelete(nullptr);
    return;
  }
  bool loaded = settings_load_energy_hist(hist);
  console_printf("Energie: history %s (%u bytes)\n", loaded ? "loaded" : "new", static_cast<unsigned>(sizeof(EnergyHist)));
  int fails = 0;
  for (;;) {
    EnergySettings s = settings_load_energy();
    uint32_t wait_s = s.poll_s;
    if (!s.on || s.host[0] == '\0' || !wifi_connected() || !clock_synced()) {
      publish(s, nullptr, *hist, 0, s.host[0] == '\0' ? "keine Adresse" : !s.on ? "aus" : "warte auf WLAN/Zeit", time(nullptr));
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
      continue;
    }
    char url[96];
    snprintf(url, sizeof(url), "http://%s/api/v1/report", s.host);
    HttpGetResult http = http_get(url, nullptr, nullptr, body, kBodyCap, 4000);
    const int64_t now = time(nullptr);
    EnergyReading r;
    bool parsed = http.ok && http.status == 200 && energy_parse_report(reinterpret_cast<const char*>(body), http.length,
                                                                        s.water_on ? s.water_bus : 0, &r);
    // The M-Bus water value is not in every report: keep the last one meanwhile.
    static bool have_water = false;
    static double last_water = 0;
    static int64_t last_water_ts = 0;
    static EnergyTsState tss = {};
    if (parsed && r.ok && s.water_on) {
      if (r.has_water) {
        have_water = true;
        last_water = r.water_m3;
        last_water_ts = r.water_ts;
      } else if (have_water) {
        r.has_water = true;
        r.water_m3 = last_water;
        r.water_ts = last_water_ts;
      }
    }
    if (parsed && r.ok) {
      static bool first = true;
      if (first) {
        first = false;
        console_printf("Energie: first reading %.0f W, water %s (bus %u, %u bytes)\n", r.p_in_kw * 1000,
                       r.has_water ? "yes" : "no", s.water_bus, static_cast<unsigned>(http.length));
      }
      int res = energy_hist_update(hist, r, now, s.tariff, &tss);
      if (res & kHistLate) console_printf("Energie: late reading booked to a finished day\n");
      if (res & (kHistHour | kHistDay | kHistReset | kHistLate)) {
        bool ok = settings_save_energy_hist(*hist);  // at most hourly (and on the first reading / meter change)
        console_printf("Energie: history saved=%d%s\n", ok ? 1 : 0, (res & kHistDay) ? " (new day)" : "");
      }
      if (res & kHistRejected) console_printf("Energie: counter went backwards, reading ignored\n");
      publish(s, &r, *hist, http.status, "", now);
      fails = 0;
    } else {
      char err[40];
      if (!http.ok && http.status == 0) snprintf(err, sizeof(err), "keine Verbindung");
      else if (http.status != 200) snprintf(err, sizeof(err), "HTTP %d", http.status);
      else if (!parsed) snprintf(err, sizeof(err), "Antwort unlesbar");
      else snprintf(err, sizeof(err), "Z\xC3\xA4hler meldet keine Daten");
      if (++fails == 1 || fails % 30 == 0) console_printf("Energie: %s (%d)\n", err, fails);
      publish(s, nullptr, *hist, http.status, err, now);
      if (fails > 3) wait_s = s.poll_s < 30 ? 30 : s.poll_s;  // back off while the reader is away
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_s * 1000));
  }
}

}  // namespace

void energy_start() {
  g_mu = xSemaphoreCreateMutex();
  g_view = static_cast<EnergyView*>(heap_caps_calloc(1, sizeof(EnergyView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (g_mu == nullptr || g_view == nullptr) return;
  // Core 0 with the network; priority 3 < net (4) < Loxone (5). Stack in PSRAM.
  xTaskCreatePinnedToCoreWithCaps(task, "energy", 10 * 1024, nullptr, 3, &g_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void energy_get(EnergyView* out) {
  if (g_mu == nullptr || g_view == nullptr) {
    memset(out, 0, sizeof(*out));
    return;
  }
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *out = *g_view;
  xSemaphoreGive(g_mu);
  const int64_t now = time(nullptr);
  out->stale = !out->have || now - out->fetched > kEnergyStaleS ||
               (out->r.ts > 0 && (now - out->r.ts > kEnergyStaleS || out->r.ts - now > kEnergyStaleS));
}

void energy_kick() {
  if (g_task != nullptr) xTaskNotifyGive(g_task);
}
