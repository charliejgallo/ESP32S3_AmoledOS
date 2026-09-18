/*
 * BLACKJACK - the art. See bj_art.h.
 *
 * Everything is drawn in card coordinates (0..BJ_CARD_W, 0..BJ_CARD_H) and
 * placed at BJ_CARD_PAD. Whatever a real deck prints twice - the index, the
 * pips of the lower half, the figure - is drawn once through a transform
 * that turns it 180 degrees, so the two halves cannot disagree by a pixel.
 */
#include "bj_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --- palette -------------------------------------------------------------- */

#define C_PAPER_T   0xFFFFFF
#define C_PAPER_B   0xF1EDE4
#define C_EDGE      0x9C958A
#define C_RED       0xD0142C
#define C_BLACK     0x17171C
#define C_BACK      0x9A1A2A
#define C_BACK_L    0xC03446
#define C_BACK_D    0x6E0F1C
#define C_GOLD      0xE4BD55
#define C_GOLD_D    0xA67C1E
#define C_FRAME     0xFFF6E2

const int      bj_chip_value[BJ_NCHIPS] = { 10, 50, 100, 500 };
const uint32_t bj_chip_color[BJ_NCHIPS] = { 0x2F6FD8, 0xD3303A, 0x26262C, 0x7B45C0 };

/* --- pixels --------------------------------------------------------------- */

/* fminf/fmaxf are not in the firmware's symbol table; these are enough. */
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 'src' over the pixel, with coverage a (0..255). Straight alpha. */
static inline void plot(bj_img_t *m, int x, int y, uint32_t rgb, int a)
{
    if (a <= 0 || x < 0 || y < 0 || x >= m->w || y >= m->h) {
        return;
    }
    uint32_t *p  = &m->px[y * m->w + x];
    uint32_t  d  = *p;
    int       da = (int)(d >> 24);
    if (a >= 255 || da == 0) {
        *p = ((uint32_t)(a > 255 ? 255 : a) << 24) | (rgb & 0xFFFFFF);
        return;
    }
    int k  = da * (255 - a) / 255;          /* what is left of the old one  */
    int oa = a + k;
    int r = ((int)((rgb >> 16) & 0xFF) * a + (int)((d >> 16) & 0xFF) * k) / oa;
    int g = ((int)((rgb >> 8) & 0xFF) * a + (int)((d >> 8) & 0xFF) * k) / oa;
    int b = ((int)(rgb & 0xFF) * a + (int)(d & 0xFF) * k) / oa;
    *p = ((uint32_t)oa << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t)
{
    t = clampf(t, 0.f, 1.f);
    int r = (int)(((a >> 16) & 0xFF) + (((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * t));
    int g = (int)(((a >> 8) & 0xFF) + (((int)((b >> 8) & 0xFF) - (int)((a >> 8) & 0xFF)) * t));
    int c = (int)((a & 0xFF) + (((int)(b & 0xFF) - (int)(a & 0xFF)) * t));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)c;
}

static uint32_t shade(uint32_t c, int pct)          /* pct < 100 darkens */
{
    int r = (int)((c >> 16) & 0xFF) * pct / 100;
    int g = (int)((c >> 8) & 0xFF) * pct / 100;
    int b = (int)(c & 0xFF) * pct / 100;
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void bj_img_clear(bj_img_t *img)
{
    memset(img->px, 0, (size_t)img->w * img->h * 4);
}

/* --- the transform --------------------------------------------------------
 * Card coordinates to image pixels, straight or turned 180 degrees. Points
 * are continuous (a pixel's centre is +0.5), so turning maps x to W - x.
 * ------------------------------------------------------------------------- */

typedef struct {
    int ox, oy, w, h;           /* where the card is in the image, its size  */
    bool rot;
} xf_t;

static void xf_pt(const xf_t *t, float x, float y, float *ox, float *oy)
{
    if (t->rot) {
        x = (float)t->w - x;
        y = (float)t->h - y;
    }
    *ox = x + (float)t->ox;
    *oy = y + (float)t->oy;
}

/* --- rounded rectangles, by signed distance ------------------------------ */

static float sd_rrect(float px, float py, float x0, float y0, float w, float h, float r)
{
    float cx = x0 + w * 0.5f, cy = y0 + h * 0.5f;
    float qx = fabsf(px - cx) - (w * 0.5f - r);
    float qy = fabsf(py - cy) - (h * 0.5f - r);
    float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
    float out = sqrtf(mx * mx + my * my);
    float in  = qx > qy ? qx : qy;
    return out + (in < 0 ? in : 0) - r;
}

/* Fill, with a vertical gradient from ct to cb. In image pixels. */
static void rrect_fill(bj_img_t *m, float x0, float y0, float w, float h, float r,
                       uint32_t ct, uint32_t cb, int opa)
{
    int ix0 = (int)floorf(x0) - 1, iy0 = (int)floorf(y0) - 1;
    int ix1 = (int)ceilf(x0 + w) + 1, iy1 = (int)ceilf(y0 + h) + 1;
    for (int y = iy0; y < iy1; y++) {
        uint32_t c = ct == cb ? ct : lerp_rgb(ct, cb, ((float)y + 0.5f - y0) / h);
        for (int x = ix0; x < ix1; x++) {
            float d = sd_rrect((float)x + 0.5f, (float)y + 0.5f, x0, y0, w, h, r);
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov > 0) {
                plot(m, x, y, c, (int)(cov * (float)opa + 0.5f));
            }
        }
    }
}

/* A band 'bw' wide just inside the edge. */
static void rrect_stroke(bj_img_t *m, float x0, float y0, float w, float h, float r,
                         float bw, uint32_t c, int opa)
{
    int ix0 = (int)floorf(x0) - 1, iy0 = (int)floorf(y0) - 1;
    int ix1 = (int)ceilf(x0 + w) + 1, iy1 = (int)ceilf(y0 + h) + 1;
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            float d  = sd_rrect((float)x + 0.5f, (float)y + 0.5f, x0, y0, w, h, r);
            float c1 = clampf(0.5f - d, 0.f, 1.f);
            float c2 = clampf(d + bw + 0.5f, 0.f, 1.f);
            float cov = c1 < c2 ? c1 : c2;
            if (cov > 0) {
                plot(m, x, y, c, (int)(cov * (float)opa + 0.5f));
            }
        }
    }
}

