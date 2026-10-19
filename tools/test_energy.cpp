// Host test for main/energy.cpp (whatwatt parser, tariff schedule, history).
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src tools/test_energy.cpp main/energy.cpp -o /tmp/te && TZ=Europe/Zurich /tmp/te
// (run from the repo root: reads tools/testdata/whatwatt_report.json)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "energy.h"

static int fails = 0;
#define CHECK(c, what)                                       \
  do {                                                       \
    if (!(c)) {                                              \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);   \
      fails++;                                               \
    }                                                        \
  } while (0)
#define NEAR(a, b, eps) (fabs((double)(a) - (double)(b)) < (eps))

static int64_t at(int y, int mo, int d, int h, int mi) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}

static EnergyReading rd(double in, double out, double water) {
  EnergyReading r = {};
  r.ok = true; r.e_in = in; r.e_out = out; r.has_water = water >= 0; r.water_m3 = water; r.meter = 42;
  return r;
}

static void test_parse() {
  FILE* f = fopen("tools/testdata/whatwatt_report.json", "rb");
  CHECK(f != nullptr, "sample file");
  if (f == nullptr) return;
  static char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  EnergyReading r;
  CHECK(energy_parse_report(buf, n, 2, &r), "parse sample");
  CHECK(r.ok, "meter OK");
  CHECK(NEAR(r.p_in_kw, 0.609, 1e-6) && r.p_out_kw == 0, "power kW");
  CHECK(NEAR(r.i_a[1], 2.39, 1e-5), "current L2");
  CHECK(NEAR(r.e_in, 28519.384, 1e-6) && NEAR(r.e_out, 19684.866, 1e-6), "energy totals");
  CHECK(NEAR(r.e_in_t1, 7039.711, 1e-6) && NEAR(r.e_in_t2, 21479.674, 1e-6), "import T1/T2");
  CHECK(r.has_water && NEAR(r.water_m3, 1398.985, 1e-9), "water bus 2");
  CHECK(r.ts == at(2026, 10, 17, 19, 30) + 10, "date_time_utc");
  CHECK(r.tariff == 0, "no tariff field");
  CHECK(energy_parse_report(buf, n, 1, &r) && !r.has_water, "no meter on bus 1");
  // Defensive: missing fields, wrong status, garbage
  const char* minimal = "{\"report\":{\"energy\":{\"active\":{\"positive\":{\"total\":5}}}},\"meter\":{\"status\":\"OK\"}}";
  CHECK(energy_parse_report(minimal, strlen(minimal), 2, &r) && r.ok && r.e_in == 5 && r.e_out == 0 && r.i_a[0] < 0 &&
            r.e_in_t1 < 0 && r.ts == 0 && !r.has_water, "minimal report");
  const char* nodata = "{\"report\":{},\"meter\":{\"status\":\"NO DATA\"}}";
  CHECK(energy_parse_report(nodata, strlen(nodata), 2, &r) && !r.ok, "NO DATA -> not ok");
  CHECK(!energy_parse_report("<html>", 6, 2, &r), "HTML page rejected");
  const char* conv = "{\"report\":{\"conv_factor\":10,\"instantaneous_power\":{\"active\":{\"positive\":{\"total\":0.5}}},"
                     "\"energy\":{\"active\":{\"positive\":{\"total\":2}}}},\"meter\":{\"status\":\"OK\"}}";
  CHECK(energy_parse_report(conv, strlen(conv), 0, &r) && NEAR(r.p_in_kw, 5, 1e-6) && r.e_in == 20, "conv_factor");
}

