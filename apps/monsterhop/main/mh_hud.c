/*
 * MONSTER HOP - the HUD (see mh_hud.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "mh_hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* fminf/fmaxf are not in the firmware's symbol table */
static inline float mn(float a, float b) { return a < b ? a : b; }
static inline float mx(float a, float b) { return a > b ? a : b; }
static inline float ab(float a) { return a < 0 ? -a : a; }

#define GOLD    0xFFC83A
#define GOLD_D  0x6A5020
#define RED     0xFF4A5A
#define WHITE   0xFFFFFF

void mh_hud_free(mh_hud_t *h)
{
    for (int i = 0; i < 12; i++) {
        free(h->dig[i].a);
        free(h->sdig[i].a);
    }
    for (int i = 0; i < MSG_N; i++) free(h->msg[i].a);
    free(h->title.a);
    memset(h, 0, sizeof(*h));
}

void mh_hud_events(mh_hud_state_t *s, uint32_t ev, float dt)
{
    if (s->msg >= 0) {
        s->msg_t += dt;
        if (s->msg_t > 1.8f) s->msg = -1;
    }
    if (s->key_flash > 0) s->key_flash -= dt;
    if (s->coin_flash > 0) s->coin_flash -= dt;
    int m = -1;
    if (ev & EV_KEY) { m = MSG_KEY; s->key_flash = 0.8f; }
    if (ev & EV_OPEN) m = MSG_OPEN;
    if (ev & EV_CHECK) m = MSG_CHECK;
    if (ev & EV_TIMEUP) m = MSG_TIMEUP;
    if (ev & EV_LIFE) m = MSG_LIFE;
    if (ev & EV_TIME) m = MSG_TIME;
    if (ev & EV_LOW_TIME) m = MSG_LOW;
    if (ev & (EV_COIN | EV_CHEST)) s->coin_flash = 0.4f;
    if (m >= 0) {
        s->msg = m;
        s->msg_t = 0;
    }
}

/* ---- little drawings ---- */

static void outline_text(mh_img_t *im, const mh_mask_t *m, int x, int y, uint16_t c, int alpha)
{
    uint16_t k = mh_hex(0x000000);
    int oa = alpha * 3 >> 2;
    mh_mask_draw(im, m, x - 1, y, k, oa);
    mh_mask_draw(im, m, x + 1, y, k, oa);
    mh_mask_draw(im, m, x, y - 1, k, oa);
    mh_mask_draw(im, m, x, y + 1, k, oa);
    mh_mask_draw(im, m, x, y + 2, k, oa);
    mh_mask_draw(im, m, x, y, c, alpha);
}

/* a string of digits and ':' '/' with the digit masks; returns its width */
static int number_w(const mh_mask_t *d, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        int i = *s >= '0' && *s <= '9' ? *s - '0' : *s == ':' ? 10 : *s == '/' ? 11 : -1;
        if (i >= 0 && d[i].a) w += d[i].w - 1;
    }
    return w;
}
static void number(mh_img_t *im, const mh_mask_t *d, const char *s, int x, int y, uint16_t c)
{
    for (; *s; s++) {
        int i = *s >= '0' && *s <= '9' ? *s - '0' : *s == ':' ? 10 : *s == '/' ? 11 : -1;
        if (i < 0 || !d[i].a) continue;
        outline_text(im, &d[i], x, y, c, 255);
        x += d[i].w - 1;
    }
}

/* a key: a ring and a toothed shaft, 22 x 12 px */
/* who: 0 not taken, 1 taken here, 2 taken by the other watch */
static void key_icon(mh_img_t *im, int x, int y, int who, float glow)
{
    bool have = who != 0;
    uint16_t c = mh_hex(who == 2 ? 0x9AD8FF : have ? GOLD : GOLD_D);
    uint16_t k = mh_hex(0x1A1208);
    int a = 255;
    if (have && glow > 0) {
        mh_disc(im, (x + 11) * 16, (y + 6) * 16, (int)(16 * (9 + glow * 6)), mh_hex(0xFFE8A0), (int)(glow * 120));
    }
    mh_disc(im, (x + 5) * 16, (y + 6) * 16, 6 * 16, k, a);
    mh_disc(im, (x + 5) * 16, (y + 6) * 16, 5 * 16, c, a);
    mh_disc(im, (x + 5) * 16, (y + 6) * 16, 2 * 16, k, a);
    mh_rect(im, x + 9, y + 4, 12, 4, k);
    mh_rect(im, x + 9, y + 5, 12, 2, c);
    mh_rect(im, x + 16, y + 7, 2, 4, c);
    mh_rect(im, x + 19, y + 7, 2, 3, c);
}

