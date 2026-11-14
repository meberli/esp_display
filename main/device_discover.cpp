#include "device_discover.h"

#include <stdio.h>
#include <string.h>

#include "energy.h"

namespace {

bool looks_like_html(const char* body, size_t len) {
  if (body == nullptr || len < 5) return false;
  for (size_t i = 0; i + 4 < len && i < 64; ++i) {
    if (body[i] == '<' && (body[i + 1] == '!' || body[i + 1] == 'h' || body[i + 1] == 'H')) return true;
  }
  return false;
}

// Copy the value of 'key' (single- or double-quoted, or unquoted token) into
// out. The Loxone /jdev/cfg/api "value" field is often a Python-like dict
// inside a string: {'snr':'ABC','version':'14.2.3.24'}.
bool extract_field(const char* body, size_t len, const char* key, char* out, size_t out_n) {
  if (out_n == 0) return false;
  out[0] = '\0';
  if (body == nullptr || key == nullptr || key[0] == '\0') return false;
  const char* end = body + len;
  const size_t klen = strlen(key);
  const char* p = body;
  while (p + klen + 2 < end) {
    if ((p == body || p[-1] == '\'' || p[-1] == '"' || p[-1] == '{' || p[-1] == ',' || p[-1] == ' ') &&
        strncmp(p, key, klen) == 0) {
      const char* q = p + klen;
      while (q < end && (*q == '\'' || *q == '"' || *q == ' ')) ++q;
      if (q >= end || *q != ':') {
        ++p;
        continue;
      }
      ++q;
      while (q < end && (*q == ' ')) ++q;
      char quote = 0;
      if (q < end && (*q == '\'' || *q == '"')) quote = *q++;
      size_t i = 0;
      while (q < end && i + 1 < out_n) {
        if (quote != 0 && *q == quote) break;
        if (quote == 0 && (*q == ',' || *q == '}' || *q == ' ' || *q == '\n' || *q == '\r')) break;
        out[i++] = *q++;
      }
      out[i] = '\0';
      return i > 0;
    }
    ++p;
  }
  return false;
}

}  // namespace

bool discover_is_loxone_api(const char* body, size_t len, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (body == nullptr || len == 0 || looks_like_html(body, len)) return false;
  // Public unauthenticated endpoints: /jdev/cfg/api and /jdev/cfg/apiKey.
  bool marker = false;
  if (strstr(body, "dev/cfg/api") != nullptr || strstr(body, "hashAlg") != nullptr ||
      strstr(body, "httpsStatus") != nullptr)
    marker = true;
  char snr[24] = "";
  char ver[24] = "";
  extract_field(body, len, "snr", snr, sizeof(snr));
  extract_field(body, len, "version", ver, sizeof(ver));
  if (!marker && snr[0] == '\0') return false;
  if (!marker && strstr(body, "snr") == nullptr) return false;
  if (snr[0] == '\0' && strstr(body, "snr") == nullptr && !marker) return false;
  // Require at least one Loxone-specific token so a random JSON with "version"
  // is not treated as a Miniserver.
  if (!marker && snr[0] == '\0') return false;
  if (name != nullptr && name_n > 0) {
    if (snr[0] != '\0' && ver[0] != '\0')
      snprintf(name, name_n, "Miniserver %s", snr);
    else if (snr[0] != '\0')
      snprintf(name, name_n, "Miniserver %s", snr);
    else
      snprintf(name, name_n, "Miniserver");
  }
  return true;
}

bool discover_is_whatwatt_report(const char* body, size_t len, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (body == nullptr || len == 0 || looks_like_html(body, len)) return false;
  EnergyReading r = {};
  if (!energy_parse_report(body, len, 0, &r)) return false;
  if (name != nullptr && name_n > 0) {
    if (r.meter_id[0] != '\0' && strcmp(r.meter_id, "00000000") != 0)
      snprintf(name, name_n, "whatwatt Go %s", r.meter_id);
    else
      snprintf(name, name_n, "whatwatt Go");
  }
  return true;
}

void discover_format_host(char* out, size_t n, const char* ip, uint16_t port) {
  if (out == nullptr || n == 0) return;
  if (ip == nullptr || ip[0] == '\0') {
    out[0] = '\0';
    return;
  }
  if (port == 0 || port == 80) snprintf(out, n, "%s", ip);
  else snprintf(out, n, "%s:%u", ip, static_cast<unsigned>(port));
}

int discover_hosts(uint32_t ip_host, uint32_t mask_host, uint32_t self_host, uint32_t* out, int cap) {
  if (out == nullptr || cap <= 0 || mask_host == 0) return 0;
  int prefix = 0;
  for (int i = 0; i < 32; ++i) {
    if (mask_host & (1u << (31 - i))) ++prefix;
    else break;
  }
  uint32_t net, bcast;
  if (prefix < 23) {
    net = self_host & 0xffffff00u;
    bcast = net | 0x000000ffu;
  } else {
    net = ip_host & mask_host;
    bcast = net | ~mask_host;
  }
  int n = 0;
  if (bcast <= net + 1) return 0;
  for (uint32_t h = net + 1; h < bcast && n < cap; ++h) {
    if (h == self_host) continue;
    out[n++] = h;
  }
  return n;
}

int discover_add_hit(DiscoverHit* hits, int n, int cap, const DiscoverHit& hit) {
  if (hits == nullptr || n < 0 || cap <= 0 || hit.host[0] == '\0') return n;
  if (n >= cap) return n;
  for (int i = 0; i < n; ++i) {
    if (strcmp(hits[i].host, hit.host) == 0) return n;
  }
  hits[n] = hit;
  return n + 1;
}
