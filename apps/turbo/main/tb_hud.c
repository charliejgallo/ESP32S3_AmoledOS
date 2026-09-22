/*
 * TURBO - the HUD (see tb_hud.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "tb_hud.h"
#include "tb_render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Baking
 * -------------------------------------------------------------------------- */

bool tb_hud_bake(tb_sprite_t *out, const tb_mask_t *m, uint32_t top, uint32_t bottom, int outline)
{
    memset(out, 0, sizeof(*out));
    if (!m->a || m->w <= 0 || m->h <= 0) return false;
    int o = outline;
    int w = m->w + o * 2, h = m->h + o * 2;
    out->px = (uint16_t *)tb_malloc((size_t)w * h * 2);
    out->a = (uint8_t *)tb_calloc((size_t)w * h, 1);
    if (!out->px || !out->a) {
        free(out->px);
        free(out->a);
        out->px = NULL;
        out->a = NULL;
        return false;
    }
    out->w = (int16_t)w;
    out->h = (int16_t)h;
    out->ox = 0;
    out->oy = 0;
    /* the outline: the mask dilated by a disc of radius o */
    uint8_t *ol = (uint8_t *)tb_calloc((size_t)w * h, 1);
    if (ol && o > 0) {
        for (int y = 0; y < m->h; y++) {
            for (int x = 0; x < m->w; x++) {
                int a = m->a[y * m->w + x];
                if (a < 40) continue;
                for (int dy = -o; dy <= o; dy++) {
                    for (int dx = -o; dx <= o; dx++) {
                        if (dx * dx + dy * dy > o * o + o) continue;
                        uint8_t *p = &ol[(size_t)(y + o + dy) * w + x + o + dx];
                        if (*p < a) *p = (uint8_t)a;
                    }
                }
            }
        }
    }
    for (int y = 0; y < h; y++) {
        int t = h > 1 ? y * 256 / (h - 1) : 0;
        uint32_t c = tb_mix(top, bottom, t);
        int cr = (int)(c >> 16) & 255, cg = (int)(c >> 8) & 255, cb = (int)c & 255;
        for (int x = 0; x < w; x++) {
            int mx = x - o, my = y - o;
            int a = (mx >= 0 && my >= 0 && mx < m->w && my < m->h) ? m->a[my * m->w + mx] : 0;
            int oa = ol ? ol[(size_t)y * w + x] : 0;
            size_t i = (size_t)y * w + x;
            /* colour over the dark outline */
            int r = (cr * a + 10 * (255 - a)) / 255, g = (cg * a + 8 * (255 - a)) / 255, b = (cb * a + 14 * (255 - a)) / 255;
            out->px[i] = tb_rgb(r, g, b);
            out->a[i] = (uint8_t)(a > oa ? a : oa);
        }
    }
    free(ol);
    tb_sprite_runs(out);
    return true;
}

