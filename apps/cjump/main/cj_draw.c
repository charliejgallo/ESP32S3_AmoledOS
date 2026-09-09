/*
 * CLAUDE JUMP - drawing
 *
 * Three blocks, and the separation matters:
 *
 *   cj_bg_build()    the whole sky into the 'bg' buffer. Only on opening the
 *                    app and on changing zone. It is the game's one expensive
 *                    frame.
 *   cj_draw_movers() what moves, onto 'fb', recording each rectangle.
 *   cj_draw_hud()    the score, only when a number changes.
 *
 * THE RULE THAT CANNOT BE BROKEN: everything drawn in cj_draw_movers() has to
 * record its rectangle with mark(). If something moves and does not record it,
 * nothing crashes: a trail stays stuck on the screen until the next full
 * repaint, and that only shows up after a while of playing.
 *
 * And a second, less obvious one: the dirty rectangles are clipped to the
 * playable strip (mark() does it). The score lives above that strip and
 * repaints itself; if a dirty rectangle reached it, the next frame's restore
 * from 'bg' would erase its numbers.
 */
#include "cjump.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Dirty rectangles
 * -------------------------------------------------------------------------- */

static void dirty_rect(cj_t *g, const cj_rect_t *r)
{
    int y = r->y0 < CJ_TOP ? CJ_TOP : r->y0;
    if (r->y1 <= y) {
        return;
    }
    cj_dirty_add(&g->d_cur, r->x0, y, r->x1 - r->x0, r->y1 - y);
}

/* Records the old and the new position of something that moved, and keeps the
 * new one. cj_dirty_add merges the two into a single rectangle whenever it
 * comes cheap, which with a displacement of a few pixels is always. */
static void mark(cj_t *g, cj_rect_t *prev, uint8_t *drawn,
                 int x, int y, int w, int h)
{
    if (*drawn) {
        dirty_rect(g, prev);
    }
    prev->x0 = (int16_t)x;
    prev->y0 = (int16_t)y;
    prev->x1 = (int16_t)(x + w);
    prev->y1 = (int16_t)(y + h);
    *drawn = 1;
    dirty_rect(g, prev);
}

/* World -> screen. The camera only moves the vertical axis. */
static inline int sx_of(int32_t wx)
{
    return (int)(wx / FX);
}

static inline int sy_of(const cj_t *g, int32_t wy)
{
    return (int)((wy - g->cam_y) / FX);
}

static bool on_screen(int y, int h)
{
    return y + h > CJ_TOP && y < CJ_H;
}

/* --------------------------------------------------------------------------
 * The sky
 *
 * The decorations are fixed in screen coordinates and do not scroll: that is
 * what lets the background be a copy and not a drawing (see cj_pixel.h). They
 * are rolled once per zone, so each game has its own sky.
 * -------------------------------------------------------------------------- */

static void clouds(cj_t *g, const cj_zone_t *z)
{
    uint16_t a = cj_rgb(z->deco_a);
    uint16_t b = cj_rgb(z->deco_b);

    /* Two things that had to be corrected by looking at the screen, not at the
     * code:
     *
     * 1. They go through cj_glow and not cj_disc, that is, BLENDED with the
     *    sky. With opaque discs, at sunset the clouds ended up as prominent as
     *    the platforms and the playing field became illegible.
     * 2. The shape is a HORIZONTAL capsule -discs in a row, larger in the
     *    middle- and not four overlapping circles. With overlapping circles
     *    and two different colours it did not come out as a cloud: it came out
     *    as a flower.
     */
    for (int i = 0; i < 5; i++) {
        int cx = cj_rand_range(g, 16, CJ_W - 16);
        int cy = cj_rand_range(g, 10, CJ_H - 16);
        int r  = cj_rand_range(g, 5, 8);
        int n  = cj_rand_range(g, 3, 5);

        for (int k = 0; k < n; k++) {
            /* the radius falls off towards the ends: 16/16 in the middle, 9/16
             * at the outside */
            int d  = k - n / 2;
            int dd = d < 0 ? -d : d;
            int rr = r - dd * r * 7 / (16 * (n / 2 + 1));
            cj_glow(&g->bg, cx + d * r, cy, rr, b, 9);
        }
        /* A lighter brushstroke on top, offset: it gives volume without
         * changing the silhouette. */
        cj_glow(&g->bg, cx - r / 2, cy - r / 2, r * 3 / 4, a, 8);
    }
}

