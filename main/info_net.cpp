#include "diag.h"
#include "info.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "waste_api.h"
#include "transit_parse.h"
#include "net.h"
#include "sys.h"
#include <esp_log.h>

static const char* const TAG = "info_net";

namespace {

uint8_t* psram(size_t n) { return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)); }

int64_t local_mktime(struct tm* t) { return static_cast<int64_t>(mktime(t)); }

// UTF-8 -> what the Inter fonts can draw (ASCII + Latin-1); a few typographic
// signs mapped, everything else (emoji, ...) dropped.
void sanitize(char* s) {
  unsigned char* r = reinterpret_cast<unsigned char*>(s);
  unsigned char* w = r;
  while (*r != 0) {
    if (*r < 0x80) {
      *w++ = *r++;
    } else if ((*r == 0xC2 || *r == 0xC3) && r[1] >= 0x80 && r[1] < 0xC0) {
      *w++ = *r++;
      *w++ = *r++;
    } else if (r[0] == 0xE2 && r[1] == 0x80 && r[2] != 0) {
      if (r[2] == 0x93 || r[2] == 0x94) *w++ = '-';
      else if (r[2] == 0x98 || r[2] == 0x99) *w++ = '\'';
      else if (r[2] == 0x9C || r[2] == 0x9D) *w++ = '"';
      r += 3;
    } else {
      int len = (*r & 0xE0) == 0xC0 ? 2 : (*r & 0xF0) == 0xE0 ? 3 : (*r & 0xF8) == 0xF0 ? 4 : 1;
      for (int i = 0; i < len && *r != 0; ++i) ++r;
    }
  }
  *w = 0;
  // collapse a leading space left by a dropped emoji
  if (s[0] == ' ') memmove(s, s + 1, strlen(s));
}

void url_encode(const char* in, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p != 0 && o + 4 < n; ++p) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
        *p == '.' || *p == '_') {
      out[o++] = static_cast<char>(*p);
    } else {
      out[o++] = '%';
      out[o++] = hex[*p >> 4];
      out[o++] = hex[*p & 15];
    }
  }
  out[o] = '\0';
}

}  // namespace

const char* warning_name(uint8_t type) {
  static const char* k[12] = {"Wind",        "Gewitter",  "Regen",   "Schnee",  "Gl\xC3\xA4tte",  "Frost",
                              "Rutschungen", "Hitze",     "Lawinen", "Erdbeben", "Waldbrand", "Hochwasser"};
  return type < 12 ? k[type] : "Warnung";
}

int transit_search(const char* query, Station* out, int cap) {
  char q[150];
  url_encode(query, q, sizeof(q));
  char url[300];
  snprintf(url, sizeof(url),
           "https://transport.opendata.ch/v1/locations?type=station&query=%s"
           "&fields[]=stations/id&fields[]=stations/name",
           q);
  constexpr size_t kCap = 16 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return -1;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 10000);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Transit search HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return -1;
  }
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length);
  heap_caps_free(body);
  if (err) return -1;
  int n = 0;
  for (JsonObject s : doc["stations"].as<JsonArray>()) {
    const char* id = s["id"] | "";
    if (id[0] == '\0' || n >= cap) continue;  // addresses/POIs have no id
    snprintf(out[n].id, sizeof(out[n].id), "%s", id);
    snprintf(out[n].name, sizeof(out[n].name), "%s", s["name"] | "?");
    sanitize(out[n].name);
    n++;
  }
  console_printf("Transit search \"%s\" -> %d stations\n", query, n);
  return n;
}

