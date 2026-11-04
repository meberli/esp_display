// Host test: stationboard parsing, delay from prognosis, bus merge, dedupe.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "transit_parse.h"

static const char* kRail = R"({"stationboard":[
 {"category":"S","number":"9","to":"Musterstadt","stop":{"departureTimestamp":1000200,"delay":0,"platform":"2","prognosis":{"platform":null,"departure":"1970-01-12T14:50:00+0100"}}},
 {"category":"IR","number":"77","to":"Beispielhausen","stop":{"departureTimestamp":1000500,"delay":null,"platform":"1","prognosis":{"platform":"3","departure":"1970-01-12T14:58:00+0100"}}},
 {"category":"IR","number":"77","to":"Beispielhausen","stop":{"departureTimestamp":1000500,"delay":null,"platform":"1","prognosis":{"platform":"3","departure":"1970-01-12T14:58:00+0100"}}},
 {"category":"RE","number":"5","to":"Testdorf","stop":{"departureTimestamp":1001400,"delay":4,"platform":"1","prognosis":{"platform":null,"departure":null}}},
 {"category":"S","number":"9","to":"Nirgendwo","stop":{"departureTimestamp":0,"delay":null,"platform":"","prognosis":{}}}
]})";
static const char* kBus = R"({"stationboard":[
 {"category":"B","number":"42","to":"Am Hang","stop":{"departureTimestamp":1000300,"delay":0,"platform":"A","prognosis":{}}},
 {"category":"B","number":"7","to":"Talstation","stop":{"departureTimestamp":1000560,"delay":null,"platform":"C","prognosis":{"departure":null}}}
]})";

int main() {
  // 1000200 = 1970-01-12T14:50:00+0100
  assert(transit_iso_to_unix("1970-01-12T14:50:00+0100") == 1000200);
  assert(transit_iso_to_unix("1970-01-12T13:50:00Z") == 1000200);
  assert(transit_iso_to_unix("2026-10-28T21:48:00+0100") == 1793220480);
  assert(transit_iso_to_unix("x") == 0);

  Departure d[12];
  int n = transit_parse_board(kRail, strlen(kRail), d, 12, nullptr);
  assert(n == 3);  // duplicate (coupled train) and dep==0 dropped
  assert(!strcmp(d[0].line, "S9") && d[0].delay == 0 && !d[0].bus);
  assert(!strcmp(d[1].line, "IR77") && d[1].delay == 3);   // from prognosis: 14:58 vs 14:55
  assert(!strcmp(d[1].platform, "3!"));
  assert(!strcmp(d[2].line, "RE5") && d[2].delay == 4);

  Departure b[12];
  int nb = transit_parse_board(kBus, strlen(kBus), b, 12, nullptr);
  assert(nb == 2 && b[0].bus && !strcmp(b[0].line, "42") && b[1].delay == 0);

  n = transit_merge(d, n, b, nb, 12);
  assert(n == 5);
  const char* order[] = {"S9", "42", "7", "IR77", "RE5"};  // by real departure (IR +3 -> 1000680)
  for (int i = 0; i < 5; ++i) {
    if (strcmp(d[i].line, order[i])) { printf("pos %d = %s\n", i, d[i].line); return 1; }
  }
  // cap: soonest real departures win, train or bus
  Departure c[3];
  int nc = transit_parse_board(kRail, strlen(kRail), c, 3, nullptr);
  nc = transit_merge(c, nc, b, nb, 3);
  assert(nc == 3 && !strcmp(c[0].line, "S9") && !strcmp(c[1].line, "42") && !strcmp(c[2].line, "7"));
  assert(transit_merge(c, nc, c, nc, 3) == 3);  // self-merge = dedupe only

  char nm[40];
  assert(transit_bus_stop_name("Musterstadt", nm, sizeof nm) && !strcmp(nm, "Musterstadt, Bahnhof"));
  assert(!transit_bus_stop_name("Musterstadt, Post", nm, sizeof nm));
  puts("transit OK");
  return 0;
}
