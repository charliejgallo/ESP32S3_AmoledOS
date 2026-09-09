/*
 * ARKANOS - drawing
 *
 * Two buffers and one rule: 'bg' holds everything that does not change between
 * frames (sky, stars, walls, bricks) and 'fb' holds the frame on screen.
 * Before drawing, the loop restores from bg the rectangles we dirtied on the
 * previous frame; afterwards, everything that moves is drawn into fb and
 * records its rectangle. Only those rectangles are upscaled and invalidated.
 *
 * From that comes this file's unusual requirement: ANY rectangle of the
 * background has to be repaintable, because when a brick breaks whatever was
 * behind it has to be reconstructed. That is why the background is a function
 * of the rectangle -a gradient computed per row, stars stored in a table- and
 * not a drawing made once and then forgotten.
 */
#include "arkanos.h"
#include "aos_i18n.h"

#include <string.h>

void ak_brick_box(int row, int col, int *x, int *y)
{
    *x = AK_BRICK_X0 + col * AK_BRICK_W;
    *y = AK_BRICK_Y0 + row * AK_BRICK_H;
}

/* --------------------------------------------------------------------------
 * Background
 * -------------------------------------------------------------------------- */

/* Paints sky, stars and structure inside whatever clip the background buffer
 * already has set. */
static void bg_paint_clipped(ak_t *g)
{
    const ak_level_t *lv = ak_level_get(g->level);
    ak_buf_t *b = &g->bg;
    uint16_t top = ak_rgb(lv->sky_top);
    uint16_t bot = ak_rgb(lv->sky_bot);
    uint16_t acc = ak_rgb(lv->accent);

    /* gradient: one line per row of the clip, not of the screen */
    for (int y = b->cy0; y < b->cy1; y++) {
        int f = y * 16 / (AK_H - 1);
        ak_hline(b, b->cx0, y, b->cx1 - b->cx0, ak_mix(top, bot, f));
    }

    for (int i = 0; i < g->nstars; i++) {
        uint16_t c = ak_mix(ak_mix(top, bot, g->star_y[i] * 16 / (AK_H - 1)),
                            0xFFFF, g->star_b[i]);
        ak_px(b, g->star_x[i], g->star_y[i], c);
    }

    /* score bar */
    ak_rect(b, 0, 0, AK_W, AK_HUD_H, ak_rgb(0x05060C));
    ak_hline(b, 0, AK_HUD_H - 1, AK_W, ak_tone(acc, -8));

    /* ceiling and walls: a strip of metal with a light edge facing inwards and
     * rivets every 16 px, which is what gives the field its scale */
    uint16_t metal  = ak_tone(acc, -10);
    uint16_t claro  = ak_tone(acc, -2);
    uint16_t oscuro = ak_tone(acc, -13);

    ak_rect(b, 0, AK_HUD_H, AK_W, AK_FIELD_Y0 - AK_HUD_H, metal);
    ak_rect(b, 0, AK_FIELD_Y0, AK_FIELD_X0, AK_H - AK_FIELD_Y0, metal);
    ak_rect(b, AK_FIELD_X1, AK_FIELD_Y0, AK_W - AK_FIELD_X1, AK_H - AK_FIELD_Y0, metal);

    ak_hline(b, 0, AK_FIELD_Y0 - 1, AK_W, claro);
    ak_vline(b, AK_FIELD_X0 - 1, AK_FIELD_Y0, AK_H - AK_FIELD_Y0, claro);
    ak_vline(b, AK_FIELD_X1, AK_FIELD_Y0, AK_H - AK_FIELD_Y0, claro);

    for (int y = AK_FIELD_Y0 + 6; y < AK_H; y += 16) {
        ak_px(b, 1, y, oscuro);
        ak_px(b, AK_W - 2, y, oscuro);
    }
    for (int x = 8; x < AK_W - 8; x += 16) {
        ak_px(b, x, AK_HUD_H + 1, oscuro);
    }
}

static void bg_paint(ak_t *g, int x, int y, int w, int h)
{
    ak_clip(&g->bg, x, y, x + w, y + h);
    bg_paint_clipped(g);
    ak_clip_none(&g->bg);
}

/* A brick: a block with a light edge at the top and on the left, shadow at the
 * bottom and on the right, and a dark outline so two neighbouring bricks do
 * not merge. */