/* A soft shadow: alpha falls off over 'blur' pixels outside the shape. */
static void rrect_shadow(bj_img_t *m, float x0, float y0, float w, float h, float r,
                         float blur, int opa)
{
    int ix0 = (int)floorf(x0 - blur) - 1, iy0 = (int)floorf(y0 - blur) - 1;
    int ix1 = (int)ceilf(x0 + w + blur) + 1, iy1 = (int)ceilf(y0 + h + blur) + 1;
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            float d = sd_rrect((float)x + 0.5f, (float)y + 0.5f, x0, y0, w, h, r);
            float t = clampf(1.f - (d + 0.5f) / blur, 0.f, 1.f);
            if (t > 0) {
                plot(m, x, y, 0x000000, (int)(t * t * (float)opa));
            }
        }
    }
}

/* --- the suits ------------------------------------------------------------
 * Each is a test "is (nx, ny) inside", in a box from -1 to 1 with y down.
 * ------------------------------------------------------------------------- */

static bool in_heart_raw(float nx, float ny)
{
    /* The sextic (x^2 + y^2 - 1)^3 - x^2 y^3 <= 0, y up, fitted to the box. */
    float X = nx * 1.20f;
    float Y = 0.06f - 1.16f * ny;
    float a = X * X + Y * Y - 1.f;
    return a * a * a - X * X * Y * Y * Y <= 0.f;
}

static bool in_stem(float nx, float ny, float top)
{
    if (ny < top || ny > 0.98f) {
        return false;
    }
    float u  = (ny - top) / (0.98f - top);
    float hw = 0.07f + 0.36f * u * u * u;       /* flares at the foot */
    return fabsf(nx) <= hw;
}

static bool in_suit(int suit, float nx, float ny)
{
    switch (suit) {
    case 1:                                     /* hearts */
        return in_heart_raw(nx, ny * 1.03f);
    case 2: {                                   /* diamonds */
        float ax = fabsf(nx) / 0.80f, ay = fabsf(ny);
        return powf(ax, 0.88f) + powf(ay, 0.88f) <= 1.f;
    }
    case 0: {                                   /* spades: a heart upside down */
        float t = (ny + 1.f) / 0.78f - 1.f;     /* the body: ny -1 .. 0.56 */
        if (t <= 1.f && in_heart_raw(nx * 1.02f, -t)) {
            return true;
        }
        return in_stem(nx, ny, 0.30f);
    }
    default: {                                  /* clubs */
        static const float c[3][2] = { { 0.f, -0.50f }, { -0.50f, 0.12f }, { 0.50f, 0.12f } };
        const float r = 0.40f;
        for (int i = 0; i < 3; i++) {
            float dx = nx - c[i][0], dy = ny - c[i][1];
            if (dx * dx + dy * dy <= r * r) {
                return true;
            }
        }
        if (nx * nx + (ny + 0.05f) * (ny + 0.05f) <= 0.26f * 0.26f) {
            return true;
        }
        return in_stem(nx, ny, 0.10f);
    }
    }
}

/* Masks are made once per (suit, size) and kept. 4x4 samples per pixel. */
typedef struct {
    uint8_t *a;
    int      size;
    int      suit;
} mask_t;

#define MAX_MASKS 24
static mask_t s_masks[MAX_MASKS];
static int    s_nmasks;

