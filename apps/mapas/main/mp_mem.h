/*
 * MAPAS - memory. Everything the app keeps goes to PSRAM on purpose: with
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL a plain malloc() of 1 KB or less
 * lands in internal RAM, and the first build baked its glyphs one malloc
 * each (600-1000 bytes): the three fonts took the internal RAM the worker's
 * stack needed, and the worker did not start (2026-09-26).
 */
#pragma once

#include <stddef.h>
#include <stdlib.h>

#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
#include "esp_heap_caps.h"
static inline void *mp_malloc(size_t n)
{
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *mp_realloc(void *p, size_t n)
{
    return heap_caps_realloc(p, n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void mp_free(void *p)
{
    heap_caps_free(p);
}
#else
static inline void *mp_malloc(size_t n) { return malloc(n ? n : 1); }
static inline void *mp_realloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
static inline void mp_free(void *p) { free(p); }
#endif

static inline void *mp_calloc(size_t n)
{
    void *p = mp_malloc(n);
    if (p) {
        unsigned char *c = (unsigned char *)p;
        for (size_t i = 0; i < n; i++) c[i] = 0;
    }
    return p;
}
