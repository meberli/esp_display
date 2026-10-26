#pragma once
// Minimal iCalendar (RFC 5545) reader for the Kalender view. Pure C++, no
// ESP-IDF calls, so tools/test_ical.cpp can run it on a PC.
//
// Supported: VEVENT with SUMMARY, DTSTART (UTC "Z", local/TZID, all-day
// VALUE=DATE), RRULE FREQ=DAILY/WEEKLY/MONTHLY/YEARLY with INTERVAL, COUNT,
// UNTIL and BYDAY (weekly only), EXDATE. TZID times are taken as panel-local
// time (the family calendar uses Europe/Zurich, the panel's zone).
// Not supported: RECURRENCE-ID overrides (the moved instance shows in
// addition to the original), BYMONTHDAY/BYSETPOS rules.

#include <stddef.h>
#include <stdint.h>
#include <time.h>

struct CalEvent {
  int64_t start;   // Unix seconds (all-day: local midnight)
  bool all_day;
  uint8_t kind;    // 0 = family calendar, 1 = waste collection
  uint32_t uid;    // FNV-1a of the iCal UID (waste: of the title); reminders key on uid + start
  char title[64];  // UTF-8, truncated
};

// local_mktime: struct tm (local wall time) -> Unix seconds. Injected so the
// PC test can pin a zone; on the panel it is mktime() under the TZ setting.
using LocalMktime = int64_t (*)(struct tm* t);

// Parses an iCalendar text (unfolds continuation lines in place, so buf is
// modified) and stores occurrences with from <= start < to, sorted by start.
// Returns the number stored (<= cap).
int ical_parse(char* buf, size_t len, int64_t from, int64_t to, CalEvent* out, int cap, uint8_t kind,
               LocalMktime local_mktime);


uint32_t cal_hash(const char* s, uint32_t h = 2166136261u);  // FNV-1a

// Sorted insert helper shared by both parsers (drops the latest when full).
int cal_insert(CalEvent* out, int n, int cap, const CalEvent& e);
