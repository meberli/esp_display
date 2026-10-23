#include "pv.h"

#include <string.h>

namespace {
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t u32(const uint16_t* r, int i) { return static_cast<uint32_t>(r[i]) << 16 | r[i + 1]; }
}  // namespace

size_t pv_mb_request(uint8_t* out, size_t cap, uint16_t tid, uint8_t unit, uint8_t fc, uint16_t start, uint16_t count) {
  if (cap < 12 || (fc != kPvFcInput && fc != kPvFcHolding) || count == 0 || count > 125) return 0;
  const uint8_t f[12] = {static_cast<uint8_t>(tid >> 8), static_cast<uint8_t>(tid), 0, 0, 0, 6, unit, fc,
                         static_cast<uint8_t>(start >> 8), static_cast<uint8_t>(start),
                         static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)};
  memcpy(out, f, 12);
  return 12;
}

int pv_mb_response(const uint8_t* buf, size_t n, uint16_t tid, uint8_t unit, uint8_t fc, uint16_t count, uint16_t* regs,
                   uint8_t* exc) {
  if (n < 8) return 0;
  const uint16_t len = be16(buf + 4);
  if (be16(buf + 2) != 0 || len < 2 || len > 254) return -1;
  if (n < static_cast<size_t>(6 + len)) return 0;
  if (be16(buf) != tid || buf[6] != unit) return -1;
  const uint8_t f = buf[7];
  if (f == (fc | 0x80)) {
    if (len < 3) return -1;
    if (exc != nullptr) *exc = buf[8];
    return -2;
  }
  if (f != fc || len < 3 || buf[8] != count * 2 || len != 3 + count * 2) return -1;
  for (uint16_t i = 0; i < count; ++i) regs[i] = be16(buf + 9 + 2 * i);
  return 6 + len;
}

bool pv_decode_input(const uint16_t* r, size_t n, PvReading* o) {
  memset(o, 0, sizeof(*o));
  if (n < 33) return false;
  o->status = r[0];
  o->pv_w = u32(r, 1) / 10.0f;
  o->pv1_v = r[3] / 10.0f;
  o->pv1_a = r[4] / 10.0f;
  o->pv1_w = u32(r, 5) / 10.0f;
  o->pv2_v = r[7] / 10.0f;
  o->pv2_a = r[8] / 10.0f;
  o->pv2_w = u32(r, 9) / 10.0f;
  o->ac_w = u32(r, 11) / 10.0f;
  o->grid_hz = r[13] / 100.0f;
  for (int i = 0; i < 3; ++i) {
    o->grid_v[i] = r[14 + 4 * i] / 10.0f;
    o->grid_a[i] = r[15 + 4 * i] / 10.0f;
  }
  o->today_kwh = u32(r, 26) / 10.0f;
  o->total_kwh = static_cast<float>(u32(r, 28) / 10.0);
  o->temp_c = static_cast<int16_t>(r[32]) / 10.0f;
  o->valid = true;
  return true;
}

bool pv_stale(int64_t now, int64_t last) { return last <= 0 || now - last > kPvStaleS; }

void pv_hist_clear(PvHist* h) {
  memset(h, 0, sizeof(*h));
  h->magic = kPvHistMagic;
  h->today = -1;
  h->prev_final = -1;
}

int pv_hist_update(PvHist* h, int32_t ymd, float v) {
  if (v < 0 || v > 1000) return 0;
  int res = 0;
  if (h->ymd != ymd) {
    if (h->ymd != 0 && h->today >= 0) {
      memmove(&h->days[1], &h->days[0], sizeof(PvDay) * (kPvDays - 1));
      h->days[0].ymd = h->ymd;
      h->days[0].kwh = h->today;
      h->prev_final = h->today;
    } else {
      h->prev_final = -1;
    }
    h->ymd = ymd;
    h->today = -1;
    h->fresh = 0;
    res = 2;
  }
  // Morning: until the inverter resets its counter it may still report the
  // previous day's total. Accept once it is below that, or the previous day is
  // unknown or was (nearly) zero.
  if (!h->fresh) {
    if (h->prev_final < 0.05f || v < h->prev_final - 0.05f) h->fresh = 1;
    else return res;
  }
  if (v != h->today) {
    h->today = v;
    res |= 1;
  }
  return res;
}

float pv_hist_day(const PvHist& h, int32_t ymd) {
  for (const PvDay& d : h.days)
    if (d.ymd == ymd && ymd != 0) return d.kwh;
  return -1;
}