static void make_pedal(tb_sprite_t *s, bool gas, bool pressed)
{
    int w = gas ? 76 : 104, h = gas ? 100 : 80;
    s->w = (int16_t)w;
    s->h = (int16_t)h;
    s->ox = 0;
    s->oy = 0;
    s->px = (uint16_t *)tb_malloc((size_t)w * h * 2);
    s->a = (uint8_t *)tb_calloc((size_t)w * h, 1);
    if (!s->px || !s->a) return;
    int r = 14;
    int inset = pressed ? 3 : 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xx = x - inset, yy = y - inset, ww = w - inset * 2, hh = h - inset * 2;
            float cov = 1.0f;
            if (xx < 0 || yy < 0 || xx >= ww || yy >= hh) continue;
            float qx = 0, qy = 0;
            if (xx < r) qx = (float)(r - xx) - 0.5f;
            else if (xx >= ww - r) qx = (float)(xx - (ww - r)) + 0.5f;
            if (yy < r) qy = (float)(r - yy) - 0.5f;
            else if (yy >= hh - r) qy = (float)(yy - (hh - r)) + 0.5f;
            if (qx > 0 && qy > 0) {
                cov = (float)r - sqrtf(qx * qx + qy * qy) + 0.5f;
                if (cov <= 0) continue;
                if (cov > 1) cov = 1;
            }
            /* brushed metal rim, a rubber or aluminium face */
            int edge = xx < 5 || yy < 5 || xx >= ww - 5 || yy >= hh - 5;
            int v;
            if (edge) {
                v = 170 - yy * 60 / hh;
            } else if (gas) {
                /* aluminium with rows of oval holes */
                v = 150 + (xx * 20 / ww) - (yy * 30 / hh);
                int cx = (xx - 12) % 13, cy = (yy - 10) % 16;
                if (xx > 10 && xx < ww - 10 && yy > 8 && yy < hh - 8 && cx >= 0 && cx < 7 && cy >= 0 && cy < 10) v = 40;
            } else {
                /* rubber with horizontal ridges */
                v = 60 + ((yy / 6) & 1 ? 26 : 0) - (yy * 20 / hh);
            }
            if (pressed) v = v * 3 / 4;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            size_t i = (size_t)y * w + x;
            s->px[i] = gas ? tb_rgb(v, v, v + 8 > 255 ? 255 : v + 8) : tb_rgb(v, v, v);
            /* opaque inside, only the rounded edge blends: a see-through pedal
             * cost a blend for each of its 16,000 pixels in every frame */
            s->a[i] = (uint8_t)(cov * 255.0f);
        }
    }
    tb_sprite_runs(s);
}

static void make_pause(tb_sprite_t *s)
{
    int d = 34;
    s->w = s->h = (int16_t)d;
    s->ox = s->oy = 0;
    s->px = (uint16_t *)tb_malloc((size_t)d * d * 2);
    s->a = (uint8_t *)tb_calloc((size_t)d * d, 1);
    if (!s->px || !s->a) return;
    float c = (d - 1) * 0.5f;
    for (int y = 0; y < d; y++) {
        for (int x = 0; x < d; x++) {
            float dx = x - c, dy = y - c;
            float cov = (float)d * 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            bool bar = (y >= 10 && y < 24) && ((x >= 11 && x < 15) || (x >= 19 && x < 23));
            size_t i = (size_t)y * d + x;
            s->px[i] = bar ? tb_rgb(255, 255, 255) : tb_rgb(0, 0, 0);
            s->a[i] = (uint8_t)(cov * (bar ? 255.0f : 120.0f));
        }
    }
}

void tb_hud_make_pedals(tb_hud_t *h)
{
    make_pedal(&h->pedal[0][0], false, false);
    make_pedal(&h->pedal[0][1], false, true);
    make_pedal(&h->pedal[1][0], true, false);
    make_pedal(&h->pedal[1][1], true, true);
    make_pause(&h->pause);
}

static void spr_free(tb_sprite_t *s)
{
    tb_sprite_free(s);
}

void tb_hud_free(tb_hud_t *h)
{
    for (int i = 0; i < TB_GLYPHS; i++) {
        spr_free(&h->big[i]);
        spr_free(&h->mid[i]);
        spr_free(&h->sml[i]);
    }
    for (int i = 0; i < TX_N; i++) spr_free(&h->word[i]);
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) spr_free(&h->pedal[i][j]);
    spr_free(&h->pause);
    h->ok = false;
}

/* --------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------- */

static int glyph(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
    case ':': return 10;
    case '.': return 11;
    case '+': return 12;
    case '-': return 13;
    default: return -1;
    }
}

/* the glyphs overlap by their outline */
static int text_w(const tb_sprite_t *set, const char *s, int kern)
{
    int w = 0;
    for (; *s; s++) {
        int g = glyph(*s);
        if (g >= 0 && set[g].px) w += set[g].w - kern;
    }
    return w + kern;
}