static const mask_t *suit_mask(int suit, int size)
{
    for (int i = 0; i < s_nmasks; i++) {
        if (s_masks[i].suit == suit && s_masks[i].size == size) {
            return &s_masks[i];
        }
    }
    if (s_nmasks >= MAX_MASKS) {
        return NULL;
    }
    uint8_t *a = (uint8_t *)malloc((size_t)size * size);
    if (!a) {
        return NULL;
    }
    const float inv = 2.f / (float)size;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int n = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float nx = ((float)x + ((float)sx + 0.5f) / 4.f) * inv - 1.f;
                    float ny = ((float)y + ((float)sy + 0.5f) / 4.f) * inv - 1.f;
                    n += in_suit(suit, nx, ny);
                }
            }
            a[y * size + x] = (uint8_t)(n * 255 / 16);
        }
    }
    mask_t *m = &s_masks[s_nmasks++];
    m->a = a;
    m->size = size;
    m->suit = suit;
    return m;
}

static uint32_t suit_color(int suit)
{
    return (suit == 1 || suit == 2) ? C_RED : C_BLACK;
}

/* A pip whose box has its top-left corner at (x, y) in card coordinates. */
static void pip(bj_img_t *m, const xf_t *t, int suit, int size, int x, int y, uint32_t color)
{
    const mask_t *mk = suit_mask(suit, size);
    if (!mk) {
        return;
    }
    int bx = t->rot ? t->w - x - size : x;
    int by = t->rot ? t->h - y - size : y;
    bx += t->ox;
    by += t->oy;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            int sx = t->rot ? size - 1 - i : i;
            int sy = t->rot ? size - 1 - j : j;
            plot(m, bx + i, by + j, color, mk->a[sy * size + sx]);
        }
    }
}

/* --- the stroke font ------------------------------------------------------
 * Glyphs in a 10 x 14 box, y down. M moves with the pen down, P lifts it,
 * L draws a line, A an elliptical arc from a0 to a1 degrees (0 = right,
 * 90 = down), joined to the pen if it is down.
 * ------------------------------------------------------------------------- */

enum { G_M = 1, G_L, G_A, G_P, G_E };
#define M_(x, y)                  G_M, x, y
#define L_(x, y)                  G_L, x, y
#define A_(cx, cy, rx, ry, a, b)  G_A, cx, cy, rx, ry, a, b
#define P_                        G_P
#define E_                        G_E

static const float GL_A[]  = { M_(0.6f, 14.f), L_(5.f, 0.2f), L_(9.4f, 14.f), M_(2.3f, 9.4f), L_(7.7f, 9.4f), E_ };
static const float GL_2[]  = { P_, A_(5.f, 4.3f, 4.2f, 4.1f, 190.f, 395.f), L_(0.8f, 13.8f), L_(9.6f, 13.8f), E_ };
static const float GL_3[]  = { P_, A_(5.f, 3.7f, 3.9f, 3.5f, 200.f, 450.f), P_, A_(5.f, 10.3f, 4.4f, 3.6f, 270.f, 515.f), E_ };
static const float GL_4[]  = { M_(7.3f, 14.f), L_(7.3f, 0.2f), L_(0.5f, 9.8f), L_(9.8f, 9.8f), E_ };
static const float GL_5[]  = { M_(9.f, 0.3f), L_(2.1f, 0.3f), L_(1.5f, 6.4f), A_(5.0f, 9.5f, 4.4f, 4.3f, 226.f, 505.f), E_ };
static const float GL_6[]  = { P_, A_(5.f, 9.6f, 4.3f, 4.3f, 0.f, 360.f), P_, A_(9.4f, 9.6f, 8.7f, 9.4f, 253.f, 180.f), E_ };
static const float GL_7[]  = { M_(0.5f, 0.3f), L_(9.5f, 0.3f), L_(3.7f, 14.f), E_ };
static const float GL_8[]  = { P_, A_(5.f, 3.6f, 3.6f, 3.4f, 0.f, 360.f), P_, A_(5.f, 10.3f, 4.3f, 3.7f, 0.f, 360.f), E_ };
static const float GL_9[]  = { P_, A_(5.f, 4.4f, 4.3f, 4.3f, 0.f, 360.f), P_, A_(0.6f, 4.4f, 8.7f, 9.4f, 73.f, 0.f), E_ };
static const float GL_0[]  = { P_, A_(5.f, 7.f, 4.3f, 6.8f, 0.f, 360.f), E_ };
static const float GL_1[]  = { M_(2.2f, 3.0f), L_(5.6f, 0.2f), L_(5.6f, 14.f), E_ };
static const float GL_10[] = { M_(0.2f, 2.8f), L_(2.8f, 0.2f), L_(2.8f, 14.f),
                               P_, A_(8.9f, 7.f, 3.5f, 6.8f, 0.f, 360.f), E_ };
static const float GL_J[]  = { M_(8.2f, 0.2f), L_(8.2f, 9.6f), A_(4.7f, 9.6f, 3.5f, 4.2f, 0.f, 165.f), E_ };
static const float GL_Q[]  = { P_, A_(5.f, 7.f, 4.4f, 6.8f, 0.f, 360.f), M_(5.9f, 9.9f), L_(9.8f, 14.4f), E_ };
static const float GL_K[]  = { M_(1.f, 0.2f), L_(1.f, 14.f), M_(9.3f, 0.2f), L_(1.2f, 8.7f), M_(4.3f, 5.6f), L_(9.6f, 14.f), E_ };

