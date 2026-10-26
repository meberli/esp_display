// PC test for main/ical.cpp:
//   g++ -std=c++17 -I main tools/test_ical.cpp main/ical.cpp -o /tmp/ti && TZ=Europe/Zurich /tmp/ti cal.ics page.html
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ical.h"

static int64_t lm(struct tm* t) { return static_cast<int64_t>(mktime(t)); }

static char* slurp(const char* path, size_t* len) {
  FILE* f = fopen(path, "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  *len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char* b = static_cast<char*>(malloc(*len + 1));
  *len = fread(b, 1, *len, f);
  b[*len] = 0;
  fclose(f);
  return b;
}

static void dump(const CalEvent* ev, int n) {
  for (int i = 0; i < n; ++i) {
    time_t t = ev[i].start;
    struct tm lt;
    localtime_r(&t, &lt);
    char b[32];
    strftime(b, sizeof(b), ev[i].all_day ? "%a %d.%m.%Y" : "%a %d.%m.%Y %H:%M", &lt);
    printf("  %s %s %s\n", b, ev[i].kind ? "[A]" : "   ", ev[i].title);
  }
}

int main(int argc, char** argv) {
  tzset();
  int64_t now = time(nullptr);
  CalEvent ev[16];
  // synthetic rules
  const char* syn =
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART;VALUE=DATE:20200310\r\nRRULE:FREQ=YEARLY\r\nSUMMARY:Geburi\r\n"
      " stag\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART;TZID=Europe/Zurich:20260105T180000\r\n"
      "RRULE:FREQ=WEEKLY;BYDAY=MO,TH\r\nEXDATE;TZID=Europe/Zurich:20261015T180000\r\nSUMMARY:Training\\, Halle\r\n"
      "END:VEVENT\r\nBEGIN:VEVENT\r\nDTSTART:20261020T080000Z\r\nSUMMARY:UTC 10:00 local\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nDTSTART:20261001T070000Z\r\nRRULE:FREQ=DAILY;COUNT=3\r\nSUMMARY:past daily\r\nEND:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  char* b = strdup(syn);
  struct tm t0 = {};
  t0.tm_year = 126; t0.tm_mon = 9; t0.tm_mday = 12; t0.tm_isdst = -1;
  int64_t from = mktime(&t0);
  int n = ical_parse(b, strlen(b), from, from + 400LL * 86400, ev, 16, 0, lm);
  printf("synthetic (from 12.10.2026): %d\n", n);
  dump(ev, n);
  int fails = 0;
  if (n < 5 || strcmp(ev[0].title, "Training, Halle") != 0) { puts("FAIL order/unescape"); fails++; }
  for (int i = 0; i < n; ++i) if (strstr(ev[i].title, "past")) { puts("FAIL past daily"); fails++; }
  bool has_15 = false;
  for (int i = 0; i < n; ++i) { time_t t = ev[i].start; struct tm lt; localtime_r(&t, &lt); if (lt.tm_mday == 15 && lt.tm_mon == 9) has_15 = true; }
  if (has_15) { puts("FAIL exdate"); fails++; }
  if (argc > 1) {
    size_t len;
    char* c = slurp(argv[1], &len);
    n = ical_parse(c, len, now, now + 60LL * 86400, ev, 16, 0, lm);
    printf("%s: %d upcoming\n", argv[1], n);
    dump(ev, n);
  }
  printf("fails: %d\n", fails);
  return fails;
}
