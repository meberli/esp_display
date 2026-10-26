#pragma once
// Data for the ÖV, Kalender and MeteoSchweiz views. Fetched by the network
// task (info_net.cpp), copied into the UI task through the result queue.

#include <stddef.h>
#include <stdint.h>

#include "ical.h"

// --- ÖV (transport.opendata.ch) ---
struct Station {
  char id[12];    // "8509404"
  char name[48];  // "Buchs SG"
};
constexpr int kStationN = 2;
constexpr int kMaxStationHits = 10;

struct Departure {
  char line[12];      // "S4", "IC 3", "B 11"
  char to[40];        // destination
  int64_t dep;        // planned departure, Unix seconds
  int delay;          // minutes, 0 = none / unknown
  char platform[8];   // "4", "4!" when changed (prognosis differs)
  bool bus;
};
constexpr int kMaxDeps = 12;

struct Board {
  Station st;
  int n;
  Departure d[kMaxDeps];
  int64_t fetched;  // Unix seconds (0 = never)
  bool ok;
};

// --- Kalender ---
constexpr int kMaxEvents = 16;  // family events in the next 60 days
constexpr int kMaxWaste = 8;    // waste collections in the next 45 days

struct Calendar {
  int n_events;
  CalEvent events[kMaxEvents];
  int n_waste;
  CalEvent waste[kMaxWaste];
  bool ok_events;   // false: URL missing or fetch failed
  bool ok_waste;
  bool have_events_url;
  char waste_name[40];  // Gemeinde from the Abfallkalender-API ("Buchs")
  int waste_zones;      // zones in the answer (0: whole Gemeinde)
  bool waste_zone_used; // the configured zone was found
  int waste_http;       // last API answer: 200, 400/404/503, 0 = no connection / not tried
  int64_t fetched;
};

// --- MeteoSchweiz (app-prod-ws.meteoswiss-app.ch/v1/plzDetail) ---
struct Warning {
  uint8_t type;   // 0 wind, 1 thunderstorm, 2 rain, 3 snow, 4 slippery roads, 5 frost, 6 mass movements,
                  // 7 heat, 8 avalanches, 9 earthquake, 10 forest fire, 11 flood
  uint8_t level;  // 1..5
  bool outlook;
  int64_t from;   // Unix seconds
  int64_t to;     // 0 = open end
  char text[600]; // warning text, Latin-1 subset (the list scrolls)
};
constexpr int kMaxWarnings = 4;
constexpr int kRainSteps = 12;  // 10-minute steps: the next 2 hours

struct MeteoNow {
  bool ok;
  int n;
  Warning w[kMaxWarnings];  // level >= 2, highest first
  int64_t rain_start;       // Unix seconds of rain[0]
  float rain[kRainSteps];   // mm/h, 10-minute steps from now
  float rain_min[kRainSteps];
  float rain_max[kRainSteps];
  int64_t fetched;
};

const char* warning_name(uint8_t type);  // German

// Network task only.
int transit_search(const char* query, Station* out, int cap);  // -1 on error
bool transit_board(const Station& st, Board* out);
// Waste: Abfallkalender-API <waste_base>/api/abfallkalender?gemeinde=<bfs>, fetched at
// most once per day (and again when base/BFS/zone change; after a failure at most hourly).
bool calendar_fetch(const char* ical_url, const char* waste_base, const char* bfs, const char* zone, Calendar* out);
bool meteo_fetch(const char* plz, MeteoNow* out);
