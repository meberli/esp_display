#pragma once
// Windy Webcams API v3 (https://api.windy.com/webcams/docs), network task only.
// Every request sends the x-windy-api-key header. Image URLs from the API
// carry short-lived tokens (10 min on the free tier), so the details are
// fetched again right before each image download.

#include <stddef.h>
#include <stdint.h>

constexpr int kCamFrameW = 480;
constexpr int kCamFrameH = 480;

struct CamInfo {
  uint32_t id;
  char title[64];   // "Grabs · Nordwesten: Altmann" (German, Latin-1 subset)
  char city[40];
  char region[40];
  int64_t updated;  // lastUpdatedOn, Unix seconds (0 = unknown)
  char url[200];    // preview image (400x224 on the free tier); used once, never stored
  float km;         // distance for nearby results
};

// Details for one webcam. status = HTTP status (401/403: bad key, 404: no such ID).
bool windy_cam_info(const char* key, uint32_t id, CamInfo* out, int* status);
// Webcams within radius_km around lat/lon, nearest first. -1 on error.
int windy_nearby(const char* key, float lat, float lon, int radius_km, CamInfo* out, int cap, int* status);
// Downloads a JPEG and fills frame (kCamFrameW x kCamFrameH RGB565, PSRAM):
// fitted and centred, never upscaled (Windy terms), dark background around it.
bool windy_cam_image(const char* url, uint16_t* frame, char* err, size_t err_n);
