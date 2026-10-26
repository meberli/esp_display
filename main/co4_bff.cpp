#include "co4_bff.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <ArduinoJson.h>

#include "co4_oidc.h"

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

// Unit: the ZEV schema does not name the unit of energyStats; the related ZEV
// balance types document kWh. Treated as kWh (offene Frage an Co4).

namespace {
constexpr size_t kDocCap = 24 * 1024;  // PSRAM; a day @15 min with 3 sources needs ~10 KB

int64_t local_ms(struct tm m) {
  m.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&m)) * 1000;
}

bool error_answer(JsonDocument& doc, char* err, size_t err_n) {
  if (!doc["errors"].isNull() || !doc["error"].isNull()) {
    snprintf(err, err_n, "Co4 meldet einen Fehler");
    return true;
  }
  return false;
}
}  // namespace

bool co4_bff_niid_ok(const char* s) {
  if (s == nullptr) return false;
  size_t i = 0;
  for (; s[i] != '\0'; ++i) {
    if (s[i] < '0' || s[i] > '9' || i >= 19) return false;
  }
  return i > 0;
}

bool co4_bff_stats_path(char* out, size_t n, const char* zone_niid, int64_t start_ms, int64_t end_ms,
                        const char* period) {
  if (!co4_bff_niid_ok(zone_niid) || period == nullptr || end_ms <= start_ms) return false;
  int w = snprintf(out, n, "zevGetZoneEnergyStats?zoneNiid=%s&start=%" PRId64 "&end=%" PRId64 "&period=%s", zone_niid,
                   start_ms, end_ms, period);
  return w > 0 && static_cast<size_t>(w) < n;
}

bool co4_bff_zones_path(char* out, size_t n, const char* site_niid) {
  if (!co4_bff_niid_ok(site_niid)) return false;
  int w = snprintf(out, n, "zevGetConsumptionZones?niid=%s", site_niid);
  return w > 0 && static_cast<size_t>(w) < n;
}

void co4_bff_windows(int64_t now_ms, Co4BffWindow out[3]) {
  time_t t = static_cast<time_t>(now_ms / 1000);
  struct tm lt;
  localtime_r(&t, &lt);
  struct tm m = lt;
  m.tm_hour = m.tm_min = m.tm_sec = 0;
  const int64_t mid = local_ms(m);
  struct tm tom = m;
  tom.tm_mday += 1;
  const int64_t tomorrow = local_ms(tom);

  const int64_t q = 15 * 60 * 1000;
  int64_t end = mid + ((now_ms - mid + q - 1) / q) * q;
  if (end <= mid) end = mid + q;
  if (end > tomorrow) end = tomorrow;
  out[0] = {Co4Res::Min15, mid, end, q, "900S"};

  struct tm first = m;
  first.tm_mday = 1;
  out[1] = {Co4Res::Day, local_ms(first), tomorrow, 24LL * 3600 * 1000, "1D"};

  struct tm jan = first;
  jan.tm_mon = 0;
  struct tm next = first;
  next.tm_mon += 1;
  out[2] = {Co4Res::Month, local_ms(jan), local_ms(next), 0, "1M"};
}

Co4BffRc co4_bff_parse_stats(const char* json, size_t len, float* v, int cap, uint16_t* n, bool* partial,
                             char* err, size_t err_n) {
  if (err_n > 0) err[0] = '\0';
  *n = 0;
  *partial = false;
  for (int i = 0; i < kCo4SeriesN * cap; ++i) v[i] = NAN;
  StaticJsonDocument<512> filter;
  JsonObject es = filter["data"]["energyStats"].to<JsonObject>();
  es["end"] = true;
  es["partialSources"] = true;
  es["sources"][0]["type"] = true;
  es["sources"][0]["production"] = true;
  filter["errors"][0]["message"] = true;
  filter["error"] = true;
  Doc doc(kDocCap);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) {
    snprintf(err, err_n, "Antwort unlesbar");
    return Co4BffRc::Error;
  }
  if (error_answer(doc, err, err_n)) return Co4BffRc::Error;
  JsonObjectConst st = doc["data"]["energyStats"].as<JsonObjectConst>();
  if (st.isNull()) {
    snprintf(err, err_n, "Antwort ohne Messreihen");
    return Co4BffRc::Error;
  }
  *partial = st["partialSources"] | false;
  JsonArrayConst ends = st["end"].as<JsonArrayConst>();
  int cnt = ends.isNull() ? 0 : static_cast<int>(ends.size());
  if (cnt > cap) cnt = cap;
  *n = static_cast<uint16_t>(cnt);
  JsonVariantConst srcs = st["sources"];
  if (srcs.isNull()) {
    snprintf(err, err_n, "Co4 rechnet noch");
    return Co4BffRc::NotReady;
  }
  for (JsonObjectConst s : srcs.as<JsonArrayConst>()) {
    const char* type = s["type"] | "";
    int series = -1;
    if (strcmp(type, "GRID") == 0) series = kCo4Bezug;
    else if (strcmp(type, "PV") == 0) series = kCo4Eigen;
    if (series < 0) continue;
    float* dst = v + series * cap;
    int i = 0;
    for (JsonVariantConst x : s["production"].as<JsonArrayConst>()) {
      if (i >= cnt) break;
      if (x.is<float>()) dst[i] = isnan(dst[i]) ? x.as<float>() : dst[i] + x.as<float>();
      ++i;
    }
  }
  return Co4BffRc::Ok;
}

bool co4_bff_parse_zones(const char* json, size_t len, Co4ZoneList* out, char* err, size_t err_n) {
  memset(out, 0, sizeof(*out));
  if (err_n > 0) err[0] = '\0';
  StaticJsonDocument<512> filter;
  JsonObject z = filter["data"]["zones"][0].to<JsonObject>();
  z["niid"] = true;
  z["zoneType"] = true;
  z["details"]["name"][0]["value"] = true;
  filter["errors"][0]["message"] = true;
  filter["error"] = true;
  Doc doc(8192);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) {
    snprintf(err, err_n, "Antwort unlesbar");
    return false;
  }
  if (error_answer(doc, err, err_n)) return false;
  JsonArrayConst arr = doc["data"]["zones"].as<JsonArrayConst>();
  if (arr.isNull()) {
    snprintf(err, err_n, "Antwort ohne Zonen");
    return false;
  }
  for (JsonObjectConst zo : arr) {
    if (out->n >= kCo4MaxZones) break;
    char id[24];
    JsonVariantConst ni = zo["niid"];
    if (ni.is<int64_t>()) {
      snprintf(id, sizeof(id), "%" PRId64, ni.as<int64_t>());
    } else {
      snprintf(id, sizeof(id), "%s", ni | "");
    }
    if (!co4_bff_niid_ok(id)) continue;
    const char* name = zo["details"]["name"][0]["value"] | "";
    Co4Zone& dst = out->z[out->n++];
    snprintf(dst.id, sizeof(dst.id), "%s", id);
    co4_copy_utf8(dst.name, sizeof(dst.name), name[0] != '\0' ? name : id);
  }
  return true;
}

void co4_bff_energy_to_power(float* v, int n, int64_t step_ms) {
  if (step_ms <= 0) return;
  const float f = 3600000.0f / static_cast<float>(step_ms);
  for (int i = 0; i < n; ++i) {
    if (!isnan(v[i])) v[i] *= f;
  }
}
