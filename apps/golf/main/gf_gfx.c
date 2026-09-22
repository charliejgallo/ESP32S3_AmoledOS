/*
 * GOLF - pixels (see gf_gfx.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
#include "esp_heap_caps.h"
#endif

void *gf_malloc(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
    void *p = heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n ? n : 1);
#else
    return malloc(n ? n : 1);
#endif
}

void *gf_calloc(size_t n, size_t size)
{
    void *p = gf_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

static void (*s_yield)(void);
static uint32_t (*s_clock)(void);
static uint32_t s_prof[8];

void gf_set_clock(uint32_t (*clock)(void))
{
    s_clock = clock;
}

uint32_t gf_clock(void)
{
    return s_clock ? s_clock() : 0;
}

void gf_prof_set(int i, uint32_t ms)
{
    if (i >= 0 && i < 8) s_prof[i] = ms;
}

uint32_t gf_prof_get(int i)
{
    return i >= 0 && i < 8 ? s_prof[i] : 0;
}

void gf_set_yield(void (*hook)(void))
{
    s_yield = hook;
}

void gf_yield(void)
{
    if (s_yield) s_yield();
}

void gf_img_init(gf_img_t *im, uint16_t *px, int w, int h)
{
    im->px = px;
    im->w  = (int16_t)w;
    im->h  = (int16_t)h;
    gf_img_noclip(im);
}

void gf_img_clip(gf_img_t *im, int x0, int y0, int x1, int y1)
{
    im->cx0 = (int16_t)(x0 < 0 ? 0 : x0);
    im->cy0 = (int16_t)(y0 < 0 ? 0 : y0);
    im->cx1 = (int16_t)(x1 > im->w ? im->w : x1);
    im->cy1 = (int16_t)(y1 > im->h ? im->h : y1);
}

void gf_img_noclip(gf_img_t *im)
{
    im->cx0 = 0;
    im->cy0 = 0;
    im->cx1 = im->w;
    im->cy1 = im->h;
}

uint16_t gf_blend(uint16_t b, uint16_t a, int alpha)
{
    if (alpha >= 255) return a;
    if (alpha <= 0) return b;
    /* 565 blend in the classic 32-bit spread form, alpha in 0..32 */
    uint32_t al = (uint32_t)(alpha + 4) >> 3;
    uint32_t bb = (b | ((uint32_t)b << 16)) & 0x07E0F81FU;
    uint32_t aa = (a | ((uint32_t)a << 16)) & 0x07E0F81FU;
    uint32_t r  = ((((aa - bb) * al) >> 5) + bb) & 0x07E0F81FU;
    return (uint16_t)(r | (r >> 16));
}

uint16_t gf_scale(uint16_t c, int k)
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    r = r * k >> 8;
    g = g * k >> 8;
    b = b * k >> 8;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int gf_isqrt(int v)
{
    if (v <= 0) return 0;
    int r = (int)sqrtf((float)v);
    while (r * r > v) r--;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* -------------------------------------------------------------------------- */

void gf_fill(gf_img_t *im, uint16_t c)
{
    gf_rect(im, 0, 0, im->w, im->h, c);
}

void gf_rect(gf_img_t *im, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = c;
        }
    }
}

void gf_rect_blend(gf_img_t *im, int x, int y, int w, int h, uint16_t c, int alpha)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = gf_blend(row[xx], c, alpha);
        }
    }
}

static inline void px_blend(gf_img_t *im, int x, int y, uint16_t c, int a)
{
    if (x < im->cx0 || y < im->cy0 || x >= im->cx1 || y >= im->cy1 || a <= 0) {
        return;
    }
    uint16_t *p = im->px + (size_t)y * im->w + x;
    *p = gf_blend(*p, c, a);
}

void gf_disc(gf_img_t *im, int cx16, int cy16, int r16, uint16_t c, int alpha)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, r = r16 / 16.0f;
    int x0 = (int)gf_floorf(cx - r - 1), x1 = (int)gf_ceilf(cx + r + 1);
    int y0 = (int)gf_floorf(cy - r - 1), y1 = (int)gf_ceilf(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = r - d + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            px_blend(im, x, y, c, (int)(cov * (float)alpha));
        }
    }
}