static void heart_icon(mh_img_t *im, int x, int y, uint16_t c)
{
    uint16_t k = mh_hex(0x200810);
    for (int pass = 0; pass < 2; pass++) {
        uint16_t col = pass ? c : k;
        int g = pass ? 0 : 1;
        mh_disc(im, (x + 4) * 16, (y + 4) * 16, (4 + g) * 16, col, 255);
        mh_disc(im, (x + 10) * 16, (y + 4) * 16, (4 + g) * 16, col, 255);
        for (int r = 0; r < 8 + g; r++) {
            int half = 7 + g - r;
            if (half < 0) break;
            mh_rect(im, x + 7 - half, y + 5 + r, 2 * half, 1, col);
        }
    }
}

static void coin_icon(mh_img_t *im, int x, int y, float flash)
{
    mh_disc(im, (x + 7) * 16, (y + 7) * 16, 8 * 16, mh_hex(0x3A2808), 255);
    mh_disc(im, (x + 7) * 16, (y + 7) * 16, 7 * 16, mh_hex(flash > 0 ? 0xFFF0A0 : GOLD), 255);
    mh_disc(im, (x + 7) * 16, (y + 7) * 16, 4 * 16, mh_hex(0xE8A020), 255);
}

/* a filled triangle pointing along (dx, dy) with its tip at (x, y) */
static void arrow(mh_img_t *im, float x, float y, float dx, float dy, uint16_t c, int alpha)
{
    float n = sqrtf(dx * dx + dy * dy);
    if (n < 1e-3f) return;
    dx /= n;
    dy /= n;
    float len = 22, half = 12;
    float bx = x - dx * len, by = y - dy * len;
    float px = -dy, py = dx;
    float ax = bx + px * half, ay = by + py * half, cx = bx - px * half, cy = by - py * half;
    int y0 = (int)mn(mn(y, ay), cy) - 1, y1 = (int)mx(mx(y, ay), cy) + 1;
    int x0 = (int)mn(mn(x, ax), cx) - 1, x1 = (int)mx(mx(x, ax), cx) + 1;
    if (y0 < im->cy0) y0 = im->cy0;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
    if (x0 < im->cx0) x0 = im->cx0;
    if (x1 >= im->cx1) x1 = im->cx1 - 1;
    uint16_t k = mh_hex(0x000000);
    for (int yy = y0; yy <= y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx <= x1; xx++) {
            float qx = xx + 0.5f, qy = yy + 0.5f;
            /* inside the triangle (x,y) a c, with a soft 1.5 px rim */
            float e0 = (ax - x) * (qy - y) - (ay - y) * (qx - x);
            float e1 = (cx - ax) * (qy - ay) - (cy - ay) * (qx - ax);
            float e2 = (x - cx) * (qy - cy) - (y - cy) * (qx - cx);
            bool in = (e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!in) continue;
            float d0 = ab(e0) / 24, d1 = ab(e1) / 24, d2 = ab(e2) / 22;
            float d = mn(d0, mn(d1, d2));
            row[xx] = mh_blend(row[xx], d < 2.0f ? k : c, alpha);
        }
    }
}