static void text_draw(tb_img_t *im, const tb_sprite_t *set, const char *s, int x, int y, int kern)
{
    for (; *s; s++) {
        int g = glyph(*s);
        if (g < 0 || !set[g].px) continue;
        tb_sprite(im, &set[g], x, y);
        x += set[g].w - kern;
    }
}

static void text_center(tb_img_t *im, const tb_sprite_t *set, const char *s, int cx, int y, int kern)
{
    text_draw(im, set, s, cx - text_w(set, s, kern) / 2, y, kern);
}

/* nearest, integer scale: the countdown */
static void sprite_x2(tb_img_t *im, const tb_sprite_t *s, int x, int y)
{
    for (int yy = 0; yy < s->h * 2; yy++) {
        int dy = y + yy;
        if (dy < im->cy0 || dy >= im->cy1) continue;
        uint16_t *d = im->px + (size_t)dy * im->w;
        const uint16_t *sp = s->px + (size_t)(yy >> 1) * s->w;
        const uint8_t *sa = s->a + (size_t)(yy >> 1) * s->w;
        for (int xx = 0; xx < s->w * 2; xx++) {
            int dx = x + xx;
            if (dx < im->cx0 || dx >= im->cx1) continue;
            int a = sa[xx >> 1];
            if (a) d[dx] = a >= 250 ? sp[xx >> 1] : tb_blend(d[dx], sp[xx >> 1], a);
        }
    }
}

static void word_center(tb_img_t *im, const tb_hud_t *h, int w, int cx, int y)
{
    const tb_sprite_t *s = &h->word[w];
    if (s->px) tb_sprite(im, s, cx - s->w / 2, y);
}

static void fmt_time(char *b, int n, float t, bool tenths)
{
    if (t < 0) t = 0;
    int ds = (int)(t * 10.0f);
    int m = ds / 600, sec = (ds / 10) % 60, d = ds % 10;
    if (tenths) snprintf(b, (size_t)n, "%d:%02d.%d", m, sec, d);
    else snprintf(b, (size_t)n, "%d:%02d", m, sec);
}

void tb_hud_events(tb_hud_state_t *st, const tb_game_t *g, uint32_t ev, float dt)
{
    if (st->banner_t > 0) {
        st->banner_t -= dt;
        if (st->banner_t <= 0) {
            st->banner = -1;
            st->split_show = false;
        }
    }
    if (ev & EV_GO) {
        st->banner = TX_GO;
        st->banner_t = 1.0f;
    }
    if (ev & EV_CHECKPOINT) {
        st->banner = TX_EXTRA;
        st->banner_t = 2.2f;
        st->added = g->cp_added;
    }
    if (ev & EV_FINISH) {
        st->banner = TX_FINISH;
        st->banner_t = 99.0f;
    }
    if (ev & EV_TIMEUP) {
        st->banner = TX_TIMEUP;
        st->banner_t = 99.0f;
    }
    st->low_time = g->state == RS_RACING && g->time_left < 10.0f;
}

void tb_hud_prepare(tb_hud_state_t *st, const tb_game_t *g)
{
    st->blink = ((int)(g->elapsed * 4.0f) & 1) != 0;
    int secs = (int)ceilf(g->time_left);
    snprintf(st->t_clock, sizeof st->t_clock, "%d", secs < 0 ? 0 : secs);
    fmt_time(st->t_elapsed, sizeof st->t_elapsed, g->elapsed, true);
    snprintf(st->t_speed, sizeof st->t_speed, "%d", tb_game_kmh(g));
    snprintf(st->t_gear, sizeof st->t_gear, "%d", g->gear);
    snprintf(st->t_extra, sizeof st->t_extra, "+%d", (int)(st->added + 0.5f));
    fmt_time(st->t_final, sizeof st->t_final, g->elapsed, true);
    st->count = 0;
    if (g->state == RS_COUNTDOWN) {
        int n = 3 - (int)g->t_state;
        if (n >= 1 && n <= 3) st->count = n;
    }
}

