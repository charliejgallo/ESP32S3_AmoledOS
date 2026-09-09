/*
 * CHATARRA - pixel art engine with dirty rectangles
 *
 * Copied from apps/cjump/main/cj_pixel.c, which in turn came from arkanos. It
 * is code proven on the board and there is no point rewriting it. What differs
 * from those two:
 *
 *   - the palette is expanded from a table in .rodata into an array, instead
 *     of being a forty-branch switch. Here .text is the scarce resource (48 KB
 *     of reservation for ALL loaded apps) and .rodata is free: the loader
 *     sends it to PSRAM. This app's whole design rule comes out of that
 *     asymmetry.
 *   - the palette also brings earth, grass, water, stone and wood, which is
 *     what an RPG map needs and a jumping game did not.
 *
 * Why it works for an RPG, which is not obvious: here the camera does NOT
 * move. The world is split into one-screen rooms and crossing an edge changes
 * room. With that the background stands still except for one expensive frame
 * per room change, and the only things repainted per frame are the player's
 * robot and the few creatures that move. It is exactly arkanos's case.
 *
 * The alternative -a camera following the character over a continuous map-
 * moves the whole background on every frame: 165 thousand pixels of upscaling
 * plus 165 thousand of drawing, that is, the 15 fps measured in 2043. That is
 * why it was discarded.
 *
 * The two buffers and the dirty list:
 *
 *   bg    the room as drawn: floor, walls, trees, closed chests.
 *   fb    the frame on screen. It starts as a copy of bg.
 *
 * And per frame:
 *
 *   1. restore from bg into fb the rectangles we dirtied last frame
 *   2. draw what moves, recording each rectangle
 *   3. upscale into big and invalidate ONLY the union of the two sets
 *
 * Everything that moves records ONE rectangle enclosing its old position and
 * its new one. Anything that moves and does not record its rectangle leaves a
 * trail stuck on the screen: nothing crashes, it just looks dirty.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CH_SCALE        2
#define CH_W            184
#define CH_H            224

/* --------------------------------------------------------------------------
 * Buffer
 *
 * It carries its own clip. That is not a luxury: the HUD is drawn separately
 * and only repainted when a number changes, so a particle escaping upwards
 * would leave rubbish there until the next score change. With the clip set to
 * the playing field, that cannot happen.
 * -------------------------------------------------------------------------- */
typedef struct {
    uint16_t *px;
    int16_t   w, h;
    int16_t   cx0, cy0, cx1, cy1;   /* clip; x1/y1 exclusive */
} ch_buf_t;

void ch_buf_init(ch_buf_t *b, uint16_t *px, int w, int h);
void ch_clip(ch_buf_t *b, int x0, int y0, int x1, int y1);
void ch_clip_none(ch_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t ch_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t ch_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t ch_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define CH_MAX_DIRTY    24

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} ch_rect_t;

typedef struct {
    ch_rect_t r[CH_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} ch_dirty_t;

void ch_dirty_reset(ch_dirty_t *d);
void ch_dirty_all(ch_dirty_t *d);
/* Adds a rectangle. If it is worth it, it merges it with one already there:
 * keeping twenty separate rectangles costs more than one slightly larger. */
void ch_dirty_add(ch_dirty_t *d, int x, int y, int w, int h);
void ch_dirty_join(ch_dirty_t *dst, const ch_dirty_t *src);
/* How many pixels the rectangles add up to: useful for measuring the saving. */
int  ch_dirty_area(const ch_dirty_t *d);

/* Copies from one buffer to another of the same size, only inside the rectangle. */
void ch_restore(uint16_t *dst, const uint16_t *src, const ch_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (CH_W x CH_H) onto dst (twice the size). */
void ch_expand(const uint16_t *src, uint16_t *dst, const ch_rect_t *r);
/* The same for any buffer and any scale, the whole thing. Used by the shop's
 * preview, which is a small buffer upscaled x4. */
void ch_expand_n(const uint16_t *src, int sw, int sh, uint16_t *dst, int scale);

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void ch_px(ch_buf_t *b, int x, int y, uint16_t c);
void ch_fill(ch_buf_t *b, uint16_t c);
void ch_rect(ch_buf_t *b, int x, int y, int w, int h, uint16_t c);
void ch_frame(ch_buf_t *b, int x, int y, int w, int h, uint16_t c);
void ch_hline(ch_buf_t *b, int x, int y, int len, uint16_t c);
void ch_vline(ch_buf_t *b, int x, int y, int len, uint16_t c);
void ch_line(ch_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void ch_disc(ch_buf_t *b, int cx, int cy, int r, uint16_t c);
void ch_ring(ch_buf_t *b, int cx, int cy, int r, uint16_t c);
/* rectangle with bitten corners */
void ch_round(ch_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* vertical gradient between two colours, from row y0 to y1 inclusive */
void ch_vgrad(ch_buf_t *b, int x, int y0, int w, int y1, uint16_t top, uint16_t bot);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void ch_shade(ch_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there: halos, glints and ripples */
void ch_glow(ch_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void ch_wave(ch_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry
 *
 * Angles in brads (256 per turn), result in 1/256. With this there is no need
 * for libm, which in a dynamic app is paid for symbol by symbol.
 * -------------------------------------------------------------------------- */
int ch_sin(int brad);
int ch_cos(int brad);
int ch_isqrt(int v);

/* --------------------------------------------------------------------------
 * ASCII sprites and text
 * -------------------------------------------------------------------------- */

#define CH_SPRITE(a)    (a), (int)(sizeof(a) / sizeof((a)[0]))

void     ch_pal_init(void);     /* once, before drawing anything */
bool     ch_pal(char ch, uint16_t *out);
void     ch_blit(ch_buf_t *b, int x, int y, const char *const *rows, int nrows);
void     ch_blit_c(ch_buf_t *b, int cx, int cy, const char *const *rows, int nrows);
int      ch_sprite_w(const char *const *rows);

#define CH_FW         5
#define CH_FH         7
#define CH_FADV       6

int  ch_text_w(const char *s);
void ch_text(ch_buf_t *b, int x, int y, const char *s, uint16_t c);
void ch_text_sh(ch_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh);
void ch_text_center(ch_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *ch_num(char *dst, uint32_t v, int min_digits);
