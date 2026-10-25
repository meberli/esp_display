#pragma once
// Energie (whatwatt Go smart meter reader): pure logic, no ESP-IDF, host tested
// in tools/test_energy.cpp.
//  - energy_parse_report: GET /api/v1/report JSON (defensive: every field optional)
//  - Tariff: flat rate or HT/NT by weekday time windows, automatic switch date
//  - EnergyHist: today (hourly) and the last days from counter snapshots

#include <stddef.h>
#include <stdint.h>

struct EnergyReading {
  bool ok;              // meter.status OK and the import counter present
  float p_in_kw;        // instantaneous import (A+)
  float p_out_kw;       // instantaneous export (A-)
  float i_a[3];         // phase currents, < 0: not reported
  double e_in, e_in_t1, e_in_t2;    // kWh counters (t1/t2 < 0: not reported)
  double e_out, e_out_t1, e_out_t2;
  int tariff;           // report.tariff 1/2, 0 = not reported
  bool has_water;
  double water_m3;      // volume[bus].cumulative
  int64_t ts;           // report.date_time_utc (Unix seconds), 0 = missing: electricity meter time
  int64_t water_ts;     // volume[bus].date_time (with its UTC offset), 0 = missing: M-Bus reading time
  uint32_t meter;       // hash of meter.id (meter change guard)
  double er_in, er_out; // reactive kvarh totals 3.8.0 / 4.8.0 as delivered by the meter, < 0: not reported
  double er_q[4];       // reactive kvarh per quadrant Q1..Q4 (OBIS 5.8.0..8.8.0), < 0: not reported
  char meter_id[24];    // meter.id (truncated), "" = missing
};

// water_bus: M-Bus number of the water meter (0 = none).
bool energy_parse_report(const char* json, size_t len, int water_bus, EnergyReading* out);

// --- Tariff --------------------------------------------------------------------
enum TariffModel : uint8_t { kTariffAuto = 0, kTariffFlat = 1, kTariffHtNt = 2 };
struct Tariff {
  uint8_t model;       // kTariffAuto: flat before switch_ymd, HT/NT from then on
  uint16_t flat;       // Rp./kWh x 100 (2727 = 27.27 Rp.)
  uint16_t ht, nt;     // Rp./kWh x 100
  uint16_t feed;       // feed-in remuneration Rp./kWh x 100
  uint16_t water;      // CHF/m3 x 100
  uint16_t win[2][2];  // HT windows Mon-Fri, minutes [from, to); from == to: unused
  int32_t switch_ymd;  // 20270101
};
void tariff_defaults(Tariff* t);
enum TariffSlot : uint8_t { kSlotFlat = 0, kSlotHt = 1, kSlotNt = 2 };
TariffSlot tariff_slot(const Tariff& t, int64_t when);  // local time (TZ)
uint16_t tariff_price(const Tariff& t, int64_t when);   // Rp./kWh x 100 for imports at `when`

// Text fields of Einstellungen > Energie. Return false on malformed input.
bool energy_parse_price(const char* s, uint16_t* x100);           // "27.27", "27,3", "30"
bool energy_parse_windows(const char* s, uint16_t win[2][2]);     // "06:00-10:00,17:00-21:00" (1 or 2)
bool energy_parse_date(const char* s, int32_t* ymd);              // "2027-01-01"
void energy_format_price(char* out, size_t n, uint16_t x100);     // "27.27"
void energy_format_windows(char* out, size_t n, const uint16_t win[2][2]);

// --- History -------------------------------------------------------------------
constexpr int kEnergyDays = 8;
constexpr uint8_t kDayApprox = 1;       // incomplete: started mid-day, gap over midnight, ...
constexpr uint8_t kDayWaterApprox = 2;

struct EnergyDay {
  int32_t ymd;   // 0 = empty
  float in_kwh, out_kwh, water_l, cost_chf, credit_chf;
  uint8_t flags;
};

struct EnergyHist {
  uint32_t magic;
  uint32_t meter;
  int32_t ymd;          // today (local)
  int8_t hour;          // hour of the last reading
  uint8_t flags;        // today: kDayApprox / kDayWaterApprox
  uint8_t have, have_water;
  double day_in, day_out, day_water;     // counters at the start of today
  double last_in, last_out, last_water;  // last accepted reading
  int64_t last_ts;                       // panel time of the last accepted reading
  float h_in[24], h_out[24], h_water[24];  // kWh / kWh / litres per hour, today
  float cost, credit;                    // CHF today (imports at the slot price, export credit)
  EnergyDay days[kEnergyDays];           // [0] = yesterday, older after
};
constexpr uint32_t kEnergyHistMagic = 0x45480001;

// Result bits of energy_hist_update.
constexpr int kHistChanged = 1;    // accepted
constexpr int kHistHour = 2;       // a new hour started (persist)
constexpr int kHistDay = 4;        // a new day started (persist)
constexpr int kHistReset = 8;      // first reading / meter changed (persist)
constexpr int kHistRejected = 16;  // counter went backwards: ignored
constexpr int kHistLate = 32;      // a late reading corrected a finished day (persist once)

// Water is different: every increase goes to the previous local day (see
// book_water in energy.cpp); only electricity uses the meter time below.
// Booking by meter time: each delta goes to the local hour/day of its meter
// timestamp (minus 1 s: a reading at 00:00:00 closes the previous interval),
// not the panel's receive time. A delta spanning several hours is booked as a
// whole in the hour of its timestamp (not split). Fallback to the receive time
// when the timestamp is missing, more than kTsAheadS ahead, before 2024 or
// older than the previous one of the same meter. Deltas for a finished day in
// the 8-day ring are added to that day (with the tariff at that time); older
// ones are dropped (the counter baseline still advances, nothing is counted twice).
constexpr int64_t kTsAheadS = 120;
struct EnergyTsState {  // runtime only (not persisted): last meter timestamps
  int64_t last_e, last_w;
};
constexpr int64_t kHistGapS = 900; // longer gaps over midnight mark both days approximate

void energy_hist_clear(EnergyHist* h);
// Day before ymd (YYYYMMDD).
int32_t energy_prev_ymd(int32_t ymd);
// Water of the previous local day (the meter reports once a day), < 0 if unknown; *flags = that day's flags.
float energy_yesterday_water_l(const EnergyHist& h, uint8_t* flags = nullptr);
int energy_hist_update(EnergyHist* h, const EnergyReading& r, int64_t now, const Tariff& t, EnergyTsState* ts = nullptr);
// Effective booking time for a meter timestamp (see above); updates *last.
int64_t energy_book_time(int64_t meter_ts, int64_t now, int64_t* last);
// Today so far.
float energy_today_in(const EnergyHist& h);
float energy_today_out(const EnergyHist& h);
float energy_today_water_l(const EnergyHist& h);
