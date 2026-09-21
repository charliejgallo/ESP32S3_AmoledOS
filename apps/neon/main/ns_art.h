/*
 * NEON SNAKES - the sprites, drawn by code
 *
 * Nothing here is a bitmap. Every fruit and every part of a snake is a short
 * list of shapes (discs, capsules, cones, ellipses, arcs) in CELL units, and
 * one function turns a list into RGB565 at whatever size a cell has: 16 px in
 * the normal mode, 10 in combat, where the camera is further away. The same
 * drawing serves both, which a hand-drawn 16x16 would not.
 *
 * The look comes from the distance to each shape: inside, the colour going
 * to white at the core the way a neon tube does; at the edge, a
 * one-pixel antialiased rim; outside, a glow that fades over half a cell.
 * The background is black - every pixel of it, always - so the glow can be
 * baked into the sprite and the compositor can combine sprites by taking the
 * brighter of each channel, which is what light does anyway: a glow over a
 * glow looks right and costs no alpha.
 *
 * A sprite is 2x2 cells with its cell in the middle, so its glow reaches half
 * a cell into each neighbour and never further. That bound is what lets the
 * compositor repaint a cell from its 3x3 neighbourhood alone (ns_draw.c).
 */
#pragma once

#include <stdint.h>
#include "ns_game.h"

#define NS_COLOURS      5           /* four snakes + the white of the flash */
#define NS_WHITE        4
#define NS_PULSES       3           /* glow levels a fruit breathes through */

/* Snake sprite indices inside a colour: 6 body bends x 2 stripes, 4 tails x
 * 2 stripes, 4 heads. */
#define NS_SPR_BODY(shape, stripe)  ((stripe) * 6 + (shape))
#define NS_SPR_TAIL(dir, stripe)    (12 + (stripe) * 4 + (dir))
#define NS_SPR_HEAD(dir)            (20 + (dir))
#define NS_SNAKE_SPRITES            24

typedef struct {
    int       cell;                 /* C: pixels per cell                   */
    int       size;                 /* S = 2C: pixels per sprite side       */
    uint8_t   colours;              /* how many snake colours were drawn    */
    uint16_t *snake[NS_COLOURS][NS_SNAKE_SPRITES];
    uint16_t *fruit[NS_KIND_COUNT][NS_PULSES];
    uint16_t *block;                /* the one allocation behind them all   */
} ns_art_t;

/* Draws every sprite for a cell of 'cell' px: 'colours' snake colours (1 for
 * the normal mode) plus white, and all the fruits. false if out of memory. */
bool ns_art_build(ns_art_t *a, int cell, int colours);
void ns_art_free(ns_art_t *a);

/* The body shape for a pair of connections (bits: 1 up, 2 right, 4 down,
 * 8 left), -1 if the pair is not a body. */
int ns_body_shape(int mask);

/* Each snake's main colour, for effects drawn outside the sprites. */
uint32_t ns_snake_rgb(int colour);
uint32_t ns_fruit_rgb(int kind);

/* The title: "NEON" over "SNAKES" in tube letters, drawn straight into a
 * 368-wide RGB565 buffer, taking the brighter channel. */
void ns_art_title(uint16_t *buf, int stride, int h, int y_top);

/* RGB565 helpers shared with the compositor */
static inline uint16_t ns_565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 | ((rgb >> 10) & 0x3F) << 5 | ((rgb >> 3) & 0x1F));
}

static inline uint16_t ns_max565(uint16_t a, uint16_t b)
{
    uint16_t r = (a & 0xF800) > (b & 0xF800) ? (a & 0xF800) : (b & 0xF800);
    uint16_t g = (a & 0x07E0) > (b & 0x07E0) ? (a & 0x07E0) : (b & 0x07E0);
    uint16_t l = (a & 0x001F) > (b & 0x001F) ? (a & 0x001F) : (b & 0x001F);
    return (uint16_t)(r | g | l);
}
