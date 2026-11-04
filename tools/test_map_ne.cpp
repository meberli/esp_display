// Host test: embedded Natural Earth map renders and places Buchs SG at the centre.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "map_ne.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)
int main(int argc, char** argv) {
  static uint16_t px[256 * 256];
  map_ne_render(px, 256, 47.168f, 9.478f);
  int drawn = 0;
  for (uint16_t p : px) drawn += p != px[0];
  CHECK(drawn > 1500);
  bool buchs = false; const char* n; int x, y;
  for (int i = 0; map_ne_town(i, 256, 47.168f, 9.478f, &n, &x, &y); ++i)
    if (strcmp(n, "Buchs SG") == 0) { buchs = true; CHECK(abs(x - 128) <= 1 && abs(y - 128) <= 1); }
  CHECK(buchs);
  map_ne_render(px, 256, 0.0f, 0.0f);  // outside the bbox: background only
  drawn = 0;
  for (uint16_t p : px) drawn += p != px[0];
  CHECK(drawn == 0);
  if (argc > 1) {  // optional PPM preview
    map_ne_render(px, 256, 47.168f, 9.478f);
    FILE* f = fopen(argv[1], "wb"); fprintf(f, "P6 256 256 255\n");
    for (uint16_t p : px) { unsigned char c[3] = {(unsigned char)((p >> 8) & 0xF8), (unsigned char)((p >> 3) & 0xFC), (unsigned char)(p << 3)}; fwrite(c, 1, 3, f); }
    fclose(f);
  }
  printf(fails ? "map_ne: %d FAIL\n" : "map_ne: ok\n", fails);
  return fails ? 1 : 0;
}
