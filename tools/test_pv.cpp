// Host test for main/pv.cpp (Growatt via Modbus TCP gateway, read-only).
//   python3 -m ziglang c++ -std=c++17 -Wall -I main tools/test_pv.cpp main/pv.cpp -o /tmp/tp && /tmp/tp
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pv.h"

static int fails = 0;
#define CHECK(c, what)                                     \
  do {                                                     \
    if (!(c)) {                                            \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
      fails++;                                             \
    }                                                      \
  } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 0.051)

// Real FC04 0..44 sample (23.10.2026 17:26, gateway 192.168.1.244), padded to 45.
static const uint16_t kSample[45] = {1, 0, 593, 2968, 2, 0, 593, 3680, 0, 0, 0, 0, 14, 4997, 2319, 0, 0, 0, 2334, 0, 0,
                                     0, 2328, 0, 0, 0, 0, 46, 9, 21385, 4743, 62813, 369};

static size_t response(uint8_t* b, uint16_t tid, uint8_t unit, uint8_t fc, const uint16_t* regs, int n) {
  int len = 3 + 2 * n;
  b[0] = tid >> 8; b[1] = tid; b[2] = 0; b[3] = 0; b[4] = len >> 8; b[5] = len; b[6] = unit; b[7] = fc; b[8] = 2 * n;
  for (int i = 0; i < n; ++i) { b[9 + 2 * i] = regs[i] >> 8; b[10 + 2 * i] = regs[i]; }
  return 6 + len;
}

#define CHECK1(c) CHECK(c, #c)
#include <cmath>
// ---- PvHours
static int64_t T0 = 1793000000;  // arbitrary
static void hours_tests() {
  PvHours h;
  pv_hours_clear(&h);
  // 1 kW constant for one hour from 10:00, readings every 10 s.
  int r = 0;
  for (int s = 0; s <= 3600; s += 10) r |= pv_hours_add(&h, 20261024, T0 + s, 36000 + s, 1000, 1.0f + s / 3600.0f);
  CHECK1(r & 4);
  CHECK1(std::fabs(h.wh[10] - 1000.0f * 3590 / 3600) < 1.0f || std::fabs(h.wh[10] + h.wh[11] - 1000) < 1.0f);
  CHECK1(std::fabs(h.wh[10] + h.wh[11] - 1000) < 1.0f);
  CHECK1(h.eac_q[40] >= 0.99f && h.eac_q[40] <= 1.01f);
  CHECK1(h.eac_q[44] >= 1.99f);
  // Hour change reported once (the 39600 sample entered hour 11 already).
  CHECK1((pv_hours_add(&h, 20261024, T0 + 3610, 36000 + 3610, 1000, 2.0f) & 2) == 0);
  // Gap > 60 s: not integrated.
  float before = h.wh[12];
  pv_hours_add(&h, 20261024, T0 + 7200, 43200, 1000, 2.5f);
  pv_hours_add(&h, 20261024, T0 + 7300, 43300, 1000, 2.5f);
  CHECK1(h.wh[12] == before);
  pv_hours_add(&h, 20261024, T0 + 7310, 43310, 1000, 2.5f);
  CHECK1(std::fabs(h.wh[12] - before - 1000.0f * 10 / 3600) < 0.01f);
  // Negative / corrupt power clamped.
  float w12 = h.wh[12];
  pv_hours_add(&h, 20261024, T0 + 7320, 43320, -50, 2.5f);
  CHECK1(std::fabs(h.wh[12] - w12 - 500.0f * 10 / 3600) < 0.01f);
  // Eac -1 (morning, not fresh) does not snapshot.
  PvHours m; pv_hours_clear(&m);
  pv_hours_add(&m, 20261024, T0, 25200, 100, -1);
  CHECK1(m.eac_q[28] < 0);
  pv_hours_add(&m, 20261024, T0 + 10, 25210, 100, 0.0f);
  CHECK1(m.eac_q[28] == 0.0f);
  // New day resets.
  r = pv_hours_add(&h, 20261025, T0 + 86400, 30000, 500, 0.0f);
  CHECK1((r & 4) && h.wh[10] == 0 && h.ymd == 20261025 && h.last_t == T0 + 86400);
  // Scaling: integrated 2.0 kWh between snapshots 1.0 -> 3.2 = 2.2: k = 1.1.
  PvHours s; pv_hours_clear(&s, 20261024);
  s.wh[11] = 1200; s.wh[12] = 800; s.eac_q[44] = 1.0f;
  float out[24]; bool cov[24], kn[24];
  float k = pv_hours_scaled(s, 3.2f, out, cov);
  CHECK1(std::fabs(k - 1.1f) < 1e-3f && std::fabs(out[11] - 1.32f) < 1e-3f && std::fabs(out[12] - 0.88f) < 1e-3f);
  CHECK1(cov[11] && !cov[12] && !cov[3]);  // coverage = samples (q_wh) or an Eac snapshot (q44 = 11:00)
  // Implausible ratio (part of the day missing after a reboot) -> unscaled.
  k = pv_hours_scaled(s, 6.0f, out);
  CHECK1(k == 1.0f && std::fabs(out[11] - 1.2f) < 1e-3f);
  // Small delta (< 0.3 kWh, 0.1 kWh steps) -> unscaled.
  PvHours t; pv_hours_clear(&t, 20261024); t.wh[8] = 150; t.eac_q[32] = 0.0f;
  CHECK1(pv_hours_scaled(t, 0.2f, out) == 1.0f);
  // No Eac -> unscaled.
  CHECK1(pv_hours_scaled(s, -1, out) == 1.0f);
  // Known hours: boot at 12:00 with Eac 2.5 already -> morning unknown; boot at 07:00 with 0 -> known.
  PvHours b; pv_hours_clear(&b, 20261024); b.eac_q[48] = 2.5f; b.q_wh[48] = 10;
  pv_hours_scaled(b, 2.5f, out, cov); pv_hours_known(b, cov, kn);
  CHECK1(!kn[9] && kn[12] && kn[20]);
  pv_hours_clear(&b, 20261024); b.eac_q[28] = 0.0f; b.q_wh[29] = 5;
  pv_hours_scaled(b, 0.1f, out, cov); pv_hours_known(b, cov, kn);
  CHECK1(kn[3] && kn[7] && kn[22]);
  PvHours e; pv_hours_clear(&e, 20261024);
  pv_hours_scaled(e, -1, out, cov); pv_hours_known(e, cov, kn);
  CHECK1(!kn[12]);  // nothing at all today: unknown
  // ymd 0 (clock not set) ignored.
  CHECK1(pv_hours_add(&e, 0, T0, 100, 100, 1) == 0);
}

