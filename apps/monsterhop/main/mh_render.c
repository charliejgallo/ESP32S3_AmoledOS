/*
 * MONSTER HOP - a frame (see mh_render.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "mh_render.h"

#include <stdlib.h>
#include <string.h>

#define BIAS 2          /* a sprite may sit this far "inside" the ground */

void mh_dlist_clear(mh_dlist_t *l)
{
    l->n = 0;
}

mh_draw_t *mh_dlist_add(mh_dlist_t *l)
{
    if (l->n >= MH_MAX_DRAW) return NULL;
    mh_draw_t *d = &l->d[l->n++];
    memset(d, 0, sizeof(*d));
    d->alpha = 255;
    return d;
}

static int cmp_draw(const void *pa, const void *pb)
{
    const mh_draw_t *a = (const mh_draw_t *)pa, *b = (const mh_draw_t *)pb;
    /* shadows under everything */
    int sa = a->fmt == MH_PX_PLANE, sb = b->fmt == MH_PX_PLANE;
    if (sa != sb) return sb - sa;
    if (a->d != b->d) return (int)b->d - (int)a->d;
    return (int)a->prio - (int)b->prio;
}

void mh_dlist_sort(mh_dlist_t *l)
{
    qsort(l->d, (size_t)l->n, sizeof(mh_draw_t), cmp_draw);
}

/* ---- the sprites ---- */

static inline const uint16_t *cache_row(const mh_world_t *w, int lpy)
{
    int cy = lpy % MH_CH;
    if (cy < 0) cy += MH_CH;
    return w->cd + (size_t)cy * MH_CW;
}

static void draw_lid(const mh_world_t *w, mh_img_t *im, const mh_draw_t *e, int cam_x, int cam_y)
{
    const mh_spr_t *s = e->s;
    const mh_lut_t *lut = e->lut;
    int lx0 = e->x - s->ax, ly0 = e->y - s->ay;         /* LP of the frame's (0,0) */
    int sx = lx0 - cam_x, sy = ly0 - cam_y;
    int r0 = im->cy0 - sy, r1 = im->cy1 - sy;
    if (r0 < 0) r0 = 0;
    if (r1 > s->h) r1 = s->h;
    int ga = e->alpha;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < im->cx0) { p += (size_t)(im->cx0 - c0) * 3; c0 = im->cx0; }
        if (c1 > im->cx1) c1 = im->cx1;
        if (c0 >= c1) continue;
        uint16_t *dst = im->px + (size_t)(sy + r) * im->w;
        const uint16_t *cd = cache_row(w, ly0 + r);
        int lpx = c0 + cam_x;
        for (int x = c0; x < c1; x++, p += 3, lpx++) {
            int ida = p[0];
            int a = (ida & 15) * 17;
            if (!a) continue;
            int sd = e->d + p[2] - 128;
            if (ga < 255) a = a * ga >> 8;
            if (sd < (int)cd[lpx & (MH_CW - 1)] + BIAS || (e->flags & DR_NOZ)) {
                dst[x] = mh_blend(dst[x], lut->c[ida >> 4][p[1] >> 2], a);
            } else if (e->flags & DR_XRAY) {
                dst[x] = mh_blend(dst[x], e->xray, a * 3 >> 3);
            }
        }
    }
}

