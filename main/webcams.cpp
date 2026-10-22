#include "diag.h"
#include "webcams.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "net.h"
#include "sys.h"
#include <esp_log.h>

static const char* const TAG = "webcams";

namespace {

constexpr const char* kApi = "https://api.windy.com/webcams/api/v3/webcams";
constexpr const char* kKeyHeader = "x-windy-api-key";
constexpr size_t kJsonCap = 48 * 1024;
constexpr size_t kJpegCap = 512 * 1024;

// Keeps ASCII and Latin-1 (the range of the Inter fonts); maps a few
// typographic signs Windy uses ("›", dashes) and drops the rest.
void sanitize(const char* in, char* out, size_t n) {
  size_t o = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(in);
  while (*p != 0 && o + 3 < n) {
    if (*p < 0x80) {
      out[o++] = static_cast<char>(*p++);
    } else if ((*p == 0xC2 || *p == 0xC3) && p[1] != 0) {
      out[o++] = static_cast<char>(*p++);
      out[o++] = static_cast<char>(*p++);
    } else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] != 0) {
      if (p[2] == 0xBA || p[2] == 0xB9) {  // › ‹
        out[o++] = static_cast<char>(0xC2);
        out[o++] = static_cast<char>(0xB7);
      } else if (p[2] == 0x93 || p[2] == 0x94) {  // – —
        out[o++] = '-';
      }
      p += 3;
    } else {
      ++p;
      while ((*p & 0xC0) == 0x80) ++p;  // skip continuation bytes
    }
  }
  out[o] = '\0';
}

int64_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// "2026-10-12T11:00:30.000Z" -> Unix seconds (UTC).
int64_t parse_iso(const char* s) {
  int y, mo, d, h, mi, se;
  if (s == nullptr || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  return days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
}

void fill_info(JsonObjectConst w, CamInfo* out) {
  memset(out, 0, sizeof(*out));
  out->id = w["webcamId"] | 0u;
  sanitize(w["title"] | "", out->title, sizeof(out->title));
  sanitize(w["location"]["city"] | "", out->city, sizeof(out->city));
  sanitize(w["location"]["region"] | "", out->region, sizeof(out->region));
  out->updated = parse_iso(w["lastUpdatedOn"] | "");
  snprintf(out->url, sizeof(out->url), "%s", w["images"]["current"]["preview"] | "");
}

void make_filter(JsonDocument& f, bool list) {
  JsonObject w;
  if (list) {
    f["total"] = true;
    w = f.createNestedArray("webcams").createNestedObject();
  } else {
    w = f.to<JsonObject>();
  }
  w["webcamId"] = true;
  w["title"] = true;
  w["lastUpdatedOn"] = true;
  w["location"]["city"] = true;
  w["location"]["region"] = true;
  w["location"]["latitude"] = true;
  w["location"]["longitude"] = true;
  w["images"]["current"]["preview"] = true;
}

bool get_json(const char* key, const char* url, bool list, DynamicJsonDocument& doc, int* status) {
  uint8_t* body = static_cast<uint8_t*>(heap_caps_malloc(kJsonCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (body == nullptr) return false;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kJsonCap, 10000, kKeyHeader, key);
  if (status != nullptr) *status = http.status;
  diag_svc(kSvcWindy, http.ok && http.status == 200, http.status);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Windy HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return false;
  }
  StaticJsonDocument<512> filter;
  make_filter(filter, list);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length,
                                             DeserializationOption::Filter(filter));
  heap_caps_free(body);
  if (err) {
    ESP_LOGW(TAG, "Windy JSON %s", err.c_str());
    return false;
  }
  return true;
}

float km_between(float lat1, float lon1, float lat2, float lon2) {
  const float r = 0.01745329f;
  float x = (lon2 - lon1) * r * cosf((lat1 + lat2) * 0.5f * r);
  float y = (lat2 - lat1) * r;
  return 6371.f * sqrtf(x * x + y * y);
}

}  // namespace

bool windy_cam_info(const char* key, uint32_t id, CamInfo* out, int* status) {
  char url[200];
  snprintf(url, sizeof(url), "%s/%lu?lang=de&include=images,location", kApi, static_cast<unsigned long>(id));
  DynamicJsonDocument doc(4096);
  if (!get_json(key, url, false, doc, status)) return false;
  fill_info(doc.as<JsonObjectConst>(), out);
  return out->id == id;
}

int windy_nearby(const char* key, float lat, float lon, int radius_km, CamInfo* out, int cap, int* status) {
  char url[240];
  snprintf(url, sizeof(url), "%s?lang=de&nearby=%.4f,%.4f,%d&limit=%d&include=location", kApi, lat, lon, radius_km,
           cap > 50 ? 50 : cap);
  DynamicJsonDocument doc(16384);
  if (!get_json(key, url, true, doc, status)) return -1;
  int n = 0;
  for (JsonObjectConst w : doc["webcams"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    fill_info(w, &out[n]);
    out[n].km = km_between(lat, lon, w["location"]["latitude"] | lat, w["location"]["longitude"] | lon);
    n++;
  }
  // nearest first (insertion sort, n <= 50)
  for (int i = 1; i < n; ++i) {
    CamInfo t = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].km > t.km) {
      out[j + 1] = out[j];
      --j;
    }
    out[j + 1] = t;
  }
  console_printf("Windy nearby %.4f,%.4f r=%d -> %d (total %d)\n", lat, lon, radius_km, n, doc["total"] | 0);
  return n;
}

