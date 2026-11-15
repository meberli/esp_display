#pragma once

// Structured Miniserver I/O paths for analog Daytimer overrides.
// Paths are built from a validated action uuid and integers. The command
// segment is never taken from free-form UI or network text (so '/' and ';'
// cannot sneak into loxone_id_ok).

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "loxone_types.h"

constexpr int kLoxTimerLevelMin = 0;
constexpr int kLoxTimerLevelMax = 10;

// startOverride always needs a duration. "Until I cancel" is a long override
// ended with stopOverride. There is no documented "until the next program
// entry" command (that needs the weekly schedule, later).
constexpr uint32_t kLoxOverride30m = 30 * 60;
constexpr uint32_t kLoxOverride1h = 60 * 60;
constexpr uint32_t kLoxOverride2h = 2 * 60 * 60;
constexpr uint32_t kLoxOverride4h = 4 * 60 * 60;
constexpr uint32_t kLoxOverrideUntilCancel = 7 * 24 * 60 * 60;  // 7 days
constexpr uint32_t kLoxOverrideMaxSecs = 30 * 24 * 60 * 60;     // accepted upper bound

enum LoxDurationId : uint8_t {
  kLoxDur30m = 0,
  kLoxDur1h = 1,
  kLoxDur2h = 2,
  kLoxDur4h = 3,
  kLoxDurUntilCancel = 4,
  kLoxDurCount = 5,
};

inline uint32_t lox_duration_seconds(LoxDurationId id) {
  switch (id) {
    case kLoxDur30m:
      return kLoxOverride30m;
    case kLoxDur1h:
      return kLoxOverride1h;
    case kLoxDur2h:
      return kLoxOverride2h;
    case kLoxDur4h:
      return kLoxOverride4h;
    case kLoxDurUntilCancel:
      return kLoxOverrideUntilCancel;
    default:
      return 0;
  }
}

// de-CH labels for the duration chips (en dash is not needed here).
inline const char* lox_duration_label(LoxDurationId id) {
  switch (id) {
    case kLoxDur30m:
      return "30 min";
    case kLoxDur1h:
      return "1 h";
    case kLoxDur2h:
      return "2 h";
    case kLoxDur4h:
      return "4 h";
    case kLoxDurUntilCancel:
      return "Bis ich beende";
    default:
      return "";
  }
}

inline bool lox_timer_level_ok(int level) {
  return level >= kLoxTimerLevelMin && level <= kLoxTimerLevelMax;
}

inline bool lox_override_secs_ok(uint32_t seconds) {
  return seconds >= 1 && seconds <= kLoxOverrideMaxSecs;
}

inline bool lox_path_start_override(const char* action, int level, uint32_t seconds, char* out, size_t n) {
  if (out == nullptr || n == 0) {
    return false;
  }
  out[0] = '\0';
  if (!loxone_id_ok(action, 40) || !lox_timer_level_ok(level) || !lox_override_secs_ok(seconds)) {
    return false;
  }
  int wrote = snprintf(out, n, "/jdev/sps/io/%s/startOverride/%d/%u", action, level, seconds);
  return wrote > 0 && static_cast<size_t>(wrote) < n;
}

inline bool lox_path_stop_override(const char* action, char* out, size_t n) {
  if (out == nullptr || n == 0) {
    return false;
  }
  out[0] = '\0';
  if (!loxone_id_ok(action, 40)) {
    return false;
  }
  int wrote = snprintf(out, n, "/jdev/sps/io/%s/stopOverride", action);
  return wrote > 0 && static_cast<size_t>(wrote) < n;
}

inline bool lox_path_named(const char* action, const char* command, char* out, size_t n) {
  if (out == nullptr || n == 0) {
    return false;
  }
  out[0] = '\0';
  if (!loxone_id_ok(action, 40) || !loxone_id_ok(command, 16)) {
    return false;
  }
  int wrote = snprintf(out, n, "/jdev/sps/io/%s/%s", action, command);
  return wrote > 0 && static_cast<size_t>(wrote) < n;
}

// "Aus" for level 0, otherwise the integer.
inline void lox_format_level(int level, char* out, size_t n) {
  if (out == nullptr || n == 0) {
    return;
  }
  if (level == 0) {
    snprintf(out, n, "Aus");
  } else {
    snprintf(out, n, "%d", level);
  }
}

// "Noch 45 s" / "Noch 28 min" / "Noch 1 h" / "Noch 1 h 5 min"
inline void lox_format_remaining(int32_t seconds, char* out, size_t n) {
  if (out == nullptr || n == 0) {
    return;
  }
  out[0] = '\0';
  if (seconds <= 0) {
    return;
  }
  if (seconds < 60) {
    snprintf(out, n, "Noch %d s", static_cast<int>(seconds));
    return;
  }
  int mins = static_cast<int>((seconds + 30) / 60);
  if (mins < 60) {
    snprintf(out, n, "Noch %d min", mins);
    return;
  }
  int hours = mins / 60;
  int rem = mins % 60;
  if (rem == 0) {
    snprintf(out, n, "Noch %d h", hours);
  } else {
    snprintf(out, n, "Noch %d h %d min", hours, rem);
  }
}
