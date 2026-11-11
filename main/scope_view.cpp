// See scope_view.h.
#include "scope_view.h"

#include <stdio.h>
#include <string.h>

namespace scope {

const uint32_t kAnalogTb[kAnalogTbN] = {10000,    20000,    50000,    100000,   200000,   500000,   1000000,
                                        2000000,  5000000,  10000000, 20000000, 50000000, 100000000, 200000000};
const uint32_t kLogicTb[kLogicTbN] = {100,    200,    500,    1000,    2000,    5000,    10000,   20000,
                                      50000,  100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000};
const uint16_t kVdivMv[kVdivN] = {100, 200, 500, 1000, 2000};
const uint32_t kChColor[4] = {0xFFD54A, 0x4DD0E1, 0xA5D66F, 0xF48FB1};

namespace {
Plan plan(uint64_t span_ns, uint32_t max_rate, uint32_t max_n, uint32_t min_n, bool auto_mode) {
  Plan p{};
  // rate = min(max_rate, max_n / span)
  uint64_t r = static_cast<uint64_t>(max_n) * 1000000000ull / (span_ns ? span_ns : 1);
  if (r > max_rate) r = max_rate;
  if (r < 1) r = 1;
  p.rate = static_cast<uint32_t>(r);
  uint64_t n = (span_ns * p.rate + 999999999ull) / 1000000000ull;
  if (n < min_n) n = min_n;
  if (n > max_n) n = max_n;
  p.n = static_cast<uint32_t>(n);
  uint32_t span_ms = static_cast<uint32_t>(span_ns / 1000000ull);
  p.timeout_ms = auto_mode ? (span_ms * 2 + 60) : 2000;
  if (p.timeout_ms > 5000) p.timeout_ms = 5000;
  return p;
}
}  // namespace

Plan plan_analog(uint32_t tdiv_ns, int nch, bool auto_mode) {
  if (nch < 1) nch = 1;
  return plan(static_cast<uint64_t>(tdiv_ns) * kDivX, RPS_ADC_MAX_RATE / nch, RPS_ANALOG_MAX_N / nch, 16, auto_mode);
}

Plan plan_logic(uint32_t tdiv_ns, uint32_t max_rate, bool auto_mode) {
  if (max_rate == 0) max_rate = 100000000u;
  Plan p = plan(static_cast<uint64_t>(tdiv_ns) * kDivX, max_rate, RPS_LOGIC_MAX_N, 64, auto_mode);
  p.n = (p.n + 3) & ~3u;
  return p;
}

size_t arm_text(const Arm& a, const Plan& p, char* out, size_t n) {
  int r = snprintf(out, n, "ARM m=%c ch=%d r=%lu n=%lu p=%d e=%c l=%u s=%d t=%c w=%d o=%lu", a.logic ? 'l' : 'a',
                   a.logic ? 0 : a.ch, static_cast<unsigned long>(p.rate), static_cast<unsigned long>(p.n), a.pre_pct,
                   a.edge, static_cast<unsigned>(a.level), a.src, a.normal ? 'n' : 'a', a.cols,
                   static_cast<unsigned long>(p.timeout_ms));
  return r < 0 || static_cast<size_t>(r) >= n ? 0 : static_cast<size_t>(r);
}

size_t view_text(int32_t off, uint32_t n_samples, int cols, char* out, size_t n) {
  if (off < 0) off = 0;
  int r = snprintf(out, n, "VIEW o=%ld n=%lu w=%d", static_cast<long>(off), static_cast<unsigned long>(n_samples), cols);
  return r < 0 || static_cast<size_t>(r) >= n ? 0 : static_cast<size_t>(r);
}

uint16_t volts_to_counts(float v) {
  if (v <= 0) return 0;
  if (v >= kVref) return 4095;
  return static_cast<uint16_t>(v / kVref * 4095.0f + 0.5f);
}

float counts_to_volts(uint32_t c) { return static_cast<float>(c) * kVref / 4095.0f; }

void fmt_freq(float f, char* out, size_t n) {
  if (!(f > 0)) {
    snprintf(out, n, "\xE2\x80\x93");
    return;
  }
  double hz = f;
  if (hz >= 1e6) snprintf(out, n, "%.3f MHz", hz / 1e6);
  else if (hz >= 1e3) snprintf(out, n, "%.2f kHz", hz / 1e3);
  else snprintf(out, n, "%.1f Hz", hz);
}

void fmt_time(uint32_t ns, char* out, size_t n) {
  if (ns >= 1000000) snprintf(out, n, "%lu ms", static_cast<unsigned long>(ns / 1000000));
  else if (ns >= 1000) snprintf(out, n, "%lu \xC2\xB5s", static_cast<unsigned long>(ns / 1000));
  else snprintf(out, n, "%lu ns", static_cast<unsigned long>(ns));
}

void fmt_rate(uint32_t sps, char* out, size_t n) {
  if (sps >= 1000000) snprintf(out, n, "%g MS/s", sps / 1e6);
  else if (sps >= 1000) snprintf(out, n, "%g kS/s", sps / 1e3);
  else snprintf(out, n, "%lu S/s", static_cast<unsigned long>(sps));
}

uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

namespace {
constexpr uint32_t kBg = 0x0E1216, kGrid = 0x39424C, kAxis = 0x5A6672, kTrig = 0xFF9F43;

inline void put(uint16_t* px, int x, int y, uint16_t c) {
  if (x >= 0 && x < kPlotW && y >= 0 && y < kPlotH) px[y * kPlotW + x] = c;
}

void vline(uint16_t* px, int x, int y0, int y1, uint16_t c) {
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  if (y1 < 0 || y0 >= kPlotH || x < 0 || x >= kPlotW) return;
  if (y0 < 0) y0 = 0;
  if (y1 >= kPlotH) y1 = kPlotH - 1;
  for (int y = y0; y <= y1; ++y) px[y * kPlotW + x] = c;
}

void grid(uint16_t* px) {
  uint16_t bg = rgb565(kBg), g = rgb565(kGrid), a = rgb565(kAxis);
  for (int i = 0; i < kPlotW * kPlotH; ++i) px[i] = bg;
  for (int d = 0; d <= kDivX; ++d) {
    int x = d * (kPlotW - 1) / kDivX;
    for (int y = 0; y < kPlotH; y += (d == kDivX / 2 ? 2 : 4)) put(px, x, y, d == kDivX / 2 ? a : g);
  }
  for (int d = 0; d <= kDivY; ++d) {
    int y = d * (kPlotH - 1) / kDivY;
    for (int x = 0; x < kPlotW; x += (d == kDivY / 2 ? 2 : 4)) put(px, x, y, d == kDivY / 2 ? a : g);
  }
}

// Column (frame) index for pixel x.
inline int col_of(int x, int cols) { return static_cast<int>((static_cast<int64_t>(x) * cols) / kPlotW); }

void trig_marker(uint16_t* px, const rps_frame_t& f) {
  if (f.view_n == 0) return;
  int64_t rel = static_cast<int64_t>(f.pre) - f.view_off;
  if (rel < 0 || rel >= static_cast<int64_t>(f.view_n)) return;
  int x = static_cast<int>(rel * kPlotW / f.view_n);
  uint16_t c = rgb565(kTrig);
  for (int y = 0; y < kPlotH; y += 6) vline(px, x, y, y + 2, c);
  for (int i = 0; i < 6; ++i) vline(px, x - 5 + i, 0, 5 - i, c), vline(px, x + 5 - i, 0, 5 - i, c);
}
}  // namespace

void render_empty(uint16_t* px, bool logic) {
  grid(px);
  (void)logic;
}

void render_analog(uint16_t* px, const rps_frame_t& f, const View& v) {
  grid(px);
  if (f.cols == 0 || v.vdiv_mv <= 0) return;
  const float px_per_v = (kPlotH / static_cast<float>(kDivY)) * 1000.0f / static_cast<float>(v.vdiv_mv);
  const float base_v = v.offset_div * v.vdiv_mv / 1000.0f;
  auto y_of = [&](uint8_t u8) {
    float volts = counts_to_volts(static_cast<uint32_t>(u8) * 16u + 8u);
    return kPlotH - 1 - static_cast<int>((volts - base_v) * px_per_v + 0.5f);
  };
  if (v.show_level) {
    uint16_t c = rgb565(kTrig);
    float volts = counts_to_volts(v.trig_lvl);
    int y = kPlotH - 1 - static_cast<int>((volts - base_v) * px_per_v + 0.5f);
    for (int x = 0; x < kPlotW; x += 8) for (int k = 0; k < 4; ++k) put(px, x + k, y, c);
  }
  trig_marker(px, f);
  for (int ch = 0; ch < f.nch && ch < 2; ++ch) {
    uint16_t c = rgb565(kChColor[ch]);
    int py_lo = 0, py_hi = 0;
    bool have = false;
    for (int x = 0; x < kPlotW; ++x) {
      int k = col_of(x, f.cols);
      int ylo = y_of(f.amax[ch][k]);  // higher value -> smaller y
      int yhi = y_of(f.amin[ch][k]);
      int a = ylo, b = yhi;
      if (have) {  // connect to the previous column
        if (py_hi < a) a = py_hi;
        if (py_lo > b) b = py_lo;
      }
      vline(px, x, a, b, c);
      if (x + 1 < kPlotW) vline(px, x + 1, ylo, yhi, c);  // 2 px wide trace
      py_lo = ylo;
      py_hi = yhi;
      have = true;
    }
  }
}

void render_logic(uint16_t* px, const rps_frame_t& f) {
  grid(px);
  if (f.cols == 0) return;
  trig_marker(px, f);
  const int lane = kPlotH / RPS_LOGIC_CH;
  for (int ch = 0; ch < RPS_LOGIC_CH; ++ch) {
    uint16_t c = rgb565(kChColor[ch]);
    int yh = ch * lane + kLogicHiPad, yl = ch * lane + lane - kLogicLoPad;
    int prev = -1;
    for (int x = 0; x < kPlotW; ++x) {
      uint8_t b = static_cast<uint8_t>((f.lbits[col_of(x, f.cols)] >> (2 * ch)) & 3u);
      bool lo = b & 1, hi = b & 2;
      if ((lo && hi) || (prev >= 0 && prev != b && b != 0)) vline(px, x, yh, yl, c);
      if (hi) { put(px, x, yh, c); put(px, x, yh + 1, c); }
      if (lo) { put(px, x, yl, c); put(px, x, yl - 1, c); }
      prev = b;
    }
  }
}

}  // namespace scope
