#include "maint.h"

#include <stdio.h>
#include <string.h>

#include <atomic>

#include "clock_sync.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_ota.h"
#include "ota_board.h"
#include "net.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "sys.h"
#include <esp_log.h>
#include "esp_attr.h"

static const char* const TAG = "maint";

namespace {

httpd_handle_t g_server = nullptr;

// --- Access control ---------------------------------------------------------------
// Every endpoint except the page itself needs the 6-digit PIN shown on the panel
// in the X-Panel-Pin header. A custom header makes browsers send a CORS
// preflight (which this server never allows), so other web pages cannot post
// to the panel (CSRF). 5 wrong PINs lock the API for 60 s. The server switches
// itself off 15 min after the last authenticated request.
constexpr uint32_t kIdleOffMs = 15 * 60 * 1000;
std::atomic<uint32_t> g_idle_off_ms{kIdleOffMs};  // test harness may shorten (WEBIDLE)
constexpr uint32_t kLockMs = 60 * 1000;
constexpr uint32_t kGrantMs = 60 * 1000;
constexpr int kMaxFails = 5;
char g_pin[8] = "";
std::atomic<uint32_t> g_last_ms{0};
std::atomic<int> g_fails{0};
std::atomic<uint32_t> g_lock_until{0};
// Secrets export: requested by the browser, confirmed on the panel, then one
// download within kGrantMs.
std::atomic<bool> g_secrets_req{false};
std::atomic<uint32_t> g_secrets_grant_until{0};

uint32_t ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool pin_equal(const char* a, const char* b) {
  // constant time over the PIN length
  size_t n = strlen(b);
  if (strlen(a) != n) return false;
  unsigned d = 0;
  for (size_t i = 0; i < n; ++i) d |= static_cast<unsigned>(a[i] ^ b[i]);
  return d == 0;
}

// true if the request may proceed; otherwise a response was sent.
bool authorized(httpd_req_t* req) {
  uint32_t now = ms();
  if (static_cast<int32_t>(g_lock_until.load() - now) > 0) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    httpd_resp_sendstr(req, "Zu viele falsche PINs - bitte 1 Minute warten");
    return false;
  }
  // Same-origin only (browsers send Origin on cross-site POSTs).
  char origin[96] = "", host[64] = "";
  if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK &&
      httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
    const char* o = strstr(origin, "://");
    if (o == nullptr || strcmp(o + 3, host) != 0) {
      httpd_resp_set_status(req, "403 Forbidden");
      httpd_resp_sendstr(req, "Fremde Herkunft abgelehnt");
      return false;
    }
  }
  char pin[12] = "";
  if (httpd_req_get_hdr_value_str(req, "X-Panel-Pin", pin, sizeof(pin)) != ESP_OK || !pin_equal(pin, g_pin)) {
    int f = ++g_fails;
    if (f >= kMaxFails) {
      g_fails = 0;
      g_lock_until = now + kLockMs;
    }
    console_printf("Web: wrong or missing PIN (%d)\n", f);
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "PIN falsch - PIN steht am Panel unter Einstellungen > Wartung");
    return false;
  }
  g_fails = 0;
  g_last_ms = now;
  return true;
}
std::atomic<int> g_ota_pct{-1};      // written by httpd or net task, read by the UI
std::atomic<bool> g_restart{false};

void restart_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}

void schedule_restart() {
  g_restart = true;
  xTaskCreatePinnedToCore(restart_task, "restart", 2048, nullptr, 1, nullptr, 0);
}

size_t status_json(char* out, size_t n) {
  MaintStatus s;
  maint_collect(&s);
  return snprintf(out, n,
                  "{\"version\":\"%s\",\"built\":\"%s\",\"idf\":\"%s\",\"partition\":\"%s\",\"uptime_s\":%lu,"
                  "\"wifi\":%s,\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"heap_internal\":%lu,"
                  "\"heap_internal_largest\":%lu,\"heap_psram\":%lu,\"ntp_synced\":%s,\"ntp_age_s\":%lu}",
                  s.version, s.built, s.idf, s.partition, static_cast<unsigned long>(s.uptime_s),
                  s.wifi ? "true" : "false", s.ssid, s.rssi, s.ip, static_cast<unsigned long>(s.free_internal),
                  static_cast<unsigned long>(s.largest_internal), static_cast<unsigned long>(s.free_psram),
                  s.ntp_synced ? "true" : "false", static_cast<unsigned long>(s.ntp_age_s));
}