/* by rank: A 2 3 4 5 6 7 8 9 10 J Q K; then the digits 0..9 for the chips */
static const float *const RANK_GLYPH[13] = {
    GL_A, GL_2, GL_3, GL_4, GL_5, GL_6, GL_7, GL_8, GL_9, GL_10, GL_J, GL_Q, GL_K,
};
static const float *const DIGIT_GLYPH[10] = {
    GL_0, GL_1, GL_2, GL_3, GL_4, GL_5, GL_6, GL_7, GL_8, GL_9,
};

#define MAX_SEGS 96
typedef struct { float x0, y0, x1, y1; } seg_t;

/* The glyph's segments, in glyph units. Returns how many. */
static int glyph_segs(const float *g, seg_t *out)
{
    int   n = 0;
    float px = 0, py = 0;
    bool  down = false;
    while (*g != G_E && n < MAX_SEGS) {
        int op = (int)*g++;
        if (op == G_M) {
            px = g[0];
            py = g[1];
            g += 2;
            down = true;
        } else if (op == G_P) {
            down = false;
        } else if (op == G_L) {
            if (down) {
                out[n++] = (seg_t){ px, py, g[0], g[1] };
            }
            px = g[0];
            py = g[1];
            g += 2;
            down = true;
        } else if (op == G_A) {
            float cx = g[0], cy = g[1], rx = g[2], ry = g[3], a0 = g[4], a1 = g[5];
            g += 6;
            int steps = (int)(fabsf(a1 - a0) / 12.f) + 2;
            for (int i = 0; i <= steps && n < MAX_SEGS; i++) {
                float a = (a0 + (a1 - a0) * (float)i / (float)steps) * 3.14159265f / 180.f;
                float x = cx + rx * cosf(a), y = cy + ry * sinf(a);
                if (down) {
                    out[n++] = (seg_t){ px, py, x, y };
                }
                px = x;
                py = y;
                down = true;
            }
        }
    }
    return n;
}

static float seg_dist2(const seg_t *s, float x, float y)
{
    float dx = s->x1 - s->x0, dy = s->y1 - s->y0;
    float l2 = dx * dx + dy * dy;
    float t  = l2 > 0 ? ((x - s->x0) * dx + (y - s->y0) * dy) / l2 : 0;
    t = clampf(t, 0.f, 1.f);
    float ex = s->x0 + dx * t - x, ey = s->y0 + dy * t - y;
    return ex * ex + ey * ey;
}

/* Glyph units are 10 wide; "10" is 12.4. */
static float glyph_units_w(const float *g)
{
    return g == GL_10 ? 12.4f : 10.f;
}

/* Draws glyph 'g' with its box's top-left at (x, y) card units, 'h' pixels
 * tall, horizontal scale 'sx', stroke 'sw' pixels wide. */
static void glyph(bj_img_t *m, const xf_t *t, const float *g, float x, float y,
                  float h, float sx, float sw, uint32_t color)
{
    seg_t seg[MAX_SEGS];
    int   n = glyph_segs(g, seg);
    float k = h / 14.f;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) {
        float ax, ay, bx, by;
        xf_pt(t, x + seg[i].x0 * k * sx, y + seg[i].y0 * k, &ax, &ay);
        xf_pt(t, x + seg[i].x1 * k * sx, y + seg[i].y1 * k, &bx, &by);
        seg[i] = (seg_t){ ax, ay, bx, by };
        minx = minf(minx, minf(ax, bx));
        maxx = maxf(maxx, maxf(ax, bx));
        miny = minf(miny, minf(ay, by));
        maxy = maxf(maxy, maxf(ay, by));
    }
    float hw = sw * 0.5f;
    int   x0 = (int)floorf(minx - hw - 1), x1 = (int)ceilf(maxx + hw + 1);
    int   y0 = (int)floorf(miny - hw - 1), y1 = (int)ceilf(maxy + hw + 1);
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
            float best = 1e9f;
            for (int i = 0; i < n; i++) {
                float d2 = seg_dist2(&seg[i], cx, cy);
                if (d2 < best) {
                    best = d2;
                }
            }
            float cov = clampf(hw + 0.5f - sqrtf(best), 0.f, 1.f);
            if (cov > 0) {
                plot(m, px, py, color, (int)(cov * 255.f + 0.5f));
            }
        }
    }
}

/* --- the card ------------------------------------------------------------- */

/* Where the pips go, as the top-left corner of a 16 px box on a 76 x 106
 * card, and whether the pip also has a turned copy. Columns at 18, 30 and
 * 42 (centres 26, 38, 50), clear of the index in the corner; the lower half
 * of each layout is the upper half turned, which is what a real deck does. */