namespace {
// One stationboard request into out (cap). false on HTTP/JSON error.
bool board_fetch(const char* id, Departure* out, int cap, int* n) {
  *n = 0;
  char url[440];
  snprintf(url, sizeof(url),
           "https://transport.opendata.ch/v1/stationboard?id=%s&limit=%d"
           "&fields[]=stationboard/category&fields[]=stationboard/number&fields[]=stationboard/to"
           "&fields[]=stationboard/stop/departureTimestamp&fields[]=stationboard/stop/delay"
           "&fields[]=stationboard/stop/platform&fields[]=stationboard/stop/prognosis/platform"
           "&fields[]=stationboard/stop/prognosis/departure",
           id, cap);
  constexpr size_t kCap = 24 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return false;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 10000);
  diag_svc(kSvcOev, http.ok && http.status == 200, http.status);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Stationboard HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return false;
  }
  int r = transit_parse_board(reinterpret_cast<const char*>(body), http.length, out, cap, sanitize);
  heap_caps_free(body);
  if (r < 0) {
    ESP_LOGW(TAG, "Stationboard JSON unreadable");
    return false;
  }
  *n = r;
  return true;
}

// Rail stations list no buses: those leave from a separate stop ("<Ort>, Bahnhof").
// Looked up once per station id and boot; "" = none.
struct BusStop {
  char rail[12];
  char bus[12];
  bool tried;
};
BusStop g_bus[kStationN] = {};

const char* bus_stop_for(const Station& st) {
  BusStop* e = nullptr;
  for (BusStop& b : g_bus)
    if (b.tried && strcmp(b.rail, st.id) == 0) return b.bus;
  for (BusStop& b : g_bus)
    if (!b.tried) { e = &b; break; }
  if (e == nullptr) e = &g_bus[0];  // stations changed: reuse a slot
  memset(e, 0, sizeof(*e));
  snprintf(e->rail, sizeof(e->rail), "%s", st.id);
  char name[64];
  if (!transit_bus_stop_name(st.name, name, sizeof(name))) {
    e->tried = true;
    return "";
  }
  Station hits[kMaxStationHits];
  int n = transit_search(name, hits, kMaxStationHits);
  if (n < 0) return "";  // network error: try again next time
  e->tried = true;
  char want[64];
  snprintf(want, sizeof(want), "%s", name);
  sanitize(want);
  for (int i = 0; i < n; ++i)
    if (strcmp(hits[i].name, want) == 0 && strcmp(hits[i].id, st.id) != 0) {
      snprintf(e->bus, sizeof(e->bus), "%s", hits[i].id);
      break;
    }
  return e->bus;
}
}  // namespace

bool transit_board(const Station& st, Board* out) {
  memset(out, 0, sizeof(*out));
  out->st = st;
  int n = 0;
  if (!board_fetch(st.id, out->d, kMaxDeps, &n)) return false;
  out->n = n;
  bool has_bus = false;
  for (int i = 0; i < n; ++i) has_bus |= out->d[i].bus;
  int nb = 0;
  if (!has_bus) {
    const char* bus = bus_stop_for(st);
    if (bus[0] != '\0') {
      Departure* tmp = static_cast<Departure*>(heap_caps_malloc(sizeof(Departure) * kMaxDeps, MALLOC_CAP_SPIRAM));
      if (tmp != nullptr) {
        if (board_fetch(bus, tmp, kMaxDeps, &nb)) out->n = transit_merge(out->d, out->n, tmp, nb, kMaxDeps);
        heap_caps_free(tmp);
      }
    }
  }
  out->fetched = time(nullptr);
  out->ok = true;
  console_printf("Stationboard: %d departures (%d from the bus stop)\n", out->n, nb);
  return true;
}

