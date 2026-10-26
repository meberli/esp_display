#include "esp_attr.h"
#include "net.h"
#include "diag.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>

#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "sys.h"

namespace {
std::atomic<bool> g_connected{false};
std::atomic<bool> g_failed{false};
std::atomic<bool> g_scanning{false};    // no reconnects while a scan runs
std::atomic<bool> g_hold{false};        // Playground CII mode: stay disconnected
std::atomic<int> g_last_reason{0};      // last WIFI_REASON_* of a disconnect
std::atomic<int> g_auth_fails{0};       // auth / handshake rejections since wifi_start
std::atomic<int> g_noap_fails{0};       // "AP not found" since wifi_start
esp_netif_t* g_netif = nullptr;
char g_ssid[33] = "";
// Reconnect backoff: the first 3 attempts right away (WPA3 routers often
// refuse the first SAE try), then 2, 4, 8 ... 30 s, reset on got IP.
int g_retry = 0;
esp_timer_handle_t g_retry_timer = nullptr;
void retry_cb(void*) {
  if (!g_scanning && !g_hold && g_ssid[0] != '\0' && !g_connected) esp_wifi_connect();
}
void reconnect() {
  int n = g_retry++;
  if (n < 3) {
    esp_wifi_connect();
    return;
  }
  int s = (n - 3) >= 4 ? 30 : (2 << (n - 3));
  if (s > 30) s = 30;
  if (g_retry_timer == nullptr) {
    esp_timer_create_args_t a = {};
    a.callback = retry_cb;
    a.name = "wifi_retry";
    esp_timer_create(&a, &g_retry_timer);
  }
  esp_timer_stop(g_retry_timer);
  esp_timer_start_once(g_retry_timer, static_cast<uint64_t>(s) * 1000000ULL);
  printf("Wi-Fi retry in %d s\n", s);
}
// Bit 0: got IP; bit 1: a disconnect happened (waiters re-check the counters).
constexpr EventBits_t kBitIp = 1, kBitDisc = 2, kBitScan = 4;  // bit 2: scan done
// Highest WLAN-scan generation the UI cancelled ("Abbrechen"); read by the net task.
std::atomic<uint32_t> g_scan_cancel{0};
EventGroupHandle_t wifi_events() {
  static StaticEventGroup_t buf;
  static EventGroupHandle_t g = xEventGroupCreateStatic(&buf);
  return g;
}

bool is_auth_reason(int r) {
  return r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT ||
         r == WIFI_REASON_MIC_FAILURE || r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_ASSOC_FAIL ||
         r == WIFI_REASON_CONNECTION_FAIL;
}

void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    xEventGroupSetBits(wifi_events(), kBitScan);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (g_ssid[0] != '\0' && !g_hold) esp_wifi_connect();  // empty SSID: station only for scanning
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (g_connected) diag_wifi_disconnect();
    g_connected = false;
    xEventGroupClearBits(wifi_events(), kBitIp);
    xEventGroupSetBits(wifi_events(), kBitDisc);
    auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
    int reason = ev != nullptr ? ev->reason : 0;
    g_last_reason = reason;
    if (reason == WIFI_REASON_NO_AP_FOUND || reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD ||
        reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD || reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY) {
      g_noap_fails = g_noap_fails + 1;
      g_failed = true;
    } else if (is_auth_reason(reason)) {
      g_auth_fails = g_auth_fails + 1;
      g_failed = true;
    }
    printf("Wi-Fi disconnect reason %d (auth %d, no AP %d)\n", reason, g_auth_fails.load(), g_noap_fails.load());
    // Keep trying (also the saved network after a failed boot); the caller
    // decides when to give up or switch. A scan pauses the retries.
    if (!g_scanning && !g_hold && g_ssid[0] != '\0') reconnect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    g_connected = true;
    g_failed = false;
    g_retry = 0;
    xEventGroupSetBits(wifi_events(), kBitIp);
  }
}
}  // namespace

bool wifi_wait(uint32_t ms) {
  if (g_connected) return true;
  xEventGroupClearBits(wifi_events(), kBitDisc);
  xEventGroupWaitBits(wifi_events(), kBitIp | kBitDisc, pdFALSE, pdFALSE, pdMS_TO_TICKS(ms));
  return g_connected;
}

void wifi_start(const char* ssid, const char* password, const char* hostname) {
  static bool inited = false;
  if (!inited) {
    g_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr);
    if (hostname != nullptr) esp_netif_set_hostname(g_netif, hostname);
    inited = true;
  } else {
    g_scanning = true;  // the stop below raises a disconnect: do not reconnect to the old AP
    esp_wifi_stop();
    g_scanning = false;
  }
  g_connected = false;
  g_failed = false;
  g_auth_fails = 0;
  g_noap_fails = 0;
  g_retry = 0;
  if (g_retry_timer != nullptr) esp_timer_stop(g_retry_timer);
  g_last_reason = 0;
  snprintf(g_ssid, sizeof(g_ssid), "%s", ssid);
  wifi_config_t wc = {};
  snprintf(reinterpret_cast<char*>(wc.sta.ssid), sizeof(wc.sta.ssid), "%s", ssid);
  snprintf(reinterpret_cast<char*>(wc.sta.password), sizeof(wc.sta.password), "%s", password);
  wc.sta.threshold.authmode = password[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_set_ps(WIFI_PS_NONE);  // was WiFi.setSleep(false)
  esp_wifi_start();
}

