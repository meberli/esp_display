#include "diag.h"
#include "ota_check.h"
#include "ota_ver.h"
#include "ota_board.h"
#include "ota_channel.h"
#include "ota_github.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "net.h"
#include "sys.h"

namespace {
char g_override[32] = "";


void ascii_line(const char* body, char* out, size_t n) {
  // First meaningful line of GitHub release notes: skip headings, empty lines
  // and the "Full Changelog" link; keep ASCII only (panel fonts are Latin-1).
  out[0] = '\0';
  if (body == nullptr) return;
  const char* p = body;
  while (*p != '\0') {
    const char* e = strchr(p, '\n');
    size_t len = e != nullptr ? static_cast<size_t>(e - p) : strlen(p);
    while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == ' ')) --len;
    const char* q = p;
    while (len > 0 && (*q == ' ' || *q == '*' || *q == '-')) { ++q; --len; }
    bool skip = len == 0 || *p == '#' || strncmp(q, "Full Changelog", 14) == 0 || strncmp(p, "**Full", 6) == 0;
    if (!skip) {
      size_t k = 0;
      for (size_t i = 0; i < len && k + 1 < n; ++i) {
        unsigned char c = static_cast<unsigned char>(q[i]);
        out[k++] = c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
      }
      out[k] = '\0';
      return;
    }
    if (e == nullptr) break;
    p = e + 1;
  }
}

bool check_github(const char* page_url, bool prereleases, const char* skip, OtaCheck* out) {
  // 20 releases with current notes are ~173 KB; the old 96 KiB cap made
  // http_get return HTTP 200 / ok=false and the UI showed "GitHub-Fehler HTTP 200".
  constexpr size_t kCap = 256 * 1024;
  uint8_t* body = static_cast<uint8_t*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (body == nullptr) {
    snprintf(out->err, sizeof(out->err), "Kein Speicher");
    return false;
  }
  char api[192];
  int pages = ota_github::kListPerPage;
  HttpGetResult r = {};
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!ota_github::list_api(page_url, api, sizeof(api), pages)) {
      snprintf(out->err, sizeof(out->err), "Update-URL unbrauchbar");
      heap_caps_free(body);
      return false;
    }
    r = http_get(api, nullptr, nullptr, body, kCap, 10000, "Accept", "application/vnd.github+json");
    if (r.status == 0 && strstr(r.error, "CONNECT") != nullptr) {  // e.g. ESP_ERR_HTTP_CONNECT: one retry
      console_printf("OTA check: %s, retry in 3 s\n", r.error);
      vTaskDelay(pdMS_TO_TICKS(3000));
      r = http_get(api, nullptr, nullptr, body, kCap, 10000, "Accept", "application/vnd.github+json");
    }
    if (r.ok && r.status == 200) break;
    if (attempt == 0 && ota_github::body_too_big(r.error) && pages > 3) {
      console_printf("OTA check: list is %s, retry with per_page=3\n", r.error);
      pages = 3;
      continue;
    }
    break;
  }
  ota_github::list_http_err(r.status, r.ok, r.error, out->err, sizeof(out->err));
  diag_svc(kSvcOta, r.ok && r.status == 200, r.status);
  console_printf("OTA check %s: HTTP %d, %u bytes%s%s\n", api, r.status, static_cast<unsigned>(r.length),
                 r.error[0] != '\0' ? " " : "", r.error);
  if (out->err[0] != '\0') {
    heap_caps_free(body);
    return false;
  }
  StaticJsonDocument<256> filter;
  filter[0]["tag_name"] = true;
  filter[0]["prerelease"] = true;
  filter[0]["draft"] = true;
  filter[0]["assets"][0]["name"] = true;
  filter[0]["assets"][0]["browser_download_url"] = true;
  DynamicJsonDocument doc(24576);
  DeserializationError e = deserializeJson(doc, reinterpret_cast<const char*>(body), r.length,
                                           DeserializationOption::Filter(filter));
  heap_caps_free(body);
  if (e || !doc.is<JsonArray>()) {
    snprintf(out->err, sizeof(out->err), "Antwort unlesbar (%s)", e ? e.c_str() : "kein Array");
    return false;
  }
  char asset[48];
  ota_board_asset(esp_app_get_description()->project_name, asset, sizeof(asset));
  constexpr int kMax = 20;
  ota_channel::Release rels[kMax];
  char vers[kMax][32];
  char urls[kMax][256];
  int n = 0;
  for (JsonObject a : doc.as<JsonArray>()) {
    if (n >= kMax) break;
    const char* tag = a["tag_name"] | "";
    if (tag[0] == '\0') continue;
    snprintf(vers[n], sizeof(vers[n]), "%s", (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag);
    urls[n][0] = '\0';
    for (JsonObject as : a["assets"].as<JsonArray>()) {
      const char* name = as["name"] | "";
      if (strcmp(name, asset) == 0) snprintf(urls[n], sizeof(urls[n]), "%s", as["browser_download_url"] | "");
    }
    rels[n] = {vers[n], a["prerelease"] | false, a["draft"] | false, urls[n]};
    ++n;
  }
  int i = ota_channel::pick(rels, n, prereleases, ota_running_version(), skip);
  if (i >= 0) {
    snprintf(out->version, sizeof(out->version), "%s", rels[i].version);
    snprintf(out->url, sizeof(out->url), "%s", rels[i].url);
    return true;
  }
  // Nothing newer to install: still report the newest channel-matching release
  // so the UI can say "up to date" or "no image for this board".
  int shown = -1;
  for (int k = 0; k < n; ++k) {
    if (rels[k].draft) continue;
    if (!prereleases && rels[k].prerelease) continue;
    if (shown < 0 || ota_ver::cmp(rels[k].version, rels[shown].version) > 0) shown = k;
  }
  if (shown < 0) {
    snprintf(out->err, sizeof(out->err), "Kein Release gefunden");
    return false;
  }
  snprintf(out->version, sizeof(out->version), "%s", rels[shown].version);
  if (ota_ver::offer_cmp(rels[shown].version, ota_running_version()) > 0) {
    if (rels[shown].url[0] == '\0') {
      console_printf("OTA check: release v%s has no %s\n", out->version, asset);
    } else if (ota_channel::skip_has(skip, rels[shown].version)) {
      // Failed this version already: do not offer it again.
      console_printf("OTA check: skipping failed v%s\n", out->version);
      snprintf(out->version, sizeof(out->version), "%s", ota_running_version());
    }
  }
  return true;
}