#define PIP     16
#define COL_L   18
#define COL_C   30
#define COL_R   42
#define ROW_T   12          /* the top row; turned it is the bottom one      */
#define ROW_Q   33          /* second of four rows                           */
#define ROW_M   45          /* the middle, never turned                      */
#define ROW_7   28          /* the centre pip between top and middle         */
#define ROW_10  22          /* the centre pip between the first two of four  */

typedef struct { int8_t x, y, both; } pipspot_t;   /* both: also turned */

static const pipspot_t SP2[]  = { { COL_C, ROW_T, 1 } };
static const pipspot_t SP3[]  = { { COL_C, ROW_T, 1 }, { COL_C, ROW_M, 0 } };
static const pipspot_t SP4[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 } };
static const pipspot_t SP5[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_C, ROW_M, 0 } };
static const pipspot_t SP6[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_L, ROW_M, 0 }, { COL_R, ROW_M, 0 } };
static const pipspot_t SP7[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_L, ROW_M, 0 }, { COL_R, ROW_M, 0 },
                                  { COL_C, ROW_7, 0 } };
static const pipspot_t SP8[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_L, ROW_M, 0 }, { COL_R, ROW_M, 0 },
                                  { COL_C, ROW_7, 1 } };
static const pipspot_t SP9[]  = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_L, ROW_Q, 1 }, { COL_R, ROW_Q, 1 },
                                  { COL_C, ROW_M, 0 } };
static const pipspot_t SP10[] = { { COL_L, ROW_T, 1 }, { COL_R, ROW_T, 1 }, { COL_L, ROW_Q, 1 }, { COL_R, ROW_Q, 1 },
                                  { COL_C, ROW_10, 1 } };

static const pipspot_t *const SPOTS[11] = { NULL, NULL, SP2, SP3, SP4, SP5, SP6, SP7, SP8, SP9, SP10 };
static const uint8_t NSPOTS[11] = { 0, 0, 1, 2, 2, 3, 4, 5, 5, 5, 5 };

static void draw_index(bj_img_t *m, const xf_t *t, int suit, int rank, uint32_t col)
{
    const float *g  = RANK_GLYPH[rank];
    float        h  = 15.f;
    float        sx = rank == 9 ? 0.72f : 0.86f;        /* "10" is narrower   */
    float        w  = glyph_units_w(g) * (h / 14.f) * sx;
    glyph(m, t, g, 9.5f - w * 0.5f, 5.f, h, sx, 2.2f, col);
    pip(m, t, suit, 11, 4, 24, col);
}

static void draw_face(bj_img_t *m, int card)
{
    int      suit = BJ_SUIT(card), rank = BJ_RANK(card);
    uint32_t col  = suit_color(suit);
    xf_t     up   = { BJ_CARD_PAD, BJ_CARD_PAD, BJ_CARD_W, BJ_CARD_H, false };
    xf_t     dn   = up;
    dn.rot = true;

    rrect_fill(m, BJ_CARD_PAD, BJ_CARD_PAD, BJ_CARD_W, BJ_CARD_H, 6.f, C_PAPER_T, C_PAPER_B, 255);
    rrect_stroke(m, BJ_CARD_PAD, BJ_CARD_PAD, BJ_CARD_W, BJ_CARD_H, 6.f, 1.f, C_EDGE, 255);

    if (rank >= 10) {
        /* The court: a framed picture, the figure twice. */
        float fx = BJ_CARD_PAD + 14.f, fy = BJ_CARD_PAD + 12.f;
        rrect_fill(m, fx, fy, 48.f, 82.f, 2.f, C_FRAME, C_FRAME, 255);
        bj_court_draw(m, suit, rank, BJ_CARD_PAD + 15, BJ_CARD_PAD + 13);
        rrect_stroke(m, fx, fy, 48.f, 82.f, 2.f, 1.f, C_GOLD_D, 255);
        rrect_stroke(m, fx + 2.f, fy + 2.f, 44.f, 78.f, 1.f, 1.f, col, 120);
    } else if (rank == 0) {
        /* The ace: one big pip; the ace of spades gets the traditional
         * flourish, a ring around a larger one. */
        if (suit == 0) {
            pip(m, &up, suit, 42, 17, 32, col);
            float cx = BJ_CARD_PAD + 38.f, cy = BJ_CARD_PAD + 53.f;
            rrect_stroke(m, cx - 27.f, cy - 27.f, 54.f, 54.f, 27.f, 1.2f, C_GOLD_D, 255);
            rrect_stroke(m, cx - 24.f, cy - 24.f, 48.f, 48.f, 24.f, 0.8f, C_GOLD_D, 160);
        } else {
            pip(m, &up, suit, 40, 18, 33, col);
        }
    } else {
        int n = rank + 1;
        for (int i = 0; i < NSPOTS[n]; i++) {
            const pipspot_t *s = &SPOTS[n][i];
            pip(m, &up, suit, PIP, s->x, s->y, col);
            if (s->both) {
                pip(m, &dn, suit, PIP, s->x, s->y, col);
            }
        }
    }

    draw_index(m, &up, suit, rank, col);
    draw_index(m, &dn, suit, rank, col);
}

