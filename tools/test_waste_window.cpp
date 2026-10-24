// Host test for the waste reminder window (main/waste_window.cpp).
//   g++ -std=c++17 -Wall -I main tools/test_waste_window.cpp main/waste_window.cpp -o /tmp/tww && TZ=Europe/Zurich /tmp/tww
#include <stdio.h>
#include <time.h>

#include "waste_window.h"

static int fails = 0;
#define CHECK(c, what)                                \
  do {                                                \
    if (!(c)) {                                       \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
      fails++;                                        \
    }                                                 \
  } while (0)

static int64_t at(int y, int mo, int d, int h, int mi) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}

int main() {
  int64_t day = at(2026, 10, 16, 0, 0);  // collection Friday
  // Defaults: Vortag 18:00 .. Sammeltag 08:00
  CHECK(waste_window_start(day, 1080) == at(2026, 10, 15, 18, 0), "start 18:00 day before");
  CHECK(waste_window_end(day, 480) == at(2026, 10, 16, 8, 0), "end 08:00");
  CHECK(!waste_window_active(day, at(2026, 10, 15, 17, 59), 1080, 480), "17:59 before");
  CHECK(waste_window_active(day, at(2026, 10, 15, 18, 0), 1080, 480), "18:00 shown");
  CHECK(waste_window_active(day, at(2026, 10, 16, 0, 30), 1080, 480), "night shown");
  CHECK(waste_window_active(day, at(2026, 10, 16, 7, 59), 1080, 480), "07:59 shown");
  CHECK(!waste_window_active(day, at(2026, 10, 16, 8, 0), 1080, 480), "08:00 gone");
  CHECK(!waste_window_active(day, at(2026, 10, 14, 18, 30), 1080, 480), "two days before");
  // Other times; whole day; any time of the day as input
  CHECK(waste_window_start(at(2026, 10, 16, 13, 0), 1200) == at(2026, 10, 15, 20, 0), "20:00, day given at noon");
  CHECK(waste_window_end(day, 1440) == at(2026, 10, 17, 0, 0), "whole day");
  CHECK(waste_window_active(day, at(2026, 10, 16, 23, 59), 1080, 1440), "23:59 whole day");
  CHECK(waste_window_start(day, 0) == at(2026, 10, 15, 0, 0), "from 00:00");
  CHECK(waste_window_start(day, 5000) == at(2026, 10, 15, 23, 59), "clamped");
  // Month boundary and DST end (25 Oct 2026) / DST start (29 Mar 2026)
  CHECK(waste_window_start(at(2026, 11, 1, 0, 0), 1080) == at(2026, 10, 31, 18, 0), "month boundary");
  CHECK(waste_window_start(at(2026, 10, 26, 0, 0), 1080) == at(2026, 10, 25, 18, 0), "after DST end");
  CHECK(waste_window_end(at(2026, 10, 25, 0, 0), 480) == at(2026, 10, 25, 8, 0), "DST end day");
  CHECK(waste_window_start(at(2026, 3, 30, 0, 0), 1080) == at(2026, 3, 29, 18, 0), "after DST start");
  CHECK(waste_window_end(at(2026, 3, 29, 0, 0), 480) == at(2026, 3, 29, 8, 0), "DST start day");
  CHECK(waste_window_end(at(2026, 3, 29, 0, 0), 480) - waste_window_start(at(2026, 3, 29, 0, 0), 1080) == 13 * 3600,
        "13 h incl. lost hour");
  // Family calendar: all-day from the day before 18:00 to the end of the day
  CHECK(cal_window_start(day, true, 1080, 60) == at(2026, 10, 15, 18, 0), "cal all-day start");
  CHECK(cal_window_end(day, true) == at(2026, 10, 17, 0, 0), "cal all-day end of day");
  CHECK(!cal_window_active(day, true, at(2026, 10, 15, 17, 59), 1080, 60), "cal all-day 17:59 before");
  CHECK(cal_window_active(day, true, at(2026, 10, 16, 23, 59), 1080, 60), "cal all-day 23:59 shown");
  CHECK(!cal_window_active(day, true, at(2026, 10, 17, 0, 0), 1080, 60), "cal all-day next day gone");
  CHECK(cal_window_start(day, true, 720, 60) == at(2026, 10, 15, 12, 0), "cal all-day 12:00");
  // all-day on the DST end day (25 Oct): day before 18:00, end next midnight (25 h day)
  int64_t dst = at(2026, 10, 25, 0, 0);
  CHECK(cal_window_start(dst, true, 1080, 60) == at(2026, 10, 24, 18, 0), "cal all-day DST start");
  CHECK(cal_window_end(dst, true) - dst == 25 * 3600, "cal all-day DST 25 h day");
  // Timed: lead minutes before the start until the start
  int64_t st = at(2026, 10, 16, 14, 30);
  CHECK(cal_window_start(st, false, 1080, 60) == at(2026, 10, 16, 13, 30), "timed 60 min");
  CHECK(cal_window_start(st, false, 1080, 15) == at(2026, 10, 16, 14, 15), "timed 15 min");
  CHECK(cal_window_start(st, false, 1080, 180) == at(2026, 10, 16, 11, 30), "timed 3 h");
  CHECK(!cal_window_active(st, false, at(2026, 10, 16, 13, 29), 1080, 60), "timed before lead");
  CHECK(cal_window_active(st, false, at(2026, 10, 16, 13, 30), 1080, 60), "timed lead reached");
  CHECK(cal_window_active(st, false, st - 1, 1080, 60), "timed just before start");
  CHECK(!cal_window_active(st, false, st, 1080, 60), "timed gone at start");
  // Timed across midnight: 00:30 with 2 h lead -> 22:30 the day before
  int64_t early = at(2026, 10, 16, 0, 30);
  CHECK(cal_window_start(early, false, 1080, 120) == at(2026, 10, 15, 22, 30), "timed across midnight");
  printf("%s (%d failures)\n", fails ? "FAIL" : "OK", fails);
  return fails ? 1 : 0;
}
