/*
 * MAPAS - the renderer: tiles to pixels, in a dark style for the AMOLED.
 * See mp_render.h.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#include "mp_mem.h"
#include "mp_render.h"
#include "mp_store.h"
#include "mp_tile.h"
#include "aos_hal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f

/* ---------------------------------------------------------------------------
 * Projection
 * ------------------------------------------------------------------------- */

void mp_lonlat_to_world(float lon, float lat, uint32_t *x, uint32_t *y)
{
    if (lat > 85.05f) lat = 85.05f;
    if (lat < -85.05f) lat = -85.05f;
    while (lon < -180.0f) lon += 360.0f;
    while (lon >= 180.0f) lon -= 360.0f;
    float fx = (lon + 180.0f) / 360.0f;
    float phi = lat * PI_F / 180.0f;
    float fy = (1.0f - logf(tanf(PI_F / 4 + phi / 2)) / PI_F) * 0.5f;
    if (fy < 0) fy = 0;
    if (fx >= 1.0f) fx = 0.99999994f;
    if (fy >= 1.0f) fy = 0.99999994f;
    *x = (uint32_t)(fx * 4294967296.0f);
    *y = (uint32_t)(fy * 4294967296.0f);
}

void mp_world_to_lonlat(uint32_t x, uint32_t y, float *lon, float *lat)
{
    float fx = (float)x / 4294967296.0f, fy = (float)y / 4294967296.0f;
    *lon = fx * 360.0f - 180.0f;
    float my = PI_F * (1.0f - 2.0f * fy);
    *lat = (2.0f * atan2f(expf(my), 1.0f) - PI_F / 2) * 180.0f / PI_F;
}

float mp_px_per_unit(float z)
{
    return exp2f(z - 24.0f);
}

float mp_metres_per_px(const mp_view_t *v)
{
    float lon, lat;
    mp_world_to_lonlat(v->cx, v->cy, &lon, &lat);
    return 40075016.7f * cosf(lat * PI_F / 180.0f) / (256.0f * exp2f(v->z));
}

/* ---------------------------------------------------------------------------
 * Style
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t rgb;
    float    minz;
    float    w16;           /* lines: width at z 16; 0 = an area */
    float    wmin;          /* lines: never thinner than this once shown */
} style_t;

static const style_t ST[MC_GEOM_END] = {
    [MC_LAND_GRASS] = { 0x0F2117, 0, 0, 0 },
    [MC_LAND_WOOD]  = { 0x0D2515, 0, 0, 0 },
    [MC_LAND_SAND]  = { 0x221F17, 0, 0, 0 },
    /* Residential areas are not drawn: in this dark style their tone was a
     * shade off the land's, and at z12 over Buenos Aires they were the
     * dearest class of all (120-150 ms of a 320 ms render, measured). */
    [MC_RESID]      = { 0x13161C, 99, 0, 0 },
    [MC_INDUS]      = { 0x19171E, 10, 0, 0 },
    [MC_CIVIC]      = { 0x1B1824, 12, 0, 0 },
    [MC_PARK]       = { 0x0F2D1D, 0, 0, 0 },
    [MC_AERO_AREA]  = { 0x181B22, 9, 0, 0 },
    [MC_WATER]      = { 0x0C2946, 0, 0, 0 },
    [MC_BUILDING]   = { 0x252A34, 15, 0, 0 },
    [MC_STREAM]     = { 0x15406A, 13, 1.6f, 0 },
    [MC_RIVER]      = { 0x15406A, 8, 4.0f, 0.8f },
    [MC_BOUNDARY]   = { 0x6A5A8C, 0, 0, 0 },         /* fixed width, below */
    [MC_RUNWAY]     = { 0x353A45, 11, 10.0f, 0 },
    [MC_PATH]       = { 0x454A55, 15, 1.3f, 0 },
    [MC_SERVICE]    = { 0x2F3440, 14.5f, 2.6f, 0 },
    [MC_MINOR]      = { 0x3F4653, 12.5f, 4.6f, 0 },
    [MC_TERTIARY]   = { 0x59616F, 11, 6.0f, 0 },
    [MC_SECONDARY]  = { 0x8B8464, 9, 7.0f, 0.9f },
    [MC_PRIMARY]    = { 0xB88E44, 7, 8.0f, 1.1f },
    [MC_TRUNK]      = { 0xCF893B, 5, 8.5f, 1.3f },
    [MC_MOTORWAY]   = { 0xDF7935, 4, 9.0f, 1.4f },
    [MC_RAIL]       = { 0x686B75, 10, 1.6f, 0 },
    [MC_FERRY]      = { 0x2B5A89, 8, 1.2f, 0 },
};

