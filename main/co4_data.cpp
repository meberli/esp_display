#include "co4_data.h"
#include "co4_oidc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <ArduinoJson.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
namespace {
struct PsramAlloc {
  void* allocate(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void* p) { heap_caps_free(p); }
  void* reallocate(void* p, size_t n) { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
using Doc = BasicJsonDocument<PsramAlloc>;
}  // namespace
#else
using Doc = DynamicJsonDocument;
#endif

bool co4_ratio(const float (*v)[kCo4MaxBars], int from, int to, Co4Ratio* out) {
  memset(out, 0, sizeof(*out));
  if (from < 0) from = 0;
  if (to > kCo4MaxBars) to = kCo4MaxBars;
  bool any = false;
  for (int s = 0; s < kCo4SeriesN; ++s) {
    for (int i = from; i < to; ++i) {
      if (!isnan(v[s][i])) {
        out->sum[s] += v[s][i];
        any = true;
      }
    }
  }
  // Bezug may be negative (corrections in the Co4 data, shown as delivered).
  // The shares stay in [0, 100]: Bezug <= 0 with own use -> Autarkie 100 %.
  const float eig = out->sum[kCo4Eigen], bez = out->sum[kCo4Bezug], pv = out->sum[kCo4Pv];
  const float use = eig + (bez > 0.0f ? bez : 0.0f);
  if (use > 0.0f && eig > 0.0f) out->autarky_pct = fminf(100.0f, 100.0f * eig / use);
  if (pv > 0.0f && eig > 0.0f) out->self_pct = fminf(100.0f, 100.0f * eig / pv);
  return any;
}

void co4_fmt_kwh(char* out, size_t n, float v) {
  if (!(v == v) || isinf(v)) v = 0;
  const float a = fabsf(v);
  // newlib snprintf (not LVGL's, which has no %f); no "-0.0" for tiny negatives.
  if (a >= 1000) snprintf(out, n, "%.2f MWh", static_cast<double>(v / 1000.0f));
  else if (a >= 100) snprintf(out, n, "%.0f kWh", static_cast<double>(v));
  else if (a < 0.05f) snprintf(out, n, "0.0 kWh");
  else snprintf(out, n, "%.1f kWh", static_cast<double>(v));
}

namespace {

// --- Fixture --------------------------------------------------------------------
// BEISPIELDATEN: synthetic, anonymised. A household with ~8 kWp PV, base load
// and morning/evening peaks. No real meter, zone or customer behind it.

float pv_kw(float h, float scale) {
  if (h < 7.0f || h > 19.0f) return 0.0f;
  float x = (h - 13.0f) / 3.4f;
  return scale * 6.2f * expf(-x * x);
}

float load_kw(float h) {
  float base = 0.35f;
  float m = (h - 7.0f) / 0.8f;
  float e = (h - 19.0f) / 1.5f;
  float noon = (h - 12.2f) / 0.6f;
  return base + 1.6f * expf(-m * m) + 2.3f * expf(-e * e) + 1.1f * expf(-noon * noon);
}

// Deterministic 0..1 "weather" per day number.
float day_factor(int yday) {
  uint32_t x = static_cast<uint32_t>(yday) * 2654435761u;
  x ^= x >> 15;
  return 0.35f + 0.65f * static_cast<float>(x % 1000) / 999.0f;
}

float season(int month0) {  // 0 = January
  static const float k[12] = {0.25f, 0.38f, 0.62f, 0.82f, 0.95f, 1.0f, 1.0f, 0.92f, 0.74f, 0.52f, 0.3f, 0.22f};
  return k[month0 % 12];
}

void split(float pv, float load, float* bez, float* ein, float* eig) {
  *eig = pv < load ? pv : load;
  *bez = load - *eig;
  *ein = pv - *eig;
}

int64_t local_midnight_ms(int64_t now_ms, struct tm* lt) {
  time_t t = static_cast<time_t>(now_ms / 1000);
  localtime_r(&t, lt);
  struct tm m = *lt;
  m.tm_hour = 0;
  m.tm_min = 0;
  m.tm_sec = 0;
  m.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&m)) * 1000;
}

void block_init(Co4Res res, int64_t t0, int64_t step, int n, const char* unit, float* v, int cap,
                Co4Res* r_out, int64_t* t0_out, int64_t* step_out, uint16_t* n_out, char* unit_out) {
  *r_out = res;
  *t0_out = t0;
  *step_out = step;
  *n_out = static_cast<uint16_t>(n);
  snprintf(unit_out, 6, "%s", unit);
  for (int i = 0; i < kCo4SeriesN * cap; ++i) v[i] = NAN;
}

// Fixture site block from a zone block: f times the zone, a battery that charges
// bat of the feed-in and gives it back in the evening (synthetic).
template <int N>
void site_scale(const Co4Block<N>& z, Co4Block<N>* s, float f, float bat) {
  *s = z;
  float stored = 0;
  for (int i = 0; i < z.n; ++i) {
    for (int k = 0; k < kCo4BatLaden; ++k)
      if (!isnan(z.v[k][i])) s->v[k][i] = z.v[k][i] * f;
    if (isnan(z.v[kCo4Einspeisung][i])) continue;
    float ch = s->v[kCo4Einspeisung][i] * bat;
    s->v[kCo4BatLaden][i] = ch;
    s->v[kCo4Einspeisung][i] -= ch;
    stored += ch;
    float out = z.res == Co4Res::Min15 ? (s->v[kCo4Pv][i] < 0.05f * f ? stored * 0.25f : 0) : ch * 0.9f;
    if (out > s->v[kCo4Bezug][i]) out = s->v[kCo4Bezug][i];
    s->v[kCo4BatEntladen][i] = out;
    s->v[kCo4Bezug][i] -= out;
    stored -= out;
  }
}

bool fixture_fetch(const char* zone_id, const char* bearer, int64_t now_ms, Co4Data* out, char* err, size_t err_n) {
  (void)zone_id;
  (void)bearer;
  (void)err;
  (void)err_n;
  co4_fixture_fill(now_ms, out);
  return true;
}

bool live_fetch(const char*, const char*, int64_t, Co4Data*, char* err, size_t err_n) {
  // The Co4 series API comes later (API examples from Co4). Nothing guessed here.
  snprintf(err, err_n, "Co4-Messreihen noch nicht angebunden");
  return false;
}

const Co4Source kFixture = {"Beispieldaten", true, fixture_fetch};
const Co4Source kLive = {"Co4", false, live_fetch};

}  // namespace