void tb_hud_draw(const tb_hud_t *h, tb_img_t *im, const tb_game_t *g, const tb_hud_state_t *st)
{
    bool blink = st->blink;
    int y0 = im->cy0, y1 = im->cy1;

    /* the top: the clock, the elapsed time, the progress, the pause button */
    if (y0 < 96) {
        word_center(im, h, TX_TIME, TB_CX, 4);
        if (!(st->low_time && blink)) text_center(im, h->big, st->t_clock, TB_CX, 26, 4);
        int ew = text_w(h->sml, st->t_elapsed, 2);
        text_draw(im, h->sml, st->t_elapsed, TB_W - 8 - ew, 10, 2);
        int bx0 = 92, bx1 = TB_W - 92, by = 88;
        tb_rect_blend(im, bx0, by, bx1 - bx0, 4, tb_rgb(0, 0, 0), 150);
        const tb_track_t *t = g->trk;
        float fin = (float)t->cp_seg[t->ncp - 1];
        for (int i = 0; i < t->ncp; i++) {
            int x = bx0 + (int)((float)(bx1 - bx0) * (float)t->cp_seg[i] / fin);
            bool passed = i < g->next_cp;
            tb_rect(im, x - 1, by - 3, 3, 10, passed ? tb_rgb(255, 210, 40) : tb_rgb(200, 200, 200));
        }
        int px = bx0 + (int)((float)(bx1 - bx0) * tb_game_progress(g));
        tb_rect(im, bx0, by, px - bx0, 4, tb_rgb(255, 180, 30));
        if (st->rival) {
            int rx = bx0 + (int)((float)(bx1 - bx0) * st->rival_prog);
            tb_disc(im, rx * 16 + 8, (by + 2) * 16, 5 * 16, tb_rgb(60, 200, 255), 255);
        }
        tb_disc(im, px * 16 + 8, (by + 2) * 16, 5 * 16, tb_rgb(255, 255, 255), 255);
        if (h->pause.px) tb_sprite(im, &h->pause, 8, 6);
    }

    /* the banners, the middle */
    if (y1 > 140 && y0 < 260) {
        if (st->count) {
            const tb_sprite_t *s = &h->big[st->count];
            if (s->px) sprite_x2(im, s, TB_CX - s->w, 150);
        } else if (st->banner >= 0) {
            word_center(im, h, st->banner, TB_CX, 150);
            if (st->banner == TX_EXTRA) text_center(im, h->big, st->t_extra, TB_CX, 184, 4);
            if (st->banner == TX_FINISH || st->banner == TX_TIMEUP) text_center(im, h->mid, st->t_final, TB_CX, 196, 3);
        } else if (st->low_time && blink) {
            word_center(im, h, TX_HURRY, TB_CX, 150);
        }
    }

    /* the bottom: the pedals and the speed */
    if (y1 > TB_PEDAL_Y - 10) {
        const tb_sprite_t *pb = &h->pedal[0][st->brake ? 1 : 0];
        const tb_sprite_t *pg = &h->pedal[1][st->gas ? 1 : 0];
        if (pb->px) tb_sprite(im, pb, TB_BRAKE_X0 + 4, TB_PEDAL_Y + TB_PEDAL_H - pb->h - 4);
        if (pg->px) tb_sprite(im, pg, TB_GAS_X1 - pg->w - 4, TB_PEDAL_Y + TB_PEDAL_H - pg->h - 4);
        text_center(im, h->mid, st->t_speed, TB_CX, 380, 3);
        word_center(im, h, TX_KMH, TB_CX, 424);
        text_draw(im, h->sml, st->t_gear, TB_CX - 70, 392, 2);
        int rw = (int)(g->rpm * 90.0f);
        tb_rect_blend(im, TB_CX - 45, 418, 90, 3, tb_rgb(0, 0, 0), 140);
        tb_rect(im, TB_CX - 45, 418, rw, 3, g->rpm > 0.9f ? tb_rgb(255, 60, 40) : tb_rgb(120, 220, 255));
    }
}
