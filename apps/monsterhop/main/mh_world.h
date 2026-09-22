/*
 * MONSTER HOP - the world: projection, the level's art, the background cache
 *
 * The projection is the one the art was rendered with (tools/blender/
 * mh_common.py): +1 m along X is (60, 14) px, +1 m along Y is (20, -42) px,
 * one floor (0.50923 m) is (0, -23) px. Positions on the "level plane" (LP)
 * are pixels of an imaginary picture of the whole level; the camera is the
 * LP pixel at the screen's top-left.
 *
 * Depth is in 1/32 m along the view direction, smaller = nearer, offset so
 * a whole level stays positive. On any horizontal plane the depth depends
 * only on the LP row (-0.5162 per pixel down): shadows and light pools on
 * the ground are tested against that, not against geometry.
 *
 * The background (blocks, water, bridges, props, their shadows and glows)
 * is drawn once into a toroidal cache of 512 x 576 LP pixels, colour +
 * depth, in blocks of 64 x 64 that are (re)drawn when they come into view
 * or when something in them changes. Every frame then starts as a copy of
 * the cache, and the moving things are depth-tested against it per pixel.
 */
#pragma once

#include "mh_art.h"
#include "mh_gfx.h"
#include "mh_level.h"

#include <stdbool.h>
#include <stdint.h>

#define MH_FLOOR_M      0.50923f
#define MH_CW           512         /* the cache, LP pixels                   */
#define MH_CH           576
#define MH_CB           64          /* its blocks                             */
#define MH_CNX          (MH_CW / MH_CB)
#define MH_CNY          (MH_CH / MH_CB)
#define MH_DFAR         0x7FFF      /* the depth of nothing                   */
#define MH_DPLANE_PX    (-0.51620f) /* depth per LP row on a horizontal plane */

/* depth units per metre along X, Y, Z (1/32 m along the view direction) */
#define MH_DX           (-7.22662f)
#define MH_DY           (21.67985f)
#define MH_DZ           (-22.40000f)

typedef struct {
    uint32_t void_top, void_bot;    /* the sky/abyss around the level       */
    uint32_t tint;                  /* characters' light (x/255 per channel) */
    uint32_t fog;                   /* distance tint of the void edges      */
    uint32_t water_glint;           /* the colour of the sparkles on water  */
} mh_zone_look_t;

const mh_zone_look_t *mh_zone_look(int zone);

typedef struct {
    const mh_level_t *lv;
    int ox, oy;                     /* LP of the world origin                */
    int lw, lh;                     /* LP size of the whole level            */
    int dofs;                       /* depth offset                          */
    uint16_t *cc, *cd;              /* the cache: colour, depth (MH_CW x MH_CH) */
    int32_t  tag[MH_CNY][MH_CNX];   /* which LP block each slot holds, -1   */
    mh_anim_t art[MH_LV_MAXASSET];  /* per level asset: the sheet           */
    mh_anim_t sh[MH_LV_MAXASSET];   /* its shadow, if any                   */
    mh_anim_t gl[MH_LV_MAXASSET];   /* its glow, if any                     */
    uint16_t void_row[MH_CH];       /* the void's colour per cache row      */
    int blocks_drawn;               /* statistics                           */
} mh_world_t;

/* the level's art is loaded here (all assets it names); false if the cache
 * does not fit (missing art is drawn as nothing) */
bool mh_world_init(mh_world_t *w, const mh_level_t *lv);
void mh_world_free(mh_world_t *w);

/* LP position of a world point (x, y in metres, z in metres) */
static inline float mh_lpx(const mh_world_t *w, float x, float y) { return 60.0f * x + 20.0f * y + (float)w->ox; }
static inline float mh_lpy(const mh_world_t *w, float x, float y, float z)
{
    return 14.0f * x - 42.0f * y - z * (23.0f / MH_FLOOR_M) + (float)w->oy;
}
static inline int mh_depth(const mh_world_t *w, float x, float y, float z)
{
    return mh_iround(MH_DX * x + MH_DY * y + MH_DZ * z) + w->dofs;
}
/* a cell's floor top in metres */
static inline float mh_floor_z(int floor) { return (float)floor * MH_FLOOR_M; }

/* forget cached blocks (all, or those a cell's art can reach) */
void mh_world_invalidate_all(mh_world_t *w);
void mh_world_invalidate_cell(mh_world_t *w, int x, int y);
/* makes the blocks under the view valid; then draws up to `extra` blocks
 * just outside it towards (dirx, diry). Returns the blocks it drew. */
int  mh_world_prepare(mh_world_t *w, int cam_x, int cam_y, int dirx, int diry, int extra);

/* the cache's pixel pointers for an LP position (wraps) */
static inline int mh_cache_idx(int px, int py)
{
    int cy = py % MH_CH;
    if (cy < 0) cy += MH_CH;
    return cy * MH_CW + (px & (MH_CW - 1));
}