bool wifi_connected() { return g_connected; }
void wifi_hold(bool on) {
  if (on == g_hold) return;
  g_hold = on;
  if (on) {
    if (g_retry_timer != nullptr) esp_timer_stop(g_retry_timer);
    esp_wifi_disconnect();
    printf("Wi-Fi held (Playground CII mode)\n");
  } else {
    g_retry = 0;
    if (g_ssid[0] != '\0') esp_wifi_connect();
    printf("Wi-Fi released: reconnecting\n");
  }
}
bool wifi_failed() { return g_failed; }
int wifi_auth_fails() { return g_auth_fails; }
int wifi_noap_fails() { return g_noap_fails; }
int wifi_last_reason() { return g_last_reason; }

namespace {
SemaphoreHandle_t g_tls_mu = nullptr;
portMUX_TYPE g_heap_mux = portMUX_INITIALIZER_UNLOCKED;
size_t g_heap_min = SIZE_MAX;
char g_heap_label[40] = "boot";
}  // namespace

void heap_note(const char* label) {
  size_t m = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  bool dropped = false;
  portENTER_CRITICAL(&g_heap_mux);
  if (m < g_heap_min) {
    dropped = g_heap_min != SIZE_MAX;
    g_heap_min = m;
    snprintf(g_heap_label, sizeof(g_heap_label), "%s", label);
  }
  portEXIT_CRITICAL(&g_heap_mux);
  if (dropped) console_printf("HEAP: new internal minimum %u B during %s\n", (unsigned)m, label);
}
const char* heap_min_label() { return g_heap_label; }
size_t heap_min_value() { return g_heap_min; }

bool tls_lock(const char* label, uint32_t wait_ms) {
  static portMUX_TYPE init_mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&init_mux);
  bool create = g_tls_mu == nullptr;
  portEXIT_CRITICAL(&init_mux);
  if (create) {
    SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    portENTER_CRITICAL(&init_mux);
    if (g_tls_mu == nullptr) { g_tls_mu = m; m = nullptr; }
    portEXIT_CRITICAL(&init_mux);
    if (m != nullptr) vSemaphoreDelete(m);
  }
  heap_note("other");  // a drop before this request is not ours
  if (xSemaphoreTakeRecursive(g_tls_mu, wait_ms == 0 ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
    console_printf("TLS: %s waited %lu ms, giving up\n", label, (unsigned long)wait_ms);
    return false;
  }
  return true;
}

void tls_unlock(const char* label) {
  heap_note(label);
  xSemaphoreGiveRecursive(g_tls_mu);
}

HttpGetResult http_get(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                       int timeout_ms, const char* hdr_name, const char* hdr_value) {
  // Label = host only (never the path: it may carry tokens, e.g. the iCal URL).
  char label[40];
  const char* h = strstr(url, "://");
  h = h != nullptr ? h + 3 : url;
  size_t hl = strcspn(h, "/:?");
  snprintf(label, sizeof(label), "%.*s", static_cast<int>(hl < 39 ? hl : 39), h);
  if (strncmp(url, "https://", 8) == 0) {
    TlsGuard g(label);
    if (!g.ok) {
      HttpGetResult r = {};
      snprintf(r.error, sizeof(r.error), "TLS busy");
      return r;
    }
    return http_get_raw(url, user, password, body, cap, timeout_ms, hdr_name, hdr_value);
  }
  HttpGetResult r = http_get_raw(url, user, password, body, cap, timeout_ms, hdr_name, hdr_value);
  heap_note(label);
  return r;
}

