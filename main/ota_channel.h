// OTA channel selection (stable vs beta) and skipped-version list.
// Header-only so tools/test_ota_channel.cpp tests the real code.
#pragma once
#include "ota_ver.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ota_channel {

constexpr int kSkipMax = 8;
constexpr size_t kVerMax = 31;

struct Release {
  const char* version;  // tag or version; a leading 'v' is fine
  bool prerelease;
  bool draft;
  const char* url;  // board asset URL; empty if this board has no image
};

inline void strip_v(const char* s, char* out, size_t n) {
  if (n == 0) return;
  if (s == nullptr) {
    out[0] = '\0';
    return;
  }
  if (*s == 'v' || *s == 'V') ++s;
  snprintf(out, n, "%s", s);
}

inline bool ver_same(const char* a, const char* b) { return ota_ver::cmp(a, b) == 0; }

inline bool skip_has(const char* list, const char* ver) {
  if (list == nullptr || ver == nullptr || ver[0] == '\0') return false;
  char want[32];
  strip_v(ver, want, sizeof(want));
  const char* p = list;
  while (*p != '\0') {
    while (*p == ',' || *p == ' ') ++p;
    if (*p == '\0') break;
    const char* e = p;
    while (*e != '\0' && *e != ',') ++e;
    char have[32];
    size_t n = static_cast<size_t>(e - p);
    if (n >= sizeof(have)) n = sizeof(have) - 1;
    memcpy(have, p, n);
    have[n] = '\0';
    if (ver_same(have, want)) return true;
    p = e;
  }
  return false;
}

// Append ver to a comma-separated list. Duplicates are ignored. Keeps at most
// kSkipMax entries, dropping the oldest. list may be empty.
inline void skip_add(char* list, size_t n, const char* ver) {
  if (list == nullptr || n == 0 || ver == nullptr || ver[0] == '\0') return;
  char add[32];
  strip_v(ver, add, sizeof(add));
  if (add[0] == '\0' || skip_has(list, add)) return;
  char parts[kSkipMax][32];
  int count = 0;
  const char* p = list;
  while (*p != '\0' && count < kSkipMax) {
    while (*p == ',' || *p == ' ') ++p;
    if (*p == '\0') break;
    const char* e = p;
    while (*e != '\0' && *e != ',') ++e;
    size_t len = static_cast<size_t>(e - p);
    if (len >= sizeof(parts[0])) len = sizeof(parts[0]) - 1;
    memcpy(parts[count], p, len);
    parts[count][len] = '\0';
    if (parts[count][0] != '\0') ++count;
    p = e;
  }
  if (count == kSkipMax) {
    for (int i = 0; i < kSkipMax - 1; ++i) memcpy(parts[i], parts[i + 1], sizeof(parts[0]));
    --count;
  }
  snprintf(parts[count], sizeof(parts[count]), "%s", add);
  ++count;
  size_t w = 0;
  list[0] = '\0';
  for (int i = 0; i < count; ++i) {
    int m = snprintf(list + w, n - w, "%s%s", i ? "," : "", parts[i]);
    if (m < 0 || static_cast<size_t>(m) >= n - w) {
      list[w] = '\0';
      break;
    }
    w += static_cast<size_t>(m);
  }
}

// Index of the newest acceptable release, or -1. beta=false keeps only
// non-prerelease (stable) entries. Drafts, missing assets, skipped versions
// and anything not newer than `running` (ota_ver::offer_cmp) are ignored.
inline int pick(const Release* r, int n, bool beta, const char* running, const char* skip_list) {
  int best = -1;
  for (int i = 0; i < n; ++i) {
    if (r[i].draft) continue;
    if (!beta && r[i].prerelease) continue;
    if (r[i].url == nullptr || r[i].url[0] == '\0') continue;
    if (r[i].version == nullptr || r[i].version[0] == '\0') continue;
    if (ota_ver::offer_cmp(r[i].version, running) <= 0) continue;
    if (skip_has(skip_list, r[i].version)) continue;
    if (best < 0 || ota_ver::cmp(r[i].version, r[best].version) > 0) best = i;
  }
  return best;
}

}  // namespace ota_channel
