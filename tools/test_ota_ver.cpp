// Host test for main/ota_ver.h: g++ -std=c++17 -I main tools/test_ota_ver.cpp -o t && ./t
#include <cstdio>
#include "ota_ver.h"
int main() {
  struct { const char *a, *b; int r; } t[] = {
      {"0.7.5", "0.7.4", 1}, {"v0.7.4", "0.7.4", 0}, {"0.7.10", "0.7.9", 1}, {"0.8.0", "0.7.99", 1},
      {"1.0.0", "0.9.9", 1}, {"0.7.4", "0.7.4-3-gabc", 1}, {"0.7.3", "0.7.4", -1}, {"0.7", "0.7.0", 0},
      // Proper semver: beta < final of the same version; beta.N is numeric.
      {"0.9.14-beta.1", "0.9.14", -1},
      {"0.9.14", "0.9.14-beta.1", 1},
      {"0.9.14-beta.1", "0.9.14-beta.2", -1},
      {"0.9.14-beta.2", "0.9.14-beta.10", -1},
      {"0.9.14-beta.10", "0.9.14-beta.2", 1},
      {"0.9.14-alpha", "0.9.14-beta", -1},
      {"0.9.14-alpha", "0.9.14-alpha.1", -1},
      {"0.9.14-beta.1", "0.9.13", 1},
      {"0.9.13", "0.9.14-beta.1", -1},
      {"v0.9.14-beta.1", "0.9.14-beta.1", 0},
      {"0.9.14-beta.1+build.5", "0.9.14-beta.1", 0},
      {"0.9.14+build.5", "0.9.14", 0},
      {"0.9.14-rc.1", "0.9.14-beta.10", 1},
      {"0.9.15-beta.1", "0.9.14", 1},
  };
  // offer_cmp(offered, running): release-to-release unchanged, test builds need a strictly greater base.
  struct { const char *o, *r; int r_; } u[] = {
      {"0.9.8", "0.9.8-test.8ac6a68", 0},   // the reported bug: same base must not be offered
      {"v0.9.8", "0.9.8-test.8ac6a68", 0},
      {"0.9.9", "0.9.8-test.8ac6a68", 1},  {"0.10.0", "0.9.8-test.b5fd8f9", 1},
      {"1.0.0", "0.9.8-test.b5fd8f9", 1},  {"0.9.7", "0.9.8-test.b5fd8f9", -1},
      {"0.9.8", "0.9.7", 1},  {"0.9.8", "0.9.8", 0},  {"0.9.7", "0.9.8", -1},
      {"0.7.4", "0.7.4-3-gabc", 1},  // non-test dev builds keep semver order
      {"0.9.14", "0.9.14-beta.1", 1},
      {"0.9.14-beta.2", "0.9.14-beta.1", 1},
      {"0.9.14-beta.1", "0.9.14", -1},
  };
  int fails = 0;
  for (auto& c : t) {
    int got = ota_ver::cmp(c.a, c.b);
    if (got != c.r) { printf("FAIL cmp %s vs %s got %d want %d\n", c.a, c.b, got, c.r); ++fails; }
    int back = ota_ver::cmp(c.b, c.a);
    if (back != -c.r) { printf("FAIL cmp reverse %s vs %s got %d want %d\n", c.b, c.a, back, -c.r); ++fails; }
  }
  for (auto& c : u) {
    int got = ota_ver::offer_cmp(c.o, c.r);
    if (got != c.r_) { printf("FAIL offer %s on %s got %d want %d\n", c.o, c.r, got, c.r_); ++fails; }
  }
  printf("%d failures\n", fails);
  return fails;
}
