// Host test for main/ota_ver.h: g++ -std=c++17 -I main tools/test_ota_ver.cpp -o t && ./t
#include <cstdio>
#include "ota_ver.h"
int main() {
  struct { const char *a, *b; int r; } t[] = {
      {"0.7.5", "0.7.4", 1}, {"v0.7.4", "0.7.4", 0}, {"0.7.10", "0.7.9", 1}, {"0.8.0", "0.7.99", 1},
      {"1.0.0", "0.9.9", 1}, {"0.7.4", "0.7.4-3-gabc", 1}, {"0.7.3", "0.7.4", -1}, {"0.7", "0.7.0", 0}};
  // offer_cmp(offered, running): release-to-release unchanged, test builds need a strictly greater base.
  struct { const char *o, *r; int r_; } u[] = {
      {"0.9.8", "0.9.8-test.8ac6a68", 0},   // the reported bug: same base must not be offered
      {"v0.9.8", "0.9.8-test.8ac6a68", 0},
      {"0.9.9", "0.9.8-test.8ac6a68", 1},  {"0.10.0", "0.9.8-test.b5fd8f9", 1},
      {"1.0.0", "0.9.8-test.b5fd8f9", 1},  {"0.9.7", "0.9.8-test.b5fd8f9", -1},
      {"0.9.8", "0.9.7", 1},  {"0.9.8", "0.9.8", 0},  {"0.9.7", "0.9.8", -1},
      {"0.7.4", "0.7.4-3-gabc", 1}};  // non-test dev builds keep semver order
  int fails = 0;
  for (auto& c : t) if (ota_ver::cmp(c.a, c.b) != c.r) { printf("FAIL cmp %s vs %s\n", c.a, c.b); ++fails; }
  for (auto& c : u) if (ota_ver::offer_cmp(c.o, c.r) != c.r_) { printf("FAIL offer %s on %s\n", c.o, c.r); ++fails; }
  printf("%d failures\n", fails);
  return fails;
}
