#pragma once
// Rain radar for the Regen window: RainViewer tiles (free, no key, attribution
// "RainViewer"), fetched by an own task on core 0 only while the window is open.
// Frames live in PSRAM and are freed as soon as the window closes.
#include <stdint.h>

#include "radar_parse.h"

struct RadarEvent {
  uint32_t gen;  // radar_open() generation the event belongs to
  int8_t idx;    // frame ready (0 = oldest), -1 = failure
  int8_t n;      // frames expected
};

// Start (or restart) loading for lat/lon. Returns the generation.
uint32_t radar_open(float lat, float lon);
// Window closed: the task drops/frees all frames. The UI must not use frame
// pointers after this call.
void radar_close();
// UI task: next event, false if none.
bool radar_take(RadarEvent* ev);
// Pixel data (LV_IMG_CF_TRUE_COLOR_ALPHA, kRadarTile^2 * 3 bytes) of a frame
// announced for gen; nullptr if not (or no longer) there.
const uint8_t* radar_frame(uint32_t gen, int idx, int64_t* t);
// DIAG: state without coordinates.
void radar_diag(char* out, unsigned n);