static float line_width(int c, float z, int sub)
{
    if (c == MC_BOUNDARY) return sub <= 2 ? 1.8f : 1.1f;
    const style_t *s = &ST[c];
    float w = s->w16 * exp2f((z - 16.0f) * 0.7f);
    if (w < s->wmin) w = s->wmin;
    return w;
}

/* ---------------------------------------------------------------------------
 * Scratch
 * ------------------------------------------------------------------------- */

static float *s_xy;
static int    s_xy_cap;
static float *s_cum;
static int    s_cum_cap;

static bool xy_room(int n)
{
    if (n <= s_xy_cap) return true;
    int nc = s_xy_cap ? s_xy_cap : 4096;
    while (nc < n) nc *= 2;
    float *p = (float *)mp_realloc(s_xy, (size_t)nc * 2 * sizeof(float));
    if (!p) return false;
    s_xy = p;
    s_xy_cap = nc;
    float *c = (float *)mp_realloc(s_cum, (size_t)nc * sizeof(float));
    if (!c) return false;
    s_cum = c;
    s_cum_cap = nc;
    return true;
}

typedef struct {
    mp_tile_t *t;
    float      ox, oy, s;
    int        cx0, cy0, cx1, cy1;
    bool       own;
} item_t;

#define MAX_ITEMS 40

static uint32_t *s_rn;
static int       s_rn_cap;

/* Whether the feature's box can touch the clip (grown by pad): most of an
 * enlarged tile is off the buffer, and this skips it without a multiply
 * per point. */
static bool visible(const item_t *it, const mp_feat_t *f, float pad)
{
    float x0 = it->ox + f->bx0 * it->s, x1 = it->ox + f->bx1 * it->s;
    float y0 = it->oy + f->by0 * it->s, y1 = it->oy + f->by1 * it->s;
    return x1 + pad >= (float)it->cx0 && x0 - pad <= (float)it->cx1 &&
           y1 + pad >= (float)it->cy0 && y0 - pad <= (float)it->cy1;
}

/* The feature's points into s_xy, ring by ring into s_rn, dropping every
 * point closer than tol pixels to the last one kept (the last of a line is
 * always kept). A z12 tile seen at z13 has eight units to the pixel: most of
 * its points are the same pixel. Returns the rings kept (a polygon ring that
 * shrinks under three points goes). */
static int transform(const item_t *it, const mp_feat_t *f, int minpts, float tol,
                     mp_render_stats_t *st)
{
    const mp_tile_t *t = it->t;
    int npts = 0;
    for (int r = 0; r < f->nring; r++) npts += (int)t->ring_n[f->ring + r];
    if (!xy_room(npts)) return 0;
    if (f->nring > s_rn_cap) {
        int nc = s_rn_cap ? s_rn_cap : 512;
        while (nc < f->nring) nc *= 2;
        uint32_t *p = (uint32_t *)mp_realloc(s_rn, (size_t)nc * sizeof(uint32_t));
        if (!p) return 0;
        s_rn = p;
        s_rn_cap = nc;
    }
    const mp_pt_t *p = t->p + f->pt;
    const float s = it->s, ox = it->ox, oy = it->oy, tol2 = tol * tol;
    int out = 0, rings = 0;
    for (int r = 0; r < f->nring; r++) {
        int n = (int)t->ring_n[f->ring + r], start = out;
        float lx = 0, ly = 0;
        for (int i = 0; i < n; i++) {
            float x = ox + p[i].x * s, y = oy + p[i].y * s;
            if (i > 0 && i < n - 1) {
                float dx = x - lx, dy = y - ly;
                if (dx * dx + dy * dy < tol2) continue;
            }
            s_xy[2 * out] = x;
            s_xy[2 * out + 1] = y;
            out++;
            lx = x;
            ly = y;
        }
        p += n;
        int kept = out - start;
        if (kept < minpts) {
            out = start;
            continue;
        }
        s_rn[rings++] = (uint32_t)kept;
    }
    st->pts_in += (uint32_t)npts;
    st->pts_out += (uint32_t)out;
    return rings;
}


