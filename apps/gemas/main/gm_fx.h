/*
 * GEMAS - effects
 *
 * Sparks, waves, beams, flying labels and the screen flash. They are all LVGL
 * objects from a fixed pool that is reused: nothing is created or destroyed
 * while playing, which is what stops the rhythm breaking.
 *
 * It is drawn with objects and not with a canvas on purpose: LVGL only
 * repaints what gets dirtied, so twenty eight-pixel sparks cost twenty little
 * rectangles and not a whole screen per frame.
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#define GM_FX_PARTS     64
#define GM_FX_RINGS     6
#define GM_FX_BEAMS     4
#define GM_FX_TEXTS     5

typedef struct {
    lv_obj_t *obj;
    int16_t   x, y, vx, vy;     /* in 1/16 of a pixel */
    uint8_t   life, life0;
    uint8_t   size;
    uint8_t   grav;
} gm_part_t;

typedef struct {
    lv_obj_t *obj;
    uint8_t   life, life0;
    int16_t   cx, cy;
    int16_t   r0, r1;
    uint8_t   width;
} gm_ring_t;

typedef struct {
    lv_obj_t *obj;
    uint8_t   life, life0;
} gm_beam_t;

typedef struct {
    lv_obj_t *obj;
    int16_t   x, y;
    int16_t   vy;               /* 1/16 of a pixel */
    uint8_t   life, life0;
} gm_text_t;

typedef struct {
    lv_obj_t  *parent;
    gm_part_t  part[GM_FX_PARTS];
    gm_ring_t  ring[GM_FX_RINGS];
    gm_beam_t  beam[GM_FX_BEAMS];
    gm_text_t  text[GM_FX_TEXTS];
    lv_obj_t  *flash;
    uint8_t    flash_life, flash_life0;
    uint8_t    flash_opa;
    int8_t     shake;           /* frames of shake remaining */
} gm_fx_t;

void gm_fx_init(gm_fx_t *fx, lv_obj_t *parent);
void gm_fx_step(gm_fx_t *fx);
void gm_fx_clear(gm_fx_t *fx);

void gm_fx_burst(gm_fx_t *fx, int x, int y, uint32_t color, int count,
                 int speed, bool gravity);
void gm_fx_ring(gm_fx_t *fx, int x, int y, uint32_t color, int r0, int r1,
                int frames, int width);
void gm_fx_beam(gm_fx_t *fx, int x, int y, bool horizontal, uint32_t color,
                int frames);
void gm_fx_text(gm_fx_t *fx, int x, int y, const char *txt, uint32_t color,
                bool big);
void gm_fx_flash(gm_fx_t *fx, int opa, int frames);
void gm_fx_shake(gm_fx_t *fx, int frames);