static void brick_paint(ak_t *g, int row, int col)
{
    int kind = g->grid[row][col];
    if (kind == BK_NONE) {
        return;
    }
    const ak_brick_def_t *d = ak_brick(kind);
    ak_buf_t *b = &g->bg;
    int x, y;
    ak_brick_box(row, col, &x, &y);

    uint16_t base = ak_rgb(d->color);
    /* The multi-hit ones fade: it is the only clue as to how much they have
     * left, and it reads at a glance better than any little number. */
    if (d->hp > 1 && g->hp[row][col] < d->hp) {
        base = ak_tone(base, -(int)(d->hp - g->hp[row][col]) * 4);
    }

    ak_rect(b, x, y, AK_BRICK_W, AK_BRICK_H, base);
    ak_hline(b, x + 1, y, AK_BRICK_W - 2, ak_tone(base, 5));
    ak_vline(b, x, y + 1, AK_BRICK_H - 2, ak_tone(base, 3));
    ak_hline(b, x + 1, y + AK_BRICK_H - 1, AK_BRICK_W - 2, ak_tone(base, -6));
    ak_vline(b, x + AK_BRICK_W - 1, y + 1, AK_BRICK_H - 2, ak_tone(base, -5));
    ak_px(b, x, y, ak_tone(base, -8));
    ak_px(b, x + AK_BRICK_W - 1, y, ak_tone(base, -8));
    ak_px(b, x, y + AK_BRICK_H - 1, ak_tone(base, -8));
    ak_px(b, x + AK_BRICK_W - 1, y + AK_BRICK_H - 1, ak_tone(base, -8));

    if (kind == BK_STEEL) {
        /* diagonal sheen: it reads as metal and not as "one more colour" */
        for (int i = 0; i < 4; i++) {
            ak_line(b, x + 3 + i * 4, y + AK_BRICK_H - 2, x + 6 + i * 4, y + 1,
                    ak_tone(base, 6));
        }
    } else if (kind == BK_BOMB) {
        ak_disc(b, x + AK_BRICK_W / 2, y + AK_BRICK_H / 2, 2, ak_rgb(0xFFE45E));
        ak_px(b, x + AK_BRICK_W / 2, y + AK_BRICK_H / 2, ak_rgb(0xFFFFFF));
    } else if (kind == BK_MYST) {
        ak_text(b, x + AK_BRICK_W / 2 - 2, y + 1, "?", ak_rgb(0x05060C));
    } else if (d->hp > 1) {
        /* cracks: one per hit taken */
        int golpes = d->hp - g->hp[row][col];
        for (int i = 0; i < golpes && i < 3; i++) {
            int gx = x + 3 + i * 5;
            ak_line(b, gx, y + 2, gx + 2, y + AK_BRICK_H - 3, ak_tone(base, -9));
        }
    }
}

void ak_bg_brick(ak_t *g, int row, int col)
{
    int x, y;
    ak_brick_box(row, col, &x, &y);
    bg_paint(g, x, y, AK_BRICK_W, AK_BRICK_H);
    ak_clip(&g->bg, x, y, x + AK_BRICK_W, y + AK_BRICK_H);
    brick_paint(g, row, col);
    ak_clip_none(&g->bg);
    ak_dirty_add(&g->d_bg, x, y, AK_BRICK_W, AK_BRICK_H);
}

void ak_bg_build(ak_t *g)
{
    const ak_level_t *lv = ak_level_get(g->level);

    g->nstars = (uint8_t)(lv->stars > 40 ? 40 : lv->stars);
    for (int i = 0; i < g->nstars; i++) {
        g->star_x[i] = (int16_t)ak_rnd_range(g, AK_FIELD_X0 + 1, AK_FIELD_X1 - 2);
        g->star_y[i] = (int16_t)ak_rnd_range(g, AK_FIELD_Y0 + 1, AK_H - 2);
        g->star_b[i] = (uint8_t)ak_rnd_range(g, 3, 13);
    }

    ak_clip_none(&g->bg);
    bg_paint_clipped(g);
    for (int r = 0; r < AK_ROWS; r++) {
        for (int c = 0; c < AK_COLS; c++) {
            brick_paint(g, r, c);
        }
    }

    /* the frame starts as a copy of the background */
    memcpy(g->fb.px, g->bg.px, (size_t)AK_W * AK_H * sizeof(uint16_t));
    ak_dirty_all(&g->d_bg);
}