int main() {
  hours_tests();
  // Request: exact MBAP bytes, and no write function codes ever.
  uint8_t q[16];
  CHECK(pv_mb_request(q, sizeof(q), 0x1234, 1, kPvFcInput, 0, 45) == 12, "request len");
  const uint8_t want[12] = {0x12, 0x34, 0, 0, 0, 6, 1, 4, 0, 0, 0, 45};
  CHECK(memcmp(q, want, 12) == 0, "request bytes");
  CHECK(pv_mb_request(q, sizeof(q), 1, 1, kPvFcHolding, 0, 10) == 12 && q[7] == 3, "fc03 ok");
  const uint8_t bad[] = {0x05, 0x06, 0x0F, 0x10, 0x01, 0x02, 0x17, 0x00};
  for (uint8_t f : bad) CHECK(pv_mb_request(q, sizeof(q), 1, 1, f, 0, 1) == 0, "write/other fc refused");
  CHECK(pv_mb_request(q, 11, 1, 1, 4, 0, 1) == 0, "cap");
  CHECK(pv_mb_request(q, sizeof(q), 1, 1, 4, 0, 126) == 0, "count");

  // Response parsing.
  uint8_t b[300];
  uint16_t regs[45];
  size_t n = response(b, 7, 1, 4, kSample, 45);
  CHECK(n == 99, "frame size");
  for (size_t k = 0; k < n; ++k) CHECK(pv_mb_response(b, k, 7, 1, 4, 45, regs, nullptr) == 0, "partial -> need more");
  CHECK(pv_mb_response(b, n, 7, 1, 4, 45, regs, nullptr) == (int)n, "complete");
  CHECK(memcmp(regs, kSample, sizeof(kSample)) == 0, "regs");
  CHECK(pv_mb_response(b, n, 8, 1, 4, 45, regs, nullptr) == -1, "tid mismatch");
  CHECK(pv_mb_response(b, n, 7, 2, 4, 45, regs, nullptr) == -1, "unit mismatch");
  CHECK(pv_mb_response(b, n, 7, 1, 3, 45, regs, nullptr) == -1, "fc mismatch");
  CHECK(pv_mb_response(b, n, 7, 1, 4, 44, regs, nullptr) == -1, "count mismatch");
  b[2] = 1;
  CHECK(pv_mb_response(b, n, 7, 1, 4, 45, regs, nullptr) == -1, "protocol id");
  const uint8_t ex[9] = {0, 7, 0, 0, 0, 3, 1, 0x84, 0x0B};
  uint8_t code = 0;
  CHECK(pv_mb_response(ex, 9, 7, 1, 4, 45, regs, &code) == -2 && code == 0x0B, "exception");

  // Decode the real sample.
  PvReading r;
  CHECK(pv_decode_input(kSample, 45, &r) && r.valid, "decode");
  CHECK(r.status == 1, "status");
  CHECK(NEAR(r.pv_w, 59.3), "Ppv");
  CHECK(NEAR(r.pv1_v, 296.8) && NEAR(r.pv1_a, 0.2) && NEAR(r.pv1_w, 59.3), "pv1");
  CHECK(NEAR(r.pv2_v, 368.0) && NEAR(r.pv2_w, 0), "pv2");
  CHECK(NEAR(r.ac_w, 1.4), "Pac");
  CHECK(NEAR(r.grid_hz, 49.97), "Fac");
  CHECK(NEAR(r.grid_v[0], 231.9) && NEAR(r.grid_v[1], 233.4) && NEAR(r.grid_v[2], 232.8), "Vac");
  CHECK(NEAR(r.today_kwh, 4.6), "today");
  CHECK(NEAR(r.total_kwh, 61120.9), "total");
  CHECK(NEAR(r.temp_c, 36.9), "temp");
  CHECK(!pv_decode_input(kSample, 32, &r) && !r.valid, "short");
  pv_decode_input(kSample, 45, &r);
  printf("sample: status %d Ppv %.1f W Pac %.1f W Fac %.2f Vac %.1f/%.1f/%.1f today %.1f total %.1f temp %.1f\n", 1,
         r.pv_w, r.ac_w, r.grid_hz, r.grid_v[0], r.grid_v[1], r.grid_v[2], r.today_kwh, r.total_kwh, r.temp_c);

  // Stale.
  CHECK(pv_stale(1000, 0), "never");
  CHECK(!pv_stale(1000, 940), "60 s ok");
  CHECK(pv_stale(1000, 939), "61 s stale");

  // Daily history.
  PvHist h;
  pv_hist_clear(&h);
  CHECK(pv_hist_update(&h, 20261023, 4.6f) == 3 && NEAR(h.today, 4.6), "first day");
  CHECK(pv_hist_update(&h, 20261023, 4.6f) == 0, "unchanged");
  CHECK(pv_hist_update(&h, 20261023, 5.0f) == 1, "grows");
  // Next morning: the inverter still reports 5.0 before its reset -> not taken.
  CHECK(pv_hist_update(&h, 20261024, 5.0f) == 2 && h.today < 0, "new day, old value ignored");
  CHECK(h.days[0].ymd == 20261023 && NEAR(h.days[0].kwh, 5.0), "yesterday stored");
  CHECK(pv_hist_update(&h, 20261024, 5.0f) == 0 && h.today < 0, "still old");
  CHECK(pv_hist_update(&h, 20261024, 0.0f) == 1 && NEAR(h.today, 0), "reset seen");
  CHECK(pv_hist_update(&h, 20261024, 0.3f) == 1, "counts");
  // A day without readings (night only) is not booked; next day shifts by one.
  CHECK(pv_hist_update(&h, 20261026, 0.1f) == 3, "skip a day");
  CHECK(NEAR(pv_hist_day(h, 20261024), 0.3) && NEAR(pv_hist_day(h, 20261023), 5.0), "lookup");
  CHECK(pv_hist_day(h, 20261025) < 0, "unknown day");
  // A zero-production day: next morning any value is accepted.
  PvHist z;
  pv_hist_clear(&z);
  pv_hist_update(&z, 20261101, 0.0f);
  CHECK(pv_hist_update(&z, 20261102, 0.2f) == 3, "after zero day");
  CHECK(pv_hist_update(&z, 20261102, -1.0f) == 0 && pv_hist_update(&z, 20261102, 5000.0f) == 0, "implausible");
  // Ring keeps 8 days.
  PvHist g;
  pv_hist_clear(&g);
  for (int d = 1; d <= 12; ++d) pv_hist_update(&g, 20261100 + d, d == 1 ? 1.0f : 0.0f);
  CHECK(g.days[7].ymd == 20261104 && g.days[0].ymd == 20261111, "ring");

  printf(fails ? "%d FAILED\n" : "all PV tests passed\n", fails);
  return fails ? 1 : 0;
}
