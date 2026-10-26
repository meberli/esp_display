#pragma once
// Co4 BFF (WunderGraph) client pieces for the live data source. Pure functions
// (no ESP-IDF, no LVGL): request paths, time windows and capped parsers that map
// the real BFF answers onto the co4_data interface. Host-testable.
//
// Operations (co4-backend-bff, .wundergraph/operations/):
//  - zevGetZoneEnergyStats(zoneNiid: BigInt!, start: Timestamp!, end: Timestamp!, period: String)
//      GET {BFF}/operations/zevGetZoneEnergyStats?zoneNiid=…&start=…&end=…&period=…
//      -> {"data":{"energyStats":{"start":ms,"end":[ms…],"sources":[{"type":"GRID|PV|BATTERY",
//          "category":…,"production":[x|null…]}]|null,"partialSources":bool}}}
//      sources == null: billing blocks not ready, retry in >= 60 s.
//  - zevGetConsumptionZones(niid: BigInt! = site niid)
//      GET {BFF}/operations/zevGetConsumptionZones?niid=…
//      -> {"data":{"zones":[{"id":int,"niid":BigInt,"zoneType":"PRIVATE|SHARED|FACILITY",
//          "details":{"name":[{"value":"…"}]},…}]}}
// Timestamps are Unix milliseconds (UTC). period: "<n>S" seconds, "<n>M" months,
// the ZEV pilet also uses "1D". Blocks are aligned on start.
#include <stddef.h>
#include <stdint.h>

#include "co4_data.h"

enum class Co4BffRc : uint8_t { Ok, NotReady, Error };

// true if s is a plain decimal niid (1..19 digits). Everything that goes into a
// request path is checked with this first.
bool co4_bff_niid_ok(const char* s);

// Operation path + query ("zevGetZoneEnergyStats?zoneNiid=…"), to append to
// "{BFF}/operations/". false if the niid is not valid or n is too small.
bool co4_bff_stats_path(char* out, size_t n, const char* zone_niid, int64_t start_ms, int64_t end_ms,
                        const char* period);
bool co4_bff_zones_path(char* out, size_t n, const char* site_niid);

// The three windows the dashboard needs, in local time (localtime_r/mktime):
// today 15 min (end = now rounded up to the next 15 min), this month per day
// (end = tomorrow 00:00), this year per month (end = first of next month).
struct Co4BffWindow {
  Co4Res res;
  int64_t start_ms, end_ms, step_ms;  // step 0 for months
  const char* period;                 // "900S", "1D", "1M"
};
void co4_bff_windows(int64_t now_ms, Co4BffWindow out[3]);

// Parses a zevGetZoneEnergyStats answer into v[kCo4SeriesN][cap] (energy per
// block as delivered, unit see co4_bff.cpp). GRID -> kCo4Bezug, PV -> kCo4Eigen
// (PV energy from the ZEV allocated to the zone); BATTERY and the site-only
// series (Einspeisung, PV-Produktion) stay NAN. Values beyond cap are dropped.
Co4BffRc co4_bff_parse_stats(const char* json, size_t len, float* v, int cap, uint16_t* n, bool* partial,
                             char* err, size_t err_n);

// Parses a zevGetConsumptionZones answer. Zone id = niid (decimal), name = the
// system object name. Only these fields are read (filter): occupations, e-mail
// addresses and accounts in the answer are never stored.
bool co4_bff_parse_zones(const char* json, size_t len, Co4ZoneList* out, char* err, size_t err_n);

// kWh per block -> mean kW over the block.
void co4_bff_energy_to_power(float* v, int n, int64_t step_ms);
