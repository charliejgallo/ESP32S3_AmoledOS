/*
 * LX_PIXEL - the pixel engine the Lua scripts draw with
 *
 * Copied from apps/burbujas/main/bb_pixel.[ch] (which came from Topos, which
 * came from Claude Jump, which came from Arkanos, proven on the board) with
 * the prefix changed and nothing taken out. The prefix is what keeps the
 * simulator linking: it compiles every app's sources into one binary, so two
 * engines with the same names would collide.
 *
 * Here it has a second job it did not have in a game: every one of these
 * primitives is reachable from a script, so every one of them is a place
 * where a wrong argument could walk off the buffer. They already clip -that
 * is the point of the clip- and lx_api.c does not trust the script either.
 *
 * The lip (a floor per column, Topos's mole coming out of its hole) is left
 * in so the engines stay the same file; nothing here ever sets one.
 *
 * Scripts draw into 184x224 and the app upscales x2 to the 368x448 screen,
 * as 2043 and Burbujas do. Never LV_IMAGE_ALIGN_STRETCH: 129 ms a frame.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LX_SCALE        2
#define LX_W            184
#define LX_H            224

/* --------------------------------------------------------------------------
 * Buffer, clip and lip
 * -------------------------------------------------------------------------- */
typedef struct {
    uint16_t      *px;
    int16_t        w, h;
    int16_t        cx0, cy0, cx1, cy1;  /* clip; x1/y1 exclusive              */

    /* Lip. With lip != NULL a pixel of column x is drawn only when its row is
     * ABOVE lip[x - lip_x0] (exclusive); outside the table the floor is
     * lip_def. NULL means no lip. */
    const int16_t *lip;
    int16_t        lip_x0, lip_n, lip_def;
} lx_buf_t;

void lx_buf_init(lx_buf_t *b, uint16_t *px, int w, int h);
void lx_clip(lx_buf_t *b, int x0, int y0, int x1, int y1);
void lx_clip_none(lx_buf_t *b);
void lx_lip(lx_buf_t *b, const int16_t *tab, int x0, int n, int def);
void lx_lip_off(lx_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t lx_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t lx_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t lx_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define LX_MAX_DIRTY    18

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} lx_rect_t;

typedef struct {
    lx_rect_t r[LX_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} lx_dirty_t;

void lx_dirty_reset(lx_dirty_t *d);
void lx_dirty_all(lx_dirty_t *d);
/* Adds a rectangle, merging it with one already there when that is cheap. */
void lx_dirty_add(lx_dirty_t *d, int x, int y, int w, int h);
int  lx_dirty_area(const lx_dirty_t *d);

static inline bool lx_rect_hit(const lx_rect_t *a, const lx_rect_t *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* Copies from one LX_W x LX_H buffer to another, only inside the rectangle. */
void lx_restore(uint16_t *dst, const uint16_t *src, const lx_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (LX_W x LX_H) onto dst. */
void lx_expand(const uint16_t *src, uint16_t *dst, const lx_rect_t *r);

/* --------------------------------------------------------------------------
 * Primitives. All of them honour the clip and the lip.
 * -------------------------------------------------------------------------- */

void lx_px(lx_buf_t *b, int x, int y, uint16_t c);
/* blends c over what is there, f in sixteenths */
void lx_px_mix(lx_buf_t *b, int x, int y, uint16_t c, int f);
void lx_fill(lx_buf_t *b, uint16_t c);
void lx_rect(lx_buf_t *b, int x, int y, int w, int h, uint16_t c);
void lx_frame(lx_buf_t *b, int x, int y, int w, int h, uint16_t c);
void lx_hline(lx_buf_t *b, int x, int y, int len, uint16_t c);
void lx_vline(lx_buf_t *b, int x, int y, int len, uint16_t c);
void lx_line(lx_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void lx_disc(lx_buf_t *b, int cx, int cy, int r, uint16_t c);
void lx_ring(lx_buf_t *b, int cx, int cy, int r, uint16_t c);
/* filled axis-aligned ellipse */
void lx_ellipse(lx_buf_t *b, int cx, int cy, int rx, int ry, uint16_t c);
/* rectangle with bitten corners */
void lx_round(lx_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void lx_shade(lx_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there, fading towards its edge */
void lx_glow(lx_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* disc blended evenly: smoke, shadows */
void lx_disc_mix(lx_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void lx_wave(lx_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry. Angles in brads (256 per turn), result in 1/256.
 * -------------------------------------------------------------------------- */
int lx_sin(int brad);
int lx_cos(int brad);
int lx_isqrt(int v);

/* --------------------------------------------------------------------------
 * 5x7 font, upper case, digits and a few signs. ASCII only: NO TRANSLATABLE
 * TEXT GOES THROUGH HERE (see topos.h). Numbers and signs only.
 * -------------------------------------------------------------------------- */

#define LX_CH_W         5
#define LX_CH_H         7
#define LX_CH_ADV       6

int  lx_text_w(const char *s, int scale);
void lx_text(lx_buf_t *b, int x, int y, const char *s, uint16_t c, int scale);
/* with a one-pixel outline all round, in the buffer's pixels */
void lx_text_ol(lx_buf_t *b, int x, int y, const char *s,
                uint16_t fill, uint16_t outline, int scale);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *lx_num(char *dst, uint32_t v, int min_digits);