void mh_hud_draw(const mh_hud_t *h, mh_img_t *im, const mh_game_t *g, const mh_hud_state_t *s,
                 const mh_world_t *w, int cam_x, int cam_y)
{
    /* the band: skip what does not touch it */
    int by0 = im->cy0, by1 = im->cy1;
    if (by0 < 64) {
        /* a dark strip for legibility */
        for (int y = by0; y < by1 && y < 40; y++) {
            uint16_t *row = im->px + (size_t)y * im->w;
            int k = 256 - (40 - y) * 4;
            for (int x = 0; x < MH_W; x++) row[x] = mh_darken(row[x], k);
        }
        /* pause */
        if (s->pause_icon) {
            mh_rrect(im, 12, 10, 6, 20, 2, mh_hex(0xFFFFFF), 200);
            mh_rrect(im, 22, 10, 6, 20, 2, mh_hex(0xFFFFFF), 200);
        }
        /* keys */
        int kx = 44;
        if (g->link) {
            /* a race: this watch's keys in gold first, the other's in blue */
            int mine = g->my_keys, theirs = g->keys - g->my_keys;
            for (int i = 0; i < MH_KEYS; i++)
                key_icon(im, kx + i * 25, 8, i < mine ? 1 : i < mine + theirs ? 2 : 0, i < mine ? s->key_flash : 0);
        } else {
            for (int i = 0; i < MH_KEYS; i++) key_icon(im, kx + i * 25, 8, i < g->keys, s->key_flash);
            /* lives */
            int lx = 44;
            for (int i = 0; i < g->lives && i < 6; i++) heart_icon(im, lx + i * 17, 26, mh_hex(RED));
        }
        /* the clock, top right; the coins under it */
        char b[16];
        if (g->timer) {
            int t = (int)ceilf(g->time_left);
            if (t < 0) t = 0;
            snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
            int ww = number_w(h->dig, b);
            bool low = t <= 10 && ((int)(g->t * 4) & 1);
            number(im, h->dig, b, MH_W - 14 - ww, 4, mh_hex(low ? 0xFF6060 : WHITE));
        }
        snprintf(b, sizeof b, "%d", g->coins);
        int cw = number_w(h->sdig, b);
        int cy = g->timer ? 34 : 8;
        coin_icon(im, MH_W - 18 - cw - 18, cy + 1, s->coin_flash);
        number(im, h->sdig, b, MH_W - 14 - cw, cy, mh_hex(s->coin_flash > 0 ? 0xFFF0A0 : WHITE));
    }
    /* the arrow towards the nearest key, when it is off the screen */
    float tx, ty;
    if (g->state == GS_PLAY && mh_game_target(g, &tx, &ty)) {
        int tz = 0;
        int ix = (int)tx, iy = (int)ty;
        if (mh_in(g->lv, ix, iy)) tz = mh_cell(g->lv, ix, iy)->h;
        float sx = mh_lpx(w, tx, ty) - cam_x, sy = mh_lpy(w, tx, ty, tz * MH_FLOOR_M) - cam_y - 20;
        bool off = sx < 0 || sx > MH_W || sy < 50 || sy > MH_H;
        if (off) {
            float cx = MH_W / 2.0f, cy = MH_H / 2.0f + 20;
            float dx = sx - cx, dy = sy - cy;
            /* clamp the ray to the screen, inset */
            float kx = dx > 0 ? (MH_W - 26 - cx) / dx : dx < 0 ? (26 - cx) / dx : 1e9f;
            float ky = dy > 0 ? (MH_H - 26 - cy) / dy : dy < 0 ? (70 - cy) / dy : 1e9f;
            float k = mn(kx, ky);
            float ex = cx + dx * k, ey = cy + dy * k;
            float pulse = 0.75f + 0.25f * sinf(g->t * 6.0f);
            if (ey + 24 > by0 && ey - 24 < by1)
                arrow(im, ex, ey, dx, dy, mh_hex(g->exit_open ? 0x6AF07A : GOLD), (int)(230 * pulse));
        }
    }
    /* the banner */
    if (s->msg >= 0 && s->msg < MSG_N && h->msg[s->msg].a) {
        const mh_mask_t *m = &h->msg[s->msg];
        float t = s->msg_t;
        int a = t < 0.15f ? (int)(t / 0.15f * 255) : t > 1.4f ? (int)((1.8f - t) / 0.4f * 255) : 255;
        if (a < 0) a = 0;
        int y = 120 - (int)(t < 0.15f ? (0.15f - t) * 80 : 0);
        if (y + m->h > by0 && y < by1) {
            uint32_t col = s->msg == MSG_TIMEUP || s->msg == MSG_LOW ? 0xFF7070 :
                           s->msg == MSG_OPEN ? 0x8AF59A : 0xFFE070;
            outline_text(im, m, (MH_W - m->w) / 2, y, mh_hex(col), a);
        }
    }
    if (s->show_title && h->title.a) {
        const mh_mask_t *m = &h->title;
        int y = 70;
        if (y + m->h > by0 && y < by1) outline_text(im, m, (MH_W - m->w) / 2, y, mh_hex(0xFFFFFF), 255);
    }
}