static uint32_t cycles(void)
{
#if defined(__XTENSA__)
    uint32_t c;
    __asm__ volatile("rsr %0, ccount" : "=a"(c));
    return c;
#else
    return (uint32_t)(aos_hal_uptime_ms() * 240000u);
#endif
}

/* ---------------------------------------------------------------------------
 * Labels
 * ------------------------------------------------------------------------- */

#define CELL 5

static uint32_t s_ldraw;          /* cycles spent drawing glyphs, for the log */

static uint8_t *s_grid;
static int      s_gw, s_gh, s_grid_cap;

static bool grid_init(int w, int h)
{
    s_gw = (w + CELL - 1) / CELL;
    s_gh = (h + CELL - 1) / CELL;
    int n = s_gw * s_gh;
    if (n > s_grid_cap) {
        uint8_t *g = (uint8_t *)mp_realloc(s_grid, (size_t)n);
        if (!g) return false;
        s_grid = g;
        s_grid_cap = n;
    }
    memset(s_grid, 0, (size_t)n);
    return true;
}

static bool grid_free(float x0, float y0, float x1, float y1)
{
    int a = (int)(x0 / CELL), b = (int)(y0 / CELL), c = (int)(x1 / CELL), d = (int)(y1 / CELL);
    if (a < 0 || b < 0 || c >= s_gw || d >= s_gh) return false;
    for (int y = b; y <= d; y++)
        for (int x = a; x <= c; x++)
            if (s_grid[y * s_gw + x]) return false;
    return true;
}

static void grid_mark(float x0, float y0, float x1, float y1)
{
    int a = (int)(x0 / CELL), b = (int)(y0 / CELL), c = (int)(x1 / CELL), d = (int)(y1 / CELL);
    if (a < 0) a = 0;
    if (b < 0) b = 0;
    if (c >= s_gw) c = s_gw - 1;
    if (d >= s_gh) d = s_gh - 1;
    for (int y = b; y <= d; y++)
        for (int x = a; x <= c; x++) s_grid[y * s_gw + x] = 1;
}

#define MAX_RESERVED 8
static int16_t s_res[MAX_RESERVED][4];
static int     s_nres;

void mp_render_reserve(const int16_t (*rects)[4], int n)
{
    s_nres = n < MAX_RESERVED ? n : MAX_RESERVED;
    memcpy(s_res, rects, (size_t)s_nres * sizeof s_res[0]);
}

typedef struct {
    const item_t    *it;
    const mp_feat_t *f;
    int              prio;
} cand_t;

static int cand_cmp(const void *a, const void *b)
{
    return ((const cand_t *)a)->prio - ((const cand_t *)b)->prio;
}

/* names already placed, so a street cut into many pieces is named once or
 * twice on the screen, not on every block */
#define MAX_PLACED 96
static struct { uint32_t h; float x, y; } s_placed[MAX_PLACED];
static int s_nplaced;

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static bool placed_near(uint32_t h, float x, float y, float d)
{
    for (int i = 0; i < s_nplaced; i++) {
        if (s_placed[i].h != h) continue;
        float dx = s_placed[i].x - x, dy = s_placed[i].y - y;
        if (dx * dx + dy * dy < d * d) return true;
    }
    return false;
}

