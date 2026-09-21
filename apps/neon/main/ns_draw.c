/*
 * NEON SNAKES - the compositor (see ns_draw.h)
 */
#include "ns_draw.h"

#include <math.h>
#include <string.h>

#define W NS_SCREEN_W
#define H NS_SCREEN_H

static inline int ext_i(const ns_view_t *v, int x, int y)
{
    return (y + 1) * (v->cols + 2) + (x + 1);
}

static void mark_block(ns_view_t *v, int x, int y)
{
    for (int yy = y - 1; yy <= y + 1; yy++) {
        if (yy < -1 || yy > v->rows) continue;
        for (int xx = x - 1; xx <= x + 1; xx++) {
            if (xx < -1 || xx > v->cols) continue;
            v->rep[ext_i(v, xx, yy)] = 1;
        }
    }
    v->rep_any = true;
}

/* -------------------------------------------------------------------------- */
/* The frame around the arena, drawn once into bg                              */

static void draw_frame(ns_view_t *v)
{
    memset(v->bg, 0, (size_t)W * H * 2);
    const int C = v->cell;
    /* a rounded rectangle a quarter of a cell outside the arena, 1.5 px of
     * line and a short glow, all inside the margin ring */
    const float x0 = v->ox - C * 0.25f, y0 = v->oy - C * 0.25f;
    const float x1 = v->ox + v->cols * C + C * 0.25f, y1 = v->oy + v->rows * C + C * 0.25f;
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    const float rad = C * 0.6f, line = 0.8f, glow = C * 0.3f + 2.0f;
    const int r0 = 0x5A, g0 = 0x3C, b0 = 0xFF;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float qx = (x + 0.5f - cx), qy = (y + 0.5f - cy);
            if (qx < 0) qx = -qx;
            if (qy < 0) qy = -qy;
            qx = qx - hx + rad;
            qy = qy - hy + rad;
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float out = ox * ox + oy * oy;
            float in = qx > qy ? qx : qy;
            if (in > 0) in = 0;
            /* the distance to the rectangle's outline, cheaply: only near it */
            float d;
            d = (out > 0 ? sqrtf(out) : 0) + in - rad;
            if (d < 0) d = -d;
            float k;
            if (d <= line) k = 0.85f;
            else if (d < line + glow) {
                float f = 1.0f - (d - line) / glow;
                k = 0.35f * f * f;
            } else continue;
            uint32_t r = (uint32_t)(r0 * k), g = (uint32_t)(g0 * k), b = (uint32_t)(b0 * k);
            if (d <= line) {                    /* the tube's own light */
                r = (uint32_t)(r0 * 0.6f + 255 * 0.3f);
                g = (uint32_t)(g0 * 0.6f + 255 * 0.3f);
                b = (uint32_t)(b0 * 0.85f + 255 * 0.15f);
            }
            v->bg[y * W + x] = ns_565((r << 16) | (g << 8) | b);
        }
    }
}

void ns_view_init(ns_view_t *v, uint16_t *fb, uint16_t *bg, const ns_art_t *art,
                  const ns_game_t *g)
{
    memset(v, 0, sizeof *v);
    v->fb   = fb;
    v->bg   = bg;
    v->art  = art;
    v->cell = art->cell;
    v->cols = g->cols;
    v->rows = g->rows;
    v->ox   = (W - g->cols * v->cell) / 2;
    v->oy   = (H - g->rows * v->cell) / 2;
    v->me   = -1;
    v->halo_x = v->halo_y = -100;
    draw_frame(v);
}

/* -------------------------------------------------------------------------- */
/* What a cell shows                                                           */

static int dir_between(int x0, int y0, int x1, int y1)
{
    if (x1 > x0) return NS_RIGHT;
    if (x1 < x0) return NS_LEFT;
    if (y1 > y0) return NS_DOWN;
    return NS_UP;
}

