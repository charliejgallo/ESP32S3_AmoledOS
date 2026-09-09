/*
 * TRUCO - drawing of the cards and of the matchstick scoreboard. See
 * tr_cards.h.
 */
#include "tr_cards.h"
#include "tr_game.h"
#include "aos_theme.h"

#include <stdlib.h>
#include <string.h>

/* --- palette -------------------------------------------------------------- */

#define C_PAPER_T   0xFBF6E8        /* the card stock, at the top */
#define C_PAPER_B   0xE7DAB9        /* and at the bottom: old cards yellow */
#define C_EDGE      0x8C7C5C
#define C_INK       0x2B2620
#define C_GOLD      0xD9A62B
#define C_GOLD_D    0x8E6510
#define C_GOLD_L    0xF3D874
#define C_STEEL     0xC8CED6
#define C_STEEL_D   0x7E8790
#define C_WOOD      0x8A5A2B
#define C_WOOD_D    0x54341A
#define C_RED       0xA6291F
#define C_BLUE      0x2E4E8C
#define C_SKIN      0xEBC7A1
#define C_HAIR      0x46311F
#define C_BACK      0x7C1D18
#define C_BACK_L    0x9C2E26
#define C_BACK_D    0x5A100D

/* --- primitives ----------------------------------------------------------- */

static void rect(lv_layer_t *l, int x0, int y0, int x1, int y1,
                 uint32_t color, int radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa   = LV_OPA_COVER;
    d.radius   = radius;
    lv_area_t a = { x0, y0, x1, y1 };
    lv_draw_rect(l, &d, &a);
}

static void rect_opa(lv_layer_t *l, int x0, int y0, int x1, int y1,
                     uint32_t color, int radius, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa   = opa;
    d.radius   = radius;
    lv_area_t a = { x0, y0, x1, y1 };
    lv_draw_rect(l, &d, &a);
}

static void disc(lv_layer_t *l, int cx, int cy, int r, uint32_t color)
{
    rect(l, cx - r, cy - r, cx + r, cy + r, color, LV_RADIUS_CIRCLE);
}

static void ring(lv_layer_t *l, int cx, int cy, int r, int w, uint32_t color)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color       = lv_color_hex(color);
    d.width       = w;
    d.radius      = (uint16_t)r;
    d.center.x    = cx;
    d.center.y    = cy;
    d.start_angle = 0;
    d.end_angle   = 360;
    d.opa         = LV_OPA_COVER;
    lv_draw_arc(l, &d);
}

static void line(lv_layer_t *l, int x0, int y0, int x1, int y1, int w,
                 uint32_t color, bool round)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color       = lv_color_hex(color);
    d.width       = w;
    d.opa         = LV_OPA_COVER;
    d.round_start = round;
    d.round_end   = round;
    d.p1.x = x0; d.p1.y = y0;
    d.p2.x = x1; d.p2.y = y1;
    lv_draw_line(l, &d);
}

static void tri(lv_layer_t *l, int x0, int y0, int x1, int y1, int x2, int y2,
                uint32_t color)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_color_hex(color);
    d.opa   = LV_OPA_COVER;
    d.p[0].x = x0; d.p[0].y = y0;
    d.p[1].x = x1; d.p[1].y = y1;
    d.p[2].x = x2; d.p[2].y = y2;
    lv_draw_triangle(l, &d);
}

static void text(lv_layer_t *l, int x0, int y0, int x1, int y1, const char *s,
                 const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text  = s;                /* NOTE: the pointer is stored, not the string */
    d.font  = font;
    d.color = lv_color_hex(color);
    d.align = align;
    d.opa   = LV_OPA_COVER;
    lv_area_t a = { x0, y0, x1, y1 };
    lv_draw_label(l, &d, &a);
}

/* --- the four suits -------------------------------------------------------
 * Each one is drawn centred on (cx, cy) and 's' tall. The measurements go in
 * fractions of 's' so the same code serves both for the large symbol of an ace
 * and for the little ones on a seven.
 * ------------------------------------------------------------------------- */