static void test_tariff() {
  Tariff t;
  tariff_defaults(&t);
  // 2026: flat all day
  CHECK(tariff_slot(t, at(2026, 10, 19, 7, 0)) == kSlotFlat && tariff_price(t, at(2026, 10, 19, 7, 0)) == 2727, "2026 flat");
  CHECK(tariff_slot(t, at(2026, 12, 31, 23, 59)) == kSlotFlat, "last flat minute");
  // 2027: Fri 1 Jan 06:00 HT, 05:59 NT, 10:00 NT, 17:00 HT, 21:00 NT
  CHECK(tariff_slot(t, at(2027, 1, 1, 5, 59)) == kSlotNt, "05:59 NT");
  CHECK(tariff_slot(t, at(2027, 1, 1, 6, 0)) == kSlotHt && tariff_price(t, at(2027, 1, 1, 6, 0)) == 3000, "06:00 HT");
  CHECK(tariff_slot(t, at(2027, 1, 1, 9, 59)) == kSlotHt, "09:59 HT");
  CHECK(tariff_slot(t, at(2027, 1, 1, 10, 0)) == kSlotNt && tariff_price(t, at(2027, 1, 1, 10, 0)) == 2567, "10:00 NT");
  CHECK(tariff_slot(t, at(2027, 1, 1, 17, 0)) == kSlotHt, "17:00 HT");
  CHECK(tariff_slot(t, at(2027, 1, 1, 21, 0)) == kSlotNt, "21:00 NT");
  CHECK(tariff_slot(t, at(2027, 1, 2, 7, 0)) == kSlotNt, "Saturday NT");
  CHECK(tariff_slot(t, at(2027, 1, 3, 18, 0)) == kSlotNt, "Sunday NT");
  // DST: Sun 28 Mar 2027 starts summer time; Mon 29 Mar local times still apply
  CHECK(tariff_slot(t, at(2027, 3, 29, 5, 59)) == kSlotNt && tariff_slot(t, at(2027, 3, 29, 6, 0)) == kSlotHt, "after DST start");
  CHECK(tariff_slot(t, at(2027, 11, 1, 6, 30)) == kSlotHt, "after DST end (Mon 1 Nov)");
  int64_t utc6 = at(2027, 3, 29, 6, 0);
  CHECK(tariff_slot(t, utc6 - 3600) == kSlotNt, "one hour before 06:00 CEST is NT");
  // Models
  t.model = kTariffFlat;
  CHECK(tariff_slot(t, at(2027, 1, 4, 7, 0)) == kSlotFlat, "flat model");
  t.model = kTariffHtNt;
  CHECK(tariff_slot(t, at(2026, 10, 19, 7, 0)) == kSlotHt, "HT/NT model in 2026");
  t.model = kTariffAuto;
  t.switch_ymd = 20261019;
  CHECK(tariff_slot(t, at(2026, 10, 19, 7, 0)) == kSlotHt, "custom switch date");
  // Text fields
  uint16_t p;
  CHECK(energy_parse_price("27.27", &p) && p == 2727, "price 27.27");
  CHECK(energy_parse_price("27,3", &p) && p == 2730, "price 27,3");
  CHECK(energy_parse_price("30", &p) && p == 3000, "price 30");
  CHECK(energy_parse_price("2.44", &p) && p == 244, "price 2.44");
  CHECK(!energy_parse_price("", &p) && !energy_parse_price("abc", &p) && !energy_parse_price("1e3", &p), "bad prices");
  uint16_t w[2][2];
  CHECK(energy_parse_windows("06:00-10:00,17:00-21:00", w) && w[0][0] == 360 && w[1][1] == 1260, "windows");
  CHECK(energy_parse_windows("07:00-20:00", w) && w[0][1] == 1200 && w[1][0] == 0 && w[1][1] == 0, "one window");
  CHECK(!energy_parse_windows("10:00-06:00", w) && !energy_parse_windows("", w) && !energy_parse_windows("6-10", w), "bad windows");
  char s[48];
  uint16_t w2[2][2] = {{360, 600}, {1020, 1260}};
  energy_format_windows(s, sizeof(s), w2);
  CHECK(strcmp(s, "06:00-10:00,17:00-21:00") == 0, "format windows");
  energy_format_price(s, sizeof(s), 1004);
  CHECK(strcmp(s, "10.04") == 0, "format price");
  int32_t ymd;
  CHECK(energy_parse_date("2027-01-01", &ymd) && ymd == 20270101, "date");
  CHECK(!energy_parse_date("2027-13-01", &ymd) && !energy_parse_date("1.1.2027", &ymd), "bad dates");
}

