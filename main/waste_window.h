#pragma once
// Display window of a waste collection reminder (home card + screensaver):
// from "Am Vortag ab" (minutes after midnight on the day before) until
// "Am Sammeltag bis" (minutes on the collection day, 1440 = the whole day).
// Local wall time (TZ), DST safe. Pure C, tools/test_waste_window.cpp.

#include <stdint.h>

constexpr uint16_t kWasteFromDefault = 1080;  // 18:00 on the day before

// day: any time on the collection day (all-day events: local midnight).
int64_t waste_window_start(int64_t day, uint16_t from_min);
int64_t waste_window_end(int64_t day, uint16_t until_min);
bool waste_window_active(int64_t day, int64_t now, uint16_t from_min, uint16_t until_min);

// Family calendar reminders (config version 3).
// All-day events: from cal_from (minutes) on the day before until the end of the event day.
// Timed events: lead minutes before the start, until the start.
constexpr uint16_t kCalFromDefault = 1080;  // 18:00 on the day before
constexpr uint16_t kCalLeadDefault = 60;    // minutes
int64_t cal_window_start(int64_t start, bool all_day, uint16_t from_min, uint16_t lead_min);
int64_t cal_window_end(int64_t start, bool all_day);
bool cal_window_active(int64_t start, bool all_day, int64_t now, uint16_t from_min, uint16_t lead_min);
