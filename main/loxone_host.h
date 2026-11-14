// Parse a Miniserver address as entered on the panel or stored in NVS.
// Header-only so tools/test_loxone_login.cpp tests the real code.
// Accepts "192.168.1.10", "host.local:8080", "http://host:8080/path".
// Strips a trailing :port from the host so the HTTP URL is never
// "http://192.168.1.10:8080:8080/...". HTTPS is rejected (not implemented).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace loxone_host {

inline void trim_copy(const char* s, char* out, size_t n) {
  if (out == nullptr || n == 0) return;
  out[0] = '\0';
  if (s == nullptr) return;
  while (*s == ' ' || *s == '\t') ++s;
  snprintf(out, n, "%s", s);
  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t' || out[len - 1] == '\r')) out[--len] = '\0';
}

// host receives only the hostname/IP (no scheme, path or port). port is 80
// unless the input contains a valid :port. err is a short German message.
inline bool parse(const char* in, char* host, size_t host_n, uint16_t* port, char* err, size_t err_n,
                  uint16_t default_port = 80) {
  if (host != nullptr && host_n > 0) host[0] = '\0';
  if (port != nullptr) *port = 80;
  auto fail = [&](const char* m) {
    if (err != nullptr && err_n > 0) snprintf(err, err_n, "%s", m);
    return false;
  };
  if (host == nullptr || host_n < 2 || port == nullptr) return fail("Adresse fehlt");
  char buf[96];
  trim_copy(in, buf, sizeof(buf));
  if (strncmp(buf, "https://", 8) == 0) {
    return fail("HTTPS wird nicht unterst\xC3\xBCtzt - http-Adresse oder IPv4 eingeben");
  }
  if (strncmp(buf, "http://", 7) == 0) memmove(buf, buf + 7, strlen(buf + 7) + 1);
  char* slash = strchr(buf, '/');
  if (slash != nullptr) *slash = '\0';
  size_t n = strlen(buf);
  while (n > 0 && buf[n - 1] == ' ') buf[--n] = '\0';
  uint16_t p = default_port != 0 ? default_port : 80;
  char* colon = strrchr(buf, ':');
  if (colon != nullptr) {
    char* end = nullptr;
    long v = strtol(colon + 1, &end, 10);
    if (end == colon + 1 || *end != '\0' || v < 1 || v > 65535) {
      return fail("Port ung\xC3\xBCltig (1-65535)");
    }
    p = static_cast<uint16_t>(v);
    *colon = '\0';
  }
  if (buf[0] == '\0') return fail("Adresse fehlt");
  if (strlen(buf) >= host_n) return fail("Adresse zu lang");
  snprintf(host, host_n, "%s", buf);
  *port = p;
  if (err != nullptr && err_n > 0) err[0] = '\0';
  return true;
}

}  // namespace loxone_host
