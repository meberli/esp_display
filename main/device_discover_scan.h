#pragma once
// ESP-IDF scan of the local IPv4 subnet with unauthenticated HTTP GET only.
// Runs on the network task (UI stays responsive). Never sends credentials.

#include "device_discover.h"

#include <stdint.h>

// Probe the local subnet for `kind` for up to budget_ms (default 5 s).
// Writes up to cap hits (no auto-select; the caller just presents them).
// Returns the number found (>= 0). -1 if there is no station address.
// `gen` is reserved so the UI can ignore a stale result; the scan itself
// always runs to the budget or to cap.
int device_discover_scan(DiscoverKind kind, DiscoverHit* out, int cap, uint32_t budget_ms, uint32_t gen);
