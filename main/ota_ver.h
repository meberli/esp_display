// Firmware version comparison, header-only so tools/test_ota_ver.cpp tests the real code.
#pragma once
#include <cstdlib>
#include <cstring>

namespace ota_ver {
struct Ver {
  long n[3];
  bool suffix;     // any "-..." / "+..." after the base version
  bool test;       // CI test build: "X-test.<sha>"
};

inline Ver parse(const char* s) {
  Ver v = {{0, 0, 0}, false, false};
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
  v.test = strncmp(s, "-test.", 6) == 0;
  return v;
}

// Plain semver-ish order: a prerelease/dev suffix sorts below its release.
inline int cmp(const char* a, const char* b) {
  Ver x = parse(a), y = parse(b);
  for (int i = 0; i < 3; ++i) {
    if (x.n[i] != y.n[i]) return x.n[i] < y.n[i] ? -1 : 1;
  }
  if (x.suffix != y.suffix) return x.suffix ? -1 : 1;
  return 0;
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
