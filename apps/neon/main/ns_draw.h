/*
 * NEON SNAKES - the compositor
 *
 * The screen is one 368x448 RGB565 buffer shown 1:1 (no upscaling: the
 * sprites are drawn at the cell size of each mode) and a second one, 'bg',
 * with the arena's frame on black. Nothing is ever redrawn whole during
 * play. A CELL is the unit of repainting: restore it from bg, then blit into
 * it, clipped, the sprite of every cell of its 3x3 neighbourhood - a sprite
 * is 2x2 cells centred on its own, so nothing further can reach in - taking
 * the brighter channel, plus any effect centred on those nine cells.
 *
 * What gets repainted in a frame is the 3x3 block around every cell the
 * engine marked (a head that moved, a tail that left, a fruit that appeared),
 * around every fruit when the pulse changes level, and around every effect
 * while it lives and on the frame it ends. The repainted cells are then
 * gathered into rows and rectangles for LVGL to flush, so what reaches the
 * panel is a few small patches per step.
 *
 * The rule that keeps it exact (tools/ns_harness.c checks it against a full
 * repaint every frame): a cell's pixels depend only on the state of its
 * neighbourhood, never on what was in the buffer.
 *
 * The grid has a ring of margin cells around the arena (x = -1..cols,
 * y = -1..rows) that hold nothing but receive glow; they are repainted like
 * any other, and the frame lives in them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ns_art.h"
#include "ns_game.h"

#define NS_SCREEN_W     368
#define NS_SCREEN_H     448
#define NS_EXT_COLS     (NS_MAX_COLS + 2)
#define NS_EXT_ROWS     (NS_MAX_ROWS + 2)
#define NS_MAX_FX       12
#define NS_MAX_RECTS    24

typedef struct {
    int16_t x0, y0, x1, y1;         /* px, x1/y1 exclusive */
} ns_rect_t;

enum { NS_FX_RING = 1, NS_FX_BURST };

typedef struct {
    uint8_t  type;
    int8_t   x, y;                  /* the cell it is centred on */
    int8_t   t, len;                /* frame (-1 = not shown yet), length */
    uint32_t rgb;
} ns_fx_t;

typedef struct {
    uint16_t *fb, *bg;
    int       cell, ox, oy, cols, rows;
    const ns_art_t *art;

    uint8_t   rep[NS_EXT_COLS * NS_EXT_ROWS];
    bool      rep_any;
    uint8_t   pulse;                /* 0..3: the fruits' breathing phase    */

    ns_fx_t   fx[NS_MAX_FX];
    uint8_t   nfx;

    /* the ring that says "this one is you" for the first seconds */
    int8_t    me;                   /* snake index, -1 for none             */
    uint8_t   halo_t;               /* frames left                          */
    int8_t    halo_x, halo_y;       /* where it was drawn last              */

    ns_rect_t rects[NS_MAX_RECTS];
    uint8_t   nrects;
    uint32_t  pixels;               /* pushed in the last frame, for the log*/
} ns_view_t;

/* Lays out the arena for the game's grid on the two buffers and draws the
 * frame into bg. */
void ns_view_init(ns_view_t *v, uint16_t *fb, uint16_t *bg, const ns_art_t *art,
                  const ns_game_t *g);

/* A burst of colour where something was eaten or died. */
void ns_view_fx(ns_view_t *v, uint8_t type, int x, int y, uint32_t rgb);

/* Show the "you" ring around snake 'me' for 'frames' frames. */
void ns_view_halo(ns_view_t *v, int me, int frames);

/* One frame: consume the engine's marks, advance effects and the pulse
 * (pulse = 0..3), repaint, and leave in v->rects what to invalidate. */
void ns_view_frame(ns_view_t *v, ns_game_t *g, uint8_t pulse);

/* Repaint every cell (and the whole screen goes in v->rects). */
void ns_view_full(ns_view_t *v, ns_game_t *g);