const Co4Source& co4_fixture_source() { return kFixture; }
const Co4Source& co4_live_source() { return kLive; }

void co4_fixture_fill(int64_t now_ms, Co4Data* out) {
  struct tm lt;
  const int64_t mid = local_midnight_ms(now_ms, &lt);
  out->example = true;
  out->fetched_ms = now_ms;

  // Today: 15 min power, values up to now.
  Co4Curve& d = out->day;
  block_init(Co4Res::Min15, mid, 15 * 60 * 1000, kCo4MaxPoints, "kW", &d.v[0][0], kCo4MaxPoints, &d.res, &d.t0_ms,
             &d.step_ms, &d.n, d.unit);
  const float today = day_factor(lt.tm_yday) * season(lt.tm_mon);
  for (int i = 0; i < kCo4MaxPoints; ++i) {
    int64_t t = mid + static_cast<int64_t>(i) * d.step_ms;
    if (t > now_ms) break;
    float h = static_cast<float>(i) / 4.0f + 0.125f;
    float pv = pv_kw(h, today);
    float wob = 0.15f * sinf(static_cast<float>(i) * 1.7f);  // a little texture
    float load = load_kw(h) + (wob > 0 ? wob : 0);
    split(pv, load, &d.v[kCo4Bezug][i], &d.v[kCo4Einspeisung][i], &d.v[kCo4Eigen][i]);
    d.v[kCo4Pv][i] = pv;
  }

  // This month: energy per day, up to today.
  Co4Totals& m = out->days;
  struct tm first = lt;
  first.tm_mday = 1;
  first.tm_hour = first.tm_min = first.tm_sec = 0;
  first.tm_isdst = -1;
  const int64_t t_first = static_cast<int64_t>(mktime(&first)) * 1000;
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int ndays = kDays[lt.tm_mon];
  int y = lt.tm_year + 1900;
  if (lt.tm_mon == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ndays = 29;
  block_init(Co4Res::Day, t_first, 24LL * 3600 * 1000, ndays, "kWh", &m.v[0][0], kCo4MaxBars, &m.res, &m.t0_ms,
             &m.step_ms, &m.n, m.unit);
  for (int i = 0; i < lt.tm_mday && i < kCo4MaxBars; ++i) {
    int yd = lt.tm_yday - (lt.tm_mday - 1 - i);
    float f = day_factor(yd) * season(lt.tm_mon);
    float pv = 38.0f * f;
    float load = 13.5f + 3.0f * day_factor(yd + 101);
    float eig = pv * 0.34f < load * 0.8f ? pv * 0.34f : load * 0.8f;
    m.v[kCo4Pv][i] = pv;
    m.v[kCo4Eigen][i] = eig;
    m.v[kCo4Bezug][i] = load - eig;
    m.v[kCo4Einspeisung][i] = pv - eig;
  }

  // Whole site (Beispiel: 6 zones, battery present).
  Co4SiteData& s = out->site;
  s.state = Co4SiteState::Ok;
  s.partial = false;
  s.err[0] = '\0';
  site_scale(d, &s.day, 6.2f, 0.25f);
  site_scale(m, &s.days, 6.2f, 0.25f);

  // This year: energy per month, up to this month.
  Co4Totals& yr = out->months;
  struct tm jan = first;
  jan.tm_mon = 0;
  jan.tm_isdst = -1;
  block_init(Co4Res::Month, static_cast<int64_t>(mktime(&jan)) * 1000, 0, 12, "kWh", &yr.v[0][0], kCo4MaxBars,
             &yr.res, &yr.t0_ms, &yr.step_ms, &yr.n, yr.unit);
  for (int i = 0; i <= lt.tm_mon; ++i) {
    float pv = 1150.0f * season(i);
    float load = 470.0f - 120.0f * season(i);
    float eig = pv * 0.3f < load * 0.75f ? pv * 0.3f : load * 0.75f;
    float share = i == lt.tm_mon ? static_cast<float>(lt.tm_mday) / static_cast<float>(ndays) : 1.0f;
    yr.v[kCo4Pv][i] = pv * share;
    yr.v[kCo4Eigen][i] = eig * share;
    yr.v[kCo4Bezug][i] = (load - eig) * share;
    yr.v[kCo4Einspeisung][i] = (pv - eig) * share;
  }
  site_scale(yr, &s.months, 6.2f, 0.25f);
}