static const uint16_t *sprite_for(const ns_view_t *v, const ns_game_t *g, int x, int y)
{
    uint16_t c = g->grid[y * g->cols + x];
    if (!c) return NULL;
    const ns_art_t *a = v->art;
    if (NS_IS_FRUIT(c)) {
        static const uint8_t LEVEL[4] = { 0, 1, 2, 1 };
        return a->fruit[(c & 0x0FFF) % NS_KIND_COUNT][LEVEL[v->pulse & 3]];
    }
    int o = NS_SNAKE_OF(c);
    const ns_snake_t *s = &g->s[o];
    int k = ns_seg_index(s, c);
    int colour = (s->dying && (s->dying & 1)) ? NS_WHITE : s->colour;
    if (colour != NS_WHITE && colour >= a->colours) colour = 0;
    int stripe = c & 1;
    if (s->len < 2) {
        return a->snake[colour][NS_SPR_HEAD(s->dir & 3)];
    }
    if (k == 0) {
        int f = dir_between(ns_seg_x(s, 1), ns_seg_y(s, 1), x, y);
        return a->snake[colour][NS_SPR_HEAD(f)];
    }
    int px = ns_seg_x(s, k - 1), py = ns_seg_y(s, k - 1);
    int to_prev = dir_between(x, y, px, py);
    if (k == s->len - 1) {
        return a->snake[colour][NS_SPR_TAIL(to_prev, stripe)];
    }
    int to_next = dir_between(x, y, ns_seg_x(s, k + 1), ns_seg_y(s, k + 1));
    int shape = ns_body_shape((1 << to_prev) | (1 << to_next));
    if (shape < 0) shape = 0;
    return a->snake[colour][NS_SPR_BODY(shape, stripe)];
}

/* -------------------------------------------------------------------------- */
/* Painting                                                                    */

static void blit_max(uint16_t *fb, const uint16_t *spr, int S, int sx, int sy,
                     int x0, int y0, int x1, int y1)
{
    int ax = sx > x0 ? sx : x0, bx = sx + S < x1 ? sx + S : x1;
    int ay = sy > y0 ? sy : y0, by = sy + S < y1 ? sy + S : y1;
    for (int y = ay; y < by; y++) {
        const uint16_t *s = spr + (y - sy) * S + (ax - sx);
        uint16_t *d = fb + y * W + ax;
        for (int x = ax; x < bx; x++, s++, d++) {
            if (*s) *d = ns_max565(*d, *s);
        }
    }
}

static uint16_t scaled565(uint32_t rgb, int num, int den)
{
    uint32_t r = ((rgb >> 16) & 0xFF) * num / den, g = ((rgb >> 8) & 0xFF) * num / den,
             b = (rgb & 0xFF) * num / den;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ns_565((r << 16) | (g << 8) | b);
}

/* A ring of radius r (in 1/16 px) and half width w around (cx, cy), in
 * integers: squared distances only. */
static void ring(uint16_t *fb, int cx16, int cy16, int r16, int w16, uint16_t col,
                 int x0, int y0, int x1, int y1)
{
    /* 32 bits are enough: a screen's diagonal in 1/16 px squared is ~130 M */
    int32_t in = r16 > w16 ? (r16 - w16) * (r16 - w16) : 0;
    int32_t out = (r16 + w16) * (r16 + w16);
    for (int y = y0; y < y1; y++) {
        int dy = y * 16 + 8 - cy16;
        for (int x = x0; x < x1; x++) {
            int dx = x * 16 + 8 - cx16;
            int32_t d2 = dx * dx + dy * dy;
            if (d2 >= in && d2 <= out) {
                uint16_t *d = &fb[y * W + x];
                *d = ns_max565(*d, col);
            }
        }
    }
}

static void draw_fx(const ns_view_t *v, const ns_fx_t *f, int x0, int y0, int x1, int y1)
{
    const int C = v->cell;
    int cx16 = (v->ox + f->x * C) * 16 + C * 8;
    int cy16 = (v->oy + f->y * C) * 16 + C * 8;
    int t = f->t, len = f->len;
    /* grows from a third of a cell to 1.33 cells, fading: it stays inside
     * the 3x3 block that is repainted for it */
    int r16 = C * 16 / 3 + (C * 16) * t / (len - 1 > 0 ? len - 1 : 1);
    int fade = len - t;
    uint16_t col = scaled565(f->rgb, fade * 10, len * 10);
    ring(v->fb, cx16, cy16, r16, 20, col, x0, y0, x1, y1);
    if (f->type == NS_FX_BURST && t > 1) {
        ring(v->fb, cx16, cy16, r16 * 2 / 3, 14, scaled565(0xFFFFFF, fade * 6, len * 10),
             x0, y0, x1, y1);
    }
}

