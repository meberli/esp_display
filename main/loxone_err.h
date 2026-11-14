// Short German messages for a Miniserver login test (Testen).
// Never include credentials. Header-only for tools/test_loxone_login.cpp.
#pragma once

#include <cstdio>
#include <cstring>

namespace loxone_err {

// status: HTTP status, or <= 0 if no HTTP response.
// transport: esp_http error name or short English from http_get; may be empty.
inline void test_msg(int status, const char* transport, char* out, size_t n) {
  if (out == nullptr || n == 0) return;
  out[0] = '\0';
  if (status == 401 || status == 403) {
    snprintf(out, n, "Benutzer oder Passwort falsch (%d)", status);
    return;
  }
  if (status > 0) {
    snprintf(out, n, "Server antwortet mit HTTP %d", status);
    return;
  }
  const char* e = transport != nullptr ? transport : "";
  if (strstr(e, "HTTPS") != nullptr || strstr(e, "https") != nullptr) {
    snprintf(out, n, "HTTPS wird nicht unterst\xC3\xBCtzt - http-Adresse oder IPv4 eingeben");
    return;
  }
  if (e[0] != '\0' && (strstr(e, "imeout") != nullptr || strstr(e, "IMEOUT") != nullptr ||
                       strstr(e, "EAGAIN") != nullptr || strstr(e, "body timeout") != nullptr)) {
    snprintf(out, n, "Zeit\xC3\xBC" "berschreitung");
    return;
  }
  if (e[0] == '\0' || strstr(e, "CONNECT") != nullptr || strstr(e, "connect") != nullptr ||
      strstr(e, "ESP_FAIL") != nullptr || strstr(e, "NOT_FOUND") != nullptr) {
    snprintf(out, n, "nicht erreichbar");
    return;
  }
  // Transport text is an error name (no credentials). Keep it short and German.
  snprintf(out, n, "nicht erreichbar (%s)", e);
}

}  // namespace loxone_err
