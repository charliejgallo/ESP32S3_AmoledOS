/*
 * MILA - the level on screen: projection, the kit's art, the background
 * cache, the whole-level picture
 *
 * The projection is the one the art was rendered with (tools/blender/
 * ml_common.py): one cell is (72, 0) px to the right and (0, 54) px down the
 * screen, one metre up is (0, -47.62) px. Positions on the "level plane"
 * (LP) are pixels of an imaginary picture of the whole level; the camera is
 * the LP pixel at the screen's top-left. Grid coordinates (gx, gy) are cells
 * with the cell's centre at +0.5, gy growing DOWN the screen like the level
 * text.
 *
 * Depth is in 1/32 m along the view direction, smaller = nearer: -21.17 per
 * cell down the screen, -24 per metre up, offset so the level stays
 * positive. On the floor plane it depends only on the LP row.
 *
 * The static layer (floor, walls, furniture, targets, puddles, plates,
 * holes, and their shadows) is drawn into a toroidal cache of 512 x 576 LP
 * pixels, colour + depth, in blocks of 64 x 64, as in Monster Hop. Things,
 * Mila, gates and flaps move: they are sprites depth-tested against it.
 */
#pragma once

#include "ml_art.h"
#include "ml_gfx.h"
#include "ml_level.h"

#include <stdbool.h>
#include <stdint.h>

#define ML_CW           512         /* the cache, LP pixels                   */
#define ML_CH           576
#define ML_CB           64          /* its blocks                             */
#define ML_CNX          (ML_CW / ML_CB)
#define ML_CNY          (ML_CH / ML_CB)
#define ML_DFAR         0x7FFF      /* the depth of nothing                   */
#define ML_DPLANE_PX    (-0.39196f) /* depth per LP row on a horizontal plane */

#define ML_CELL_W       72
#define ML_CELL_H       54
#define ML_ZPX          47.623f     /* LP px per metre up                     */
#define ML_FLOOR_M      0.50395f    /* one floor: a wall's height             */
#define ML_DGY          (-21.1660f) /* depth per cell down the screen         */
#define ML_DZ           (-24.0f)    /* depth per metre up                     */

#define ML_MAX_PROPS    16
#define ML_MAX_PROPS2   4

typedef struct {
    const ml_level_t *lv;
    const ml_state_t *st;           /* the live state: plates, filled holes  */
    int ox, oy;                     /* LP of the grid's top-left corner      */
    int lw, lh;                     /* LP size of the whole level picture    */
    int dofs;
    float dplane;                   /* depth per LP row on the floor plane   */
    uint16_t *cc, *cd;              /* the cache: colour, depth              */
    int32_t  tag[ML_CNY][ML_CNX];   /* which LP block each slot holds, -1    */
    /* the kit */
    ml_anim_t floor, wall, wall_sh, target, target_gl, wet, plate_up, plate_down, hole, hole_fill;
    ml_anim_t prop[ML_MAX_PROPS], prop_sh[ML_MAX_PROPS], prop_gl[ML_MAX_PROPS];
    ml_anim_t prop2[ML_MAX_PROPS2], prop2_sh[ML_MAX_PROPS2];
    int nprop, nprop2;
    uint8_t  pressed[ML_CELLS];     /* plates as drawn in the cache          */
    uint16_t filled;                /* holes as drawn in the cache           */
    int blocks_drawn;
} ml_world_t;

/* the kit's names of props (n1, n2) without loading anything: the level
 * needs them to be furnished before the world is built */
void ml_kit_counts(const char *kit, int *n1, int *n2);
/* loads the kit and allocates the cache; false without memory */
bool ml_world_init(ml_world_t *w, const ml_level_t *lv, const ml_state_t *st, const char *kit);
void ml_world_free(ml_world_t *w);

static inline float ml_lpx(const ml_world_t *w, float gx) { return (float)w->ox + ML_CELL_W * gx; }
static inline float ml_lpy(const ml_world_t *w, float gy, float z) { return (float)w->oy + ML_CELL_H * gy - ML_ZPX * z; }
static inline int ml_depth(const ml_world_t *w, float gy, float z)
{
    return ml_iround(ML_DGY * gy + ML_DZ * z) + w->dofs;
}

/* a cache holding one picture (the casita's room) at LP = screen, for
 * ml_render_band with the camera at (0, 0); dplane for its zoom */
bool ml_world_backdrop(ml_world_t *w, const ml_spr_t *s, int x, int y, int d, float dplane);

void ml_world_invalidate_all(ml_world_t *w);
void ml_world_invalidate_cell(ml_world_t *w, int cell);
/* after a step: plates and holes that changed are redrawn */
void ml_world_sync(ml_world_t *w);
/* makes the blocks under the view valid, then up to `extra` more ahead */
int  ml_world_prepare(ml_world_t *w, int cam_x, int cam_y, int dirx, int diry, int extra);

/* ---- the whole level, scaled to fit (the overview) ---- */

/* one sprite of the overview's painter list */
typedef struct {
    const ml_spr_t *s;
    const ml_lut_t *lut;            /* LID sprites                            */
    float    gx, gy, z;             /* where its anchor is                    */
    int16_t  d;                     /* for the order                          */
    uint8_t  fmt;
    uint8_t  layer;                 /* 0 ground, 1 flat on it, 2 shadows, 3 standing */
} ml_ov_item_t;

typedef struct {
    float scale;                    /* screen px per LP px                    */
    float ox, oy;                   /* screen position of LP (0, 0)           */
} ml_ov_view_t;

/* the scale and placement that fit the level in w x h (with margins) */
void ml_overview_fit(const ml_world_t *w, int sw, int sh, int top, int bottom, ml_ov_view_t *v);
/* the zoom from the overview: rows y0..y1 of the screen showing the LP
 * point (cx, cy) in the middle at sc screen px per LP px, sampled from the
 * overview picture (bilinear) and laid over what im holds with alpha a */
void ml_zoom_band(const uint16_t *ov, const ml_ov_view_t *v, ml_img_t *im, int y0, int y1, float cx, float cy,
                  float sc, int a);
/* paints the static layer plus `extra` (the moving things) into dst
 * (ML_W x ML_H, native RGB565), black around */
void ml_overview_paint(const ml_world_t *w, const ml_ov_view_t *v, uint16_t *dst,
                       const ml_ov_item_t *extra, int nextra);
