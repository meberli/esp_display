#include "map_ne.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {
struct MapLine { uint16_t start, n; uint8_t kind; };  // 0 border, 1 lake, 2 river
struct MapTown { const char* name; int16_t x, y; };
#include "map_ne_data.inc"

constexpr uint16_t rgb(uint32_t c) {
  return static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c & 0xFF) >> 3));
}
constexpr uint16_t kBg = rgb(0x0B1220), kKind[3] = {rgb(0x9AA4B6), rgb(0x4F8FD0), rgb(0x3A6EA8)};

// Window origin in map units (world px * SUB relative to the data origin).
void origin(int size, float lat, float lon, double* ox, double* oy) {
  const double w = 256.0 * 128;
  const double s = sin(lat * M_PI / 180.0);
  const double cx = (lon + 180.0) / 360.0 * w;
  const double cy = (0.5 - log((1 + s) / (1 - s)) / (4 * M_PI)) * w;
  *ox = (cx - size / 2.0 - MAP_NE_ORIGIN_X) * MAP_NE_SUB;
  *oy = (cy - size / 2.0 - MAP_NE_ORIGIN_Y) * MAP_NE_SUB;
}

void line(uint16_t* px, int size, int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (int guard = 0; guard < 4096; ++guard) {
    if (x0 >= 0 && y0 >= 0 && x0 < size && y0 < size) px[y0 * size + x0] = c;
    if (x0 == x1 && y0 == y1) return;
    int e2 = 2 * e;
    if (e2 >= dy) { e += dy; x0 += sx; }
    if (e2 <= dx) { e += dx; y0 += sy; }
  }
}
}  // namespace

void map_ne_render(uint16_t* px, int size, float lat, float lon) {
  for (int i = 0; i < size * size; ++i) px[i] = kBg;
  double ox, oy;
  origin(size, lat, lon, &ox, &oy);
  for (int pass = 2; pass >= 0; --pass) {  // rivers, lakes, borders on top
    for (const MapLine& l : kMapLines) {
      if (l.kind != pass) continue;
      int px0 = 0, py0 = 0;
      for (int k = 0; k < l.n; ++k) {
        const int16_t* p = kMapPts[l.start + k];
        const int x = static_cast<int>(floor((p[0] - ox) / MAP_NE_SUB));
        const int y = static_cast<int>(floor((p[1] - oy) / MAP_NE_SUB));
        if (k > 0 && !((px0 < 0 && x < 0) || (py0 < 0 && y < 0) || (px0 >= size && x >= size) ||
                       (py0 >= size && y >= size)))
          line(px, size, px0, py0, x, y, kKind[l.kind]);
        px0 = x;
        py0 = y;
      }
    }
  }
}

bool map_ne_town(int i, int size, float lat, float lon, const char** name, int* x, int* y) {
  double ox, oy;
  origin(size, lat, lon, &ox, &oy);
  int seen = 0;
  for (const MapTown& t : kMapTowns) {
    const int tx = static_cast<int>(floor((t.x - ox) / MAP_NE_SUB)), ty = static_cast<int>(floor((t.y - oy) / MAP_NE_SUB));
    if (tx < 4 || ty < 4 || tx >= size - 4 || ty >= size - 4) continue;
    if (seen++ == i) {
      *name = t.name;
      *x = tx;
      *y = ty;
      return true;
    }
  }
  return false;
}