static void suit_oro(lv_layer_t *l, int cx, int cy, int s)
{
    int r = s / 2;
    disc(l, cx, cy, r, C_GOLD_D);
    disc(l, cx, cy, r - 1, C_GOLD);
    ring(l, cx, cy, r - 2, 1, C_GOLD_L);

    if (r < 8) {
        disc(l, cx, cy, LV_MAX(1, r / 3), C_GOLD_D);
        return;
    }

    /* A minted coin: two rings, four cardinal marks between them and the dot
     * in the centre. No stars and no triangles: at 20 px they smudge together
     * and leave a blob. */
    ring(l, cx, cy, r - 4, 1, C_GOLD_D);
    disc(l, cx, cy, r - 6, C_GOLD_L);
    int a = r - 5, b = r - 3;
    rect(l, cx - 1, cy - b, cx + 1, cy - a, C_GOLD_D, 0);
    rect(l, cx - 1, cy + a, cx + 1, cy + b, C_GOLD_D, 0);
    rect(l, cx - b, cy - 1, cx - a, cy + 1, C_GOLD_D, 0);
    rect(l, cx + a, cy - 1, cx + b, cy + 1, C_GOLD_D, 0);
    disc(l, cx, cy, LV_MAX(1, r / 5), C_GOLD_D);
}

static void suit_copa(lv_layer_t *l, int cx, int cy, int s)
{
    int w  = (s * 7) / 10;              /* width of the mouth */
    int y0 = cy - s / 2;
    int bowl = (s * 9) / 20;

    /* the cup: a straight mouth at the top and a rounded belly below */
    rect(l, cx - w / 2, y0, cx + w / 2, y0 + bowl / 2, C_GOLD, 1);
    rect(l, cx - w / 2, y0 + bowl / 4, cx + w / 2, y0 + bowl, C_GOLD, w / 2);
    rect(l, cx - w / 2, y0, cx + w / 2, y0 + LV_MAX(1, s / 12), C_GOLD_L, 0);

    /* foot */
    int st = LV_MAX(2, s / 8);
    rect(l, cx - st / 2, y0 + bowl, cx + st / 2, cy + s / 3, C_GOLD_D, 0);
    if (s >= 16) disc(l, cx, y0 + bowl + s / 8, LV_MAX(2, s / 10), C_GOLD);
    rect(l, cx - w / 2, cy + s / 3, cx + w / 2, cy + s / 2, C_GOLD, LV_MAX(1, s / 12));
    rect(l, cx - w / 2, cy + s / 3, cx + w / 2, cy + s / 3 + 1, C_GOLD_D, 0);
}

static void suit_espada(lv_layer_t *l, int cx, int cy, int s)
{
    int y0 = cy - s / 2;                        /* point */
    int gy = cy + s / 5;                        /* the crossguard */
    int bw = LV_MAX(5, (s * 22) / 100);         /* width of the blade */
    int ty = y0 + LV_MAX(3, s / 5);             /* where the point ends */

    /* Blade: dark edge first and steel on top, which is the only thing that
     * makes it read as metal when it is 17 px. */
    tri(l, cx, y0, cx - bw / 2 - 1, ty, cx + bw / 2 + 1, ty, C_STEEL_D);
    tri(l, cx, y0 + 2, cx - bw / 2, ty, cx + bw / 2, ty, C_STEEL);
    rect(l, cx - bw / 2 - 1, ty - 1, cx + bw / 2 + 1, gy, C_STEEL_D, 0);
    rect(l, cx - bw / 2, ty - 1, cx + bw / 2, gy, C_STEEL, 0);
    rect(l, cx - 1, ty, cx, gy - 1, 0xEDF1F5, 0);       /* the edge */

    int gw = LV_MAX(9, (s * 62) / 100);
    int gh = LV_MAX(3, s / 9);
    rect(l, cx - gw / 2, gy, cx + gw / 2, gy + gh, C_GOLD_D, 1);
    rect(l, cx - gw / 2, gy, cx + gw / 2, gy + gh - 1, C_GOLD, 1);

    int hw = LV_MAX(3, s / 8);
    rect(l, cx - hw / 2, gy + gh, cx + hw / 2, cy + s / 2 - s / 8, C_WOOD_D, 1);
    disc(l, cx, cy + s / 2 - s / 10, LV_MAX(2, (s * 11) / 100), C_GOLD_D);
    disc(l, cx, cy + s / 2 - s / 10, LV_MAX(1, (s * 11) / 100 - 1), C_GOLD);
}