void gf_ball(gf_img_t *im, int cx16, int cy16, int r16)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, r = r16 / 16.0f;
    int x0 = (int)gf_floorf(cx - r - 1), x1 = (int)gf_ceilf(cx + r + 1);
    int y0 = (int)gf_floorf(cy - r - 1), y1 = (int)gf_ceilf(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cov = r - d + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            /* lit from the upper left, a soft rim */
            float l = 1.0f - ((dx + r * 0.45f) * (dx + r * 0.45f) + (dy + r * 0.45f) * (dy + r * 0.45f)) / (r * r * 2.2f);
            if (l < 0.0f) l = 0.0f;
            int v = 150 + (int)(105.0f * l);
            uint16_t c = gf_rgb(v, v, v > 200 ? v : v + 8);
            /* a dark outline so it reads on light green */
            if (d > r - 0.9f) {
                c = gf_rgb(70, 76, 70);
                cov *= 0.85f;
            }
            px_blend(im, x, y, c, (int)(cov * 255.0f));
        }
    }
}

void gf_shadow(gf_img_t *im, int cx16, int cy16, int rx16, int ry16, int alpha)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, rx = rx16 / 16.0f, ry = ry16 / 16.0f;
    if (rx < 0.5f) rx = 0.5f;
    if (ry < 0.5f) ry = 0.5f;
    int x0 = (int)gf_floorf(cx - rx - 1), x1 = (int)gf_ceilf(cx + rx + 1);
    int y0 = (int)gf_floorf(cy - ry - 1), y1 = (int)gf_ceilf(cy + ry + 1);
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 >= im->cx1) x1 = im->cx1 - 1;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = im->px + (size_t)y * im->w;
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / rx, dy = ((float)y + 0.5f - cy) / ry;
            float d = dx * dx + dy * dy;
            if (d >= 1.0f) continue;
            float k = (1.0f - d);
            k = k * k * (3.0f - 2.0f * k);
            int a = (int)(k * (float)alpha);
            row[x] = gf_scale(row[x], 256 - a);
        }
    }
}

void gf_line(gf_img_t *im, int x0, int y0, int x1, int y1, int w16, uint16_t c, int alpha)
{
    /* coordinates in 1/16 px; distance-to-segment coverage */
    float ax = x0 / 16.0f, ay = y0 / 16.0f, bx = x1 / 16.0f, by = y1 / 16.0f;
    float hw = w16 / 32.0f;
    int minx = (int)gf_floorf((ax < bx ? ax : bx) - hw - 1), maxx = (int)gf_ceilf((ax > bx ? ax : bx) + hw + 1);
    int miny = (int)gf_floorf((ay < by ? ay : by) - hw - 1), maxy = (int)gf_ceilf((ay > by ? ay : by) + hw + 1);
    if (minx < im->cx0) minx = im->cx0;
    if (miny < im->cy0) miny = im->cy0;
    if (maxx >= im->cx1) maxx = im->cx1 - 1;
    if (maxy >= im->cy1) maxy = im->cy1 - 1;
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            float qx = ax + t * dx - px, qy = ay + t * dy - py;
            float d = sqrtf(qx * qx + qy * qy);
            float cov = hw - d + 0.5f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            px_blend(im, x, y, c, (int)(cov * (float)alpha));
        }
    }
}

void gf_ring(gf_img_t *im, int cx16, int cy16, int r16, int w16, uint16_t c, int alpha)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, r = r16 / 16.0f, hw = w16 / 32.0f;
    int x0 = (int)gf_floorf(cx - r - hw - 1), x1 = (int)gf_ceilf(cx + r + hw + 1);
    int y0 = (int)gf_floorf(cy - r - hw - 1), y1 = (int)gf_ceilf(cy + r + hw + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - r);
            float cov = hw - d + 0.5f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            px_blend(im, x, y, c, (int)(cov * (float)alpha));
        }
    }
}

void gf_rrect(gf_img_t *im, int x, int y, int w, int h, int r, uint16_t c, int alpha)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            float cov = 1.0f;
            float qx = 0, qy = 0;
            if (xx < x + r) qx = (float)(x + r) - ((float)xx + 0.5f);
            else if (xx >= x + w - r) qx = ((float)xx + 0.5f) - (float)(x + w - r);
            if (yy < y + r) qy = (float)(y + r) - ((float)yy + 0.5f);
            else if (yy >= y + h - r) qy = ((float)yy + 0.5f) - (float)(y + h - r);
            if (qx > 0 && qy > 0) {
                float d = sqrtf(qx * qx + qy * qy);
                cov = (float)r - d + 0.5f;
                if (cov <= 0) continue;
                if (cov > 1) cov = 1;
            }
            px_blend(im, xx, yy, c, (int)(cov * (float)alpha));
        }
    }
}

