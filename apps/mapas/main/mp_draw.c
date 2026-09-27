/*
 * MAPAS - polygons, lines and text into a big-endian RGB565 buffer.
 * See mp_draw.h. The worker is the only caller except mp_font_bake().
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "mp_mem.h"
#include "mp_draw.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* over a big-endian RGB565 pixel, colour 0xRRGGBB at alpha 0..255 */
static inline uint16_t blend(uint16_t px, uint32_t rgb, int al)
{
    uint16_t v = (uint16_t)((px >> 8) | (px << 8));
    int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    r += (((int)(rgb >> 16) - r) * al) >> 8;
    g += ((((int)(rgb >> 8) & 255) - g) * al) >> 8;
    b += (((int)rgb & 255) - b) * al >> 8;
    uint16_t o = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((o >> 8) | (o << 8));
}

/* n pixels of one colour: two at a time, the buffer being PSRAM, where
 * the number of stores is what costs (a full 560 x 640 fill took ~45 ms
 * a pixel at a time on the board). */
static inline void fill16(uint16_t *d, int n, uint16_t c)
{
    if (n <= 0) return;
    if ((uintptr_t)d & 2) {
        *d++ = c;
        n--;
    }
    uint32_t c2 = (uint32_t)c | (uint32_t)c << 16;
    uint32_t *w = (uint32_t *)d;
    int n2 = n >> 1, i = 0;
    for (; i + 4 <= n2; i += 4) {
        w[i] = c2;
        w[i + 1] = c2;
        w[i + 2] = c2;
        w[i + 3] = c2;
    }
    for (; i < n2; i++) w[i] = c2;
    if (n & 1) d[n - 1] = c;
}

void mp_fb_clip(mp_fb_t *fb, int x0, int y0, int x1, int y1)
{
    fb->cx0 = x0 < 0 ? 0 : x0;
    fb->cy0 = y0 < 0 ? 0 : y0;
    fb->cx1 = x1 > fb->w ? fb->w : x1;
    fb->cy1 = y1 > fb->h ? fb->h : y1;
}

void mp_fb_clip_all(mp_fb_t *fb)
{
    mp_fb_clip(fb, 0, 0, fb->w, fb->h);
}

void mp_fill_rect(mp_fb_t *fb, int x, int y, int w, int h, uint32_t rgb)
{
    int x0 = x < fb->cx0 ? fb->cx0 : x, x1 = x + w > fb->cx1 ? fb->cx1 : x + w;
    int y0 = y < fb->cy0 ? fb->cy0 : y, y1 = y + h > fb->cy1 ? fb->cy1 : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    uint16_t c = mp_be565(rgb);
    for (int yy = y0; yy < y1; yy++) fill16(fb->px + (size_t)yy * fb->w + x0, x1 - x0, c);
}

void mp_blend_px(mp_fb_t *fb, int x, int y, uint32_t rgb, int alpha)
{
    if (x < fb->cx0 || x >= fb->cx1 || y < fb->cy0 || y >= fb->cy1 || alpha <= 0) return;
    uint16_t *p = fb->px + (size_t)y * fb->w + x;
    *p = alpha >= 255 ? mp_be565(rgb) : blend(*p, rgb, alpha);
}

/* ---------------------------------------------------------------------------
 * Polygons: scanline, even-odd
 * ------------------------------------------------------------------------- */

typedef struct {
    float x, dx;
    int   y0, y1;       /* rows [y0, y1) */
} edge_t;

static edge_t *s_edges;
static int     s_edges_cap;
static int    *s_act;
static float  *s_xs;
static int     s_act_cap;

static bool ensure(int ne)
{
    if (ne > s_edges_cap) {
        int nc = s_edges_cap ? s_edges_cap : 1024;
        while (nc < ne) nc *= 2;
        edge_t *e = (edge_t *)mp_realloc(s_edges, (size_t)nc * sizeof(edge_t));
        if (!e) return false;
        s_edges = e;
        s_edges_cap = nc;
    }
    if (ne > s_act_cap) {
        int nc = s_act_cap ? s_act_cap : 1024;
        while (nc < ne) nc *= 2;
        int *a = (int *)mp_realloc(s_act, (size_t)nc * sizeof(int));
        if (!a) return false;
        s_act = a;
        float *x = (float *)mp_realloc(s_xs, (size_t)nc * sizeof(float));
        if (!x) return false;
        s_xs = x;
        s_act_cap = nc;
    }
    return true;
}


