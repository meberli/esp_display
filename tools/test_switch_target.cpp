// g++ -std=c++17 -Wall -Imain tools/test_switch_target.cpp -o /tmp/test_switch_target && /tmp/test_switch_target
#include <stdio.h>
#include <string.h>

#include "switch_target.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

int main() {
  const char* ids[3] = {"relay", "a1", "b2"};
  CHECK(swt::pick(ids, 0, "") == -1);
  CHECK(swt::pick(ids, 3, "") == 0);
  CHECK(swt::pick(ids, 3, nullptr) == 0);
  CHECK(swt::pick(ids, 3, "b2") == 2);
  CHECK(swt::pick(ids, 3, "gone") == 0);
  CHECK(strcmp(swt::command(kLoxoneSwitch, false, false), "On") == 0);
  CHECK(strcmp(swt::command(kLoxoneSwitch, true, false), "On") == 0);
  CHECK(strcmp(swt::command(kLoxoneSwitch, true, true), "Off") == 0);
  CHECK(strcmp(swt::command(kLoxoneSwitch, false, true), "On") == 0);
  CHECK(strcmp(swt::command(kLoxonePulse, true, true), "Pulse") == 0);
  CHECK(swt::may_fire(false, false));
  CHECK(!swt::may_fire(true, false));   // scroll/swipe never toggles
  CHECK(!swt::may_fire(false, true));   // one command at a time
  printf(fails ? "FAILED\n" : "ok\n");
  return fails ? 1 : 0;
}
