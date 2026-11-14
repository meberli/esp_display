// GitHub release-list URL and error text for OTA checks.
// Header-only so tools/test_ota_github.cpp tests the real code.
// The list JSON for per_page=20 grew past the old 96 KiB body cap
// (172691 bytes on 14 Nov 2026) and 0.9.14 then showed "GitHub-Fehler HTTP 200".
#pragma once

#include <cstdio>
#include <cstring>

namespace ota_github {

// Newest releases are enough to find an update; keep this modest so the
// JSON stays well under the PSRAM body cap even when release notes are long.
constexpr int kListPerPage = 8;

// "https://github.com/<owner>/<repo>/releases/..." -> API URL of the release list.
inline bool list_api(const char* url, char* api, size_t n, int per_page = kListPerPage) {
  if (api != nullptr && n > 0) api[0] = '\0';
  if (url == nullptr || api == nullptr || n < 16) return false;
  if (per_page < 1) per_page = 1;
  if (per_page > 20) per_page = 20;
  const char* k = "https://github.com/";
  if (strncmp(url, k, strlen(k)) != 0) return false;
  const char* o = url + strlen(k);
  const char* s1 = strchr(o, '/');
  if (s1 == nullptr) return false;
  const char* s2 = strchr(s1 + 1, '/');
  if (s2 == nullptr || strncmp(s2, "/releases/", 10) != 0) return false;
  snprintf(api, n, "https://api.github.com/repos/%.*s/releases?per_page=%d", static_cast<int>(s2 - o), o, per_page);
  return true;
}

inline bool body_too_big(const char* transport) {
  const char* e = transport != nullptr ? transport : "";
  return strstr(e, "bytes") != nullptr || strstr(e, "limit") != nullptr || strstr(e, "exceeded") != nullptr;
}

// German message for a failed list fetch. Never uses "HTTP 200" as the cause
// when the status was 200 but the body could not be used (too large, timeout).
inline void list_http_err(int status, bool ok, const char* transport, char* out, size_t n) {
  if (out == nullptr || n == 0) return;
  out[0] = '\0';
  if (ok && status == 200) return;
  const char* e = transport != nullptr ? transport : "";
  if (status == 403 || status == 429) {
    snprintf(out, n, "GitHub-Limit erreicht, sp\xC3\xA4ter erneut pr\xC3\xBC" "fen");
    return;
  }
  if (status == 404) {
    snprintf(out, n, "Kein Release gefunden");
    return;
  }
  if (status == 0) {
    snprintf(out, n, "Keine Verbindung zu GitHub (%s)", e[0] != '\0' ? e : "Fehler");
    return;
  }
  if (!ok) {
    if (body_too_big(e)) {
      snprintf(out, n, "GitHub-Antwort zu gross");
      return;
    }
    if (strstr(e, "imeout") != nullptr || strstr(e, "TIMEOUT") != nullptr) {
      snprintf(out, n, "Zeit\xC3\xBC" "berschreitung bei GitHub");
      return;
    }
    snprintf(out, n, "GitHub-Antwort unbrauchbar");
    return;
  }
  snprintf(out, n, "GitHub-Fehler HTTP %d", status);
}

}  // namespace ota_github