static void span(uint16_t *row, float xa, float xb, int cx0, int cx1, uint16_t c, uint32_t rgb)
{
    if (xb <= (float)cx0 || xa >= (float)cx1 || xb <= xa) return;
    if (xa < (float)cx0) xa = (float)cx0;
    if (xb > (float)cx1) xb = (float)cx1;
    int ia = (int)xa, ib = (int)xb;
    if (ia == ib) {
        int al = (int)((xb - xa) * 255.0f);
        if (al > 0 && ia < cx1) row[ia] = blend(row[ia], rgb, al);
        return;
    }
    int al = (int)(((float)(ia + 1) - xa) * 255.0f);
    if (al >= 250) row[ia] = c;
    else if (al > 0) row[ia] = blend(row[ia], rgb, al);
    fill16(row + ia + 1, ib - ia - 1, c);
    if (ib < cx1) {
        al = (int)((xb - (float)ib) * 255.0f);
        if (al > 0) row[ib] = blend(row[ib], rgb, al);
    }
}

static int *s_cnt;                     /* edges starting at each row, then where they go */
static int  s_cnt_cap;
static int *s_ord;                     /* the edges, by starting row */
static int  s_ord_cap;

static bool ensure_rows(int rows, int ne)
{
    if (rows > s_cnt_cap) {
        int *c = (int *)mp_realloc(s_cnt, (size_t)rows * sizeof(int));
        if (!c) return false;
        s_cnt = c;
        s_cnt_cap = rows;
    }
    if (ne > s_ord_cap) {
        int nc = s_ord_cap ? s_ord_cap : 1024;
        while (nc < ne) nc *= 2;
        int *o = (int *)mp_realloc(s_ord, (size_t)nc * sizeof(int));
        if (!o) return false;
        s_ord = o;
        s_ord_cap = nc;
    }
    return true;
}

/* ceil for the coordinates a buffer has (well inside +-32768), inline:
 * ceilf is a call into newlib, twice per edge */
static inline int iceil(float v)
{
    int i = (int)(v + 32768.0f) - 32768;
    return (float)i < v ? i + 1 : i;
}

static edge_t *s_act_e;                 /* the active edges themselves, sorted by x */
static int     s_act_e_cap;

/* Scanline, even-odd. The edges are bucketed by their first row (a count,
 * not a sort: a tile's buildings in one polygon are tens of thousands of
 * edges), and the active ones are COPIED into a small array kept sorted by x
 * from row to row, which after one step is nearly sorted already. Pointing
 * into the big edge array instead (800 KB of PSRAM for a city's buildings)
 * missed the cache on every active edge of every row. aa: the two end
 * pixels of each span blended by their coverage; without it they are
 * rounded, which is write-only (buildings, where it does not show). */
/* A polygon of a few edges (a building): the edges on the stack, sorted by
 * insertion, and only its own rows walked. Nothing here touches a big array,
 * so it all stays in the cache; the general path below costs a count over
 * every row of the buffer per polygon. */
#define SMALL_EDGES 48

