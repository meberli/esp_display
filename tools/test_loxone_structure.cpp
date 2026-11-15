// g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src -DLOX_STRUCTURE_HOST
//   tools/test_loxone_structure.cpp main/loxone_structure.cpp -o /tmp/tlxs && /tmp/tlxs
#include <stdio.h>
#include <string.h>
#include <string>

#include "loxone_structure.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static const char* kJson = R"JSON({
  "msInfo": {"msName": "Testserver"},
  "rooms": {"r1": {"name": "Cellar"}},
  "cats": {
    "c-air": {"name": "AirChange"},
    "c-light": {"name": "Lights"}
  },
  "controls": {
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01": {
      "name": "Schedule",
      "type": "Daytimer",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01",
      "room": "r1",
      "cat": "c-air",
      "isFavorite": true,
      "details": {"analog": true, "format": "%.0f"},
      "states": {
        "value": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee02",
        "override": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee03"
      }
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee10": {
      "name": "Boost",
      "type": "Pushbutton",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee10",
      "cat": "c-air",
      "isFavorite": true,
      "states": {"active": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee11"}
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee20": {
      "name": "DeskLamp",
      "type": "Switch",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee20",
      "cat": "c-light",
      "states": {"active": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee21"}
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee30": {
      "name": "DigitalClock",
      "type": "Daytimer",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee30",
      "details": {"analog": false, "text": {"on": "Aktiv", "off": "Inaktiv"}},
      "states": {"value": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee31"}
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee40": {
      "name": "AirChange",
      "type": "Daytimer",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee40",
      "details": {"analog": true},
      "states": {"value": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee41"}
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee50": {
      "name": "Puff",
      "type": "Pushbutton",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee50",
      "cat": "c-air",
      "isFavorite": false,
      "states": {"active": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee51"}
    }
  }
})JSON";

// Uncategorized analog timer whose name matches a category name, plus a
// pushbutton in that category.
static const char* kJsonFallback = R"JSON({
  "msInfo": {"msName": "Other"},
  "cats": {"c-air": {"name": "AirChange"}},
  "controls": {
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee60": {
      "name": "AirChange",
      "type": "Daytimer",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee60",
      "details": {"analog": true},
      "states": {"value": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee61"}
    },
    "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee70": {
      "name": "Boost",
      "type": "Pushbutton",
      "uuidAction": "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee70",
      "cat": "c-air"
    }
  }
})JSON";


// Runtime-built LoxAPP3 that fills the cap and allocates the category tables
// on the heap (PSRAM on device). Synthetic ids only.
static std::string large_structure_json() {
  std::string j =
      "{\"msInfo\":{\"msName\":\"BigHall\"},"
      "\"rooms\":{\"r1\":{\"name\":\"Hall\"}},"
      "\"cats\":{\"c-air\":{\"name\":\"AirChange\"},\"c-light\":{\"name\":\"Lights\"}},"
      "\"controls\":{";
  auto add = [&](const char* rec, bool comma) {
    if (comma) j += ',';
    j += rec;
  };
  add("\"aaaaaaaa-bbbb-cccc-ddddeeeeee000001\":{"
      "\"name\":\"Schedule\",\"type\":\"Daytimer\","
      "\"uuidAction\":\"aaaaaaaa-bbbb-cccc-ddddeeeeee000001\","
      "\"room\":\"r1\",\"cat\":\"c-air\",\"isFavorite\":true,"
      "\"details\":{\"analog\":true},"
      "\"states\":{\"value\":\"aaaaaaaa-bbbb-cccc-ddddeeeeee000002\","
      "\"override\":\"aaaaaaaa-bbbb-cccc-ddddeeeeee000003\"}}",
      false);
  add("\"aaaaaaaa-bbbb-cccc-ddddeeeeee000010\":{"
      "\"name\":\"Boost\",\"type\":\"Pushbutton\","
      "\"uuidAction\":\"aaaaaaaa-bbbb-cccc-ddddeeeeee000010\","
      "\"cat\":\"c-air\",\"isFavorite\":true}",
      true);
  // 62 switches stored with the timer and button = 64. Ten more exist but
  // do not fit; two digital daytimers are skipped entirely.
  char rec[512], id[40], st[40];
  for (int i = 0; i < 72; ++i) {
    snprintf(id, sizeof(id), "aaaaaaaa-bbbb-cccc-ddddeeeeee%06d", 100000 + i);
    snprintf(st, sizeof(st), "aaaaaaaa-bbbb-cccc-ddddeeeeee%06d", 200000 + i);
    snprintf(rec, sizeof(rec),
             "\"%s\":{\"name\":\"Lamp%02d\",\"type\":\"Switch\","
             "\"uuidAction\":\"%s\",\"cat\":\"c-light\","
             "\"states\":{\"active\":\"%s\"}}",
             id, i, id, st);
    add(rec, true);
  }
  for (int i = 0; i < 2; ++i) {
    snprintf(id, sizeof(id), "aaaaaaaa-bbbb-cccc-ddddeeeeee%06d", 300000 + i);
    snprintf(st, sizeof(st), "aaaaaaaa-bbbb-cccc-ddddeeeeee%06d", 400000 + i);
    snprintf(rec, sizeof(rec),
             "\"%s\":{\"name\":\"Clock%02d\",\"type\":\"Daytimer\","
             "\"uuidAction\":\"%s\",\"details\":{\"analog\":false},"
             "\"states\":{\"value\":\"%s\"}}",
             id, i, id, st);
    add(rec, true);
  }
  j += "}}";
  return j;
}