static void draw_halo(const ns_view_t *v, const ns_game_t *g, int x0, int y0, int x1, int y1)
{
    const int C = v->cell;
    int cx16 = (v->ox + v->halo_x * C) * 16 + C * 8;
    int cy16 = (v->oy + v->halo_y * C) * 16 + C * 8;
    /* a slow breath between 1.1 and 1.35 cells, in the snake's colour */
    int ph = v->halo_t & 15;
    int breath = ph < 8 ? ph : 15 - ph;
    int r16 = C * 16 * 110 / 100 + C * 16 * 25 / 100 * breath / 7;
    uint32_t rgb = ns_snake_rgb(g->s[v->me].colour);
    ring(v->fb, cx16, cy16, r16, 14, scaled565(rgb, 8, 10), x0, y0, x1, y1);
}

static void paint_cell(ns_view_t *v, const ns_game_t *g, int x, int y)
{
    const int C = v->cell, S = C * 2, half = C / 2;
    int x0 = v->ox + x * C, y0 = v->oy + y * C, x1 = x0 + C, y1 = y0 + C;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1) return;
    for (int yy = y0; yy < y1; yy++) {
        memcpy(v->fb + yy * W + x0, v->bg + yy * W + x0, (size_t)(x1 - x0) * 2);
    }
    for (int ny = y - 1; ny <= y + 1; ny++) {
        if (ny < 0 || ny >= g->rows) continue;
        for (int nx = x - 1; nx <= x + 1; nx++) {
            if (nx < 0 || nx >= g->cols) continue;
            const uint16_t *spr = sprite_for(v, g, nx, ny);
            if (spr) {
                blit_max(v->fb, spr, S, v->ox + nx * C - half, v->oy + ny * C - half,
                         x0, y0, x1, y1);
            }
        }
    }
    for (int i = 0; i < v->nfx; i++) {
        const ns_fx_t *f = &v->fx[i];
        if (f->t < 0 || f->t >= f->len) continue;
        int dx = f->x - x, dy = f->y - y;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) draw_fx(v, f, x0, y0, x1, y1);
    }
    if (v->halo_t && v->me >= 0) {
        int dx = v->halo_x - x, dy = v->halo_y - y;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) draw_halo(v, g, x0, y0, x1, y1);
    }
}

/* Gathers the repainted cells into rectangles: runs along each row, and a
 * run that has exactly the span of one in the row above extends it. */
