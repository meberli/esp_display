// Host tests: Playground scope protocol (rp2040/libraries/RpScope) and
// scope_view (planning, text, renderer).
//   gcc -std=c99 -Wall -Wextra -c rp2040/libraries/RpScope/src/rp_scope.c -o /tmp/rp_scope.o &&
//   g++ -std=c++17 -Wall -Wextra -I main -I rp2040/libraries/RpScope/src tools/test_scope.cpp
//       main/scope_view.cpp /tmp/rp_scope.o -o /tmp/tsc && /tmp/tsc
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "rp_scope.h"
#include "scope_view.h"

static int fails = 0;
#define CHECK(c)                                                  \
  do {                                                            \
    if (!(c)) {                                                   \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      ++fails;                                                    \
    }                                                             \
  } while (0)

static void test_cobs() {
  srand(1);
  for (int t = 0; t < 2000; ++t) {
    size_t n = static_cast<size_t>(rand() % 2100);
    std::vector<uint8_t> in(n), enc(n + n / 254 + 4), dec(n + 4);
    for (auto& b : in) b = (rand() % 4 == 0) ? 0 : static_cast<uint8_t>(rand());
    size_t e = rps_cobs_encode(in.data(), n, enc.data(), enc.size());
    CHECK(e > 0);
    for (size_t i = 0; i < e; ++i) CHECK(enc[i] != 0);
    size_t d = rps_cobs_decode(enc.data(), e, dec.data(), dec.size());
    CHECK(d == n);
    CHECK(memcmp(in.data(), dec.data(), n) == 0);
  }
  // Run of 254 / 255 / 600 non-zero bytes (0xFF code blocks).
  for (size_t n : {253u, 254u, 255u, 508u, 600u}) {
    std::vector<uint8_t> in(n, 0x11), enc(n + 8), dec(n + 8);
    size_t e = rps_cobs_encode(in.data(), n, enc.data(), enc.size());
    CHECK(rps_cobs_decode(enc.data(), e, dec.data(), dec.size()) == n);
  }
  uint8_t small[4];
  uint8_t in[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  CHECK(rps_cobs_encode(in, 10, small, sizeof(small)) == 0);  // no room
  uint8_t bad[3] = {5, 1, 2};
  uint8_t out[8];
  CHECK(rps_cobs_decode(bad, 3, out, sizeof(out)) == 0);  // truncated block
}

static void test_cmd() {
  uint8_t f[200];
  size_t n = rps_cmd_frame("HELLO", f, sizeof(f));
  CHECK(n == 9);  // code + "S HELLO" + 0x00
  CHECK(f[0] == 8 && f[1] == 'S' && f[2] == ' ' && f[n - 1] == 0);
  uint8_t d[200];
  CHECK(rps_cobs_decode(f, n - 1, d, sizeof(d)) == 7 && memcmp(d, "S HELLO", 7) == 0);
  // Non-printable or too long texts are refused.
  CHECK(rps_cmd_frame("A\xA3", f, sizeof(f)) == 0);
  CHECK(rps_cmd_frame("A\x01", f, sizeof(f)) == 0);
  char big[200];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  CHECK(rps_cmd_frame(big, f, sizeof(f)) == 0);
  // Every printable command up to the limit: no 0xA3 on the wire, ever.
  for (size_t len = 1; len + 2 <= RPS_MAX_CMD; ++len) {
    char t[RPS_MAX_CMD + 1];
    for (size_t i = 0; i < len; ++i) t[i] = static_cast<char>(0x20 + (i * 7 + len) % 95);
    t[len] = 0;
    size_t m = rps_cmd_frame(t, f, sizeof(f));
    CHECK(m == len + 4);
    for (size_t i = 0; i < m; ++i) CHECK(f[i] != RPS_FORBIDDEN_BYTE);
  }
  // Real ARM texts from the planner, all timebases / modes.
  for (int logic = 0; logic < 2; ++logic)
    for (int tb = 0; tb < (logic ? scope::kLogicTbN : scope::kAnalogTbN); ++tb) {
      scope::Arm a{logic != 0, 2, 1, 'f', true, 4095, 90, scope::kPlotW};
      scope::Plan p = logic ? scope::plan_logic(scope::kLogicTb[tb], 0, false)
                            : scope::plan_analog(scope::kAnalogTb[tb], 2, false);
      char t[RPS_MAX_CMD];
      CHECK(scope::arm_text(a, p, t, sizeof(t)) > 0);
      size_t m = rps_cmd_frame(t, f, sizeof(f));
      CHECK(m > 0);
      for (size_t i = 0; i < m; ++i) CHECK(f[i] != RPS_FORBIDDEN_BYTE);
      rps_cmd_t k;
      CHECK(rps_cmd_parse(t, strlen(t), &k));
      CHECK(!strcmp(k.key, "ARM"));
      CHECK(k.m == (logic ? 'l' : 'a') && k.e == 'f' && k.t == 'n' && k.p == 90 && k.s == 1);
      CHECK(static_cast<uint32_t>(k.r) == p.rate && static_cast<uint32_t>(k.n) == p.n);
      CHECK(k.w == scope::kPlotW && k.l == 4095);
    }
  rps_cmd_t k;
  CHECK(rps_cmd_parse("VIEW o=120 n=900 w=450", 22, &k) && !strcmp(k.key, "VIEW") && k.o == 120 && k.n == 900);
  CHECK(rps_cmd_parse("STOP", 4, &k) && !strcmp(k.key, "STOP"));
  CHECK(rps_cmd_parse("  ", 2, &k) == 0);
  CHECK(rps_cmd_parse("ARM junk zz=5 r=7", 17, &k) && k.r == 7);
}

static void test_plan() {
  // 10 µs/div analog: 100 µs at 500 kS/s = 50 samples.
  scope::Plan p = scope::plan_analog(10000, 1, true);
  CHECK(p.rate == 500000 && p.n == 50);
  // Dual channel halves the rate.
  p = scope::plan_analog(10000, 2, true);
  CHECK(p.rate == 250000 && p.n == 25);
  // 100 ms/div: 1 s span, limited by the buffer.
  p = scope::plan_analog(100000000, 1, true);
  CHECK(p.n <= RPS_ANALOG_MAX_N && p.rate == RPS_ANALOG_MAX_N);
  CHECK(p.timeout_ms == 2060);
  for (int i = 0; i < scope::kAnalogTbN; ++i) {
    for (int nch = 1; nch <= 2; ++nch) {
      scope::Plan q = scope::plan_analog(scope::kAnalogTb[i], nch, false);
      CHECK(q.rate * nch <= RPS_ADC_MAX_RATE);
      CHECK(q.n * nch <= RPS_ANALOG_MAX_N && q.n >= 16);
      CHECK(q.timeout_ms == 2000);
    }
  }
  // Logic 100 ns/div: 1 µs at 100 MS/s = 100 samples (rounded to 4).
  p = scope::plan_logic(100, 100000000, true);
  CHECK(p.rate == 100000000 && p.n == 100);
  for (int i = 0; i < scope::kLogicTbN; ++i) {
    scope::Plan q = scope::plan_logic(scope::kLogicTb[i], 100000000, true);
    CHECK(q.rate <= 100000000 && q.n <= RPS_LOGIC_MAX_N && q.n >= 64 && q.n % 4 == 0);
  }
  CHECK(scope::volts_to_counts(0) == 0 && scope::volts_to_counts(3.3f) == 4095 && scope::volts_to_counts(9) == 4095);
  CHECK(fabsf(scope::counts_to_volts(scope::volts_to_counts(1.65f)) - 1.65f) < 0.002f);
  char b[32];
  scope::fmt_freq(1000.0f, b, sizeof(b));
  CHECK(!strcmp(b, "1.00 kHz"));
  scope::fmt_freq(12345678.9f, b, sizeof(b));
  CHECK(!strcmp(b, "12.346 MHz"));
  scope::fmt_freq(50.0f, b, sizeof(b));
  CHECK(!strcmp(b, "50.0 Hz"));
  scope::fmt_freq(0.0f, b, sizeof(b));
  CHECK(!strcmp(b, "\xE2\x80\x93"));
  scope::fmt_time(500, b, sizeof(b));
  CHECK(!strcmp(b, "500 ns"));
  scope::fmt_time(20000, b, sizeof(b));
  CHECK(!strcmp(b, "20 \xC2\xB5s"));
  scope::fmt_rate(100000000, b, sizeof(b));
  CHECK(!strcmp(b, "100 MS/s"));
  scope::fmt_rate(250000, b, sizeof(b));
  CHECK(!strcmp(b, "250 kS/s"));
}

// Synthetic ADC ring: sine of f Hz at rate, starting at ring index start (wraps).
static std::vector<uint16_t> g_ring(RPS_ANALOG_RING);

static void test_analog() {
  const uint32_t mask = RPS_ANALOG_RING - 1, rate = 500000, start = RPS_ANALOG_RING - 1000, n = 20000;
  const double f = 10000.0;  // 10 kHz: 50 samples per period
  for (uint32_t i = 0; i < n; ++i) {
    double v = 2048 + 1500 * sin(2 * M_PI * f * i / rate);
    g_ring[(start + i) & mask] = static_cast<uint16_t>(v);
  }
  rps_ameas_t m;
  rps_ameasure(g_ring.data(), mask, start, n, 1, rate, &m);
  CHECK(m.vmin >= 548 && m.vmin <= 552 && m.vmax >= 3543 && m.vmax <= 3548);
  CHECK(m.vmean > 2040 && m.vmean < 2056);
  CHECK(fabsf(m.freq_hz - 10000.0f) < 2.0f);  // 10 kHz +- 2 Hz
  uint8_t mn[RPS_MAX_COLS], mx[RPS_MAX_COLS];
  CHECK(rps_adecimate(g_ring.data(), mask, start, n, 1, 450, mn, mx) == 450);
  for (int c = 0; c < 450; ++c) CHECK(mn[c] <= mx[c]);
  CHECK(rps_adecimate(g_ring.data(), mask, start, 50, 1, 450, mn, mx) == 50);  // fewer samples than columns
  // Interleaved dual channel: ch0 sine, ch1 constant 1000.
  for (uint32_t i = 0; i < n; ++i) {
    double v = 2048 + 1500 * sin(2 * M_PI * f * i / (rate / 2));
    g_ring[(start + 2 * i) & mask] = static_cast<uint16_t>(v);
    g_ring[(start + 2 * i + 1) & mask] = 1000;
  }
  rps_ameasure(g_ring.data(), mask, start + 1, n, 2, rate / 2, &m);
  CHECK(m.vmin == 1000 && m.vmax == 1000 && m.freq_hz == 0);
  rps_ameasure(g_ring.data(), mask, start, n, 2, rate / 2, &m);
  CHECK(fabsf(m.freq_hz - 10000.0f) < 2.0f);
  // Trigger with hysteresis: noise around the level does not fire twice.
  rps_atrig_t t;
  rps_atrig_init(&t, 2000, 24, 1);
  int fires = 0;
  uint16_t seq[] = {1500, 1990, 2010, 1995, 2005, 2100, 1990, 2010, 1900, 1960, 2001, 3000};
  for (uint16_t v : seq) fires += rps_atrig_step(&t, v);
  CHECK(fires == 2);  // 1500 -> 2010, then re-armed by 1960 (< 1976) -> 2001
  rps_atrig_init(&t, 2000, 24, 0);
  fires = 0;
  uint16_t fall[] = {2500, 1999, 2010, 1990, 2100, 1900};
  for (uint16_t v : fall) fires += rps_atrig_step(&t, v);
  CHECK(fires == 2);
}

static std::vector<uint8_t> g_lring(RPS_LOGIC_RING);

static void test_logic() {
  const uint32_t mask = RPS_LOGIC_RING - 1, start = RPS_LOGIC_RING - 3000, n = 10000, rate = 100000000;
  // D0: 10 MHz square (period 10), D1: 25 % duty at 250 kHz (period 400), D2 always high, D3 low.
  for (uint32_t i = 0; i < n; ++i) {
    uint8_t v = 0;
    if ((i % 10) < 5) v |= rps_logic_bit(0);
    if ((i % 400) < 100) v |= rps_logic_bit(1);
    v |= rps_logic_bit(2);
    v |= 0x3C;  // GPIO22..25 noise bits must be ignored
    g_lring[(start + i) & mask] = v;
  }
  rps_lmeas_t m[RPS_LOGIC_CH];
  rps_lmeasure(g_lring.data(), mask, start, n, rate, 0, m);
  CHECK(fabsf(m[0].freq_hz - 1e7f) < 1.0f && m[0].duty_pm == 500);
  CHECK(fabsf(m[1].freq_hz - 250000.0f) < 0.1f && m[1].duty_pm == 250);
  CHECK(m[2].rising == 0 && m[2].level0 == 1 && m[2].freq_hz == 0);
  CHECK(m[3].rising == 0 && m[3].level0 == 0);
  uint8_t bits[RPS_MAX_COLS];
  CHECK(rps_ldecimate(g_lring.data(), mask, start, n, 450, bits) == 450);
  for (int c = 0; c < 450; ++c) {
    CHECK(((bits[c] >> 0) & 3) == 3);  // D0 toggles inside every column (22 samples/col)
    CHECK(((bits[c] >> 4) & 3) == 2);  // D2 only high
    CHECK(((bits[c] >> 6) & 3) == 1);  // D3 only low
  }
  // First rising edge of D1 after 1030 is 1200; falling after 1030 is 1300; D0 rising after 1031 is 1040.
  int64_t g0 = start;  // global index == ring index here (wraps at 3000)
  CHECK(rps_lfind_edge(g_lring.data(), mask, g0 + 1030, g0 + 5000, rps_logic_bit(1), 1) == g0 + 1200);
  CHECK(rps_lfind_edge(g_lring.data(), mask, g0 + 1030, g0 + 5000, rps_logic_bit(1), 0) == g0 + 1300);
  CHECK(rps_lfind_edge(g_lring.data(), mask, g0 + 1031, g0 + 5000, rps_logic_bit(0), 1) == g0 + 1040);
  CHECK(rps_lfind_edge(g_lring.data(), mask, g0 + 2991, g0 + 3010, rps_logic_bit(0), 1) == g0 + 3000);  // across the wrap
  CHECK(rps_lfind_edge(g_lring.data(), mask, g0 + 10, g0 + 5000, rps_logic_bit(2), 1) == -1);
}

static void test_frames() {
  static rps_frame_t a, b;
  memset(&a, 0, sizeof(a));
  a.type = RPS_T_ANALOG;
  a.seq = 7;
  a.flags = RPS_F_TRIGGERED | RPS_F_VIEW;
  a.chmode = 2;
  a.nch = 2;
  a.cols = 450;
  a.rate = 250000;
  a.n = 30720;
  a.pre = 3072;
  a.view_off = 1000;
  a.view_n = 4000;
  a.am[0] = {100, 4000, 2000, 1234.5f};
  a.am[1] = {7, 8, 9, 0.0f};
  for (int c = 0; c < 450; ++c) {
    a.amin[0][c] = static_cast<uint8_t>(c);
    a.amax[0][c] = static_cast<uint8_t>(c + 1);
    a.amin[1][c] = 0xA3;
    a.amax[1][c] = 0;
  }
  uint8_t buf[RPS_MAX_FRAME];
  size_t n = rps_put_frame(&a, buf, sizeof(buf));
  CHECK(n == 1 + 27 + 2 * 10 + 2 * 450 * 2);
  CHECK(n <= RPS_MAX_FRAME);
  CHECK(rps_parse_frame(buf, n, &b));
  CHECK(b.type == a.type && b.seq == 7 && b.flags == a.flags && b.chmode == 2 && b.nch == 2 && b.cols == 450);
  CHECK(b.rate == a.rate && b.n == a.n && b.pre == a.pre && b.view_off == 1000 && b.view_n == 4000);
  CHECK(b.am[0].freq_hz == 1234.5f && b.am[0].vmax == 4000 && b.am[1].vmean == 9);
  CHECK(memcmp(b.amin, a.amin, sizeof(a.amin[0]) * 2) == 0 || (memcmp(b.amin[0], a.amin[0], 450) == 0 &&
                                                               memcmp(b.amin[1], a.amin[1], 450) == 0));
  CHECK(memcmp(b.amax[0], a.amax[0], 450) == 0);
  CHECK(rps_parse_frame(buf, n - 1, &b) == 0);  // truncated
  // Over the wire: COBS + delimiter round trip.
  uint8_t enc[RPS_MAX_FRAME + 16], dec[RPS_MAX_FRAME + 16];
  size_t e = rps_cobs_encode(buf, n, enc, sizeof(enc));
  CHECK(e > 0 && rps_cobs_decode(enc, e, dec, sizeof(dec)) == n && memcmp(dec, buf, n) == 0);

  memset(&a, 0, sizeof(a));
  a.type = RPS_T_LOGIC;
  a.nch = RPS_LOGIC_CH;
  a.chmode = RPS_LOGIC_CH;
  a.cols = 450;
  a.rate = 100000000;
  a.n = 126976;
  a.lm[3] = {5, 4.9e7f, 500, 1, 0};
  for (int c = 0; c < 450; ++c) a.lbits[c] = static_cast<uint8_t>(c * 3);
  n = rps_put_frame(&a, buf, sizeof(buf));
  CHECK(n == 1 + 27 + 4 * 12 + 450);
  CHECK(rps_parse_frame(buf, n, &b));
  CHECK(b.type == RPS_T_LOGIC && b.lm[3].rising == 5 && b.lm[3].freq_hz == 4.9e7f && b.lm[3].duty_pm == 500);
  CHECK(b.lm[3].level0 == 1 && memcmp(b.lbits, a.lbits, 450) == 0 && b.n == 126976);

  rps_caps_t c{}, d{};
  c.sys_hz = 200000000;
  c.adc_max_rate = 500000;
  c.logic_max_rate = 100000000;
  c.analog_max_n = RPS_ANALOG_MAX_N;
  c.logic_max_n = RPS_LOGIC_MAX_N;
  c.logic_ch = 4;
  strcpy(c.fw, "scope-1.0");
  n = rps_put_caps(&c, buf, sizeof(buf));
  CHECK(n == 35 && rps_parse_caps(buf, n, &d));
  CHECK(d.sys_hz == c.sys_hz && d.logic_max_rate == c.logic_max_rate && !strcmp(d.fw, "scope-1.0"));
}

static void test_render() {
  static uint16_t px[scope::kPlotW * scope::kPlotH];
  static rps_frame_t f;
  memset(&f, 0, sizeof(f));
  f.type = RPS_T_ANALOG;
  f.nch = 1;
  f.cols = 450;
  f.n = f.view_n = 4500;
  f.pre = 450;
  // Flat 1.65 V (counts 2048 -> u8 128).
  for (int c = 0; c < 450; ++c) f.amin[0][c] = f.amax[0][c] = 128;
  scope::View v{500, 0, 2048, 0, false};
  scope::render_analog(px, f, v);
  const uint16_t c0 = scope::rgb565(scope::kChColor[0]);
  // ~1.657 V at 500 mV/div (kPlotH / 8 px per div): y = kPlotH - 1 - 1.657 * 2 * kPlotH / 8.
  const int want = scope::kPlotH - 1 - static_cast<int>(scope::counts_to_volts(128 * 16 + 8) * 2.0f * scope::kPlotH / 8 + 0.5f);
  int hits = 0, row = -1;
  for (int y = 0; y < scope::kPlotH; ++y)
    if (px[y * scope::kPlotW + 200] == c0) { ++hits; row = y; }
  CHECK(hits >= 1 && hits <= 2 && row >= want - 2 && row <= want + 2);
  // Trigger marker at x = kPlotW * 450 / 4500 = kPlotW / 10 (orange).
  const uint16_t tr = scope::rgb565(0xFF9F43);
  CHECK(px[0 * scope::kPlotW + scope::kPlotW / 10] == tr);
  // Out-of-range values are clipped, no crash.
  for (int c = 0; c < 450; ++c) { f.amin[0][c] = 0; f.amax[0][c] = 255; }
  scope::View z{100, -8, 0, 0, true};
  scope::render_analog(px, f, z);
  // Logic: D0 high only, D1 toggling.
  memset(&f, 0, sizeof(f));
  f.type = RPS_T_LOGIC;
  f.nch = 4;
  f.cols = 100;  // stretched to kPlotW px
  f.n = f.view_n = 100;
  for (int c = 0; c < 100; ++c) f.lbits[c] = static_cast<uint8_t>(0x2 | ((c & 1) ? 0x8 : 0x4) | 0x10 | 0x40);
  scope::render_logic(px, f);
  const uint16_t l0 = scope::rgb565(scope::kChColor[0]);
  CHECK(px[scope::kLogicHiPad * scope::kPlotW + 100] == l0);         // D0 high line
  CHECK(px[(scope::kPlotH / 4 - scope::kLogicLoPad) * scope::kPlotW + 100] != l0);  // D0 never low
  // D3 low line stays above the bottom readouts (last 24 px of the plot).
  const uint16_t l3 = scope::rgb565(scope::kChColor[3]);
  CHECK(px[(scope::kPlotH - scope::kLogicLoPad) * scope::kPlotW + 100] == l3);
  CHECK(scope::kPlotH - scope::kLogicLoPad < scope::kPlotH - 24);
  scope::render_empty(px, true);
}

int main() {
  test_cobs();
  test_cmd();
  test_plan();
  test_analog();
  test_logic();
  test_frames();
  test_render();
  if (fails) {
    printf("%d failure(s)\n", fails);
    return 1;
  }
  printf("test_scope: all passed\n");
  return 0;
}