/* --------------------------------------------------------------------------
 * Score
 *
 * It only repaints when a number changes: it restores its strip from the
 * background, writes over it and records the rectangle. It does not enter the
 * next frame's list because nothing moves up there (everything else is clipped
 * to the playing field), so what is left written still holds.
 * -------------------------------------------------------------------------- */
void ak_draw_hud(ak_t *g)
{
    ak_buf_t *b = &g->fb;
    ak_rect_t band = { 0, 0, AK_W, AK_HUD_H };
    char buf[16];

    ak_restore(g->fb.px, g->bg.px, &band);
    ak_clip(b, 0, 0, AK_W, AK_HUD_H);

    /* While the finger is down, the strip lightens and where the level goes it
     * says PAUSA: that is all the feedback the hold gesture gets. It goes
     * before the texts so as not to dirty them. */
    if (g->hud_hold) {
        ak_shade(b, 0, 0, AK_W, AK_HUD_H - 1, 5);
    }

    ak_num(buf, g->score, 6);
    ak_text(b, 4, 4, buf, ak_rgb(0xFFFFFF));

    char *p = buf;
    if (g->hud_hold) {
        /* Bounded copy: it used to be sizeof() over the literal, which with a
         * translation longer than buf overran. */
        const char *pausa = _("PAUSA");
        int i = 0;
        while (pausa[i] && i < (int)sizeof(buf) - 1) {
            buf[i] = pausa[i];
            i++;
        }
        buf[i] = '\0';
    } else {
        *p++ = 'N';
        p = ak_num(p, (uint32_t)(g->level + 1), 1);
        if (g->lap) {
            *p++ = '-';
            p = ak_num(p, (uint32_t)(g->lap + 1), 1);
        }
        *p = '\0';
    }
    ak_text(b, AK_W / 2 - ak_text_w(buf) / 2, 4, buf,
            ak_rgb(g->hud_hold ? 0xFFE45E : 0x9AA3BC));

    /* lives: little paddles, which is what is lost */
    for (int i = 0; i < g->lives && i < 5; i++) {
        int x = AK_W - 6 - (i + 1) * 9;
        ak_rect(b, x, 6, 7, 3, ak_rgb(0x4A9DF5));
        ak_hline(b, x + 1, 6, 5, ak_rgb(0xBFE9FF));
    }

    ak_clip_none(b);
    ak_dirty_add(&g->d_push, 0, 0, AK_W, AK_HUD_H);
    g->hud_dirty = 0;
}

/* --------------------------------------------------------------------------
 * What moves
 * -------------------------------------------------------------------------- */

/* Records a dirty rectangle, clipped to the field just like the drawing.
 *
 * The clip is not decorative: the score's strip is NOT restored on every frame
 * (it only repaints when a number changes), so if a rectangle touched it, the
 * next frame would restore it from the background and erase the score until
 * the next change. The blast of a bomb on the top row reaches a radius of
 * 27 px, so it does reach it. */
static void mark(ak_t *g, int x, int y, int w, int h)
{
    if (y < AK_FIELD_Y0) {
        h -= AK_FIELD_Y0 - y;
        y = AK_FIELD_Y0;
    }
    ak_dirty_add(&g->d_cur, x, y, w, h);
}

static void draw_paddle(ak_t *g)
{
    ak_buf_t *b = &g->fb;
    int w = g->pad_w;
    int x = UNFX(g->pad_x) - w / 2;
    int y = AK_PAD_Y;
    uint16_t cuerpo = ak_rgb(0x4A9DF5);
    uint16_t filo   = ak_rgb(0xBFE9FF);

    if (g->t_catch) {
        cuerpo = ak_rgb(0x4ADE80);
        filo   = ak_rgb(0xC8FFD8);
    }

    ak_round(b, x, y, w, AK_PAD_H, 1, ak_tone(cuerpo, -6));
    ak_hline(b, x + 2, y, w - 4, filo);
    ak_hline(b, x + 1, y + 1, w - 2, cuerpo);
    ak_hline(b, x + 1, y + 2, w - 2, ak_tone(cuerpo, -3));

    if (g->t_laser) {
        /* the cannons poke out: you have to see the upgrade is fitted */
        ak_rect(b, x + 1, y - 2, 3, 2, ak_rgb(0xFF4A3D));
        ak_rect(b, x + w - 4, y - 2, 3, 2, ak_rgb(0xFF4A3D));
    }
    /* Upgrades about to run out blink for the last second and a half: losing
     * the wide paddle mid-rally is an unpleasant surprise. */
    if ((g->t_wide && g->t_wide < 45 && (g->t_wide & 4)) ||
        (g->t_laser && g->t_laser < 45 && (g->t_laser & 4)) ||
        (g->t_catch && g->t_catch < 45 && (g->t_catch & 4))) {
        ak_shade(b, x, y - 2, w, AK_PAD_H + 2, 7);
    }

    mark(g, x - 1, y - 3, w + 2, AK_PAD_H + 4);
}