namespace {

// --- Abfallkalender-API: own cache, at most one fetch per day ---
struct WasteCache {
  uint32_t key;       // hash of base + BFS + zone
  int ok_day;         // year * 1000 + yday of the last good fetch (-1: none)
  int64_t tried;      // Unix seconds of the last attempt
  int http;           // status of the last attempt
  bool ok;
  int n;
  CalEvent ev[kMaxWaste];
  WasteApiInfo info;
};
WasteCache g_waste = {0, -1, 0, 0, false, 0, {}, {}};

void waste_update(const char* base, const char* bfs, const char* zone, int64_t now, int64_t midnight,
                  const struct tm& lt) {
  if (base == nullptr || base[0] == '\0' || bfs == nullptr || bfs[0] == '\0') {
    g_waste.ok = false;
    g_waste.n = 0;
    g_waste.key = 0;
    return;
  }
  uint32_t key = cal_hash(zone != nullptr ? zone : "", cal_hash(bfs, cal_hash(base)));
  int today = (lt.tm_year + 1900) * 1000 + lt.tm_yday;
  bool changed = key != g_waste.key;
  if (!changed && g_waste.ok && g_waste.ok_day == today) return;            // once per day
  if (!changed && g_waste.tried != 0 && now - g_waste.tried < 600) return;  // failed: retry after 10 min
  g_waste.key = key;
  g_waste.tried = now;
  if (changed) {
    g_waste.ok = false;
    g_waste.n = 0;
    memset(&g_waste.info, 0, sizeof(g_waste.info));
  }
  char url[320];
  if (!waste_api_url(url, sizeof(url), base, bfs, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday)) return;
  constexpr size_t kCap = 64 * 1024;
  uint8_t* body = psram(kCap + 1);
  if (body == nullptr) return;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 20000);
  diag_svc(kSvcWaste, http.ok && http.status == 200, http.status);
  g_waste.http = http.status;
  if (http.ok && http.status == 200) {
    CalEvent ev[kMaxWaste];
    WasteApiInfo info;
    int n = waste_parse_api(reinterpret_cast<char*>(body), http.length, zone, midnight, now + 45LL * 86400, ev,
                            kMaxWaste, local_mktime, &info);
    if (n >= 0) {
      for (int i = 0; i < n; ++i) sanitize(ev[i].title);
      sanitize(info.gemeinde);
      memcpy(g_waste.ev, ev, sizeof(ev));
      g_waste.n = n;
      g_waste.info = info;
      g_waste.ok = true;
      g_waste.ok_day = today;
      console_printf("Waste: %s (%d), %d dates, %d zones%s (%u bytes)\n", info.gemeinde, info.bfs, n, info.zones,
                     info.zone_used ? ", zone used" : "", static_cast<unsigned>(http.length));
    } else {
      console_printf("Waste API: unreadable answer (%u bytes)\n", static_cast<unsigned>(http.length));
    }
  } else {
    // 400: BFS missing, 404: unknown Gemeinde, 503: no data yet
    console_printf("Waste API: HTTP %d %s\n", http.status, http.error);
  }
  heap_caps_free(body);
}

}  // namespace

bool calendar_fetch(const char* ical_url, const char* waste_base, const char* bfs, const char* zone, Calendar* out) {
  memset(out, 0, sizeof(*out));
  const int64_t now = time(nullptr);
  // Today from local midnight, so all-day entries of today still count.
  time_t tn = static_cast<time_t>(now);
  struct tm lt;
  localtime_r(&tn, &lt);
  lt.tm_hour = 0;
  lt.tm_min = 0;
  lt.tm_sec = 0;
  lt.tm_isdst = -1;
  const int64_t midnight = mktime(&lt);
  out->have_events_url = ical_url != nullptr && ical_url[0] != '\0';
  if (out->have_events_url) {
    constexpr size_t kCap = 512 * 1024;
    uint8_t* body = psram(kCap + 1);
    if (body != nullptr) {
      HttpGetResult http = http_get(ical_url, nullptr, nullptr, body, kCap, 20000);
      diag_svc(kSvcCalendar, http.ok && http.status == 200, http.status);
      if (http.ok && http.status == 200 && http.length > 15 && memcmp(body, "BEGIN:VCALENDAR", 15) == 0) {
        out->n_events = ical_parse(reinterpret_cast<char*>(body), http.length, now, now + 60LL * 86400, out->events,
                                   kMaxEvents, 0, local_mktime);
        for (int i = 0; i < out->n_events; ++i) sanitize(out->events[i].title);
        out->ok_events = true;
        console_printf("Calendar: %d events in 60 days (%u bytes)\n", out->n_events, static_cast<unsigned>(http.length));
      } else {
        ESP_LOGW(TAG, "Calendar HTTP %d %s", http.status, http.error);  // never log the private URL
      }
      heap_caps_free(body);
    }
  }
  waste_update(waste_base, bfs, zone, now, midnight, lt);
  for (int i = 0; i < g_waste.n; ++i) {
    if (g_waste.ev[i].start + 86399 < midnight) continue;  // passed since the last fetch
    out->waste[out->n_waste++] = g_waste.ev[i];
  }
  out->ok_waste = g_waste.ok;
  snprintf(out->waste_name, sizeof(out->waste_name), "%s", g_waste.info.gemeinde);
  out->waste_zones = g_waste.info.zones;
  out->waste_zone_used = g_waste.info.zone_used;
  out->waste_http = g_waste.http;
  out->fetched = now;
  return out->ok_events || out->ok_waste;
}