static void draw_back(bj_img_t *m)
{
    const float x0 = BJ_CARD_PAD, y0 = BJ_CARD_PAD;
    rrect_fill(m, x0, y0, BJ_CARD_W, BJ_CARD_H, 6.f, C_PAPER_T, C_PAPER_B, 255);
    rrect_stroke(m, x0, y0, BJ_CARD_W, BJ_CARD_H, 6.f, 1.f, C_EDGE, 255);

    /* The printed field: a lattice of diagonals, lighter where they cross. */
    const float fx = x0 + 4.f, fy = y0 + 4.f, fw = BJ_CARD_W - 8.f, fh = BJ_CARD_H - 8.f;
    for (int y = (int)fy - 1; y <= (int)(fy + fh) + 1; y++) {
        for (int x = (int)fx - 1; x <= (int)(fx + fw) + 1; x++) {
            float d   = sd_rrect((float)x + 0.5f, (float)y + 0.5f, fx, fy, fw, fh, 3.5f);
            float cov = clampf(0.5f - d, 0.f, 1.f);
            if (cov <= 0) {
                continue;
            }
            int u = (x + y) & 7, v = (x - y + 64) & 7;
            uint32_t c = C_BACK;
            if (u == 0 && v == 0) {
                c = 0xF0C8CE;
            } else if (u == 0 || v == 0) {
                c = C_BACK_L;
            } else if ((u == 4) && (v == 4)) {
                c = C_BACK_D;
            }
            plot(m, x, y, c, (int)(cov * 255.f));
        }
    }
    rrect_stroke(m, fx + 3.f, fy + 3.f, fw - 6.f, fh - 6.f, 2.f, 1.f, 0xF3D7DB, 200);

    /* The medallion: a dark oval with a gold ring and a gold spade. */
    float cx = x0 + BJ_CARD_W * 0.5f, cy = y0 + BJ_CARD_H * 0.5f;
    rrect_fill(m, cx - 19.f, cy - 23.f, 38.f, 46.f, 19.f, C_BACK_D, 0x4E0913, 255);
    rrect_stroke(m, cx - 19.f, cy - 23.f, 38.f, 46.f, 19.f, 2.f, C_GOLD, 255);
    rrect_stroke(m, cx - 15.f, cy - 19.f, 30.f, 38.f, 15.f, 0.8f, C_GOLD_D, 255);
    xf_t up = { BJ_CARD_PAD, BJ_CARD_PAD, BJ_CARD_W, BJ_CARD_H, false };
    pip(m, &up, 0, 22, 27, 42, C_GOLD);
}

void bj_card_draw(bj_img_t *img, int card)
{
    bj_img_clear(img);
    rrect_shadow(img, BJ_CARD_PAD + 1.f, BJ_CARD_PAD + 2.f, BJ_CARD_W, BJ_CARD_H, 6.f, 4.5f, 120);
    if (card < 0) {
        draw_back(img);
    } else {
        draw_face(img, card);
    }
}

/* --- chips ----------------------------------------------------------------
 * Seen from above: a disc with six white inserts on the rim, a dashed ring
 * and the value on a white inlay. Sampled 4x4 per pixel from a function
 * that answers the colour at a point, which is the simplest way to get the
 * angular edges of the inserts smooth.
 * ------------------------------------------------------------------------- */

static int chip_index(int value)
{
    for (int i = 0; i < BJ_NCHIPS; i++) {
        if (bj_chip_value[i] == value) {
            return i;
        }
    }
    return 0;
}

/* Colour and coverage (0 = outside) of a chip of radius R at (x, y) from its
 * centre. */
static int chip_at(float x, float y, float R, uint32_t c, uint32_t *out)
{
    float r = sqrtf(x * x + y * y);
    if (r > R) {
        return 0;
    }
    float t = r / R;
    if (t > 0.965f) {
        *out = shade(c, 62);
        return 1;
    }
    if (t > 0.74f) {
        float a = atan2f(y, x) * 180.f / 3.14159265f + 360.f + 15.f;
        float f = fmodf(a, 60.f);
        *out = f < 22.f ? 0xF4F4F4 : c;
        return 1;
    }
    if (t > 0.60f && t < 0.66f) {
        float a = atan2f(y, x) * 180.f / 3.14159265f + 360.f;
        *out = fmodf(a, 15.f) < 9.f ? lerp_rgb(c, 0xFFFFFF, 0.65f) : c;
        return 1;
    }
    if (t < 0.54f) {
        *out = t > 0.50f ? shade(c, 80) : 0xFAFAF7;
        return 1;
    }
    *out = c;
    return 1;
}

