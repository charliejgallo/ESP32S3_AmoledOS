/*
 * TOPOS - drawing (see topos.h)
 *
 * THE LAWN DOES NOT MOVE. Grass, holes and mounds are painted once into bg;
 * what changes is what comes out of the holes, the effects and the mallet.
 * That is Claude Jump's and arkanos's dirty-rectangle scheme, and it is what
 * keeps a full field at 30 fps: most frames push a few percent of the screen.
 *
 * The compositor is one step more general than theirs. There, every moving
 * object was redrawn every frame and recorded the union of its old and new
 * positions. Here most things stand still most of the time -a mole waiting,
 * a hat on a head-, so a slot that did not change costs nothing, and when one
 * does change, the union of its boxes is REBUILT: restored from bg, and every
 * slot that touches it repainted inside it, in z-order. That is what lets an
 * explosion cover a neighbouring mole, or a popup float over one, without
 * either leaving a trail.
 *
 * Two rules keep that exact, and tools/tp_harness.c checks both every frame:
 *
 *   - a slot is drawn from its tp_dp_t and nothing else (topos.h);
 *   - its box is measured by running THE SAME painting code with no buffer
 *     (pen_t), so the box cannot miss a pixel the painting puts down.
 */
#include "topos.h"
#include "tp_art.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Colours
 * -------------------------------------------------------------------------- */

#define C_GRASS_A   0x88C24F
#define C_GRASS_B   0x80BA49
#define C_TUFT      0x5E9C36
#define C_TUFT_HI   0xA8DA6C
#define C_SHADOW    0x2E6A18

#define C_DIRT_HI   0xF2BE7A
#define C_DIRT_LT   0xDDA05E
#define C_DIRT_MD   0xBE7E42
#define C_DIRT_DK   0x94602F
#define C_DIRT_OL   0x5A3418
#define C_PIT       0x2A150A
#define C_PIT_WALL  0x5A3218
#define C_PIT_WALL2 0x3E220F
#define C_LIP       0xF7D29A
#define C_SPECK_D   0xA86A34
#define C_SPECK_L   0xF6D6A0

#define C_TEXT_OL   0x1A0E06

/* The title's scene, in buffer pixels. The LVGL title sits above it and the
 * mode buttons start right under the big mound (screen y 232). */
#define TITLE_HX    92
#define TITLE_HY    98
#define TITLE_MRX   48
#define TITLE_MRY   17
#define TITLE_ORX   32
#define TITLE_ORY   10
#define TITLE_RISE  33

/* Mole flags, tp_dp_t v[4] */
#define MF_HELMET   0x01
#define MF_PAWS     0x02
#define MF_STARS    0x04
#define MF_TWINKLE  0x08
#define MF_SWEAT    0x10

/* The floor of each hole's opening, per column: see tp_buf_t's lip. */
static int16_t s_lip[TP_HOLES][2 * TP_OPEN_RX + 1];
static int16_t s_tlip[2 * TITLE_ORX + 1];

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* --------------------------------------------------------------------------
 * The lawn
 *
 * Mowing stripes, tufts on a jittered grid, and now and then a clover, a
 * flower or a pebble. All from a hash of the position, so it is the same
 * lawn every time, and none of it within reach of a mound.
 * -------------------------------------------------------------------------- */

static bool near_hole(int x, int y, bool title)
{
    if (title) {
        /* the big hole, with a margin, and the bomb beside it */
        const int rx = TITLE_MRX + 6, ry = TITLE_MRY + 6;
        int dx = x - TITLE_HX, dy = y - TITLE_HY;
        int bx = x - 30, by = y - 96;
        return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry ||
               bx * bx + by * by <= 18 * 18;
    }
    for (int i = 0; i < TP_HOLES; i++) {
        int dx = x - TP_COL_X(i % TP_COLS), dy = y - TP_ROW_Y(i / TP_COLS);
        /* ellipse 30 x 14 around each hole */
        if (dx * dx * 196 + dy * dy * 900 <= 900 * 196) {
            return true;
        }
    }
    return false;
}

static void tuft(tp_buf_t *b, int x, int y, uint32_t h)
{
    const uint16_t d = tp_rgb(C_TUFT), l = tp_rgb(C_TUFT_HI);
    tp_px(b, x, y, d);
    tp_px(b, x + 1, y + 1, d);
    tp_px(b, x + 3, y, d);
    tp_px(b, x + 2, y + 1, d);
    if (h & 0x100) {
        tp_px(b, x + 5, y + 1, d);          /* a third blade */
        tp_px(b, x + 6, y, d);
    }
    if (h & 0x200) {
        tp_px(b, x + 1, y - 1, l);
    }
}

static void flower(tp_buf_t *b, int x, int y, uint32_t h)
{
    uint16_t petal = tp_rgb((h & 0x400) ? 0xFFFFFF : 0xFFC2D8);
    tp_px(b, x + 1, y + 3, tp_rgb(C_TUFT));
    tp_px(b, x, y + 1, petal);
    tp_px(b, x + 2, y + 1, petal);
    tp_px(b, x + 1, y, petal);
    tp_px(b, x + 1, y + 2, petal);
    tp_px(b, x + 1, y + 1, tp_rgb(0xFFD23F));
}