static void test_hist() {
  Tariff t;
  tariff_defaults(&t);
  EnergyHist h;
  energy_hist_clear(&h);
  // First reading at 00:05: complete day
  int r = energy_hist_update(&h, rd(100, 50, 10), at(2026, 10, 18, 0, 5), t);
  CHECK((r & kHistReset) && h.flags == 0, "first reading near midnight");
  r = energy_hist_update(&h, rd(101, 50, 10.1), at(2026, 10, 18, 0, 30), t);
  CHECK(r == kHistChanged, "same hour");
  CHECK(NEAR(energy_today_in(h), 1, 1e-6) && NEAR(energy_today_water_l(h), 100, 1e-3), "today in / water");
  CHECK(NEAR(h.h_in[0], 1, 1e-6) && NEAR(h.cost, 0.2727, 1e-5), "hour 0 and flat cost");
  r = energy_hist_update(&h, rd(103, 52, 10.1), at(2026, 10, 18, 13, 0), t);
  CHECK((r & kHistHour) && NEAR(h.h_in[13], 2, 1e-6) && NEAR(h.h_out[13], 2, 1e-6), "hour 13");
  CHECK(NEAR(h.credit, 2 * 0.1004, 1e-5), "feed-in credit");
  // Backwards: rejected, nothing changes
  r = energy_hist_update(&h, rd(102, 52, 10.1), at(2026, 10, 18, 13, 5), t);
  CHECK(r == kHistRejected && NEAR(energy_today_in(h), 3, 1e-6), "backwards rejected");
  r = energy_hist_update(&h, rd(103, 52, 10.0), at(2026, 10, 18, 13, 6), t);
  CHECK(r == kHistRejected, "water backwards rejected");
  // Day rollover (no gap): yesterday closed, today starts at the last reading
  energy_hist_update(&h, rd(104, 52, 10.2), at(2026, 10, 18, 23, 58), t);
  r = energy_hist_update(&h, rd(104.5, 52, 10.25), at(2026, 10, 19, 0, 1), t);
  CHECK((r & kHistDay) && (r & kHistHour), "day rollover");
  CHECK(h.days[0].ymd == 20261018 && NEAR(h.days[0].in_kwh, 4, 1e-5) && NEAR(h.days[0].water_l, 200, 1e-2) &&
            h.days[0].flags == 0, "yesterday record");
  CHECK(NEAR(energy_today_in(h), 0.5, 1e-6) && NEAR(h.h_in[0], 0.5, 1e-6) && h.h_in[13] == 0, "today restarted");
  // 2027 HT/NT: costs per slot
  EnergyHist h2;
  energy_hist_clear(&h2);
  energy_hist_update(&h2, rd(0, 0, -1), at(2027, 1, 4, 5, 0), t);  // Mon
  energy_hist_update(&h2, rd(1, 0, -1), at(2027, 1, 4, 5, 50), t);  // NT
  energy_hist_update(&h2, rd(3, 0, -1), at(2027, 1, 4, 7, 0), t);   // HT
  CHECK(NEAR(h2.cost, 0.2567 + 2 * 0.30, 1e-5), "HT/NT cost by slot");
  CHECK((h2.flags & kDayApprox) && !h2.have_water, "started 05:00: approximate, no water");
  // Gap over midnight (panel off): both days approximate
  r = energy_hist_update(&h2, rd(10, 0, -1), at(2027, 1, 5, 8, 0), t);
  CHECK((r & kHistDay) && (h2.days[0].flags & kDayApprox) && (h2.flags & kDayApprox), "gap marks both days");
  // Meter change: new baseline, finished days kept
  EnergyReading m = rd(5000, 0, -1);
  m.meter = 7;
  r = energy_hist_update(&h2, m, at(2027, 1, 5, 9, 0), t);
  CHECK((r & kHistReset) && energy_today_in(h2) == 0 && h2.days[0].ymd == 20270104, "meter change");
  // Ring keeps 8 days
  EnergyHist h3;
  energy_hist_clear(&h3);
  for (int d = 0; d < 12; ++d) energy_hist_update(&h3, rd(d, 0, 1), at(2026, 11, 1 + d, 12, 0), t);
  CHECK(h3.days[0].ymd == 20261111 && h3.days[7].ymd == 20261104, "8-day ring");
  // DST end day (25 h): hours by local time, no double rollover
  EnergyHist h4;
  energy_hist_clear(&h4);
  energy_hist_update(&h4, rd(0, 0, -1), at(2026, 10, 25, 0, 1), t);
  int64_t t2 = at(2026, 10, 25, 2, 30);
  r = energy_hist_update(&h4, rd(1, 0, -1), t2, t);
  r = energy_hist_update(&h4, rd(2, 0, -1), t2 + 3600, t);  // 02:30 again (winter time)
  CHECK(!(r & kHistDay) && NEAR(h4.h_in[2], 2, 1e-6), "DST end: repeated hour adds up");
  r = energy_hist_update(&h4, rd(3, 0, -1), at(2026, 10, 26, 0, 1), t);
  CHECK((r & kHistDay) && NEAR(h4.days[0].in_kwh, 2, 1e-6), "DST end day closed once (until the last reading)");
}

