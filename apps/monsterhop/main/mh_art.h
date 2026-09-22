/*
 * MONSTER HOP - the art rendered in Blender (tools/blender/SPEC.md)
 *
 * One file, monsterhop.pak (tools/pack_assets.py), next to monsterhop.so on
 * the card. Its table is read when the app opens; each entry is a SHEET: all
 * the frames of one animation (or the variants of one tile), in one pixel
 * format, LZ4-compressed. Only what a level uses is loaded, in the worker.
 *
 * Every frame stores, per row, the span of columns that has pixels and only
 * those pixels, so transparent corners cost nothing. Formats:
 *   COL    tiles, props, objects: 4 bytes per pixel, RGB565 (native order),
 *          alpha, depth
 *   LID    characters: 3 bytes, (region id << 4 | alpha >> 4), light, depth;
 *          coloured through a palette table (mh_lut_t) when drawn
 *   PLANE  shadows: 1 byte, how much it darkens
 *   GLOW   light pools: 2 bytes, RGB565 added to the ground
 *   IMG    UI art: 3 bytes, RGB565, alpha
 * Depth is relative to the anchor: 128 = the anchor, 1 step = 1/32 m,
 * smaller = nearer, 255 = nothing there.
 */
#pragma once

#include "mh_gfx.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    MH_PX_COL = 1,
    MH_PX_LID = 2,
    MH_PX_PLANE = 3,
    MH_PX_GLOW = 4,
    MH_PX_IMG = 5,
    MH_BLOB = 8,        /* a level, a palette: raw bytes                    */
};

#define MH_Z_EMPTY 255

typedef struct {
    int16_t   w, h, ax, ay;     /* the anchor lands at (ax, ay) of the frame  */
    const uint16_t *span;       /* per row: x0, x1 (exclusive)               */
    const uint32_t *row;        /* per row: byte offset of its first pixel    */
    const uint8_t  *data;
} mh_spr_t;

typedef struct {
    uint8_t   fmt;              /* MH_PX_*                                    */
    uint8_t   n;                /* frames                                     */
    uint16_t  ms;               /* per frame, 0 = not an animation            */
    mh_spr_t *f;
    uint8_t  *mem;              /* the unpacked sheet: frames point into it   */
    uint32_t  bytes;            /* PSRAM it holds                             */
} mh_anim_t;

bool mh_art_open(const char *path);
void mh_art_close(void);
bool mh_art_ok(void);
bool mh_art_has(const char *name);
/* loads one sheet; false if missing or out of memory (out is zeroed) */
bool mh_art_load(const char *name, mh_anim_t *out);
void mh_anim_free(mh_anim_t *a);
/* a raw entry (levels, palettes); free() it */
uint8_t *mh_art_blob(const char *name, uint32_t *len);
/* bytes of PSRAM the loaded sheets hold (for the logs) */
uint32_t mh_art_bytes(void);
/* compressed bytes read from the card so far (only grows), and what the
 * entries whose name starts with prefix weigh: for the loading bar */
uint32_t mh_art_read(void);
uint32_t mh_art_prefix_bytes(const char *prefix);

static inline const uint8_t *mh_spr_row(const mh_spr_t *s, int y, int *x0, int *x1)
{
    *x0 = s->span[2 * y];
    *x1 = s->span[2 * y + 1];
    return s->data + s->row[y];
}

/* ---- palettes: 16 region colours -> a table of 16 x 64 light levels ---- */
typedef struct {
    uint32_t c[16];             /* 8-bit RGB per region id                    */
} mh_pal_t;

typedef struct {
    uint16_t c[16][64];         /* colour of region id at light level l*4     */
} mh_lut_t;

/* tint: an 8-bit RGB multiplier applied to everything (zone light), 0xFFFFFF
 * = none; `keep` is a bit mask of ids that are not tinted (glowing eyes) */
void mh_lut_build(mh_lut_t *l, const mh_pal_t *p, uint32_t tint, uint16_t keep);
/* a palette entry of the pack (pal_<name>): false if missing */
bool mh_pal_load(const char *name, mh_pal_t *out);
