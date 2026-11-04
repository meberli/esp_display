#pragma once
// Pure model of how the Co4 day chart draws one series (LVGL 8 line chart,
// non-crowded mode, plus our dot rule). Host-testable; the UI uses
// co4_plot_dot() in its draw callback, the host test and DIAGCO4 use the rest.
#include <stdint.h>

constexpr int16_t kCo4PlotNone = INT16_MAX;  // == LV_CHART_POINT_NONE

// A point gets a visible dot only if it has no drawn line on either side.
inline bool co4_plot_dot(const int16_t* y, int n, int i) {
  if (i < 0 || i >= n || y[i] == kCo4PlotNone) return false;
  const bool prev = i > 0 && y[i - 1] != kCo4PlotNone;
  const bool next = i + 1 < n && y[i + 1] != kCo4PlotNone;
  return !prev && !next;
}

struct Co4PlotShape {
  int valid;     // points with a value
  int runs;      // connected runs (>= 1 point)
  int isolated;  // runs of exactly one point (drawn as dots)
  int segments;  // drawn line segments
  int max_dy;    // steepest segment, pixels
};

// Pixel y of a value as LVGL computes it (h = content height, y down).
inline int co4_plot_py(int16_t v, int ymin, int ymax, int h) {
  if (ymax == ymin) return h;
  return h - static_cast<int>((static_cast<int32_t>(v) - ymin) * h / (ymax - ymin));
}
inline int co4_plot_px(int i, int n, int w) { return n > 1 ? w * i / (n - 1) : 0; }

// seg(i, x1,y1,x2,y2) for every line, dot(i, x,y) for every visible dot.
template <typename Seg, typename Dot>
Co4PlotShape co4_plot(const int16_t* y, int n, int w, int h, int ymin, int ymax, Seg seg, Dot dot) {
  Co4PlotShape s{};
  for (int i = 0; i < n; ++i) {
    if (y[i] == kCo4PlotNone) continue;
    ++s.valid;
    const int x = co4_plot_px(i, n, w), py = co4_plot_py(y[i], ymin, ymax, h);
    const bool prev = i > 0 && y[i - 1] != kCo4PlotNone;
    if (!prev) ++s.runs;
    if (co4_plot_dot(y, n, i)) {
      ++s.isolated;
      dot(i, x, py);
    }
    if (i + 1 < n && y[i + 1] != kCo4PlotNone) {
      const int x2 = co4_plot_px(i + 1, n, w), y2 = co4_plot_py(y[i + 1], ymin, ymax, h);
      ++s.segments;
      const int dy = y2 > py ? y2 - py : py - y2;
      if (dy > s.max_dy) s.max_dy = dy;
      seg(i, x, py, x2, y2);
    }
  }
  return s;
}
