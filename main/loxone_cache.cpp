#include "loxone_cache.h"

#include <stdio.h>
#include <string.h>

namespace {
constexpr uint8_t kMagic[4] = {'L', 'X', 'C', '1'};

bool put_str(uint8_t* out, size_t cap, size_t* o, const char* s, size_t max) {
  size_t l = strnlen(s, max - 1);
  if (*o + 1 + l > cap) return false;
  out[(*o)++] = static_cast<uint8_t>(l);
  memcpy(out + *o, s, l);
  *o += l;
  return true;
}

bool get_str(const uint8_t* in, size_t len, size_t* i, char* s, size_t max) {
  if (*i >= len) return false;
  size_t l = in[(*i)++];
  if (l >= max || *i + l > len) return false;
  memcpy(s, in + *i, l);
  s[l] = '\0';
  *i += l;
  return true;
}
// Loxone UUIDs ("0f1b2c3d-0a1b-2c3d-ffff0123456789ab", 35 chars) are stored as
// 16 raw bytes behind the length marker 0xFF: ~40 % smaller, keeps the blob in NVS.
constexpr uint8_t kUuidMark = 0xFF;
int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;  // upper case is kept as text (exact round trip)
}
bool is_uuid(const char* s) {
  static const int kDash[] = {8, 13, 18};
  if (strlen(s) != 35) return false;
  for (int i = 0, d = 0; i < 35; ++i) {
    if (d < 3 && i == kDash[d]) {
      if (s[i] != '-') return false;
      ++d;
    } else if (hexv(s[i]) < 0) {
      return false;
    }
  }
  return true;
}

bool put_id(uint8_t* out, size_t cap, size_t* o, const char* s, size_t max) {
  if (!is_uuid(s)) return put_str(out, cap, o, s, max);
  if (*o + 17 > cap) return false;
  out[(*o)++] = kUuidMark;
  int k = 0;
  for (int i = 0; i < 35; ++i) {
    if (s[i] == '-') continue;
    if (k % 2 == 0) out[*o + k / 2] = static_cast<uint8_t>(hexv(s[i]) << 4);
    else out[*o + k / 2] |= static_cast<uint8_t>(hexv(s[i]));
    ++k;
  }
  *o += 16;
  return true;
}

bool get_id(const uint8_t* in, size_t len, size_t* i, char* s, size_t max) {
  if (*i < len && in[*i] == kUuidMark) {
    if (*i + 17 > len || max < 36) return false;
    ++*i;
    static const char kHex[] = "0123456789abcdef";
    int k = 0;
    for (int j = 0; j < 35; ++j) {
      if (j == 8 || j == 13 || j == 18) {
        s[j] = '-';
        continue;
      }
      uint8_t b = in[*i + k / 2];
      s[j] = kHex[k % 2 == 0 ? b >> 4 : b & 15];
      ++k;
    }
    s[35] = '\0';
    *i += 16;
    return true;
  }
  return get_str(in, len, i, s, max);
}
}  // namespace

size_t lox_cache_pack(const LoxoneControl* c, size_t n, const char* server, uint8_t* out, size_t cap) {
  if (n > kLoxoneControlCap || cap < 6) return 0;
  size_t o = 0;
  memcpy(out, kMagic, 4);
  o = 4;
  out[o++] = static_cast<uint8_t>(n);
  if (!put_str(out, cap, &o, server != nullptr ? server : "", 48)) return 0;
  for (size_t k = 0; k < n; ++k) {
    if (o + 3 > cap) return 0;
    out[o++] = c[k].kind;
    out[o++] = c[k].state_kind;
    out[o++] = c[k].favorite ? 1 : 0;
    if (!put_str(out, cap, &o, c[k].name, sizeof(c[k].name)) || !put_str(out, cap, &o, c[k].room, sizeof(c[k].room)) ||
        !put_id(out, cap, &o, c[k].action, sizeof(c[k].action)) ||
        !put_id(out, cap, &o, c[k].state, sizeof(c[k].state)))
      return 0;
  }
  return o;
}

