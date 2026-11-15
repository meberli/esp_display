#pragma once

#include <stddef.h>
#include <stdint.h>

// Controls this panel can show. Other LoxAPP3.json types are skipped.
enum LoxoneKind : uint8_t {
  kLoxoneSwitch = 0,   // On / Off
  kLoxonePulse = 1,    // Pulse
  kLoxoneShutter = 2,  // Jalousie: FullUp / Stop / FullDown
  kLoxoneTimer = 3,    // Analog Daytimer: level 0..10 and startOverride / stopOverride
};

// Which states.* entry to read, and how to turn the text into on/off or a position.
enum LoxoneStateKind : uint8_t {
  kLoxoneStateNone = 0,
  kLoxoneStateActive,    // Switch, TimedSwitch: "0" off, "1" on
  kLoxoneStatePosition,  // Dimmer 0..100 (0 off). Jalousie 0..1 (0 up, 1 down)
  kLoxoneStateScene,     // LightController: scene 0 is all off
  kLoxoneStateValue,     // Analog Daytimer: current numeric output
};

struct LoxoneControl {
  char name[64];
  char room[40];
  char action[40];
  char state[40];
  char override_state[40];  // analog Daytimer states.override, else empty
  char pulse_action[40];    // linked Pushbutton in the same category, else empty
  char pulse_name[64];
  LoxoneKind kind;
  LoxoneStateKind state_kind;
  bool favorite;
};

// 64 leaves headroom past a house that already fills the old 48 slots with
// switches and shutters, while the 4 KiB NVS structure cache still packs a
// typical set (extra Daytimer fields are stored only for kLoxoneTimer).
constexpr size_t kLoxoneControlCap = 64;

// Ids that are safe to place in a Miniserver URL path.
inline bool loxone_id_ok(const char* s, size_t max_len) {
  if (s == nullptr || s[0] == '\0') {
    return false;
  }
  size_t n = 0;
  for (const char* p = s; *p != '\0'; ++p, ++n) {
    if (n >= max_len) {
      return false;
    }
    char c = *p;
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum && c != '-' && c != '_' && c != '.' && c != ':') {
      return false;
    }
  }
  return true;
}
