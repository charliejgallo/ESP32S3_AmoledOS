/*
 * BURBUJAS - pixel art engine with dirty rectangles
 *
 * Copied from apps/topos/main/tp_pixel.[ch] (which comes from cjump, and that
 * one from arkanos, proven on the board) with the names changed and nothing
 * taken out. What matters here:
 *
 *   - Every primitive honours the clip, blending ones included. That is what
 *     lets the compositor (bb_draw.c) rebuild any rectangle from the
 *     background plus every object touching it and get the same pixels a full
 *     redraw would give.
 *   - Text at any integer scale and with an outline, for the score.
 *   - THE LIP -a floor per column under which nothing is drawn- is Topos's,
 *     for a mole coming out of a hole. Burbujas never sets one, and every
 *     primitive checks for it in one branch that is always false. It is left
 *     in so the two engines stay the same file; if a third app needs a fix
 *     here, it is one diff and not two.
 *
 * The game is drawn into a 184x224 RGB565 buffer that the app upscales x2 to
 * the 368x448 screen. The app upscales, never LVGL: LV_IMAGE_ALIGN_STRETCH
 * costs a measured 129 ms per frame on the board (see cj_pixel.h).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BB_SCALE        2
#define BB_W            184
#define BB_H            224

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
} bb_buf_t;

void bb_buf_init(bb_buf_t *b, uint16_t *px, int w, int h);
void bb_clip(bb_buf_t *b, int x0, int y0, int x1, int y1);
void bb_clip_none(bb_buf_t *b);
void bb_lip(bb_buf_t *b, const int16_t *tab, int x0, int n, int def);
void bb_lip_off(bb_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t bb_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t bb_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t bb_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define BB_MAX_DIRTY    18

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} bb_rect_t;

typedef struct {
    bb_rect_t r[BB_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} bb_dirty_t;

void bb_dirty_reset(bb_dirty_t *d);
void bb_dirty_all(bb_dirty_t *d);
/* Adds a rectangle, merging it with one already there when that is cheap. */
void bb_dirty_add(bb_dirty_t *d, int x, int y, int w, int h);
int  bb_dirty_area(const bb_dirty_t *d);

static inline bool bb_rect_hit(const bb_rect_t *a, const bb_rect_t *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* Copies from one BB_W x BB_H buffer to another, only inside the rectangle. */
void bb_restore(uint16_t *dst, const uint16_t *src, const bb_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (BB_W x BB_H) onto dst. */
void bb_expand(const uint16_t *src, uint16_t *dst, const bb_rect_t *r);

/* --------------------------------------------------------------------------
 * Primitives. All of them honour the clip and the lip.
 * -------------------------------------------------------------------------- */

void bb_px(bb_buf_t *b, int x, int y, uint16_t c);
/* blends c over what is there, f in sixteenths */
void bb_px_mix(bb_buf_t *b, int x, int y, uint16_t c, int f);
void bb_fill(bb_buf_t *b, uint16_t c);
void bb_rect(bb_buf_t *b, int x, int y, int w, int h, uint16_t c);
void bb_frame(bb_buf_t *b, int x, int y, int w, int h, uint16_t c);
void bb_hline(bb_buf_t *b, int x, int y, int len, uint16_t c);
void bb_vline(bb_buf_t *b, int x, int y, int len, uint16_t c);
void bb_line(bb_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void bb_disc(bb_buf_t *b, int cx, int cy, int r, uint16_t c);
void bb_ring(bb_buf_t *b, int cx, int cy, int r, uint16_t c);
/* filled axis-aligned ellipse */
void bb_ellipse(bb_buf_t *b, int cx, int cy, int rx, int ry, uint16_t c);
/* rectangle with bitten corners */
void bb_round(bb_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void bb_shade(bb_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there, fading towards its edge */
void bb_glow(bb_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* disc blended evenly: smoke, shadows */
void bb_disc_mix(bb_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void bb_wave(bb_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry. Angles in brads (256 per turn), result in 1/256.
 * -------------------------------------------------------------------------- */
int bb_sin(int brad);
int bb_cos(int brad);
int bb_isqrt(int v);

/* --------------------------------------------------------------------------
 * 5x7 font, upper case, digits and a few signs. ASCII only: NO TRANSLATABLE
 * TEXT GOES THROUGH HERE (see topos.h). Numbers and signs only.
 * -------------------------------------------------------------------------- */

#define BB_CH_W         5
#define BB_CH_H         7
#define BB_CH_ADV       6

int  bb_text_w(const char *s, int scale);
void bb_text(bb_buf_t *b, int x, int y, const char *s, uint16_t c, int scale);
/* with a one-pixel outline all round, in the buffer's pixels */
void bb_text_ol(bb_buf_t *b, int x, int y, const char *s,
                uint16_t fill, uint16_t outline, int scale);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *bb_num(char *dst, uint32_t v, int min_digits);