const char kPage[] =
    "<!doctype html><html lang=de><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>esp_display Wartung</title><style>body{font-family:system-ui,sans-serif;background:#0b0f1a;color:#e8ecf4;"
    "max-width:640px;margin:24px auto;padding:0 16px}section{background:#1b2333;border:1px solid #2a3448;"
    "border-radius:16px;padding:16px;margin:16px 0}a,button{color:#7cc4ff}button{background:#131926;border:1px solid "
    "#2a3448;border-radius:10px;padding:8px 14px;font-size:15px}pre{white-space:pre-wrap;font-size:13px}"
    "textarea{width:100%;height:120px;background:#131926;color:#e8ecf4;border-radius:8px}</style></head><body>"
    "<h2>esp_display Wartung</h2><section><h3>Status</h3><pre id=s>...</pre></section>"
    "<section><h3>PIN</h3><input id=pin inputmode=numeric maxlength=6 placeholder='PIN vom Panel'> "
    "<button onclick=savepin()>OK</button></section>"
    "<section><h3>Einstellungen sichern</h3><button onclick=backup(0)>Backup (ohne Geheimnisse)</button> "
    "<button onclick=backup(1)>mit WLAN-Passwort, API-Key und Kalender-URL</button><pre id=br></pre>"
    "<small>Mit Geheimnissen: erst am Panel erlauben, dann erneut klicken.</small></section>"
    "<section><h3>Einstellungen wiederherstellen</h3><input type=file id=rf accept=.json> "
    "<button onclick=restore()>Wiederherstellen</button><pre id=rr></pre></section>"
    "<section><h3>Firmware-Update</h3><input type=file id=ff accept=.bin> <button onclick=upload()>Hochladen und "
    "installieren</button><pre id=fr></pre></section><script>"
    "pin.value=sessionStorage.pin||'';"
    "function H(){return{'X-Panel-Pin':pin.value}}"
    "function savepin(){sessionStorage.pin=pin.value;st()}"
    "async function st(){const r=await fetch('/status',{headers:H()});s.textContent=r.ok?"
    "JSON.stringify(await r.json(),null,1):await r.text()}st();"
    "async function backup(x){br.textContent='...';const r=await fetch('/backup'+(x?'?secrets=1':''),{headers:H()});"
    "if(!r.ok){br.textContent=await r.text();return}const b=await r.blob(),a=document.createElement('a');"
    "a.href=URL.createObjectURL(b);a.download=x?'esp_display-settings-secrets.json':'esp_display-settings.json';"
    "a.click();br.textContent='gespeichert'}"
    "async function restore(){const f=rf.files[0];if(!f)return;rr.textContent='...';const r=await fetch('/restore',"
    "{method:'POST',headers:H(),body:await f.text()});rr.textContent=await r.text()}"
    "async function upload(){const f=ff.files[0];if(!f)return;fr.textContent='Lade hoch ('+f.size+' Bytes) ...';"
    "const r=await fetch('/update',{method:'POST',headers:H(),body:f});fr.textContent=await r.text()}"
    "</script></body></html>";

esp_err_t on_root(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPage, sizeof(kPage) - 1);
}

esp_err_t on_status(httpd_req_t* req) {
  if (!authorized(req)) return ESP_OK;
  char buf[512];
  size_t n = status_json(buf, sizeof(buf));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

esp_err_t on_backup(httpd_req_t* req) {
  if (!authorized(req)) return ESP_OK;
  bool secrets = false;
  char q[32];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    char v[4];
    secrets = httpd_query_key_value(q, "secrets", v, sizeof(v)) == ESP_OK && v[0] == '1';
  }
  if (secrets) {
    uint32_t until = g_secrets_grant_until.exchange(0);  // one download per confirmation
    if (until == 0 || static_cast<int32_t>(until - ms()) <= 0) {
      g_secrets_req = true;
      console_puts("Web: secrets export requested - waiting for the panel");
      httpd_resp_set_status(req, "403 Forbidden");
      return httpd_resp_sendstr(req, "Am Panel erlauben (Dialog), dann innerhalb 1 Minute erneut klicken");
    }
  }
  constexpr size_t kCap = 16 * 1024;
  char* buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return httpd_resp_send_500(req);
  size_t n = settings_export_json(buf, kCap, secrets);
  console_printf("Web: settings backup (%u bytes, secrets=%d)\n", static_cast<unsigned>(n), secrets ? 1 : 0);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=esp_display-settings.json");
  esp_err_t e = httpd_resp_send(req, buf, n);
  heap_caps_free(buf);
  return e;
}

