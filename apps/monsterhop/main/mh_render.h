/*
 * MONSTER HOP - a frame: the cache copied in, the moving things on top
 *
 * The worker builds a draw list per frame (mh_draw_t: a sprite at an LP
 * anchor with its depth), sorts it far to near, and renders the screen in
 * bands of rows in internal RAM: each band starts as a copy of the cache and
 * then every sprite that crosses it is drawn, depth-tested per pixel
 * against the cache. Sprites do not write depth: between moving things the
 * sort decides.
 */
#pragma once

#include "mh_art.h"
#include "mh_gfx.h"
#include "mh_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    DR_XRAY  = 1 << 0,      /* what is hidden shows as a faint silhouette   */
    DR_NOZ   = 1 << 1,      /* not depth-tested (effects above everything)  */
    DR_ADD   = 1 << 2,      /* COL added instead of blended (sparkles)       */
};

typedef struct {
    const mh_spr_t *s;
    const mh_lut_t *lut;        /* LID sprites                              */
    int16_t  x, y;              /* LP of the anchor                          */
    int16_t  d;                 /* depth of the anchor                       */
    uint8_t  fmt;               /* MH_PX_*                                   */
    uint8_t  alpha;             /* 0..255                                    */
    uint8_t  flags;             /* DR_*                                      */
    int8_t   prio;              /* order between equal depths (higher later) */
    uint16_t xray;              /* the silhouette's colour                   */
} mh_draw_t;

#define MH_MAX_DRAW 192

typedef struct {
    mh_draw_t d[MH_MAX_DRAW];
    int n;
} mh_dlist_t;

void mh_dlist_clear(mh_dlist_t *l);
mh_draw_t *mh_dlist_add(mh_dlist_t *l);
void mh_dlist_sort(mh_dlist_t *l);

/* rows y0..y1 of the screen (the band's image clips to them) */
void mh_render_band(const mh_world_t *w, mh_img_t *im, int cam_x, int cam_y, int y0, int y1,
                    const mh_dlist_t *l);