static void placed_add(uint32_t h, float x, float y)
{
    if (s_nplaced >= MAX_PLACED) return;
    s_placed[s_nplaced].h = h;
    s_placed[s_nplaced].x = x;
    s_placed[s_nplaced].y = y;
    s_nplaced++;
}

/* place: which font and colour, or false if not at this zoom */
static bool place_style(int sub, float z, int *font, uint32_t *rgb)
{
    switch (sub) {
    case MP_PLACE_COUNTRY: *font = MP_FONT_L; *rgb = 0xE6E6EE; return z < 7;
    case MP_PLACE_STATE:   *font = MP_FONT_M; *rgb = 0xB9A9DA; return z >= 5 && z < 9;
    case MP_PLACE_CITY:    *font = MP_FONT_L; *rgb = 0xFFFFFF; return z >= 4 && z < 15.5f;
    case MP_PLACE_TOWN:    *font = MP_FONT_M; *rgb = 0xF0F0F2; return z >= 8 && z < 16;
    case MP_PLACE_VILLAGE: *font = MP_FONT_M; *rgb = 0xD5D9E0; return z >= 11 && z < 17;
    case MP_PLACE_SUBURB:  *font = MP_FONT_M; *rgb = 0xB4BECC; return z >= 11.5f && z < 16.5f;
    case MP_PLACE_QUARTER:
    case MP_PLACE_NEIGHBOURHOOD: *font = MP_FONT_S; *rgb = 0x9EA9B8; return z >= 13.5f;
    case MP_PLACE_HAMLET:  *font = MP_FONT_S; *rgb = 0x9EA9B8; return z >= 13;
    default:               *font = MP_FONT_S; *rgb = 0x9EA9B8; return z >= 15;
    }
}

static bool road_label_visible(int sub, float z)
{
    if (sub <= 2) return z >= 13.0f;
    if (sub <= 4) return z >= 14.0f;
    if (sub == 5) return z >= 15.0f;
    return z >= 16.5f;
}

static bool place_point(mp_fb_t *fb, const mp_font_t *font, float x, float y, const char *name, uint32_t rgb)
{
    int w = mp_text_width(font, name), h = font->line_h;
    float x0 = x - w * 0.5f - 2, y0 = y - h * 0.5f - 1, x1 = x + w * 0.5f + 2, y1 = y + h * 0.5f + 1;
    if (x0 < 2 || y0 < 2 || x1 > fb->w - 3 || y1 > fb->h - 3) return false;
    if (!grid_free(x0, y0, x1, y1)) return false;
    grid_mark(x0, y0, x1, y1);
    uint32_t c0 = cycles();
    mp_text(fb, font, (int)(x - w * 0.5f), (int)(y - h * 0.5f), name, rgb, MP_BG, true);
    s_ldraw += cycles() - c0;
    return true;
}

/* the point at distance s along the polyline (cum = running length): a
 * binary search, since every glyph asks three times and a street can have
 * hundreds of points (walking from the start each time was most of placing) */
static void along(const float *xy, const float *cum, int n, float s, int *k, float *px, float *py)
{
    int lo = 0, hi = n - 2;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (cum[mid] <= s) lo = mid;
        else hi = mid - 1;
    }
    *k = lo;
    int i = lo;
    float seg = cum[i + 1] - cum[i];
    float t = seg > 1e-4f ? (s - cum[i]) / seg : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    *px = xy[2 * i] + (xy[2 * i + 2] - xy[2 * i]) * t;
    *py = xy[2 * i + 1] + (xy[2 * i + 3] - xy[2 * i + 1]) * t;
}

#define MAX_LABEL_GLYPHS 48