static void stars(cj_t *g, const cj_zone_t *z)
{
    uint16_t a = cj_rgb(z->deco_a);
    uint16_t b = cj_rgb(z->deco_b);

    for (int i = 0; i < 70; i++) {
        int x = cj_rand_range(g, 0, CJ_W - 1);
        int y = cj_rand_range(g, 0, CJ_H - 1);
        cj_px(&g->bg, x, y, (i & 3) ? a : b);
    }
    /* Half a dozen larger ones, with their four points. */
    for (int i = 0; i < 6; i++) {
        int x = cj_rand_range(g, 4, CJ_W - 5);
        int y = cj_rand_range(g, 4, CJ_H - 5);
        cj_hline(&g->bg, x - 2, y, 5, a);
        cj_vline(&g->bg, x, y - 2, 5, a);
        cj_px(&g->bg, x, y, cj_rgb(0xFFFFFF));
    }
}

static void planets(cj_t *g, const cj_zone_t *z)
{
    stars(g, z);
    for (int i = 0; i < 3; i++) {
        int cx = cj_rand_range(g, 20, CJ_W - 20);
        int cy = cj_rand_range(g, 20, CJ_H - 30);
        int r  = cj_rand_range(g, 9, 15);
        uint16_t c = cj_rgb(i & 1 ? z->deco_a : z->deco_b);
        cj_disc(&g->bg, cx, cy, r, cj_tone(c, -4));
        cj_disc(&g->bg, cx - r / 3, cy - r / 3, r * 2 / 3, c);
        if (i == 1) {
            cj_ring(&g->bg, cx, cy, r + 4, cj_tone(c, 5));
            cj_ring(&g->bg, cx, cy, r + 5, cj_tone(c, 2));
        }
    }
}

void cj_bg_build(cj_t *g)
{
    const cj_zone_t *z = &cj_zones[g->zone];

    cj_clip_none(&g->bg);
    /* The sky reaches all the way to the top and leaves NO gap for the score:
     * the strip is painted over it by cj_draw_hud(). That way the menu and the
     * shop, which have no score, show the whole sky and not a loose black
     * bar. */
    cj_vgrad(&g->bg, 0, 0, CJ_W, CJ_H - 1,
             cj_rgb(z->sky_top), cj_rgb(z->sky_bot));

    /* The decorations are rolled with a seed that comes from the game and the
     * zone, not from the game's generator. Without this, every time the
     * background has to be rebuilt -pausing and carrying on, for instance- the
     * clouds appear somewhere else, and that looks like the whole screen
     * flickering. */
    uint32_t save = g->rng;
    g->rng = (g->bg_seed ^ (0x9E3779B9u * (uint32_t)(g->zone + 1))) | 1u;

    switch (z->deco_kind) {
    case 1:  stars(g, z);   break;
    case 2:  planets(g, z); break;
    default: clouds(g, z);  break;
    }
    g->rng = save;

    /* Only the meadow has ground: it is the only zone where the ground is in
     * view, and it also gives the start a reference that you are climbing. */
    if (g->zone == 0) {
        uint16_t pasto = cj_rgb(0x2E9E52);
        cj_rect(&g->bg, 0, CJ_H - 10, CJ_W, 10, pasto);
        cj_hline(&g->bg, 0, CJ_H - 10, CJ_W, cj_rgb(0x4ADE80));
    }

    memcpy(g->fb.px, g->bg.px, (size_t)CJ_W * CJ_H * sizeof(uint16_t));
}

/* --------------------------------------------------------------------------
 * Platforms
 * -------------------------------------------------------------------------- */

static void draw_spring(cj_buf_t *b, int x, int y)
{
    uint16_t m = cj_rgb(0xD5DCEB);
    uint16_t r = cj_rgb(0xFF4A3D);
    cj_rect(b, x, y + 4, 8, 2, cj_rgb(0x606B85));
    cj_hline(b, x + 1, y + 3, 6, m);
    cj_hline(b, x + 2, y + 2, 4, m);
    cj_hline(b, x + 1, y + 1, 6, m);
    cj_rect(b, x, y - 1, 8, 2, r);
}