esp_err_t on_restore(httpd_req_t* req) {
  if (!authorized(req)) return ESP_OK;
  constexpr size_t kCap = 16 * 1024;
  if (req->content_len == 0 || req->content_len >= kCap) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, "Leere oder zu grosse Datei");
  }
  char* buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) return httpd_resp_send_500(req);
  size_t got = 0;
  int timeouts = 0;
  while (got < req->content_len) {
    int r = httpd_req_recv(req, buf + got, req->content_len - got);
    if (r <= 0) {
      if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 6) continue;
      heap_caps_free(buf);
      return ESP_FAIL;
    }
    got += static_cast<size_t>(r);
  }
  char report[384];
  int n = settings_import_json(buf, got, report, sizeof(report));
  memset(buf, 0, got);  // may hold secrets
  heap_caps_free(buf);
  console_printf("Web: settings restore -> %d (%s)\n", n, report);  // report: key names only
  if (n < 0) {
    char msg[480];
    snprintf(msg, sizeof(msg), "%s%s",
             n == -2   ? "Datei von neuerer Firmware - bitte erst Firmware aktualisieren. "
             : n == -3 ? "Ungueltige Werte, nichts uebernommen: "
                       : "Keine esp_display-Einstellungsdatei. ",
             report);
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, msg);
  }
  char msg[480];
  snprintf(msg, sizeof(msg), "%d Einstellungen wiederhergestellt. %s%sNeustart ...", n, report[0] ? "Ignoriert: " : "",
           report);
  httpd_resp_sendstr(req, msg);
  schedule_restart();
  return ESP_OK;
}

esp_err_t on_update(httpd_req_t* req) {
  if (!authorized(req)) return ESP_OK;
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (part == nullptr || req->content_len == 0 || req->content_len > part->size) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_sendstr(req, part == nullptr ? "Keine OTA-Partition" : "Falsche Dateigroesse");
  }
  esp_ota_handle_t ota = 0;
  if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) return httpd_resp_send_500(req);
  console_printf("Web: firmware upload %u bytes -> %s\n", static_cast<unsigned>(req->content_len), part->label);
  char* buf = static_cast<char*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    esp_ota_abort(ota);
    return httpd_resp_send_500(req);
  }
  size_t left = req->content_len;
  esp_err_t err = ESP_OK;
  uint8_t hdr[kOtaBoardHeaderBytes];
  size_t hdr_n = 0;
  bool wrong_board = false;
  char board[33] = "";
  g_ota_pct = 0;
  int timeouts = 0;
  while (left > 0 && err == ESP_OK) {
    int r = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
    // A vanished client must not block the (single) httpd task forever.
    if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 6) continue;
    timeouts = 0;
    if (r <= 0) {
      err = ESP_FAIL;
      break;
    }
    if (hdr_n < sizeof(hdr)) {  // board check on the header, before anything can boot
      size_t k = sizeof(hdr) - hdr_n < static_cast<size_t>(r) ? sizeof(hdr) - hdr_n : static_cast<size_t>(r);
      memcpy(hdr + hdr_n, buf, k);
      hdr_n += k;
      if (hdr_n == sizeof(hdr) &&
          ota_board_check_image(hdr, hdr_n, esp_app_get_description()->project_name, board) == kOtaBoardMismatch) {
        wrong_board = true;
        err = ESP_ERR_INVALID_VERSION;
        break;
      }
    }
    err = esp_ota_write(ota, buf, r);
    left -= static_cast<size_t>(r);
    g_ota_pct = static_cast<int>(100 - (left * 100) / req->content_len);
  }
  heap_caps_free(buf);
  if (err == ESP_OK) err = esp_ota_end(ota);
  else esp_ota_abort(ota);
  if (err == ESP_OK) err = esp_ota_set_boot_partition(part);
  g_ota_pct = -1;
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Web: firmware upload failed: %s", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    char msg[120];
    if (wrong_board) {
      console_printf("Web: firmware for board '%s' refused (running '%s')\n", board,
                     esp_app_get_description()->project_name);
      httpd_resp_set_status(req, "400 Bad Request");
      snprintf(msg, sizeof(msg), "Update abgelehnt: Firmware fuer anderes Board (%.40s)", board);
      return httpd_resp_sendstr(req, msg);
    }
    snprintf(msg, sizeof(msg), "Update fehlgeschlagen: %s", err == ESP_ERR_OTA_VALIDATE_FAILED
                                                              ? "Signatur ungueltig oder fehlt (nur signierte Releases)"
                                                              : esp_err_to_name(err));
    return httpd_resp_sendstr(req, msg);
  }
  console_printf("Web: firmware installed in %s, restarting\n", part->label);
  httpd_resp_sendstr(req, "Firmware installiert. Neustart ...");
  schedule_restart();
  return ESP_OK;
}

}  // namespace

