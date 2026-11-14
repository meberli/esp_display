#pragma once
// Small pure helpers (host-tested in tools/test_retry_logic.cpp).
#include <stdint.h>

// Network / unreachable structure retry: 30 s, 2 min, 5 min, then every 15 min.
inline uint32_t lox_backoff_ms(int fails) {
  static const uint32_t kSteps[] = {30000, 120000, 300000, 900000};
  if (fails < 1) return 0;
  int i = fails - 1;
  return kSteps[i < 3 ? i : 3];
}

// HTTP 401/403: wrong user/password, or the Miniserver is already locking the
// IP out. Further automatic attempts extend that lockout.
inline bool lox_is_auth_status(int status) { return status == 401 || status == 403; }

// After this many rejected logins, stop automatic retries. A new attempt only
// after a reboot or when the user saves new credentials.
constexpr int kLoxAuthFailCap = 3;

inline bool lox_auth_give_up(int auth_fails) { return auth_fails >= kLoxAuthFailCap; }

// Delay before the next attempt after 1 or 2 auth failures: 30 s, then 2 min.
// 0 once we have given up (or before the first failure).
inline uint32_t lox_auth_backoff_ms(int auth_fails) {
  if (auth_fails < 1 || lox_auth_give_up(auth_fails)) return 0;
  return auth_fails == 1 ? 30000u : 120000u;
}

// WLAN scan generation: every start or cancel gets a new number; a scan result
// is only shown if it belongs to the scan that is still running (avoids the
// race between "Abbrechen" and a late scan-done).
struct ScanGen {
  uint32_t cur = 0;
  bool running = false;
  uint32_t start() {
    running = true;
    return ++cur;
  }
  void cancel() {
    running = false;
    ++cur;
  }
  bool accept(uint32_t gen) {
    if (!running || gen != cur) return false;
    running = false;
    return true;
  }
};