/* -------------------------------------------------------------------------- */

void gf_sprite_box(const gf_sprite_t *s, int x, int y, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = x - s->ox;
    *y0 = y - s->oy;
    *x1 = *x0 + s->w;
    *y1 = *y0 + s->h;
}

void gf_sprite(gf_img_t *im, const gf_sprite_t *s, int x, int y)
{
    int sx0 = x - s->ox, sy0 = y - s->oy;
    int x0 = sx0 < im->cx0 ? im->cx0 : sx0, y0 = sy0 < im->cy0 ? im->cy0 : sy0;
    int x1 = sx0 + s->w > im->cx1 ? im->cx1 : sx0 + s->w;
    int y1 = sy0 + s->h > im->cy1 ? im->cy1 : sy0 + s->h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t       *d  = im->px + (size_t)yy * im->w;
        const uint16_t *sp = s->px + (size_t)(yy - sy0) * s->w - sx0;
        const uint8_t  *sa = s->a + (size_t)(yy - sy0) * s->w - sx0;
        for (int xx = x0; xx < x1; xx++) {
            int a = sa[xx];
            if (a == 0) continue;
            d[xx] = a >= 250 ? sp[xx] : gf_blend(d[xx], sp[xx], a);
        }
    }
}

void gf_sprite_scaled(gf_img_t *im, const gf_sprite_t *s, int x, int y, int scale16, int opa)
{
    if (scale16 <= 0) return;
    int dw = s->w * scale16 / 16, dh = s->h * scale16 / 16;
    if (dw <= 0 || dh <= 0) return;
    int dx0 = x - s->ox * scale16 / 16, dy0 = y - s->oy * scale16 / 16;
    int x0 = dx0 < im->cx0 ? im->cx0 : dx0, y0 = dy0 < im->cy0 ? im->cy0 : dy0;
    int x1 = dx0 + dw > im->cx1 ? im->cx1 : dx0 + dw;
    int y1 = dy0 + dh > im->cy1 ? im->cy1 : dy0 + dh;
    int step = (16 << 8) / scale16;         /* source px per dest px, 8.8 */
    for (int yy = y0; yy < y1; yy++) {
        int sy = ((yy - dy0) * step + (step >> 1)) >> 8;
        if (sy >= s->h) sy = s->h - 1;
        const uint16_t *sp = s->px + (size_t)sy * s->w;
        const uint8_t  *sa = s->a + (size_t)sy * s->w;
        uint16_t *d = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) {
            int sx = ((xx - dx0) * step + (step >> 1)) >> 8;
            if (sx >= s->w) sx = s->w - 1;
            int a = sa[sx] * opa / 255;
            if (a == 0) continue;
            d[xx] = gf_blend(d[xx], sp[sx], a);
        }
    }
}

/* -------------------------------------------------------------------------- */

void gf_dirty_reset(gf_dirty_t *d)
{
    d->n = 0;
    d->all = false;
}

void gf_dirty_all(gf_dirty_t *d)
{
    d->all = true;
    d->n = 0;
}

static int area_of(int x0, int y0, int x1, int y1)
{
    return (x1 - x0) * (y1 - y0);
}

void gf_dirty_add(gf_dirty_t *d, int x0, int y0, int x1, int y1)
{
    if (d->all) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GF_W) x1 = GF_W;
    if (y1 > GF_H) y1 = GF_H;
    if (x1 <= x0 || y1 <= y0) return;
    /* merge with an existing one when the union costs little more */
    for (int i = 0; i < d->n; i++) {
        gf_rect_t *r = &d->r[i];
        int ux0 = r->x0 < x0 ? r->x0 : x0, uy0 = r->y0 < y0 ? r->y0 : y0;
        int ux1 = r->x1 > x1 ? r->x1 : x1, uy1 = r->y1 > y1 ? r->y1 : y1;
        int ua = area_of(ux0, uy0, ux1, uy1);
        if (ua <= area_of(r->x0, r->y0, r->x1, r->y1) + area_of(x0, y0, x1, y1) + 600) {
            r->x0 = (int16_t)ux0; r->y0 = (int16_t)uy0;
            r->x1 = (int16_t)ux1; r->y1 = (int16_t)uy1;
            return;
        }
    }
    if (d->n < GF_MAX_DIRTY) {
        gf_rect_t *r = &d->r[d->n++];
        r->x0 = (int16_t)x0; r->y0 = (int16_t)y0;
        r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
        return;
    }
    /* full: fold into the first */
    gf_rect_t *r = &d->r[0];
    if (x0 < r->x0) r->x0 = (int16_t)x0;
    if (y0 < r->y0) r->y0 = (int16_t)y0;
    if (x1 > r->x1) r->x1 = (int16_t)x1;
    if (y1 > r->y1) r->y1 = (int16_t)y1;
}

