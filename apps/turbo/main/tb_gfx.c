/*
 * TURBO - pixels (see tb_gfx.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "tb_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(TB_HARNESS)
#include "esp_heap_caps.h"
#endif

void *tb_malloc(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(TB_HARNESS)
    void *p = heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n ? n : 1);
#else
    return malloc(n ? n : 1);
#endif
}

void *tb_malloc_internal(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(TB_HARNESS)
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return malloc(n ? n : 1);
#endif
}

void *tb_calloc(size_t n, size_t size)
{
    void *p = tb_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

static void (*s_yield)(void);
static uint32_t (*s_clock)(void);

void tb_set_yield(void (*hook)(void)) { s_yield = hook; }
void tb_yield(void) { if (s_yield) s_yield(); }
void tb_set_clock(uint32_t (*clock)(void)) { s_clock = clock; }
uint32_t tb_clock(void) { return s_clock ? s_clock() : 0; }

/* -------------------------------------------------------------------------- */

uint32_t tb_mix(uint32_t a, uint32_t b, int t)
{
    int ra = (int)(a >> 16) & 255, ga = (int)(a >> 8) & 255, ba = (int)a & 255;
    int rb = (int)(b >> 16) & 255, gb = (int)(b >> 8) & 255, bb = (int)b & 255;
    int r = ra + ((rb - ra) * t >> 8), g = ga + ((gb - ga) * t >> 8), bl = ba + ((bb - ba) * t >> 8);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void tb_img_init(tb_img_t *im, uint16_t *px, int w, int h)
{
    im->px = px;
    im->w = (int16_t)w;
    im->h = (int16_t)h;
    tb_img_clip(im, 0, 0, w, h);
}

void tb_img_clip(tb_img_t *im, int x0, int y0, int x1, int y1)
{
    im->cx0 = (int16_t)(x0 < 0 ? 0 : x0);
    im->cy0 = (int16_t)(y0 < 0 ? 0 : y0);
    im->cx1 = (int16_t)(x1 > im->w ? im->w : x1);
    im->cy1 = (int16_t)(y1 > im->h ? im->h : y1);
}

void tb_rect(tb_img_t *im, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void tb_rect_blend(tb_img_t *im, int x, int y, int w, int h, uint16_t c, int alpha)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = tb_blend(row[xx], c, alpha);
    }
}

static inline void px_blend(tb_img_t *im, int x, int y, uint16_t c, int a)
{
    if (x < im->cx0 || y < im->cy0 || x >= im->cx1 || y >= im->cy1 || a <= 0) return;
    uint16_t *p = im->px + (size_t)y * im->w + x;
    *p = tb_blend(*p, c, a);
}

void tb_disc(tb_img_t *im, int cx16, int cy16, int r16, uint16_t c, int alpha)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, r = r16 / 16.0f;
    int x0 = tb_ifloor(cx - r - 1), x1 = tb_ifloor(cx + r + 2);
    int y0 = tb_ifloor(cy - r - 1), y1 = tb_ifloor(cy + r + 2);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float cov = r - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            px_blend(im, x, y, c, (int)(cov * (float)alpha));
        }
    }
}

void tb_rrect(tb_img_t *im, int x, int y, int w, int h, int r, uint16_t c, int alpha)
{
    int ya = y < im->cy0 ? im->cy0 : y, yb = y + h > im->cy1 ? im->cy1 : y + h;
    int xa = x < im->cx0 ? im->cx0 : x, xb = x + w > im->cx1 ? im->cx1 : x + w;
    for (int yy = ya; yy < yb; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        bool edge_y = yy < y + r || yy >= y + h - r;
        for (int xx = xa; xx < xb; xx++) {
            int a = alpha;
            if (edge_y && (xx < x + r || xx >= x + w - r)) {
                float qx = xx < x + r ? (float)(x + r) - ((float)xx + 0.5f) : ((float)xx + 0.5f) - (float)(x + w - r);
                float qy = yy < y + r ? (float)(y + r) - ((float)yy + 0.5f) : ((float)yy + 0.5f) - (float)(y + h - r);
                float cov = (float)r - sqrtf(qx * qx + qy * qy) + 0.5f;
                if (cov <= 0) continue;
                if (cov < 1) a = (int)(cov * (float)alpha);
            }
            row[xx] = tb_blend(row[xx], c, a);
        }
    }
}

void tb_shadow(tb_img_t *im, int cx, int cy, int rx, int ry, int alpha)
{
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    int x0 = cx - rx, x1 = cx + rx, y0 = cy - ry, y1 = cy + ry;
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 >= im->cx1) x1 = im->cx1 - 1;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
    int irx = 65536 / rx, iry = 65536 / ry;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = im->px + (size_t)y * im->w;
        int dy = (y - cy) * iry >> 8;                    /* 8.8 */
        for (int x = x0; x <= x1; x++) {
            int dx = (x - cx) * irx >> 8;
            int d = (dx * dx + dy * dy) >> 8;            /* 0..256 inside */
            if (d >= 256) continue;
            int k = 256 - d;
            row[x] = tb_scale(row[x], 256 - (k * alpha >> 8));
        }
    }
}

