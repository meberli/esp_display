// Host test for the Abfallkalender-API parser (main/waste_api.cpp).
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src tools/test_waste_api.cpp main/waste_api.cpp main/ical.cpp -o /tmp/tw && TZ=Europe/Zurich /tmp/tw
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>

#include "waste_api.h"

static int fails = 0;
#define CHECK(c, ...)                         \
  do {                                        \
    if (!(c)) {                               \
      printf("FAIL %s:%d ", __FILE__, __LINE__); \
      printf(__VA_ARGS__);                    \
      puts("");                               \
      fails++;                                \
    }                                         \
  } while (0)

static int64_t lm(struct tm* t) { return mktime(t); }

static std::string slurp(const char* p) {
  FILE* f = fopen(p, "rb");
  if (!f) { printf("missing %s\n", p); exit(2); }
  std::string s;
  char b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
  fclose(f);
  return s;
}

static int64_t day(int y, int m, int d) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_isdst = -1;
  return mktime(&t);
}

static void ymd(int64_t s, char* out) {
  time_t t = static_cast<time_t>(s);
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(out, 11, "%Y-%m-%d", &lt);
}

int main() {
  CalEvent ev[16];
  WasteApiInfo info;
  char d[11];
  // Buchs: 45-day window from 2026-10-14, cap 8
  std::string j = slurp("tools/testdata/waste_buchs.json");
  int n = waste_parse_api(j.data(), j.size(), "", day(2026, 10, 14), day(2026, 10, 14) + 45LL * 86400, ev, 8, lm, &info);
  CHECK(n == 8, "buchs n=%d", n);
  CHECK(strcmp(info.gemeinde, "Buchs") == 0 && info.bfs == 3271 && info.zones == 0, "info %s %d %d", info.gemeinde,
        info.bfs, info.zones);
  ymd(ev[0].start, d);
  CHECK(strcmp(d, "2026-10-14") == 0 && strcmp(ev[0].title, "Gr\xC3\xBCngut") == 0, "first %s %s", d, ev[0].title);
  CHECK(ev[0].all_day && ev[0].kind == 1 && ev[0].uid == cal_hash("Gr\xC3\xBCngut"), "flags");
  ymd(ev[1].start, d);
  CHECK(strcmp(d, "2026-10-15") == 0 && strcmp(ev[1].title, "Karton") == 0, "second %s %s", d, ev[1].title);
  for (int i = 1; i < n; ++i) CHECK(ev[i].start >= ev[i - 1].start, "sorted %d", i);
  // Window: from 2026-11-01, 10 days -> Kehricht 11-06 only
  n = waste_parse_api(j.data(), j.size(), nullptr, day(2026, 11, 1), day(2026, 11, 11), ev, 8, lm, &info);
  CHECK(n == 2, "window n=%d", n);  // 11-06 Kehricht, 11-10 Papier
  // Zones: union without a zone, the zone's dates with one
  j = slurp("tools/testdata/waste_zonen_synth.json");
  int64_t f = day(2026, 10, 14), t = f + 45LL * 86400;
  n = waste_parse_api(j.data(), j.size(), "", f, t, ev, 16, lm, &info);
  CHECK(n == 3 && info.zones == 2 && !info.zone_used, "union n=%d zones=%d", n, info.zones);
  n = waste_parse_api(j.data(), j.size(), "Nord", f, t, ev, 16, lm, &info);
  CHECK(n == 2 && info.zone_used, "Nord n=%d", n);
  ymd(ev[0].start, d);
  CHECK(strcmp(d, "2026-10-15") == 0, "Nord kehricht %s", d);
  n = waste_parse_api(j.data(), j.size(), "S\xC3\xBC" "d", f, t, ev, 16, lm, &info);
  CHECK(n == 3 && info.zone_used, "Sued n=%d", n);  // Kehricht 16, Papier 20, Metall 22 (zone only)
  n = waste_parse_api(j.data(), j.size(), "Unbekannt", f, t, ev, 16, lm, &info);
  CHECK(n == 3 && !info.zone_used, "unknown zone n=%d", n);
  // Real multi-zone answer (Nesslau) parses
  j = slurp("tools/testdata/waste_zonen_3360.json");
  n = waste_parse_api(j.data(), j.size(), "", f, t, ev, 16, lm, &info);
  CHECK(n > 0 && info.zones == 2 && info.bfs == 3360, "nesslau n=%d zones=%d", n, info.zones);
  // Errors / junk
  const char* e1 = "{\"error\":\"Parameter ?gemeinde=<Name oder BFS-Nr> fehlt\"}";
  CHECK(waste_parse_api(e1, strlen(e1), "", f, t, ev, 16, lm, &info) == -1, "error json");
  CHECK(waste_parse_api("<html>", 6, "", f, t, ev, 16, lm, &info) == -1, "html");
  const char* e2 = "{\"termine\":{\"Kehricht\":[\"bad\",\"2026-13-01\",\"2026-10-20\"]}}";
  CHECK(waste_parse_api(e2, strlen(e2), "", f, t, ev, 16, lm, &info) == 1, "bad dates skipped");
  // URL
  char u[160];
  CHECK(waste_api_url(u, sizeof(u), "https://abfall-sg.hispeed.workers.dev/", "3271", 2026, 10, 4) &&
            strcmp(u, "https://abfall-sg.hispeed.workers.dev/api/abfallkalender?gemeinde=3271&ab=2026-10-04") == 0,
        "url %s", u);
  CHECK(!waste_api_url(u, sizeof(u), "https://x", "", 2026, 1, 1), "empty bfs");
  printf("%s (%d failures)\n", fails ? "FAIL" : "OK", fails);
  return fails ? 1 : 0;
}
