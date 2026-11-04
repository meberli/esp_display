#pragma once

// Home "Schalter" button: picks the configured target and the command for one
// tap. Pure, so the host test can check it without touching a device.
#include <string.h>

#include "loxone_types.h"

namespace swt {

// ids[i]: the target's id ("relay" for the local relay, else the Loxone action).
// Returns the index of `saved`, or 0 (the first target) when it is empty or
// gone, or -1 when there is no target at all.
inline int pick(const char* const* ids, int n, const char* saved) {
  if (n <= 0) return -1;
  if (saved != nullptr && saved[0] != '\0') {
    for (int i = 0; i < n; ++i) {
      if (ids[i] != nullptr && strcmp(ids[i], saved) == 0) return i;
    }
  }
  return 0;
}

// Command for one tap: switches flip from their known state (unknown -> On),
// everything else pulses.
inline const char* command(LoxoneKind kind, bool known, bool on) {
  if (kind == kLoxoneSwitch) return (known && on) ? "Off" : "On";
  return "Pulse";
}

// A tap fires only if it was a real tap (no swipe/scroll) and nothing is in flight.
inline bool may_fire(bool gesture_consumed, bool command_pending) {
  return !gesture_consumed && !command_pending;
}

}  // namespace swt