static bool place_line(mp_fb_t *fb, const mp_font_t *font, float *xy, int n, const char *name,
                       uint32_t rgb, uint32_t hname)
{
    if (n < 2) return false;
    float *cum = s_cum;
    cum[0] = 0;
    for (int i = 1; i < n; i++) {
        float dx = xy[2 * i] - xy[2 * i - 2], dy = xy[2 * i + 1] - xy[2 * i - 1];
        cum[i] = cum[i - 1] + sqrtf(dx * dx + dy * dy);
    }
    float L = cum[n - 1];
    int tw = mp_text_width(font, name);
    if (L < tw + 24 || tw <= 0) return false;

    static const float FR[3] = { 0.5f, 0.28f, 0.72f };
    uint32_t cps[MAX_LABEL_GLYPHS];
    int adv[MAX_LABEL_GLYPHS], ng = 0;
    const char *p = name;
    uint32_t cp;
    while ((cp = mp_utf8_next(&p)) != 0 && ng < MAX_LABEL_GLYPHS) {
        cps[ng] = cp;
        char tmp[3] = { 0 };
        if (cp < 0x80) { tmp[0] = (char)cp; }
        else { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); }
        adv[ng] = mp_text_width(font, tmp);
        ng++;
    }
    float gx[MAX_LABEL_GLYPHS], gy[MAX_LABEL_GLYPHS], ga[MAX_LABEL_GLYPHS];
    const float half = font->line_h * 0.42f;

    for (int tries = 0; tries < (L > tw * 3 ? 3 : 1); tries++) {
        float s0 = L * FR[tries] - tw * 0.5f;
        if (s0 < 4 || s0 + tw > L - 4) continue;
        /* read left to right: if the stretch runs leftwards, walk it backwards */
        int k = 0;
        float ax, ay, bx, by;
        along(xy, cum, n, s0, &k, &ax, &ay);
        along(xy, cum, n, s0 + tw, &k, &bx, &by);
        bool rev = bx < ax;
        float pen = 0, prev = 0;
        bool ok = true;
        for (int g = 0; g < ng && ok; g++) {
            float sc = s0 + pen + adv[g] * 0.5f;
            float sa = sc - adv[g] * 0.5f - 1, sb = sc + adv[g] * 0.5f + 1;
            if (rev) {
                sc = 2 * s0 + tw - sc;
                float t = sa;
                sa = 2 * s0 + tw - sb;
                sb = 2 * s0 + tw - t;
            }
            int k1 = 0, k2 = 0, k3 = 0;
            float cx, cy, x1, y1, x2, y2;
            along(xy, cum, n, sc, &k1, &cx, &cy);
            along(xy, cum, n, sa, &k2, &x1, &y1);
            along(xy, cum, n, sb, &k3, &x2, &y2);
            float an = rev ? atan2f(y1 - y2, x1 - x2) : atan2f(y2 - y1, x2 - x1);
            if (g > 0) {
                float d = an - prev;
                while (d > PI_F) d -= 2 * PI_F;
                while (d < -PI_F) d += 2 * PI_F;
                if (fabsf(d) > 0.55f) ok = false;
            }
            prev = an;
            gx[g] = cx;
            gy[g] = cy;
            ga[g] = an;
            if (cx - half < 2 || cy - half < 2 || cx + half > fb->w - 3 || cy + half > fb->h - 3) ok = false;
            else if (!grid_free(cx - half, cy - half, cx + half, cy + half)) ok = false;
            pen += adv[g];
        }
        if (!ok) continue;
        for (int g = 0; g < ng; g++) grid_mark(gx[g] - half, gy[g] - half, gx[g] + half, gy[g] + half);
        uint32_t c0 = cycles();
        for (int g = 0; g < ng; g++) mp_glyph_rot(fb, font, cps[g], gx[g], gy[g], ga[g], MP_BG, 0);
        for (int g = 0; g < ng; g++) mp_glyph_rot(fb, font, cps[g], gx[g], gy[g], ga[g], rgb, 1);
        s_ldraw += cycles() - c0;
        placed_add(hname, gx[ng / 2], gy[ng / 2]);
        return true;
    }
    return false;
}