void maint_collect(MaintStatus* s) {
  memset(s, 0, sizeof(*s));
  wifi_ap_record_t ap;
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    s->wifi = true;
    s->rssi = ap.rssi;
    snprintf(s->ssid, sizeof(s->ssid), "%s", reinterpret_cast<const char*>(ap.ssid));
  }
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip = {};
  if (netif != nullptr && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
    snprintf(s->ip, sizeof(s->ip), IPSTR, IP2STR(&ip.ip));
  }
  s->uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
  s->free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  s->largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  s->free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const esp_app_desc_t* d = esp_app_get_description();
  snprintf(s->version, sizeof(s->version), "%s", d->version);
  snprintf(s->built, sizeof(s->built), "%.11s %.5s", d->date, d->time);
  snprintf(s->idf, sizeof(s->idf), "%s", d->idf_ver);
  const esp_partition_t* run = esp_ota_get_running_partition();
  snprintf(s->partition, sizeof(s->partition), "%s", run != nullptr ? run->label : "?");
  s->ntp_synced = clock_synced();
  unsigned last = clock_last_sync_s();
  s->ntp_age_s = last > 0 && s->uptime_s >= last ? s->uptime_s - last : 0;
  s->web = g_server != nullptr;
}

namespace {
constexpr uint32_t kSkipValidMagic = 0x5A1DBAD0;
RTC_NOINIT_ATTR uint32_t g_skip_valid;  // survives esp_restart (test harness only)
#ifdef CONFIG_PANEL_TEST_HARNESS
constexpr uint32_t kVerifyMs = 90 * 1000;
#else
constexpr uint32_t kVerifyMs = 10 * 60 * 1000;
#endif
bool pending_verify() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  return run != nullptr && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
}
}  // namespace

