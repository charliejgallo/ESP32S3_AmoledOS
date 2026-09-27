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

static int edge_cmp(const void *a, const void *b)
{
    return ((const edge_t *)a)->y0 - ((const edge_t *)b)->y0;
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

void mp_fill_poly(mp_fb_t *fb, const float *xy, const uint32_t *ring_n, int nring, uint32_t rgb)
{
    int total = 0;
    for (int r = 0; r < nring; r++) total += (int)ring_n[r];
    if (total < 3 || !ensure(total)) return;

    int ne = 0, ymin = 1 << 30, ymax = -(1 << 30);
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
                int y0 = (int)ceilf(ya - 0.5f), y1 = (int)ceilf(yb - 0.5f);
                if (y0 >= y1 || y1 <= fb->cy0 || y0 >= fb->cy1) continue;
                edge_t *e = &s_edges[ne++];
                e->dx = (xb - xa) / (yb - ya);
                e->x = xa + ((float)y0 + 0.5f - ya) * e->dx;
                e->y0 = y0;
                e->y1 = y1;
                if (y0 < ymin) ymin = y0;
                if (y1 > ymax) ymax = y1;
            }
        }
        p += 2 * n;
    }
    if (ne < 2) return;
    qsort(s_edges, (size_t)ne, sizeof(edge_t), edge_cmp);

    uint16_t c = mp_be565(rgb);
    int y = ymin < fb->cy0 ? fb->cy0 : ymin;
    int yend = ymax > fb->cy1 ? fb->cy1 : ymax;
    int next = 0, na = 0;
    for (; y < yend; y++) {
        while (next < ne && s_edges[next].y0 <= y) {
            edge_t *e = &s_edges[next];
            if (e->y0 < y) e->x += (float)(y - e->y0) * e->dx;
            s_act[na++] = next++;
        }
        int nx = 0;
        for (int i = 0; i < na;) {
            edge_t *e = &s_edges[s_act[i]];
            if (e->y1 <= y) {
                s_act[i] = s_act[--na];
                continue;
            }
            /* insertion into the sorted crossings */
            float x = e->x;
            int k = nx++;
            while (k > 0 && s_xs[k - 1] > x) {
                s_xs[k] = s_xs[k - 1];
                k--;
            }
            s_xs[k] = x;
            e->x += e->dx;
            i++;
        }
        if (nx < 2) {
            if (!na && next >= ne) break;
            continue;
        }
        uint16_t *row = fb->px + (size_t)y * fb->w;
        for (int k = 0; k + 1 < nx; k += 2) span(row, s_xs[k], s_xs[k + 1], fb->cx0, fb->cx1, c, rgb);
    }
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

/* A thin segment (w <= 2.5), the way Wu draws lines: one step per pixel
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
    if (w <= 2.5f) {
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

static inline int sample(const uint8_t *m, int w, int h, float u, float v)
{
    u -= 0.5f;
    v -= 0.5f;
    int iu = (int)floorf(u), iv = (int)floorf(v);
    float fu = u - (float)iu, fv = v - (float)iv;
    int a = (iu >= 0 && iv >= 0 && iu < w && iv < h) ? m[iv * w + iu] : 0;
    int b = (iu + 1 >= 0 && iv >= 0 && iu + 1 < w && iv < h) ? m[iv * w + iu + 1] : 0;
    int c = (iu >= 0 && iv + 1 >= 0 && iu < w && iv + 1 < h) ? m[(iv + 1) * w + iu] : 0;
    int d = (iu + 1 >= 0 && iv + 1 >= 0 && iu + 1 < w && iv + 1 < h) ? m[(iv + 1) * w + iu + 1] : 0;
    float top = (float)a + ((float)b - (float)a) * fu;
    float bot = (float)c + ((float)d - (float)c) * fu;
    return (int)(top + (bot - top) * fv);
}

void mp_glyph_rot(mp_fb_t *fb, const mp_font_t *f, uint32_t cp, float cx, float cy,
                  float angle, uint32_t rgb, int pass)
{
    const mp_glyph_t *g = glyph(f, cp);
    if (!g->a) return;
    const float u0 = (float)(MP_HALO + 1) + g->adv * 0.5f, v0 = (float)MP_HALO + f->line_h * 0.5f;
    const float cs = cosf(angle), sn = sinf(angle);
    float rr = sqrtf((float)(g->w * g->w + g->h * g->h)) * 0.5f + 2.0f;
    int x0 = (int)floorf(cx - rr), x1 = (int)ceilf(cx + rr);
    int y0 = (int)floorf(cy - rr), y1 = (int)ceilf(cy + rr);
    if (x0 < fb->cx0) x0 = fb->cx0;
    if (y0 < fb->cy0) y0 = fb->cy0;
    if (x1 > fb->cx1) x1 = fb->cx1;
    if (y1 > fb->cy1) y1 = fb->cy1;
    const uint8_t *m = pass ? g->a : g->halo;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = fb->px + (size_t)y * fb->w;
        float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - cx;
            float u = u0 + dx * cs + dy * sn;
            float v = v0 - dx * sn + dy * cs;
            if (u < -1 || v < -1 || u > g->w + 1 || v > g->h + 1) continue;
            int al = sample(m, g->w, g->h, u, v);
            if (al > 8) row[x] = al >= 250 ? mp_be565(rgb) : blend(row[x], rgb, al);
        }
    }
}
