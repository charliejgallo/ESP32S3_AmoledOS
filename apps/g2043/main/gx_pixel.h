/*
 * 2043 - pixel art engine
 *
 * The whole game is drawn into a single 184x224 RGB565 buffer that LVGL
 * stretches x2 with nearest-neighbour (lv_image_set_inner_align STRETCH +
 * antialiasing off). 184*2 = 368 and 224*2 = 448, that is, exactly the screen.
 *
 * Why 184x224 and not the native resolution: drawing 41 thousand pixels per
 * frame in software fits comfortably, 165 thousand does not. The cost of the
 * stretch is paid by LVGL, which does it by the same path Claudito already
 * uses.
 *
 * The buffer is opaque, with no alpha channel: it is painted back to front.
 * The sprites are written as ASCII art and '.' means "do not touch".
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define GX_SCALE        2
#define GX_W            184
#define GX_H            224

typedef struct {
    uint16_t *px;
    int16_t   w;
    int16_t   h;
} gx_buf_t;

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t gx_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t gx_mix(uint16_t a, uint16_t b, int f);

/* Palette of the ASCII sprites. false if the character is transparent. */
bool gx_pal(char ch, uint16_t *out);
uint16_t gx_pal_or(char ch, uint16_t fallback);

/* ---- integer trigonometry ------------------------------------------------
 * The angle goes in brads (0..255 is a full turn) and the result in 1/256.
 * With this there is no need for libm, which in a dynamic app has to be
 * exported symbol by symbol. */
int gx_sin(int brad);
int gx_cos(int brad);
/* integer square root, for distances */
int gx_isqrt(int v);
/* angle of a vector, in brads */
int gx_atan2(int y, int x);

/* ---- primitives ---------------------------------------------------------- */

void gx_px(gx_buf_t *b, int x, int y, uint16_t c);
void gx_fill(gx_buf_t *b, uint16_t c);
void gx_rect(gx_buf_t *b, int x, int y, int w, int h, uint16_t c);
void gx_frame(gx_buf_t *b, int x, int y, int w, int h, uint16_t c);
void gx_hline(gx_buf_t *b, int x, int y, int len, uint16_t c);
void gx_vline(gx_buf_t *b, int x, int y, int len, uint16_t c);
void gx_line(gx_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void gx_disc(gx_buf_t *b, int cx, int cy, int r, uint16_t c);
void gx_ring(gx_buf_t *b, int cx, int cy, int r, uint16_t c);
/* rectangle with bitten corners: the style's basic shape */
void gx_round(gx_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* vertical gradient between two colours, from row y0 to y1 inclusive */
void gx_vgrad(gx_buf_t *b, int y0, int y1, uint16_t top, uint16_t bot);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void gx_shade(gx_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there: halos, glints and shock waves */
void gx_glow(gx_buf_t *b, int cx, int cy, int r, uint16_t c, int f);

/* ---- ASCII sprites ------------------------------------------------------- */

#define GX_SPRITE(a)    (a), (int)(sizeof(a) / sizeof((a)[0]))

/* (x,y) is the top-left corner */
void gx_blit(gx_buf_t *b, int x, int y, const char *const *rows, int nrows);
/* the same but in a single colour: silhouettes, shadows and the hit flash */
void gx_blit_solid(gx_buf_t *b, int x, int y, const char *const *rows, int nrows,
                   uint16_t c);
/* centred on (cx,cy), which is how the entities' positions are stored */
void gx_blit_c(gx_buf_t *b, int cx, int cy, const char *const *rows, int nrows);
void gx_blit_c_solid(gx_buf_t *b, int cx, int cy, const char *const *rows,
                     int nrows, uint16_t c);
/* the same sprite but narrowed or widened: with this the player's barrel roll
 * and the respawn come out without drawing a frame per step */
void gx_blit_c_xscale(gx_buf_t *b, int cx, int cy, const char *const *rows,
                      int nrows, int dst_w);
/* mirrored vertically: the same sprites work facing downwards */
void gx_blit_c_flipv(gx_buf_t *b, int cx, int cy, const char *const *rows, int nrows);

int gx_sprite_w(const char *const *rows);

/* ---- text (our own 5x7 font, upper case only) ---------------------------- */

#define GX_CH_W         5
#define GX_CH_H         7
#define GX_CH_ADV       6

int  gx_text_w(const char *s);
void gx_text(gx_buf_t *b, int x, int y, const char *s, uint16_t c);
/* with a hard 1 px shadow: it reads over any background */
void gx_text_sh(gx_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh);
void gx_text_center(gx_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh);
