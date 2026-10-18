#pragma once
// PV (Growatt ShineLan): minimal on-panel Grott server. The stick sends its
// records to the panel (server address = panel IP, TCP 5279); this task (core 0,
// priority 2, below whatwatt/net/Loxone) answers pings, acks data records and
// sends one time-sync after the announce record. Read only: no register reads or
// writes, nothing is forwarded. Only the configured stick IP is accepted.

#include <stdint.h>

#include "pv.h"

struct PvView {
  bool enabled;         // pv_on and an IP set
  bool listening;
  bool connected;       // stick connected now
  bool have;            // a live record since boot
  bool stale;           // see pv_stale()
  uint8_t proto;        // last protocol (2/5/6)
  int32_t interval_s;   // observed seconds between live records (0 = unknown)
  uint32_t records, rejected;  // live records / refused connections since boot
  int64_t fetched;      // panel time of the last live record
  char peer[16];        // configured stick IP
  char error[40];
  PvReading r;
};

void pv_start();
void pv_get(PvView* out);  // any task
void pv_kick();            // settings changed: re-open the listener
