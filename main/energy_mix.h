#pragma once
// Energy balance (pure logic, host tested in tools/test_energy_mix.cpp).
//   Verbrauch      = Netzbezug + PV-Erzeugung - Einspeisung
//   Eigenverbrauch = PV-Erzeugung - Einspeisung
// Netzbezug/Einspeisung come from the smart meter (whatwatt), PV from the
// inverter's own day counter (Growatt "Eac today", AC side, 0.1 kWh steps).
// Both sources are counted independently, so export can slightly exceed PV
// (rounding, a late inverter reading, PV offline for part of the day): the
// self-consumption is then clamped to 0 and the result flagged inconsistent.

struct EnergyMix {
  bool valid;         // import/export known and PV known (pv >= 0)
  bool inconsistent;  // export > PV + kMixTolKwh: PV under-reported
  float import_kwh, export_kwh, pv_kwh;
  float self_kwh;     // Eigenverbrauch, 0..pv
  float use_kwh;      // Verbrauch = import + self
  float pv_share;     // Herkunft: share of the consumption from PV (0..1), Netz = 1 - pv_share
  float self_share;   // Verwendung: share of the production used in the house (0..1), Einspeisung = 1 - self_share
};
constexpr float kMixTolKwh = 0.15f;

// pv_kwh < 0: unknown (PV disabled or no data for that day) -> valid = false.
EnergyMix energy_mix(float import_kwh, float export_kwh, float pv_kwh);

// Live house load in W from grid net (import - export) and PV AC power; clamped at 0.
float energy_house_w(float net_w, float pv_ac_w);