HttpGetResult http_get_raw(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                           int timeout_ms, const char* hdr_name, const char* hdr_value) {
  HttpGetResult r = {};
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = timeout_ms;
  cfg.buffer_size = 2048;
  cfg.disable_auto_redirect = true;
  if (strncmp(url, "https://", 8) == 0) {
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
  }
  if (user != nullptr) {
    cfg.username = user;
    cfg.password = password != nullptr ? password : "";
    cfg.auth_type = HTTP_AUTH_TYPE_BASIC;
  }
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) {
    snprintf(r.error, sizeof(r.error), "client init failed");
    return r;
  }
  if (hdr_name != nullptr && hdr_value != nullptr) {
    esp_http_client_set_header(c, hdr_name, hdr_value);
  }
  esp_err_t err = esp_http_client_open(c, 0);
  if (err != ESP_OK) {
    snprintf(r.error, sizeof(r.error), "%s", esp_err_to_name(err));
    esp_http_client_cleanup(c);
    return r;
  }
  int64_t content_length = esp_http_client_fetch_headers(c);
  r.status = esp_http_client_get_status_code(c);
  if (content_length > 0 && static_cast<size_t>(content_length) > cap) {
    snprintf(r.error, sizeof(r.error), "Response is %lld bytes (limit %u)", content_length,
             static_cast<unsigned>(cap));
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return r;
  }
  size_t total = 0;
  bool overflow = false;
  // timeout_ms bounds connect and each read; this bounds the whole body (a peer
  // that trickles data would otherwise restart the read timeout forever).
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 6000;
  for (;;) {
    if (timeout_ms > 0 && esp_timer_get_time() > deadline) {
      snprintf(r.error, sizeof(r.error), "body timeout");
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
    if (total >= cap) {
      char dummy;
      if (esp_http_client_read(c, &dummy, 1) > 0) overflow = true;
      break;
    }
    int n = esp_http_client_read(c, reinterpret_cast<char*>(body + total), cap - total);
    if (n < 0) {
      snprintf(r.error, sizeof(r.error), "read error");
      esp_http_client_close(c);
      esp_http_client_cleanup(c);
      return r;
    }
    if (n == 0) {
      if (esp_http_client_is_complete_data_received(c) || content_length <= 0) break;
      // no more data although incomplete: connection closed or timeout
      break;
    }
    total += static_cast<size_t>(n);
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  r.length = total;
  if (overflow) {
    snprintf(r.error, sizeof(r.error), "Response exceeded %u bytes", static_cast<unsigned>(cap));
    return r;
  }
  r.ok = true;
  return r;
}

const char* wifi_ssid() { return g_ssid; }

void wifi_scan_cancel(uint32_t gen) {
  uint32_t c = g_scan_cancel.load();
  while (gen > c && !g_scan_cancel.compare_exchange_weak(c, gen)) {
  }
}

namespace {
bool scan_cancelled(uint32_t gen) { return gen != 0 && g_scan_cancel.load() >= gen; }

// One non-blocking scan; waits for SCAN_DONE in 100 ms slices so a cancel stops it.
esp_err_t scan_once(const wifi_scan_config_t& sc, uint32_t gen) {
  xEventGroupClearBits(wifi_events(), kBitScan);
  esp_err_t err = esp_wifi_scan_start(&sc, false);
  if (err != ESP_OK) return err;
  for (int t = 0; t < 150; ++t) {  // 15 s upper bound (a full active scan takes ~2-4 s)
    if (xEventGroupWaitBits(wifi_events(), kBitScan, pdTRUE, pdFALSE, pdMS_TO_TICKS(100)) & kBitScan) return ESP_OK;
    if (scan_cancelled(gen)) break;
  }
  esp_wifi_scan_stop();  // also raises SCAN_DONE
  xEventGroupWaitBits(wifi_events(), kBitScan, pdTRUE, pdFALSE, pdMS_TO_TICKS(500));
  esp_wifi_clear_ap_list();
  return scan_cancelled(gen) ? ESP_ERR_INVALID_STATE : ESP_ERR_TIMEOUT;
}
}  // namespace

int wifi_scan(WifiAp* out, int cap, uint32_t gen) {
  if (scan_cancelled(gen)) return -1;
  wifi_scan_config_t sc = {};
  sc.show_hidden = false;
  // A scan fails while the station is (re)connecting, e.g. to an absent saved
  // network after a failed boot: pause the retries for the scan.
  bool paused = !g_connected;
  if (paused) {
    g_scanning = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  // Retry: right after the pause the driver may still be mid-connect (the scan
  // is refused or comes back empty).
  esp_err_t err = ESP_FAIL;
  uint16_t num = 0;
  EXT_RAM_BSS_ATTR static wifi_ap_record_t recs[32];
  for (int i = 0; i < 5 && !scan_cancelled(gen); ++i) {
    err = scan_once(sc, gen);
    num = 32;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&num, recs);
    if (err == ESP_OK && num > 0) break;
    printf("Wi-Fi scan attempt %d: %s, %u networks\n", i + 1, esp_err_to_name(err), static_cast<unsigned>(num));
    for (int w = 0; w < 7 && !scan_cancelled(gen); ++w) vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (paused) {
    g_scanning = false;
    if (g_ssid[0] != '\0' && !g_hold) esp_wifi_connect();
  }
  if (scan_cancelled(gen)) {
    printf("Wi-Fi scan %lu cancelled\n", static_cast<unsigned long>(gen));
    return -1;
  }
  if (err != ESP_OK) return 0;
  int n = 0;
  for (int i = 0; i < num && n < cap; ++i) {  // records come sorted by RSSI
    const char* ssid = reinterpret_cast<const char*>(recs[i].ssid);
    if (ssid[0] == '\0') continue;
    bool dup = false;
    for (int j = 0; j < n; ++j) {
      if (strcmp(out[j].ssid, ssid) == 0) dup = true;
    }
    if (dup) continue;
    snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", ssid);
    out[n].rssi = recs[i].rssi;
    out[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
    n++;
  }
  return n;
}