static void fill_small(mp_fb_t *fb, const float *xy, const uint32_t *ring_n, int nring,
                       uint32_t rgb, bool aa)
{
    edge_t e[SMALL_EDGES], act[SMALL_EDGES];
    int ne = 0, ymin = 1 << 30, ymax = -(1 << 30);
    const float *p = xy;
    for (int r = 0; r < nring; r++) {
        int n = (int)ring_n[r];
        for (int i = 0; i < n && n >= 3; i++) {
            float xa = p[2 * i], ya = p[2 * i + 1];
            int j = i + 1 == n ? 0 : i + 1;
            float xb = p[2 * j], yb = p[2 * j + 1];
            if (ya == yb) continue;
            if (ya > yb) {
                float t = xa; xa = xb; xb = t;
                t = ya; ya = yb; yb = t;
            }
            int y0 = iceil(ya - 0.5f), y1 = iceil(yb - 0.5f);
            if (y1 > fb->cy1) y1 = fb->cy1;
            if (y0 >= y1 || y1 <= fb->cy0) continue;
            float dx = (xb - xa) / (yb - ya);
            if (y0 < fb->cy0) y0 = fb->cy0;
            edge_t ed = { xa + ((float)y0 + 0.5f - ya) * dx, dx, y0, y1 };
            int k = ne++;
            while (k > 0 && e[k - 1].y0 > y0) {
                e[k] = e[k - 1];
                k--;
            }
            e[k] = ed;
            if (y0 < ymin) ymin = y0;
            if (y1 > ymax) ymax = y1;
        }
        p += 2 * n;
    }
    if (ne < 2) return;
    uint16_t c = mp_be565(rgb);
    int na = 0, next = 0;
    for (int y = ymin; y < ymax; y++) {
        for (; next < ne && e[next].y0 <= y; next++) {
            int k = na++;
            while (k > 0 && act[k - 1].x > e[next].x) {
                act[k] = act[k - 1];
                k--;
            }
            act[k] = e[next];
        }
        if (na >= 2) {
            uint16_t *row = fb->px + (size_t)y * fb->w;
            for (int k = 0; k + 1 < na; k += 2) {
                if (aa) {
                    span(row, act[k].x, act[k + 1].x, fb->cx0, fb->cx1, c, rgb);
                } else {
                    int xa = iceil(act[k].x - 0.5f), xb = iceil(act[k + 1].x - 0.5f);
                    if (xa < fb->cx0) xa = fb->cx0;
                    if (xb > fb->cx1) xb = fb->cx1;
                    if (xb > xa) fill16(row + xa, xb - xa, c);
                }
            }
        }
        int w = 0;
        for (int i = 0; i < na; i++) {
            if (act[i].y1 <= y + 1) continue;
            edge_t ed = act[i];
            ed.x += ed.dx;
            int k = w++;
            while (k > 0 && act[k - 1].x > ed.x) {
                act[k] = act[k - 1];
                k--;
            }
            act[k] = ed;
        }
        na = w;
    }
}

