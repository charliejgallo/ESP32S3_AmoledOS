/*
 * CLAUDE JUMP - pixel art engine with dirty rectangles
 *
 * Copied from apps/arkanos/main/ak_pixel.c with the names changed. It is code
 * proven on the board and there is no point rewriting it; what follows
 * explains why it also works for a game that scrolls, which is not obvious.
 *
 * The game is drawn into a 184x224 RGB565 buffer that is then upscaled x2 to
 * the screen's 368x448 (184*2 = 368, 224*2 = 448, exact). The upscaling is
 * done by the app, NEVER by LVGL: leaving it to LVGL with
 * LV_IMAGE_ALIGN_STRETCH costs a measured 129 ms per frame on the board,
 * because lv_draw_sw_transform invents an alpha plane for the RGB565 source
 * and composites with blending instead of copying. That is already settled in
 * 2043, in claudito and in arkanos.
 *
 * The second half of the problem is how much gets repainted. In 2043 the whole
 * background moves, so the entire screen has to be upscaled and invalidated on
 * every frame: 165 thousand pixels of upscaling plus 165 thousand of LVGL
 * drawing, that is, 15 fps.
 *
 * A jumping game is vertical and scrolls, so it looks like 2043's case and not
 * arkanos's. The way out is in the game's design: THE BACKGROUND DOES NOT
 * SCROLL. The sky, the clouds and the stars are fixed in screen coordinates
 * -it is what the original Doodle Jump does with its squared paper- and the
 * only things that move as the camera climbs are the platforms, the critter
 * and the objects. With that the work per frame is a handful of small
 * rectangles and not the whole screen.
 *
 * Hence the two buffers and the dirty list:
 *
 *   bg    the sky and its decorations. It is rebuilt only on changing zone.
 *   fb    the frame on screen. It starts as a copy of bg.
 *
 * And per frame:
 *
 *   1. restore from bg into fb the rectangles we dirtied last frame
 *   2. draw what moves, recording each rectangle
 *   3. upscale into big and invalidate ONLY the union of the two sets
 *
 * Everything that moves records ONE rectangle enclosing its old position and
 * its new one, so a platform descending 9 px with the scroll costs 30x15 and
 * not two separate rectangles.
 *
 * A design consequence worth bearing in mind: any effect touching the whole
 * screen (a shake moving the canvas, a global flash, an animated background, a
 * scrolling sky) throws all of this away. That is why the zone change -which
 * does repaint the whole background- is one expensive frame every few hundred
 * metres and not something continuous.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CJ_SCALE        2
#define CJ_W            184
#define CJ_H            224

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
} cj_buf_t;

void cj_buf_init(cj_buf_t *b, uint16_t *px, int w, int h);
void cj_clip(cj_buf_t *b, int x0, int y0, int x1, int y1);
void cj_clip_none(cj_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t cj_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t cj_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t cj_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define CJ_MAX_DIRTY    18

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} cj_rect_t;

typedef struct {
    cj_rect_t r[CJ_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} cj_dirty_t;

void cj_dirty_reset(cj_dirty_t *d);
void cj_dirty_all(cj_dirty_t *d);
/* Adds a rectangle. If it is worth it, it merges it with one already there:
 * keeping twenty separate rectangles costs more than one slightly larger. */
void cj_dirty_add(cj_dirty_t *d, int x, int y, int w, int h);
void cj_dirty_join(cj_dirty_t *dst, const cj_dirty_t *src);
/* How many pixels the rectangles add up to: useful for measuring the saving. */
int  cj_dirty_area(const cj_dirty_t *d);

/* Copies from one buffer to another of the same size, only inside the
 * rectangle. */
void cj_restore(uint16_t *dst, const uint16_t *src, const cj_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (CJ_W x CJ_H) onto dst (twice the
 * size). */
void cj_expand(const uint16_t *src, uint16_t *dst, const cj_rect_t *r);
/* The same for any buffer and any scale, the whole thing. Used by the shop's
 * preview, which is a small buffer upscaled x4. */
void cj_expand_n(const uint16_t *src, int sw, int sh, uint16_t *dst, int scale);

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void cj_px(cj_buf_t *b, int x, int y, uint16_t c);
void cj_fill(cj_buf_t *b, uint16_t c);
void cj_rect(cj_buf_t *b, int x, int y, int w, int h, uint16_t c);
void cj_frame(cj_buf_t *b, int x, int y, int w, int h, uint16_t c);
void cj_hline(cj_buf_t *b, int x, int y, int len, uint16_t c);
void cj_vline(cj_buf_t *b, int x, int y, int len, uint16_t c);
void cj_line(cj_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void cj_disc(cj_buf_t *b, int cx, int cy, int r, uint16_t c);
void cj_ring(cj_buf_t *b, int cx, int cy, int r, uint16_t c);
/* rectangle with bitten corners */
void cj_round(cj_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* vertical gradient between two colours, from row y0 to y1 inclusive */
void cj_vgrad(cj_buf_t *b, int x, int y0, int w, int y1, uint16_t top, uint16_t bot);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void cj_shade(cj_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there: halos, glints and ripples */
void cj_glow(cj_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void cj_wave(cj_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry
 *
 * Angles in brads (256 per turn), result in 1/256. With this there is no need
 * for libm, which in a dynamic app is paid for symbol by symbol.
 * -------------------------------------------------------------------------- */
int cj_sin(int brad);
int cj_cos(int brad);
int cj_isqrt(int v);

/* --------------------------------------------------------------------------
 * ASCII sprites and text
 * -------------------------------------------------------------------------- */

#define CJ_SPRITE(a)    (a), (int)(sizeof(a) / sizeof((a)[0]))

bool     cj_pal(char ch, uint16_t *out);
void     cj_blit(cj_buf_t *b, int x, int y, const char *const *rows, int nrows);
void     cj_blit_c(cj_buf_t *b, int cx, int cy, const char *const *rows, int nrows);
int      cj_sprite_w(const char *const *rows);

#define CJ_CH_W         5
#define CJ_CH_H         7
#define CJ_CH_ADV       6

int  cj_text_w(const char *s);
void cj_text(cj_buf_t *b, int x, int y, const char *s, uint16_t c);
void cj_text_sh(cj_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh);
void cj_text_center(cj_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *cj_num(char *dst, uint32_t v, int min_digits);
