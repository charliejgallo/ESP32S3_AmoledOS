/*
 * MILA - the world map (see ml_map.h)
 *
 * Strip coordinates: pixels from the top of the whole strip. Panels are
 * stacked edge to edge, top to bottom: map_soon, the last world ... the
 * first world, map_home (each panel's picture carries the black gap and the
 * paw prints that join it to the next).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "ml_app.h"
#include "ml_audio.h"
#include "ml_map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAP         0           /* the panels carry their own gap and paw prints */
#define MAXP        (ML_MAX_WORLDS + 2)
#define MAXN        ML_MAX_LEVELS

typedef struct {
    ml_anim_t pic;
    int       world;            /* -1 home, -2 soon                        */
    int       y;                /* strip y of its top                      */
    int       h;
    int       nn;
    int16_t   node[MAXN][2];    /* stones, panel px                        */
    int16_t   house[4];         /* home: the house's tap box               */
    bool      open;
} panel_t;

typedef struct {
    panel_t   p[MAXP];
    int       np;
    int       strip_h;
    ml_anim_t stone, stone_done, stone_locked, ring, marker;
    float     scroll, vel;      /* strip y at the screen's top             */
    float     t;
    int       cur_w, cur_l;     /* where Mila stands                        */
    /* the finger (LVGL thread writes) */
    volatile bool   held;
    volatile float  drag_to;
    volatile bool   tap;
    volatile int    tap_x, tap_y;
    float     last_y;
    uint32_t  last_ms;
    float     fling;
    int       press_x, press_y;
    bool      pressed, dragged;
} map_t;

static map_t *M(app_t *a) { return (map_t *)a->map; }

