// Host test: g++ -std=c++17 -Wall -I main tools/test_energy_mix.cpp main/energy_mix.cpp
#include <cmath>
#include <cstdio>
#include "energy_mix.h"

static int fails = 0;
static void near(const char* what, float got, float want) {
  if (std::fabs(got - want) > 1e-3f) {
    printf("FAIL %s: got %.4f want %.4f\n", what, got, want);
    ++fails;
  }
}
static void is(const char* what, bool got, bool want) {
  if (got != want) { printf("FAIL %s: got %d want %d\n", what, got, want); ++fails; }
}

int main() {
  // Sunny day: import 4, PV 20, export 12 -> self 8, use 12, PV share 2/3, self share 0.4.
  EnergyMix m = energy_mix(4, 12, 20);
  is("sunny valid", m.valid, true); is("sunny incons", m.inconsistent, false);
  near("sunny self", m.self_kwh, 8); near("sunny use", m.use_kwh, 12);
  near("sunny pv_share", m.pv_share, 8.0f / 12); near("sunny self_share", m.self_share, 0.4f);
  // Night / no PV yet today: all grid.
  m = energy_mix(3.2f, 0, 0);
  is("night valid", m.valid, true); near("night use", m.use_kwh, 3.2f); near("night pv_share", m.pv_share, 0);
  near("night self_share", m.self_share, 0);
  // PV unknown (disabled, no data that day).
  m = energy_mix(5, 1, -1);
  is("unknown valid", m.valid, false);
  m = energy_mix(5, 1, NAN);
  is("nan valid", m.valid, false);
  // Rounding: export 0.1 above PV (Eac 0.1 kWh steps) -> clamp, still consistent.
  m = energy_mix(1, 6.1f, 6.0f);
  is("round incons", m.inconsistent, false); near("round self", m.self_kwh, 0); near("round use", m.use_kwh, 1);
  near("round self_share", m.self_share, 0);
  // PV offline half the day: export far above PV -> flagged, never negative.
  m = energy_mix(2, 9, 4);
  is("offline incons", m.inconsistent, true); near("offline self", m.self_kwh, 0); near("offline use", m.use_kwh, 2);
  // All self-consumed.
  m = energy_mix(0, 0, 5);
  near("selfonly use", m.use_kwh, 5); near("selfonly pv_share", m.pv_share, 1); near("selfonly self_share", m.self_share, 1);
  // Nothing at all (start of day).
  m = energy_mix(0, 0, 0);
  is("zero valid", m.valid, true); near("zero shares", m.pv_share + m.self_share, 0);
  // Negative inputs (corrupt) are clamped.
  m = energy_mix(-1, -2, 3);
  near("neg use", m.use_kwh, 3); near("neg self", m.self_kwh, 3);
  // Live house load.
  near("house import", energy_house_w(500, 1200), 1700);
  near("house export", energy_house_w(-800, 1200), 400);
  near("house night", energy_house_w(300, 0), 300);
  near("house clamp", energy_house_w(-900, 800), 0);   // PV AC sampled before the meter: never negative
  near("house neg pv", energy_house_w(200, -5), 200);
  printf("%d failures\n", fails);
  return fails ? 1 : 0;
}