static void chip_disc(bj_img_t *m, float cx, float cy, float R, uint32_t c)
{
    for (int y = (int)(cy - R) - 1; y <= (int)(cy + R) + 1; y++) {
        for (int x = (int)(cx - R) - 1; x <= (int)(cx + R) + 1; x++) {
            int n = 0, r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    uint32_t col;
                    float px = (float)x + ((float)sx + 0.5f) / 4.f - cx;
                    float py = (float)y + ((float)sy + 0.5f) / 4.f - cy;
                    if (chip_at(px, py, R, c, &col)) {
                        n++;
                        r += (int)((col >> 16) & 0xFF);
                        g += (int)((col >> 8) & 0xFF);
                        b += (int)(col & 0xFF);
                    }
                }
            }
            if (n) {
                uint32_t avg = ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
                plot(m, x, y, avg, n * 255 / 16);
            }
        }
    }
}

static void number(bj_img_t *m, int value, float cx, float cy, float h, float sx, float sw,
                   uint32_t color)
{
    char s[12];
    int  n = 0;
    do {
        s[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && n < 11);
    float k   = h / 14.f;
    float adv = (10.f + 3.2f) * k * sx;
    float w   = adv * (float)n - 3.2f * k * sx;
    float x   = cx - w * 0.5f;
    xf_t  id  = { 0, 0, m->w, m->h, false };
    for (int i = n - 1; i >= 0; i--) {
        glyph(m, &id, DIGIT_GLYPH[s[i] - '0'], x, cy - h * 0.5f, h, sx, sw, color);
        x += adv;
    }
}

void bj_chip_draw(bj_img_t *img, int value)
{
    bj_img_clear(img);
    uint32_t c  = bj_chip_color[chip_index(value)];
    float    cx = BJ_CHIP_CV * 0.5f - 1.f, cy = BJ_CHIP_CV * 0.5f - 1.f;
    float    R  = BJ_CHIP_R;
    rrect_shadow(img, cx - R + 1.f, cy - R + 2.f, 2 * R, 2 * R, R, 3.f, 140);
    chip_disc(img, cx, cy, R, c);
    float sx = value >= 100 ? 0.62f : 0.8f;
    number(img, value, cx, cy, 11.f, sx, 2.0f, c == 0x26262C ? 0x26262C : shade(c, 85));
}

/* --- the pile ------------------------------------------------------------- */

#define ST_A    21.f            /* half width of a chip seen from the side    */
#define ST_B    7.f             /* half height of its ellipse                 */
#define ST_T    4.f             /* thickness                                  */
#define ST_MAX  14

/* Colour at a point of the pile, or 0 if outside. Chips from the top down,
 * the first one hit wins. */
static int stack_at(const uint8_t *chips, int n, float bx, float by, float x, float y,
                    uint32_t *out)
{
    for (int i = n - 1; i >= 0; i--) {
        uint32_t c   = bj_chip_color[chips[i]];
        float    top = by - (float)i * ST_T - ST_T;     /* centre of the face */
        float    dx  = (x - bx) / ST_A;
        if (fabsf(dx) > 1.f) {
            continue;
        }
        float dyt = (y - top) / ST_B;
        if (dx * dx + dyt * dyt <= 1.f) {                /* the top face */
            float r = sqrtf(dx * dx + dyt * dyt);
            if (r > 0.93f) {
                *out = shade(c, 70);
            } else if (r > 0.72f) {
                float a = atan2f(dyt, dx) * 180.f / 3.14159265f + 375.f;
                *out = fmodf(a, 60.f) < 20.f ? 0xF4F4F4 : c;
            } else if (r < 0.46f) {
                *out = lerp_rgb(c, 0xFFFFFF, 0.55f);
            } else {
                *out = c;
            }
            return 1;
        }
        /* the side band: from the face's middle row down by the thickness,
         * closed by the lower ellipse */
        float ey = ST_B * sqrtf(1.f - dx * dx);
        if (y >= top && y <= top + ST_T + ey) {
            float a = asinf(dx) * 180.f / 3.14159265f + 90.f;   /* 0..180 */
            uint32_t side = shade(c, 72);
            if (fmodf(a + 12.f, 45.f) < 12.f) {
                side = 0xDADADA;
            }
            *out = y > top + ST_T + ey - 0.9f ? shade(side, 70) : side;
            return 1;
        }
    }
    return 0;
}

void bj_stack_draw(bj_img_t *img, int amount)
{
    bj_img_clear(img);
    uint8_t chips[ST_MAX];
    int     n = 0;
    for (int i = BJ_NCHIPS - 1; i >= 0 && n < ST_MAX; i--) {
        while (amount >= bj_chip_value[i] && n < ST_MAX) {
            amount -= bj_chip_value[i];
            chips[n++] = (uint8_t)i;
        }
    }
    if (n == 0) {
        return;
    }
    /* the biggest at the bottom */
    float bx = BJ_STACK_W * 0.5f, by = BJ_STACK_H - ST_B - 4.f;
    rrect_shadow(img, bx - ST_A + 2.f, by - ST_B + 1.f, 2 * ST_A, 2 * ST_B, ST_B, 3.f, 120);
    int top = (int)(by - (float)n * ST_T - ST_T - ST_B) - 1;
    for (int y = top < 0 ? 0 : top; y < BJ_STACK_H; y++) {
        for (int x = (int)(bx - ST_A) - 1; x <= (int)(bx + ST_A) + 1; x++) {
            int k = 0, r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 3; sy++) {
                for (int sx = 0; sx < 3; sx++) {
                    uint32_t col;
                    float px = (float)x + ((float)sx + 0.5f) / 3.f;
                    float py = (float)y + ((float)sy + 0.5f) / 3.f;
                    if (stack_at(chips, n, bx, by, px, py, &col)) {
                        k++;
                        r += (int)((col >> 16) & 0xFF);
                        g += (int)((col >> 8) & 0xFF);
                        b += (int)(col & 0xFF);
                    }
                }
            }
            if (k) {
                uint32_t avg = ((uint32_t)(r / k) << 16) | ((uint32_t)(g / k) << 8) | (uint32_t)(b / k);
                plot(img, x, y, avg, k * 255 / 9);
            }
        }
    }
}

