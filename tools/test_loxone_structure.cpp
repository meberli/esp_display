// g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src -DLOX_STRUCTURE_HOST
//   tools/test_loxone_structure.cpp main/loxone_structure.cpp -o /tmp/tlxs && /tmp/tlxs
#include <stdio.h>
#include <string.h>

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

  if (fails == 0) printf("loxone_structure ok\n");
  return fails == 0 ? 0 : 1;
}
