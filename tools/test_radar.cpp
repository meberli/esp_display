// Host test: RainViewer index parsing, tile URL, RGBA -> RGB565+A conversion.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "radar_parse.h"

int main() {
  const char* j = R"({"version":"2.0","generated":1000,"host":"https://tilecache.example.com",
   "radar":{"past":[{"time":100,"path":"/v2/radar/a1"},{"time":200,"path":"/v2/radar/a2"},
   {"time":300,"path":"/v2/radar/a3"},{"time":400,"path":"/v2/radar/a4"},{"time":500,"path":"/v2/radar/a5"},
   {"time":600,"path":"/v2/radar/a6"},{"time":700,"path":"/v2/radar/a7"},{"time":800,"path":"/v2/radar/a8"}],
   "nowcast":[]},"satellite":{"infrared":[]}})";
  RadarIndex ix;
  assert(radar_parse_index(j, strlen(j), &ix));
  assert(ix.n == kRadarMaxFrames && ix.t[0] == 300 && ix.t[5] == 800);
  assert(!strcmp(ix.path[5], "/v2/radar/a8"));
  char url[160];
  assert(radar_tile_url(ix, 5, 47.0f, 9.5f, url, sizeof url));
  assert(!strcmp(url, "https://tilecache.example.com/v2/radar/a8/256/7/47.0000/9.5000/2/1_0.png"));
  assert(!radar_tile_url(ix, 6, 47.0f, 9.5f, url, sizeof url));
  const char* bad = R"({"host":"http://x","radar":{"past":[{"time":1,"path":"/p"}]}})";
  assert(!radar_parse_index(bad, strlen(bad), &ix));
  const char* empty = R"({"host":"https://x","radar":{"past":[]}})";
  assert(!radar_parse_index(empty, strlen(empty), &ix));
  assert(!radar_parse_index("{", 1, &ix));

  uint8_t px[8] = {255, 0, 0, 255, 0, 0, 255, 0};  // red opaque, blue transparent
  uint8_t o[8];
  assert(radar_rgba_to_565a(px, px, 2) == 1);  // in place
  memcpy(o, px, 6);
  assert(o[0] == 0x00 && o[1] == 0xF8 && o[2] == 255);
  assert(o[3] == 0x1F && o[4] == 0x00 && o[5] == 0);
  puts("radar OK");
  return 0;
}
