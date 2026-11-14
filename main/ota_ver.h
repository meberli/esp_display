// Firmware version comparison, header-only so tools/test_ota_ver.cpp tests the real code.
// Semver 2.0.0 for X.Y.Z and optional pre-release identifiers (beta < final of
// the same version; 0.9.14-beta.1 < 0.9.14-beta.2 < 0.9.14). Build metadata
// after '+' is ignored. A leading 'v' is stripped.
#pragma once
#include <cstdlib>
#include <cstring>

namespace ota_ver {
struct Ver {
  long n[3];
  bool suffix;     // has a pre-release part ("-...")
  bool test;       // CI test build: "X-test.<sha>"
  char pre[40];    // pre-release identifiers without the leading '-', else empty
};

inline Ver parse(const char* s) {
  Ver v = {{0, 0, 0}, false, false, {0}};
  if (s == nullptr) return v;
  if (*s == 'v' || *s == 'V') ++s;
  for (int i = 0; i < 3; ++i) {
    char* end = nullptr;
    v.n[i] = strtol(s, &end, 10);
    s = end;
    if (*s != '.') break;
    ++s;
  }
  if (*s == '-') {
    v.suffix = true;
    v.test = strncmp(s, "-test.", 6) == 0;
    ++s;
    size_t k = 0;
    while (*s != '\0' && *s != '+' && k + 1 < sizeof(v.pre)) v.pre[k++] = *s++;
    v.pre[k] = '\0';
  }
  return v;
}

// Compare one pre-release identifier sequence (dot-separated) as in semver 2.0.0.
inline int pre_cmp(const char* a, const char* b) {
  while (true) {
    const bool aend = (*a == '\0');
    const bool bend = (*b == '\0');
    if (aend && bend) return 0;
    if (aend) return -1;  // fewer identifiers is lower (alpha < alpha.1)
    if (bend) return 1;
    const char* ae = a;
    while (*ae != '\0' && *ae != '.') ++ae;
    const char* be = b;
    while (*be != '\0' && *be != '.') ++be;
    bool anum = a < ae, bnum = b < be;
    for (const char* p = a; p < ae; ++p) {
      if (*p < '0' || *p > '9') anum = false;
    }
    for (const char* p = b; p < be; ++p) {
      if (*p < '0' || *p > '9') bnum = false;
    }
    int c = 0;
    if (anum && bnum) {
      long av = strtol(a, nullptr, 10);
      long bv = strtol(b, nullptr, 10);
      c = av < bv ? -1 : av > bv ? 1 : 0;
    } else if (anum != bnum) {
      c = anum ? -1 : 1;  // numeric identifiers have lower precedence
    } else {
      const size_t na = static_cast<size_t>(ae - a), nb = static_cast<size_t>(be - b);
      const size_t n = na < nb ? na : nb;
      c = memcmp(a, b, n);
      if (c == 0) c = na < nb ? -1 : na > nb ? 1 : 0;
    }
    if (c != 0) return c < 0 ? -1 : 1;
    a = (*ae == '.') ? ae + 1 : ae;
    b = (*be == '.') ? be + 1 : be;
  }
}

// Plain semver order: a pre-release sorts below the matching final release.
inline int cmp(const char* a, const char* b) {
  Ver x = parse(a), y = parse(b);
  for (int i = 0; i < 3; ++i) {
    if (x.n[i] != y.n[i]) return x.n[i] < y.n[i] ? -1 : 1;
  }
  if (x.suffix != y.suffix) return x.suffix ? -1 : 1;
  if (!x.suffix) return 0;
  return pre_cmp(x.pre, y.pre);
}

// Should `offered` be proposed while `running` is installed? >0 offer, 0 up to date, <0 older.
// A CI test build "X-test.<sha>" is built from master AFTER release X, so release X is not newer;
// only a strictly greater base version is offered. Everything else keeps plain cmp().
inline int offer_cmp(const char* offered, const char* running) {
  int c = cmp(offered, running);
  Ver r = parse(running), o = parse(offered);
  if (c > 0 && r.test && !o.suffix && o.n[0] == r.n[0] && o.n[1] == r.n[1] && o.n[2] == r.n[2]) return 0;
  return c;
}
}  // namespace ota_ver
