/*
 * MONSTER HOP - the desktop's finishing touches (see mh_post.h)
 */
#ifdef MH_DESKTOP

#include "mh_post.h"
#include "mh_art.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sizes and speeds in screen pixels times the art's scale */
#define P MH_PX

enum { B_ASH = 1, B_MOTE, B_SAND, B_FIREFLY, B_LEAF, B_EMBER, B_PLANKTON, B_BUBBLE };

/* ---- the zone's bits ---- */

typedef struct {
    uint8_t kind, count;
} mix_t;

static int zone_mix(int zone, mix_t *m)
{
    switch (zone) {
    case ZONE_CITY: m[0] = (mix_t){ B_ASH, 45 }; return 1;
    case ZONE_CASTLE: m[0] = (mix_t){ B_MOTE, 36 }; return 1;
    case ZONE_DESERT: m[0] = (mix_t){ B_SAND, 55 }; return 1;
    case ZONE_FOREST: m[0] = (mix_t){ B_FIREFLY, 26 }; m[1] = (mix_t){ B_LEAF, 16 }; return 2;
    case ZONE_DINO: m[0] = (mix_t){ B_EMBER, 42 }; m[1] = (mix_t){ B_ASH, 22 }; return 2;
    case ZONE_BAY: m[0] = (mix_t){ B_PLANKTON, 44 }; m[1] = (mix_t){ B_BUBBLE, 12 }; return 2;
    default: return 0;
    }
}

static float frand(uint32_t *s)
{
    return mh_randf(s);
}

static void spawn(mh_post_t *p, int i, int kind, int cam_x, int cam_y, int w, int h, bool anywhere)
{
    uint32_t *r = &p->rnd;
    p->bit[i].kind = (uint8_t)kind;
    p->bit[i].x = cam_x - 40 * P + frand(r) * (w + 80 * P);
    p->bit[i].y = cam_y - 40 * P + frand(r) * (h + 80 * P);
    p->bit[i].t = anywhere ? frand(r) * 3.0f : 0;
    p->bit[i].phase = frand(r) * 6.28f;
    float life = 4.0f + frand(r) * 5.0f;
    float vx = 0, vy = 0;
    switch (kind) {
    case B_ASH: vx = 6 + frand(r) * 8; vy = 10 + frand(r) * 10; break;
    case B_MOTE: vx = -4 + frand(r) * 8; vy = -(8 + frand(r) * 10); break;
    case B_SAND: vx = 160 + frand(r) * 120; vy = 6 + frand(r) * 10; life = 1.2f + frand(r) * 1.5f; break;
    case B_FIREFLY: vx = -12 + frand(r) * 24; vy = -12 + frand(r) * 24; break;
    case B_LEAF: vx = 14 + frand(r) * 16; vy = 22 + frand(r) * 18; break;
    case B_EMBER: vx = -6 + frand(r) * 12; vy = -(22 + frand(r) * 26); life = 2.5f + frand(r) * 3.0f; break;
    case B_PLANKTON: vx = -4 + frand(r) * 8; vy = -3 + frand(r) * 6; life = 5.0f + frand(r) * 6.0f; break;
    case B_BUBBLE: vx = -2 + frand(r) * 4; vy = -(18 + frand(r) * 14); life = 2.0f + frand(r) * 2.0f; break;
    default: break;
    }
    p->bit[i].vx = vx * P;
    p->bit[i].vy = vy * P;
    p->bit[i].life = life;
}

/* light added around a point: a soft disc that saturates */
static void glow(mh_img_t *im, int cx, int cy, int r, uint32_t rgb, int a)
{
    if (a <= 0) return;
    uint16_t c = mh_hex(rgb);
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < im->cy0 || y >= im->cy1) continue;
        uint16_t *row = im->px + (size_t)y * im->w;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < im->cx0 || x >= im->cx1) continue;
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            int k = a * (r * r - d2) / (r * r);
            row[x] = mh_add(row[x], mh_scale(c, k));
        }
    }
}