void tb_glow(tb_img_t *im, int cx, int cy, int r, uint32_t rgb, int alpha)
{
    if (r < 1) r = 1;
    int x0 = cx - r, x1 = cx + r, y0 = cy - r, y1 = cy + r;
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 >= im->cx1) x1 = im->cx1 - 1;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
    int cr = (int)(rgb >> 16) & 255, cg = (int)(rgb >> 8) & 255, cb = (int)rgb & 255;
    int ir = 65536 / r;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = im->px + (size_t)y * im->w;
        int dy = (y - cy) * ir >> 8;
        for (int x = x0; x <= x1; x++) {
            int dx = (x - cx) * ir >> 8;
            int d = (dx * dx + dy * dy) >> 8;
            if (d >= 256) continue;
            int k = (256 - d) * (256 - d) >> 8;
            k = k * alpha >> 8;
            int r0, g0, b0;
            tb_unpack(row[x], &r0, &g0, &b0);
            r0 += cr * k >> 8; g0 += cg * k >> 8; b0 += cb * k >> 8;
            row[x] = tb_rgb(r0 > 255 ? 255 : r0, g0 > 255 ? 255 : g0, b0 > 255 ? 255 : b0);
        }
    }
}

/* -------------------------------------------------------------------------- */

bool tb_sprite_runs(tb_sprite_t *s)
{
    free(s->run);
    s->run = NULL;
    if (!s->a || s->h <= 0) return false;
    s->run = (uint16_t *)tb_malloc((size_t)s->h * 8);
    if (!s->run) return false;
    for (int y = 0; y < s->h; y++) {
        const uint8_t *a = s->a + (size_t)y * s->w;
        int x0 = 0, x1 = s->w;
        while (x0 < x1 && a[x0] == 0) x0++;
        while (x1 > x0 && a[x1 - 1] == 0) x1--;
        int bo0 = 0, bo1 = 0;
        for (int x = x0; x < x1;) {
            if (a[x] < 255) {
                x++;
                continue;
            }
            int e = x;
            while (e < x1 && a[e] == 255) e++;
            if (e - x > bo1 - bo0) {
                bo0 = x;
                bo1 = e;
            }
            x = e;
        }
        if (bo1 <= bo0) bo0 = bo1 = x0;
        uint16_t *r = s->run + y * 4;
        r[0] = (uint16_t)x0;
        r[1] = (uint16_t)x1;
        r[2] = (uint16_t)bo0;
        r[3] = (uint16_t)bo1;
    }
    return true;
}

void tb_sprite_free(tb_sprite_t *s)
{
    free(s->px);
    free(s->a);
    free(s->span);
    free(s->run);
    s->px = NULL;
    s->a = NULL;
    s->span = NULL;
    s->run = NULL;
}

static inline void blend_span(uint16_t *d, const uint16_t *sp, const uint8_t *sa, int x0, int x1)
{
    for (int xx = x0; xx < x1; xx++) {
        int a = sa[xx];
        if (a == 0) continue;
        d[xx] = a >= 250 ? sp[xx] : tb_blend(d[xx], sp[xx], a);
    }
}

void tb_sprite(tb_img_t *im, const tb_sprite_t *s, int x, int y)
{
    int sx0 = x - s->ox, sy0 = y - s->oy;
    int x0 = sx0 < im->cx0 ? im->cx0 : sx0, y0 = sy0 < im->cy0 ? im->cy0 : sy0;
    int x1 = sx0 + s->w > im->cx1 ? im->cx1 : sx0 + s->w;
    int y1 = sy0 + s->h > im->cy1 ? im->cy1 : sy0 + s->h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *d = im->px + (size_t)yy * im->w;
        const uint16_t *sp = s->px + (size_t)(yy - sy0) * s->w - sx0;
        if (!s->a) {
            memcpy(d + x0, sp + x0, (size_t)(x1 - x0) * 2);
            continue;
        }
        const uint8_t *sa = s->a + (size_t)(yy - sy0) * s->w - sx0;
        if (!s->run) {
            blend_span(d, sp, sa, x0, x1);
            continue;
        }
        /* the row's runs, in screen columns, clipped */
        const uint16_t *r = s->run + (yy - sy0) * 4;
        int v0 = sx0 + r[0], v1 = sx0 + r[1], o0 = sx0 + r[2], o1 = sx0 + r[3];
        if (v0 < x0) v0 = x0;
        if (v1 > x1) v1 = x1;
        if (o0 < v0) o0 = v0;
        if (o1 > v1) o1 = v1;
        if (o1 < o0) o1 = o0;
        blend_span(d, sp, sa, v0, o0);
        if (o1 > o0) memcpy(d + o0, sp + o0, (size_t)(o1 - o0) * 2);
        blend_span(d, sp, sa, o1, v1);
    }
}