int gf_dirty_area(const gf_dirty_t *d)
{
    if (d->all) return GF_W * GF_H;
    int a = 0;
    for (int i = 0; i < d->n; i++) {
        a += area_of(d->r[i].x0, d->r[i].y0, d->r[i].x1, d->r[i].y1);
    }
    return a;
}

void gf_copy_rect(uint16_t *dst, const uint16_t *src, const gf_rect_t *r)
{
    int w = r->x1 - r->x0;
    if (w <= 0) return;
    for (int y = r->y0; y < r->y1; y++) {
        memcpy(dst + (size_t)y * GF_W + r->x0, src + (size_t)y * GF_W + r->x0, (size_t)w * 2);
    }
}

/* -------------------------------------------------------------------------- */

bool gf_mip_build(gf_mip_t *m, const gf_sprite_t *src)
{
    m->lv[0] = *src;
    m->n = 1;
    while (m->n < GF_MIPS) {
        const gf_sprite_t *a = &m->lv[m->n - 1];
        if (a->w < 4 || a->h < 4) break;
        gf_sprite_t *b = &m->lv[m->n];
        b->w = (int16_t)(a->w / 2);
        b->h = (int16_t)(a->h / 2);
        b->ox = (int16_t)(a->ox / 2);
        b->oy = (int16_t)(a->oy / 2);
        b->a = (uint8_t *)gf_malloc((size_t)b->w * b->h);
        b->px = a->px ? (uint16_t *)gf_malloc((size_t)b->w * b->h * 2) : NULL;
        if (!b->a || (a->px && !b->px)) {
            free(b->a);
            free(b->px);
            break;
        }
        for (int y = 0; y < b->h; y++) {
            for (int x = 0; x < b->w; x++) {
                int al = 0, r = 0, g = 0, bl = 0;
                for (int k = 0; k < 4; k++) {
                    int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                    size_t i = (size_t)sy * a->w + sx;
                    int aa = a->a[i];
                    al += aa;
                    if (a->px) {
                        int rr, gg, bb;
                        gf_unpack(a->px[i], &rr, &gg, &bb);
                        /* weighted by alpha, and a floor so the colour of
                         * transparent edges still counts a little */
                        int w = aa + 1;
                        r += rr * w; g += gg * w; bl += bb * w;
                    }
                }
                size_t o = (size_t)y * b->w + x;
                b->a[o] = (uint8_t)(al / 4);
                if (b->px) {
                    int wsum = 0;
                    for (int k = 0; k < 4; k++) {
                        int sx = x * 2 + (k & 1), sy = y * 2 + (k >> 1);
                        wsum += a->a[(size_t)sy * a->w + sx] + 1;
                    }
                    b->px[o] = gf_rgb(r / wsum, g / wsum, bl / wsum);
                }
            }
        }
        m->n++;
    }
    return true;
}

void gf_mip_free(gf_mip_t *m)
{
    for (int i = 0; i < m->n; i++) {
        free(m->lv[i].a);
        free(m->lv[i].px);
        m->lv[i].a = NULL;
        m->lv[i].px = NULL;
    }
    m->n = 0;
}

static int pick_level(const gf_mip_t *m, float scale, float *lscale)
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