static void draw_col(const mh_world_t *w, mh_img_t *im, const mh_draw_t *e, int cam_x, int cam_y)
{
    const mh_spr_t *s = e->s;
    int lx0 = e->x - s->ax, ly0 = e->y - s->ay;
    int sx = lx0 - cam_x, sy = ly0 - cam_y;
    int r0 = im->cy0 - sy, r1 = im->cy1 - sy;
    if (r0 < 0) r0 = 0;
    if (r1 > s->h) r1 = s->h;
    int ga = e->alpha;
    bool add = (e->flags & DR_ADD) != 0, noz = (e->flags & DR_NOZ) != 0;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < im->cx0) { p += (size_t)(im->cx0 - c0) * 4; c0 = im->cx0; }
        if (c1 > im->cx1) c1 = im->cx1;
        if (c0 >= c1) continue;
        uint16_t *dst = im->px + (size_t)(sy + r) * im->w;
        const uint16_t *cd = cache_row(w, ly0 + r);
        int lpx = c0 + cam_x;
        for (int x = c0; x < c1; x++, p += 4, lpx++) {
            int a = p[2];
            if (!a || p[3] == MH_Z_EMPTY) continue;
            int sd = e->d + p[3] - 128;
            if (!noz && sd >= (int)cd[lpx & (MH_CW - 1)] + BIAS) continue;
            if (ga < 255) a = a * ga >> 8;
            uint16_t c = (uint16_t)(p[0] | (p[1] << 8));
            if (add) dst[x] = mh_add(dst[x], mh_scale(c, a));
            else dst[x] = mh_blend(dst[x], c, a);
        }
    }
}

static void draw_plane(const mh_world_t *w, mh_img_t *im, const mh_draw_t *e, int cam_x, int cam_y)
{
    const mh_spr_t *s = e->s;
    int lx0 = e->x - s->ax, ly0 = e->y - s->ay;
    int sx = lx0 - cam_x, sy = ly0 - cam_y;
    int r0 = im->cy0 - sy, r1 = im->cy1 - sy;
    if (r0 < 0) r0 = 0;
    if (r1 > s->h) r1 = s->h;
    int k = e->alpha;
    for (int r = r0; r < r1; r++) {
        int x0, x1;
        const uint8_t *p = mh_spr_row(s, r, &x0, &x1);
        int c0 = sx + x0, c1 = sx + x1;
        if (c0 < im->cx0) { p += im->cx0 - c0; c0 = im->cx0; }
        if (c1 > im->cx1) c1 = im->cx1;
        if (c0 >= c1) continue;
        uint16_t *dst = im->px + (size_t)(sy + r) * im->w;
        const uint16_t *cd = cache_row(w, ly0 + r);
        int dp = e->d + mh_iround((float)(ly0 + r - e->y) * MH_DPLANE_PX);
        int lpx = c0 + cam_x;
        for (int x = c0; x < c1; x++, p++, lpx++) {
            int v = *p;
            if (!v) continue;
            int dz = (int)cd[lpx & (MH_CW - 1)] - dp;
            if (dz < -4 || dz > 4) continue;
            dst[x] = mh_darken(dst[x], 256 - (v * k >> 8));
        }
    }
}

void mh_render_band(const mh_world_t *w, mh_img_t *im, int cam_x, int cam_y, int y0, int y1,
                    const mh_dlist_t *l)
{
    /* the background: rows of the cache, wrapping */
    int cx = cam_x & (MH_CW - 1);
    int n1 = MH_CW - cx;
    if (n1 > MH_W) n1 = MH_W;
    for (int y = y0; y < y1; y++) {
        int cy = (cam_y + y) % MH_CH;
        if (cy < 0) cy += MH_CH;
        const uint16_t *src = w->cc + (size_t)cy * MH_CW;
        uint16_t *dst = im->px + (size_t)y * im->w;
        memcpy(dst, src + cx, (size_t)n1 * 2);
        if (n1 < MH_W) memcpy(dst + n1, src, (size_t)(MH_W - n1) * 2);
    }
    for (int i = 0; i < l->n; i++) {
        const mh_draw_t *e = &l->d[i];
        const mh_spr_t *s = e->s;
        if (!s) continue;
        int top = e->y - s->ay - cam_y;
        if (top >= y1 || top + s->h <= y0) continue;
        switch (e->fmt) {
        case MH_PX_LID: draw_lid(w, im, e, cam_x, cam_y); break;
        case MH_PX_COL: draw_col(w, im, e, cam_x, cam_y); break;
        case MH_PX_PLANE: draw_plane(w, im, e, cam_x, cam_y); break;
        default: break;
        }
    }
}