static void draw_rocket(cj_buf_t *b, int x, int y)
{
    uint16_t body = cj_rgb(0xE6E9F0);
    uint16_t fin  = cj_rgb(0xFF4A3D);
    cj_rect(b, x + 2, y - 6, 3, 7, body);
    cj_px(b, x + 3, y - 8, body);
    cj_px(b, x + 3, y - 7, body);
    cj_vline(b, x + 1, y - 3, 4, fin);
    cj_vline(b, x + 5, y - 3, 4, fin);
    cj_px(b, x + 3, y - 5, cj_rgb(0x7BE9FF));
}

static void draw_plat(cj_t *g, cj_plat_t *p)
{
    const cj_zone_t *z = &cj_zones[g->zone];
    int x = sx_of(p->x);
    int y = sy_of(g, p->y);
    int w = PLAT_W;

    /* The one that fades SHRINKS instead of blending with the background:
     * blending would require knowing the colour of the sky under every pixel
     * -which also has clouds and stars- and shrinking reads just as well. */
    if (p->fade) {
        w = PLAT_W - (int)p->fade * 4;
        if (w < 2) {
            w = 2;
        }
        x += (PLAT_W - w) / 2;
    }

    /* The dirty box is always that of the full size plus the object on top,
     * even if the platform has shrunk: if the shrunken one were recorded, the
     * part that stopped being drawn would never be restored. */
    int box_y = p->item != ITEM_NONE ? y - 9 : y - 1;
    int box_h = (y + PLAT_H + 1) - box_y;
    mark(g, &p->prev, &p->drawn, sx_of(p->x) - 1, box_y, PLAT_W + 2, box_h);

    if (!on_screen(y, PLAT_H)) {
        return;
    }

    uint16_t cara, canto;
    switch (p->type) {
    case PLAT_FRAGILE:
        cara  = cj_rgb(0xC49A6C);
        canto = cj_rgb(0x7A5836);
        break;
    case PLAT_FADING:
        cara  = cj_rgb(0xF2F2F5);
        canto = cj_rgb(0x9AA3B8);
        break;
    default:
        cara  = cj_rgb(z->plat_a);
        canto = cj_rgb(z->plat_b);
        break;
    }

    cj_round(&g->fb, x, y, w, PLAT_H, 1, cara);
    cj_hline(&g->fb, x + 1, y + PLAT_H - 1, w - 2, canto);
    cj_hline(&g->fb, x + 1, y, w - 2, cj_tone(cara, 4));

    if (p->type == PLAT_FRAGILE) {
        /* Two cracks, so "this one breaks" reads without reading a sign. */
        cj_vline(&g->fb, x + w / 3, y + 1, PLAT_H - 2, canto);
        cj_vline(&g->fb, x + 2 * w / 3, y + 1, PLAT_H - 2, canto);
    } else if (p->type == PLAT_MOVING) {
        /* Rails at the ends: it says it moves while standing still. */
        cj_vline(&g->fb, x + 1, y + 1, PLAT_H - 2, canto);
        cj_vline(&g->fb, x + w - 2, y + 1, PLAT_H - 2, canto);
    }

    if (p->fade) {
        return;                     /* no spring and no rocket while it goes  */
    }
    if (p->item == ITEM_SPRING) {
        draw_spring(&g->fb, x + w / 2 - 4, y - 5);
    } else if (p->item == ITEM_ROCKET) {
        draw_rocket(&g->fb, x + w / 2 - 3, y);
    }
}

/* --------------------------------------------------------------------------
 * Coins
 *
 * They spin by narrowing horizontally: six widths in a table and that is that.
 * Really rotating in LVGL costs a layer, and here it is not even needed.
 * -------------------------------------------------------------------------- */

static const uint8_t coin_w[8] = { 8, 7, 5, 3, 3, 5, 7, 8 };

static void draw_coin(cj_t *g, cj_coin_t *c)
{
    int x = sx_of(c->x);
    int y = sy_of(g, c->y);

    mark(g, &c->prev, &c->drawn, x - 1, y - 1, COIN_R * 2 + 2, COIN_R * 2 + 2);

    if (!on_screen(y, COIN_R * 2)) {
        return;
    }
    int w  = coin_w[(c->phase >> 2) & 7];
    int cx = x + COIN_R - w / 2;

    cj_round(&g->fb, cx, y, w, COIN_R * 2, w > 4 ? 2 : 1, cj_rgb(0xFFD60A));
    cj_vline(&g->fb, cx + w / 2, y + 2, COIN_R * 2 - 4, cj_rgb(0xC08A00));
    if (w > 4) {
        cj_px(&g->fb, cx + 1, y + 1, cj_rgb(0xFFF3B0));
    }
}

