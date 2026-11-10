#pragma once
// Playground scope (SenseCAP D1): timebase planning, measurement text and the
// plot renderer. Pure C++ (no LVGL / IDF), host-tested in tools/test_scope.cpp.
// The plot is an RGB565 pixel buffer (LV_COLOR_DEPTH 16, no byte swap) that
// panel_ui_scope.inc hands to an lv_canvas.

#include <stddef.h>
#include <stdint.h>

#include "rp_scope.h"

namespace scope {

constexpr int kPlotW = 480;  // full screen width: 10 divisions of 48 px (RPS_MAX_COLS)
constexpr int kPlotH = 336;  // 8 divisions of 42 px
constexpr int kDivX = 10, kDivY = 8;
constexpr float kVref = 3.3f;

// Timebases (ns per division).
constexpr int kAnalogTbN = 14;
extern const uint32_t kAnalogTb[kAnalogTbN];  // 10 µs .. 200 ms
constexpr int kLogicTbN = 16;
extern const uint32_t kLogicTb[kLogicTbN];    // 100 ns .. 10 ms
constexpr int kVdivN = 5;
extern const uint16_t kVdivMv[kVdivN];        // 100 .. 1000 mV

struct Plan {
  uint32_t rate;  // samples/s per channel
  uint32_t n;     // samples per channel
  uint32_t timeout_ms;
};
// nch: analog channels (1/2). Respects RPS_ADC_MAX_RATE / RPS_ANALOG_MAX_N.
Plan plan_analog(uint32_t tdiv_ns, int nch, bool auto_mode);
// max_rate: from CAPS (0 = 100 MS/s).
Plan plan_logic(uint32_t tdiv_ns, uint32_t max_rate, bool auto_mode);

// "ARM ..." command text (without the leading "S ").
struct Arm {
  bool logic;
  int ch;        // analog 0 GPIO26, 1 GPIO27, 2 both
  int src;       // trigger source (analog channel 0/1 in dual mode, logic 0..3)
  char edge;     // r / f / n
  bool normal;   // normal trigger (else auto)
  uint16_t level;  // analog trigger level, 12-bit counts
  int pre_pct;
  int cols;
};
size_t arm_text(const Arm& a, const Plan& p, char* out, size_t n);
size_t view_text(int32_t off, uint32_t n_samples, int cols, char* out, size_t n);

uint16_t volts_to_counts(float v);
float counts_to_volts(uint32_t c);  // 12-bit counts

// "12.35 kHz", "–" when 0.
void fmt_freq(float hz, char* out, size_t n);
// "50 µs", "2 ms" (per division labels).
void fmt_time(uint32_t ns, char* out, size_t n);
void fmt_rate(uint32_t sps, char* out, size_t n);  // "500 kS/s", "100 MS/s"

struct View {
  int vdiv_mv;
  int offset_div;     // volts at the bottom line = offset_div * vdiv
  uint16_t trig_lvl;  // analog: 12-bit counts, drawn for the source channel
  int trig_src;
  bool show_level;
};
// px: kPlotW * kPlotH RGB565.
void render_analog(uint16_t* px, const rps_frame_t& f, const View& v);
void render_logic(uint16_t* px, const rps_frame_t& f);
void render_empty(uint16_t* px, bool logic);

// Colours (RGB565).
uint16_t rgb565(uint32_t rgb);
extern const uint32_t kChColor[4];

}  // namespace scope
