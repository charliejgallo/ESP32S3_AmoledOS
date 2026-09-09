/*
 * GEMAS - the jewels' art
 *
 * The jewels are not stored bitmaps: they are drawn in code when the app
 * opens, pixel by pixel, into RGB565A8 sprites that LVGL then paints like any
 * other image. It comes out cheaper than carrying bitmaps in the .so (a 44x44
 * sprite is 5.8 KB; seven jewels would be 40 KB of flash) and it also lets the
 * palette or the size be changed with a constant.
 *
 * The format is RGB565A8 (colour and alpha in separate planes, 3 bytes per
 * pixel) because it is the only one with an alpha channel that LVGL's drawer
 * blends without converting anything: the screen is RGB565 too.
 *
 * Each jewel is a convex polygon with facets. For each pixel, which edge the
 * ray from the centre falls on is looked up, and from that comes the relative
 * distance to the edge (the cut's "depth") and which facet is being looked at.
 * From those two things come the central table, the crown, the dark fillet at
 * the edge and the specular highlight.
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#define GM_CELL         44      /* side of the cell, in pixels */
#define GM_SPRITE       44      /* side of the sprite (the jewel fits in 40) */
#define GM_TYPES        7       /* jewel colours */

/* Types of special jewel (the ones formed by lining up 4 or more) */
typedef enum {
    GM_SP_NONE = 0,
    GM_SP_FLAME,        /* line up 4: it explodes in a 3x3          */
    GM_SP_STAR,         /* an L or T shape: it clears a row and a column */
    GM_SP_HYPER,        /* line up 5: it takes a whole colour        */
} gm_special_t;

typedef struct {
    lv_image_dsc_t dsc;
    uint8_t       *data;        /* RGB565 (2 bytes) per pixel + A8 plane */
} gm_sprite_t;

typedef struct {
    gm_sprite_t gem[GM_TYPES];  /* the ordinary jewels       */
    gm_sprite_t hyper;          /* the rainbow hypercube     */
    gm_sprite_t flame;          /* flame, drawn on top       */
    gm_sprite_t star;           /* four-pointed sparkle      */
    gm_sprite_t tile;           /* board background, 2x2 cells, opaque */
    bool        ready;
} gm_art_t;

bool gm_art_init(gm_art_t *art);
void gm_art_free(gm_art_t *art);

/* Representative colour of each jewel: used by the sparks and the labels. */
uint32_t gm_art_color(int type);
uint32_t gm_art_color_light(int type);