/* --------------------------------------------------------------------------
 * Bugs
 *
 * A program's enemy is a bug, so here it literally is one.
 * -------------------------------------------------------------------------- */

static void draw_bug(cj_t *g, cj_bug_t *b)
{
    int x = sx_of(b->x);
    int y = sy_of(g, b->y);

    mark(g, &b->prev, &b->drawn, x - 2, y - 4, BUG_W + 4, BUG_H + 6);

    if (!on_screen(y, BUG_H)) {
        return;
    }

    uint16_t body = cj_rgb(0xE4453B);
    uint16_t dark = cj_rgb(0x8E1F18);
    uint16_t eye  = cj_rgb(0xFFFFFF);

    if (b->kind == 1) {
        /* The little wings of the floating one, beating in two positions. */
        int up = (b->anim & 8) ? 3 : 1;
        uint16_t w = cj_rgb(0xFFC9C4);
        cj_round(&g->fb, x - 2, y + 2 - up, 6, 4, 1, w);
        cj_round(&g->fb, x + BUG_W - 4, y + 2 - up, 6, 4, 1, w);
    }

    cj_round(&g->fb, x, y + 2, BUG_W, BUG_H - 2, 2, body);
    cj_hline(&g->fb, x + 1, y + BUG_H - 1, BUG_W - 2, dark);
    cj_vline(&g->fb, x + BUG_W / 2, y + 3, BUG_H - 5, dark);

    /* Antennae and eyes */
    cj_px(&g->fb, x + 3, y, dark);
    cj_px(&g->fb, x + 4, y + 1, dark);
    cj_px(&g->fb, x + BUG_W - 4, y, dark);
    cj_px(&g->fb, x + BUG_W - 5, y + 1, dark);
    cj_rect(&g->fb, x + 3, y + 4, 3, 3, eye);
    cj_rect(&g->fb, x + BUG_W - 6, y + 4, 3, 3, eye);
    cj_px(&g->fb, x + 4, y + 5, cj_rgb(0x1A1010));
    cj_px(&g->fb, x + BUG_W - 5, y + 5, cj_rgb(0x1A1010));

    if (b->kind == 0) {
        int step = (b->anim & 8) ? 1 : 0;
        cj_vline(&g->fb, x + 2, y + BUG_H, 2 + step, dark);
        cj_vline(&g->fb, x + BUG_W - 3, y + BUG_H, 3 - step, dark);
    }
}

/* --------------------------------------------------------------------------
 * Everything that moves
 * -------------------------------------------------------------------------- */

void cj_draw_movers(cj_t *g)
{
    cj_dirty_reset(&g->d_cur);
    cj_clip(&g->fb, 0, CJ_TOP, CJ_W, CJ_H);

    for (int i = 0; i < MAX_PLATS; i++) {
        cj_plat_t *p = &g->plats[i];
        if (p->active) {
            draw_plat(g, p);
        } else if (p->drawn) {
            /* It is gone: the rectangle it occupied is already in d_prev and
             * this frame's restore erased it. All that is left is to forget
             * it. */
            p->drawn = 0;
        }
    }

    for (int i = 0; i < MAX_COINS; i++) {
        cj_coin_t *c = &g->coins[i];
        if (c->active) {
            draw_coin(g, c);
        } else {
            c->drawn = 0;
        }
    }

    for (int i = 0; i < MAX_BUGS; i++) {
        cj_bug_t *b = &g->bugs[i];
        if (b->active) {
            draw_bug(g, b);
        } else {
            b->drawn = 0;
        }
    }

    for (int i = 0; i < MAX_PARTS; i++) {
        cj_part_t *p = &g->parts[i];
        if (!p->life) {
            p->drawn = 0;
            continue;
        }
        int x = sx_of(p->x);
        int y = sy_of(g, p->y);
        mark(g, &p->prev, &p->drawn, x, y, 2, 2);
        if (on_screen(y, 2)) {
            cj_rect(&g->fb, x, y, 2, 2, p->color);
        }
    }

    /* The critter goes last: it is drawn on top of everything else. */
    {
        int x = sx_of(g->hx);
        int y = sy_of(g, g->hy);
        cj_rect_t box;
        cj_hero_box(x, y, &box);
        mark(g, &g->hero_prev, &g->hero_drawn,
             box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0);

        int pose = g->hvy < -8 ? 1 : (g->hvy > 24 ? 2 : 0);
        cj_hero_draw(&g->fb, x, y, g->skin, pose, g->squash, g->facing,
                     g->rocket != 0);
    }

    cj_clip_none(&g->fb);
}

