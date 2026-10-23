#pragma once
// PV (Growatt 6000UE): read-only Modbus TCP client for the RS485 gateway
// (default 192.168.1.244:502, unit 1). Core 0, priority 2 (below whatwatt/net/
// Loxone). One persistent connection (the gateway allows at most 4), one request
// at a time: FC04 0..44 every 10 s (3.5 s timeout, >= 200 ms between requests),
// once FC03 0..9 for the rated power. Nothing is ever written. At night the
// inverter sleeps and does not answer: the last values (and today's kWh) are
// kept and the view reports "asleep".

#include <stdint.h>

#include "pv.h"

constexpr int32_t kPvPollS = 10;

struct PvView {
  bool enabled;          // pv_on and a gateway IP set
  bool connected;        // TCP connection to the gateway open
  bool have;             // a good reading since boot
  bool stale;            // last good reading > kPvStaleS ago
  bool asleep;           // gateway reachable (or was) but the inverter does not answer (night)
  bool down;             // gateway not reachable
  uint32_t polls, ok, errors;  // since boot
  int64_t fetched;       // panel time of the last good reading
  uint32_t rated_w;      // FC03 reg 8 (0 = unknown)
  char host[16];
  uint16_t port;
  uint8_t unit;
  char error[40];
  PvReading r;
  PvHist hist;           // daily PV production (today = inverter Eac today)
};

void pv_start();
void pv_get(PvView* out);  // any task (copy under a mutex)
void pv_kick();            // settings changed: reconnect
