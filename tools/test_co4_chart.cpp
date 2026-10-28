// Host test: the Co4 day chart keeps gaps as gaps and draws isolated values as
// dots at their value (no vertical strokes). Also replays LVGL 8's
// draw_series_line point loop with its single shared rect descriptor to show
// the old callback hid every dot after the first line point.
#include <assert.h>
#include <stdio.h>
#include <vector>

#include "co4_chart_plot.h"

constexpr int16_t N = kCo4PlotNone;
constexpr int W = 396, H = 100;

struct Dot { int i, x, y; };
struct Seg { int i, x1, y1, x2, y2; };

// LVGL loop (non-crowded): for slot i it reports point i-1 and draws its rect if
// valid, with whatever opacity the shared descriptor has after the callback.
template <typename Cb>
std::vector<int> lvgl_visible_dots(const int16_t* y, int n, Cb cb) {
  std::vector<int> out;
  bool opaque = true;  // lv_draw_rect_dsc_init: bg_opa = COVER, once per series
  for (int i = 1; i < n; ++i) {
    cb(y, n, i - 1, &opaque);
    if (y[i - 1] != N && opaque) out.push_back(i - 1);
  }
  return out;
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  // 0..96 (97 slots, the last always empty): an isolated value, a gap, a run of
  // three, isolated values every other slot, a run of two, one at the end.
  int16_t y[97];
  for (auto& v : y) v = N;
  y[3] = 150;
  y[10] = 100; y[11] = 120; y[12] = 110;
  for (int i = 30; i <= 40; i += 2) y[i] = 400;  // sparse: every other slot
  y[50] = 0; y[51] = 300;
  y[95] = 250;
  const int ymin = 0, ymax = 500;

  std::vector<Dot> dots;
  std::vector<Seg> segs;
  Co4PlotShape s = co4_plot(y, 97, W, H, ymin, ymax,
      [&](int i, int x1, int y1, int x2, int y2) { segs.push_back({i, x1, y1, x2, y2}); },
      [&](int i, int x, int yy) { dots.push_back({i, x, yy}); });

  printf("valid=%d runs=%d isolated=%d segments=%d max_dy=%d\n", s.valid, s.runs, s.isolated, s.segments, s.max_dy);
  for (auto& d : dots) printf("  dot  i=%2d x=%3d y=%3d\n", d.i, d.x, d.y);
  for (auto& g : segs) printf("  line i=%2d (%3d,%3d)-(%3d,%3d)\n", g.i, g.x1, g.y1, g.x2, g.y2);

  assert(s.valid == 13 && s.runs == 10 && s.isolated == 8 && s.segments == 3);
  // every isolated value: a dot at exactly its value, no line touching it
  for (auto& d : dots) {
    assert(d.y == co4_plot_py(y[d.i], ymin, ymax, H));
    assert(d.x == co4_plot_px(d.i, 97, W));
    for (auto& g : segs) assert(g.i != d.i && g.i + 1 != d.i);
  }
  // lines only between two adjacent values; gaps never bridged
  for (auto& g : segs) assert(y[g.i] != N && y[g.i + 1] != N);
  // the sparse block is dots only (the old "vertical strokes" region)
  for (int i = 30; i <= 40; i += 2) {
    bool found = false;
    for (auto& d : dots) found |= d.i == i;
    assert(found);
  }

  // LVGL replay: old callback (only ever set TRANSP) vs new (sets both ways)
  auto old_cb = [](const int16_t* v, int n, int i, bool* op) {
    const bool prev = i > 0 && v[i - 1] != N, next = i + 1 < n && v[i + 1] != N;
    if (prev || next) *op = false;
  };
  auto new_cb = [](const int16_t* v, int n, int i, bool* op) { *op = co4_plot_dot(v, n, i); };
  std::vector<int> o = lvgl_visible_dots(y, 97, old_cb), nw = lvgl_visible_dots(y, 97, new_cb);
  printf("old callback: %zu visible dots, new: %zu\n", o.size(), nw.size());
  for (int v : o) printf("  old visible i=%d\n", v);
  // the empty slot before an isolated value already turns the shared descriptor
  // transparent: every isolated value vanished, only 2+ runs (4 px wide, steep =
  // "vertical strokes") stayed visible.
  assert(o.empty());
  assert(nw.size() == dots.size());
  for (size_t k = 0; k < nw.size(); ++k) assert(nw[k] == dots[k].i);
  // 96 slots (no trailing empty one) would lose the dot at the last value
  std::vector<int> s96 = lvgl_visible_dots(y, 96, new_cb);
  assert(s96.size() == nw.size() - 1);
  puts("co4 chart OK");
  return 0;
}
