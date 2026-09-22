/*
 * GOLF - the course seen from above
 *
 * One renderer for every top-down picture of a hole, at any scale and
 * rotation: the whole hole to aim, the green close up to putt, and the
 * texture the 3D view samples (gf_view3d.c). It is vector all the way: the
 * polygons are rasterised with antialiased coverage at the scale asked for,
 * and every texture (mowing stripes, sand grain, the water's depth) is a
 * function of the WORLD position, so a zoom never shows a pixel of anything.
 *
 * Painting order, back to front: rough, forest floor, deep rough, waste,
 * first cut, fairway, tee, fringe, green, bunker, water, path; then the hill
 * light, the trees' shadows, the trees, and the markers.
 */
#pragma once

#include "gf_world.h"

#include <stdint.h>

typedef struct {
    float ox, oy;           /* world point shown at (scx, scy)               */
    float scx, scy;
    float ppm;              /* pixels per metre                              */
    float ang;              /* the world heading that points up, radians;
                               0 = +y (the hole's own direction)             */
    float fx, fy;           /* derived: world unit vector pointing up        */
    float rx, ry;           /*          and pointing right                   */
} gf_view_t;

void gf_view_set(gf_view_t *v, float ox, float oy, float scx, float scy, float ppm, float ang);
void gf_view_w2s(const gf_view_t *v, float x, float y, float *sx, float *sy);
void gf_view_s2w(const gf_view_t *v, float sx, float sy, float *x, float *y);

enum {
    MAP_SHADE   = 1u << 0,  /* the hills' light                              */
    MAP_TREES   = 1u << 1,  /* trees and their shadows                       */
    MAP_MARKERS = 1u << 2,  /* the flag and the tee markers                  */
    MAP_GRID    = 1u << 3,  /* the green's slope, as a grid of chevrons      */
};

/* The whole picture into out (w x h RGB565, row stride w). Dithered. */
void gf_map_render(const gf_world_t *wd, const gf_view_t *v, uint16_t *out, int w, int h, unsigned flags);

/* The texture for the 3D view: the ground's own colour (no hill light, no
 * trees), 'mpp' metres per texel, covering the height grid. Not dithered:
 * the 3D view lights and dithers it. */
void gf_map_albedo(const gf_world_t *wd, uint16_t *out, int w, int h, float mpp);

/* Tree sprites seen from above, when the app has them (gf_assets): kind ->
 * canopy and its shadow. NULL falls back to a drawn canopy. */
typedef struct gf_tree_art gf_tree_art_t;
void gf_map_set_tree_art(const gf_tree_art_t *art);

void gf_tex_init(void);                 /* the noise tile, once              */
float gf_tex(float x, float y);         /* tileable noise, about [-1, 1]     */