static void lawn(tp_buf_t *b, bool title)
{
    const uint16_t a = tp_rgb(C_GRASS_A), c = tp_rgb(C_GRASS_B);
    for (int y = 0; y < TP_H; y++) {
        tp_hline(b, 0, y, TP_W, ((y / 16) & 1) ? c : a);
    }

    for (int gy = 0; gy < TP_H; gy += 11) {
        for (int gx = 0; gx < TP_W; gx += 13) {
            uint32_t h = hash32((uint32_t)(gx * 7919 + gy * 104729 + 17));
            int x = gx + (int)(h & 7);
            int y = gy + (int)((h >> 3) & 7);
            if (near_hole(x + 2, y + 1, title)) {
                continue;
            }
            int kind = (int)((h >> 12) % 40u);
            if (kind < 22) {
                tuft(b, x, y, h);
            } else if (kind < 25) {
                const uint16_t l = tp_rgb(C_TUFT_HI);
                tp_px(b, x, y, l);
                tp_px(b, x + 1, y, l);
                tp_px(b, x, y + 1, l);
                tp_px(b, x + 1, y + 1, tp_rgb(C_TUFT));
            } else if (kind < 27) {
                flower(b, x, y, h);
            } else if (kind < 28) {
                tp_px(b, x, y, tp_rgb(0xB8B4A8));
                tp_px(b, x + 1, y, tp_rgb(0x8A857A));
                tp_px(b, x, y - 1, tp_rgb(0xE6E2D6));
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * A hole
 *
 * A mound of dirt round an opening. The mound is a bumpy ellipse (its edge
 * wobbles by a hash of the column, so it reads as crumbly soil), lit on its
 * back half, shaded on its front slope, outlined and speckled; it casts a
 * shadow down-right on the grass. The opening shows its back wall in two
 * lighter rows and has a highlight on its front lip.
 *
 * The same code draws the title's big hole, and it fills in the lip table the
 * occupants are clipped against: for each column, the first row of the front
 * lip. An occupant is visible above it and hidden from it down.
 * -------------------------------------------------------------------------- */

#define HM_W        (2 * (TITLE_MRX + 3) + 1)
#define HM_H        (2 * (TITLE_MRY + 3) + 1)
static uint8_t s_hm[HM_W * HM_H];          /* 0 grass, 1 mound, 2 opening */

static int open_e(int dx, int orx, int ory)
{
    if (dx < 0) {
        dx = -dx;
    }
    if (dx > orx) {
        return -1;
    }
    return (ory * tp_isqrt((orx * orx - dx * dx) * 1024) + 16) / (orx * 32);
}

static void hole_draw(tp_buf_t *b, int cx, int cy, int mrx, int mry,
                      int orx, int ory, uint32_t seed, int16_t *lip)
{
    const int ox = mrx + 3, oy = mry + 3;
    const int W = 2 * ox + 1, H = 2 * oy + 1;

    for (int dy = -oy; dy <= oy; dy++) {
        for (int dx = -ox; dx <= ox; dx++) {
            int e = open_e(dx, orx, ory);
            uint8_t m = 0;
            if (e >= 0 && dy >= -e && dy <= e) {
                m = 2;
            } else {
                int d = dx * dx * 256 / (mrx * mrx) + dy * dy * 256 / (mry * mry);
                int wob = (int)(hash32(seed * 977u + (uint32_t)((dx + 64) / 3) * 131u +
                                       (dy < 0 ? 7u : 0u)) % 40u) - 14;
                if (d <= 256 + wob) {
                    m = 1;
                }
            }
            s_hm[(dy + oy) * W + dx + ox] = m;
        }
    }
#define HM(dx, dy) (((unsigned)((dx) + ox) < (unsigned)W && \
                     (unsigned)((dy) + oy) < (unsigned)H) ? \
                    s_hm[((dy) + oy) * W + (dx) + ox] : 0)

    /* the shadow on the grass */
    const uint16_t shadow = tp_rgb(C_SHADOW);
    for (int dy = -oy; dy <= oy + 3; dy++) {
        for (int dx = -ox; dx <= ox + 2; dx++) {
            if (HM(dx, dy) == 0 && HM(dx - 2, dy - 3) != 0) {
                tp_px_mix(b, cx + dx, cy + dy, shadow, 6);
            }
        }
    }

    /* the mound */
    const uint16_t hi = tp_rgb(C_DIRT_HI), lt = tp_rgb(C_DIRT_LT);
    const uint16_t md = tp_rgb(C_DIRT_MD), dk = tp_rgb(C_DIRT_DK);
    const uint16_t ol = tp_rgb(C_DIRT_OL);
    for (int dy = -oy; dy <= oy; dy++) {
        for (int dx = -ox; dx <= ox; dx++) {
            if (HM(dx, dy) != 1) {
                continue;
            }
            bool edge = HM(dx - 1, dy) == 0 || HM(dx + 1, dy) == 0 ||
                        HM(dx, dy - 1) == 0 || HM(dx, dy + 1) == 0;
            int k = (dx * dx * 256 / (mrx * mrx) + dy * dy * 256 / (mry * mry)) * 100 / 256;
            uint16_t c;
            if (edge) {
                c = ol;
            } else if (dy > 0) {
                c = k < 50 ? lt : (k < 80 ? md : dk);       /* front slope */
            } else {
                c = k < 52 ? hi : (k < 82 ? lt : md);       /* lit back    */
            }
            uint32_t h = hash32(seed + (uint32_t)((dx + 64) * 31 + (dy + 64) * 977));
            if (!edge && h % 11u == 0) {
                c = (h & 16) ? tp_rgb(C_SPECK_L) : tp_rgb(C_SPECK_D);
            }
            tp_px(b, cx + dx, cy + dy, c);
        }
    }

    /* the opening, column by column so the lip table matches it exactly */
    const uint16_t pit = tp_rgb(C_PIT);
    const uint16_t wall = tp_rgb(C_PIT_WALL), wall2 = tp_rgb(C_PIT_WALL2);
    const uint16_t lipc = tp_rgb(C_LIP);
    for (int dx = -orx; dx <= orx; dx++) {
        int e = open_e(dx, orx, ory);
        for (int dy = -e; dy <= e; dy++) {
            int from_back = dy + e;
            tp_px(b, cx + dx, cy + dy,
                  from_back == 0 ? wall : (from_back == 1 && e > 1 ? wall2 : pit));
        }
        tp_px(b, cx + dx, cy + e + 1, lipc);
        lip[dx + orx] = (int16_t)(cy + e + 1);
    }

    /* crumbs thrown on the grass round the front */
    for (int k = 0; k < 7; k++) {
        uint32_t h = hash32(seed * 31u + (uint32_t)k * 7777u);
        int dx = (int)(h % (uint32_t)(2 * mrx + 8)) - mrx - 4;
        int dy = mry + 1 + (int)((h >> 8) % 4u);
        if (HM(dx, dy) == 0) {
            tp_px(b, cx + dx, cy + dy, (h & 0x10000) ? md : dk);
        }
    }
#undef HM
}

/* --------------------------------------------------------------------------
 * The pen: paints, or only measures
 * -------------------------------------------------------------------------- */

typedef struct {
    tp_buf_t  *b;           /* NULL: measuring                              */
    tp_rect_t  box;
    bool       any;
} pen_t;

static void pen_box(pen_t *pn, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (!pn->any) {
        pn->box.x0 = (int16_t)x0;
        pn->box.y0 = (int16_t)y0;
        pn->box.x1 = (int16_t)x1;
        pn->box.y1 = (int16_t)y1;
        pn->any = true;
        return;
    }
    if (x0 < pn->box.x0) pn->box.x0 = (int16_t)x0;
    if (y0 < pn->box.y0) pn->box.y0 = (int16_t)y0;
    if (x1 > pn->box.x1) pn->box.x1 = (int16_t)x1;
    if (y1 > pn->box.y1) pn->box.y1 = (int16_t)y1;
}

static void pen_img(pen_t *pn, int id, int x, int y, int s)
{
    if (pn->b) {
        tp_img_draw(pn->b, id, x, y, s);
    } else {
        tp_rect_t r;
        tp_img_rect(id, x, y, s, &r);
        pen_box(pn, r.x0, r.y0, r.x1, r.y1);
    }
}

static void pen_rect(pen_t *pn, int x, int y, int w, int h, uint16_t c)
{
    if (pn->b) {
        tp_rect(pn->b, x, y, w, h, c);
    } else {
        pen_box(pn, x, y, x + w, y + h);
    }
}

static void pen_disc(pen_t *pn, int cx, int cy, int r, uint16_t c)
{
    if (r < 0) {
        return;
    }
    if (pn->b) {
        tp_disc(pn->b, cx, cy, r, c);
    } else {
        pen_box(pn, cx - r, cy - r, cx + r + 1, cy + r + 1);
    }
}

static void pen_disc_mix(pen_t *pn, int cx, int cy, int r, uint16_t c, int f)
{
    if (r < 0 || f <= 0) {
        return;
    }
    if (pn->b) {
        tp_disc_mix(pn->b, cx, cy, r, c, f);
    } else {
        pen_box(pn, cx - r, cy - r, cx + r + 1, cy + r + 1);
    }
}

static void pen_wave(pen_t *pn, int cx, int cy, int r, int th, uint16_t c, int f)
{
    if (r <= 0 || th <= 0 || f <= 0) {
        return;
    }
    if (pn->b) {
        tp_wave(pn->b, cx, cy, r, th, c, f);
    } else {
        pen_box(pn, cx - r - th, cy - r - th, cx + r + th + 1, cy + r + th + 1);
    }
}

static void pen_line(pen_t *pn, int x0, int y0, int x1, int y1, uint16_t c)
{
    if (pn->b) {
        tp_line(pn->b, x0, y0, x1, y1, c);
    } else {
        pen_box(pn, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1,
                (x0 > x1 ? x0 : x1) + 1, (y0 > y1 ? y0 : y1) + 1);
    }
}

static void pen_text(pen_t *pn, int x, int y, const char *s,
                     uint16_t fill, uint16_t ol)
{
    if (pn->b) {
        tp_text_ol(pn->b, x, y, s, fill, ol, 1);
    } else {
        /* the outline ring and the drop shadow two rows down */
        pen_box(pn, x - 1, y - 1, x + tp_text_w(s, 1) + 1, y + TP_CH_H + 3);
    }
}

static void pen_lip(pen_t *pn, const int16_t *tab, int x0, int n, int def)
{
    if (pn->b) {
        tp_lip(pn->b, tab, x0, n, def);
    }
}

static void pen_lip_off(pen_t *pn)
{
    if (pn->b) {
        tp_lip_off(pn->b);
    }
}

/* --------------------------------------------------------------------------
 * Painters
 * -------------------------------------------------------------------------- */

/* A mole, at scale s (1 in the game, 2 on the title). p->x/p->y is the
 * hole's centre; everything is placed from the head's top, which the rise
 * gives. */
static void paint_mole(pen_t *pn, const tp_dp_t *p, int s,
                       const int16_t *lip, int lip_x0, int lip_n)
{
    const int hx    = p->x;
    const int cx    = p->x + p->w0 * s;         /* the taunt's wiggle      */
    const int cy    = p->y;
    const int body  = p->v[1];
    const bool sq   = body == IMG_BODY_SQ || body == IMG_BODY_GOLD_SQ;
    const int top   = cy + (10 - (int8_t)p->v[0]) * s + (sq ? TP_SQUASH_DY * s : 0);
    const int flags = p->v[4];
    const int fr    = p->v[5];

    pen_lip(pn, lip, lip_x0, lip_n, cy);
    pen_img(pn, body, cx, top, s);
    pen_img(pn, IMG_EYES0 + p->v[2], cx, top, s);
    pen_img(pn, IMG_MOUTH0 + p->v[3], cx, top, s);
    if (flags & MF_HELMET) {
        pen_img(pn, IMG_HELMET0, cx, top + 2 * s, s);
    }
    if (flags & MF_SWEAT) {
        pen_img(pn, IMG_SWEAT, cx + 10 * s, top + 2 * s, s);
    }
    pen_lip_off(pn);

    if (flags & MF_PAWS) {
        pen_img(pn, IMG_PAW_L, hx - 12 * s, cy + 4 * s, s);
        pen_img(pn, IMG_PAW_R, hx + 12 * s, cy + 4 * s, s);
    }
    if (flags & MF_STARS) {
        /* three stars orbiting over the head; the ones going behind it are
         * the small ones, which is all the depth this needs */
        for (int k = 0; k < 3; k++) {
            int a  = fr * 8 + k * 85;
            int sx = cx + tp_cos(a) * 14 * s / 256;
            int sy = top - 3 * s + tp_sin(a) * 3 * s / 256;
            pen_img(pn, tp_sin(a) < 0 ? IMG_STAR_SMALL : IMG_STAR, sx, sy, s);
        }
    }
    if (flags & MF_TWINKLE) {
        static const int8_t tw[3][2] = { { -16, 6 }, { 15, 12 }, { -12, 22 } };
        for (int k = 0; k < 3; k++) {
            int f = (fr + k) % 3;
            if (f < TP_TWINKLE_FRAMES) {
                pen_img(pn, IMG_TWINKLE0 + f, cx + tw[k][0] * s, top + tw[k][1] * s, s);
            }
        }
    }
}

/* The fuse, from the cap up and to the right, 'n' of its 12 points left. */
static const int8_t FUSE[12][2] = {
    { 0, 0 }, { 0, -1 }, { 1, -2 }, { 2, -3 }, { 3, -4 }, { 4, -4 },
    { 5, -5 }, { 6, -5 }, { 7, -6 }, { 8, -6 }, { 9, -6 }, { 10, -7 },
};

static void paint_fuse(pen_t *pn, int x, int y, int n, int spark)
{
    const uint16_t ol = tp_rgb(0x2A1A0C);
    const uint16_t ca = tp_rgb(0xD9B27A), cb = tp_rgb(0x8A6438);
    if (n > 12) {
        n = 12;
    }
    for (int k = 0; k < n; k++) {
        pen_rect(pn, x + FUSE[k][0] - 1, y + FUSE[k][1] - 1, 3, 3, ol);
    }
    for (int k = 0; k < n; k++) {
        pen_rect(pn, x + FUSE[k][0], y + FUSE[k][1], 1, 1, (k & 1) ? ca : cb);
    }
    if (n > 0 && spark < TP_SPARK_FRAMES) {
        pen_img(pn, IMG_SPARK0 + spark, x + FUSE[n - 1][0] + 1,
                y + FUSE[n - 1][1] - 1, 1);
    }
}

static void paint_bomb(pen_t *pn, const tp_dp_t *p, const int16_t *lip,
                       int lip_x0, int lip_n)
{
    const int cx  = p->x + p->w0;               /* the last stretch shakes */
    const int bcy = TP_BOMB_CY(p->y, (int8_t)p->v[0]);

    pen_lip(pn, lip, lip_x0, lip_n, p->y);
    pen_img(pn, IMG_BOMB + p->v[3], cx, bcy, 1);
    paint_fuse(pn, cx, bcy - 15, p->v[1], p->v[2]);
    pen_lip_off(pn);
}

static void paint_title_bomb(pen_t *pn, const tp_dp_t *p)
{
    pen_img(pn, IMG_BOMB, p->x, p->y, 1);
    paint_fuse(pn, p->x, p->y - 15, p->v[1], p->v[2]);
}

/* A bomb going off, frame by frame (38 ms each, 20 frames):
 *   0-1   white flash
 *   1-8   the fireball grows in lobes, white-yellow core to red edge
 *   2-9   a shock ring runs out and fades
 *   6-19  it turns to smoke that rises and thins out
 *   1-14  debris flies out and falls */
static void paint_boom(pen_t *pn, const tp_dp_t *p)
{
    const int x = p->x, y = p->y, f = p->v[0], seed = p->v[1];
    const uint16_t W = tp_rgb(0xFFFFFF), Y = tp_rgb(0xFFE14D);
    const uint16_t O = tp_rgb(0xFF8A1E), R = tp_rgb(0xE8401C);
    const uint16_t D = tp_rgb(0x9A2412);
    const uint16_t S1 = tp_rgb(0xA4A8B2), S2 = tp_rgb(0x626670);

    /* The smoke goes under the fire, dense at first: the first version
     * started it at half strength and the blast ended in a flat red disc
     * that simply vanished. */
    if (f >= 6) {
        int k2 = f - 6;
        for (int k = 0; k < 5; k++) {
            int a  = seed * 3 + k * 51;
            int sx = x + tp_cos(a) * (7 + k2) / 256;
            int sy = y - 4 - k2 * 2 + tp_sin(a) * 4 / 256;
            int r  = 5 + k2 / 2 + (k & 1);
            pen_disc_mix(pn, sx, sy, r, (k & 1) ? S1 : S2, 14 - k2);
        }
    }
    if (f <= 1) {
        pen_disc(pn, x, y, 9 + f * 3, W);
        pen_disc(pn, x, y, 5 + f * 2, Y);
    } else if (f < 10) {
        /* Three layers in every frame -dark lobes, a smaller ring of bright
         * ones, a core- each cooling a step behind the one inside it. It
         * grows to frame 6 and then shrinks into the smoke. */
        int r  = f <= 6 ? 8 + f * 2 : 20 - (f - 6) * 3;
        int up = f / 2;
        const uint16_t outer = f < 4 ? O : (f < 6 ? R : D);
        const uint16_t mid   = f < 4 ? Y : (f < 7 ? O : R);
        const uint16_t core  = f < 5 ? W : (f < 7 ? Y : O);
        for (int k = 0; k < 7; k++) {
            int a  = seed + k * 37;
            int lx = x + tp_cos(a) * r / 512;
            int ly = y - up + tp_sin(a) * r / 512;
            pen_disc(pn, lx, ly, r * 50 / 100 + (k & 1), outer);
        }
        for (int k = 0; k < 5; k++) {
            int a  = seed * 7 + k * 51 + 20;
            int lx = x + tp_cos(a) * r / 700;
            int ly = y - up + tp_sin(a) * r / 700;
            pen_disc(pn, lx, ly, r * 36 / 100, mid);
        }
        pen_disc(pn, x, y - up, r * 30 / 100, core);
    }
    if (f >= 2 && f <= 9) {
        pen_wave(pn, x, y, 10 + (f - 2) * 5, 2, W, 11 - (f - 2));
    }
    if (f >= 1 && f < 15) {
        for (int k = 0; k < 7; k++) {
            int a  = seed * 5 + k * 36 + 10;
            int v  = 3 + k % 3;
            int dx = tp_cos(a) * v * f / 256;
            int dy = tp_sin(a) * v * f / 256 - 2 * f + f * f / 4;
            pen_rect(pn, x + dx, y + dy, 2, 2,
                     (k & 1) ? tp_rgb(0x2A2E3A) : tp_rgb(0x5A3418));
        }
    }
}

/* The fizzle's puff: three grey balls rising, growing and thinning. */
static void paint_smoke(pen_t *pn, const tp_dp_t *p)
{
    const int f = p->v[0], seed = p->v[1];
    const uint16_t S1 = tp_rgb(0xC4C8D0), S2 = tp_rgb(0x80848E);
    for (int k = 0; k < 3; k++) {
        int fk = f - k * 2;
        if (fk < 0) {
            continue;
        }
        int r  = 2 + fk / 2;
        int sx = p->x + ((k & 1) ? 3 : -2) + (fk / 4) * (((seed >> k) & 1) ? 1 : -1);
        int sy = p->y - fk * 2 - k * 2;
        pen_disc_mix(pn, sx, sy, r, (k & 1) ? S1 : S2, 14 - fk);
    }
}

static void paint_popup(pen_t *pn, const tp_dp_t *p)
{
    static const uint32_t col[] = { 0xFFFFFF, 0xFFD83A, 0xFF4A3A, 0xFF9A1E, 0x7FE8FF };
    char buf[12];
    char *e = buf;
    int v = p->w0;

    if (p->v[2] == POP_MULT) {
        *e++ = 'X';
    } else {
        *e++ = v < 0 ? '-' : '+';
    }
    e = tp_num(e, (uint32_t)(v < 0 ? -v : v), 1);
    if (p->v[2] == POP_SECS) {
        *e++ = 'S';                 /* seconds: a unit, not a word */
        *e = '\0';
    }

    int w  = tp_text_w(buf, 1);
    int tx = p->x - w / 2;
    if (tx < 3) tx = 3;
    if (tx > TP_W - w - 3) tx = TP_W - w - 3;
    int ty = p->y - p->v[0];
    uint16_t c = tp_rgb(col[p->v[1] < 5 ? p->v[1] : 0]);
    pen_text(pn, tx, ty, buf, c, tp_rgb(C_TEXT_OL));
}

/* A whack: eight rays that jump out and go from white to yellow. */
static void paint_rays(pen_t *pn, const tp_dp_t *p)
{
    const int f = p->v[0];
    const uint16_t c = f < 2 ? tp_rgb(0xFFFFFF) : tp_rgb(0xFFE14D);
    int r0 = 7 + f * 2, r1 = r0 + 4 - f;
    for (int k = 0; k < 8; k++) {
        int a  = k * 32 + 16;
        int x0 = p->x + tp_cos(a) * r0 / 256, y0 = p->y + tp_sin(a) * r0 / 256 / 2;
        int x1 = p->x + tp_cos(a) * r1 / 256, y1 = p->y + tp_sin(a) * r1 / 256 / 2;
        pen_line(pn, x0, y0, x1, y1, c);
        pen_line(pn, x0 + 1, y0, x1 + 1, y1, c);
    }
}

/* The mallet on the hard hat: short sparks, upwards, and a bright star. */
static void paint_clang(pen_t *pn, const tp_dp_t *p)
{
    const int f = p->v[0];
    const uint16_t w = tp_rgb(0xFFFFFF), c = tp_rgb(0xBFF4FF);
    for (int k = 0; k < 6; k++) {
        int a  = 128 + 16 + k * 19;             /* the upper half */
        int r0 = 4 + f * 3, r1 = r0 + 3;
        pen_line(pn, p->x + tp_cos(a) * r0 / 256, p->y + tp_sin(a) * r0 / 256,
                 p->x + tp_cos(a) * r1 / 256, p->y + tp_sin(a) * r1 / 256,
                 (k & 1) ? w : c);
    }
    if (f < 2) {
        pen_rect(pn, p->x - 1, p->y - 3, 3, 7, w);
        pen_rect(pn, p->x - 3, p->y - 1, 7, 3, w);
    }
}

/* A tap on nothing: three puffs of dust. */
static void paint_dust(pen_t *pn, const tp_dp_t *p)
{
    const int f = p->v[0];
    const uint16_t c = tp_rgb(0xEEDCB4);
    int fade = 12 - f * 3 / 2;
    pen_disc_mix(pn, p->x - 4 - f / 2, p->y + 1, 1 + f / 2, c, fade);
    pen_disc_mix(pn, p->x + 4 + f / 2, p->y + 1, 1 + f / 2, c, fade);
    pen_disc_mix(pn, p->x, p->y - 1 - f / 3, 1 + f / 3, c, fade);
}

/* Dirt thrown up by something coming out: six crumbs on a parabola. */
static void paint_crumbs(pen_t *pn, const tp_dp_t *p)
{
    const int f = p->v[0], seed = p->v[1];
    static const uint32_t col[3] = { C_DIRT_MD, C_DIRT_DK, C_DIRT_LT };
    for (int k = 0; k < 6; k++) {
        int vx = (int)((seed * (k + 3) + k * 5) % 9) - 4;
        int vy = -(3 + k % 3);
        int x  = p->x + (k - 3) * 3 + vx * f / 2;
        int y  = p->y + vy * f + f * f / 4;
        if (y > p->y + 3) {
            continue;                       /* back on the ground */
        }
        pen_rect(pn, x, y, (k & 1) + 1, 1 + (k & 1), tp_rgb(col[k % 3]));
    }
}

static void paint(pen_t *pn, const tp_dp_t *p)
{
    switch (p->kind) {
    case DK_MOLE:
        paint_mole(pn, p, 1, s_lip[p->v[6]], p->x - TP_OPEN_RX, 2 * TP_OPEN_RX + 1);
        break;
    case DK_BOMB:
        paint_bomb(pn, p, s_lip[p->v[6]], p->x - TP_OPEN_RX, 2 * TP_OPEN_RX + 1);
        break;
    case DK_TITLE_MOLE:
        paint_mole(pn, p, 2, s_tlip, p->x - TITLE_ORX, 2 * TITLE_ORX + 1);
        break;
    case DK_TITLE_BOMB:
        paint_title_bomb(pn, p);
        break;
    case DK_FX_HELMET:
        pen_img(pn, IMG_HELMET0 + (p->v[0] & (TP_HELMET_FRAMES - 1)), p->x, p->y, 1);
        break;
    case DK_FX_BOOM:
        paint_boom(pn, p);
        break;
    case DK_FX_SMOKE:
        paint_smoke(pn, p);
        break;
    case DK_FX_POPUP:
        paint_popup(pn, p);
        break;
    case DK_FX_RAYS:
        paint_rays(pn, p);
        break;
    case DK_FX_CLANG:
        paint_clang(pn, p);
        break;
    case DK_FX_DUST:
        paint_dust(pn, p);
        break;
    case DK_FX_CRUMBS:
        paint_crumbs(pn, p);
        break;
    case DK_MALLET:
        pen_img(pn, IMG_MALLET0 + p->v[0], p->x, p->y, 1);
        break;
    default:
        break;
    }
}

/* Which pass a slot is painted in: occupants, then effects, then the mallet,
 * and the score popups on top of everything so they are always read. */
static int layer_of(int kind)
{
    switch (kind) {
    case DK_MOLE:
    case DK_BOMB:
    case DK_TITLE_MOLE:
    case DK_TITLE_BOMB:
        return 0;
    case DK_MALLET:
        return 2;
    case DK_FX_POPUP:
        return 3;
    default:
        return 1;
    }
}

void tp_slot_box(const tp_dp_t *p, tp_rect_t *out, bool *any)
{
    pen_t pn = { 0 };
    paint(&pn, p);
    *out = pn.box;
    *any = pn.any;
}

void tp_slot_paint(tp_buf_t *b, const tp_dp_t *p)
{
    pen_t pn = { 0 };
    pn.b = b;
    paint(&pn, p);
    tp_lip_off(b);
}

/* --------------------------------------------------------------------------
 * From the game's state to slots
 * -------------------------------------------------------------------------- */

static void idle_face(const tp_hole_t *h, int *ex, int *mo)
{
    static const uint8_t look[6] = {
        EX_NORMAL, EX_LEFT, EX_NORMAL, EX_RIGHT, EX_NORMAL, EX_NORMAL
    };
    if (h->occ == OCC_HELMET && h->helmet) {
        *ex = EX_SMUG;
        *mo = MO_SMIRK;
    } else if (h->occ == OCC_HELMET) {
        *ex = EX_ANGRY;                     /* hat gone, and furious */
        *mo = MO_GRIT;
    } else {
        *ex = look[((h->up_t + h->seed * 37) / 420) % 6];
        *mo = MO_TEETH;
    }
    if ((h->up_t + h->seed * 53) % 1700 < 110) {
        *ex = EX_BLINK;
    }
}

static void build_hole(const tp_game_t *g, int i, tp_slot_t *s)
{
    const tp_hole_t *h = &g->holes[i];
    tp_dp_t *p = &s->p;

    if (h->occ == OCC_NONE || h->phase == PH_EMPTY || h->phase == PH_COOL ||
        h->rise <= 0) {
        return;
    }
    s->vis = 1;
    p->x    = (int16_t)TP_COL_X(i % TP_COLS);
    p->y    = (int16_t)TP_ROW_Y(i / TP_COLS);
    p->v[0] = (uint8_t)h->rise;
    p->v[6] = (uint8_t)i;

    if (h->occ == OCC_BOMB) {
        int n = 12, spark = 255, variant = 0, shake = 0;
        switch (h->phase) {
        case PH_RISE:
            spark = (h->t / 70) % TP_SPARK_FRAMES;
            break;
        case PH_UP: {
            int fm = h->fuse_ms ? h->fuse_ms : 1;
            n = 12 - h->fuse_t * 11 / fm;
            spark = (h->fuse_t / 70) % TP_SPARK_FRAMES;
            /* The last 45% blinks red and shakes: the telegraph that it is
             * about to go out, and that it is still dangerous. */
            if (h->fuse_t * 100 / fm >= 55) {
                variant = ((h->fuse_t / 130) & 1) ? 1 : 0;
                shake   = ((h->fuse_t / 50) & 1) ? 1 : -1;
            }
            break;
        }
        default:                            /* FIZZLE, SINK: a dud */
            n = 1;
            variant = 2;
            break;
        }
        p->kind = DK_BOMB;
        p->v[1] = (uint8_t)n;
        p->v[2] = (uint8_t)spark;
        p->v[3] = (uint8_t)variant;
        p->w0   = (int16_t)shake;
        return;
    }

    const bool gold = h->occ == OCC_GOLD;
    int body = gold ? IMG_BODY_GOLD : IMG_BODY;
    int ex = EX_NORMAL, mo = MO_TEETH, flags = 0, fr = 0, wig = 0;
    if (h->helmet) {
        flags |= MF_HELMET;
    }

    switch (h->phase) {
    case PH_RISE:
        if (h->rise < TP_MOLE_RISE * 2 / 3) {
            ex = EX_WIDE;                   /* peeking out */
            mo = MO_O;
        } else if (h->helmet) {
            ex = EX_SMUG;
            mo = MO_SMIRK;
        }
        if (h->rise >= TP_MOLE_RISE - 3) {
            flags |= MF_PAWS;
        }
        break;
    case PH_UP:
        idle_face(h, &ex, &mo);
        flags |= MF_PAWS;
        if (gold) {
            flags |= MF_TWINKLE;
            fr = (h->up_t / 120) % 3;
        }
        break;
    case PH_BONK:
        ex = EX_WIDE;
        mo = MO_O;
        flags |= MF_PAWS | MF_SWEAT;
        break;
    case PH_TAUNT:
        ex = EX_HAPPY;
        mo = MO_TONGUE;
        flags |= MF_PAWS;
        wig = ((h->t / 80) & 1) ? 1 : -1;
        break;
    case PH_HIT:
        if (h->t < 220) {
            body = gold ? IMG_BODY_GOLD_SQ : IMG_BODY_SQ;
        }
        ex = EX_DIZZY;
        mo = MO_OPEN;
        flags |= MF_STARS;
        fr = (h->t / 45) & 31;
        break;
    case PH_SINK:
        if (h->was_hit) {
            ex = EX_DIZZY;
            mo = MO_OPEN;
            /* not over an empty hole: once the head is nearly in, they go */
            if (h->rise > 16) {
                flags |= MF_STARS;
            }
            fr = ((TP_HIT_MS + h->t) / 45) & 31;    /* the stars keep turning */
        } else {
            ex = EX_HAPPY;
            mo = MO_TONGUE;
        }
        break;
    default:
        break;
    }
    p->kind = DK_MOLE;
    p->v[1] = (uint8_t)body;
    p->v[2] = (uint8_t)ex;
    p->v[3] = (uint8_t)mo;
    p->v[4] = (uint8_t)flags;
    p->v[5] = (uint8_t)fr;
    p->w0   = (int16_t)wig;
}

static void build_fx(const tp_fx_t *f, tp_slot_t *s)
{
    tp_dp_t *p = &s->p;
    if (f->kind == FX_NONE) {
        return;
    }
    s->vis = 1;
    p->x = f->x;
    p->y = f->y;

    switch (f->kind) {
    case FX_HELMET:
        p->kind = DK_FX_HELMET;
        p->x    = (int16_t)(f->x >> 4);
        p->y    = (int16_t)(f->y >> 4);
        p->v[0] = (uint8_t)(((f->a + 16) >> 5) & (TP_HELMET_FRAMES - 1));
        break;
    case FX_BOOM:
        p->kind = DK_FX_BOOM;
        p->v[0] = (uint8_t)(f->t / 38 > 19 ? 19 : f->t / 38);
        p->v[1] = f->seed;
        break;
    case FX_SMOKE:
        p->kind = DK_FX_SMOKE;
        p->v[0] = (uint8_t)(f->t / 50 > 15 ? 15 : f->t / 50);
        p->v[1] = f->seed;
        break;
    case FX_POPUP:
        p->kind = DK_FX_POPUP;
        p->v[0] = (uint8_t)(f->t / 45 > 17 ? 17 : f->t / 45);
        p->v[1] = f->c;
        p->v[2] = f->type;
        p->w0   = f->a;
        break;
    case FX_RAYS:
        p->kind = DK_FX_RAYS;
        p->v[0] = (uint8_t)(f->t / 38 > 3 ? 3 : f->t / 38);
        break;
    case FX_CLANG:
        p->kind = DK_FX_CLANG;
        p->v[0] = (uint8_t)(f->t / 45 > 3 ? 3 : f->t / 45);
        break;
    case FX_DUST:
        p->kind = DK_FX_DUST;
        p->v[0] = (uint8_t)(f->t / 40 > 7 ? 7 : f->t / 40);
        break;
    case FX_CRUMBS:
        p->kind = DK_FX_CRUMBS;
        p->v[0] = (uint8_t)(f->t / 33 > 14 ? 14 : f->t / 33);
        p->v[1] = f->seed;
        break;
    default:
        s->vis = 0;
        break;
    }
}

/* The title: the big mole in its hard hat glances about and blinks, and the
 * bomb's spark flickers. */
static void build_title(tp_game_t *g)
{
    tp_slot_t *m = &g->slot[TP_SLOT_TITLE0];
    tp_slot_t *b = &g->slot[TP_SLOT_TITLE0 + 1];
    static const uint8_t cyc[10] = {
        EX_SMUG, EX_SMUG, EX_SMUG, EX_LEFT, EX_SMUG,
        EX_SMUG, EX_RIGHT, EX_SMUG, EX_SMUG, EX_SMUG,
    };
    uint32_t t = g->title_t;
    int ex = cyc[(t / 600) % 10];
    if (t % 2600 < 120) {
        ex = EX_BLINK;
    }

    m->vis    = 1;
    m->p.kind = DK_TITLE_MOLE;
    m->p.x    = TITLE_HX;
    m->p.y    = TITLE_HY;
    m->p.v[0] = TITLE_RISE;
    m->p.v[1] = IMG_BODY;
    m->p.v[2] = (uint8_t)ex;
    m->p.v[3] = MO_SMIRK;
    m->p.v[4] = MF_HELMET | MF_PAWS;

    b->vis    = 1;
    b->p.kind = DK_TITLE_BOMB;
    b->p.x    = 30;
    b->p.y    = 96;
    b->p.v[1] = 9;
    b->p.v[2] = (uint8_t)((t / 70) % TP_SPARK_FRAMES);
}

static int field_y0(const tp_game_t *g)
{
    return g->state == GS_TITLE ? 0 : TP_HUD_H;
}

static void build_slots(tp_game_t *g)
{
    memset(g->slot, 0, sizeof(g->slot));

    if (g->state == GS_TITLE) {
        build_title(g);
    } else {
        for (int i = 0; i < TP_HOLES; i++) {
            build_hole(g, i, &g->slot[TP_SLOT_HOLE0 + i]);
        }
        for (int i = 0; i < TP_MAX_FX; i++) {
            build_fx(&g->fx[i], &g->slot[TP_SLOT_FX0 + i]);
        }
        if (g->mallet) {
            tp_slot_t *s = &g->slot[TP_SLOT_MALLET];
            /* on target at once -the tap has to feel instant- then lifting */
            s->vis    = 1;
            s->p.kind = DK_MALLET;
            s->p.x    = g->mx;
            s->p.y    = g->my;
            s->p.v[0] = (uint8_t)(g->mt < 110 ? 2 : (g->mt < 170 ? 1 : 0));
        }
    }

    const int y0 = field_y0(g);
    for (int i = 0; i < TP_SLOTS; i++) {
        tp_slot_t *s = &g->slot[i];
        if (!s->vis) {
            continue;
        }
        bool any;
        tp_slot_box(&s->p, &s->box, &any);
        if (s->box.x0 < 0) s->box.x0 = 0;
        if (s->box.y0 < y0) s->box.y0 = (int16_t)y0;
        if (s->box.x1 > TP_W) s->box.x1 = TP_W;
        if (s->box.y1 > TP_H) s->box.y1 = TP_H;
        if (!any || s->box.x1 <= s->box.x0 || s->box.y1 <= s->box.y0) {
            s->vis = 0;
        }
    }
}

/* --------------------------------------------------------------------------
 * The compositor
 * -------------------------------------------------------------------------- */

static void compose(tp_game_t *g, const tp_rect_t *r)
{
    tp_restore(g->fb.px, g->bg.px, r);
    tp_clip(&g->fb, r->x0, r->y0, r->x1, r->y1);

    pen_t pn = { 0 };
    pn.b = &g->fb;
    for (int layer = 0; layer < 4; layer++) {
        for (int i = 0; i < TP_SLOTS; i++) {
            const tp_slot_t *s = &g->slot[i];
            if (s->vis && layer_of(s->p.kind) == layer && tp_rect_hit(&s->box, r)) {
                paint(&pn, &s->p);
            }
        }
    }
    tp_lip_off(&g->fb);
    tp_clip_none(&g->fb);
}

static bool changed(const tp_slot_t *a, const tp_slot_t *b)
{
    if (a->vis != b->vis) {
        return true;
    }
    return a->vis && memcmp(&a->p, &b->p, sizeof(tp_dp_t)) != 0;
}

static void add_rect(tp_dirty_t *d, const tp_rect_t *r)
{
    tp_dirty_add(d, r->x0, r->y0, r->x1 - r->x0, r->y1 - r->y0);
}

/* --------------------------------------------------------------------------
 * The score's strip
 *
 * Painted over the lawn and pushed on its own, only when one of its values
 * changes. The field never paints up here (its clip starts at TP_HUD_H), so
 * nothing else has to restore it.
 * -------------------------------------------------------------------------- */

static void round_shade(tp_buf_t *b, int x, int y, int w, int h, int cut, int f)
{
    for (int yy = 0; yy < h; yy++) {
        int inset = 0;
        if (yy < cut) {
            inset = cut - yy;
        } else if (yy >= h - cut) {
            inset = cut - (h - 1 - yy);
        }
        for (int xx = inset; xx < w - inset; xx++) {
            tp_px_mix(b, x + xx, y + yy, 0x0000, f);
        }
    }
}

static void hud_draw(tp_game_t *g)
{
    tp_buf_t *b = &g->fb;
    const tp_rect_t hr = { 0, 0, TP_W, TP_HUD_H };
    const uint16_t white = tp_rgb(0xFFFFFF), ol = tp_rgb(C_TEXT_OL);
    const uint16_t red = tp_rgb(0xFF4A3A);
    char buf[16];

    tp_restore(b->px, g->bg.px, &hr);
    tp_clip(b, 0, 0, TP_W, TP_HUD_H);

    /* Pause, on the left. DELIBERATELY IN FROM THE EDGE: the glass has a
     * 38 px radius and the bezel eats more; Claude Jump's score read "_9M"
     * against the edge. The whole corner is the button (topos.c). */
    round_shade(b, 12, 7, 17, 16, 4, 7);
    tp_rect(b, 17, 11, 3, 8, white);
    tp_rect(b, 22, 11, 3, 8, white);

    /* score, centred, big */
    tp_num(buf, g->hud.score, 1);
    tp_text_ol(b, TP_W / 2 - tp_text_w(buf, 2) / 2, 6, buf,
               g->hud.flash == 2 ? red : white, ol, 2);

    if (g->hud.mult >= 2) {
        char m[6] = { 'X', 0 };
        tp_num(m + 1, g->hud.mult, 1);
        tp_text_ol(b, TP_W / 2 - tp_text_w(m, 1) / 2, 23, m,
                   tp_rgb(0xFF9A1E), ol, 1);
    }

    if (g->mode == MODE_SURVIVAL) {
        for (int k = 0; k < TP_LIVES; k++) {
            int id = k < g->hud.lives ? IMG_HEART : IMG_HEART_EMPTY;
            if (g->hud.flash == 2 && k == g->hud.lives) {
                id = IMG_HEART;             /* the one just lost blinks */
            }
            tp_img_draw(b, id, 140 + k * 11, 9, 1);
        }
    } else {
        tp_num(buf, (uint32_t)(g->hud.secs < 0 ? 0 : g->hud.secs), 1);
        int w  = tp_text_w(buf, 2);
        int tx = 170 - w;
        tp_img_draw(b, IMG_CLOCK, tx - 12, 9, 1);
        tp_text_ol(b, tx, 7, buf, g->hud.red ? red : white, ol, 2);
    }

    if (g->show_fps) {
        char f[12];
        char *e = tp_num(f, (uint32_t)(g->fps10 / 10), 1);
        *e++ = '.';
        tp_num(e, (uint32_t)(g->fps10 % 10), 1);
        tp_text(b, 34, 22, f, tp_rgb(0x7BE9FF), 1);
    }

    tp_clip_none(b);
}

static void hud_update(tp_game_t *g)
{
    uint32_t score = g->score;
    int16_t  secs  = (int16_t)((g->time_ms + 999) / 1000);
    uint8_t  red   = g->mode != MODE_SURVIVAL && secs <= 10 &&
                     (secs > 5 || ((g->time_ms / 250) & 1) == 0);
    uint8_t  flash = g->flash_ms ? (uint8_t)(((g->flash_ms / 100) & 1) + 1) : 0;

    if (g->mode == MODE_SURVIVAL) {
        secs = -1;
    }
    if (g->hud_valid && g->hud.score == score && g->hud.secs == secs &&
        g->hud.lives == g->lives && g->hud.mult == g->mult &&
        g->hud.red == red && g->hud.mode == g->mode && g->hud.flash == flash &&
        !(g->show_fps && g->hud_fps != g->fps10)) {
        return;
    }
    g->hud.score = score;
    g->hud.secs  = secs;
    g->hud.lives = g->lives;
    g->hud.mult  = g->mult;
    g->hud.red   = red;
    g->hud.mode  = g->mode;
    g->hud.flash = flash;
    g->hud_fps   = g->fps10;
    g->hud_valid = 1;
    hud_draw(g);
    g->hud_push = 1;
}

/* --------------------------------------------------------------------------
 * Public
 * -------------------------------------------------------------------------- */

void tp_bg_build(tp_game_t *g, bool title)
{
    tp_buf_t *b = &g->bg;
    tp_clip_none(b);
    tp_lip_off(b);
    lawn(b, title);

    if (title) {
        hole_draw(b, TITLE_HX, TITLE_HY, TITLE_MRX, TITLE_MRY,
                  TITLE_ORX, TITLE_ORY, 99u, s_tlip);
        /* the bomb's shadow on the grass, and the mallet waiting */
        for (int dx = -10; dx <= 10; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (dx * dx * 4 + dy * dy * 100 <= 400) {
                    tp_px_mix(b, 32 + dx, 108 + dy, tp_rgb(C_SHADOW), 7);
                }
            }
        }
        tp_img_draw(b, IMG_MALLET0, 138, 104, 1);
    } else {
        for (int i = 0; i < TP_HOLES; i++) {
            hole_draw(b, TP_COL_X(i % TP_COLS), TP_ROW_Y(i / TP_COLS),
                      TP_MOUND_RX, TP_MOUND_RY, TP_OPEN_RX, TP_OPEN_RY,
                      (uint32_t)(i * 7 + 3), s_lip[i]);
        }
    }

    memcpy(g->fb.px, b->px, (size_t)TP_W * TP_H * sizeof(uint16_t));
    memset(g->prev, 0, sizeof(g->prev));
    g->full      = 1;
    g->hud_valid = 0;
}

void tp_present(tp_game_t *g)
{
    const int y0 = field_y0(g);

    build_slots(g);
    tp_dirty_reset(&g->push);

    if (g->full) {
        g->full = 0;
        tp_rect_t all = { 0, (int16_t)y0, TP_W, TP_H };
        compose(g, &all);
        add_rect(&g->push, &all);
    } else {
        for (int i = 0; i < TP_SLOTS; i++) {
            const tp_slot_t *now = &g->slot[i], *was = &g->prev[i];
            if (!changed(now, was)) {
                continue;
            }
            if (was->vis) {
                add_rect(&g->push, &was->box);
            }
            if (now->vis) {
                add_rect(&g->push, &now->box);
            }
        }
        for (int i = 0; i < g->push.n; i++) {
            compose(g, &g->push.r[i]);
        }
    }
    memcpy(g->prev, g->slot, sizeof(g->slot));

    g->hud_push = 0;
    if (g->state != GS_TITLE) {
        hud_update(g);
    }
}

void tp_render_full(tp_game_t *g, uint16_t *out)
{
    const int y0 = field_y0(g);
    memcpy(out, g->bg.px, (size_t)TP_W * TP_H * sizeof(uint16_t));

    tp_buf_t b;
    tp_buf_init(&b, out, TP_W, TP_H);
    tp_clip(&b, 0, y0, TP_W, TP_H);

    pen_t pn = { 0 };
    pn.b = &b;
    for (int layer = 0; layer < 4; layer++) {
        for (int i = 0; i < TP_SLOTS; i++) {
            const tp_slot_t *s = &g->slot[i];
            if (s->vis && layer_of(s->p.kind) == layer) {
                paint(&pn, &s->p);
            }
        }
    }
    /* the score's strip is drawn apart: take it from fb as it is */
    memcpy(out, g->fb.px, (size_t)y0 * TP_W * sizeof(uint16_t));
}