static const LoxoneControl* find_action(const LoxoneControl* c, size_t n, const char* action) {
  for (size_t i = 0; i < n; ++i) {
    if (strcmp(c[i].action, action) == 0) return &c[i];
  }
  return nullptr;
}

int main() {
  LoxoneControl out[kLoxoneControlCap];
  memset(out, 0, sizeof(out));
  size_t shown = 0, supported = 0;
  char server[48];
  char err[80];
  CHECK(loxone_parse_structure(reinterpret_cast<const uint8_t*>(kJson), strlen(kJson), out, kLoxoneControlCap,
                               &shown, &supported, server, sizeof(server), err, sizeof(err)));
  CHECK(strcmp(server, "Testserver") == 0);
  // Analog timers (2) + pushbuttons (2) + switch (1); digital daytimer skipped.
  CHECK(supported == 5);
  CHECK(shown == 5);

  const LoxoneControl* sched = find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee01");
  CHECK(sched != nullptr);
  CHECK(sched->kind == kLoxoneTimer);
  CHECK(sched->state_kind == kLoxoneStateValue);
  CHECK(sched->favorite);
  CHECK(strcmp(sched->name, "Schedule") == 0);
  CHECK(strcmp(sched->room, "Cellar") == 0);
  CHECK(strcmp(sched->state, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee02") == 0);
  CHECK(strcmp(sched->override_state, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee03") == 0);
  // Same-category favorite Boost is preferred over non-favorite Puff.
  CHECK(strcmp(sched->pulse_action, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee10") == 0);
  CHECK(strcmp(sched->pulse_name, "Boost") == 0);

  const LoxoneControl* uncat = find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee40");
  CHECK(uncat != nullptr && uncat->kind == kLoxoneTimer);
  // In this fixture the timer name equals the category of Boost.
  CHECK(strcmp(uncat->pulse_action, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee10") == 0);

  CHECK(find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee20") != nullptr);
  CHECK(find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee20")->kind == kLoxoneSwitch);
  CHECK(find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee30") == nullptr);

  memset(out, 0, sizeof(out));
  shown = supported = 0;
  CHECK(loxone_parse_structure(reinterpret_cast<const uint8_t*>(kJsonFallback), strlen(kJsonFallback), out,
                               kLoxoneControlCap, &shown, &supported, server, sizeof(server), err, sizeof(err)));
  CHECK(shown == 2 && supported == 2);
  const LoxoneControl* fb = find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee60");
  CHECK(fb != nullptr);
  CHECK(fb->kind == kLoxoneTimer);
  CHECK(strcmp(fb->pulse_action, "aaaaaaaa-bbbb-cccc-ddddeeeeeeeeee70") == 0);
  CHECK(strcmp(fb->pulse_name, "Boost") == 0);

  // Cap: still counts supported, only stores `cap` items.
  memset(out, 0, sizeof(out));
  CHECK(loxone_parse_structure(reinterpret_cast<const uint8_t*>(kJson), strlen(kJson), out, 1, &shown, &supported,
                               server, sizeof(server), err, sizeof(err)));
  CHECK(supported == 5);
  CHECK(shown == 1);

  CHECK(!loxone_parse_structure(nullptr, 0, out, 8, &shown, &supported, server, sizeof(server), err, sizeof(err)));

  // Large synthetic structure: cap is full, extras are counted, pulse is linked.
  {
    std::string big = large_structure_json();
    memset(out, 0, sizeof(out));
    shown = supported = 0;
    CHECK(loxone_parse_structure(reinterpret_cast<const uint8_t*>(big.data()), big.size(), out, kLoxoneControlCap,
                                 &shown, &supported, server, sizeof(server), err, sizeof(err)));
    CHECK(strcmp(server, "BigHall") == 0);
    // 1 analog timer + 1 pushbutton + 72 switches; 2 digital daytimers skipped.
    CHECK(supported == 74);
    CHECK(shown == kLoxoneControlCap);
    const LoxoneControl* sched = find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeee000001");
    CHECK(sched != nullptr && sched->kind == kLoxoneTimer);
    CHECK(strcmp(sched->pulse_action, "aaaaaaaa-bbbb-cccc-ddddeeeeee000010") == 0);
    CHECK(strcmp(sched->pulse_name, "Boost") == 0);
    CHECK(find_action(out, shown, "aaaaaaaa-bbbb-cccc-ddddeeeeee100000") != nullptr);
  }


  if (fails == 0) printf("loxone_structure ok\n");
  return fails == 0 ? 0 : 1;
}