void mhp_bits(mh_post_t *p, int zone, mh_img_t *im, int cam_x, int cam_y, int w, int h, float dt)
{
    mix_t mix[2];
    int nm = zone_mix(zone, mix);
    if (!p->rnd) p->rnd = 0x51ED270Bu;
    p->clock += dt;
    int want = 0;
    for (int k = 0; k < nm; k++) want += mix[k].count;
    if (want > MHP_BITS) want = MHP_BITS;
    /* the population of the zone: the first ones of each kind in order */
    if (p->n != want) {
        int i = 0;
        for (int k = 0; k < nm; k++)
            for (int c = 0; c < mix[k].count && i < want; c++, i++) spawn(p, i, mix[k].kind, cam_x, cam_y, w, h, true);
        p->n = want;
    }
    for (int i = 0; i < p->n; i++) {
        float *bx = &p->bit[i].x, *by = &p->bit[i].y;
        p->bit[i].t += dt;
        float wob = sinf(p->clock * 1.7f + p->bit[i].phase);
        int kind = p->bit[i].kind;
        *bx += (p->bit[i].vx + (kind == B_FIREFLY || kind == B_PLANKTON ? wob * 10 : kind == B_EMBER ? wob * 14 : 0) * P) * dt;
        *by += p->bit[i].vy * dt;
        bool out = *bx < cam_x - 60 * P || *bx > cam_x + w + 60 * P || *by < cam_y - 60 * P || *by > cam_y + h + 60 * P;
        if (p->bit[i].t > p->bit[i].life || out) {
            spawn(p, i, kind, cam_x, cam_y, w, h, false);
            continue;
        }
        /* fade in and out over its life */
        float f = p->bit[i].t / p->bit[i].life;
        float fade = f < 0.15f ? f / 0.15f : f > 0.8f ? (1 - f) / 0.2f : 1;
        int x = (int)(*bx - cam_x), y = (int)(*by - cam_y);
        if (x < -8 * P || x > w + 8 * P || y < -8 * P || y > h + 8 * P) continue;
        switch (kind) {
        case B_ASH:
            mh_rect_blend(im, x, y, 2 * P, 2 * P, mh_hex(zone == ZONE_DINO ? 0x6A5A50 : 0xB8BCC8), (int)(110 * fade));
            break;
        case B_MOTE:
            glow(im, x, y, 3 * P, 0xB070FF, (int)(170 * fade * (0.6f + 0.4f * wob)));
            break;
        case B_SAND:
            mh_rect_blend(im, x, y, 7 * P, P, mh_hex(0xF0D8A8), (int)(90 * fade));
            break;
        case B_FIREFLY: {
            /* they blink */
            float on = sinf(p->clock * 3.1f + p->bit[i].phase * 3.0f);
            if (on > 0) glow(im, x, y, 4 * P, 0xC8FF60, (int)(230 * fade * on));
            break;
        }
        case B_LEAF: {
            bool wide = ((int)(p->clock * 5 + p->bit[i].phase * 4)) & 1;
            mh_rect_blend(im, x, y, (wide ? 4 : 2) * P, (wide ? 2 : 3) * P, mh_hex(0xC8702A), (int)(200 * fade));
            break;
        }
        case B_EMBER:
            glow(im, x, y, 3 * P, 0xFF7A20, (int)(240 * fade));
            mh_rect_blend(im, x, y, P, P, mh_hex(0xFFE0A0), (int)(255 * fade));
            break;
        case B_PLANKTON:
            glow(im, x, y, 3 * P, (p->bit[i].phase > 3.14f) ? 0x40E8FF : 0xFF60D0, (int)(180 * fade * (0.7f + 0.3f * wob)));
            break;
        case B_BUBBLE:
            mh_disc(im, x * 16, y * 16, 40 * P, mh_hex(0xC8F0FF), (int)(90 * fade));
            break;
        default:
            break;
        }
    }
}

