#pragma once

// LVGL's allocator (LV_MEM_CUSTOM_ALLOC/FREE/REALLOC in lv_conf.h) - PSRAM
// first, internal RAM only as a fallback (2026-10-23).
//
// Why: lv_conf.h has always used LV_MEM_CUSTOM=1 with plain malloc(). The
// Arduino-ESP32 core sends small malloc()s to INTERNAL RAM (only large ones
// go to PSRAM), so every widget, style and table cell ate the ~110KB of
// internal heap that WiFi, FreeRTOS objects and SD file handles also need.
// Found on the bench: opening Settings (which builds every sub-page at once)
// took internal free from ~34KB to ~2KB, and the next fopen() - CAN capture
// starting - aborted inside newlib when it couldn't allocate a 100-byte
// mutex. Widget data in PSRAM costs a little render speed (it's read through
// the cache); the draw buffers themselves stay static in internal RAM
// (firmware.ino), so the pixel path is unaffected.

#include <stddef.h>
#include "esp_heap_caps.h"

#define LV_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static inline void *lv_psram_malloc(size_t size) {
    void *p = heap_caps_malloc(size, LV_PSRAM_CAPS);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

static inline void *lv_psram_realloc(void *ptr, size_t size) {
    void *p = heap_caps_realloc(ptr, size, LV_PSRAM_CAPS);
    return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

static inline void lv_psram_free(void *ptr) {
    heap_caps_free(ptr);
}
