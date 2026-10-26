// Host test for ota_ver_cmp: g++ -std=c++17 -I main -DOTA_VER_HOST tools/test_ota_ver.cpp -o t && ./t
#include <cstdio>
#include <cstdlib>
#include <cstring>
struct Ver { long n[3]; bool suffix; };
static Ver parse(const char* s) {
  Ver v = {{0, 0, 0}, false};
  if (*s == 'v' || *s == 'V') ++s;
  for (int i = 0; i < 3; ++i) { char* e = nullptr; v.n[i] = strtol(s, &e, 10); s = e; if (*s != '.') break; ++s; }
  v.suffix = *s == '-' || *s == '+';
  return v;
}
static int cmp(const char* a, const char* b) {
  Ver x = parse(a), y = parse(b);
  for (int i = 0; i < 3; ++i) if (x.n[i] != y.n[i]) return x.n[i] < y.n[i] ? -1 : 1;
  if (x.suffix != y.suffix) return x.suffix ? -1 : 1;
  return 0;
}
int main() {
  struct { const char *a, *b; int r; } t[] = {
      {"0.7.5", "0.7.4", 1}, {"v0.7.4", "0.7.4", 0}, {"0.7.10", "0.7.9", 1}, {"0.8.0", "0.7.99", 1},
      {"1.0.0", "0.9.9", 1}, {"0.7.4", "0.7.4-3-gabc", 1}, {"0.7.3", "0.7.4", -1}, {"0.7", "0.7.0", 0}};
  int fails = 0;
  for (auto& c : t) if (cmp(c.a, c.b) != c.r) { printf("FAIL %s vs %s\n", c.a, c.b); ++fails; }
  printf("%d failures\n", fails);
  return fails;
}
