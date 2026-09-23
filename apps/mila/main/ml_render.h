/*
 * MILA - a frame: the cache copied in, the moving things on top
 *
 * The worker builds a draw list per frame (ml_draw_t: a sprite at an LP
 * anchor with its depth), sorts it far to near, and renders the screen in
 * bands of rows in internal RAM: each band starts as a copy of the cache and
 * then every sprite that crosses it is drawn, depth-tested per pixel
 * against the cache. Sprites do not write depth: between moving things the
 * sort decides.
 */
#pragma once

#include "ml_art.h"
#include "ml_gfx.h"
#include "ml_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    DR_XRAY  = 1 << 0,      /* what is hidden shows as a faint silhouette   */
    DR_NOZ   = 1 << 1,      /* not depth-tested (effects above everything)  */
    DR_ADD   = 1 << 2,      /* COL added instead of blended (sparkles)       */
};

typedef struct {
    const ml_spr_t *s;
    const ml_lut_t *lut;        /* LID sprites                              */
    int16_t  x, y;              /* LP of the anchor                          */
    int16_t  d;                 /* depth of the anchor                       */
    uint8_t  fmt;               /* ML_PX_*                                   */
    uint8_t  alpha;             /* 0..255                                    */
    uint8_t  flags;             /* DR_*                                      */
    int8_t   prio;              /* order between equal depths (higher later) */
    uint16_t xray;              /* the silhouette's colour                   */
} ml_draw_t;

#define ML_MAX_DRAW 192

typedef struct {
    ml_draw_t d[ML_MAX_DRAW];
    int n;
} ml_dlist_t;

void ml_dlist_clear(ml_dlist_t *l);
ml_draw_t *ml_dlist_add(ml_dlist_t *l);
void ml_dlist_sort(ml_dlist_t *l);

/* rows y0..y1 of the screen (the band's image clips to them) */
void ml_render_band(const ml_world_t *w, ml_img_t *im, int cam_x, int cam_y, int y0, int y1,
                    const ml_dlist_t *l);