static void collect_rects(ns_view_t *v)
{
    const int C = v->cell;
    v->nrects = 0;
    v->pixels = 0;
    bool overflow = false;
    for (int y = -1; y <= v->rows && !overflow; y++) {
        int row_start = v->nrects;
        for (int x = -1; x <= v->cols; x++) {
            if (!v->rep[ext_i(v, x, y)]) continue;
            int xe = x;
            while (xe + 1 <= v->cols && v->rep[ext_i(v, xe + 1, y)]) xe++;
            int16_t px0 = (int16_t)(v->ox + x * C), px1 = (int16_t)(v->ox + (xe + 1) * C);
            int16_t py0 = (int16_t)(v->oy + y * C), py1 = (int16_t)(py0 + C);
            bool merged = false;
            /* a rectangle that ends on the row above with the same span */
            for (int i = 0; i < row_start; i++) {
                ns_rect_t *r = &v->rects[i];
                if (r->x0 == px0 && r->x1 == px1 && r->y1 == py0) {
                    r->y1 = py1;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                if (v->nrects >= NS_MAX_RECTS) { overflow = true; break; }
                ns_rect_t *r = &v->rects[v->nrects++];
                r->x0 = px0; r->x1 = px1; r->y0 = py0; r->y1 = py1;
            }
            x = xe;
        }
    }
    if (overflow) {
        /* one box around everything that was repainted */
        int bx0 = W, by0 = H, bx1 = 0, by1 = 0;
        for (int y = -1; y <= v->rows; y++) {
            for (int x = -1; x <= v->cols; x++) {
                if (!v->rep[ext_i(v, x, y)]) continue;
                int px = v->ox + x * C, py = v->oy + y * C;
                if (px < bx0) bx0 = px;
                if (py < by0) by0 = py;
                if (px + C > bx1) bx1 = px + C;
                if (py + C > by1) by1 = py + C;
            }
        }
        v->nrects = 1;
        v->rects[0].x0 = (int16_t)bx0; v->rects[0].y0 = (int16_t)by0;
        v->rects[0].x1 = (int16_t)bx1; v->rects[0].y1 = (int16_t)by1;
    }
    for (int i = 0; i < v->nrects; i++) {
        ns_rect_t *r = &v->rects[i];
        if (r->x0 < 0) r->x0 = 0;
        if (r->y0 < 0) r->y0 = 0;
        if (r->x1 > W) r->x1 = W;
        if (r->y1 > H) r->y1 = H;
        v->pixels += (uint32_t)(r->x1 - r->x0) * (uint32_t)(r->y1 - r->y0);
    }
}

static void paint_marked(ns_view_t *v, const ns_game_t *g)
{
    for (int y = -1; y <= v->rows; y++) {
        for (int x = -1; x <= v->cols; x++) {
            if (v->rep[ext_i(v, x, y)]) paint_cell(v, g, x, y);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Frames                                                                      */

void ns_view_fx(ns_view_t *v, uint8_t type, int x, int y, uint32_t rgb)
{
    if (v->nfx >= NS_MAX_FX) {
        return;
    }
    ns_fx_t *f = &v->fx[v->nfx++];
    f->type = type;
    f->x = (int8_t)x;
    f->y = (int8_t)y;
    f->t = -1;                      /* the frame advances it to 0 */
    f->len = type == NS_FX_BURST ? 10 : 7;
    f->rgb = rgb;
}

void ns_view_halo(ns_view_t *v, int me, int frames)
{
    v->me = (int8_t)me;
    v->halo_t = (uint8_t)(frames > 255 ? 255 : frames);
}

static void consume_marks(ns_view_t *v, ns_game_t *g)
{
    if (!g->chg_any) return;
    int n = g->cols * g->rows;
    for (int i = 0; i < n; i++) {
        if (g->chg[i >> 3] & (1u << (i & 7))) mark_block(v, i % g->cols, i / g->cols);
    }
    memset(g->chg, 0, sizeof g->chg);
    g->chg_any = false;
}

void ns_view_frame(ns_view_t *v, ns_game_t *g, uint8_t pulse)
{
    consume_marks(v, g);

    if ((pulse & 3) != v->pulse) {
        v->pulse = pulse & 3;
        int n = g->cols * g->rows;
        for (int i = 0; i < n; i++) {
            if (NS_IS_FRUIT(g->grid[i])) mark_block(v, i % g->cols, i / g->cols);
        }
    }

    for (int i = 0; i < v->nfx; ) {
        ns_fx_t *f = &v->fx[i];
        f->t++;
        mark_block(v, f->x, f->y);
        if (f->t >= f->len) {
            v->fx[i] = v->fx[--v->nfx];
            continue;
        }
        i++;
    }

    if (v->halo_t) {
        mark_block(v, v->halo_x, v->halo_y);
        v->halo_t--;
        const ns_snake_t *s = v->me >= 0 ? &g->s[v->me] : NULL;
        if (v->halo_t && s && s->alive && !s->dying) {
            v->halo_x = (int8_t)ns_seg_x(s, 0);
            v->halo_y = (int8_t)ns_seg_y(s, 0);
            mark_block(v, v->halo_x, v->halo_y);
        } else {
            v->halo_t = 0;
        }
    }

    v->nrects = 0;
    v->pixels = 0;
    if (!v->rep_any) {
        return;
    }
    paint_marked(v, g);
    collect_rects(v);
    memset(v->rep, 0, sizeof v->rep);
    v->rep_any = false;
}

void ns_view_full(ns_view_t *v, ns_game_t *g)
{
    memset(g->chg, 0, sizeof g->chg);
    g->chg_any = false;
    memcpy(v->fb, v->bg, (size_t)W * H * 2);
    for (int y = -1; y <= v->rows; y++) {
        for (int x = -1; x <= v->cols; x++) paint_cell(v, g, x, y);
    }
    memset(v->rep, 0, sizeof v->rep);
    v->rep_any = false;
    v->nrects = 1;
    v->rects[0].x0 = 0; v->rects[0].y0 = 0; v->rects[0].x1 = W; v->rects[0].y1 = H;
    v->pixels = W * H;
}