size_t lox_cache_unpack(const uint8_t* in, size_t len, LoxoneControl* out, size_t cap, char* server, size_t server_n) {
  if (len < 6 || memcmp(in, kMagic, 4) != 0) return 0;
  size_t i = 4;
  size_t n = in[i++];
  if (n == 0 || n > cap || n > kLoxoneControlCap) return 0;
  char srv[48];
  if (!get_str(in, len, &i, srv, sizeof(srv))) return 0;
  for (size_t k = 0; k < n; ++k) {
    LoxoneControl& c = out[k];
    memset(&c, 0, sizeof(c));
    if (i + 3 > len) return 0;
    uint8_t kind = in[i++], sk = in[i++];
    if (kind > kLoxoneShutter || sk > kLoxoneStateScene) return 0;
    c.kind = static_cast<LoxoneKind>(kind);
    c.state_kind = static_cast<LoxoneStateKind>(sk);
    c.favorite = in[i++] != 0;
    if (!get_str(in, len, &i, c.name, sizeof(c.name)) || !get_str(in, len, &i, c.room, sizeof(c.room)) ||
        !get_id(in, len, &i, c.action, sizeof(c.action)) || !get_id(in, len, &i, c.state, sizeof(c.state)))
      return 0;
    // Ids end up in URL paths: same check as the parser.
    if (!loxone_id_ok(c.action, sizeof(c.action) - 1)) return 0;
    if (c.state[0] != '\0' && !loxone_id_ok(c.state, sizeof(c.state) - 1)) return 0;
  }
  if (i != len) return 0;
  if (server != nullptr && server_n > 0) snprintf(server, server_n, "%s", srv);
  return n;
}

#ifndef LOX_CACHE_HOST
#include "esp_heap_caps.h"
#include "nvs.h"

namespace {
constexpr const char* kNs = "loxcache";
constexpr const char* kKey = "ctl";
}  // namespace

bool lox_cache_save(const LoxoneControl* c, size_t n, const char* server) {
  uint8_t* blob = static_cast<uint8_t*>(heap_caps_malloc(2 * kLoxCacheMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (blob == nullptr) return false;
  uint8_t* old = blob + kLoxCacheMax;
  size_t len = lox_cache_pack(c, n, server, blob, kLoxCacheMax);
  bool ok = false;
  nvs_handle_t h;
  if (len == 0) {
    printf("Loxone cache: structure too large for NVS (max %u bytes), not cached\n", static_cast<unsigned>(kLoxCacheMax));
  } else if (nvs_open(kNs, NVS_READWRITE, &h) == ESP_OK) {
    size_t ol = kLoxCacheMax;
    if (nvs_get_blob(h, kKey, old, &ol) == ESP_OK && ol == len && memcmp(old, blob, len) == 0) {
      ok = true;  // unchanged: no write
    } else {
      esp_err_t e = nvs_set_blob(h, kKey, blob, len);
      if (e == ESP_OK) e = nvs_commit(h);
      ok = e == ESP_OK;
      printf("Loxone cache: %u controls, %u bytes %s\n", static_cast<unsigned>(n), static_cast<unsigned>(len),
             ok ? "saved" : esp_err_to_name(e));
    }
    nvs_close(h);
  }
  heap_caps_free(blob);
  return ok;
}

size_t lox_cache_load(LoxoneControl* out, size_t cap, char* server, size_t server_n) {
  uint8_t* blob = static_cast<uint8_t*>(heap_caps_malloc(kLoxCacheMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (blob == nullptr) return 0;
  size_t n = 0;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    size_t len = kLoxCacheMax;
    if (nvs_get_blob(h, kKey, blob, &len) == ESP_OK) n = lox_cache_unpack(blob, len, out, cap, server, server_n);
    nvs_close(h);
  }
  heap_caps_free(blob);
  return n;
}
#endif
