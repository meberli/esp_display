#include "transit_parse.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
bool is_bus(const char* cat) {
  return !strcmp(cat, "B") || !strcmp(cat, "BUS") || !strcmp(cat, "NFB") || !strcmp(cat, "T") ||
         !strcmp(cat, "KB") || !strcmp(cat, "EXB") || !strcmp(cat, "NFO");
}
}  // namespace

int64_t transit_iso_to_unix(const char* s) {
  if (s == nullptr) return 0;
  int y, mo, d, h, mi, se = 0;
  char sign = 0;
  int oh = 0, om = 0;
  if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) < 5) return 0;
  const char* t = strchr(s, 'T');
  const char* z = t != nullptr ? strpbrk(t + 6, "+-Z") : nullptr;
  if (z != nullptr && *z != 'Z') {
    sign = *z;
    if (sscanf(z + 1, "%2d", &oh) != 1) return 0;
    const char* m = z + 3;
    if (*m == ':') ++m;
    sscanf(m, "%2d", &om);
  }
  int64_t v = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
  const int off = (oh * 60 + om) * 60;
  if (sign == '+') v -= off;
  else if (sign == '-') v += off;
  return v;
}

int transit_parse_board(const char* json, size_t len, Departure* out, int cap, void (*fix)(char*)) {
  StaticJsonDocument<384> filter;
  JsonObject f = filter["stationboard"][0].to<JsonObject>();
  f["category"] = true;
  f["number"] = true;
  f["to"] = true;
  f["stop"]["departureTimestamp"] = true;
  f["stop"]["delay"] = true;
  f["stop"]["platform"] = true;
  f["stop"]["prognosis"]["platform"] = true;
  f["stop"]["prognosis"]["departure"] = true;
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return -1;
  JsonArrayConst arr = doc["stationboard"].as<JsonArrayConst>();
  if (arr.isNull()) return -1;
  int n = 0;
  for (JsonObjectConst j : arr) {
    if (n >= cap) break;
    Departure& d = out[n];
    memset(&d, 0, sizeof(d));
    const char* cat = j["category"] | "";
    const char* num = j["number"] | "";
    d.bus = is_bus(cat);
    // "S" + "4" -> "S4"; buses show just the number.
    if (d.bus) snprintf(d.line, sizeof(d.line), "%s", num[0] != '\0' ? num : cat);
    else if (strlen(cat) <= 2) snprintf(d.line, sizeof(d.line), "%s%s", cat, num);
    else snprintf(d.line, sizeof(d.line), "%s %s", cat, num);
    snprintf(d.to, sizeof(d.to), "%s", j["to"] | "");
    if (fix != nullptr) fix(d.to);
    JsonObjectConst stop = j["stop"];
    d.dep = stop["departureTimestamp"] | 0LL;
    JsonVariantConst dl = stop["delay"];
    if (dl.is<int>()) {
      d.delay = dl.as<int>();
    } else {
      const int64_t prog = transit_iso_to_unix(stop["prognosis"]["departure"] | "");
      if (prog > 0 && d.dep > 0) d.delay = static_cast<int>((prog - d.dep + 30) / 60);
    }
    if (d.delay < 0) d.delay = 0;
    const char* pl = stop["platform"] | "";
    const char* ppl = stop["prognosis"]["platform"] | "";
    if (ppl[0] != '\0' && strcmp(ppl, pl) != 0) snprintf(d.platform, sizeof(d.platform), "%s!", ppl);
    else snprintf(d.platform, sizeof(d.platform), "%s", pl);
    if (d.dep > 0) ++n;
  }
  return transit_merge(out, 0, out, n, cap);  // sort + dedupe in place
}

int transit_merge(Departure* a, int na, const Departure* b, int nb, int cap) {
  // b may alias a (from index 0); copy it first.
  Departure* tmp = static_cast<Departure*>(malloc(sizeof(Departure) * (nb > 0 ? nb : 1)));
  if (tmp == nullptr) return na;
  memcpy(tmp, b, sizeof(Departure) * nb);
  for (int i = 0; i < nb; ++i) {
    const Departure& x = tmp[i];
    bool dup = false;
    for (int k = 0; k < na && !dup; ++k)
      dup = a[k].dep == x.dep && !strcmp(a[k].line, x.line) && !strcmp(a[k].to, x.to);
    if (dup) continue;
    const int64_t rx = x.dep + x.delay * 60;
    int pos = na;
    while (pos > 0 && a[pos - 1].dep + a[pos - 1].delay * 60 > rx) --pos;
    if (pos >= cap) continue;
    const int last = na < cap ? na : cap - 1;
    memmove(&a[pos + 1], &a[pos], sizeof(Departure) * (last - pos));
    a[pos] = x;
    if (na < cap) ++na;
  }
  free(tmp);
  return na;
}

bool transit_bus_stop_name(const char* station, char* out, size_t n) {
  if (station == nullptr || station[0] == '\0' || strchr(station, ',') != nullptr) return false;
  int w = snprintf(out, n, "%s, Bahnhof", station);
  return w > 0 && static_cast<size_t>(w) < n;
}
