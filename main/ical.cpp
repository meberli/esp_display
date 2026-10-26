#include "ical.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int kMaxExdates = 24;
constexpr int kMaxIter = 20000;  // occurrence loop guard (daily rules from long ago)

struct Ev {
  bool in = false;
  bool all_day = false;
  bool utc = false;
  struct tm st = {};
  bool have_start = false;
  char summary[64] = {};
  uint32_t uid = 0;
  // RRULE
  bool rrule = false;
  char freq = 0;  // D W M Y
  int interval = 1;
  int count = 0;
  bool have_until = false;
  int64_t until = 0;
  uint8_t byday = 0;  // bit 0 = Sunday
  int64_t exdate[kMaxExdates] = {};
  int nex = 0;
};

int64_t days_from_civil(int y, unsigned m, unsigned d) {  // H. Hinnant
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

int64_t utc_seconds(const struct tm& t) {
  return days_from_civil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400 + t.tm_hour * 3600 + t.tm_min * 60 +
         t.tm_sec;
}

// "20261012", "20261012T150000", "20261012T150000Z" -> tm (+ flags)
bool parse_dt(const char* v, struct tm* t, bool* utc, bool* date_only) {
  memset(t, 0, sizeof(*t));
  int y, mo, d, h = 0, mi = 0, s = 0;
  if (sscanf(v, "%4d%2d%2d", &y, &mo, &d) != 3) return false;
  *date_only = strlen(v) < 15 || v[8] != 'T';
  if (!*date_only && sscanf(v + 9, "%2d%2d%2d", &h, &mi, &s) != 3) return false;
  *utc = !*date_only && v[15] == 'Z';
  t->tm_year = y - 1900;
  t->tm_mon = mo - 1;
  t->tm_mday = d;
  t->tm_hour = h;
  t->tm_min = mi;
  t->tm_sec = s;
  t->tm_isdst = -1;
  return true;
}

int64_t to_unix(struct tm t, bool utc, LocalMktime lm) {
  if (utc) return utc_seconds(t);
  t.tm_isdst = -1;
  return lm(&t);
}

int weekday(const struct tm& t) {  // 0 = Sunday
  int64_t days = days_from_civil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
}

void add_days(struct tm* t, int n) {
  int64_t days = days_from_civil(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday) + n;
  // civil_from_days
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  t->tm_year = static_cast<int>(y + (m <= 2)) - 1900;
  t->tm_mon = static_cast<int>(m) - 1;
  t->tm_mday = static_cast<int>(d);
}

int days_in_month(int y, int m0) {
  static const int k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m0 == 1 && leap ? 29 : k[m0];
}

// Unescape \, \; \n and strip; copy at most n-1 bytes without cutting UTF-8.
void copy_text(char* out, size_t n, const char* v) {
  size_t o = 0;
  for (const char* p = v; *p != '\0' && o + 1 < n; ++p) {
    char c = *p;
    if (c == '\\' && p[1] != '\0') {
      ++p;
      c = (*p == 'n' || *p == 'N') ? ' ' : *p;
    }
    out[o++] = c;
  }
  while (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0x80) --o;  // partial sequence
  if (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0xC0) --o;
  out[o] = '\0';
}

void parse_rrule(Ev* e, const char* v, LocalMktime lm) {
  e->rrule = true;
  char tmp[256];
  snprintf(tmp, sizeof(tmp), "%s", v);
  char* save = nullptr;
  for (char* part = strtok_r(tmp, ";", &save); part != nullptr; part = strtok_r(nullptr, ";", &save)) {
    char* eq = strchr(part, '=');
    if (eq == nullptr) continue;
    *eq = '\0';
    const char* val = eq + 1;
    if (strcmp(part, "FREQ") == 0) {
      e->freq = strcmp(val, "DAILY") == 0 ? 'D' : strcmp(val, "WEEKLY") == 0 ? 'W'
              : strcmp(val, "MONTHLY") == 0 ? 'M' : strcmp(val, "YEARLY") == 0 ? 'Y' : 0;
    } else if (strcmp(part, "INTERVAL") == 0) {
      e->interval = atoi(val) > 0 ? atoi(val) : 1;
    } else if (strcmp(part, "COUNT") == 0) {
      e->count = atoi(val);
    } else if (strcmp(part, "UNTIL") == 0) {
      struct tm t;
      bool utc, date_only;
      if (parse_dt(val, &t, &utc, &date_only)) {
        if (date_only) {
          t.tm_hour = 23;
          t.tm_min = 59;
          t.tm_sec = 59;
        }
        e->until = to_unix(t, utc, lm);
        e->have_until = true;
      }
    } else if (strcmp(part, "BYDAY") == 0) {
      static const char* kDay[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
      for (int i = 0; i < 7; ++i) {
        if (strstr(val, kDay[i]) != nullptr) e->byday |= static_cast<uint8_t>(1u << i);
      }
    } else if (strcmp(part, "BYMONTHDAY") == 0 || strcmp(part, "BYSETPOS") == 0) {
      e->freq = 0;  // unsupported: show the first occurrence only
    }
  }
}

bool excluded(const Ev& e, int64_t t) {
  for (int i = 0; i < e.nex; ++i) {
    if (e.exdate[i] == t) return true;
  }
  return false;
}

int emit(const Ev& e, int64_t from, int64_t to, CalEvent* out, int n, int cap, uint8_t kind, LocalMktime lm) {
  if (!e.have_start) return n;
  CalEvent ce = {};
  ce.all_day = e.all_day;
  ce.kind = kind;
  snprintf(ce.title, sizeof(ce.title), "%s", e.summary[0] != '\0' ? e.summary : "(ohne Titel)");
  ce.uid = e.uid != 0 ? e.uid : cal_hash(ce.title);
  // All-day events count until the end of their day.
  const int64_t span = e.all_day ? 86399 : 0;
  if (!e.rrule || e.freq == 0) {
    ce.start = to_unix(e.st, e.utc, lm);
    if (ce.start + span >= from && ce.start < to) n = cal_insert(out, n, cap, ce);
    return n;
  }
  struct tm cur = e.st;
  int made = 0;
  for (int it = 0; it < kMaxIter; ++it) {
    bool candidate = true;
    if (e.freq == 'W' && e.byday != 0) {
      // Step one day at a time; keep days in BYDAY in weeks that match INTERVAL.
      int64_t d0 = days_from_civil(e.st.tm_year + 1900, e.st.tm_mon + 1, e.st.tm_mday) - weekday(e.st);
      int64_t dc = days_from_civil(cur.tm_year + 1900, cur.tm_mon + 1, cur.tm_mday);
      int64_t week = (dc - d0) / 7;
      candidate = (e.byday & (1u << weekday(cur))) != 0 && week % e.interval == 0;
    }
    if (candidate) {
      int64_t t = to_unix(cur, e.utc, lm);
      if (e.have_until && t > e.until) break;
      if (t >= to) break;
      made++;
      if (!excluded(e, t) && t + span >= from) {
        ce.start = t;
        n = cal_insert(out, n, cap, ce);
      }
      if (e.count > 0 && made >= e.count) break;
    }
    switch (e.freq) {
      case 'D':
        add_days(&cur, e.interval);
        break;
      case 'W':
        add_days(&cur, e.byday != 0 ? 1 : 7 * e.interval);
        break;
      case 'M': {
        int y = cur.tm_year + 1900;
        int m = cur.tm_mon;
        // Skip months that do not have this day (e.g. the 31st).
        for (int k = 0; k < 48; ++k) {
          m += e.interval;
          y += m / 12;
          m %= 12;
          if (e.st.tm_mday <= days_in_month(y, m)) break;
        }
        cur.tm_year = y - 1900;
        cur.tm_mon = m;
        cur.tm_mday = e.st.tm_mday;
        break;
      }
      case 'Y':
        cur.tm_year += e.interval;
        if (cur.tm_mon == 1 && cur.tm_mday == 29 && days_in_month(cur.tm_year + 1900, 1) == 28) {
          cur.tm_mday = 28;  // Feb 29th birthdays on the 28th
        } else {
          cur.tm_mday = e.st.tm_mday;
        }
        break;
    }
  }
  return n;
}

}  // namespace

int cal_insert(CalEvent* out, int n, int cap, const CalEvent& e) {
  int pos = n;
  while (pos > 0 && out[pos - 1].start > e.start) --pos;
  if (pos >= cap) return n;
  int last = n < cap ? n : cap - 1;
  for (int i = last; i > pos; --i) out[i] = out[i - 1];
  out[pos] = e;
  return n < cap ? n + 1 : n;
}

int ical_parse(char* buf, size_t len, int64_t from, int64_t to, CalEvent* out, int cap, uint8_t kind,
               LocalMktime lm) {
  // Unfold in place: CRLF/LF followed by space or tab joins the lines.
  size_t w = 0;
  for (size_t r = 0; r < len; ++r) {
    char c = buf[r];
    if (c == '\r') continue;
    if (c == '\n' && r + 1 < len && (buf[r + 1] == ' ' || buf[r + 1] == '\t')) {
      ++r;
      continue;
    }
    buf[w++] = c;
  }
  buf[w] = '\0';  // caller allocates len + 1
  int n = 0;
  static Ev e;  // large; parsers run in one task only
  e = Ev();
  char* save = nullptr;
  for (char* line = strtok_r(buf, "\n", &save); line != nullptr; line = strtok_r(nullptr, "\n", &save)) {
    if (strcmp(line, "BEGIN:VEVENT") == 0) {
      e = Ev();
      e.in = true;
      continue;
    }
    if (!e.in) continue;
    if (strcmp(line, "END:VEVENT") == 0) {
      n = emit(e, from, to, out, n, cap, kind, lm);
      e.in = false;
      continue;
    }
    char* colon = strchr(line, ':');
    if (colon == nullptr) continue;
    *colon = '\0';
    const char* value = colon + 1;
    char* semi = strchr(line, ';');
    const char* params = semi != nullptr ? semi + 1 : "";
    if (semi != nullptr) *semi = '\0';
    if (strcmp(line, "UID") == 0) {
      e.uid = cal_hash(value);
    } else if (strcmp(line, "SUMMARY") == 0) {
      copy_text(e.summary, sizeof(e.summary), value);
    } else if (strcmp(line, "DTSTART") == 0) {
      bool utc = false, date_only = false;
      if (parse_dt(value, &e.st, &utc, &date_only)) {
        e.have_start = true;
        e.utc = utc;
        e.all_day = date_only || (strstr(params, "VALUE=DATE") != nullptr && strstr(params, "DATE-TIME") == nullptr);
      }
    } else if (strcmp(line, "RRULE") == 0) {
      parse_rrule(&e, value, lm);
    } else if (strcmp(line, "EXDATE") == 0) {
      // One or more comma separated values.
      char tmp[200];
      snprintf(tmp, sizeof(tmp), "%s", value);
      char* s2 = nullptr;
      for (char* v = strtok_r(tmp, ",", &s2); v != nullptr && e.nex < kMaxExdates; v = strtok_r(nullptr, ",", &s2)) {
        struct tm t;
        bool utc, date_only;
        if (parse_dt(v, &t, &utc, &date_only)) e.exdate[e.nex++] = to_unix(t, utc, lm);
      }
    }
  }
  return n;
}


uint32_t cal_hash(const char* s, uint32_t h) {
  for (; *s != '\0'; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  return h;
}
