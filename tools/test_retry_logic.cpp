// g++ -std=c++17 -Wall -I main tools/test_retry_logic.cpp -o /tmp/trl && /tmp/trl
#include <stdio.h>

#include "retry_logic.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  CHECK(lox_backoff_ms(0) == 0);
  CHECK(lox_backoff_ms(1) == 10000);
  CHECK(lox_backoff_ms(2) == 30000);
  CHECK(lox_backoff_ms(3) == 120000);
  CHECK(lox_backoff_ms(4) == 300000);
  CHECK(lox_backoff_ms(1000) == 300000);

  ScanGen g;
  uint32_t a = g.start();
  CHECK(g.accept(a));
  CHECK(!g.accept(a));  // only once
  uint32_t b = g.start();
  g.cancel();
  CHECK(!g.accept(b));  // cancelled: late result dropped
  uint32_t c = g.start();
  CHECK(c != b);
  CHECK(!g.accept(b));  // stale result of the cancelled scan while a new one runs
  CHECK(g.accept(c));
  uint32_t d = g.start();
  uint32_t e = g.start();  // restart without result
  CHECK(!g.accept(d));
  CHECK(g.accept(e));
  if (fails == 0) printf("retry_logic ok\n");
  return fails == 0 ? 0 : 1;
}