// Version from the image itself: esp_image_header (24) + segment header (8)
// + esp_app_desc_t (magic, ..., version[32] at +16).
bool check_image(const char* url, OtaCheck* out) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.timeout_ms = 10000;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 2048;  // long redirect URLs
  if (strncmp(url, "https://", 8) == 0) cfg.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) {
    snprintf(out->err, sizeof(out->err), "HTTP-Client-Fehler");
    return false;
  }
  esp_http_client_set_header(c, "Range", "bytes=0-255");
  uint8_t buf[256];
  int got = 0;
  int status = 0;
  esp_err_t err = ESP_OK;
  for (int hop = 0; hop < 5; ++hop) {  // follow redirects (GitHub -> CDN)
    err = esp_http_client_open(c, 0);
    if (err != ESP_OK) break;
    esp_http_client_fetch_headers(c);
    status = esp_http_client_get_status_code(c);
    if (status >= 300 && status < 400) {
      esp_http_client_set_redirection(c);
      esp_http_client_close(c);
      continue;
    }
    while (got < static_cast<int>(sizeof(buf))) {
      int n = esp_http_client_read(c, reinterpret_cast<char*>(buf + got), sizeof(buf) - got);
      if (n <= 0) break;
      got += n;
    }
    break;
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  console_printf("OTA check image %s: HTTP %d, %d bytes\n", url, status, got);
  if (err != ESP_OK) {
    snprintf(out->err, sizeof(out->err), "Keine Verbindung (%s)", esp_err_to_name(err));
    return false;
  }
  if (status != 200 && status != 206) {
    snprintf(out->err, sizeof(out->err), "Update-URL: HTTP %d", status);
    return false;
  }
  uint32_t magic = got >= 36 ? (buf[32] | buf[33] << 8 | buf[34] << 16 | static_cast<uint32_t>(buf[35]) << 24) : 0;
  if (got < 80 || buf[0] != 0xE9 || magic != 0xABCD5432) {
    snprintf(out->err, sizeof(out->err), "Keine Firmware unter der Update-URL");
    return false;
  }
  char board[33];
  if (ota_board_check_image(buf, static_cast<size_t>(got), esp_app_get_description()->project_name, board) !=
      kOtaBoardOk) {
    console_printf("OTA check: image is for board '%s', running '%s'\n", board,
                   esp_app_get_description()->project_name);
    snprintf(out->err, sizeof(out->err), "Firmware fuer anderes Board (%.40s)", board);
    return false;
  }
  char v[33];
  memcpy(v, buf + 48, 32);
  v[32] = '\0';
  snprintf(out->version, sizeof(out->version), "%s", (v[0] == 'v' || v[0] == 'V') ? v + 1 : v);
  snprintf(out->url, sizeof(out->url), "%s", url);
  return true;
}
}  // namespace

int ota_ver_cmp(const char* a, const char* b) { return ota_ver::cmp(a, b); }

const char* ota_running_version() {
  return g_override[0] != '\0' ? g_override : esp_app_get_description()->version;
}

void ota_set_version_override(const char* v) {
  snprintf(g_override, sizeof(g_override), "%s", v != nullptr ? v : "");
}

bool ota_check(const char* cfg_url, bool own, bool prereleases, const char* skip, OtaCheck* out) {
  memset(out, 0, sizeof(*out));
  TlsGuard tls("ota-check");
  if (!tls.ok) {
    snprintf(out->err, sizeof(out->err), "Netz belegt");
    return false;
  }
  char api[192];
  bool ok = (!own && ota_github::list_api(cfg_url, api, sizeof(api)))
                ? check_github(cfg_url, prereleases, skip, out)
                : check_image(cfg_url, out);
  out->ok = ok;
  if (ok) out->cmp = ota_ver::offer_cmp(out->version, ota_running_version());
  console_printf("OTA check: running %s, offered %s -> %s\n", ota_running_version(), ok ? out->version : "-",
                 !ok ? out->err : out->url[0] == '\0' ? "no image for this board" : out->cmp > 0 ? "update available" : out->cmp == 0 ? "up to date" : "older");
  return ok;
}