bool meteo_fetch(const char* plz, MeteoNow* out) {
  memset(out, 0, sizeof(*out));
  char url[160];
  snprintf(url, sizeof(url), "https://app-prod-ws.meteoswiss-app.ch/v1/plzDetail?plz=%s", plz);
  constexpr size_t kCap = 64 * 1024;
  uint8_t* body = psram(kCap);
  if (body == nullptr) return false;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 15000, "Accept-Language", "de");
  diag_svc(kSvcMeteo, http.ok && http.status == 200, http.status);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "MeteoSchweiz HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return false;
  }
  StaticJsonDocument<512> filter;
  for (const char* k : {"warnType", "warnLevel", "text", "validFrom", "validTo", "outlook"}) filter["warnings"][0][k] = true;
  filter["graph"]["start"] = true;
  filter["graph"]["precipitation10m"] = true;
  filter["graph"]["precipitationMin10m"] = true;
  filter["graph"]["precipitationMax10m"] = true;
  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length,
                                             DeserializationOption::Filter(filter));
  heap_caps_free(body);
  if (err) {
    ESP_LOGW(TAG, "MeteoSchweiz JSON %s", err.c_str());
    return false;
  }
  const int64_t now = time(nullptr);
  for (JsonObject w : doc["warnings"].as<JsonArray>()) {
    int level = w["warnLevel"] | 0;
    int64_t to = (w["validTo"] | 0LL) / 1000;
    if (level < 2 || (to != 0 && to < now)) continue;
    Warning x = {};
    x.type = static_cast<uint8_t>(w["warnType"] | 99);
    x.level = static_cast<uint8_t>(level);
    x.outlook = w["outlook"] | false;
    x.from = (w["validFrom"] | 0LL) / 1000;
    x.to = to;
    snprintf(x.text, sizeof(x.text), "%s", w["text"] | "");
    sanitize(x.text);
    // insert by level (highest first)
    int pos = out->n;
    while (pos > 0 && out->w[pos - 1].level < x.level) --pos;
    if (pos >= kMaxWarnings) continue;
    int last = out->n < kMaxWarnings ? out->n : kMaxWarnings - 1;
    for (int i = last; i > pos; --i) out->w[i] = out->w[i - 1];
    out->w[pos] = x;
    if (out->n < kMaxWarnings) out->n++;
  }
  JsonObject g = doc["graph"];
  int64_t start = (g["start"] | 0LL) / 1000;
  JsonArray p = g["precipitation10m"];
  int first = start > 0 && now > start ? static_cast<int>((now - start) / 600) : 0;
  out->rain_start = start + static_cast<int64_t>(first) * 600;
  for (int i = 0; i < kRainSteps; ++i) {
    out->rain[i] = p[first + i] | 0.0f;
    out->rain_min[i] = g["precipitationMin10m"][first + i] | 0.0f;
    out->rain_max[i] = g["precipitationMax10m"][first + i] | 0.0f;
  }
  out->fetched = now;
  out->ok = start > 0 && static_cast<int>(p.size()) > first;
  console_printf("MeteoSchweiz %s: %d warnings, rain[0..5] %.1f %.1f %.1f %.1f %.1f %.1f mm/h\n", plz, out->n,
                 out->rain[0], out->rain[1], out->rain[2], out->rain[3], out->rain[4], out->rain[5]);
  return true;
}