bool windy_cam_image(const char* url, uint16_t* frame, char* err, size_t err_n) {
  err[0] = '\0';
  if (url == nullptr || url[0] == '\0') {
    snprintf(err, err_n, "kein Bild");
    return false;
  }
  uint8_t* jpg = static_cast<uint8_t*>(heap_caps_malloc(kJpegCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (jpg == nullptr) {
    snprintf(err, err_n, "kein Speicher");
    return false;
  }
  HttpGetResult http = http_get(url, nullptr, nullptr, jpg, kJpegCap, 15000);
  diag_svc(kSvcWindy, http.ok && http.status == 200, http.status);
  if (!http.ok || http.status != 200) {
    snprintf(err, err_n, "Bild HTTP %d %s", http.status, http.error);
    heap_caps_free(jpg);
    return false;
  }
  esp_jpeg_image_cfg_t cfg = {};
  cfg.indata = jpg;
  cfg.indata_size = http.length;
  cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
  cfg.out_scale = JPEG_IMAGE_SCALE_0;
  esp_jpeg_image_output_t info = {};
  if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || info.width == 0 || info.height == 0) {
    snprintf(err, err_n, "JPEG ungueltig");
    heap_caps_free(jpg);
    return false;
  }
  // Large sources are decoded at 1/2..1/8 as long as they still cover the frame.
  int scale = 0;
  while (scale < 3 && (info.width >> (scale + 1)) >= kCamFrameW && (info.height >> (scale + 1)) >= kCamFrameH) ++scale;
  int sw = (info.width + (1 << scale) - 1) >> scale;
  int sh = (info.height + (1 << scale) - 1) >> scale;
  size_t src_n = static_cast<size_t>(sw + 8) * (sh + 8) * 2;
  uint16_t* src = static_cast<uint16_t*>(heap_caps_malloc(src_n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (src == nullptr) {
    snprintf(err, err_n, "kein Speicher (%ux%u)", info.width, info.height);
    heap_caps_free(jpg);
    return false;
  }
  cfg.indata = jpg;
  cfg.indata_size = http.length;
  cfg.outbuf = reinterpret_cast<uint8_t*>(src);
  cfg.outbuf_size = src_n;
  cfg.out_scale = static_cast<esp_jpeg_image_scale_t>(scale);
  cfg.priv.read = 0;
  esp_jpeg_image_output_t out = {};
  esp_err_t e = esp_jpeg_decode(&cfg, &out);
  heap_caps_free(jpg);
  if (e != ESP_OK) {
    snprintf(err, err_n, "JPEG %s", esp_err_to_name(e));
    heap_caps_free(src);
    return false;
  }
  sw = out.width;
  sh = out.height;
  // Windy terms: never stretch images (original size or smaller only). So the
  // picture is fitted into the frame without upscaling and centred on a dark
  // background; larger sources are reduced (bilinear, 16.16 fixed point).
  const uint16_t bg = 0x0821;  // ~#05070C
  for (int i = 0; i < kCamFrameW * kCamFrameH; ++i) frame[i] = bg;
  float s = fminf(1.f, fminf(static_cast<float>(kCamFrameW) / sw, static_cast<float>(kCamFrameH) / sh));
  int dw = static_cast<int>(sw * s);
  int dh = static_cast<int>(sh * s);
  if (dw > kCamFrameW) dw = kCamFrameW;
  if (dh > kCamFrameH) dh = kCamFrameH;
  int ox = (kCamFrameW - dw) / 2;
  int oy = (kCamFrameH - dh) / 2;
  int32_t step = static_cast<int32_t>(65536.f / s);
  for (int y = 0; y < dh; ++y) {
    uint16_t* dst = frame + (oy + y) * kCamFrameW + ox;
    if (s >= 1.f) {
      memcpy(dst, src + y * sw, dw * sizeof(uint16_t));
      continue;
    }
    int32_t fy = y * step;
    int iy = fy >> 16;
    if (iy >= sh - 1) {
      iy = sh - 2;
      fy = (iy << 16) | 0xFFFF;
    }
    uint32_t wy = (fy >> 8) & 0xFF;
    const uint16_t* r0 = src + iy * sw;
    const uint16_t* r1 = r0 + sw;
    for (int x = 0; x < dw; ++x) {
      int32_t fx = x * step;
      int ix = fx >> 16;
      if (ix >= sw - 1) {
        ix = sw - 2;
        fx = (ix << 16) | 0xFFFF;
      }
      uint32_t wx = (fx >> 8) & 0xFF;
      uint32_t p[4] = {r0[ix], r0[ix + 1], r1[ix], r1[ix + 1]};
      uint32_t w4[4] = {(256 - wx) * (256 - wy), wx * (256 - wy), (256 - wx) * wy, wx * wy};
      uint32_t r = 0, g = 0, b = 0;
      for (int k = 0; k < 4; ++k) {
        r += ((p[k] >> 11) & 0x1F) * w4[k];
        g += ((p[k] >> 5) & 0x3F) * w4[k];
        b += (p[k] & 0x1F) * w4[k];
      }
      dst[x] = static_cast<uint16_t>(((r >> 16) << 11) | ((g >> 16) << 5) | (b >> 16));
    }
    if ((y & 63) == 63) vTaskDelay(1);
  }
  heap_caps_free(src);
  console_printf("Webcam image %u bytes %dx%d (1/%d) -> %dx%d at %d,%d\n", static_cast<unsigned>(http.length), sw, sh,
                 1 << scale, dw, dh, ox, oy);
  return true;
}