static int draw_labels(mp_fb_t *fb, const item_t *items, int nitems, float z, const mp_font_t *fonts)
{
    int ncand = 0;
    for (int i = 0; i < nitems; i++) {
        if (!items[i].own) continue;
        const mp_tile_t *t = items[i].t;
        ncand += t->cls[MC_COUNT] - t->cls[MC_GEOM_END];
    }
    if (!ncand || !grid_init(fb->w, fb->h)) return 0;
    for (int i = 0; i < s_nres; i++) grid_mark(s_res[i][0], s_res[i][1], s_res[i][2], s_res[i][3]);
    cand_t *c = (cand_t *)mp_malloc((size_t)ncand * sizeof(cand_t));
    if (!c) return 0;
    int nc = 0;
    for (int i = 0; i < nitems; i++) {
        if (!items[i].own) continue;
        const mp_tile_t *t = items[i].t;
        for (int k = t->cls[MC_GEOM_END]; k < t->cls[MC_COUNT]; k++) {
            const mp_feat_t *f = &t->f[k];
            if (f->name < 0) continue;
            int cls = k >= t->cls[MC_L_PLACE] ? MC_L_PLACE : k >= t->cls[MC_L_WATER] ? MC_L_WATER : MC_L_ROAD;
            int prio = cls == MC_L_PLACE ? f->rank : cls == MC_L_ROAD ? 1000 + f->rank : 2000 + f->rank;
            if (cls == MC_L_ROAD && !road_label_visible(f->sub, z)) continue;
            c[nc].it = &items[i];
            c[nc].f = f;
            c[nc].prio = prio;
            nc++;
        }
    }
    qsort(c, (size_t)nc, sizeof(cand_t), cand_cmp);
    s_nplaced = 0;
    mp_render_stats_t dummy;
    memset(&dummy, 0, sizeof dummy);
    int placed = 0;
    for (int i = 0; i < nc; i++) {
        const item_t *it = c[i].it;
        const mp_tile_t *t = it->t;
        const mp_feat_t *f = c[i].f;
        const char *name = t->names + f->name;
        int rings = transform(it, f, 1, 0.5f, &dummy);
        if (!rings) continue;
        if (c[i].prio < 1000) {
            /* a place: its point must be inside its own tile (the buffer
             * repeats it in the neighbours) */
            const mp_pt_t *p = t->p + f->pt;
            if (p->x < 0 || p->y < 0 || p->x >= t->extent || p->y >= t->extent) continue;
            int font;
            uint32_t rgb;
            if (!place_style(f->sub, z, &font, &rgb)) continue;
            placed += place_point(fb, &fonts[font], s_xy[0], s_xy[1], name, rgb);
        } else {
            bool water = c[i].prio >= 2000;
            if (water && z < 9 && f->rank > 201) continue;
            uint32_t h = hash(name);
            if (f->flags & MF_POINT) {
                const mp_pt_t *p = t->p + f->pt;
                if (p->x < 0 || p->y < 0 || p->x >= t->extent || p->y >= t->extent) continue;
                placed += place_point(fb, &fonts[MP_FONT_S], s_xy[0], s_xy[1], name, 0x86B6E6);
                continue;
            }
            float cx = s_xy[0], cy = s_xy[1];
            if (placed_near(h, cx, cy, 260.0f)) continue;
            int off = 0;
            for (int r = 0; r < rings; r++) {
                int n = (int)s_rn[r];
                if (place_line(fb, &fonts[MP_FONT_S], s_xy + 2 * off, n, name,
                               water ? 0x86B6E6 : 0xD2D6DE, h)) {
                    placed++;
                    break;
                }
                off += n;
            }
        }
    }
    mp_free(c);
    return placed;
}

/* ---------------------------------------------------------------------------
 * The view
 * ------------------------------------------------------------------------- */