/* the columns that are not transparent, row by row: drawing a lamp post
 * walked its whole box, and most of it is air */
static void make_spans(tb_sprite_t *s)
{
    s->span = NULL;
    if (!s->a || s->h <= 0) return;
    s->span = (uint16_t *)tb_malloc((size_t)s->h * 4);
    if (!s->span) return;
    for (int y = 0; y < s->h; y++) {
        const uint8_t *a = s->a + (size_t)y * s->w;
        int x0 = 0, x1 = s->w - 1;
        while (x0 < s->w && a[x0] < 8) x0++;
        while (x1 >= x0 && a[x1] < 8) x1--;
        s->span[y * 2] = (uint16_t)x0;          /* x0 > x1: an empty row */
        s->span[y * 2 + 1] = (uint16_t)(x1 < 0 ? 0 : x1);
        if (x0 >= s->w) s->span[y * 2 + 1] = 0, s->span[y * 2] = 1;
    }
}

bool tb_mip_build(tb_mip_t *m, const tb_sprite_t *src)
{
    m->lv[0] = *src;
    m->n = 1;
    make_spans(&m->lv[0]);
    while (m->n < TB_MIPS) {
        const tb_sprite_t *a = &m->lv[m->n - 1];
        if (a->w < 4 || a->h < 4) break;
        tb_sprite_t *b = &m->lv[m->n];
        b->w = (int16_t)(a->w / 2);
        b->h = (int16_t)(a->h / 2);
        b->ox = (int16_t)(a->ox / 2);
        b->oy = (int16_t)(a->oy / 2);
        b->a = (uint8_t *)tb_malloc((size_t)b->w * b->h);
        b->px = a->px ? (uint16_t *)tb_malloc((size_t)b->w * b->h * 2) : NULL;
        if (!b->a || (a->px && !b->px)) {
            free(b->a);
            free(b->px);
            break;
        }
        for (int y = 0; y < b->h; y++) {
            for (int x = 0; x < b->w; x++) {
                int al = 0, r = 0, g = 0, bl = 0, wsum = 0;
                for (int k = 0; k < 4; k++) {
                    size_t i = (size_t)(y * 2 + (k >> 1)) * a->w + x * 2 + (k & 1);
                    int aa = a->a[i];
                    al += aa;
                    if (a->px) {
                        int rr, gg, bb;
                        tb_unpack(a->px[i], &rr, &gg, &bb);
                        int w = aa + 1;
                        r += rr * w; g += gg * w; bl += bb * w; wsum += w;
                    }
                }
                size_t o = (size_t)y * b->w + x;
                b->a[o] = (uint8_t)(al / 4);
                if (b->px) b->px[o] = tb_rgb(r / wsum, g / wsum, bl / wsum);
            }
        }
        make_spans(b);
        m->n++;
        tb_yield();
    }
    return true;
}

void tb_mip_free(tb_mip_t *m)
{
    for (int i = 0; i < m->n; i++) {
        free(m->lv[i].a);
        free(m->lv[i].px);
        free(m->lv[i].span);
        m->lv[i].a = NULL;
        m->lv[i].px = NULL;
        m->lv[i].span = NULL;
    }
    m->n = 0;
}

static int pick_level(const tb_mip_t *m, float scale, float *lscale)
{
    int l = 0;
    float s = scale;
    while (l + 1 < m->n && s < 0.5f) {
        s *= 2.0f;
        l++;
    }
    *lscale = s;
    return l;
}

/* Nearest sampling in 16.16 fixed point from the mip level just above the
 * size asked for: the level keeps it from shimmering and the fixed point
 * keeps floats out of the pixel loop. */
