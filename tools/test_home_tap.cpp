// Host test for main/home_tap.h:
//   g++ -std=c++17 -Wall -I main tools/test_home_tap.cpp -o /tmp/tht && /tmp/tht

#include <stdio.h>

#include "home_tap.h"

static int fails = 0;
#define CHECK(c, what)                                     \
  do {                                                     \
    if (!(c)) {                                            \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
      fails++;                                             \
    }                                                      \
  } while (0)

int main() {
  CHECK(energy_home_opens_setup(false, false), "no whatwatt, no co4 -> setup");
  CHECK(!energy_home_opens_setup(true, false), "whatwatt ready -> view");
  CHECK(!energy_home_opens_setup(false, true), "co4 ready -> view");
  CHECK(!energy_home_opens_setup(true, true), "both ready -> view");

  CHECK(loxone_home_opens_setup(false), "no miniserver -> setup");
  CHECK(!loxone_home_opens_setup(true), "miniserver set -> menu");

  CHECK(cameras_home_opens_setup(false, false), "no cam, no key -> setup");
  CHECK(cameras_home_opens_setup(true, false), "cams but no key -> setup");
  CHECK(cameras_home_opens_setup(false, true), "key but no cam -> setup");
  CHECK(!cameras_home_opens_setup(true, true), "cam and key -> view");

  CHECK(home_opens_setup(false) && !home_opens_setup(true), "generic predicate");

  if (fails) {
    printf("%d FAILED\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
