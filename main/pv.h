#pragma once
// PV (Growatt 6000UE via RS485 -> NJIOT TCP232 gateway, Modbus TCP): pure
// logic, no ESP-IDF, host tested in tools/test_pv.cpp.
//
// STRICTLY READ-ONLY: only function 04 (read input registers) and, once,
// function 03 (holding registers 0..9, rated power). There is no code path that
// builds any other function code (no 05/06/0F/10).
//
// Input register map (U16; U32 = high word first):
//  0 status (0 waiting, 1 normal, 3 fault)   1-2 Ppv 0.1 W
//  3 Vpv1 0.1 V  4 Ipv1 0.1 A  5-6 Ppv1      7 Vpv2  8 Ipv2  9-10 Ppv2
//  11-12 Pac 0.1 W   13 Fac 0.01 Hz   14/18/22 Vac L1-3 0.1 V   15/19/23 Iac 0.1 A
//  26-27 Eac today 0.1 kWh   28-29 Eac total 0.1 kWh   32 temperature 0.1 C

#include <stddef.h>
#include <stdint.h>

struct PvReading {
  bool valid;            // decoded from a full FC04 0..44 answer
  float pv_w;            // DC input power
  float pv1_v, pv1_a, pv1_w;
  float pv2_v, pv2_a, pv2_w;
  float ac_w;            // AC output power
  float grid_hz;
  float grid_v[3];       // L1..L3
  float grid_a[3];
  float today_kwh, total_kwh;
  float temp_c;
  int status;            // 0 waiting, 1 normal, 3 fault
};

constexpr uint16_t kPvRegStart = 0;
constexpr uint16_t kPvRegCount = 45;   // FC04 0..44 in one request
constexpr uint8_t kPvFcInput = 0x04;
constexpr uint8_t kPvFcHolding = 0x03;

// Modbus TCP (MBAP) read request: 12 bytes. fc must be 03 or 04 (anything else:
// returns 0 and writes nothing - the panel never writes to the inverter).
size_t pv_mb_request(uint8_t* out, size_t cap, uint16_t tid, uint8_t unit, uint8_t fc, uint16_t start, uint16_t count);

// Parse a response in buf[0..n). Returns the frame length (> 0) when complete and
// valid (regs[0..count) filled), 0 = need more bytes, -1 = garbage / mismatch
// (tid, unit, fc, byte count), -2 = Modbus exception (*exc = code).
int pv_mb_response(const uint8_t* buf, size_t n, uint16_t tid, uint8_t unit, uint8_t fc, uint16_t count, uint16_t* regs,
                   uint8_t* exc);

// Decode FC04 registers 0..44 (n >= 33 needed).
bool pv_decode_input(const uint16_t* regs, size_t n, PvReading* out);

// Stale: last good reading older than kPvStaleS.
constexpr int64_t kPvStaleS = 60;
bool pv_stale(int64_t now, int64_t last);

// --- Daily PV production (separate NVS blob "pv_hist"; the whatwatt history is
// unchanged). Today's value is the inverter's own "Eac today" counter.
constexpr int kPvDays = 8;
struct PvDay {
  int32_t ymd;   // 0 = empty
  float kwh;
};
struct PvHist {
  uint32_t magic;
  int32_t ymd;         // today (local)
  float today;         // kWh today (inverter Eac today), -1 = nothing yet today
  float prev_final;    // last value of the previous day (morning: inverter may still report it before its reset)
  uint8_t fresh;       // 1 = the inverter's counter is known to belong to today
  uint8_t pad[3];
  PvDay days[kPvDays];  // [0] = yesterday
};
constexpr uint32_t kPvHistMagic = 0x50480001;
void pv_hist_clear(PvHist* h);
// Feed a good reading (local date ymd). Returns 1 if changed, 2 if a new day started (persist), 0 otherwise.
int pv_hist_update(PvHist* h, int32_t ymd, float eac_today);
// kWh for a finished day, < 0 if unknown.
float pv_hist_day(const PvHist& h, int32_t ymd);

// --- Hourly / 15-min PV production (separate NVS blob "pv_hours", today only).
// Integrates the AC power of the good 10 s readings (trapezoid; a pair of
// readings more than kPvIntGapS apart is not integrated: night, outage, reboot)
// and snapshots the inverter's "Eac today" at the first fresh reading of each
// quarter hour. pv_hours_scaled() scales the integrated hours so their sum
// matches the Eac delta over the covered period (Eac has 0.1 kWh steps).
constexpr int64_t kPvIntGapS = 60;
constexpr int kPvQuarters = 96;
struct PvHours {
  uint32_t magic;
  int32_t ymd;          // day of the buckets (local), 0 = none
  int64_t last_t;       // time of the last integrated reading (0 = none)
  float last_w;
  int16_t last_q;       // quarter of the last reading (-1 none)
  uint16_t pad;
  float wh[24];         // integrated Wh per hour
  float q_wh[kPvQuarters];
  float eac_q[kPvQuarters];  // Eac today (kWh) at the first fresh reading in the quarter, -1 = none
};
constexpr uint32_t kPvHoursMagic = 0x50510001;
void pv_hours_clear(PvHours* h, int32_t ymd = 0);
// Feed a good reading: local ymd, unix time t, local seconds of the day, AC power, and
// Eac today if it is known to belong to today (PvHist.fresh), else < 0.
// Returns bit 1 = changed, 2 = entered a new hour (persist), 4 = new day.
int pv_hours_add(PvHours* h, int32_t ymd, int64_t t, int sod, float ac_w, float eac_today);
// Hourly kWh for today, scaled to the Eac delta when plausible. eac_now < 0: unscaled.
// covered[i] (optional) = the hour has integrated samples. Returns the scale factor used (1 = none).
float pv_hours_scaled(const PvHours& h, float eac_now, float out_kwh[24], bool covered[24] = nullptr);
// Which hours have a known PV value: integrated samples, or before the first sample of the
// day while the inverter had produced nothing yet (first Eac snapshot < 0.05 kWh). Hours
// without samples after that are 0 when the inverter slept, unknown otherwise; the caller
// treats them as unknown if the meter shows export in them.
void pv_hours_known(const PvHours& h, const bool covered[24], bool known[24]);