void mp_fill_poly_ex(mp_fb_t *fb, const float *xy, const uint32_t *ring_n, int nring, uint32_t rgb, bool aa)
{
    int total = 0;
    for (int r = 0; r < nring; r++) total += (int)ring_n[r];
    if (total <= SMALL_EDGES) {
        if (total >= 3) fill_small(fb, xy, ring_n, nring, rgb, aa);
        return;
    }
    const int rows = fb->cy1 - fb->cy0;
    if (total < 3 || rows <= 0 || !ensure(total) || !ensure_rows(rows + 1, total)) return;
    if (total > s_act_e_cap) {
        int nc = s_act_e_cap ? s_act_e_cap : 1024;
        while (nc < total) nc *= 2;
        edge_t *a = (edge_t *)mp_realloc(s_act_e, (size_t)nc * sizeof(edge_t));
        if (!a) return;
        s_act_e = a;
        s_act_e_cap = nc;
    }

    int ne = 0;
    const float *p = xy;
    for (int r = 0; r < nring; r++) {
        int n = (int)ring_n[r];
        if (n >= 3) {
            for (int i = 0; i < n; i++) {
                float xa = p[2 * i], ya = p[2 * i + 1];
                int j = i + 1 == n ? 0 : i + 1;
                float xb = p[2 * j], yb = p[2 * j + 1];
                if (ya == yb) continue;
                if (ya > yb) {
                    float t = xa; xa = xb; xb = t;
                    t = ya; ya = yb; yb = t;
                }
                int y0 = iceil(ya - 0.5f), y1 = iceil(yb - 0.5f);
                if (y1 > fb->cy1) y1 = fb->cy1;
                if (y0 >= y1 || y1 <= fb->cy0) continue;
                edge_t *e = &s_edges[ne++];
                e->dx = (xb - xa) / (yb - ya);
                if (y0 < fb->cy0) y0 = fb->cy0;
                e->x = xa + ((float)y0 + 0.5f - ya) * e->dx;
                e->y0 = y0;
                e->y1 = y1;
            }
        }
        p += 2 * n;
    }
    if (ne < 2) return;

    /* by starting row */
    memset(s_cnt, 0, (size_t)(rows + 1) * sizeof(int));
    int first = rows, last = 0;
    for (int i = 0; i < ne; i++) {
        int b = s_edges[i].y0 - fb->cy0;
        s_cnt[b + 1]++;
        if (b < first) first = b;
        if (s_edges[i].y1 - fb->cy0 > last) last = s_edges[i].y1 - fb->cy0;
    }
    for (int b = 0; b < rows; b++) s_cnt[b + 1] += s_cnt[b];
    for (int i = 0; i < ne; i++) s_ord[s_cnt[s_edges[i].y0 - fb->cy0]++] = i;
    /* s_cnt[b] is now the end of bucket b; bucket b starts where b-1 ended */

    uint16_t c = mp_be565(rgb);
    edge_t *act = s_act_e;
    int na = 0, next = first ? s_cnt[first - 1] : 0;
    for (int b = first; b < last; b++) {
        const int y = fb->cy0 + b;
        /* in: the ones that start here, each into its place */
        for (; next < s_cnt[b]; next++) {
            edge_t e = s_edges[s_ord[next]];
            int k = na++;
            while (k > 0 && act[k - 1].x > e.x) {
                act[k] = act[k - 1];
                k--;
            }
            act[k] = e;
        }
        if (na >= 2) {
            uint16_t *row = fb->px + (size_t)y * fb->w;
            if (aa) {
                for (int k = 0; k + 1 < na; k += 2) span(row, act[k].x, act[k + 1].x, fb->cx0, fb->cx1, c, rgb);
            } else {
                for (int k = 0; k + 1 < na; k += 2) {
                    int xa = iceil(act[k].x - 0.5f), xb = iceil(act[k + 1].x - 0.5f);
                    if (xa < fb->cx0) xa = fb->cx0;
                    if (xb > fb->cx1) xb = fb->cx1;
                    if (xb > xa) fill16(row + xa, xb - xa, c);
                }
            }
        }
        /* one row down: out the ones that end, the rest stepped and back
         * into order, in one pass */
        int w = 0;
        for (int i = 0; i < na; i++) {
            if (act[i].y1 <= y + 1) continue;
            edge_t e = act[i];
            e.x += e.dx;
            int k = w++;
            while (k > 0 && act[k - 1].x > e.x) {
                act[k] = act[k - 1];
                k--;
            }
            act[k] = e;
        }
        na = w;
    }
}

void mp_fill_poly(mp_fb_t *fb, const float *xy, const uint32_t *ring_n, int nring, uint32_t rgb)
{
    mp_fill_poly_ex(fb, xy, ring_n, nring, rgb, true);
}

/* ---------------------------------------------------------------------------
 * Lines: every segment is a capsule; a row of it is one interval, and only
 * the pixels near its edge need a square root.
 * ------------------------------------------------------------------------- */