static void draw_balls(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_BALLS; i++) {
        ak_ball_t *ba = &g->ball[i];
        if (!ba->alive) {
            continue;
        }
        /* trail: three dots fading. It costs three small rectangles that
         * nearly always merge with the ball's. */
        for (int t = ba->tn - 1; t >= 0; t--) {
            int f = 9 - t * 3;
            ak_glow(b, ba->tx[t], ba->ty[t], AK_BALL_R, ak_rgb(0x7BE9FF), f);
            mark(g, ba->tx[t] - AK_BALL_R - 1, ba->ty[t] - AK_BALL_R - 1,
                 AK_BALL_R * 2 + 3, AK_BALL_R * 2 + 3);
        }

        int x = UNFX(ba->x), y = UNFX(ba->y);
        ak_disc(b, x, y, AK_BALL_R, ak_rgb(0xE8F6FF));
        ak_px(b, x - 1, y - 1, ak_rgb(0xFFFFFF));
        ak_px(b, x + 1, y + 1, ak_rgb(0x8FB8CC));
        mark(g, x - AK_BALL_R - 1, y - AK_BALL_R - 1,
             AK_BALL_R * 2 + 3, AK_BALL_R * 2 + 3);
    }
}

static void draw_caps(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_CAPS; i++) {
        ak_cap_t *c = &g->cap[i];
        if (!c->alive) {
            continue;
        }
        const ak_cap_def_t *d = ak_cap_def(c->kind);
        uint16_t col = ak_rgb(d->color);
        int x = UNFX(c->x) - 7;
        int y = UNFX(c->y) - 4;
        char txt[2] = { d->letra, '\0' };

        ak_round(b, x, y, 14, 8, 2, ak_tone(col, -7));
        ak_round(b, x + 1, y + 1, 12, 6, 1, col);
        ak_hline(b, x + 3, y + 1, 8, ak_tone(col, 6));
        ak_text(b, x + 5, y + 1, txt, ak_rgb(0x05060C));

        mark(g, x - 1, y - 1, 16, 10);
    }
}

static void draw_shots(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_SHOTS; i++) {
        if (!g->shot[i].alive) {
            continue;
        }
        int x = UNFX(g->shot[i].x), y = UNFX(g->shot[i].y);
        ak_rect(b, x, y - 3, 1, 6, ak_rgb(0xFFE45E));
        ak_px(b, x, y - 4, ak_rgb(0xFFFFFF));
        mark(g, x - 1, y - 5, 3, 10);
    }
}

static void draw_bits(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_BITS; i++) {
        ak_bit_t *p = &g->bit[i];
        if (!p->life) {
            continue;
        }
        int x = UNFX(p->x), y = UNFX(p->y);
        int f = p->life0 ? p->life * 16 / p->life0 : 0;
        uint16_t c = ak_mix(ak_rgb(0x05060C), p->col, f);
        ak_rect(b, x, y, 2, 2, c);
        mark(g, x - 1, y - 1, 4, 4);
    }
}

static void draw_rings(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_RINGS; i++) {
        ak_ring_t *r = &g->ring[i];
        if (!r->life) {
            continue;
        }
        int paso = r->life0 - r->life;
        int rad  = 3 + paso * 2;
        int f    = r->life * 10 / (r->life0 ? r->life0 : 1);
        ak_wave(b, r->x, r->y, rad, 2, r->col, f);
        mark(g, r->x - rad - 3, r->y - rad - 3, rad * 2 + 7, rad * 2 + 7);
    }
}

static void draw_hits(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int r = 0; r < AK_ROWS; r++) {
        for (int c = 0; c < AK_COLS; c++) {
            if (!g->hit[r][c]) {
                continue;
            }
            int x, y;
            ak_brick_box(r, c, &x, &y);
            ak_shade(b, x, y, AK_BRICK_W, AK_BRICK_H, g->hit[r][c] * 3);
            mark(g, x, y, AK_BRICK_W, AK_BRICK_H);
        }
    }
}

