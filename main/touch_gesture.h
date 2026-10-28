#pragma once
// Pure touch classification (host-tested in tools/test_touch_gesture.cpp).
// The right-edge back swipe and horizontal page swipes never mix: which one a
// touch belongs to is decided at touch-down from the start x only.
#include <stdlib.h>

namespace touch {

constexpr int kScreenW = 480;
constexpr int kEdgeBack = 24;        // right edge zone, screens without paging
constexpr int kEdgeBackPaging = 16;  // narrower on screens with horizontal pages
constexpr int kBackTravel = 80;      // leftward travel needed for back
constexpr int kSwipe = 40;           // page swipe travel
constexpr int kTapSlop = 10;

// Decided once at touch-down.
inline bool starts_in_back_edge(int x0, bool paging) {
  return x0 >= kScreenW - (paging ? kEdgeBackPaging : kEdgeBack);
}

// Edge touches only: back fires after a clear, mostly horizontal leftward move.
inline bool back_fires(int dx, int dy) { return dx <= -kBackTravel && abs(dx) > 2 * abs(dy); }

// Non-edge touches only: horizontal page swipe. 3 = next (leftward), 4 = prev.
inline int page_swipe(int dx, int dy) {
  int adx = abs(dx), ady = abs(dy);
  if (adx >= kSwipe && adx > ady + 8) return dx < 0 ? 3 : 4;
  return 0;
}

}  // namespace touch