static void seg(mp_fb_t *fb, float x0, float y0, float x1, float y1, float r,
                uint16_t c, uint32_t rgb, int alpha)
{
    const float R = r + 0.5f, R2 = R * R;
    const float ri = r - 0.5f, ri2 = ri > 0 ? ri * ri : -1.0f;
    int ya = (int)floorf((y0 < y1 ? y0 : y1) - R), yb = (int)ceilf((y0 > y1 ? y0 : y1) + R);
    if (ya < fb->cy0) ya = fb->cy0;
    if (yb > fb->cy1) yb = fb->cy1;
    float mnx = (x0 < x1 ? x0 : x1) - R, mxx = (x0 > x1 ? x0 : x1) + R;
    if (ya >= yb || mxx < (float)fb->cx0 || mnx > (float)fb->cx1) return;

    const float dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
    const float inv = L2 > 1e-6f ? 1.0f / L2 : 0.0f;
    const float RL = R * sqrtf(L2);
    const float adx = fabsf(dx), ady = fabsf(dy);

    for (int y = ya; y < yb; y++) {
        const float py = (float)y + 0.5f, ey = py - y0;
        float lo = 1e9f, hi = -1e9f;
        float t = py - y0;
        if (t * t < R2) {
            float h = sqrtf(R2 - t * t);
            lo = x0 - h;
            hi = x0 + h;
        }
        t = py - y1;
        if (t * t < R2) {
            float h = sqrtf(R2 - t * t);
            if (x1 - h < lo) lo = x1 - h;
            if (x1 + h > hi) hi = x1 + h;
        }
        if (L2 > 1e-6f) {
            float a0 = -1e9f, a1 = 1e9f;
            if (adx > 1e-6f) {
                float u0 = (-ey * dy) / dx, u1 = (L2 - ey * dy) / dx;
                if (u0 > u1) { float s = u0; u0 = u1; u1 = s; }
                if (x0 + u0 > a0) a0 = x0 + u0;
                if (x0 + u1 < a1) a1 = x0 + u1;
            } else if (ey * dy < 0 || ey * dy > L2) {
                a1 = a0 - 1;
            }
            if (ady > 1e-6f) {
                float v0 = (ey * dx - RL) / dy, v1 = (ey * dx + RL) / dy;
                if (v0 > v1) { float s = v0; v0 = v1; v1 = s; }
                if (x0 + v0 > a0) a0 = x0 + v0;
                if (x0 + v1 < a1) a1 = x0 + v1;
            } else if (fabsf(ey * dx) > RL) {
                a1 = a0 - 1;
            }
            if (a1 >= a0) {
                if (a0 < lo) lo = a0;
                if (a1 > hi) hi = a1;
            }
        }
        if (hi < lo) continue;
        int xa = (int)floorf(lo), xb = (int)ceilf(hi);
        if (xa < fb->cx0) xa = fb->cx0;
        if (xb > fb->cx1) xb = fb->cx1;
        uint16_t *row = fb->px + (size_t)y * fb->w;
        for (int x = xa; x < xb; x++) {
            float ex = (float)x + 0.5f - x0;
            float tt = (ex * dx + ey * dy) * inv;
            if (tt < 0) tt = 0;
            else if (tt > 1) tt = 1;
            float qx = ex - tt * dx, qy = ey - tt * dy;
            float d2 = qx * qx + qy * qy;
            if (d2 >= R2) continue;
            if (d2 <= ri2) {
                row[x] = alpha >= 255 ? c : blend(row[x], rgb, alpha);
            } else {
                int al = (int)((R - sqrtf(d2)) * (float)alpha);
                if (al > 0) row[x] = blend(row[x], rgb, al > 255 ? 255 : al);
            }
        }
    }
}

/* A thin segment (w <= 4), the way Wu draws lines: one step per pixel
 * along the long axis, and across it the pixels the band covers, each by how
 * much of it is covered. No square roots and no per-pixel distance: the
 * capsule of seg() cost ~8 us a segment on the board, and at z13 over a
 * city the minor streets alone are 28 000 of them (230 ms, measured). The
 * columns [x0, x1) of consecutive segments meet without drawing the shared
 * one twice. */
static void seg_thin(mp_fb_t *fb, float x0, float y0, float x1, float y1, float w,
                     uint16_t c, uint32_t rgb, int alpha)
{
    float dx = x1 - x0, dy = y1 - y0;
    bool xmajor = fabsf(dx) >= fabsf(dy);
    if (!xmajor) {                      /* the same code with the axes swapped */
        float t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
        t = dx; dx = dy; dy = t;
    }
    if (x1 < x0) {
        float t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
        dy = -dy;
        dx = -dx;
    }
    if (dx < 1e-4f) return;
    const float slope = dy / dx;
    const float hv = w * 0.5f * sqrtf(1.0f + slope * slope);
    /* the major axis' clip */
    int m0 = xmajor ? fb->cx0 : fb->cy0, m1 = xmajor ? fb->cx1 : fb->cy1;
    int n0 = xmajor ? fb->cy0 : fb->cx0, n1 = xmajor ? fb->cy1 : fb->cx1;
    int a = (int)ceilf(x0 - 0.5f), b = (int)ceilf(x1 - 0.5f);
    if (a < m0) a = m0;
    if (b > m1) b = m1;
    for (int i = a; i < b; i++) {
        float cc = y0 + ((float)i + 0.5f - x0) * slope;
        float lo = cc - hv, hi = cc + hv;
        int j0 = (int)floorf(lo), j1 = (int)floorf(hi);
        if (j0 < n0) j0 = n0;
        if (j1 >= n1) j1 = n1 - 1;
        for (int j = j0; j <= j1; j++) {
            float top = (float)j > lo ? (float)j : lo, bot = (float)(j + 1) < hi ? (float)(j + 1) : hi;
            int al = (int)((bot - top) * (float)alpha);
            if (al <= 4) continue;
            uint16_t *p = xmajor ? fb->px + (size_t)j * fb->w + i : fb->px + (size_t)i * fb->w + j;
            *p = al >= 250 ? c : blend(*p, rgb, al);
        }
    }
}