static void suit_basto(lv_layer_t *l, int cx, int cy, int s)
{
    int y0 = cy - s / 2, y1 = cy + s / 2;
    int w  = LV_MAX(5, (s * 32) / 100);         /* thick at the bottom */

    /* The cut branches go FIRST, so the trunk covers them on the inside and
     * they come out of the club instead of crossing it. */
    int br = LV_MAX(2, w / 3);
    int bl = (w * 3) / 4;
    line(l, cx - 1, cy + s / 6, cx - bl, cy + s / 6 - s / 12, br, C_WOOD_D, true);
    line(l, cx + 1, cy - s / 8, cx + bl, cy - s / 8 - s / 10, br, C_WOOD_D, true);

    /* The club tapers upwards: three sections with rounded ends come out
     * cheaper than a polygon and read just the same. */
    line(l, cx, y1, cx, cy + s / 8, w, C_WOOD_D, true);
    line(l, cx, y1 - 1, cx, cy + s / 8, w - 2, C_WOOD, true);
    int w2 = LV_MAX(4, (w * 8) / 10);
    line(l, cx, cy + s / 6, cx, cy - s / 8, w2, C_WOOD_D, true);
    line(l, cx, cy + s / 6 - 1, cx, cy - s / 8, w2 - 2, C_WOOD, true);
    int w3 = LV_MAX(3, (w * 6) / 10);
    line(l, cx, cy - s / 12, cx, y0, w3, C_WOOD_D, true);
    line(l, cx, cy - s / 12, cx, y0 + 1, w3 - 2 > 0 ? w3 - 2 : 1, C_WOOD, true);

    /* the knots, alternating: it is what makes it read as a trunk */
    if (s >= 14) {
        int k = LV_MAX(1, s / 12);
        disc(l, cx - w / 3, cy + s / 4, k, C_WOOD_D);
        disc(l, cx + w / 4, cy + s / 30, k, C_WOOD_D);
        disc(l, cx - w / 4, cy - s / 4, k, C_WOOD_D);
    }
}

static void suit(lv_layer_t *l, int s_id, int cx, int cy, int size)
{
    switch (s_id) {
    case TR_ORO:    suit_oro(l, cx, cy, size);    break;
    case TR_COPA:   suit_copa(l, cx, cy, size);   break;
    case TR_ESPADA: suit_espada(l, cx, cy, size); break;
    default:        suit_basto(l, cx, cy, size);  break;
    }
}

/* --- the face cards -------------------------------------------------------
 * Knave, knight and king in a box of ~44x64. At that size no portrait is worth
 * it: what is wanted is for each one to be told from the others at a glance,
 * and that is given by the crown, the horse in profile and the standing
 * figure.
 * ------------------------------------------------------------------------- */

