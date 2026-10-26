#include "waste_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

namespace {

bool parse_day(const char* s, struct tm* t) {
  int y, m, d;
  if (s == nullptr || sscanf(s, "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  memset(t, 0, sizeof(*t));
  t->tm_year = y - 1900;
  t->tm_mon = m - 1;
  t->tm_mday = d;
  t->tm_isdst = -1;
  return true;
}

int add_dates(JsonArrayConst a, const char* title, int64_t from, int64_t to, CalEvent* out, int n, int cap,
              LocalMktime lm) {
  for (JsonVariantConst v : a) {
    struct tm t;
    if (!parse_day(v.as<const char*>(), &t)) continue;
    CalEvent e = {};
    e.start = lm(&t);
    if (e.start + 86399 < from || e.start >= to) continue;
    e.all_day = true;
    e.kind = 1;
    snprintf(e.title, sizeof(e.title), "%s", title);
    e.uid = cal_hash(e.title);
    bool dup = false;
    for (int i = 0; i < n; ++i) {
      if (out[i].start == e.start && strcmp(out[i].title, e.title) == 0) dup = true;
    }
    if (!dup) n = cal_insert(out, n, cap, e);
  }
  return n;
}

}  // namespace

int waste_parse_api(const char* json, size_t len, const char* zone, int64_t from, int64_t to, CalEvent* out, int cap,
                    LocalMktime lm, WasteApiInfo* info) {
  WasteApiInfo dummy;
  if (info == nullptr) info = &dummy;
  memset(info, 0, sizeof(*info));
  StaticJsonDocument<256> filter;
  filter["gemeinde"] = true;
  filter["bfs"] = true;
  filter["termine"] = true;
  filter["zonen"] = true;
  Doc doc(len + len / 2 + 2048);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return -1;
  JsonObjectConst termine = doc["termine"];
  if (termine.isNull()) return -1;
  snprintf(info->gemeinde, sizeof(info->gemeinde), "%s", doc["gemeinde"] | "");
  info->bfs = doc["bfs"] | 0;
  JsonObjectConst zonen = doc["zonen"];
  JsonObjectConst mine;
  if (!zonen.isNull()) {
    info->zones = static_cast<int>(zonen.size());
    if (zone != nullptr && zone[0] != '\0') mine = zonen[zone];
    info->zone_used = !mine.isNull();
  }
  int n = 0;
  for (JsonPairConst kv : termine) {
    JsonArrayConst a = kv.value().as<JsonArrayConst>();
    if (!mine.isNull() && mine.containsKey(kv.key().c_str())) a = mine[kv.key().c_str()].as<JsonArrayConst>();
    n = add_dates(a, kv.key().c_str(), from, to, out, n, cap, lm);
  }
  // Types only the zone lists (not in the union) still count.
  if (!mine.isNull()) {
    for (JsonPairConst kv : mine) {
      if (!termine.containsKey(kv.key().c_str())) n = add_dates(kv.value().as<JsonArrayConst>(), kv.key().c_str(), from, to, out, n, cap, lm);
    }
  }
  return n;
}

bool waste_api_url(char* out, size_t n, const char* base, const char* bfs, int year, int month, int day) {
  if (base == nullptr || bfs == nullptr || bfs[0] == '\0') return false;
  size_t bl = strlen(base);
  while (bl > 0 && base[bl - 1] == '/') --bl;
  int w = snprintf(out, n, "%.*s/api/abfallkalender?gemeinde=%s&ab=%04d-%02d-%02d", static_cast<int>(bl), base, bfs,
                   year, month, day);
  return w > 0 && static_cast<size_t>(w) < n;
}
