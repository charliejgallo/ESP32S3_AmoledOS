/*
 * BURBUJAS - the bubbles
 *
 * There is ONE sphere in this app, 22x22, worked out once when the app opens:
 * a map of which tone each pixel is (outline, shadow, base, light, glint),
 * lit from the upper left. A colour is five tones derived from one hex value,
 * so the six colours, the bomb and the greyed-out board are the same drawing
 * with a different palette - and changing a colour is changing one number.
 *
 * Topos rendered its sprites into buffers; here the map IS the sprite, so
 * drawing a bubble is a table lookup per pixel and there is nothing to
 * allocate or free.
 */
#pragma once

#include "bb_pixel.h"

#include <stdbool.h>
#include <stdint.h>

/* The tone map and the palettes. Cheap, but it is where the colours are
 * decided, so it has the same shape as Topos's art init. */
bool bb_art_init(void);
void bb_art_free(void);

/* How a bubble is drawn. NORMAL is the board and the launcher; FLASH is the
 * first frames of a burst; GHOST is the outline of where a shot would land. */
enum { BS_NORMAL = 0, BS_FLASH, BS_GHOST };

/* A bubble centred on (cx, cy): it covers cx-11..cx+10, cy-11..cy+10. A bomb
 * also has its fuse above, which is why the box is asked for and not assumed
 * (bb_bubble_box). 'phase' is the bomb's spark, 0..3. */
void bb_bubble(bb_buf_t *b, int cx, int cy, int color, int style, int phase);
void bb_bubble_box(int color, int cx, int cy, bb_rect_t *out);

/* A bursting bubble, frame 0..BB_POP_FRAMES-1. */
#define BB_POP_FRAMES   8
#define BB_POP_MS       30              /* per frame                          */
#define BB_POP_REACH    24              /* how far the ring and the debris get */
void bb_pop_draw(bb_buf_t *b, int cx, int cy, int color, int frame, int seed);

/* The bomb going off, frame 0..BB_BLAST_FRAMES-1. */
#define BB_BLAST_FRAMES 10
#define BB_BLAST_MS     30
#define BB_BLAST_REACH  46
void bb_blast_draw(bb_buf_t *b, int cx, int cy, int frame);

/* The palette, for everything that is not a sphere: the guide's dots, the
 * popups, the pipe. */
uint16_t bb_col_base(int color);
uint16_t bb_col_light(int color);
uint16_t bb_col_dark(int color);
