// g++ -std=c++17 -Wall -I main tools/test_touch_gesture.cpp -o /tmp/ttg && /tmp/ttg
#include <stdio.h>

#include "touch_gesture.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  using namespace touch;
  // Zone is decided by the start x only.
  CHECK(starts_in_back_edge(479, false));
  CHECK(starts_in_back_edge(456, false));
  CHECK(!starts_in_back_edge(455, false));
  CHECK(starts_in_back_edge(464, true));
  CHECK(!starts_in_back_edge(463, true));  // paging screens: narrower zone
  CHECK(!starts_in_back_edge(240, true));
  // Edge touch: back only after >= 80 px, mostly horizontal, leftward.
  CHECK(back_fires(-80, 0));
  CHECK(back_fires(-120, 50));
  CHECK(!back_fires(-79, 0));
  CHECK(!back_fires(-100, 60));  // too diagonal
  CHECK(!back_fires(100, 0));    // rightward never
  // Inside the screen a leftward swipe is a page swipe (last page still works).
  CHECK(page_swipe(-60, 0) == 3);
  CHECK(page_swipe(60, 5) == 4);
  CHECK(page_swipe(-30, 0) == 0);
  CHECK(page_swipe(-50, 45) == 0);
  // Separation: an edge-start touch is never handed to page_swipe by the UI, and a
  // non-edge touch is never handed to back_fires (see finish_touch()).
  printf("%s (%d failures)\n", fails ? "FAILED" : "touch gesture tests passed", fails);
  return fails ? 1 : 0;
}