static void load_nodes(panel_t *p, const char *panel)
{
    char nm[48];
    snprintf(nm, sizeof nm, "%s_nodes", panel);
    uint32_t len = 0;
    char *b = (char *)ml_art_blob(nm, &len);
    if (!b) return;
    for (char *line = b; line && *line;) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;
        if (!strncmp(line, "nodes ", 6)) {
            char *q = line + 6;
            int x, y, used;
            while (p->nn < MAXN && sscanf(q, "%d,%d%n", &x, &y, &used) == 2) {
                p->node[p->nn][0] = (int16_t)x;
                p->node[p->nn][1] = (int16_t)y;
                p->nn++;
                q += used;
                while (*q == ' ') q++;
            }
        } else if (!strncmp(line, "house ", 6)) {
            int v[4];
            if (sscanf(line + 6, "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]) == 4)
                for (int i = 0; i < 4; i++) p->house[i] = (int16_t)v[i];
        }
        line = eol ? eol + 1 : NULL;
    }
    free(b);
}

/* the level Mila stands on: the last played, else the first not solved */
static void pick_current(app_t *a, map_t *m)
{
    m->cur_w = 0;
    m->cur_l = 0;
    char id[24];
    int lv = 0;
    if (sscanf(a->prog.last, "%23s %d", id, &lv) == 2) {
        int w = ml_worlds_find(&a->worlds, id);
        if (w >= 0 && lv >= 0 && lv < a->worlds.w[w].nlevels) {
            m->cur_w = w;
            m->cur_l = lv;
            /* solved it: the next one, if open */
            if (a->prog.stars[w][lv] && lv + 1 < a->worlds.w[w].nlevels && mla_level_open(a, w, lv + 1))
                m->cur_l = lv + 1;
            return;
        }
    }
    for (int w = 0; w < a->worlds.nworlds; w++)
        for (int l = 0; l < a->worlds.w[w].nlevels; l++)
            if (!a->prog.stars[w][l] && mla_level_open(a, w, l)) {
                m->cur_w = w;
                m->cur_l = l;
                return;
            }
}

bool mlm_open(app_t *a)
{
    mlm_close(a);
    map_t *m = (map_t *)ml_calloc(1, sizeof(map_t));
    if (!m) return false;
    /* top to bottom: soon, worlds last..first, home */
    int order[MAXP], n = 0;
    order[n++] = -2;
    for (int w = a->worlds.nworlds - 1; w >= 0 && n < MAXP - 1; w--) order[n++] = w;
    order[n++] = -1;
    int y = 0;
    for (int i = 0; i < n; i++) {
        panel_t *p = &m->p[m->np];
        p->world = order[i];
        const char *name = p->world == -1 ? "map_home" : p->world == -2 ? "map_soon" : a->worlds.w[p->world].panel;
        if (!ml_art_load(name, &p->pic)) {
            if (p->world == -2) continue;           /* no "soon" yet: fine */
        }
        p->h = p->pic.n ? p->pic.f[0].h : 300;
        p->y = y;
        p->open = p->world < 0 || mla_world_open(a, p->world);
        load_nodes(p, name);
        y += p->h + GAP;
        m->np++;
        ml_yield();
    }
    m->strip_h = y - GAP;
    ml_art_load("map_stone", &m->stone);
    ml_art_load("map_stone_done", &m->stone_done);
    ml_art_load("map_stone_locked", &m->stone_locked);
    ml_art_load("map_stone_ring", &m->ring);
    ml_art_load("map_mila", &m->marker);
    pick_current(a, m);
    /* start with Mila's stone a little below the middle */
    m->scroll = (float)(m->strip_h - ML_H);
    for (int i = 0; i < m->np; i++) {
        panel_t *p = &m->p[i];
        if (p->world == m->cur_w && m->cur_l < p->nn) m->scroll = (float)(p->y + p->node[m->cur_l][1] - ML_H * 0.58f);
    }
    if (m->scroll < 0) m->scroll = 0;
    if (m->scroll > m->strip_h - ML_H) m->scroll = (float)(m->strip_h - ML_H);
    m->drag_to = -1;
    a->map = m;
    return true;
}

void mlm_close(app_t *a)
{
    map_t *m = M(a);
    if (!m) return;
    a->map = NULL;
    for (int i = 0; i < m->np; i++) ml_anim_free(&m->p[i].pic);
    ml_anim_free(&m->stone);
    ml_anim_free(&m->stone_done);
    ml_anim_free(&m->stone_locked);
    ml_anim_free(&m->ring);
    ml_anim_free(&m->marker);
    free(m);
}

void mlm_step(app_t *a, float dt)
{
    map_t *m = M(a);
    if (!m) return;
    m->t += dt;
    if (m->held) {
        float to = m->drag_to;
        if (to >= 0) m->scroll = to;
        m->vel = m->fling;
    } else {
        m->scroll += m->vel * dt;
        m->vel *= expf(-dt * 3.5f);
        if (fabsf(m->vel) < 5) m->vel = 0;
    }
    float lo = 0, hi = (float)(m->strip_h - ML_H);
    if (hi < 0) hi = 0;
    if (m->scroll < lo) { m->scroll = lo; m->vel = 0; }
    if (m->scroll > hi) { m->scroll = hi; m->vel = 0; }
    (void)a;
}

/* a sprite whose anchor is at screen (x, y), no depth */
static void blit(ml_img_t *im, const ml_spr_t *s, int fmt, int x, int y, int alpha)
{
    if (!s) return;
    int x0s = x - s->ax, y0s = y - s->ay;
    int r0 = im->cy0 - y0s, r1 = im->cy1 - y0s;
    if (r0 < 0) r0 = 0;
    if (r1 > s->h) r1 = s->h;
    int bpp = fmt == ML_PX_COL ? 4 : fmt == ML_PX_IMG ? 3 : fmt == ML_PX_RGB ? 2 : 1;
    for (int r = r0; r < r1; r++) {
        int a0, a1;
        const uint8_t *p = ml_spr_row(s, r, &a0, &a1);
        int c0 = x0s + a0, c1 = x0s + a1;
        if (c0 < im->cx0) { p += (size_t)(im->cx0 - c0) * bpp; c0 = im->cx0; }
        if (c1 > im->cx1) c1 = im->cx1;
        uint16_t *dst = im->px + (size_t)(y0s + r) * im->w;
        if (fmt == ML_PX_RGB && alpha >= 255) {
            if (c1 > c0) memcpy(dst + c0, p, (size_t)(c1 - c0) * 2);
            continue;
        }
        for (int xx = c0; xx < c1; xx++, p += bpp) {
            uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
            int al = fmt == ML_PX_RGB ? 255 : p[2];
            if (!al) continue;
            if (alpha < 255) al = al * alpha >> 8;
            dst[xx] = ml_blend(dst[xx], c, al);
        }
    }
}

static const ml_spr_t *f0(const ml_anim_t *a)
{
    return a->n ? &a->f[0] : NULL;
}

void mlm_band(app_t *a, ml_img_t *im, int y0, int y1)
{
    map_t *m = M(a);
    ml_rect(im, 0, y0, ML_W, y1 - y0, 0);
    if (!m) return;
    const ml_hud_t *h = &a->hud;
    int sc = ml_iround(m->scroll);
    for (int i = 0; i < m->np; i++) {
        panel_t *p = &m->p[i];
        int top = p->y - sc;
        if (top >= y1 + 60 || top + p->h + GAP <= y0) continue;
        const ml_spr_t *s = f0(&p->pic);
        if (s) blit(im, s, p->pic.fmt, s->ax, top + s->ay, 255);
        if (!p->open) {
            /* dimmed, with the stars it needs */
            int a0 = top > im->cy0 ? top : im->cy0, a1 = top + p->h < im->cy1 ? top + p->h : im->cy1;
            for (int y = a0; y < a1; y++) {
                uint16_t *row = im->px + (size_t)y * im->w;
                for (int x = 0; x < ML_W; x++) row[x] = ml_darken(row[x], 90);
            }
            int need = p->world >= 0 ? a->worlds.w[p->world].need : 0;
            int cy = top + p->h / 2;
            int wn = ml_hud_number_w(h, need, true);
            ml_hud_pill(im, ML_W / 2 - wn / 2 - 34, cy - 24, wn + 68, 48, 0, 190);
            ml_hud_star(im, ML_W / 2 - wn / 2 - 12, cy, 13, ml_rgb(255, 205, 70));
            ml_hud_number(h, im, need, ML_W / 2 - wn / 2 + 8, cy - h->dig[0].h / 2, true, 0xFFFF, 255);
        }
        if (p->world >= 0) {
            /* its name on a pill at the panel's top */
            const ml_mask_t *nm = &a->wname[p->world];
            if (nm->a && top + 18 < ML_H - 90) {
                int ty = top + 18;
                ml_hud_pill(im, 14, ty - nm->h / 2 - 5, nm->w + 24, nm->h + 10, 0, 160);
                ml_mask_draw(im, nm, 26, ty - nm->h / 2, p->open ? 0xFFFF : ml_rgb(170, 170, 180), 255);
            }
            for (int k = 0; k < p->nn && k < a->worlds.w[p->world].nlevels; k++) {
                int x = p->node[k][0], y = top + p->node[k][1];
                if (y < y0 - 60 || y > y1 + 60) continue;
                int stars = a->prog.stars[p->world][k];
                bool open = mla_level_open(a, p->world, k);
                const ml_anim_t *st = stars ? &m->stone_done : open ? &m->stone : &m->stone_locked;
                bool cur = p->world == m->cur_w && k == m->cur_l;
                if (cur && m->ring.n) {
                    int al = 170 + (int)(80 * sinf(m->t * 3));
                    blit(im, f0(&m->ring), m->ring.fmt, x, y, al);
                }
                blit(im, f0(st), st->fmt, x, y, 255);
                if (!st->n) ml_disc(im, x * 16, y * 16, 16 * 16, open ? ml_rgb(250, 235, 220) : ml_rgb(80, 80, 90), 255);
                for (int j = 0; j < stars; j++) ml_hud_star(im, x - 14 + j * 14, y + 24, 6, ml_rgb(255, 205, 70));
                if (cur) {
                    float bob = sinf(m->t * 4) * 2;
                    blit(im, f0(&m->marker), m->marker.fmt, x, y - 6 + (int)bob, 255);
                }
            }
        }
    }
    /* the HUD: stars and coins at the top, home at the bottom-left */
    if (y0 < 60) {
        int st = ml_prog_total_stars(&a->prog, &a->worlds);
        int ws = ml_hud_number_w(h, st, false);
        ml_hud_pill(im, ML_W - ws - 56, 10, ws + 46, 32, 0, 170);
        ml_hud_star(im, ML_W - ws - 36, 26, 9, ml_rgb(255, 205, 70));
        ml_hud_number(h, im, st, ML_W - ws - 22, 26 - h->sdig[0].h / 2, false, 0xFFFF, 255);
        int wn = ml_hud_number_w(h, a->prog.coins, false);
        ml_hud_pill(im, 10, 10, wn + 46, 32, 0, 170);
        ml_disc(im, 28 * 16, 26 * 16, 9 * 16, ml_rgb(255, 196, 40), 255);
        ml_disc(im, 28 * 16, 26 * 16, 5 * 16, ml_rgb(255, 225, 120), 255);
        ml_hud_number(h, im, a->prog.coins, 42, 26 - h->sdig[0].h / 2, false, ml_rgb(255, 225, 130), 255);
    }
    if (y1 > ML_H - 80) ml_hud_button_ico(im, h, ICO_HOME, &h->sym[SYM_HOME], 38, ML_H - 38, 24, false);
}

void mlm_touch(app_t *a, int code, int x, int y)
{
    map_t *m = M(a);
    if (!m) return;
    uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        m->press_x = x;
        m->press_y = y;
        m->pressed = true;
        m->dragged = false;
        m->last_y = (float)y;
        m->last_ms = now;
        m->fling = 0;
        m->drag_to = m->scroll;
        m->held = true;
        return;
    }
    if (!m->pressed) return;
    if (code == LV_EVENT_PRESSING) {
        if (abs(y - m->press_y) > 10 || abs(x - m->press_x) > 10) m->dragged = true;
        if (m->dragged) {
            float dy = (float)y - m->last_y;
            m->drag_to -= dy;
            uint32_t dtm = now - m->last_ms;
            if (dtm > 0) m->fling = -dy * 1000.0f / (float)dtm * 0.6f + m->fling * 0.4f;
            m->last_y = (float)y;
            m->last_ms = now;
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        m->pressed = false;
        m->held = false;
        if (now - m->last_ms > 90) m->fling = 0;
        if (m->dragged || code == LV_EVENT_PRESS_LOST) return;
        if ((x - 38) * (x - 38) + (y - (ML_H - 38)) * (y - (ML_H - 38)) < 32 * 32) {
            ml_snd(SND_SELECT);
            mla_set_state(a, ST_CASITA);
            return;
        }
        int sc = ml_iround(m->scroll);
        for (int i = 0; i < m->np; i++) {
            panel_t *p = &m->p[i];
            int top = p->y - sc;
            if (p->world == -1 && p->house[2] > p->house[0]) {
                if (x >= p->house[0] && x < p->house[2] && y >= top + p->house[1] && y < top + p->house[3]) {
                    ml_snd(SND_SELECT);
                    mla_set_state(a, ST_CASITA);
                    return;
                }
            }
            if (p->world < 0) continue;
            for (int k = 0; k < p->nn && k < a->worlds.w[p->world].nlevels; k++) {
                int dx = x - p->node[k][0], dy = y - (top + p->node[k][1]);
                if (dx * dx + dy * dy > 30 * 30) continue;
                if (mla_level_open(a, p->world, k)) {
                    ml_snd(SND_SELECT);
                    mla_level_start(a, p->world, k);
                } else {
                    ml_snd(SND_BUMP);
                }
                return;
            }
        }
    }
}
