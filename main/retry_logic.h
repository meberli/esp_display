#pragma once
// Small pure helpers (host-tested in tools/test_retry_logic.cpp).
#include <stdint.h>

// Loxone structure retry: 10 s, 30 s, 2 min, then every 5 min.
inline uint32_t lox_backoff_ms(int fails) {
  static const uint32_t kSteps[] = {10000, 30000, 120000, 300000};
  if (fails < 1) return 0;
  int i = fails - 1;
  return kSteps[i < 3 ? i : 3];
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
