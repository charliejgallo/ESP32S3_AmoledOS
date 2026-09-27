/*
 * MAPAS - a view of the world into a buffer: which tiles, the style, the
 * labels.
 *
 * Positions are Web Mercator normalised to 32 bits: x = 0 at 180°W, 2^32 at
 * 180°E; y = 0 at the top (85°N), 2^32 at the bottom. A uint32 carries a
 * point to a centimetre, where a float would jitter by metres at street
 * zoom; the renderer only ever works with differences, which fit a float.
 *
 * Zoom follows the usual 256-pixel convention (z 0 = the world in 256 px);
 * the data comes from tiles one level below (dz = z - 1), because
 * OpenMapTiles tiles are designed to be seen at 512 px, and never deeper than
 * 14, the deepest the service has: past z 15 the same z14 tile is enlarged,
 * which vectors do cleanly.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mp_draw.h"

#define MP_ZMIN     2.0f
#define MP_ZMAX     18.5f
#define MP_DZ_MAX   14

typedef struct {
    uint32_t cx, cy;
    float    z;
} mp_view_t;

typedef struct {
    uint8_t  z;
    uint32_t x, y;
} mp_key_t;

#define MP_MAX_MISSING 24

typedef struct {
    int      missing;               /* tiles not on the watch */
    mp_key_t miss[MP_MAX_MISSING];  /* nearest to the centre first */
    int      shown;                 /* tiles drawn from their own data */
    int      stand_in;              /* tiles drawn from an enlarged ancestor */
    uint32_t us_tiles;              /* finding them: RAM, packs, card */
    uint32_t us_geom, us_labels;    /* for the log */
    uint32_t us_ldraw;              /* of us_labels, drawing the glyphs */
    uint32_t us_cls[32];            /* geometry time per class */
    uint32_t pts_in, pts_out;       /* points before and after simplifying */
    int      labels;
} mp_render_stats_t;

enum { MP_FONT_S = 0, MP_FONT_M, MP_FONT_L, MP_FONTS };

#define MP_BG 0x0B0D11              /* the land: nearly black, for the AMOLED */

/* Draws view v into fb (the whole buffer, centred). fonts may be NULL (no
 * labels). */
void mp_render(mp_fb_t *fb, const mp_view_t *v, const mp_font_t *fonts, mp_render_stats_t *st);

/* Areas of the buffer kept free of labels (x0, y0, x1, y1 in buffer
 * pixels): what the app draws on top, where the screen will be. */
void mp_render_reserve(const int16_t (*rects)[4], int n);

/* Degrees <-> normalised Mercator. */
void  mp_lonlat_to_world(float lon, float lat, uint32_t *x, uint32_t *y);
void  mp_world_to_lonlat(uint32_t x, uint32_t y, float *lon, float *lat);
/* Metres per screen pixel at the view's latitude. */
float mp_metres_per_px(const mp_view_t *v);
/* Screen pixels per world unit (2^-32 of the world) at zoom z. */
float mp_px_per_unit(float z);