/* --- the table ------------------------------------------------------------ */

static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

#define RAIL_H  38

void bj_felt_draw(bj_img_t *img)
{
    const float cx = BJ_SCREEN_W * 0.5f, cy = 250.f;
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < img->w; x++) {
            uint32_t c;
            int      grain = (int)(hash2(x, y) & 7) - 3;
            if (y < RAIL_H) {
                /* the padded rail: leather, lit from above */
                float t = (float)y / RAIL_H;
                c = lerp_rgb(0x4A2E1A, 0x1E120A, t * t);
                if (y == RAIL_H - 3) {
                    c = 0x8C6A2E;
                } else if (y == RAIL_H - 2) {
                    c = 0x5A4220;
                }
                grain /= 2;
            } else {
                float dx = ((float)x - cx) / 300.f, dy = ((float)y - cy) / 330.f;
                float d  = sqrtf(dx * dx + dy * dy);
                c = lerp_rgb(0x1F7C4A, 0x0A3A20, d * 1.35f);
                if (y < RAIL_H + 6) {       /* the rail's shadow on the felt */
                    c = shade(c, 70 + (y - RAIL_H) * 5);
                }
            }
            int r = (int)((c >> 16) & 0xFF) + grain, g = (int)((c >> 8) & 0xFF) + grain,
                b = (int)(c & 0xFF) + grain;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            img->px[y * img->w + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* The printed band: two gold arcs around the dealer, centred above the
     * top of the screen, like the curve of a real table. The words between
     * them are the app's (translated, with the real fonts). */
    static const float R[2] = { 176.f, 222.f };
    const float        ax = cx, ay = 14.f;
    for (int y = 120; y < 250; y++) {
        for (int x = 0; x < img->w; x++) {
            float dx = (float)x + 0.5f - ax, dy = (float)y + 0.5f - ay;
            float r  = sqrtf(dx * dx + dy * dy);
            for (int i = 0; i < 2; i++) {
                float cov = clampf(1.1f - fabsf(r - R[i]), 0.f, 1.f);
                if (cov > 0) {
                    plot(img, x, y, C_GOLD, (int)(cov * 210.f));
                }
            }
        }
    }

    /* The betting circle, where the pile goes. */
    const float bx = 44.f, by = 300.f;
    for (int y = (int)by - 36; y < (int)by + 36; y++) {
        for (int x = (int)bx - 36; x < (int)bx + 36; x++) {
            float dx = (float)x + 0.5f - bx, dy = (float)y + 0.5f - by;
            float r  = sqrtf(dx * dx + dy * dy);
            float cov = clampf(0.9f - fabsf(r - 31.f), 0.f, 1.f);
            if (cov > 0) {
                plot(img, x, y, C_GOLD, (int)(cov * 150.f));
            }
            if (r < 30.f) {
                plot(img, x, y, 0x000000, 26);
            }
        }
    }
}

void bj_to_rgb565(const bj_img_t *src, uint16_t *dst)
{
    static const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
    for (int y = 0; y < src->h; y++) {
        for (int x = 0; x < src->w; x++) {
            uint32_t c = src->px[y * src->w + x];
            int      d = BAYER[(y & 3) * 4 + (x & 3)];
            int r = (int)((c >> 16) & 0xFF) + (d >> 1);
            int g = (int)((c >> 8) & 0xFF) + (d >> 2);
            int b = (int)(c & 0xFF) + (d >> 1);
            r = r > 255 ? 255 : r;
            g = g > 255 ? 255 : g;
            b = b > 255 ? 255 : b;
            dst[y * src->w + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

/* --- life ----------------------------------------------------------------- */

bool bj_art_init(void)
{
    return true;                /* the masks are made the first time asked */
}

void bj_art_free(void)
{
    for (int i = 0; i < s_nmasks; i++) {
        free(s_masks[i].a);
    }
    s_nmasks = 0;
}
