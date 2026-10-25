#include "energy.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

uint32_t fnv(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  return h;
}

int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// "2026-10-17T17:30:10Z" / "...+02:00" / "...-05:30" -> Unix seconds (UTC),
// 0 if malformed. No suffix = UTC.
int64_t parse_utc(const char* s) {
  int y, mo, d, h, mi, se, n = 0;
  if (s == nullptr || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d%n", &y, &mo, &d, &h, &mi, &se, &n) != 6) return 0;
  int64_t t = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
  const char* z = s + n;
  if (*z == '.') while (*++z >= '0' && *z <= '9') {}
  if (*z == '+' || *z == '-') {
    int oh = 0, om = 0;
    if (sscanf(z + 1, "%2d:%2d", &oh, &om) >= 1) t -= (*z == '+' ? 1 : -1) * (oh * 3600 + om * 60);
  }
  return t;
}

void local(int64_t t, struct tm* lt) {
  time_t tt = static_cast<time_t>(t);
  localtime_r(&tt, lt);
}

int32_t prev_ymd(int32_t ymd) {
  int64_t n = days_from_civil(ymd / 10000, (ymd / 100) % 100, ymd % 100) - 1;
  // civil_from_days (H. Hinnant)
  n += 719468;
  const int64_t era = (n >= 0 ? n : n - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(n - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned dd = doy - (153 * mp + 2) / 5 + 1;
  const unsigned mm = mp < 10 ? mp + 3 : mp - 9;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400 + (mm <= 2);
  return static_cast<int32_t>(yy * 10000 + mm * 100 + dd);
}

int32_t ymd_of(const struct tm& lt) { return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday; }

double num(JsonVariantConst v, double dflt) { return v.is<double>() || v.is<long>() ? v.as<double>() : dflt; }

}  // namespace

bool energy_parse_report(const char* json, size_t len, int water_bus, EnergyReading* out) {
  memset(out, 0, sizeof(*out));
  for (float& i : out->i_a) i = -1;
  out->e_in_t1 = out->e_in_t2 = out->e_out_t1 = out->e_out_t2 = -1;
  out->er_in = out->er_out = -1;
  for (double& q : out->er_q) q = -1;
  StaticJsonDocument<768> filter;
  filter["report"]["date_time_utc"] = true;
  filter["report"]["tariff"] = true;
  filter["report"]["conv_factor"] = true;
  filter["report"]["instantaneous_power"]["active"] = true;
  filter["report"]["current"] = true;
  filter["report"]["energy"]["active"] = true;
  filter["report"]["energy"]["reactive"] = true;
  filter["report"]["volume"] = true;
  filter["meter"]["status"] = true;
  filter["meter"]["id"] = true;
  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  JsonObjectConst rep = doc["report"];
  if (rep.isNull()) return false;
  double k = num(rep["conv_factor"], 1);
  if (k <= 0) k = 1;
  JsonObjectConst pa = rep["instantaneous_power"]["active"];
  out->p_in_kw = static_cast<float>(num(pa["positive"]["total"], 0) * k);
  out->p_out_kw = static_cast<float>(num(pa["negative"]["total"], 0) * k);
  const char* ph[3] = {"l1", "l2", "l3"};
  for (int i = 0; i < 3; ++i) {
    double v = num(rep["current"][ph[i]], -1);
    out->i_a[i] = v < 0 ? -1.0f : static_cast<float>(v * k);
  }
  JsonObjectConst ea = rep["energy"]["active"];
  out->e_in = num(ea["positive"]["total"], -1);
  out->e_out = num(ea["negative"]["total"], 0);
  out->e_in_t1 = num(ea["positive"]["t1"], -1);
  out->e_in_t2 = num(ea["positive"]["t2"], -1);
  out->e_out_t1 = num(ea["negative"]["t1"], -1);
  out->e_out_t2 = num(ea["negative"]["t2"], -1);
  if (out->e_in >= 0) {
    out->e_in *= k;
    out->e_out *= k;
  }
  // Reactive totals er.imp / er.exp = OBIS 3.8.0 (Q1+Q2) / 4.8.0 (Q3+Q4), only as
  // the meter delivers them: {"imported":{"total":x}} / {"imported":x} or
  // {"positive":{"total":x}}. Inductive and capacitive registers (per quadrant)
  // are never summed into a total.
  JsonObjectConst er = rep["energy"]["reactive"];
  const char* dir[2][2] = {{"imported", "positive"}, {"exported", "negative"}};
  for (int d = 0; d < 2; ++d) {
    double v = -1;
    for (const char* name : dir[d]) {
      JsonVariantConst o = er[name];
      if (v < 0) v = o.is<JsonObjectConst>() ? num(o["total"], -1) : num(o, -1);
    }
    (d == 0 ? out->er_in : out->er_out) = v < 0 ? -1 : v * k;
  }
  // Quadrant registers (whatwatt docs): import inductive = Q1, export capacitive = Q2,
  // export inductive = Q3, import capacitive = Q4.
  const char* qsrc[4][2] = {{"imported", "inductive"}, {"exported", "capacitive"}, {"exported", "inductive"},
                            {"imported", "capacitive"}};
  for (int q = 0; q < 4; ++q) {
    JsonVariantConst o = er[qsrc[q][0]][qsrc[q][1]];
    double v = o.is<JsonObjectConst>() ? num(o["total"], -1) : num(o, -1);
    out->er_q[q] = v < 0 ? -1 : v * k;
  }
  out->tariff = static_cast<int>(num(rep["tariff"], 0));
  out->ts = parse_utc(rep["date_time_utc"] | "");
  if (water_bus > 0) {
    for (JsonObjectConst v : rep["volume"].as<JsonArrayConst>()) {
      if (static_cast<int>(num(v["bus"], -1)) != water_bus) continue;
      double c = num(v["cumulative"], -1);
      if (c >= 0) {
        out->has_water = true;
        out->water_m3 = c;
        out->water_ts = parse_utc(v["date_time"] | "");
      }
    }
  }
  const char* st = doc["meter"]["status"] | "";
  out->meter = fnv(doc["meter"]["id"] | "");
  snprintf(out->meter_id, sizeof(out->meter_id), "%s", doc["meter"]["id"] | "");
  out->ok = strcmp(st, "OK") == 0 && out->e_in >= 0;
  return true;
}

// --- Tariff --------------------------------------------------------------------

void tariff_defaults(Tariff* t) {
  memset(t, 0, sizeof(*t));
  t->model = kTariffAuto;
  t->flat = 2727;
  t->ht = 3000;
  t->nt = 2567;
  t->feed = 1004;
  t->water = 244;
  t->win[0][0] = 6 * 60;
  t->win[0][1] = 10 * 60;
  t->win[1][0] = 17 * 60;
  t->win[1][1] = 21 * 60;
  t->switch_ymd = 20270101;
}

TariffSlot tariff_slot(const Tariff& t, int64_t when) {
  struct tm lt;
  local(when, &lt);
  if (t.model == kTariffFlat) return kSlotFlat;
  if (t.model == kTariffAuto && ymd_of(lt) < t.switch_ymd) return kSlotFlat;
  if (lt.tm_wday == 0 || lt.tm_wday == 6) return kSlotNt;
  int m = lt.tm_hour * 60 + lt.tm_min;
  for (const auto& w : t.win) {
    if (w[0] < w[1] && m >= w[0] && m < w[1]) return kSlotHt;
  }
  return kSlotNt;
}

uint16_t tariff_price(const Tariff& t, int64_t when) {
  switch (tariff_slot(t, when)) {
    case kSlotHt: return t.ht;
    case kSlotNt: return t.nt;
    default: return t.flat;
  }
}

bool energy_parse_price(const char* s, uint16_t* x100) {
  if (s == nullptr) return false;
  while (*s == ' ') ++s;
  long whole = 0, frac = 0;
  int fd = 0, wd = 0;
  for (; *s >= '0' && *s <= '9'; ++s, ++wd) whole = whole * 10 + (*s - '0');
  if (*s == '.' || *s == ',') {
    ++s;
    for (; *s >= '0' && *s <= '9'; ++s) {
      if (fd < 2) {
        frac = frac * 10 + (*s - '0');
        ++fd;
      }
    }
  }
  while (*s == ' ') ++s;
  if (*s != '\0' || (wd == 0 && fd == 0) || wd > 3) return false;
  if (fd == 1) frac *= 10;
  long v = whole * 100 + frac;
  if (v > 65000) return false;
  *x100 = static_cast<uint16_t>(v);
  return true;
}

namespace {
bool parse_hm(const char*& s, uint16_t* m) {
  int h = 0, mi = 0, n = 0;
  if (sscanf(s, "%2d:%2d%n", &h, &mi, &n) != 2 || h < 0 || h > 24 || mi < 0 || mi > 59 || h * 60 + mi > 1440) return false;
  s += n;
  *m = static_cast<uint16_t>(h * 60 + mi);
  return true;
}
}  // namespace

bool energy_parse_windows(const char* s, uint16_t win[2][2]) {
  uint16_t w[2][2] = {};
  int n = 0;
  while (*s == ' ') ++s;
  while (*s != '\0') {
    if (n == 2) return false;
    if (!parse_hm(s, &w[n][0])) return false;
    while (*s == ' ') ++s;
    if (*s != '-') return false;
    ++s;
    while (*s == ' ') ++s;
    if (!parse_hm(s, &w[n][1]) || w[n][1] <= w[n][0]) return false;
    ++n;
    while (*s == ' ') ++s;
    if (*s == ',' || *s == ';') ++s;
    while (*s == ' ') ++s;
  }
  if (n == 0) return false;
  memcpy(win, w, sizeof(w));
  return true;
}

bool energy_parse_date(const char* s, int32_t* ymd) {
  int y, m, d, n = 0;
  if (sscanf(s, "%4d-%2d-%2d%n", &y, &m, &d, &n) != 3 || s[n] != '\0' || y < 2000 || y > 2099 || m < 1 || m > 12 ||
      d < 1 || d > 31)
    return false;
  *ymd = y * 10000 + m * 100 + d;
  return true;
}

void energy_format_price(char* out, size_t n, uint16_t x100) { snprintf(out, n, "%u.%02u", x100 / 100, x100 % 100); }

void energy_format_windows(char* out, size_t n, const uint16_t win[2][2]) {
  out[0] = '\0';
  for (int i = 0; i < 2; ++i) {
    if (win[i][0] >= win[i][1]) continue;
    size_t l = strlen(out);
    snprintf(out + l, n - l, "%s%02u:%02u-%02u:%02u", l ? "," : "", win[i][0] / 60, win[i][0] % 60, win[i][1] / 60,
             win[i][1] % 60);
  }
}

// --- History -------------------------------------------------------------------

void energy_hist_clear(EnergyHist* h) {
  memset(h, 0, sizeof(*h));
  h->magic = kEnergyHistMagic;
  h->hour = -1;
}

int32_t energy_prev_ymd(int32_t ymd) { return prev_ymd(ymd); }
float energy_yesterday_water_l(const EnergyHist& h, uint8_t* flags) {
  if (!h.have_water || h.days[0].ymd != prev_ymd(h.ymd)) return -1;
  if (flags != nullptr) *flags = h.days[0].flags;
  return h.days[0].water_l;
}

float energy_today_in(const EnergyHist& h) { return h.have ? static_cast<float>(h.last_in - h.day_in) : 0; }
float energy_today_out(const EnergyHist& h) { return h.have ? static_cast<float>(h.last_out - h.day_out) : 0; }
float energy_today_water_l(const EnergyHist& h) {
  return h.have_water ? static_cast<float>((h.last_water - h.day_water) * 1000.0) : 0;
}

int64_t energy_book_time(int64_t ts, int64_t now, int64_t* last) {
  constexpr int64_t k2024 = 1704067200;  // 2024-01-01T00:00:00Z
  if (ts <= 0 || ts < k2024 || ts > now + kTsAheadS || (last != nullptr && ts < *last)) return now;
  if (last != nullptr) *last = ts;
  return ts;
}

namespace {
enum Kind { kIn, kOut, kWater };

// Book one delta (kWh or litres) at meter time `at` (effective, see header).
// Returns kHistLate if a finished day was corrected.

// Water (bus 2) reports its total once a day around midnight, and reads can be
// missed: every increase is credited to the local day before the day it is
// detected on (today's water stays 0). A missing yesterday entry is created.
int book_water(EnergyHist* h, double litres) {
  if (litres == 0) return 0;
  h->day_water += litres / 1000.0;
  const int32_t y = prev_ymd(h->ymd);
  for (int i = 0; i < kEnergyDays; ++i)
    if (h->days[i].ymd == y) {
      h->days[i].water_l += static_cast<float>(litres);
      return kHistLate;
    }
  if (h->days[0].ymd > y) return 0;  // cannot happen (ring is newest first)
  memmove(&h->days[1], &h->days[0], sizeof(EnergyDay) * (kEnergyDays - 1));
  EnergyDay e = {};
  e.ymd = y;
  e.water_l = static_cast<float>(litres);
  e.flags = kDayApprox;  // the panel did not record electricity for that day
  h->days[0] = e;
  return kHistLate;
}

int book(EnergyHist* h, Kind k, double d, int64_t at, int64_t now, const Tariff& t) {
  if (d == 0) return 0;
  struct tm lt;
  const int64_t slot = at == now ? now : at - 1;  // a meter reading at hh:00:00 closes the previous hour
  local(slot, &lt);
  const int32_t ymd = ymd_of(lt);
  struct tm ln;
  local(now, &ln);
  int hour = lt.tm_hour;
  if (ymd >= h->ymd) {  // today (or a little ahead): never later than the current hour
    int64_t price_at = slot;
    if (ymd > h->ymd || hour > ln.tm_hour) {
      hour = ln.tm_hour;
      price_at = now;
    }
    float* bars = k == kIn ? h->h_in : k == kOut ? h->h_out : h->h_water;
    bars[hour] += static_cast<float>(d);
    if (k == kIn) h->cost += static_cast<float>(d * tariff_price(t, price_at) / 10000.0);
    if (k == kOut) h->credit += static_cast<float>(d * t.feed / 10000.0);
    return 0;
  }
  // An earlier day: it is no longer part of today's counter span.
  double& base = k == kIn ? h->day_in : k == kOut ? h->day_out : h->day_water;
  base += k == kWater ? d / 1000.0 : d;
  for (int i = 0; i < kEnergyDays; ++i) {
    EnergyDay& day = h->days[i];
    if (day.ymd != ymd) continue;
    if (k == kIn) {
      day.in_kwh += static_cast<float>(d);
      day.cost_chf += static_cast<float>(d * tariff_price(t, slot) / 10000.0);
    } else if (k == kOut) {
      day.out_kwh += static_cast<float>(d);
      day.credit_chf += static_cast<float>(d * t.feed / 10000.0);
    } else {
      day.water_l += static_cast<float>(d);
    }
    return kHistLate;
  }
  return 0;  // older than the ring: dropped
}
}  // namespace

int energy_hist_update(EnergyHist* h, const EnergyReading& r, int64_t now, const Tariff& t, EnergyTsState* tss) {
  if (!r.ok) return 0;
  struct tm lt;
  local(now, &lt);
  const int32_t ymd = ymd_of(lt);
  const int hour = lt.tm_hour;
  if (!h->have || h->meter != r.meter) {
    EnergyDay keep[kEnergyDays];
    memcpy(keep, h->days, sizeof(keep));
    const bool keep_days = h->have != 0;  // meter changed: keep the finished days
    energy_hist_clear(h);
    if (keep_days) memcpy(h->days, keep, sizeof(keep));
    h->meter = r.meter;
    h->have = 1;
    h->ymd = ymd;
    h->hour = static_cast<int8_t>(hour);
    h->day_in = h->last_in = r.e_in;
    h->day_out = h->last_out = r.e_out;
    if (r.has_water) {
      h->have_water = 1;
      h->day_water = h->last_water = r.water_m3;
    }
    h->last_ts = now;
    // Started mid-day: today's totals miss the time before.
    if (hour * 60 + lt.tm_min > 15) h->flags = kDayApprox | kDayWaterApprox;
    return kHistChanged | kHistReset;
  }
  if (r.e_in < h->last_in - 0.0005 || r.e_out < h->last_out - 0.0005 ||
      (h->have_water && r.has_water && r.water_m3 < h->last_water - 0.0005))
    return kHistRejected;
  int res = kHistChanged;
  if (ymd != h->ymd) {
    const bool gap = now - h->last_ts > kHistGapS;
    EnergyDay d = {};
    d.ymd = h->ymd;
    d.in_kwh = energy_today_in(*h);
    d.out_kwh = energy_today_out(*h);
    d.water_l = energy_today_water_l(*h);
    d.cost_chf = h->cost;
    d.credit_chf = h->credit;
    d.flags = static_cast<uint8_t>(h->flags | (gap ? kDayApprox | kDayWaterApprox : 0));
    memmove(&h->days[1], &h->days[0], sizeof(EnergyDay) * (kEnergyDays - 1));
    h->days[0] = d;
    // The new day starts at the last reading before midnight.
    h->day_in = h->last_in;
    h->day_out = h->last_out;
    h->day_water = h->last_water;
    h->flags = gap ? kDayApprox | kDayWaterApprox : 0;
    memset(h->h_in, 0, sizeof(h->h_in));
    memset(h->h_out, 0, sizeof(h->h_out));
    memset(h->h_water, 0, sizeof(h->h_water));
    h->cost = h->credit = 0;
    h->ymd = ymd;
    res |= kHistDay;
  }
  if (hour != h->hour) {
    res |= kHistHour;
    h->hour = static_cast<int8_t>(hour);
  }
  const double din = r.e_in - h->last_in;
  const double dout = r.e_out - h->last_out;
  const int64_t at_e = energy_book_time(r.ts, now, tss != nullptr ? &tss->last_e : nullptr);
  res |= book(h, kIn, din, at_e, now, t);
  res |= book(h, kOut, dout, at_e, now, t);
  h->last_in = r.e_in;
  h->last_out = r.e_out;
  if (r.has_water) {
    if (!h->have_water) {
      h->have_water = 1;
      h->day_water = r.water_m3;
      h->flags |= kDayWaterApprox;
    } else {
      res |= book_water(h, (r.water_m3 - h->last_water) * 1000.0);
    }
    h->last_water = r.water_m3;
  }
  h->last_ts = now;
  return res;
}
