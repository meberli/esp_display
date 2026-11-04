#pragma once
// transport.opendata.ch stationboard parsing (host-testable, no ESP deps).
#include <stddef.h>
#include <stdint.h>

#include "info.h"

// ISO 8601 with offset ("2026-10-28T21:48:00+0100") -> Unix seconds, 0 if unreadable.
int64_t transit_iso_to_unix(const char* s);
// Parses a stationboard answer into out (cap). Delay: stop.delay (minutes), else
// derived from stop.prognosis.departure. Returns the count, -1 if unreadable.
// fix(s) may clean up text (UTF-8 -> font range); nullptr = none.
int transit_parse_board(const char* json, size_t len, Departure* out, int cap, void (*fix)(char*));
// Merges b into a (na, cap): sorted by real departure, duplicates (same line,
// planned time and destination, e.g. coupled trains) dropped. Returns the new count.
int transit_merge(Departure* a, int na, const Departure* b, int nb, int cap);
// Companion bus stop name for a rail station: "Buchs SG" -> "Buchs SG, Bahnhof";
// false if the name already names a stop (contains ',').
bool transit_bus_stop_name(const char* station, char* out, size_t n);
