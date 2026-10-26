#pragma once
// Abfallkalender-API (abfall-sg, Kanton St. Gallen):
//   GET <base>/api/abfallkalender?gemeinde=<BFS>&ab=YYYY-MM-DD
//   {"gemeinde":"Buchs","bfs":3271,"termine":{"Kehricht":["2026-10-16",...],"Grüngut":[...]},
//    "zonen":{"<Tour>":{"Kehricht":[...]}}, ...}
// Pure C++ (ArduinoJson only), so tools/test_waste_api.cpp runs it on a PC.

#include <stddef.h>
#include <stdint.h>

#include "ical.h"

struct WasteApiInfo {
  char gemeinde[40];  // "Buchs"
  int bfs;
  int zones;          // number of zones in the answer (0: whole Gemeinde)
  bool zone_used;     // the configured zone was found and used
};

// Parses the answer and stores collections with from <= day < to (all-day,
// local midnight, kind 1, title = waste type), sorted. zone (may be empty):
// a key of "zonen"; for each type the zone's dates replace the union in
// "termine" when the zone lists that type. Returns the number stored, or -1
// when the JSON is not an Abfallkalender answer.
int waste_parse_api(const char* json, size_t len, const char* zone, int64_t from, int64_t to, CalEvent* out, int cap,
                    LocalMktime local_mktime, WasteApiInfo* info);

// "<base>/api/abfallkalender?gemeinde=<bfs>&ab=<YYYY-MM-DD>" (base without trailing '/').
bool waste_api_url(char* out, size_t n, const char* base, const char* bfs, int year, int month, int day);