void mp_polyline(mp_fb_t *fb, const float *xy, int n, float w, uint32_t rgb, int alpha)
{
    if (n < 2 || alpha <= 0) return;
    if (w < 1.0f) {
        alpha = (int)((float)alpha * w);
        w = 1.0f;
        if (alpha < 8) return;
    }
    uint16_t c = mp_be565(rgb);
    if (w <= 4.0f) {
        for (int i = 0; i + 1 < n; i++)
            seg_thin(fb, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], w, c, rgb, alpha);
        return;
    }
    float r = w * 0.5f;
    for (int i = 0; i + 1 < n; i++)
        seg(fb, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], r, c, rgb, alpha);
}

void mp_disc(mp_fb_t *fb, float cx, float cy, float r, uint32_t rgb, int alpha)
{
    uint16_t c = mp_be565(rgb);
    seg(fb, cx, cy, cx, cy, r, c, rgb, alpha);
}

/* ---------------------------------------------------------------------------
 * Text
 * ------------------------------------------------------------------------- */

static int utf8_put(char *o, uint32_t cp)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    o[0] = (char)(0xC0 | (cp >> 6));
    o[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
}

uint32_t mp_utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p;
    if (!c) return 0;
    if (c < 0x80) {
        *s += 1;
        return c;
    }
    int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    uint32_t cp = n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *s += i;
            return '?';
        }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return n == 1 ? '?' : cp;
}

static const mp_glyph_t *glyph(const mp_font_t *f, uint32_t cp)
{
    if (cp < MP_GLYPH_FIRST || cp > MP_GLYPH_LAST) cp = '?';
    const mp_glyph_t *g = &f->g[cp - MP_GLYPH_FIRST];
    if (!g->a) g = &f->g['?' - MP_GLYPH_FIRST];
    return g;
}