/* ---- bloom and vignette ---- */

static bool scratch(mh_post_t *p, int w, int h)
{
    int sw = w / (4 * P), sh = h / (4 * P);
    if (p->small_r && p->sw == sw && p->sh == sh && p->vig && p->vw == w && p->vh == h) return true;
    free(p->small_r);
    free(p->small_g);
    free(p->small_b);
    free(p->tmp);
    free(p->vig);
    p->sw = sw;
    p->sh = sh;
    p->vw = w;
    p->vh = h;
    size_t n = (size_t)sw * sh;
    p->small_r = (uint16_t *)calloc(n, 2);
    p->small_g = (uint16_t *)calloc(n, 2);
    p->small_b = (uint16_t *)calloc(n, 2);
    p->tmp = (uint16_t *)calloc(n, 2);
    p->vig = (uint8_t *)malloc((size_t)w * h);
    if (!p->small_r || !p->small_g || !p->small_b || !p->tmp || !p->vig) return false;
    /* the vignette: darker only near the edges and corners */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = (x + 0.5f) / w * 2 - 1, dy = (y + 0.5f) / h * 2 - 1;
            float d = sqrtf(dx * dx * 0.8f + dy * dy);
            float k = d < 0.62f ? 0 : (d - 0.62f) / 0.6f;
            if (k > 1) k = 1;
            p->vig[(size_t)y * w + x] = (uint8_t)(255 - (int)(k * k * 105));
        }
    }
    return true;
}

static void blur(uint16_t *ch, uint16_t *tmp, int w, int h)
{
    const int r = 2, n = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        uint16_t *row = ch + (size_t)y * w, *out = tmp + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int s = 0;
            for (int k = -r; k <= r; k++) {
                int xx = x + k < 0 ? 0 : x + k >= w ? w - 1 : x + k;
                s += row[xx];
            }
            out[x] = (uint16_t)(s / n);
        }
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            int s = 0;
            for (int k = -r; k <= r; k++) {
                int yy = y + k < 0 ? 0 : y + k >= h ? h - 1 : y + k;
                s += tmp[(size_t)yy * w + x];
            }
            ch[(size_t)y * w + x] = (uint16_t)(s / n);
        }
    }
}

