#pragma once
// RainViewer weather-maps.json parsing and pixel conversion (host-testable).
#include <stddef.h>
#include <stdint.h>

constexpr int kRadarMaxFrames = 6;  // newest past frames (10 min apart)
constexpr int kRadarTile = 256;     // px

struct RadarIndex {
  char host[48];  // "https://tilecache.rainviewer.com"
  int n;          // frames, oldest first
  char path[kRadarMaxFrames][40];
  int64_t t[kRadarMaxFrames];  // Unix seconds
};

// false if unreadable / no frames. Keeps the newest kRadarMaxFrames.
bool radar_parse_index(const char* json, size_t len, RadarIndex* out);
// Tile URL centred on lat/lon (zoom 7, Universal Blue, smoothed, no snow).
bool radar_tile_url(const RadarIndex& ix, int k, float lat, float lon, char* out, size_t n);
// RGBA8888 -> LVGL 8 LV_IMG_CF_TRUE_COLOR_ALPHA at 16 bit (RGB565 little endian
// + alpha, 3 bytes/px). In place is fine (out <= in). Returns non-transparent px.
int radar_rgba_to_565a(const uint8_t* rgba, uint8_t* out, int px);