static void figure_rey(lv_layer_t *l, int x0, int y0, int x1, int y1, int s_id)
{
    int cx = (x0 + x1) / 2;
    int w  = x1 - x0;
    int hy = y0 + (y1 - y0) / 3;            /* chin */

    /* cloak */
    tri(l, cx, hy, x0 + 1, y1, x1 - 1, y1, C_RED);
    rect(l, cx - w / 6, hy, cx + w / 6, y1, C_RED, 0);
    rect(l, cx - w / 8, hy + 4, cx + w / 8, y1, C_GOLD, 0);

    /* face and beard */
    disc(l, cx, hy - 6, 8, C_SKIN);
    rect(l, cx - 7, hy - 4, cx + 7, hy + 5, 0xDCD6C6, 4);      /* the beard */
    disc(l, cx, hy - 7, 7, C_SKIN);
    disc(l, cx - 3, hy - 8, 1, C_INK);
    disc(l, cx + 3, hy - 8, 1, C_INK);

    /* crown */
    int cy0 = hy - 14;
    rect(l, cx - 9, cy0, cx + 9, cy0 + 4, C_GOLD, 1);
    tri(l, cx - 9, cy0, cx - 4, cy0 - 7, cx + 1, cy0, C_GOLD);
    tri(l, cx - 1, cy0, cx + 4, cy0 - 7, cx + 9, cy0, C_GOLD);
    disc(l, cx - 4, cy0 - 7, 1, C_RED);
    disc(l, cx + 4, cy0 - 7, 1, C_RED);

    suit(l, s_id, x0 + 7, y1 - 9, 14);
}

static void figure_caballo(lv_layer_t *l, int x0, int y0, int x1, int y1, int s_id)
{
    int cx = (x0 + x1) / 2;
    int by = y0 + ((y1 - y0) * 2) / 3;       /* line of the back */

    /* body and legs */
    rect(l, x0 + 4, by - 8, x1 - 6, by + 4, C_WOOD, 5);
    rect(l, x0 + 7, by + 2, x0 + 10, y1, C_WOOD_D, 1);
    rect(l, x1 - 12, by + 2, x1 - 9, y1, C_WOOD_D, 1);

    /* neck and head in profile, facing left */
    tri(l, cx - 2, by - 6, cx + 8, by - 6, cx - 4, y0 + 10, C_WOOD);
    rect(l, x0 + 4, y0 + 6, cx + 1, y0 + 15, C_WOOD, 4);
    rect(l, x0 + 3, y0 + 10, x0 + 10, y0 + 15, C_WOOD_D, 2);
    disc(l, cx - 4, y0 + 10, 1, C_INK);
    tri(l, cx - 3, y0 + 6, cx, y0 + 1, cx + 2, y0 + 7, C_WOOD_D);

    /* mane and tail */
    for (int i = 0; i < 3; i++) {
        line(l, cx - 1 + i * 3, y0 + 6 + i, cx + 3 + i * 3, y0 + 13 + i * 2, 2,
             C_HAIR, true);
    }
    line(l, x1 - 6, by - 6, x1 - 3, by + 6, 3, C_HAIR, true);

    suit(l, s_id, x0 + 7, y1 - 9, 14);
}

static void figure_sota(lv_layer_t *l, int x0, int y0, int x1, int y1, int s_id)
{
    int cx = (x0 + x1) / 2;
    int w  = x1 - x0;
    int hy = y0 + (y1 - y0) / 3;

    /* tunic */
    tri(l, cx, hy + 2, x0 + 3, y1, x1 - 3, y1, C_BLUE);
    rect(l, cx - w / 7, hy, cx + w / 7, y1, C_BLUE, 0);
    line(l, cx - w / 5, hy + 6, cx - w / 3 + 2, hy + 16, 3, C_BLUE, true);

    /* head with hair */
    disc(l, cx, hy - 7, 8, C_HAIR);
    disc(l, cx, hy - 8, 6, C_SKIN);
    rect(l, cx - 7, hy - 15, cx + 7, hy - 10, C_HAIR, 3);
    disc(l, cx - 2, hy - 9, 1, C_INK);
    disc(l, cx + 2, hy - 9, 1, C_INK);

    /* cap */
    rect(l, cx - 8, hy - 17, cx + 8, hy - 13, C_RED, 2);
    tri(l, cx + 4, hy - 17, cx + 11, hy - 22, cx + 8, hy - 13, C_RED);

    suit(l, s_id, x0 + 7, y1 - 9, 14);
}

/* --- the whole card ------------------------------------------------------- */