void mp_render(mp_fb_t *fb, const mp_view_t *v, const mp_font_t *fonts, mp_render_stats_t *st)
{
    memset(st, 0, sizeof *st);
    uint32_t c0 = cycles();
    mp_store_frame();

    int dz = (int)floorf(v->z) - 1;
    if (dz < 0) dz = 0;
    if (dz > MP_DZ_MAX) dz = MP_DZ_MAX;
    const float tile_px = 256.0f * exp2f(v->z - (float)dz);
    const uint32_t ntiles = 1u << dz;
    uint32_t tcx, tcy;
    float fx, fy;
    if (dz == 0) {
        tcx = tcy = 0;
        fx = (float)v->cx / 4294967296.0f;
        fy = (float)v->cy / 4294967296.0f;
    } else {
        int sh = 32 - dz;
        tcx = v->cx >> sh;
        tcy = v->cy >> sh;
        fx = (float)(v->cx & ((1u << sh) - 1)) / (float)(1u << sh);
        fy = (float)(v->cy & ((1u << sh) - 1)) / (float)(1u << sh);
    }
    const float hw = fb->w * 0.5f, hh = fb->h * 0.5f;
    int i0 = (int)floorf(-hw / tile_px + fx), i1 = (int)floorf(hw / tile_px + fx);
    int j0 = (int)floorf(-hh / tile_px + fy), j1 = (int)floorf(hh / tile_px + fy);

    item_t items[MAX_ITEMS];
    int nitems = 0;
    float missd[MP_MAX_MISSING];

    for (int j = j0; j <= j1; j++) {
        int64_t ty = (int64_t)tcy + j;
        if (ty < 0 || ty >= ntiles) continue;
        for (int i = i0; i <= i1 && nitems < MAX_ITEMS; i++) {
            uint32_t tx = (uint32_t)(((int64_t)tcx + i) & (ntiles - 1));
            float X = hw + ((float)i - fx) * tile_px, Y = hh + ((float)j - fy) * tile_px;
            item_t *it = &items[nitems];
            it->cx0 = (int)floorf(X + 0.5f);
            it->cy0 = (int)floorf(Y + 0.5f);
            it->cx1 = (int)floorf(X + tile_px + 0.5f);
            it->cy1 = (int)floorf(Y + tile_px + 0.5f);
            bool need = false;
            mp_tile_t *t = mp_store_get(dz, tx, (uint32_t)ty, &need);
            if (t) {
                it->t = t;
                it->own = true;
                it->ox = X;
                it->oy = Y;
                it->s = tile_px / t->extent;
                nitems++;
                st->shown++;
                continue;
            }
            if (need) {
                /* nearest to the centre first */
                float d = ((float)i + 0.5f - fx) * ((float)i + 0.5f - fx) +
                          ((float)j + 0.5f - fy) * ((float)j + 0.5f - fy);
                int k;
                if (st->missing < MP_MAX_MISSING) k = st->missing++;
                else if (d < missd[MP_MAX_MISSING - 1]) k = MP_MAX_MISSING - 1;
                else k = -1;
                if (k >= 0) {
                    while (k > 0 && missd[k - 1] > d) {
                        st->miss[k] = st->miss[k - 1];
                        missd[k] = missd[k - 1];
                        k--;
                    }
                    st->miss[k].z = (uint8_t)dz;
                    st->miss[k].x = tx;
                    st->miss[k].y = (uint32_t)ty;
                    missd[k] = d;
                }
            }
            /* meanwhile, an ancestor enlarged: the nearest one already in
             * RAM, and only if there is none, the parent or grandparent
             * from the card (a z12 tile of a city centre is 0.4 s of card) */
            mp_tile_t *a = NULL;
            int up = 1;
            for (; up <= 6 && dz - up >= 0 && !a; up++) a = mp_store_peek(dz - up, tx >> up, (uint32_t)ty >> up);
            if (!a) {
                for (up = 1; up <= 2 && dz - up >= 0 && !a; up++) {
                    bool nn;
                    a = mp_store_get(dz - up, tx >> up, (uint32_t)ty >> up, &nn);
                }
            }
            if (a) {
                up--;
                uint32_t m = (1u << up) - 1;
                it->t = a;
                it->own = false;
                it->ox = X - (float)(tx & m) * tile_px;
                it->oy = Y - (float)((uint32_t)ty & m) * tile_px;
                it->s = tile_px * (float)(1u << up) / a->extent;
                nitems++;
                st->stand_in++;
            }
        }
    }

    uint32_t ct = cycles();
    st->us_tiles = (ct - c0) / 240;

    /* the land, then every class across every tile */
    mp_fb_clip_all(fb);
    mp_fill_rect(fb, 0, 0, fb->w, fb->h, MP_BG);
    for (int c = 0; c < MC_GEOM_END; c++) {
        const style_t *s = &ST[c];
        if (v->z < s->minz) continue;
        bool area = s->w16 == 0 && c != MC_BOUNDARY;
        int alpha = 255;
        if (c == MC_BUILDING && v->z < s->minz + 1) alpha = (int)((v->z - s->minz) * 255);
        if (!area && v->z < s->minz + 0.5f) alpha = (int)((v->z - s->minz) * 2 * 255);
        /* fading in and still nearly invisible: an area in the land's colour
         * costs the same as a visible one (buildings at z15.0 were 155 ms of
         * a 400 ms render over the centre of Buenos Aires, all of it the
         * colour of the land) */
        if (alpha < 24) continue;
        uint32_t cc0 = cycles();
        uint32_t rgb = s->rgb;
        if (area && alpha < 255) {
            /* an area fading in: mix its colour with the land's */
            int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
            int R = (MP_BG >> 16) & 255, G = (MP_BG >> 8) & 255, B = MP_BG & 255;
            rgb = (uint32_t)((R + (r - R) * alpha / 255) << 16 | (G + (g - G) * alpha / 255) << 8 |
                             (B + (b - B) * alpha / 255));
        }
        for (int i = 0; i < nitems; i++) {
            const item_t *it = &items[i];
            const mp_tile_t *t = it->t;
            if (t->cls[c] == t->cls[c + 1]) continue;
            mp_fb_clip(fb, it->cx0, it->cy0, it->cx1, it->cy1);
            for (int k = t->cls[c]; k < t->cls[c + 1]; k++) {
                const mp_feat_t *f = &t->f[k];
                float w = area ? 0 : line_width(c, v->z, f->sub);
                if (!visible(it, f, w + 1)) continue;
                int rings = transform(it, f, area ? 3 : 2, area ? 0.6f : 0.8f, st);
                if (!rings) continue;
                if (c == MC_BUILDING) {
                    /* no anti-aliasing: dark grey on near black, it does
                     * not show, and it makes every span write-only. (A
                     * tile's buildings come as a few hundred features of
                     * many rings each; gathering them all into one polygon
                     * was tried and lost: 800 KB of edges in PSRAM cost
                     * more than the calls it saved.) */
                    mp_fill_poly_ex(fb, s_xy, s_rn, rings, rgb, false);
                    continue;
                }
                if (area) {
                    mp_fill_poly(fb, s_xy, s_rn, rings, rgb);
                } else {
                    int al = (f->flags & MF_TUNNEL) ? alpha / 2 : alpha;
                    int off = 0;
                    for (int r = 0; r < rings; r++) {
                        int n = (int)s_rn[r];
                        mp_polyline(fb, s_xy + 2 * off, n, w, rgb, al);
                        off += n;
                    }
                }
            }
        }
        st->us_cls[c] += (cycles() - cc0) / 240;
    }
    mp_fb_clip_all(fb);
    uint32_t c1 = cycles();
    s_ldraw = 0;
    if (fonts && fonts[0].ok) st->labels = draw_labels(fb, items, nitems, v->z, fonts);
    st->us_ldraw = s_ldraw / 240;
    uint32_t c2 = cycles();
    st->us_geom = (c1 - ct) / 240;
    st->us_labels = (c2 - c1) / 240;
}