void mhp_finish(mh_post_t *p, int zone, uint16_t *px, int stride, int w, int h)
{
    if (w < 16 || h < 16 || !scratch(p, w, h)) return;
    int sw = p->sw, sh = p->sh;
    const int D = 4 * P;
    /* the bright part of each block of D x D (4 x 4 screen pixels of art) */
    for (int by = 0; by < sh; by++) {
        for (int bx = 0; bx < sw; bx++) {
            int ar = 0, ag = 0, ab = 0;
            for (int y = by * D; y < by * D + D; y++) {
                const uint16_t *row = px + (size_t)y * stride + bx * D;
                for (int x = 0; x < D; x++) {
                    int r, g, b;
                    mh_unpack(row[x], &r, &g, &b);
                    int lum = (r * 3 + g * 6 + b) / 10;
                    /* only what is really bright: lava, flames, lamps, eyes */
                    int wgt = lum - 200;
                    if (wgt <= 0) continue;
                    if (wgt > 55) wgt = 55;
                    ar += r * wgt;
                    ag += g * wgt;
                    ab += b * wgt;
                }
            }
            size_t o = (size_t)by * sw + bx;
            p->small_r[o] = (uint16_t)(ar / (D * D * 55));
            p->small_g[o] = (uint16_t)(ag / (D * D * 55));
            p->small_b[o] = (uint16_t)(ab / (D * D * 55));
        }
    }
    for (int k = 0; k < 2; k++) {
        blur(p->small_r, p->tmp, sw, sh);
        blur(p->small_g, p->tmp, sw, sh);
        blur(p->small_b, p->tmp, sw, sh);
    }
    /* the nights glow more than the days */
    static const int str[ZONE_N] = { 120, 140, 80, 120, 100, 130, 150 };
    int strength = str[zone >= 0 && zone < ZONE_N ? zone : ZONE_TEST];
    for (int y = 0; y < h; y++) {
        uint16_t *row = px + (size_t)y * stride;
        const uint8_t *vg = p->vig + (size_t)y * w;
        float fy = (y + 0.5f) / D - 0.5f;
        int y0 = (int)fy;
        if (fy < 0) y0 = 0;
        int y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
        int wy = (int)((fy - y0) * 256);
        if (wy < 0) wy = 0;
        for (int x = 0; x < w; x++) {
            float fx = (x + 0.5f) / D - 0.5f;
            int x0 = (int)fx;
            if (fx < 0) x0 = 0;
            int x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
            int wx = (int)((fx - x0) * 256);
            if (wx < 0) wx = 0;
            size_t a = (size_t)y0 * sw + x0, b = (size_t)y0 * sw + x1, c = (size_t)y1 * sw + x0, d = (size_t)y1 * sw + x1;
#define LERP(ch) (((ch[a] * (256 - wx) + ch[b] * wx) * (256 - wy) + (ch[c] * (256 - wx) + ch[d] * wx) * wy) >> 16)
            int r = LERP(p->small_r) * strength >> 7, g = LERP(p->small_g) * strength >> 7,
                bb = LERP(p->small_b) * strength >> 7;
#undef LERP
            uint16_t v = row[x];
            if (r | g | bb) v = mh_add(v, mh_rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, bb > 255 ? 255 : bb));
            if (vg[x] < 255) v = mh_darken(v, vg[x] + 1);
            row[x] = v;
        }
    }
}

/* ---- the far scenery ---- */

static struct {
    int       zone;             /* loaded, -1 none                           */
    uint16_t *px;
    int       w, h, hz;
} s_bd = { -1, NULL, 0, 0, 0 };

void mhp_backdrop(mh_world_t *w, int zone)
{
    static const char *const n[ZONE_N] = { "city", "castle", "desert", "forest", "test", "dino", "bay" };
    w->bd = NULL;
    if (zone < 0 || zone >= ZONE_N) return;
    if (s_bd.zone != zone) {
        free(s_bd.px);
        s_bd.px = NULL;
        s_bd.zone = zone;
        char nm[32];
        snprintf(nm, sizeof nm, "bd_%s", n[zone]);
        mh_anim_t an;
        if (!mh_art_has(nm) || !mh_art_load(nm, &an)) return;
        const mh_spr_t *f = &an.f[0];
        s_bd.px = (uint16_t *)malloc((size_t)f->w * f->h * 2);
        if (s_bd.px) {
            s_bd.w = f->w;
            s_bd.h = f->h;
            s_bd.hz = f->ay;
            memset(s_bd.px, 0, (size_t)f->w * f->h * 2);
            /* an IMG sheet: colour lo, hi, alpha per pixel */
            for (int y = 0; y < f->h; y++) {
                int x0, x1;
                const uint8_t *q = mh_spr_row(f, y, &x0, &x1);
                uint16_t *row = s_bd.px + (size_t)y * f->w;
                for (int x = x0; x < x1; x++, q += 3) row[x] = (uint16_t)(q[0] | (q[1] << 8));
            }
        }
        mh_anim_free(&an);
    }
    if (s_bd.px) {
        w->bd = s_bd.px;
        w->bd_w = s_bd.w;
        w->bd_h = s_bd.h;
        w->bd_hz = s_bd.hz;
    }
}

void mhp_free(mh_post_t *p)
{
    free(p->small_r);
    free(p->small_g);
    free(p->small_b);
    free(p->tmp);
    free(p->vig);
    memset(p, 0, sizeof(*p));
}

#endif /* MH_DESKTOP */
