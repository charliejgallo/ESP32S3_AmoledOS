/*
 * ARKANOS - pixel art engine with dirty rectangles
 *
 * The game is drawn into a 184x224 RGB565 buffer that is then upscaled x2 to
 * the screen's 368x448 (184*2 = 368, 224*2 = 448, exact). The upscaling is
 * done by the app, NEVER by LVGL: leaving it to LVGL with
 * LV_IMAGE_ALIGN_STRETCH costs a measured 129 ms per frame on the board,
 * because lv_draw_sw_transform invents an alpha plane for the RGB565 source
 * and composites with blending instead of copying. That is already settled in
 * 2043 and in claudito.
 *
 * What is new in this app is the second half of the problem. In 2043 the whole
 * background moves, so the entire screen has to be upscaled and invalidated on
 * every frame: 165 thousand pixels of upscaling plus 165 thousand of LVGL
 * drawing. In an Arkanoid, by contrast, almost nothing moves: the ball, the
 * paddle, the odd falling capsule and the pieces of the brick that has just
 * broken. Everything else -background, walls, whole bricks- is identical to
 * the previous frame.
 *
 * Hence the two buffers and the dirty list:
 *
 *   bg    background + walls + bricks. It is rebuilt only when a brick changes.
 *   fb    the frame on screen. It starts as a copy of bg.
 *
 * And per frame:
 *
 *   1. restore from bg into fb the rectangles we dirtied last frame
 *   2. draw what moves, recording each rectangle
 *   3. upscale into big and invalidate ONLY the union of the two sets
 *
 * With that the work per frame goes from "the screen" to "a few thousand
 * pixels", and LVGL, which already redraws by invalid areas, does the same.
 *
 * A design consequence worth bearing in mind: any effect touching the whole
 * screen (a shake moving the canvas, a global flash, an animated background)
 * throws all of this away. That is why there is no screen shake here and the
 * background is fixed; hits are felt through local waves and flashes, which are
 * a few more rectangles.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define AK_SCALE        2
#define AK_W            184
#define AK_H            224

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
} ak_buf_t;

void ak_buf_init(ak_buf_t *b, uint16_t *px, int w, int h);
void ak_clip(ak_buf_t *b, int x0, int y0, int x1, int y1);
void ak_clip_none(ak_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t ak_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t ak_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t ak_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define AK_MAX_DIRTY    18

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} ak_rect_t;

typedef struct {
    ak_rect_t r[AK_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} ak_dirty_t;

void ak_dirty_reset(ak_dirty_t *d);
void ak_dirty_all(ak_dirty_t *d);
/* Adds a rectangle. If it is worth it, it merges it with one already there:
 * keeping twenty separate rectangles costs more than one slightly larger. */
void ak_dirty_add(ak_dirty_t *d, int x, int y, int w, int h);
void ak_dirty_join(ak_dirty_t *dst, const ak_dirty_t *src);
/* How many pixels the rectangles add up to: useful for measuring the saving. */
int  ak_dirty_area(const ak_dirty_t *d);

/* Copies from one buffer to another of the same size, only inside the
 * rectangle. */
void ak_restore(uint16_t *dst, const uint16_t *src, const ak_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (AK_W x AK_H) onto dst (twice the
 * size). */
void ak_expand(const uint16_t *src, uint16_t *dst, const ak_rect_t *r);

/* --------------------------------------------------------------------------
 * Primitives
 * -------------------------------------------------------------------------- */

void ak_px(ak_buf_t *b, int x, int y, uint16_t c);
void ak_fill(ak_buf_t *b, uint16_t c);
void ak_rect(ak_buf_t *b, int x, int y, int w, int h, uint16_t c);
void ak_frame(ak_buf_t *b, int x, int y, int w, int h, uint16_t c);
void ak_hline(ak_buf_t *b, int x, int y, int len, uint16_t c);
void ak_vline(ak_buf_t *b, int x, int y, int len, uint16_t c);
void ak_line(ak_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void ak_disc(ak_buf_t *b, int cx, int cy, int r, uint16_t c);
void ak_ring(ak_buf_t *b, int cx, int cy, int r, uint16_t c);
/* rectangle with bitten corners */
void ak_round(ak_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* vertical gradient between two colours, from row y0 to y1 inclusive */
void ak_vgrad(ak_buf_t *b, int x, int y0, int w, int y1, uint16_t top, uint16_t bot);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void ak_shade(ak_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there: halos, glints and ripples */
void ak_glow(ak_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void ak_wave(ak_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry
 *
 * Angles in brads (256 per turn), result in 1/256. With this there is no need
 * for libm, which in a dynamic app is paid for symbol by symbol.
 * -------------------------------------------------------------------------- */
int ak_sin(int brad);
int ak_cos(int brad);
int ak_isqrt(int v);

/* --------------------------------------------------------------------------
 * ASCII sprites and text
 * -------------------------------------------------------------------------- */

#define AK_SPRITE(a)    (a), (int)(sizeof(a) / sizeof((a)[0]))

bool     ak_pal(char ch, uint16_t *out);
void     ak_blit(ak_buf_t *b, int x, int y, const char *const *rows, int nrows);
void     ak_blit_c(ak_buf_t *b, int cx, int cy, const char *const *rows, int nrows);
int      ak_sprite_w(const char *const *rows);

#define AK_CH_W         5
#define AK_CH_H         7
#define AK_CH_ADV       6

int  ak_text_w(const char *s);
void ak_text(ak_buf_t *b, int x, int y, const char *s, uint16_t c);
void ak_text_sh(ak_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh);
void ak_text_center(ak_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *ak_num(char *dst, uint32_t v, int min_digits);
