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
//  - zevGetSiteEnergyStats(siteNiid: BigInt!, start, end: Timestamp!, period: String)
//      GET {BFF}/operations/zevGetSiteEnergyStats?siteNiid=…&start=…&end=…&period=…
//      -> {"data":{"energyStats":{"start","end":[…],"gridProduction","feedIn","selfConsumption",
//          "pvProduction","pvConsumption","consumption","batteryProduction","batteryConsumption",
//          "batteryFromLocalPv": [x|null…] each optional (undefined if the site has no such
//          metering points),"sources":[…],"partialSources":bool}}}
//      energyStats is null if the site is unknown / not readable (zev_getSite null).
//  - zevGetZone(niid: BigInt!) -> {"data":{"zone":{…,"site":{"niid":…,…}}}}: the site of a zone.
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
bool co4_bff_site_stats_path(char* out, size_t n, const char* site_niid, int64_t start_ms, int64_t end_ms,
                             const char* period);
bool co4_bff_zone_path(char* out, size_t n, const char* zone_niid);

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
// (PV energy from the ZEV allocated to the zone), BATTERY -> kCo4BatEntladen
// (energy the battery delivered to the zone, shown separately); the site-only
// series (Einspeisung, PV-Produktion) stay NAN. Values beyond cap are dropped.
Co4BffRc co4_bff_parse_stats(const char* json, size_t len, float* v, int cap, uint16_t* n, bool* partial,
                             char* err, size_t err_n);

// Parses a zevGetConsumptionZones answer. Zone id = niid (decimal), name = the
// system object name. Only these fields are read (filter): occupations, e-mail
// addresses and accounts in the answer are never stored.
// site_niid (the site the list was requested for) is stored with every zone.
bool co4_bff_parse_zones(const char* json, size_t len, const char* site_niid, Co4ZoneList* out, char* err,
                         size_t err_n);

// Parses a zevGetSiteEnergyStats answer into v[kCo4SeriesN][cap]: Bezug =
// gridProduction, Einspeisung = feedIn, Pv = pvProduction, Eigen =
// selfConsumption, BatLaden = batteryConsumption, BatEntladen =
// batteryProduction. Missing arrays / null values stay NAN. Ok or Error.
Co4BffRc co4_bff_parse_site_stats(const char* json, size_t len, float* v, int cap, uint16_t* n, bool* partial,
                                  char* err, size_t err_n);

// Zone 15-min curve: Co4 leaves blocks without zone energy null (the 1D total
// equals the sum of the non-null blocks). Up to the last block that has any
// value, null blocks of a delivered series become 0 so the line is continuous;
// trailing blocks (not computed yet) stay NAN. v is [kCo4SeriesN][cap].
void co4_bff_fill_gaps(float* v, int cap, int n);

// zevGetZone answer -> site niid (decimal). false if missing.
bool co4_bff_parse_zone_site(const char* json, size_t len, char* site, size_t site_n, char* err, size_t err_n);

// --- Object tree (zone choice) ----------------------------------------------------
// Sources (co4-backend-bff .wundergraph/operations): co4CoreNavRoots (no input,
// data.roots[] of fragment co4Gi) and co4CoreNavGet(id: BigInt, hidden: Boolean)
// (data.object.children[] of fragment co4ChildGi). Name = properties[] entry with
// label "name" (co4-frontend-app graph.ts); a ZEV zone is a natures[] entry with
// nature.code "zev_Zone" (co4-frontend-pilets-zev), its niid is that entry's id.
// soid "" = roots path.
bool co4_bff_nav_path(char* out, size_t n, const char* soid);
// roots: true for a co4CoreNavRoots answer. Fills out->n/total/it, sorted by name
// (path/depth untouched). Only id, name, natures and hasChildren are kept.
bool co4_bff_parse_nav(const char* json, size_t len, bool roots, Co4NavLevel* out, char* err, size_t err_n);

// kWh per block -> mean kW over the block.
void co4_bff_energy_to_power(float* v, int n, int64_t step_ms);