static const char *const NUMTXT[13] = {
    "", "1", "2", "3", "4", "5", "6", "7", "", "", "10", "11", "12",
};

/* The "pinta": the card's frame carries as many cuts as the suit says, so you
 * can tell which suit it is by looking only at the edge of a closed fan. Coins
 * none, cups one, swords two and clubs three. */
static const uint8_t PINTA_CUTS[4] = { 2, 3, 0, 1 };    /* swords, clubs, coins, cups */

static void draw_frame(lv_layer_t *l, int x0, int y0, int x1, int y1, int cuts)
{
    const int t = 2;

    /* whole horizontals */
    rect(l, x0, y0, x1, y0 + t - 1, C_INK, 0);
    rect(l, x0, y1 - t + 1, x1, y1, C_INK, 0);

    /* verticals, cut in the middle according to the suit */
    int h = y1 - y0;
    if (cuts == 0) {
        rect(l, x0, y0, x0 + t - 1, y1, C_INK, 0);
        rect(l, x1 - t + 1, y0, x1, y1, C_INK, 0);
        return;
    }

    const int gap = 5;
    int span = cuts * gap + (cuts - 1) * 4;
    int gy0  = y0 + h / 2 - span / 2;

    for (int side = 0; side < 2; side++) {
        int sx0 = side ? x1 - t + 1 : x0;
        int sx1 = side ? x1 : x0 + t - 1;
        int y   = y0;
        for (int i = 0; i < cuts; i++) {
            int cut0 = gy0 + i * (gap + 4);
            if (cut0 > y) rect(l, sx0, y, sx1, cut0 - 1, C_INK, 0);
            y = cut0 + gap;
        }
        if (y <= y1) rect(l, sx0, y, sx1, y1, C_INK, 0);
    }
}

static void draw_pips(lv_layer_t *l, int s_id, int n, int x0, int y0, int x1, int y1)
{
    if (n == 1) {
        /* The ace goes large: it is half a card. Coins is the only one taking
         * up as much across as down, so that one has to be limited by the
         * width or it runs off the frame. */
        int big = s_id == TR_ORO ? LV_MIN(x1 - x0, ((y1 - y0) * 4) / 5)
                                 : ((y1 - y0) * 17) / 20;
        suit(l, s_id, (x0 + x1) / 2, (y0 + y1) / 2, big);
        return;
    }

    /* The sizes come from measuring the overlap: with three rows and a symbol
     * of 25 the points collide. The middle one on the 5 and the 7 goes smaller
     * still, which is also what real decks do. */
    int size = n == 2 ? 26 : n == 3 ? 21 : n <= 5 ? 20 : 17;
    int mid  = (size * 3) / 4;
    int lx = x0 + (x1 - x0) / 4 + 1;
    int rx = x1 - (x1 - x0) / 4 - 1;
    int cx = (x0 + x1) / 2;
    int h  = y1 - y0;
    int r2a = y0 + h / 4,     r2b = y1 - h / 4;
    int r3a = y0 + h / 6, r3b = y0 + h / 2, r3c = y1 - h / 6;

    switch (n) {
    case 2:
        suit(l, s_id, cx, r2a, size); suit(l, s_id, cx, r2b, size);
        break;
    case 3:
        suit(l, s_id, cx, r3a, size); suit(l, s_id, cx, r3b, size);
        suit(l, s_id, cx, r3c, size);
        break;
    case 4:
        suit(l, s_id, lx, r2a, size); suit(l, s_id, rx, r2a, size);
        suit(l, s_id, lx, r2b, size); suit(l, s_id, rx, r2b, size);
        break;
    case 5:
        suit(l, s_id, lx, r2a, size); suit(l, s_id, rx, r2a, size);
        suit(l, s_id, lx, r2b, size); suit(l, s_id, rx, r2b, size);
        suit(l, s_id, cx, (y0 + y1) / 2, mid);
        break;
    case 6:
        suit(l, s_id, lx, r3a, size); suit(l, s_id, rx, r3a, size);
        suit(l, s_id, lx, r3b, size); suit(l, s_id, rx, r3b, size);
        suit(l, s_id, lx, r3c, size); suit(l, s_id, rx, r3c, size);
        break;
    default:
        suit(l, s_id, lx, r3a, size); suit(l, s_id, rx, r3a, size);
        suit(l, s_id, lx, r3b, size); suit(l, s_id, rx, r3b, size);
        suit(l, s_id, lx, r3c, size); suit(l, s_id, rx, r3c, size);
        suit(l, s_id, cx, (y0 + y1) / 2, mid);
        break;
    }
}

