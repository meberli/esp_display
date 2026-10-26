#include "energy_mix.h"

namespace {
float pos(float v) { return v > 0 ? v : 0; }
}  // namespace

EnergyMix energy_mix(float import_kwh, float export_kwh, float pv_kwh) {
  EnergyMix m = {};
  m.import_kwh = pos(import_kwh);
  m.export_kwh = pos(export_kwh);
  m.pv_kwh = pv_kwh;
  if (!(pv_kwh >= 0)) return m;  // also rejects NaN
  m.valid = true;
  m.inconsistent = m.export_kwh > pv_kwh + kMixTolKwh;
  m.self_kwh = pos(pv_kwh - m.export_kwh);
  m.use_kwh = m.import_kwh + m.self_kwh;
  m.pv_share = m.use_kwh > 0 ? m.self_kwh / m.use_kwh : 0;
  // Production actually leaving the inverter is at least the export.
  const float prod = m.self_kwh + m.export_kwh;
  m.self_share = prod > 0 ? m.self_kwh / prod : 0;
  return m;
}

float energy_house_w(float net_w, float pv_ac_w) { return pos(net_w + pos(pv_ac_w)); }
