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
