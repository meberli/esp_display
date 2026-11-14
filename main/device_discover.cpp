#include "device_discover.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

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


bool contains_ci(const char* hay, const char* needle) {
  if (hay == nullptr || needle == nullptr || needle[0] == '\0') return false;
  for (const char* p = hay; *p != '\0'; ++p) {
    const char* a = p;
    const char* b = needle;
    while (*a != '\0' && *b != '\0' &&
           tolower(static_cast<unsigned char>(*a)) == tolower(static_cast<unsigned char>(*b))) {
      ++a;
      ++b;
    }
    if (*b == '\0') return true;
  }
  return false;
}

bool starts_ci(const char* s, const char* pre) {
  if (s == nullptr || pre == nullptr) return false;
  while (*s && *pre &&
         tolower(static_cast<unsigned char>(*s)) == tolower(static_cast<unsigned char>(*pre))) {
    ++s;
    ++pre;
  }
  return *pre == '\0';
}

void name_from_id_suffix(char* name, size_t name_n, const char* id) {
  if (name == nullptr || name_n == 0) return;
  if (id == nullptr || id[0] == '\0') {
    snprintf(name, name_n, "whatwatt Go");
    return;
  }
  size_t n = strlen(id);
  const char* tail = n > 6 ? id + (n - 6) : id;
  snprintf(name, name_n, "whatwatt Go %s", tail);
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

bool discover_is_whatwatt_system(const char* body, size_t len, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (body == nullptr || len == 0 || looks_like_html(body, len)) return false;
  // Official /api/v1/system: {"system":{"id":"...","boot_id":"...","time_since_boot":...}, ...}
  if (strstr(body, "boot_id") == nullptr && strstr(body, "time_since_boot") == nullptr &&
      strstr(body, "\"system\"") == nullptr)
    return false;
  if (strstr(body, "boot_id") == nullptr && strstr(body, "time_since_boot") == nullptr &&
      !contains_ci(body, "whatwatt"))
    return false;  // a bare "system" key is too common
  if (name != nullptr && name_n > 0) {
    char id[24] = "";
    extract_field(body, len, "id", id, sizeof(id));
    name_from_id_suffix(name, name_n, id);
  }
  return true;
}

bool discover_is_whatwatt(const char* body, size_t len, char* name, size_t name_n) {
  if (discover_is_whatwatt_system(body, len, name, name_n)) return true;
  if (discover_is_whatwatt_report(body, len, name, name_n)) return true;
  // Loose report: a "report" object plus a "meter" object, even if the exact
  // energy schema does not parse (truncated body, license-limited payload).
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (body == nullptr || len == 0 || looks_like_html(body, len)) return false;
  if (strstr(body, "\"report\"") == nullptr || strstr(body, "\"meter\"") == nullptr) return false;
  if (name != nullptr && name_n > 0) snprintf(name, name_n, "whatwatt Go");
  return true;
}

bool discover_mdns_is_whatwatt(const char* hostname, const char* instance, const char* txt_type,
                               const char* txt_id, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  const bool by_host = starts_ci(hostname, "whatwatt");
  const bool by_inst = contains_ci(instance, "whatwatt");
  const bool by_type = txt_type != nullptr && strcmp(txt_type, "100") == 0;
  if (!by_host && !by_inst && !by_type) return false;
  if (name != nullptr && name_n > 0) {
    if (txt_id != nullptr && txt_id[0] != '\0') name_from_id_suffix(name, name_n, txt_id);
    else if (hostname != nullptr && starts_ci(hostname, "whatwatt-") && strlen(hostname) > 9) {
      char tail[24];
      snprintf(tail, sizeof(tail), "%s", hostname + 9);
      if (char* dot = strchr(tail, '.')) *dot = '\0';
      if (tail[0] != '\0') snprintf(name, name_n, "whatwatt Go %s", tail);
      else snprintf(name, name_n, "whatwatt Go");
    }
    else
      snprintf(name, name_n, "whatwatt Go");
  }
  return true;
}

bool discover_mdns_is_loxone(const char* hostname, const char* instance, char* name, size_t name_n) {
  if (name != nullptr && name_n > 0) name[0] = '\0';
  if (!contains_ci(hostname, "miniserver") && !contains_ci(hostname, "loxone") &&
      !contains_ci(instance, "miniserver") && !contains_ci(instance, "loxone"))
    return false;
  if (name != nullptr && name_n > 0) snprintf(name, name_n, "Miniserver");
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
