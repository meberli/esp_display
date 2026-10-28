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

// Unit: energyStats values are kWh (confirmed).

namespace {
constexpr size_t kDocCap = 24 * 1024;  // PSRAM; a day @15 min with 3 sources needs ~10 KB

int64_t local_ms(struct tm m) {
  m.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&m)) * 1000;
}

bool error_answer(JsonDocument& doc, char* err, size_t err_n) {
  if (!doc["errors"].isNull() || !doc["error"].isNull()) {
    // A stale zone choice (e.g. picked from removed example data) is answered
    // with "... not exist" / "not found": tell the user to pick the zone again.
    const char* m = doc["errors"][0]["message"] | "";
    if (strstr(m, "not exist") != nullptr || strstr(m, "not found") != nullptr)
      snprintf(err, err_n, "Zone bei Co4 nicht gefunden \xE2\x80\x93 bitte Zone neu w\xC3\xA4hlen");
    else
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

bool co4_bff_site_stats_path(char* out, size_t n, const char* site_niid, int64_t start_ms, int64_t end_ms,
                             const char* period) {
  if (!co4_bff_niid_ok(site_niid) || period == nullptr || end_ms <= start_ms) return false;
  int w = snprintf(out, n, "zevGetSiteEnergyStats?siteNiid=%s&start=%" PRId64 "&end=%" PRId64 "&period=%s",
                   site_niid, start_ms, end_ms, period);
  return w > 0 && static_cast<size_t>(w) < n;
}

bool co4_bff_zone_path(char* out, size_t n, const char* zone_niid) {
  if (!co4_bff_niid_ok(zone_niid)) return false;
  int w = snprintf(out, n, "zevGetZone?niid=%s", zone_niid);
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
    else if (strcmp(type, "BATTERY") == 0) series = kCo4BatEntladen;
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

bool co4_bff_parse_zones(const char* json, size_t len, const char* site_niid, Co4ZoneList* out, char* err,
                         size_t err_n) {
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
    snprintf(dst.site, sizeof(dst.site), "%s", co4_bff_niid_ok(site_niid) ? site_niid : "");
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

namespace {
bool niid_text(JsonVariantConst ni, char* out, size_t n) {
  if (ni.is<int64_t>()) snprintf(out, n, "%" PRId64, ni.as<int64_t>());
  else snprintf(out, n, "%s", ni | "");
  return co4_bff_niid_ok(out);
}
}  // namespace

Co4BffRc co4_bff_parse_site_stats(const char* json, size_t len, float* v, int cap, uint16_t* n, bool* partial,
                                  char* err, size_t err_n) {
  if (err_n > 0) err[0] = '\0';
  *n = 0;
  *partial = false;
  for (int i = 0; i < kCo4SeriesN * cap; ++i) v[i] = NAN;
  static const struct {
    const char* key;
    Co4Series s;
  } kMap[] = {{"gridProduction", kCo4Bezug},          {"feedIn", kCo4Einspeisung},
              {"pvProduction", kCo4Pv},               {"selfConsumption", kCo4Eigen},
              {"batteryConsumption", kCo4BatLaden},   {"batteryProduction", kCo4BatEntladen}};
  StaticJsonDocument<512> filter;
  JsonObject es = filter["data"]["energyStats"].to<JsonObject>();
  es["end"] = true;
  es["partialSources"] = true;
  for (const auto& m : kMap) es[m.key] = true;  // sources, consumption, pv/batteryFromLocalPv: not read
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
    snprintf(err, err_n, "Site nicht lesbar");
    return Co4BffRc::Error;
  }
  *partial = st["partialSources"] | false;
  JsonArrayConst ends = st["end"].as<JsonArrayConst>();
  int cnt = ends.isNull() ? 0 : static_cast<int>(ends.size());
  if (cnt > cap) cnt = cap;
  *n = static_cast<uint16_t>(cnt);
  for (const auto& m : kMap) {
    JsonArrayConst a = st[m.key].as<JsonArrayConst>();
    if (a.isNull()) continue;  // undefined: site has no such metering points -> hidden
    float* dst = v + m.s * cap;
    int i = 0;
    for (JsonVariantConst x : a) {
      if (i >= cnt) break;
      if (x.is<float>()) dst[i] = x.as<float>();
      ++i;
    }
  }
  return Co4BffRc::Ok;
}

bool co4_bff_parse_zone_site(const char* json, size_t len, char* site, size_t site_n, char* err, size_t err_n) {
  if (site_n > 0) site[0] = '\0';
  if (err_n > 0) err[0] = '\0';
  StaticJsonDocument<192> filter;
  filter["data"]["zone"]["site"]["niid"] = true;  // nothing else of the zone (contract, accounts) is read
  filter["errors"][0]["message"] = true;
  filter["error"] = true;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) {
    snprintf(err, err_n, "Antwort unlesbar");
    return false;
  }
  if (error_answer(doc, err, err_n)) return false;
  char tmp[24];
  if (!niid_text(doc["data"]["zone"]["site"]["niid"], tmp, sizeof(tmp)) || strlen(tmp) >= site_n) {
    snprintf(err, err_n, "Zone ohne Site");
    return false;
  }
  snprintf(site, site_n, "%s", tmp);
  return true;
}

// --- Object tree -------------------------------------------------------------------

bool co4_bff_nav_path(char* out, size_t n, const char* soid) {
  int w;
  if (soid == nullptr || soid[0] == '\0') {
    w = snprintf(out, n, "co4CoreNavRoots");
  } else {
    if (!co4_bff_niid_ok(soid)) return false;
    w = snprintf(out, n, "co4CoreNavGet?id=%s&hidden=false", soid);
  }
  return w > 0 && static_cast<size_t>(w) < n;
}

namespace {
char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool name_before(const Co4NavItem& a, const Co4NavItem& b) {
  for (size_t i = 0;; ++i) {
    char x = lower_ascii(a.name[i]), y = lower_ascii(b.name[i]);
    if (x != y || x == '\0') return x < y;
  }
}
}  // namespace

bool co4_bff_parse_nav(const char* json, size_t len, bool roots, Co4NavLevel* out, char* err, size_t err_n) {
  out->n = 0;
  out->total = 0;
  if (err_n > 0) err[0] = '\0';
  StaticJsonDocument<512> filter;
  JsonObject it = roots ? filter["data"]["roots"][0].to<JsonObject>()
                        : filter["data"]["object"]["children"][0].to<JsonObject>();
  it["id"] = true;
  it["hasChildren"] = true;
  it["properties"][0]["label"] = true;
  it["properties"][0]["value"] = true;
  it["natures"][0]["id"] = true;
  it["natures"][0]["nature"]["code"] = true;
  if (!roots) filter["data"]["object"]["id"] = true;
  filter["errors"][0]["message"] = true;
  filter["error"] = true;
  Doc doc(kDocCap * 2);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) {
    snprintf(err, err_n, "Antwort unlesbar");
    return false;
  }
  if (error_answer(doc, err, err_n)) return false;
  JsonArrayConst arr;
  if (roots) {
    arr = doc["data"]["roots"].as<JsonArrayConst>();
  } else {
    if (doc["data"]["object"].isNull()) {
      snprintf(err, err_n, "Objekt nicht gefunden");
      return false;
    }
    arr = doc["data"]["object"]["children"].as<JsonArrayConst>();  // null = no children
  }
  if (roots && arr.isNull()) {
    snprintf(err, err_n, "Antwort ohne Objekte");
    return false;
  }
  for (JsonObjectConst o : arr) {
    char soid[20];
    if (!niid_text(o["id"], soid, sizeof(soid))) continue;
    if (out->total < 0xFFFF) ++out->total;
    if (out->n >= kCo4NavMax) continue;
    Co4NavItem& d = out->it[out->n++];
    memset(&d, 0, sizeof(d));
    snprintf(d.soid, sizeof(d.soid), "%s", soid);
    const char* name = "";
    for (JsonObjectConst pr : o["properties"].as<JsonArrayConst>()) {
      const char* l = pr["label"] | "";
      if (strcmp(l, "name") == 0) {
        name = pr["value"] | "";
        break;
      }
    }
    co4_copy_utf8(d.name, sizeof(d.name), name[0] != '\0' ? name : "(ohne Namen)");
    for (JsonObjectConst na : o["natures"].as<JsonArrayConst>()) {
      const char* code = na["nature"]["code"] | "";
      if (strcmp(code, "zev_Zone") == 0 && niid_text(na["id"], d.zone, sizeof(d.zone))) break;
      d.zone[0] = '\0';
    }
    d.kids = o["hasChildren"] | false;
  }
  // insertion sort by name (frontend shows children sorted by name)
  for (int i = 1; i < out->n; ++i) {
    Co4NavItem t = out->it[i];
    int j = i - 1;
    for (; j >= 0 && name_before(t, out->it[j]); --j) out->it[j + 1] = out->it[j];
    out->it[j + 1] = t;
  }
  return true;
}
