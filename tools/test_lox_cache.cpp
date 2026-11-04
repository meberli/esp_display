// g++ -std=c++17 -Wall -I main -DLOX_CACHE_HOST tools/test_lox_cache.cpp main/loxone_cache.cpp -o /tmp/tlc && /tmp/tlc
#include <stdio.h>
#include <string.h>

#include "loxone_cache.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static LoxoneControl in[kLoxoneControlCap], out[kLoxoneControlCap];
static uint8_t blob[kLoxCacheMax];

int main() {
  for (size_t i = 0; i < kLoxoneControlCap; ++i) {
    LoxoneControl& c = in[i];
    memset(&c, 0, sizeof(c));
    snprintf(c.name, sizeof(c.name), "Licht Wohnzimmer %u", static_cast<unsigned>(i));
    snprintf(c.room, sizeof(c.room), "Wohnen");
    snprintf(c.action, sizeof(c.action), "0b734138-037d-034e-ffff403fb0c3%04x", static_cast<unsigned>(i));
    if (i % 3 != 0) snprintf(c.state, sizeof(c.state), "0b734138-037d-034e-ffff403fb0c3%04x", static_cast<unsigned>(i + 500));
    c.kind = static_cast<LoxoneKind>(i % 3);
    c.state_kind = static_cast<LoxoneStateKind>(i % 4);
    c.favorite = i % 2;
  }
  memset(in[5].action, 0, sizeof(in[5].action));
  snprintf(in[5].action, sizeof(in[5].action), "Custom_ID.1");  // non-UUID id stays text
  size_t len = lox_cache_pack(in, kLoxoneControlCap, "Miniserver Gen 2", blob, sizeof(blob));
  printf("48 controls -> %u bytes\n", static_cast<unsigned>(len));
  CHECK(len > 0 && len <= kLoxCacheMax);
  char srv[48];
  size_t n = lox_cache_unpack(blob, len, out, kLoxoneControlCap, srv, sizeof(srv));
  CHECK(n == kLoxoneControlCap);
  CHECK(strcmp(srv, "Miniserver Gen 2") == 0);
  CHECK(memcmp(in, out, sizeof(in)) == 0);
  // corrupt / truncated blobs are rejected
  CHECK(lox_cache_unpack(blob, len - 1, out, kLoxoneControlCap, srv, sizeof(srv)) == 0);
  blob[0] = 'X';
  CHECK(lox_cache_unpack(blob, len, out, kLoxoneControlCap, srv, sizeof(srv)) == 0);
  // too small target buffer -> 0 (not cached)
  CHECK(lox_cache_pack(in, kLoxoneControlCap, "x", blob, 200) == 0);
  // bad id (URL unsafe) is rejected on load
  LoxoneControl bad = in[1];
  snprintf(bad.action, sizeof(bad.action), "a/b");
  len = lox_cache_pack(&bad, 1, "", blob, sizeof(blob));
  CHECK(len > 0 && lox_cache_unpack(blob, len, out, 1, srv, sizeof(srv)) == 0);
  if (fails == 0) printf("lox_cache ok\n");
  return fails == 0 ? 0 : 1;
}
