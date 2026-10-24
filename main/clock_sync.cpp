#include "clock_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>

#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "sys.h"
#include <esp_log.h>

static const char* const TAG = "clock";

namespace {
// esp_sntp keeps the server name pointer, so the string must outlive SNTP.
char g_server[64] = "";
bool g_started = false;
std::atomic<bool> g_synced{false};
std::atomic<unsigned> g_last_sync_s{0};

void on_sync(struct timeval* tv) {
  g_synced = true;
  g_last_sync_s = static_cast<unsigned>(esp_timer_get_time() / 1000000);
  console_printf("SNTP synced from %s (%lld)\n", g_server, static_cast<long long>(tv->tv_sec));
}
}  // namespace

void clock_sntp_start(const char* server) {
  if (g_started) {
    esp_netif_sntp_deinit();
    g_started = false;
  }
  snprintf(g_server, sizeof(g_server), "%s", server != nullptr && server[0] != '\0' ? server : "pool.ntp.org");
  esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(g_server);
  cfg.sync_cb = on_sync;
  esp_err_t err = esp_netif_sntp_init(&cfg);
  g_started = err == ESP_OK;
  ESP_LOGW(TAG, "SNTP start %s: %s", g_server, esp_err_to_name(err));
}

bool clock_synced() { return g_synced; }

// Called when the station got its IP: lwIP SNTP sent its first request before
// the link was up and would wait for its retry timer (~25 s here), which held
// back everything that needs the time (MeteoSchweiz, calendar).
void clock_sntp_kick() {
  if (g_started && !g_synced) {
    esp_sntp_restart();
    console_puts("SNTP restarted after Wi-Fi connect");
  }
}

unsigned clock_last_sync_s() { return g_last_sync_s; }

void clock_apply_tz(const char* posix_tz) {
  setenv("TZ", posix_tz != nullptr && posix_tz[0] != '\0' ? posix_tz : "UTC0", 1);
  tzset();
}
