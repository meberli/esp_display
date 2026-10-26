#pragma once
// Energie: whatwatt Go polling task (core 0, priority below the net and Loxone
// tasks: a slow or missing meter reader never delays a light switch). The UI
// task reads a copy with energy_get() (no LVGL here).

#include <stdint.h>

#include "energy.h"

constexpr int64_t kEnergyStaleS = 60;

struct EnergyView {
  bool enabled;        // on and a host set
  bool have;           // at least one good reading since boot
  bool stale;          // last good reading (or its meter timestamp) older than 60 s
  int http_status;     // last answer, 0 = no connection
  char error[40];      // last error ("" when ok)
  int64_t fetched;     // panel time of the last good reading
  EnergyReading r;     // last good reading
  EnergyHist h;        // today + last days
  Tariff tariff;
  bool show_exp, water_on;
};

void energy_start();
void energy_get(EnergyView* out);  // any task
void energy_kick();                 // settings changed: poll now