static void draw_back(lv_layer_t *l, int x0, int y0, int x1, int y1)
{
    rect(l, x0, y0, x1, y1, C_BACK, 5);
    rect(l, x0 + 3, y0 + 3, x1 - 3, y1 - 3, C_BACK_D, 3);
    rect(l, x0 + 4, y0 + 4, x1 - 4, y1 - 4, C_BACK, 3);

    /* Diagonal lattice: two families of lines at 45 degrees. Since the slope
     * is exactly 1, clipping them against the inner rectangle is a matter of
     * moving the other end by as much as this one moved. */
    const int ix0 = x0 + 4, iy0 = y0 + 4, ix1 = x1 - 4, iy1 = y1 - 4;
    int w = ix1 - ix0, h = iy1 - iy0;

    for (int dir = 0; dir < 2; dir++) {
        for (int k = -h; k < w + h; k += 7) {
            int ax = ix0 + k, ay = iy0;
            int bx = dir ? ix0 + k - h : ix0 + k + h, by = iy1;
            int sgn = dir ? -1 : 1;
            if (ax < ix0) { ay += (ix0 - ax) * sgn; ax = ix0; }
            if (ax > ix1) { ay += (ax - ix1) * sgn; ax = ix1; }
            if (bx < ix0) { by -= (ix0 - bx) * sgn; bx = ix0; }
            if (bx > ix1) { by -= (bx - ix1) * sgn; bx = ix1; }
            if (ay < iy0 || ay > iy1 || by < iy0 || by > iy1) continue;
            if (ay < by) line(l, ax, ay, bx, by, 1, C_BACK_L, false);
        }
    }

    /* medallion */
    int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    disc(l, cx, cy, 13, C_BACK_D);
    ring(l, cx, cy, 12, 2, C_GOLD);
    ring(l, cx, cy, 7, 1, C_GOLD_D);
    disc(l, cx, cy, 3, C_GOLD);

    /* A 4 px border on top covers whatever the lattice ran outside the inner
     * rectangle, without having to clip line by line. */
    lv_draw_rect_dsc_t b;
    lv_draw_rect_dsc_init(&b);
    b.radius       = 5;
    b.bg_opa       = LV_OPA_TRANSP;
    b.border_color = lv_color_hex(C_BACK_D);
    b.border_width = 4;
    b.border_opa   = LV_OPA_COVER;
    lv_area_t fr = { x0, y0, x1, y1 };
    lv_draw_rect(l, &b, &fr);
}

lv_obj_t *tr_card_canvas(lv_obj_t *parent, void **buf_out)
{
    /* The large buffers through malloc(): with CONFIG_SPIRAM_USE_MALLOC they
     * go to PSRAM, of which there is plenty. And LV_DRAW_BUF_ALIGN is 4, so an
     * ordinary malloc is already enough. */
    void *buf = malloc((size_t)TR_CV_W * TR_CV_H * 4);
    if (!buf) return NULL;

    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, buf, TR_CV_W, TR_CV_H, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_size(cv, TR_CV_W, TR_CV_H);
    /* The theme gives everything a radius, and on a canvas that forces
     * clipping with a mask and drawing in layers: expensive and pointless
     * here, because the rounded corners are already drawn inside. */
    lv_obj_set_style_radius(cv, 0, 0);
    lv_image_set_antialias(cv, false);
    lv_obj_remove_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_fill_bg(cv, lv_color_hex(0), LV_OPA_TRANSP);

    if (buf_out) *buf_out = buf;
    return cv;
}

