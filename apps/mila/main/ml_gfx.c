/*
 * MILA - pixels (see ml_gfx.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "ml_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(ML_HARNESS)
#include "esp_heap_caps.h"
#endif

void *ml_malloc(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(ML_HARNESS)
    void *p = heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n ? n : 1);
#else
    return malloc(n ? n : 1);
#endif
}

void *ml_malloc_internal(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN) && !defined(ML_HARNESS)
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return malloc(n ? n : 1);
#endif
}

void *ml_calloc(size_t n, size_t size)
{
    void *p = ml_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

static void (*s_yield)(void);
static uint32_t (*s_clock)(void);

void ml_set_yield(void (*hook)(void)) { s_yield = hook; }
void ml_yield(void) { if (s_yield) s_yield(); }
void ml_set_clock(uint32_t (*clock)(void)) { s_clock = clock; }
uint32_t ml_clock(void) { return s_clock ? s_clock() : 0; }

uint32_t ml_mix(uint32_t a, uint32_t b, int t)
{
    int ra = (int)(a >> 16) & 255, ga = (int)(a >> 8) & 255, ba = (int)a & 255;
    int rb = (int)(b >> 16) & 255, gb = (int)(b >> 8) & 255, bb = (int)b & 255;
    int r = ra + ((rb - ra) * t >> 8), g = ga + ((gb - ga) * t >> 8), bl = ba + ((bb - ba) * t >> 8);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

void ml_copy_swap(uint16_t *dst, const uint16_t *src, size_t n)
{
    /* two pixels per word; written as a byte shuffle the compiler would
     * call __bswapsi2, which the firmware does not export */
    size_t i = 0;
    if ((((uintptr_t)dst | (uintptr_t)src) & 3) == 0) {
        const uint32_t *s = (const uint32_t *)src;
        uint32_t *d = (uint32_t *)dst;
        size_t n2 = n >> 1;
        for (size_t k = 0; k < n2; k++) {
            uint32_t v = s[k];
            d[k] = ((v >> 8) & 0x00FF00FFu) | ((v << 8) & 0xFF00FF00u);
        }
        i = n2 << 1;
    }
    for (; i < n; i++) dst[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
}

void ml_img_init(ml_img_t *im, uint16_t *px, int w, int h)
{
    im->px = px;
    im->w = (int16_t)w;
    im->h = (int16_t)h;
    ml_img_clip(im, 0, 0, w, h);
}

void ml_img_clip(ml_img_t *im, int x0, int y0, int x1, int y1)
{
    im->cx0 = (int16_t)(x0 < 0 ? 0 : x0);
    im->cy0 = (int16_t)(y0 < 0 ? 0 : y0);
    im->cx1 = (int16_t)(x1 > im->w ? im->w : x1);
    im->cy1 = (int16_t)(y1 > im->h ? im->h : y1);
}

void ml_rect(ml_img_t *im, int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void ml_rect_blend(ml_img_t *im, int x, int y, int w, int h, uint16_t c, int alpha)
{
    int x0 = x < im->cx0 ? im->cx0 : x, y0 = y < im->cy0 ? im->cy0 : y;
    int x1 = x + w > im->cx1 ? im->cx1 : x + w, y1 = y + h > im->cy1 ? im->cy1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = ml_blend(row[xx], c, alpha);
    }
}

static inline void px_blend(ml_img_t *im, int x, int y, uint16_t c, int a)
{
    if (x < im->cx0 || y < im->cy0 || x >= im->cx1 || y >= im->cy1 || a <= 0) return;
    uint16_t *p = im->px + (size_t)y * im->w + x;
    *p = ml_blend(*p, c, a);
}

void ml_disc(ml_img_t *im, int cx16, int cy16, int r16, uint16_t c, int alpha)
{
    float cx = cx16 / 16.0f, cy = cy16 / 16.0f, r = r16 / 16.0f;
    int x0 = ml_ifloor(cx - r - 1), x1 = ml_ifloor(cx + r + 2);
    int y0 = ml_ifloor(cy - r - 1), y1 = ml_ifloor(cy + r + 2);
    if (y0 < im->cy0) y0 = im->cy0;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
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

void ml_rrect(ml_img_t *im, int x, int y, int w, int h, int r, uint16_t c, int alpha)
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
            row[xx] = ml_blend(row[xx], c, a);
        }
    }
}

void ml_shadow_ellipse(ml_img_t *im, int cx, int cy, int rx, int ry, int alpha)
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
        int dy = (y - cy) * iry >> 8;
        for (int x = x0; x <= x1; x++) {
            int dx = (x - cx) * irx >> 8;
            int d = (dx * dx + dy * dy) >> 8;
            if (d >= 256) continue;
            int k = 256 - d;
            row[x] = ml_darken(row[x], 256 - (k * alpha >> 8));
        }
    }
}

void ml_mask_draw(ml_img_t *im, const ml_mask_t *m, int x, int y, uint16_t c, int alpha)
{
    if (!m || !m->a) return;
    int y0 = y < im->cy0 ? im->cy0 : y, y1 = y + m->h > im->cy1 ? im->cy1 : y + m->h;
    int x0 = x < im->cx0 ? im->cx0 : x, x1 = x + m->w > im->cx1 ? im->cx1 : x + m->w;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = im->px + (size_t)yy * im->w;
        const uint8_t *ma = m->a + (size_t)(yy - y) * m->w - x;
        for (int xx = x0; xx < x1; xx++) {
            int a = ma[xx];
            if (!a) continue;
            row[xx] = ml_blend(row[xx], c, alpha >= 255 ? a : a * alpha >> 8);
        }
    }
}