void gf_mip_draw(gf_img_t *im, const gf_mip_t *m, float x, float y, float scale, int opa, int tint,
                 const uint16_t *depth, uint16_t dz, uint16_t fog_c, int fog)
{
    if (!m->n || scale <= 0.0f) return;
    float ls;
    const gf_sprite_t *s = &m->lv[pick_level(m, scale, &ls)];
    float ax = (float)s->ox;
    float x0f = x - ax * ls, y0f = y - (float)s->oy * ls;
    int x0 = (int)gf_floorf(x0f), y0 = (int)gf_floorf(y0f);
    int x1 = (int)gf_ceilf(x0f + s->w * ls), y1 = (int)gf_ceilf(y0f + s->h * ls);
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 > im->cx1) x1 = im->cx1;
    if (y1 > im->cy1) y1 = im->cy1;
    float inv = 1.0f / ls;
    bool bil = ls > 0.75f;
    for (int yy = y0; yy < y1; yy++) {
        float sy = ((float)yy + 0.5f - y0f) * inv - 0.5f;
        if (sy < -0.5f || sy > s->h - 0.5f) continue;
        uint16_t *d = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) {
            if (depth && depth[(size_t)yy * im->w + xx] < dz) continue;
            float sx = ((float)xx + 0.5f - x0f) * inv - 0.5f;
            if (sx < -0.5f || sx > s->w - 0.5f) continue;
            int a;
            uint16_t c;
            if (bil) {
                int ix = (int)gf_floorf(sx), iy = (int)gf_floorf(sy);
                float tx = sx - ix, ty = sy - iy;
                int ix1 = ix + 1, iy1 = iy + 1;
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (ix1 >= s->w) ix1 = s->w - 1;
                if (iy1 >= s->h) iy1 = s->h - 1;
                if (ix >= s->w) ix = s->w - 1;
                if (iy >= s->h) iy = s->h - 1;
                size_t i00 = (size_t)iy * s->w + ix, i10 = (size_t)iy * s->w + ix1;
                size_t i01 = (size_t)iy1 * s->w + ix, i11 = (size_t)iy1 * s->w + ix1;
                float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
                float fa = s->a[i00] * w00 + s->a[i10] * w10 + s->a[i01] * w01 + s->a[i11] * w11;
                a = (int)fa;
                if (a < 2) continue;
                int r0, g0, b0, r1, g1, b1, r2, g2, b2, r3, g3, b3;
                gf_unpack(s->px[i00], &r0, &g0, &b0);
                gf_unpack(s->px[i10], &r1, &g1, &b1);
                gf_unpack(s->px[i01], &r2, &g2, &b2);
                gf_unpack(s->px[i11], &r3, &g3, &b3);
                int r = (int)(r0 * w00 + r1 * w10 + r2 * w01 + r3 * w11);
                int g = (int)(g0 * w00 + g1 * w10 + g2 * w01 + g3 * w11);
                int b = (int)(b0 * w00 + b1 * w10 + b2 * w01 + b3 * w11);
                if (tint != 256) {
                    r = r * tint >> 8; g = g * tint >> 8; b = b * tint >> 8;
                    if (r > 255) r = 255;
                    if (g > 255) g = 255;
                    if (b > 255) b = 255;
                }
                c = gf_dither(r, g, b, xx, yy);
            } else {
                int ix = (int)(sx + 0.5f), iy = (int)(sy + 0.5f);
                if (ix < 0) ix = 0;
                if (iy < 0) iy = 0;
                if (ix >= s->w) ix = s->w - 1;
                if (iy >= s->h) iy = s->h - 1;
                size_t i = (size_t)iy * s->w + ix;
                a = s->a[i];
                if (a < 2) continue;
                c = s->px[i];
                if (tint != 256) c = gf_scale(c, tint);
            }
            if (fog > 0) c = gf_blend(c, fog_c, fog);
            a = a * opa / 255;
            d[xx] = gf_blend(d[xx], c, a);
        }
    }
}

void gf_mip_shadow(gf_img_t *im, const gf_mip_t *m, float x, float y, float scale, int strength)
{
    if (!m->n || scale <= 0.0f) return;
    float ls;
    const gf_sprite_t *s = &m->lv[pick_level(m, scale, &ls)];
    float x0f = x - (float)s->ox * ls, y0f = y - (float)s->oy * ls;
    int x0 = (int)gf_floorf(x0f), y0 = (int)gf_floorf(y0f);
    int x1 = (int)gf_ceilf(x0f + s->w * ls), y1 = (int)gf_ceilf(y0f + s->h * ls);
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 > im->cx1) x1 = im->cx1;
    if (y1 > im->cy1) y1 = im->cy1;
    float inv = 1.0f / ls;
    for (int yy = y0; yy < y1; yy++) {
        int sy = (int)(((float)yy + 0.5f - y0f) * inv);
        if (sy < 0 || sy >= s->h) continue;
        uint16_t *d = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) {
            int sx = (int)(((float)xx + 0.5f - x0f) * inv);
            if (sx < 0 || sx >= s->w) continue;
            int a = s->a[(size_t)sy * s->w + sx] * strength >> 8;
            if (a) d[xx] = gf_scale(d[xx], 256 - a);
        }
    }
}