void tr_card_render(lv_obj_t *canvas, int card)
{
    if (!canvas) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0), LV_OPA_TRANSP);

    lv_layer_t l;
    lv_canvas_init_layer(canvas, &l);

    const int x0 = TR_PAD, y0 = TR_PAD;
    const int x1 = TR_PAD + TR_CARD_W - 1, y1 = TR_PAD + TR_CARD_H - 1;

    /* shadow + card stock, in a single lv_draw_rect */
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius        = 5;
    d.bg_color      = lv_color_hex(C_PAPER_T);
    d.bg_grad.dir   = LV_GRAD_DIR_VER;
    d.bg_grad.stops_count = 2;
    d.bg_grad.stops[0].color = lv_color_hex(card < 0 ? C_BACK : C_PAPER_T);
    d.bg_grad.stops[0].frac  = 0;
    d.bg_grad.stops[0].opa   = LV_OPA_COVER;
    d.bg_grad.stops[1].color = lv_color_hex(card < 0 ? C_BACK : C_PAPER_B);
    d.bg_grad.stops[1].frac  = 255;
    d.bg_grad.stops[1].opa   = LV_OPA_COVER;
    d.bg_opa        = LV_OPA_COVER;
    d.border_color  = lv_color_hex(C_EDGE);
    d.border_width  = 1;
    d.border_opa    = LV_OPA_COVER;
    d.shadow_color  = lv_color_hex(0x000000);
    d.shadow_width  = 7;
    d.shadow_offset_x = 2;
    d.shadow_offset_y = 3;
    d.shadow_opa    = 110;
    lv_area_t body = { x0, y0, x1, y1 };
    lv_draw_rect(&l, &d, &body);

    if (card < 0) {
        draw_back(&l, x0, y0, x1, y1);
        lv_canvas_finish_layer(canvas, &l);
        return;
    }

    int s_id = tr_suit((tr_card_t)card);
    int rank = tr_rank((tr_card_t)card);

    draw_frame(&l, x0 + 4, y0 + 4, x1 - 4, y1 - 4, PINTA_CUTS[s_id]);

    int ax0 = x0 + 8, ay0 = y0 + 24, ax1 = x1 - 8, ay1 = y1 - 7;

    if (rank >= 10) {
        if (rank == 12)      figure_rey(&l, ax0, ay0, ax1, ay1, s_id);
        else if (rank == 11) figure_caballo(&l, ax0, ay0, ax1, ay1, s_id);
        else                 figure_sota(&l, ax0, ay0, ax1, ay1, s_id);
    } else {
        draw_pips(&l, s_id, rank, ax0, ay0, ax1, ay1);
    }

    /* The number goes top left as on a real deck, and is drawn LAST: an ace
     * takes up half a card and otherwise it eats it. Only the ace needs the
     * card-stock plate behind it; on the rest it would look like a stuck-on
     * label. */
    if (rank == 1) rect_opa(&l, x0 + 6, y0 + 7, x0 + 24, y0 + 23, C_PAPER_T, 3, 210);
    text(&l, x0 + 7, y0 + 6, x0 + 34, y0 + 24, NUMTXT[rank], aos_font_small,
         C_INK, LV_TEXT_ALIGN_LEFT);

    lv_canvas_finish_layer(canvas, &l);
}

/* --- the matchstick scoreboard -------------------------------------------
 * Kept as at the table: every five points is a little square of four matches
 * plus the diagonal. Three squares are the malas (the first 15) and three the
 * buenas, in two rows separated by a line, which is exactly how it ends up as
 * you lay them out on the tablecloth.
 * ------------------------------------------------------------------------ */

#define M_LEN   20          /* length of the match */
#define M_STEP  25          /* from one square to the next */
#define M_ROW   22