bool mp_font_bake(mp_font_t *f, const lv_font_t *font)
{
    memset(f, 0, sizeof *f);
    int lh = lv_font_get_line_height(font);
    int adv[MP_GLYPHS], maxadv = 1;
    for (int i = 0; i < MP_GLYPHS; i++) {
        uint32_t cp = (uint32_t)(MP_GLYPH_FIRST + i);
        adv[i] = (cp >= 0x7F && cp < 0xA0) ? 0 : (int)lv_font_get_glyph_width(font, cp, 0);
        if (adv[i] > maxadv) maxadv = adv[i];
    }
    const int pad = MP_HALO + 1;
    const int cw = maxadv + 2 * pad + 2, ch = lh + 2 * MP_HALO;
    const int cols = 16, rows = (MP_GLYPHS + cols - 1) / cols;
    const int W = cols * cw, H = rows * ch;

    lv_draw_buf_t *db = lv_draw_buf_create((uint32_t)W, (uint32_t)H, LV_COLOR_FORMAT_L8, 0);
    if (!db) return false;
    static char txt[MP_GLYPHS][4];
    lv_obj_t *cv = lv_canvas_create(lv_layer_top());
    lv_obj_add_flag(cv, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(cv, db);
    lv_canvas_fill_bg(cv, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(cv, &layer);
    for (int i = 0; i < MP_GLYPHS; i++) {
        if (adv[i] <= 0) continue;
        int n = utf8_put(txt[i], (uint32_t)(MP_GLYPH_FIRST + i));
        txt[i][n] = 0;
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.color = lv_color_hex(0xFFFFFF);
        d.font = font;
        d.text = txt[i];
        int x = (i % cols) * cw + pad, y = (i / cols) * ch + MP_HALO;
        lv_area_t area = { x, y, x + cw - pad - 1, y + lh - 1 };
        lv_draw_label(&layer, &d, &area);
    }
    lv_canvas_finish_layer(cv, &layer);

    size_t total = 0;
    for (int i = 0; i < MP_GLYPHS; i++) {
        if (adv[i] <= 0) continue;
        int w = adv[i] + 2 * pad + 1;
        total += (size_t)(w > cw ? cw : w) * ch * 2;
    }
    f->atlas = (uint8_t *)mp_malloc(total);
    bool ok = f->atlas != NULL;
    size_t at = 0;
    for (int i = 0; i < MP_GLYPHS && ok; i++) {
        mp_glyph_t *g = &f->g[i];
        g->adv = (int16_t)adv[i];
        if (adv[i] <= 0) continue;
        int w = adv[i] + 2 * pad + 1, h = ch;
        if (w > cw) w = cw;
        g->w = (int16_t)w;
        g->h = (int16_t)h;
        g->a = f->atlas + at;
        at += (size_t)w * h * 2;
        g->halo = g->a + (size_t)w * h;
        int ox = (i % cols) * cw, oy = (i / cols) * ch;
        for (int y = 0; y < h; y++)
            memcpy(g->a + (size_t)y * w, db->data + (size_t)(oy + y) * db->header.stride + ox, (size_t)w);
        /* the halo: the glyph grown by a disc of MP_HALO */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int m = 0;
                for (int dy = -MP_HALO; dy <= MP_HALO; dy++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= h) continue;
                    for (int dx = -MP_HALO; dx <= MP_HALO; dx++) {
                        int xx = x + dx;
                        if (xx < 0 || xx >= w || dx * dx + dy * dy > MP_HALO * MP_HALO + 1) continue;
                        int v = g->a[yy * w + xx];
                        if (v > m) m = v;
                    }
                }
                g->halo[y * w + x] = (uint8_t)m;
            }
        }
    }
    lv_obj_delete(cv);
    lv_draw_buf_destroy(db);
    f->line_h = lh;
    f->ok = ok;
    if (!ok) mp_font_free(f);
    return ok;
}

void mp_font_free(mp_font_t *f)
{
    mp_free(f->atlas);
    f->atlas = NULL;
    for (int i = 0; i < MP_GLYPHS; i++) f->g[i].a = f->g[i].halo = NULL;
    f->ok = false;
}

int mp_text_width(const mp_font_t *f, const char *s)
{
    int w = 0;
    uint32_t cp;
    while ((cp = mp_utf8_next(&s)) != 0) w += glyph(f, cp)->adv;
    return w;
}

static void mask(mp_fb_t *fb, const uint8_t *m, int w, int h, int x0, int y0, uint32_t rgb)
{
    for (int y = 0; y < h; y++) {
        int yy = y0 + y;
        if (yy < fb->cy0 || yy >= fb->cy1) continue;
        uint16_t *row = fb->px + (size_t)yy * fb->w;
        const uint8_t *mr = m + (size_t)y * w;
        for (int x = 0; x < w; x++) {
            int xx = x0 + x;
            if (!mr[x] || xx < fb->cx0 || xx >= fb->cx1) continue;
            row[xx] = mr[x] >= 250 ? mp_be565(rgb) : blend(row[xx], rgb, mr[x]);
        }
    }
}

void mp_text(mp_fb_t *fb, const mp_font_t *f, int x, int y, const char *s,
             uint32_t rgb, uint32_t halo_rgb, bool halo)
{
    const int pad = MP_HALO + 1;
    for (int pass = halo ? 0 : 1; pass < 2; pass++) {
        const char *p = s;
        int pen = x;
        uint32_t cp;
        while ((cp = mp_utf8_next(&p)) != 0) {
            const mp_glyph_t *g = glyph(f, cp);
            if (g->a) {
                mask(fb, pass ? g->a : g->halo, g->w, g->h, pen - pad, y - MP_HALO,
                     pass ? rgb : halo_rgb);
            }
            pen += g->adv;
        }
    }
}

