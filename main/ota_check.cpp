#include "ota_check.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "net.h"
#include "sys.h"

namespace {
char g_override[32] = "";

struct Ver {
  long n[3];
  bool suffix;
};

Ver parse(const char* s) {
  Ver v = {{0, 0, 0}, false};
  if (s == nullptr) return v;
  if (*s == 'v' || *s == 'V') ++s;
  for (int i = 0; i < 3; ++i) {
    char* end = nullptr;
    v.n[i] = strtol(s, &end, 10);
    s = end;
    if (*s != '.') break;
    ++s;
  }
  v.suffix = *s == '-' || *s == '+';
  return v;
}

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

// "https://github.com/<owner>/<repo>/releases/..." -> API URL of the latest release.
bool github_api(const char* url, char* api, size_t n) {
  const char* k = "https://github.com/";
  if (strncmp(url, k, strlen(k)) != 0) return false;
  const char* o = url + strlen(k);
  const char* s1 = strchr(o, '/');
  if (s1 == nullptr) return false;
  const char* s2 = strchr(s1 + 1, '/');
  if (s2 == nullptr || strncmp(s2, "/releases/", 10) != 0) return false;
  snprintf(api, n, "https://api.github.com/repos/%.*s/releases/latest", static_cast<int>(s2 - o), o);
  return true;
}

bool check_github(const char* api, OtaCheck* out) {
  constexpr size_t kCap = 48 * 1024;
  uint8_t* body = static_cast<uint8_t*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (body == nullptr) {
    snprintf(out->err, sizeof(out->err), "Kein Speicher");
    return false;
  }
  HttpGetResult r = http_get(api, nullptr, nullptr, body, kCap, 10000, "Accept", "application/vnd.github+json");
  if (r.status == 403 || r.status == 429) {
    snprintf(out->err, sizeof(out->err), "GitHub-Limit erreicht, sp\xC3\xA4ter erneut pr\xC3\xBC" "fen");
  } else if (r.status == 404) {
    snprintf(out->err, sizeof(out->err), "Kein Release gefunden");
  } else if (r.status == 0) {
    snprintf(out->err, sizeof(out->err), "Keine Verbindung zu GitHub (%s)", r.error);
  } else if (!r.ok || r.status != 200) {
    snprintf(out->err, sizeof(out->err), "GitHub-Fehler HTTP %d", r.status);
  }
  console_printf("OTA check %s: HTTP %d, %u bytes\n", api, r.status, static_cast<unsigned>(r.length));
  if (out->err[0] != '\0') {
    heap_caps_free(body);
    return false;
  }
  StaticJsonDocument<256> filter;
  filter["tag_name"] = true;
  filter["body"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  DynamicJsonDocument doc(16384);
  DeserializationError e = deserializeJson(doc, reinterpret_cast<const char*>(body), r.length,
                                           DeserializationOption::Filter(filter));
  heap_caps_free(body);
  if (e) {
    snprintf(out->err, sizeof(out->err), "Antwort unlesbar (%s)", e.c_str());
    return false;
  }
  const char* tag = doc["tag_name"] | "";
  if (tag[0] == '\0') {
    snprintf(out->err, sizeof(out->err), "Release ohne Version");
    return false;
  }
  snprintf(out->version, sizeof(out->version), "%s", (tag[0] == 'v' || tag[0] == 'V') ? tag + 1 : tag);
  for (JsonObject a : doc["assets"].as<JsonArray>()) {
    const char* name = a["name"] | "";
    if (strcmp(name, "loxone_panel.bin") == 0) snprintf(out->url, sizeof(out->url), "%s", a["browser_download_url"] | "");
  }
  if (out->url[0] == '\0') {
    snprintf(out->err, sizeof(out->err), "Release v%s ohne loxone_panel.bin", out->version);
    return false;
  }
  ascii_line(doc["body"] | "", out->notes, sizeof(out->notes));
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
  char v[33];
  memcpy(v, buf + 48, 32);
  v[32] = '\0';
  snprintf(out->version, sizeof(out->version), "%s", (v[0] == 'v' || v[0] == 'V') ? v + 1 : v);
  snprintf(out->url, sizeof(out->url), "%s", url);
  return true;
}
}  // namespace

int ota_ver_cmp(const char* a, const char* b) {
  Ver x = parse(a), y = parse(b);
  for (int i = 0; i < 3; ++i) {
    if (x.n[i] != y.n[i]) return x.n[i] < y.n[i] ? -1 : 1;
  }
  if (x.suffix != y.suffix) return x.suffix ? -1 : 1;
  return 0;
}

const char* ota_running_version() {
  return g_override[0] != '\0' ? g_override : esp_app_get_description()->version;
}

void ota_set_version_override(const char* v) {
  snprintf(g_override, sizeof(g_override), "%s", v != nullptr ? v : "");
}

bool ota_check(const char* cfg_url, bool own, OtaCheck* out) {
  memset(out, 0, sizeof(*out));
  TlsGuard tls("ota-check");
  if (!tls.ok) {
    snprintf(out->err, sizeof(out->err), "Netz belegt");
    return false;
  }
  char api[160];
  bool ok = (!own && github_api(cfg_url, api, sizeof(api))) ? check_github(api, out) : check_image(cfg_url, out);
  out->ok = ok;
  if (ok) out->cmp = ota_ver_cmp(out->version, ota_running_version());
  console_printf("OTA check: running %s, offered %s -> %s\n", ota_running_version(), ok ? out->version : "-",
                 !ok ? out->err : out->cmp > 0 ? "update available" : out->cmp == 0 ? "up to date" : "older");
  return ok;
}
