#pragma once

// host_shim/lvgl_psram.h - PC-host stand-in for main/lvgl_psram.h.
//
// main/displays/lv_conf.h (the REAL firmware lv_conf.h, used UNMODIFIED by
// this simulator -- see tools/sim/CMakeLists.txt) sets LV_MEM_CUSTOM 1 and
// points LVGL's allocator at:
//   #define LV_MEM_CUSTOM_INCLUDE <lvgl_psram.h>
//   #define LV_MEM_CUSTOM_ALLOC   lv_psram_alloc
//   #define LV_MEM_CUSTOM_FREE    lv_psram_free
//   #define LV_MEM_CUSTOM_REALLOC lv_psram_realloc
// The real main/lvgl_psram.h (see that file) implements these with
// heap_caps_malloc/MALLOC_CAP_SPIRAM via ESP-IDF's esp_heap_caps.h and the
// firmware's own macros.h -- none of which exist, or make sense, on a PC
// host build (no PSRAM, no ESP-IDF).
//
// DECISION (documented per the task's "approach (a) vs (b)" choice):
// approach (b) -- a host-side allocator wrapper, NOT a modified copy of
// lv_conf.h. tools/sim/CMakeLists.txt compiles LVGL against the ORIGINAL,
// unmodified main/displays/lv_conf.h (so the simulator always reflects
// whatever lv_conf.h currently says) and adds ONLY this directory
// (host_shim/) to the `lvgl` target's include path -- main/ (which holds
// the real lvgl_psram.h) is deliberately never added to that target's
// include path, so there is no ambiguity about which header wins: this is
// the only lvgl_psram.h the simulator's LVGL build can see.
//
// These 3 functions keep LV_MEM_CUSTOM_ALLOC/FREE/REALLOC's exact
// signatures (see lvgl/src/misc/lv_mem.h) but forward straight to the
// host's libc allocator. No PSRAM, no capability flags, no logging -- a PC
// has one flat heap and this only needs to hand LVGL usable memory.
#include <stdlib.h>

static inline void *lv_psram_alloc(size_t size)
{
    return size ? malloc(size) : NULL;
}

static inline void lv_psram_free(void *ptr)
{
    free(ptr);
}

static inline void *lv_psram_realloc(void *ptr, size_t new_size)
{
    return realloc(ptr, new_size);
}