void mp_glyph_rot(mp_fb_t *fb, const mp_font_t *f, uint32_t cp, float cx, float cy,
                  float angle, uint32_t rgb, int pass)
{
    const mp_glyph_t *g = glyph(f, cp);
    if (!g->a) return;
    const uint8_t *m = pass ? g->a : g->halo;
    const float u0 = (float)(MP_HALO + 1) + g->adv * 0.5f, v0 = (float)MP_HALO + f->line_h * 0.5f;

    /* nearly level: the mask as it is, which is sharper and ten times
     * cheaper than resampling it */
    if (fabsf(angle) < 0.035f) {
        mask(fb, m, g->w, g->h, (int)floorf(cx - u0 + 0.5f), (int)floorf(cy - v0 + 0.5f), rgb);
        return;
    }

    /* Turned: every screen pixel of the glyph's box, mapped back into the
     * mask and sampled bilinearly, in 16.16 fixed point stepped along the
     * row (a float per pixel, and a box sized by the glyph's diagonal, was
     * ~12 ms a street name on the board). */
    const float cs = cosf(angle), sn = sinf(angle);
    /* the box: the mask's four corners turned about (u0, v0) */
    float xs[4], ys[4];
    const float cu[4] = { 0, (float)g->w, 0, (float)g->w }, cv[4] = { 0, 0, (float)g->h, (float)g->h };
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int i = 0; i < 4; i++) {
        float du = cu[i] - u0, dv = cv[i] - v0;
        xs[i] = cx + du * cs - dv * sn;
        ys[i] = cy + du * sn + dv * cs;
        if (xs[i] < bx0) bx0 = xs[i];
        if (xs[i] > bx1) bx1 = xs[i];
        if (ys[i] < by0) by0 = ys[i];
        if (ys[i] > by1) by1 = ys[i];
    }
    int x0 = (int)floorf(bx0), x1 = (int)ceilf(bx1), y0 = (int)floorf(by0), y1 = (int)ceilf(by1);
    if (x0 < fb->cx0) x0 = fb->cx0;
    if (y0 < fb->cy0) y0 = fb->cy0;
    if (x1 > fb->cx1) x1 = fb->cx1;
    if (y1 > fb->cy1) y1 = fb->cy1;
    if (x0 >= x1 || y0 >= y1) return;

    const int32_t dux = (int32_t)(cs * 65536.0f), dvx = (int32_t)(-sn * 65536.0f);
    const int W = g->w, H = g->h;
    const uint16_t c = mp_be565(rgb);
    for (int y = y0; y < y1; y++) {
        float dy = (float)y + 0.5f - cy, dx = (float)x0 + 0.5f - cx;
        /* the sample point, minus half a pixel for the bilinear corners */
        int32_t fu = (int32_t)((u0 - 0.5f + dx * cs + dy * sn) * 65536.0f);
        int32_t fv = (int32_t)((v0 - 0.5f - dx * sn + dy * cs) * 65536.0f);
        uint16_t *row = fb->px + (size_t)y * fb->w;
        for (int x = x0; x < x1; x++, fu += dux, fv += dvx) {
            int iu = fu >> 16, iv = fv >> 16;
            /* the mask has a zero border of MP_HALO + 1: its last row and
             * column can be skipped */
            if ((unsigned)iu >= (unsigned)(W - 1) || (unsigned)iv >= (unsigned)(H - 1)) continue;
            const uint8_t *p = m + iv * W + iu;
            int a = p[0], b = p[1], d0 = p[W], d1 = p[W + 1];
            if (!(a | b | d0 | d1)) continue;
            int wx = (fu >> 8) & 255, wy = (fv >> 8) & 255;
            int top = a * 256 + (b - a) * wx, bot = d0 * 256 + (d1 - d0) * wx;
            int al = (top * 256 + (bot - top) * wy) >> 16;
            if (al > 8) row[x] = al >= 250 ? c : blend(row[x], rgb, al);
        }
    }
}
