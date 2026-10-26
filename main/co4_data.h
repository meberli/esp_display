#pragma once
// Co4 Energie: internal data interface between the data source (later the Co4
// BFF, for now the fixture) and the UI. No LVGL, no ESP-IDF: host-testable.
//
// The Co4 measurement series endpoint is not known yet (it comes from Co4 with
// API examples). Nothing here guesses it: a source only has to fill Co4Data.
// Times are UTC milliseconds like everywhere in the Co4 API.

#include <stddef.h>
#include <stdint.h>

// Battery: kCo4BatLaden = charged into the battery, kCo4BatEntladen = delivered
// by the battery. Shown only when the source delivers them (else NAN), never
// added to Bezug / Eigenverbrauch.
enum Co4Series : uint8_t { kCo4Bezug, kCo4Einspeisung, kCo4Pv, kCo4Eigen, kCo4BatLaden, kCo4BatEntladen, kCo4SeriesN };

enum class Co4Res : uint8_t { Min15, Hour, Day, Month };

constexpr int kCo4MaxPoints = 96;  // one day in 15 min steps
constexpr int kCo4MaxBars = 31;    // days of a month (months of a year use 12)

// One block of equally spaced values per series. NAN = no value (future, gap).
// Curves carry power ("kW"), totals energy ("kWh").
template <int N>
struct Co4Block {
  Co4Res res;
  int64_t t0_ms;    // start of the first interval
  int64_t step_ms;  // Min15/Hour/Day: fixed; Month: 0 (calendar months from t0)
  uint16_t n;       // used points (<= N)
  char unit[6];
  float v[kCo4SeriesN][N];
};
using Co4Curve = Co4Block<kCo4MaxPoints>;
using Co4Totals = Co4Block<kCo4MaxBars>;

// Whole ZEV site (zevGetSiteEnergyStats): Bezug = gridProduction, Einspeisung =
// feedIn, Pv = pvProduction, Eigen = selfConsumption, battery from
// batteryConsumption / batteryProduction. Fetched after the zone; a site
// problem never hides the zone data.
enum class Co4SiteState : uint8_t { None, Ok, Error };  // None: no site niid known (yet)
struct Co4SiteData {
  Co4SiteState state;
  bool partial;   // partialSources: trailing blocks may still be null
  char err[48];
  Co4Curve day;   // today, 15 min power
  Co4Totals days;
  Co4Totals months;
};

struct Co4Data {
  bool example;        // fixture / example data: the UI shows "Beispieldaten"
  int64_t fetched_ms;  // when the source produced it
  Co4Curve day;        // today, 15 min power
  Co4Totals days;      // this month, energy per day
  Co4Totals months;    // this year, energy per month
  Co4SiteData site;    // whole site
};

// Autarkie = Eigenverbrauch / (Eigenverbrauch + Bezug),
// Eigenverbrauchsquote = Eigenverbrauch / PV. Over points [from, to) of a block,
// NAN points skipped. false if nothing to compute (pct are 0 then).
struct Co4Ratio {
  float sum[kCo4SeriesN];
  float autarky_pct;
  float self_pct;
};
bool co4_ratio(const float (*v)[kCo4MaxBars], int from, int to, Co4Ratio* out);

// A data source. fetch() runs on the Co4 task (core 0) and may block. bearer is
// the current access token (RAM only) or nullptr for the fixture.
struct Co4Source {
  const char* name;
  bool example;
  bool (*fetch)(const char* zone_id, const char* bearer, int64_t now_ms, Co4Data* out, char* err, size_t err_n);
};
const Co4Source& co4_fixture_source();
// Placeholder without network (host builds); the panel uses the BFF source in co4_task.cpp.
const Co4Source& co4_live_source();

// Fixture: anonymised synthetic example data for the day around now_ms (local
// time via localtime_r). Deterministic for a given now_ms.
void co4_fixture_fill(int64_t now_ms, Co4Data* out);

// --- Zones --------------------------------------------------------------------
// zevMyZones (vorgeschlagen, noch nicht vorhanden): a BFF operation listing the
// zones of the logged-in user. Proposed answer: {"data":[{"id":"…","name":"…"}]}.
// Until it exists the zone list comes from the fixture.
constexpr int kCo4MaxZones = 12;
struct Co4Zone {
  char id[40];
  char name[48];
  char site[20];  // niid of the ZEV site the zone belongs to, "" if unknown
};
struct Co4ZoneList {
  int n;
  Co4Zone z[kCo4MaxZones];
};
// Parses the proposed zevMyZones answer (also {"errors":[…]}). err: short German text.
bool co4_zones_parse(const char* json, size_t len, Co4ZoneList* out, char* err, size_t err_n);
// Example zones (placeholders, no real zone IDs).
void co4_zones_fixture(Co4ZoneList* out);
extern const char kCo4ZonesFixtureJson[];
