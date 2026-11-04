#include "radar_parse.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

bool radar_parse_index(const char* json, size_t len, RadarIndex* out) {
  memset(out, 0, sizeof(*out));
  StaticJsonDocument<192> filter;
  filter["host"] = true;
  filter["radar"]["past"][0]["time"] = true;
  filter["radar"]["past"][0]["path"] = true;
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
  const char* host = doc["host"] | "";
  if (strncmp(host, "https://", 8) != 0 || strlen(host) >= sizeof(out->host)) return false;
  snprintf(out->host, sizeof(out->host), "%s", host);
  JsonArrayConst past = doc["radar"]["past"].as<JsonArrayConst>();
  const int total = static_cast<int>(past.size());
  int first = total > kRadarMaxFrames ? total - kRadarMaxFrames : 0;
  for (int i = first; i < total; ++i) {
    const char* p = past[i]["path"] | "";
    const int64_t t = past[i]["time"] | 0LL;
    if (p[0] != '/' || strlen(p) >= sizeof(out->path[0]) || t <= 0) continue;
    snprintf(out->path[out->n], sizeof(out->path[0]), "%s", p);
    out->t[out->n] = t;
    ++out->n;
  }
  return out->n > 0;
}

bool radar_tile_url(const RadarIndex& ix, int k, float lat, float lon, char* out, size_t n) {
  if (k < 0 || k >= ix.n || !isfinite(lat) || !isfinite(lon)) return false;
  int w = snprintf(out, n, "%s%s/%d/7/%.4f/%.4f/2/1_0.png", ix.host, ix.path[k], kRadarTile, lat, lon);
  return w > 0 && static_cast<size_t>(w) < n;
}

int radar_rgba_to_565a(const uint8_t* rgba, uint8_t* out, int px) {
  int vis = 0;
  for (int i = 0; i < px; ++i) {
    const uint8_t r = rgba[4 * i], g = rgba[4 * i + 1], b = rgba[4 * i + 2], a = rgba[4 * i + 3];
    const uint16_t c = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    out[3 * i] = static_cast<uint8_t>(c & 0xFF);
    out[3 * i + 1] = static_cast<uint8_t>(c >> 8);
    out[3 * i + 2] = a;
    if (a != 0) ++vis;
  }
  return vis;
}
