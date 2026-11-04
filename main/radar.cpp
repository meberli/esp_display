#include "radar.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
// LVGL compiles lodepng.c as C; its header pulls C++ helpers under __cplusplus,
// so declare the one C entry point we use directly.
#include "lvgl.h"
extern "C" unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                                     const unsigned char* in, size_t insize);

namespace {
const char* const kTag = "radar";
constexpr size_t kBodyCap = 64 * 1024;
constexpr size_t kFrameBytes = static_cast<size_t>(kRadarTile) * kRadarTile * 3;

TaskHandle_t g_task = nullptr;
QueueHandle_t g_q = nullptr;
SemaphoreHandle_t g_mu = nullptr;  // guards g_frame/g_time/g_have against free
std::atomic<uint32_t> g_gen{0};
std::atomic<bool> g_want{false};
float g_lat = 0, g_lon = 0;  // written before g_gen++, read after
uint8_t* g_frame[kRadarMaxFrames] = {};
int64_t g_time[kRadarMaxFrames] = {};
uint32_t g_have_gen = 0;
std::atomic<int> g_loaded{0}, g_fail{0};
std::atomic<unsigned> g_ok_total{0}, g_err_total{0};

void* ps(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void free_all() {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  for (int i = 0; i < kRadarMaxFrames; ++i) {
    heap_caps_free(g_frame[i]);
    g_frame[i] = nullptr;
    g_time[i] = 0;
  }
  g_have_gen = 0;
  xSemaphoreGive(g_mu);
  g_loaded = 0;
}

void post(uint32_t gen, int idx, int n) {
  RadarEvent ev{gen, static_cast<int8_t>(idx), static_cast<int8_t>(n)};
  xQueueSend(g_q, &ev, 0);
}

// One load for generation gen. Returns when done, failed or superseded.
void load(uint32_t gen) {
  const float lat = g_lat, lon = g_lon;
  uint8_t* body = static_cast<uint8_t*>(ps(kBodyCap));
  RadarIndex* ix = static_cast<RadarIndex*>(ps(sizeof(RadarIndex)));
  int got = 0;
  if (body != nullptr && ix != nullptr) {
    HttpGetResult h = http_get("https://api.rainviewer.com/public/weather-maps.json", nullptr, nullptr, body,
                               kBodyCap, 8000);
    if (h.ok && h.status == 200 && radar_parse_index(reinterpret_cast<const char*>(body), h.length, ix)) {
      for (int k = 0; k < ix->n && g_gen == gen && g_want; ++k) {
        char url[160];
        if (!radar_tile_url(*ix, k, lat, lon, url, sizeof url)) continue;
        h = http_get(url, nullptr, nullptr, body, kBodyCap, 8000);
        if (!h.ok || h.status != 200) {
          ESP_LOGW(kTag, "tile %d HTTP %d", k, h.status);
          continue;
        }
        unsigned char* rgba = nullptr;
        unsigned w = 0, hh = 0;
        unsigned e = lodepng_decode32(&rgba, &w, &hh, body, h.length);  // lv_mem_alloc -> PSRAM
        if (e != 0 || w != kRadarTile || hh != kRadarTile) {
          ESP_LOGW(kTag, "tile %d PNG error %u (%ux%u)", k, e, w, hh);
          if (rgba != nullptr) lv_mem_free(rgba);
          continue;
        }
        uint8_t* px = static_cast<uint8_t*>(ps(kFrameBytes));
        if (px != nullptr) radar_rgba_to_565a(rgba, px, kRadarTile * kRadarTile);
        lv_mem_free(rgba);
        if (px == nullptr) break;
        xSemaphoreTake(g_mu, portMAX_DELAY);
        if (g_gen == gen && g_want) {
          g_frame[got] = px;
          g_time[got] = ix->t[k];
          g_have_gen = gen;
          px = nullptr;
        }
        xSemaphoreGive(g_mu);
        if (px != nullptr) {  // superseded meanwhile
          heap_caps_free(px);
          break;
        }
        g_loaded = got + 1;
        post(gen, got, ix->n);
        ++got;
      }
    } else {
      ESP_LOGW(kTag, "index HTTP %d %s", h.status, h.ok ? "" : h.error);
    }
  }
  heap_caps_free(body);
  heap_caps_free(ix);
  if (g_gen != gen || !g_want) return;
  if (got == 0) {
    ++g_err_total;
    g_fail = 1;
    post(gen, -1, 0);
  } else {
    ++g_ok_total;
  }
}

void task(void*) {
  uint32_t done = 0;
  for (;;) {
    if (!g_want) {
      if (g_loaded > 0 || g_have_gen != 0) free_all();
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    const uint32_t gen = g_gen;
    if (gen != done) {
      free_all();
      g_fail = 0;
      load(gen);
      done = gen;
      continue;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));  // wait for close / reopen
  }
}
}  // namespace

uint32_t radar_open(float lat, float lon) {
  if (g_q == nullptr) {
    g_q = xQueueCreate(16, sizeof(RadarEvent));
    g_mu = xSemaphoreCreateMutex();
  }
  if (g_q == nullptr || g_mu == nullptr) return 0;
  xQueueReset(g_q);
  g_lat = lat;
  g_lon = lon;
  const uint32_t gen = ++g_gen;
  g_want = true;
  if (g_task == nullptr) {
    // PSRAM stack like the Co4 task: TLS + PNG decode, never touches flash.
    if (xTaskCreatePinnedToCoreWithCaps(task, "radar", 12 * 1024, nullptr, 2, &g_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
      g_task = nullptr;
      post(gen, -1, 0);
      return gen;
    }
  }
  xTaskNotifyGive(g_task);
  return gen;
}

void radar_close() {
  g_want = false;
  ++g_gen;
  if (g_task != nullptr) xTaskNotifyGive(g_task);
}

bool radar_take(RadarEvent* ev) { return g_q != nullptr && xQueueReceive(g_q, ev, 0) == pdTRUE; }

const uint8_t* radar_frame(uint32_t gen, int idx, int64_t* t) {
  if (g_mu == nullptr || idx < 0 || idx >= kRadarMaxFrames) return nullptr;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  const uint8_t* p = g_have_gen == gen && g_gen == gen ? g_frame[idx] : nullptr;
  if (t != nullptr) *t = p != nullptr ? g_time[idx] : 0;
  xSemaphoreGive(g_mu);
  return p;
}

void radar_diag(char* out, unsigned n) {
  snprintf(out, n, "RADAR open=%d frames=%d fail=%d ok=%u err=%u task=%s psram_frames=%uKB", g_want ? 1 : 0,
           g_loaded.load(), g_fail.load(), g_ok_total.load(), g_err_total.load(), g_task ? "yes" : "no",
           static_cast<unsigned>(g_loaded.load() * kFrameBytes / 1024));
}
