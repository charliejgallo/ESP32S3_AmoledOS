/*
 * TOPOS - pixel art engine with dirty rectangles
 *
 * Copied from apps/cjump/main/cj_pixel.[ch] (which comes from arkanos, proven
 * on the board), with the names changed and three additions a whack-a-mole
 * needs:
 *
 *   - THE BUFFER CARRIES A LIP besides its clip: a floor per column under
 *     which nothing is drawn. It is how a mole comes out of a hole. The front
 *     edge of the opening is an arc, everything the occupant draws goes
 *     through it, and the mound painted in the background stays in front
 *     without drawing it twice.
 *   - Text at any integer scale and with an outline, for the score.
 *   - Every primitive honours the clip AND the lip, blending ones included.
 *     That is what lets the compositor (tp_draw.c) rebuild any rectangle from
 *     the background plus every object touching it and get the same pixels a
 *     full redraw would give.
 *
 * The game is drawn into a 184x224 RGB565 buffer that the app upscales x2 to
 * the 368x448 screen. The app upscales, never LVGL: LV_IMAGE_ALIGN_STRETCH
 * costs a measured 129 ms per frame on the board (see cj_pixel.h).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TP_SCALE        2
#define TP_W            184
#define TP_H            224

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
} tp_buf_t;

void tp_buf_init(tp_buf_t *b, uint16_t *px, int w, int h);
void tp_clip(tp_buf_t *b, int x0, int y0, int x1, int y1);
void tp_clip_none(tp_buf_t *b);
void tp_lip(tp_buf_t *b, const int16_t *tab, int x0, int n, int def);
void tp_lip_off(tp_buf_t *b);

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's format) */
static inline uint16_t tp_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Blends two RGB565s. 'f' goes from 0 (all a) to 16 (all b). */
uint16_t tp_mix(uint16_t a, uint16_t b, int f);
/* Darkens (f<0) or lightens (f>0) a colour, in sixteenths. */
uint16_t tp_tone(uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

#define TP_MAX_DIRTY    18

typedef struct {
    int16_t x0, y0, x1, y1;     /* x1/y1 exclusive */
} tp_rect_t;

typedef struct {
    tp_rect_t r[TP_MAX_DIRTY];
    uint8_t   n;
    bool      all;              /* the whole screen, no list */
} tp_dirty_t;

void tp_dirty_reset(tp_dirty_t *d);
void tp_dirty_all(tp_dirty_t *d);
/* Adds a rectangle, merging it with one already there when that is cheap. */
void tp_dirty_add(tp_dirty_t *d, int x, int y, int w, int h);
int  tp_dirty_area(const tp_dirty_t *d);

static inline bool tp_rect_hit(const tp_rect_t *a, const tp_rect_t *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* Copies from one TP_W x TP_H buffer to another, only inside the rectangle. */
void tp_restore(uint16_t *dst, const uint16_t *src, const tp_rect_t *r);
/* Upscales x2 the rectangle 'r' of src (TP_W x TP_H) onto dst. */
void tp_expand(const uint16_t *src, uint16_t *dst, const tp_rect_t *r);

/* --------------------------------------------------------------------------
 * Primitives. All of them honour the clip and the lip.
 * -------------------------------------------------------------------------- */

void tp_px(tp_buf_t *b, int x, int y, uint16_t c);
/* blends c over what is there, f in sixteenths */
void tp_px_mix(tp_buf_t *b, int x, int y, uint16_t c, int f);
void tp_fill(tp_buf_t *b, uint16_t c);
void tp_rect(tp_buf_t *b, int x, int y, int w, int h, uint16_t c);
void tp_frame(tp_buf_t *b, int x, int y, int w, int h, uint16_t c);
void tp_hline(tp_buf_t *b, int x, int y, int len, uint16_t c);
void tp_vline(tp_buf_t *b, int x, int y, int len, uint16_t c);
void tp_line(tp_buf_t *b, int x0, int y0, int x1, int y1, uint16_t c);
void tp_disc(tp_buf_t *b, int cx, int cy, int r, uint16_t c);
void tp_ring(tp_buf_t *b, int cx, int cy, int r, uint16_t c);
/* filled axis-aligned ellipse */
void tp_ellipse(tp_buf_t *b, int cx, int cy, int rx, int ry, uint16_t c);
/* rectangle with bitten corners */
void tp_round(tp_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* darkens (f<0) or lightens (f>0) an area, in sixteenths */
void tp_shade(tp_buf_t *b, int x, int y, int w, int h, int f);
/* disc blended with what is already there, fading towards its edge */
void tp_glow(tp_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* disc blended evenly: smoke, shadows */
void tp_disc_mix(tp_buf_t *b, int cx, int cy, int r, uint16_t c, int f);
/* thick blended ring: an explosion's wave */
void tp_wave(tp_buf_t *b, int cx, int cy, int r, int thick, uint16_t c, int f);

/* --------------------------------------------------------------------------
 * Integer trigonometry. Angles in brads (256 per turn), result in 1/256.
 * -------------------------------------------------------------------------- */
int tp_sin(int brad);
int tp_cos(int brad);
int tp_isqrt(int v);

/* --------------------------------------------------------------------------
 * 5x7 font, upper case, digits and a few signs. ASCII only: NO TRANSLATABLE
 * TEXT GOES THROUGH HERE (see topos.h). Numbers and signs only.
 * -------------------------------------------------------------------------- */

#define TP_CH_W         5
#define TP_CH_H         7
#define TP_CH_ADV       6

int  tp_text_w(const char *s, int scale);
void tp_text(tp_buf_t *b, int x, int y, const char *s, uint16_t c, int scale);
/* with a one-pixel outline all round, in the buffer's pixels */
void tp_text_ol(tp_buf_t *b, int x, int y, const char *s,
                uint16_t fill, uint16_t outline, int scale);

/* Integers to text without snprintf: the HUD is drawn often and newlib takes
 * several hundred bytes of stack, which here is the LVGL task's. */
char *tp_num(char *dst, uint32_t v, int min_digits);