/* --------------------------------------------------------------------------
 * Score
 *
 * It restores and draws itself, and does NOT enter the next frame's dirty
 * list: nothing moves in that strip, so what was written still holds. What it
 * does have to do is push itself to the screen, and cjump.c takes care of that
 * by recording it separately.
 * -------------------------------------------------------------------------- */

void cj_draw_hud(cj_t *g)
{
    /* DELIBERATELY LARGE MARGINS, and this can only be seen on the board.
     *
     * The glass has rounded corners (AOS_SCREEN_RADIUS is 38 px of screen) and
     * the watch's bezel eats a little more. The score lives in the top strip,
     * which is exactly where the radius takes the most width: on the first
     * usable row the clipping is 10 px of screen on each side, and with the
     * text against the edge the height came out cut off -"_9M"- and the little
     * coin was missing half of itself. In the simulator it looked perfect.
     *
     * Two corrections, both measured against the glass's radius:
     *   - 20 px of the small buffer (40 of screen) of margin on each side
     *   - the text LOWERED within the strip: at y=9 the radius's clipping is
     *     already one pixel, against the five there are at y=6
     */
    const int MARGEN = 20;
    const int TEXTO_Y = 9;

    /* The bar is painted whole, it is not restored from the background: the
     * background has sky there too (the menu takes advantage of that, since it
     * draws no score). */
    cj_clip(&g->fb, 0, 0, CJ_W, CJ_HUD_H);
    cj_rect(&g->fb, 0, 0, CJ_W, CJ_HUD_H, cj_rgb(0x14141C));
    cj_hline(&g->fb, 0, CJ_HUD_H - 1, CJ_W, cj_rgb(0x3A3A4A));

    char buf[16];
    char *end = cj_num(buf, g->score, 1);
    *end++ = 'M';                   /* a unit, not a word: not translated     */
    *end = '\0';
    cj_text_sh(&g->fb, MARGEN, TEXTO_Y, buf, cj_rgb(0xFFFFFF), cj_rgb(0x000000));

    /* Coins, on the right: the little coin and the number. */
    char cbuf[12];
    cj_num(cbuf, g->coins_run, 1);
    int wn = cj_text_w(cbuf);
    int cx = CJ_W - MARGEN - wn;
    cj_text_sh(&g->fb, cx, TEXTO_Y, cbuf, cj_rgb(0xFFD60A), cj_rgb(0x000000));
    cj_round(&g->fb, cx - 11, TEXTO_Y - 1, 7, 8, 2, cj_rgb(0xFFD60A));
    cj_vline(&g->fb, cx - 8, TEXTO_Y + 1, 4, cj_rgb(0xC08A00));

    if (g->show_fps) {
        char f[16];
        char *p = cj_num(f, (uint32_t)(g->fps10 / 10), 1);
        *p++ = '.';
        p = cj_num(p, (uint32_t)(g->fps10 % 10), 1);
        *p++ = ' ';
        p = cj_num(p, g->last_area, 1);
        *p++ = '%';
        *p = '\0';
        cj_text(&g->fb, CJ_W / 2 - cj_text_w(f) / 2, TEXTO_Y, f, cj_rgb(0x7BE9FF));
    }

    cj_clip_none(&g->fb);

    /* It is recorded in d_push and NOT in d_cur: it has to be pushed to the
     * screen this frame, but not restored on the next one. */
    cj_dirty_add(&g->d_push, 0, 0, CJ_W, CJ_HUD_H);
    g->hud_dirty  = 0;
    g->hud_score  = g->score;
    g->hud_coins  = g->coins_run;
}