static EnergyReading rdt(double in, double water, int64_t ts, int64_t wts) {
  EnergyReading r = rd(in, 0, water);
  r.ts = ts;
  r.water_ts = wts;
  return r;
}

static void test_meter_time() {
  Tariff t;
  tariff_defaults(&t);
  // Offsets / Z parsing via a minimal report
  const char* js = "{\"report\":{\"date_time_utc\":\"2026-10-19T06:37:30Z\",\"energy\":{\"active\":{\"positive\":{\"total\":5}}},"
                   "\"volume\":[{\"bus\":2,\"date_time\":\"2026-10-19T08:30:00+02:00\",\"cumulative\":1400.158}]},"
                   "\"meter\":{\"status\":\"OK\",\"id\":\"x\"}}";
  EnergyReading pr;
  CHECK(energy_parse_report(js, strlen(js), 2, &pr), "parse with timestamps");
  CHECK(pr.ts == 1792391850 && pr.water_ts == 1792391400, "utc Z and +02:00 offset parsed");

  // 1) Late water reading after midnight lands on the previous day; today excludes it.
  EnergyHist h;
  energy_hist_clear(&h);
  EnergyTsState ts = {};
  energy_hist_update(&h, rdt(100, 10.000, at(2026, 10, 18, 0, 5), at(2026, 10, 18, 0, 0)), at(2026, 10, 18, 0, 5), t, &ts);
  energy_hist_update(&h, rdt(110, 10.500, at(2026, 10, 18, 23, 59), at(2026, 10, 18, 23, 30)), at(2026, 10, 18, 23, 59), t, &ts);
  // 00:02: panel rolls over; the water reading (240 L) is stamped 00:00:00 = end of yesterday
  int r = energy_hist_update(&h, rdt(110.1, 10.740, at(2026, 10, 19, 0, 1), at(2026, 10, 19, 0, 0)), at(2026, 10, 19, 0, 2), t, &ts);
  CHECK((r & kHistDay) && (r & kHistLate), "rollover + late correction flagged");
  CHECK(h.days[0].ymd == 20261018 && NEAR(h.days[0].water_l, 740, 1e-3), "240 L added to 18.10 (500 + 240)");
  CHECK(NEAR(energy_today_water_l(h), 0, 1e-3) && NEAR(h.h_water[0], 0, 1e-6), "today has no water yet");
  CHECK(NEAR(h.days[0].in_kwh, 10, 1e-4) && NEAR(energy_today_in(h), 0.1, 1e-4) && NEAR(h.h_in[0], 0.1, 1e-4),
        "electricity at 00:01 meter time stays today");
  // Next water reading 01:00 -> today hour 0 (reading at 01:00:00 closes hour 0)
  r = energy_hist_update(&h, rdt(110.2, 10.800, at(2026, 10, 19, 1, 5), at(2026, 10, 19, 1, 0)), at(2026, 10, 19, 1, 5), t, &ts);
  CHECK(NEAR(h.h_water[0], 60, 1e-3) && NEAR(energy_today_water_l(h), 60, 1e-3) && !(r & kHistLate), "hour 0 water, no late flag");
  float sum = 0;
  for (float x : h.h_water) sum += x;
  CHECK(NEAR(sum, energy_today_water_l(h), 1e-3), "hour bars = today (no double counting)");
  // Late electricity at 23:59:58 meter time after rollover adds cost to yesterday at that time's tariff
  EnergyHist e;
  energy_hist_clear(&e);
  EnergyTsState es = {};
  energy_hist_update(&e, rdt(0, -1, at(2026, 10, 18, 0, 0), 0), at(2026, 10, 18, 0, 0), t, &es);
  energy_hist_update(&e, rdt(1, -1, at(2026, 10, 18, 23, 59), 0), at(2026, 10, 18, 23, 59), t, &es);
  r = energy_hist_update(&e, rdt(2, -1, at(2026, 10, 18, 23, 59) + 58, 0), at(2026, 10, 19, 0, 0) + 3, t, &es);
  CHECK((r & kHistLate) && NEAR(e.days[0].in_kwh, 2, 1e-4) && NEAR(e.days[0].cost_chf, 2 * 0.2727, 1e-4) &&
            NEAR(energy_today_in(e), 0, 1e-4), "late kWh + cost on yesterday");

  // 2) Fallbacks: missing, future, pre-2024, backwards -> receive time
  int64_t last = 0;
  const int64_t now = at(2026, 10, 19, 10, 0);
  CHECK(energy_book_time(0, now, &last) == now, "missing -> now");
  CHECK(energy_book_time(now + 121, now, &last) == now && energy_book_time(now + 119, now, &last) == now + 119, "future > 2 min -> now");
  CHECK(energy_book_time(at(2023, 6, 1, 0, 0), now, &last) == now, "pre-2024 -> now");
  CHECK(energy_book_time(now - 600, now, &last) == now, "backwards vs last -> now");
  last = now - 900;
  CHECK(energy_book_time(now - 300, now, &last) == now - 300 && last == now - 300, "monotonic accepted");
  // Water stamped far in the future at runtime -> booked now (hour 10)
  EnergyHist f;
  energy_hist_clear(&f);
  EnergyTsState fs = {};
  energy_hist_update(&f, rdt(0, 1.0, 0, 0), at(2026, 10, 19, 0, 0), t, &fs);
  energy_hist_update(&f, rdt(0, 1.1, 0, at(2027, 1, 1, 0, 0)), now, t, &fs);
  CHECK(NEAR(f.h_water[10], 100, 1e-3), "future water stamp -> receive hour");
  // Older than the ring: dropped, baseline advanced, no double count
  energy_hist_update(&f, rdt(0, 1.2, 0, at(2026, 10, 1, 12, 0)), now + 60, t, &fs);
  CHECK(NEAR(energy_today_water_l(f), 100, 1e-3), "too old: dropped, today unchanged");
  // Past hour of today
  energy_hist_update(&f, rdt(0, 1.25, 0, at(2026, 10, 19, 7, 30)), now + 120, t, &fs);
  CHECK(NEAR(f.h_water[7], 50, 1e-3) && NEAR(energy_today_water_l(f), 150, 1e-3), "past hour of today");

  // 3) DST: autumn 2026-10-25 03:00 CEST -> 02:00 CET; spring 2027-03-28 02:00 -> 03:00.
  EnergyHist d;
  energy_hist_clear(&d);
  EnergyTsState ds = {};
  const int64_t a0 = 1792879200;  // 2026-10-25T00:00:00+02:00 (22:00Z)
  energy_hist_update(&d, rdt(0, 0, a0 + 60, a0 + 60), a0 + 60, t, &ds);
  energy_hist_update(&d, rdt(0, 0.010, 0, a0 + 2 * 3600 + 1800), a0 + 3 * 3600, t, &ds);  // 02:30 CEST
  energy_hist_update(&d, rdt(0, 0.030, 0, a0 + 3 * 3600 + 1800), a0 + 4 * 3600, t, &ds);  // 02:30 CET
  CHECK(NEAR(d.h_water[2], 30, 1e-3) && NEAR(energy_today_water_l(d), 30, 1e-3), "autumn: both 02:xx in hour 2");
  EnergyHist s;
  energy_hist_clear(&s);
  EnergyTsState ss = {};
  const int64_t b0 = 1806188400;  // 2027-03-28T00:00:00+01:00 (23:00Z prev day)
  energy_hist_update(&s, rdt(0, 0, b0 + 60, b0 + 60), b0 + 60, t, &ss);
  energy_hist_update(&s, rdt(0, 0.020, 0, b0 + 2 * 3600 + 600), b0 + 3 * 3600, t, &ss);  // 03:10 CEST
  CHECK(NEAR(s.h_water[3], 20, 1e-3) && NEAR(s.h_water[2], 0, 1e-6), "spring: 01:00Z+10min is 03:10 local");
}

int main() {
  test_parse();
  test_meter_time();
  test_tariff();
  test_hist();
  printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
  return fails ? 1 : 0;
}
