#pragma once
// Outline background map for the Regen radar (Natural Earth, public domain).
// Embedded at build time (tools/gen_map_ne.py); no runtime downloads. The
// caller owns the pixel buffer (allocated only while the Regen page is open).
#include <stdint.h>

// Renders borders, lakes and rivers into an RGB565 (native endian) buffer of
// size x size px, matching the RainViewer zoom-7 tile centred on lat/lon.
void map_ne_render(uint16_t* px, int size, float lat, float lon);
// i-th town (biggest first) inside the size x size window: name + pixel position. false when done.
bool map_ne_town(int i, int size, float lat, float lon, const char** name, int* x, int* y);