static void match(lv_layer_t *l, int x0, int y0, int x1, int y1)
{
    /* stick with grain and head; the head goes at the (x0,y0) end */
    line(l, x0, y0 + 1, x1, y1 + 1, 4, 0x3A2A16, true);
    line(l, x0, y0, x1, y1, 3, 0xE3C48C, true);
    line(l, x0, y0, x1, y1, 1, 0xF6E4BE, false);
    disc(l, x0, y0, 3, 0x7E1B10);
    disc(l, x0, y0, 2, 0xC8412A);
    disc(l, x0 - 1, y0 - 1, 1, 0xE8836A);
}

/* A square of up to five: left, top, right, bottom and the diagonal. */
static void match_group(lv_layer_t *l, int x, int y, int n)
{
    const int s = M_LEN;
    if (n >= 1) match(l, x,     y + s, x,     y);           /* left      */
    if (n >= 2) match(l, x,     y,     x + s, y);           /* top       */
    if (n >= 3) match(l, x + s, y,     x + s, y + s);       /* right     */
    if (n >= 4) match(l, x + s, y + s, x,     y + s);       /* bottom    */
    if (n >= 5) match(l, x,     y,     x + s, y + s);       /* the fifth */
}

static void score_side(lv_layer_t *l, int x, int y, int pts)
{
    for (int g = 0; g < 6; g++) {
        int n = pts - g * 5;
        if (n <= 0) break;
        if (n > 5) n = 5;
        match_group(l, x + (g % 3) * M_STEP, y + (g / 3) * M_ROW, n);
    }
}

void tr_score_render(lv_obj_t *canvas, int nos, int ellos, int mano_yo, int valor)
{
    if (!canvas) return;

    lv_canvas_fill_bg(canvas, lv_color_hex(0), LV_OPA_TRANSP);

    lv_layer_t l;
    lv_canvas_init_layer(canvas, &l);

    /* two tablecloth cards so the matches do not float over the baize */
    rect_opa(&l, 6, 2, 156, TR_SCORE_H - 2, 0x000000, 10, 70);
    rect_opa(&l, 212, 2, 362, TR_SCORE_H - 2, 0x000000, 10, 70);

    text(&l, 6, 3, 156, 19, "NOS", aos_font_small, 0xE9E4D2, LV_TEXT_ALIGN_CENTER);
    text(&l, 212, 3, 362, 19, "ELLOS", aos_font_small, 0xE9E4D2, LV_TEXT_ALIGN_CENTER);

    /* The block of six squares is 2*M_STEP + M_LEN wide; centring it on the
     * card by hand stops the scoreboard drifting when sizes change. */
    const int blk = 2 * M_STEP + M_LEN;
    score_side(&l, 6 + (150 - blk) / 2, 22, nos);
    score_side(&l, 212 + (150 - blk) / 2, 22, ellos);

    /* the line between malas and buenas */
    rect_opa(&l, 14, 22 + M_ROW - 2, 148, 22 + M_ROW - 2, 0xF0D890, 0, 70);
    rect_opa(&l, 220, 22 + M_ROW - 2, 354, 22 + M_ROW - 2, 0xF0D890, 0, 70);

    /* In the middle, what the hand being played is worth and who is the mano. */
    static const char *const VAL[5] = { "", "1", "2", "3", "4" };
    int cx = TR_SCORE_W / 2;
    disc(&l, cx, 22, 15, 0x0B2E1B);
    ring(&l, cx, 22, 14, 2, 0xC9A227);
    text(&l, cx - 16, 13, cx + 16, 33, VAL[valor > 4 ? 4 : valor], aos_font_body,
         0xF2E7C7, LV_TEXT_ALIGN_CENTER);
    text(&l, cx - 30, 42, cx + 30, 60, mano_yo ? "MANO" : "PIE", aos_font_small,
         0xBFD8C4, LV_TEXT_ALIGN_CENTER);

    lv_canvas_finish_layer(canvas, &l);
}