void tb_mip_draw(tb_img_t *im, const tb_mip_t *m, float x, float y, float scale, int clip_y,
                 bool mirror, uint16_t fog_c, int fog, int shade)
{
    if (!m->n || scale <= 0.002f) return;
    float ls;
    const tb_sprite_t *s = &m->lv[pick_level(m, scale, &ls)];
    float ox = mirror ? (float)(s->w - s->ox) : (float)s->ox;
    float x0f = x - ox * ls, y0f = y - (float)s->oy * ls;
    int x0 = tb_ifloor(x0f), y0 = tb_ifloor(y0f);
    int x1 = tb_ifloor(x0f + s->w * ls) + 1, y1 = tb_ifloor(y0f + s->h * ls) + 1;
    int cy1 = clip_y < im->cy1 ? clip_y : im->cy1;
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 > im->cx1) x1 = im->cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    int32_t step = (int32_t)(65536.0f / ls);
    int32_t sx_start = (int32_t)(((float)x0 + 0.5f - x0f) * 65536.0f / ls);
    int32_t sy = (int32_t)(((float)y0 + 0.5f - y0f) * 65536.0f / ls);
    /* the columns whose source is inside the sprite, once for all rows */
    int32_t lim = (int32_t)s->w << 16;
    while (x0 < x1 && sx_start < 0) { sx_start += step; x0++; }
    while (x1 > x0 && sx_start + step * (x1 - 1 - x0) >= lim) x1--;
    if (x0 >= x1) return;
    int32_t sx0 = sx_start, dstep = step;
    if (mirror) {
        /* walk the source backwards: column ix becomes w - 1 - ix */
        sx0 = lim - 1 - sx_start;
        dstep = -step;
    }
    bool tinted = fog > 0 || shade != 256;
    float fstep = ls * (1.0f / 65536.0f);           /* dest px per 16.16 source unit */
    for (int yy = y0; yy < y1; yy++, sy += step) {
        int iy = sy >> 16;
        if (iy < 0) continue;
        if (iy >= s->h) break;
        uint16_t *d = im->px + (size_t)yy * im->w;
        const uint16_t *sp = s->px + (size_t)iy * s->w;
        const uint8_t *sa = s->a + (size_t)iy * s->w;
        int xa = x0, xb = x1;
        if (s->span) {
            int s0 = s->span[iy * 2], s1 = s->span[iy * 2 + 1];
            if (s1 < s0) continue;
            /* the walk runs over u = sx_start + (x - x0) * step, unmirrored */
            int m0 = mirror ? s->w - 1 - s1 : s0, m1 = mirror ? s->w - 1 - s0 : s1;
            int ca = x0 + (int)((float)(((int32_t)m0 << 16) - sx_start) * fstep);
            int cb = x0 + (int)((float)((((int32_t)m1 + 1) << 16) - sx_start) * fstep) + 1;
            if (ca > xa) xa = ca;
            if (cb < xb) xb = cb;
            if (xa > x0) xa--;              /* the float rounding, both sides */
            if (xa >= xb) continue;
        }
        int32_t sx = sx0 + dstep * (xa - x0);
        if (!tinted) {
            for (int xx = xa; xx < xb; xx++, sx += dstep) {
                int ix = sx >> 16;
                int a = sa[ix];
                if (a < 8) continue;
                d[xx] = a >= 248 ? sp[ix] : tb_blend(d[xx], sp[ix], a);
            }
        } else {
            for (int xx = xa; xx < xb; xx++, sx += dstep) {
                int ix = sx >> 16;
                int a = sa[ix];
                if (a < 8) continue;
                uint16_t c = sp[ix];
                if (shade != 256) c = tb_scale(c, shade);
                if (fog > 0) c = tb_blend(c, fog_c, fog);
                d[xx] = a >= 248 ? c : tb_blend(d[xx], c, a);
            }
        }
    }
}

void tb_mip_shadow(tb_img_t *im, const tb_mip_t *m, float x, float y, float scale, int clip_y, int strength)
{
    if (!m->n || scale <= 0.002f) return;
    float ls;
    const tb_sprite_t *s = &m->lv[pick_level(m, scale, &ls)];
    float x0f = x - (float)s->ox * ls, y0f = y - (float)s->oy * ls;
    int x0 = tb_ifloor(x0f), y0 = tb_ifloor(y0f);
    int x1 = tb_ifloor(x0f + s->w * ls) + 1, y1 = tb_ifloor(y0f + s->h * ls) + 1;
    int cy1 = clip_y < im->cy1 ? clip_y : im->cy1;
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 > im->cx1) x1 = im->cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    int32_t step = (int32_t)(65536.0f / ls);
    int32_t sx_start = (int32_t)(((float)x0 + 0.5f - x0f) * 65536.0f / ls);
    int32_t sy = (int32_t)(((float)y0 + 0.5f - y0f) * 65536.0f / ls);
    for (int yy = y0; yy < y1; yy++, sy += step) {
        int iy = sy >> 16;
        if (iy < 0 || iy >= s->h) continue;
        uint16_t *d = im->px + (size_t)yy * im->w;
        const uint8_t *sa = s->a + (size_t)iy * s->w;
        int32_t sx = sx_start;
        for (int xx = x0; xx < x1; xx++, sx += step) {
            int ix = sx >> 16;
            if (ix < 0 || ix >= s->w) continue;
            int a = sa[ix] * strength >> 8;
            if (a > 4) d[xx] = tb_darken(d[xx], 256 - a);
        }
    }
}