static void draw_pops(ak_t *g)
{
    ak_buf_t *b = &g->fb;

    for (int i = 0; i < AK_MAX_POPS; i++) {
        ak_pop_t *p = &g->pop[i];
        if (!p->life) {
            continue;
        }
        int w = ak_text_w(p->txt);
        uint16_t c = p->life < 8 ? ak_mix(ak_rgb(0x05060C), p->col, p->life * 2)
                                 : p->col;
        ak_text_sh(b, p->x - w / 2, p->y, p->txt, c, ak_rgb(0x05060C));
        mark(g, p->x - w / 2 - 1, p->y - 1, w + 3, AK_CH_H + 3);
    }
}

/* The "NIVEL 3" panel and company. It is large and stays for a while, but it
 * is still one more rectangle: it is only restored when it goes. */
static void draw_banner(ak_t *g)
{
    const char *linea1 = NULL;
    const char *linea2 = NULL;
    char buf[16];
    uint16_t col = ak_rgb(0xFFE45E);

    if (g->state == ST_READY) {
        /* -4 leaves room for the level number and the terminator. */
        const char *pre = _("NIVEL ");
        int i = 0;
        while (pre[i] && i < (int)sizeof(buf) - 4) {
            buf[i] = pre[i];
            i++;
        }
        ak_num(buf + i, (uint32_t)(g->level + 1), 1);
        linea1 = buf;
        linea2 = _(ak_level_get(g->level)->name);
    } else if (g->state == ST_CLEAR) {
        linea1 = _("SUPERADO");
        col = ak_rgb(0x4ADE80);
    } else if (g->state == ST_LOST) {
        linea1 = _("OTRA VEZ");
        col = ak_rgb(0xFF4A3D);
    } else {
        return;
    }

    int w1 = ak_text_w(linea1);
    int w2 = linea2 ? ak_text_w(linea2) : 0;
    int w  = (w1 > w2 ? w1 : w2) + 16;
    int h  = linea2 ? 26 : 16;
    int x  = AK_W / 2 - w / 2;
    int y  = 150;

    ak_buf_t *b = &g->fb;
    ak_round(b, x, y, w, h, 3, ak_rgb(0x05060C));
    ak_shade(b, x, y, w, h, -2);
    ak_text_center(b, AK_W / 2, y + 4, linea1, col, ak_rgb(0x000000));
    if (linea2) {
        ak_text_center(b, AK_W / 2, y + 14, linea2, ak_rgb(0x9AA3BC),
                       ak_rgb(0x000000));
    }
    mark(g, x - 1, y - 1, w + 2, h + 2);
}

/* Frames per second and pixels pushed. It is the number to watch on the board:
 * the second one says how much work the dirty list saved. */
static void draw_fps(ak_t *g)
{
    char buf[20];
    char *p = buf;

    p = ak_num(p, (uint32_t)(g->fps10 / 10), 1);
    *p++ = '.';
    p = ak_num(p, (uint32_t)(g->fps10 % 10), 1);
    *p++ = ' ';
    p = ak_num(p, (uint32_t)g->last_area, 1);
    *p++ = '%';
    *p = '\0';

    ak_text_sh(&g->fb, AK_FIELD_X0 + 2, AK_H - 9, buf, ak_rgb(0x4ADE80),
               ak_rgb(0x000000));
    mark(g, AK_FIELD_X0 + 1, AK_H - 10, ak_text_w(buf) + 3, AK_CH_H + 3);
}

void ak_draw_movers(ak_t *g)
{
    ak_dirty_reset(&g->d_cur);

    /* Everything clipped to the field: if a particle escaped into the score it
     * would leave rubbish there until the next score change, because that
     * strip is not restored on every frame. */
    ak_clip(&g->fb, AK_FIELD_X0, AK_FIELD_Y0, AK_FIELD_X1, AK_H);

    draw_hits(g);
    draw_rings(g);
    draw_caps(g);
    draw_bits(g);
    draw_shots(g);
    draw_balls(g);
    draw_paddle(g);
    draw_pops(g);
    draw_banner(g);
    if (g->show_fps) {
        draw_fps(g);
    }

    ak_clip_none(&g->fb);
}