void maint_verify_tick() {
  static bool checked = false, pending = false;
  if (!checked) {
    checked = true;
    pending = pending_verify();
    if (pending) console_printf("OTA: new image, must reach Wi-Fi within %lu s\n", (unsigned long)(kVerifyMs / 1000));
  }
  if (!pending) return;
  if (!pending_verify()) {  // marked valid meanwhile
    pending = false;
    return;
  }
  if (now_ms() > kVerifyMs) {
    ESP_LOGE(TAG, "new image not confirmed within %lu s: rolling back", (unsigned long)(kVerifyMs / 1000));
    g_skip_valid = 0;
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

bool maint_test_boot_other(bool skip_valid) {
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (next == nullptr || esp_ota_set_boot_partition(next) != ESP_OK) return false;
  g_skip_valid = skip_valid ? kSkipValidMagic : 0;
  console_printf("Test: booting %s%s\n", next->label, skip_valid ? " (never marked valid)" : "");
  schedule_restart();
  return true;
}

void maint_mark_valid() {
  if (g_skip_valid == kSkipValidMagic) {
    console_puts("OTA: test - not marking this image valid");
    return;
  }
  const esp_partition_t* run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (run != nullptr && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    console_puts("OTA: new image marked valid");
  }
}

int maint_ota_progress() { return g_ota_pct; }

bool maint_ota_from_url(const char* url, char* err, size_t n) {
  TlsGuard tls("ota-install", 0);  // waits for a running fetch, then holds TLS for the download
  esp_http_client_config_t http = {};
  http.url = url;
  http.timeout_ms = 20000;
  http.buffer_size = 4096;
  http.buffer_size_tx = 2048;  // GitHub redirects to long signed URLs
  http.keep_alive_enable = true;
  if (strncmp(url, "https://", 8) == 0) http.crt_bundle_attach = esp_crt_bundle_attach;
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;
  esp_https_ota_handle_t h = nullptr;
  g_ota_pct = 0;
  esp_err_t e = esp_https_ota_begin(&cfg, &h);
  if (e != ESP_OK) {
    snprintf(err, n, "Download: %s", esp_err_to_name(e));
    g_ota_pct = -1;
    return false;
  }
  // Board check before any data is written (esp_https_ota_finish would set
  // the boot partition).
  esp_app_desc_t desc = {};
  e = esp_https_ota_get_img_desc(h, &desc);
  if (e != ESP_OK || !ota_board_same(desc.project_name, esp_app_get_description()->project_name)) {
    char board[33];
    memcpy(board, desc.project_name, 32);
    board[32] = '\0';
    esp_https_ota_abort(h);
    console_printf("OTA: image for board '%s' refused (running '%s')\n", e == ESP_OK ? board : "?",
                   esp_app_get_description()->project_name);
    snprintf(err, n, e == ESP_OK ? "Firmware fuer anderes Board - abgelehnt" : "Update: %s", esp_err_to_name(e));
    g_ota_pct = -1;
    return false;
  }
  int total = esp_https_ota_get_image_size(h);
  for (;;) {
    e = esp_https_ota_perform(h);
    if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
    int got = esp_https_ota_get_image_len_read(h);
    if (total > 0) g_ota_pct = got * 100 / total;
  }
  if (e == ESP_OK && !esp_https_ota_is_complete_data_received(h)) e = ESP_ERR_INVALID_SIZE;
  if (e != ESP_OK) {
    esp_https_ota_abort(h);
    snprintf(err, n, "Update: %s", esp_err_to_name(e));
    g_ota_pct = -1;
    return false;
  }
  e = esp_https_ota_finish(h);
  g_ota_pct = -1;
  if (e != ESP_OK) {
    snprintf(err, n, "%s", e == ESP_ERR_OTA_VALIDATE_FAILED ? "Signatur ungueltig - Update abgelehnt"
                                                            : esp_err_to_name(e));
    return false;
  }
  return true;
}

bool maint_web_start() {
  if (g_server != nullptr) return true;
  snprintf(g_pin, sizeof(g_pin), "%06u", static_cast<unsigned>(esp_random() % 1000000));
  g_fails = 0;
  g_lock_until = 0;
  g_secrets_req = false;
  g_secrets_grant_until = 0;
  g_last_ms = ms();
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.core_id = 0;  // network work on core 0
  // Measured (DIAGSTACK): ~2.8 KB of the old 8 KB stack used. Internal RAM is
  // scarce, so a smaller stack and fewer sockets (one browser tab).
  cfg.stack_size = 5120;
  cfg.max_open_sockets = 3;
  cfg.lru_purge_enable = true;
  if (httpd_start(&g_server, &cfg) != ESP_OK) {
    g_server = nullptr;
    return false;
  }
  const httpd_uri_t uris[] = {
      {"/", HTTP_GET, on_root, nullptr},
      {"/status", HTTP_GET, on_status, nullptr},
      {"/backup", HTTP_GET, on_backup, nullptr},
      {"/restore", HTTP_POST, on_restore, nullptr},
      {"/update", HTTP_POST, on_update, nullptr},
  };
  for (const httpd_uri_t& u : uris) httpd_register_uri_handler(g_server, &u);
  console_puts("Web: maintenance server on port 80");
  return true;
}

void maint_web_stop() {
  if (g_server == nullptr) return;
  httpd_stop(g_server);
  g_server = nullptr;
  g_secrets_req = false;
  g_secrets_grant_until = 0;
  memset(g_pin, 0, sizeof(g_pin));
  console_puts("Web: maintenance server stopped");
}

bool maint_web_running() { return g_server != nullptr; }
bool maint_restart_pending() { return g_restart; }

const char* maint_web_pin() { return g_pin; }

bool maint_web_tick() {
  if (g_server == nullptr) return false;
  if (ms() - g_last_ms.load() > g_idle_off_ms) {
    console_puts("Web: 15 min without access - switching off");
    maint_web_stop();
    return true;
  }
  return false;
}

bool maint_secrets_requested() { return g_secrets_req.load(); }

void maint_secrets_answer(bool allow) {
  g_secrets_req = false;
  g_secrets_grant_until = allow ? ms() + kGrantMs : 0;
  console_printf("Web: secrets export %s on the panel\n", allow ? "allowed" : "denied");
}

void maint_web_set_idle_ms(uint32_t idle_ms) { g_idle_off_ms = idle_ms > 0 ? idle_ms : kIdleOffMs; }
