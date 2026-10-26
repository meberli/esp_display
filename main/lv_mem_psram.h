#pragma once
// LVGL heap in PSRAM. LVGL 8.4 calls malloc/realloc/free for every object,
// style and label text (CONFIG_LV_MEM_CUSTOM); with
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL those small blocks all landed in
// internal RAM, and with the OV/Kalender/Wartung screens the Wi-Fi driver no
// longer got its RX buffers at boot ("wifi:malloc buffer fail").
// sdkconfig.defaults points CONFIG_LV_MEM_CUSTOM_INCLUDE here (relative to
// managed_components/lvgl__lvgl/src/misc/lv_mem.c); the macros only apply in
// that file. Internal RAM is the fallback when PSRAM is exhausted.
#include <stdlib.h>

#include "esp_heap_caps.h"

#define malloc(size) heap_caps_malloc_prefer((size), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT)
#define realloc(ptr, size) \
  heap_caps_realloc_prefer((ptr), (size), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT)
