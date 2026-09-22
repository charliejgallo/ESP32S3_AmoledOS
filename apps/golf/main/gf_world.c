/*
 * GOLF - loading a hole: polygons, ground heights, normals and trees
 *
 * See gf_world.h. Everything is computed when the hole loads (about a
 * second on the board is the budget) and read many times afterwards.
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_world.h"
#include "gf_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The biggest grid any hole may need: 320 x 640 m of ground. The holes are
 * written to fit (gf_holes.c asserts nothing; the loader clamps). */
#define GRID_MAX_W   320
#define GRID_MAX_H   640
#define MARGIN       24.0f          /* ground beyond the playing area        */
#define TCELL        16.0f

/* --------------------------------------------------------------------------
 * Noise
 * -------------------------------------------------------------------------- */

uint32_t gf_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static float lattice(int ix, int iy, uint32_t seed)
{
    uint32_t h = gf_hash((uint32_t)ix * 0x8da6b343U ^ (uint32_t)iy * 0xd8163841U ^ seed * 0xcb1ab31fU);
    return (float)(h & 0xFFFF) * (2.0f / 65535.0f) - 1.0f;
}

float gf_noise(float x, float y, float period, uint32_t seed)
{
    float fx = x / period, fy = y / period;
    float flx = gf_floorf(fx), fly = gf_floorf(fy);
    int   ix = (int)flx, iy = (int)fly;
    float tx = fx - flx, ty = fy - fly;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = lattice(ix, iy, seed),     b = lattice(ix + 1, iy, seed);
    float c = lattice(ix, iy + 1, seed), d = lattice(ix + 1, iy + 1, seed);
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

static float smooth01(float e0, float e1, float v)
{
    float t = (v - e0) / (e1 - e0);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* --------------------------------------------------------------------------
 * Polygons
 * -------------------------------------------------------------------------- */

bool gf_poly_inside(const gf_world_t *w, const gf_poly_t *p, float x, float y)
{
    if (x < p->x0 || x > p->x1 || y < p->y0 || y > p->y1 || p->kind == SH_PATH) {
        return false;
    }
    const float *v = w->pts + 2 * p->first;
    bool in = false;
    int n = p->n;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = v[2 * i], yi = v[2 * i + 1], xj = v[2 * j], yj = v[2 * j + 1];
        if ((yi > y) != (yj > y)) {
            float xc = xj + (y - yj) * (xi - xj) / (yi - yj);
            if (x < xc) {
                in = !in;
            }
        }
    }
    return in;
}

float gf_poly_dist(const gf_world_t *w, const gf_poly_t *p, float x, float y)
{
    const float *v = w->pts + 2 * p->first;
    const float *il = w->inv_l2 + p->first;
    int n = p->n;
    float best = 1e18f;
    int last = p->kind == SH_PATH ? n - 1 : n;
    for (int i = 0; i < last; i++) {
        int j = i + 1 < n ? i + 1 : 0;
        float ax = v[2 * i], ay = v[2 * i + 1];
        float dx = v[2 * j] - ax, dy = v[2 * j + 1] - ay;
        float px = x - ax, py = y - ay;
        float t = (px * dx + py * dy) * il[i];
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float qx = px - t * dx, qy = py - t * dy;
        float d = qx * qx + qy * qy;
        if (d < best) best = d;
    }
    return sqrtf(best);
}

/* signed: negative inside */
static float poly_sdist(const gf_world_t *w, const gf_poly_t *p, float x, float y)
{
    float d = gf_poly_dist(w, p, x, y);
    return gf_poly_inside(w, p, x, y) ? -d : d;
}

static void poly_bbox(gf_world_t *w, gf_poly_t *p)
{
    const float *v = w->pts + 2 * p->first;
    p->x0 = p->x1 = v[0];
    p->y0 = p->y1 = v[1];
    for (int i = 1; i < p->n; i++) {
        float x = v[2 * i], y = v[2 * i + 1];
        if (x < p->x0) p->x0 = x;
        if (x > p->x1) p->x1 = x;
        if (y < p->y0) p->y0 = y;
        if (y > p->y1) p->y1 = y;
    }
}

/* Closed Catmull-Rom through the control points, 'sub' points per span; an
 * open one for paths. */
static int add_spline(gf_world_t *w, const gf_pt_t *c, int n, bool closed, int sub)
{
    int first = w->npts;
    int spans = closed ? n : n - 1;
    for (int i = 0; i < spans; i++) {
        int i0 = closed ? (i - 1 + n) % n : (i > 0 ? i - 1 : 0);
        int i1 = i;
        int i2 = closed ? (i + 1) % n : i + 1;
        int i3 = closed ? (i + 2) % n : (i + 2 < n ? i + 2 : n - 1);
        for (int s = 0; s < sub; s++) {
            if (w->npts >= GF_MAX_PTS) {
                return w->npts - first;
            }
            float t = (float)s / (float)sub, t2 = t * t, t3 = t2 * t;
            float b0 = -0.5f * t3 + t2 - 0.5f * t;
            float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
            float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
            float b3 = 0.5f * t3 - 0.5f * t2;
            w->pts[2 * w->npts]     = b0 * c[i0].x + b1 * c[i1].x + b2 * c[i2].x + b3 * c[i3].x;
            w->pts[2 * w->npts + 1] = b0 * c[i0].y + b1 * c[i1].y + b2 * c[i2].y + b3 * c[i3].y;
            w->npts++;
        }
    }
    if (!closed && w->npts < GF_MAX_PTS) {
        w->pts[2 * w->npts]     = c[n - 1].x;
        w->pts[2 * w->npts + 1] = c[n - 1].y;
        w->npts++;
    }
    return w->npts - first;
}

/* A copy of a polygon pushed out by d metres along its normals. The shapes
 * are smooth, so moving each vertex along the mean of its two edge normals is
 * enough (no self-intersection for the offsets used: 1-2 m). */
static int add_offset(gf_world_t *w, const gf_poly_t *src, float d)
{
    int n = src->n;
    if (w->npts + n > GF_MAX_PTS) {
        return -1;
    }
    const float *v = w->pts + 2 * src->first;
    /* orientation: positive area = counter-clockwise */
    float area = 0.0f;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        area += v[2 * j] * v[2 * i + 1] - v[2 * i] * v[2 * j + 1];
    }
    float sgn = area > 0.0f ? 1.0f : -1.0f;
    int first = w->npts;
    for (int i = 0; i < n; i++) {
        int a = (i - 1 + n) % n, b = (i + 1) % n;
        float ex = v[2 * b] - v[2 * a], ey = v[2 * b + 1] - v[2 * a + 1];
        float l = sqrtf(ex * ex + ey * ey);
        if (l < 1e-4f) {
            l = 1e-4f;
        }
        /* for a counter-clockwise polygon the outward normal is (ey, -ex) */
        float nx = sgn * ey / l, ny = -sgn * ex / l;
        w->pts[2 * w->npts]     = v[2 * i] + nx * d;
        w->pts[2 * w->npts + 1] = v[2 * i + 1] + ny * d;
        w->npts++;
    }
    return first;
}

/* --------------------------------------------------------------------------
 * The grid
 * -------------------------------------------------------------------------- */

void gf_world_grid_size(const gf_hole_t *h, int *gw, int *gh)
{
    *gw = (int)(((float)(h->x1 - h->x0) + 2 * MARGIN) / GF_CELL) + 1;
    *gh = (int)(((float)(h->y1 - h->y0) + 2 * MARGIN) / GF_CELL) + 1;
    if (*gw > GRID_MAX_W) *gw = GRID_MAX_W;
    if (*gh > GRID_MAX_H) *gh = GRID_MAX_H;
}

bool gf_world_init(gf_world_t *w, int cells)
{
    memset(w, 0, sizeof(*w));
    w->cells_max = cells;
    w->height = (int16_t *)gf_malloc((size_t)cells * sizeof(int16_t));
    w->nrm    = (int8_t *)gf_malloc((size_t)cells * 2);
    w->pts    = (float *)gf_malloc((size_t)GF_MAX_PTS * 2 * sizeof(float));
    w->inv_l2 = (float *)gf_malloc((size_t)GF_MAX_PTS * sizeof(float));
    w->trees  = (gf_tree_t *)gf_malloc((size_t)GF_MAX_TREES * sizeof(gf_tree_t));
    int tcw = (int)(GRID_MAX_W / TCELL) + 1, tch = (int)(GRID_MAX_H / TCELL) + 1;
    w->tcell_first = (uint16_t *)gf_malloc((size_t)(tcw * tch + 1) * sizeof(uint16_t));
    w->tidx        = (uint16_t *)gf_malloc((size_t)GF_MAX_TREES * sizeof(uint16_t));
    if (!w->height || !w->nrm || !w->pts || !w->inv_l2 || !w->trees || !w->tcell_first || !w->tidx) {
        gf_world_free(w);
        return false;
    }
    return true;
}

void gf_world_free(gf_world_t *w)
{
    free(w->height);
    free(w->nrm);
    free(w->pts);
    free(w->inv_l2);
    free(w->trees);
    free(w->tcell_first);
    free(w->tidx);
    w->height = NULL;
    w->nrm = NULL;
    w->pts = NULL;
    w->inv_l2 = NULL;
    w->trees = NULL;
    w->tcell_first = NULL;
    w->tidx = NULL;
}

static inline float hcell(const gf_world_t *w, int ix, int iy)
{
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= w->gw) ix = w->gw - 1;
    if (iy >= w->gh) iy = w->gh - 1;
    return (float)w->height[iy * w->gw + ix] * 0.01f;
}

float gf_height(const gf_world_t *w, float x, float y)
{
    float fx = (x - w->gx0) / GF_CELL, fy = (y - w->gy0) / GF_CELL;
    float flx = gf_floorf(fx), fly = gf_floorf(fy);
    int ix = (int)flx, iy = (int)fly;
    float tx = fx - flx, ty = fy - fly;
    float a = hcell(w, ix, iy),     b = hcell(w, ix + 1, iy);
    float c = hcell(w, ix, iy + 1), d = hcell(w, ix + 1, iy + 1);
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

void gf_normal(const gf_world_t *w, float x, float y, float *nx, float *ny)
{
    /* the slope, from the heights on either side: smoother than the stored
     * per-cell normal and exact enough for the roll */
    const float e = 0.5f;
    *nx = (gf_height(w, x + e, y) - gf_height(w, x - e, y)) / (2.0f * e);
    *ny = (gf_height(w, x, y + e) - gf_height(w, x, y - e)) / (2.0f * e);
}

bool gf_in_bounds(const gf_world_t *w, float x, float y)
{
    const gf_hole_t *h = w->def;
    return x >= h->x0 && x <= h->x1 && y >= h->y0 && y <= h->y1;
}

int gf_lie(const gf_world_t *w, float x, float y)
{
    if (!gf_in_bounds(w, x, y)) {
        return LIE_OB;
    }
    /* from the top of the painting order down */
    int best = -1, lie = LIE_ROUGH;
    static const int8_t prio[] = {
        [SH_FAIRWAY] = 6, [SH_GREEN] = 14, [SH_BUNKER] = 16, [SH_WATER] = 18,
        [SH_TEE] = 10, [SH_DEEP] = 2, [SH_FOREST] = 0, [SH_PATH] = 20,
        [SH_WASTE] = 4, [GF_POLY_FRINGE] = 12, [GF_POLY_FIRST] = 4, [GF_POLY_LAKE] = 3,
    };
    static const int8_t lie_of[] = {
        [SH_FAIRWAY] = LIE_FAIRWAY, [SH_GREEN] = LIE_GREEN, [SH_BUNKER] = LIE_BUNKER,
        [SH_WATER] = LIE_WATER, [SH_TEE] = LIE_TEE, [SH_DEEP] = LIE_DEEP,
        [SH_FOREST] = LIE_FOREST, [SH_PATH] = LIE_PATH, [SH_WASTE] = LIE_WASTE,
        [GF_POLY_FRINGE] = LIE_FRINGE, [GF_POLY_FIRST] = LIE_FIRST, [GF_POLY_LAKE] = LIE_WATER,
    };
    for (int i = 0; i < w->npoly; i++) {
        const gf_poly_t *p = &w->poly[i];
        if (prio[p->kind] <= best) {
            continue;
        }
        bool in;
        if (p->kind == SH_PATH) {
            in = x >= p->x0 - 1.2f && x <= p->x1 + 1.2f && y >= p->y0 - 1.2f &&
                 y <= p->y1 + 1.2f && gf_poly_dist(w, p, x, y) < 1.2f;
        } else {
            in = gf_poly_inside(w, p, x, y);
        }
        if (in) {
            best = prio[p->kind];
            lie  = lie_of[p->kind];
        }
    }
    return lie;
}

/* --------------------------------------------------------------------------
 * Trees
 * -------------------------------------------------------------------------- */

/* normal height and canopy radius of each kind, metres */
static const float TREE_H[GF_TREE_KINDS] = { 14.2f, 12.5f, 16.0f, 10.4f, 1.3f };
/* the Blender art's own sizes (assets/props/meta.json) */
static const float TREE_R[GF_TREE_KINDS] = { 3.7f, 6.8f, 2.2f, 4.2f, 0.9f };

static void add_tree(gf_world_t *w, float x, float y, int kind, int size_pct, uint32_t hs)
{
    if (w->ntrees >= GF_MAX_TREES) {
        return;
    }
    gf_tree_t *t = &w->trees[w->ntrees++];
    float s = (float)size_pct / 100.0f;
    t->x = x;
    t->y = y;
    t->kind = (uint8_t)kind;
    t->height = TREE_H[kind] * s;
    t->radius = TREE_R[kind] * s;
    t->shade = (uint8_t)(hs & 15);
    t->h = 0.0f;
}

static bool near_play(gf_world_t *w, float x, float y, float clear)
{
    for (int i = 0; i < w->npoly; i++) {
        const gf_poly_t *p = &w->poly[i];
        int k = p->kind;
        if (k != SH_FAIRWAY && k != SH_GREEN && k != SH_TEE && k != SH_BUNKER &&
            !GF_IS_WATER(k) && k != SH_PATH && k != GF_POLY_FRINGE) {
            continue;
        }
        if (x < p->x0 - clear || x > p->x1 + clear || y < p->y0 - clear || y > p->y1 + clear) {
            continue;
        }
        if (k != SH_PATH && gf_poly_inside(w, p, x, y)) {
            return true;
        }
        if (gf_poly_dist(w, p, x, y) < clear) {
            return true;
        }
    }
    return false;
}

static void fill_forest(gf_world_t *w, const gf_poly_t *p, int density, int mix, uint32_t seed)
{
    /* a jittered grid: spacing from the density, one tree per cell at most */
    float sp = 13.0f - (float)density;          /* 3..12 m */
    if (sp < 3.0f) sp = 3.0f;
    for (float y = p->y0; y <= p->y1; y += sp) {
        gf_yield();
        for (float x = p->x0; x <= p->x1; x += sp) {
            uint32_t hs = gf_hash((uint32_t)(int)(x * 7.0f) * 73856093U ^ (uint32_t)(int)(y * 7.0f) * 19349663U ^ seed);
            float jx = x + ((float)(hs & 255) / 255.0f - 0.5f) * sp * 0.9f;
            float jy = y + ((float)((hs >> 8) & 255) / 255.0f - 0.5f) * sp * 0.9f;
            if (!gf_poly_inside(w, p, jx, jy) || near_play(w, jx, jy, 2.5f)) {
                continue;
            }
            int kind;
            if (mix == GF_TREE_MIXED) {
                int r = (int)((hs >> 16) % 10);
                kind = r < 4 ? GF_TREE_PINE : (r < 8 ? GF_TREE_OAK : GF_TREE_POPLAR);
            } else {
                kind = mix;
                if (((hs >> 20) & 7) == 0) {
                    kind = GF_TREE_BUSH;
                }
            }
            add_tree(w, jx, jy, kind, 80 + (int)((hs >> 24) % 45), hs >> 4);
        }
    }
}

static void index_trees(gf_world_t *w)
{
    w->tcw = (int)((float)w->gw * GF_CELL / TCELL) + 1;
    w->tch = (int)((float)w->gh * GF_CELL / TCELL) + 1;
    int cells = w->tcw * w->tch;
    /* counting sort by cell */
    memset(w->tcell_first, 0, (size_t)(cells + 1) * sizeof(uint16_t));
    for (int i = 0; i < w->ntrees; i++) {
        int cx = (int)((w->trees[i].x - w->gx0) / TCELL);
        int cy = (int)((w->trees[i].y - w->gy0) / TCELL);
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx >= w->tcw) cx = w->tcw - 1;
        if (cy >= w->tch) cy = w->tch - 1;
        w->tcell_first[cy * w->tcw + cx + 1]++;
    }
    for (int c = 0; c < cells; c++) {
        w->tcell_first[c + 1] = (uint16_t)(w->tcell_first[c + 1] + w->tcell_first[c]);
    }
    /* scratch: the height grid's normal buffer is free at this point? no -
     * use a small local fill counter instead */
    static uint16_t fillc[(GRID_MAX_W / 16 + 1) * (GRID_MAX_H / 16 + 1)];
    memset(fillc, 0, sizeof(fillc));
    for (int i = 0; i < w->ntrees; i++) {
        int cx = (int)((w->trees[i].x - w->gx0) / TCELL);
        int cy = (int)((w->trees[i].y - w->gy0) / TCELL);
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx >= w->tcw) cx = w->tcw - 1;
        if (cy >= w->tch) cy = w->tch - 1;
        int c = cy * w->tcw + cx;
        w->tidx[w->tcell_first[c] + fillc[c]++] = (uint16_t)i;
    }
}

/* --------------------------------------------------------------------------
 * Heights
 * -------------------------------------------------------------------------- */

/* For each cell inside the bounding box (plus a margin) of a polygon, call
 * fn with the signed distance. Cheap for the small shapes, and the fairways
 * are few. */
/* The heights are built straight into the int16 grid (centimetres): two
 * float scratch grids used to cost up to 1.4 MB of PSRAM during the load,
 * which the board did not always have. HG/HS read and write in metres. */
static inline float HG(const gf_world_t *w, int i)
{
    return (float)w->height[i] * 0.01f;
}
static inline void HS(gf_world_t *w, int i, float v)
{
    float c = v * 100.0f;
    if (c > 32000.0f) c = 32000.0f;
    if (c < -32000.0f) c = -32000.0f;
    w->height[i] = (int16_t)(c < 0 ? c - 0.5f : c + 0.5f);
}

typedef void (*cell_fn)(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg);

static void for_cells(gf_world_t *w, const gf_poly_t *p, float margin, cell_fn fn, void *arg)
{
    int ix0 = (int)gf_floorf((p->x0 - margin - w->gx0) / GF_CELL);
    int ix1 = (int)gf_ceilf((p->x1 + margin - w->gx0) / GF_CELL);
    int iy0 = (int)gf_floorf((p->y0 - margin - w->gy0) / GF_CELL);
    int iy1 = (int)gf_ceilf((p->y1 + margin - w->gy0) / GF_CELL);
    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 >= w->gw) ix1 = w->gw - 1;
    if (iy1 >= w->gh) iy1 = w->gh - 1;
    for (int iy = iy0; iy <= iy1; iy++) {
        if ((iy & 7) == 0) gf_yield();
        float y = w->gy0 + (float)iy * GF_CELL;
        for (int ix = ix0; ix <= ix1; ix++) {
            float x = w->gx0 + (float)ix * GF_CELL;
            float sd = poly_sdist(w, p, x, y);
            if (sd < margin) {
                fn(w, iy * w->gw + ix, sd, p, x, y, arg);
            }
        }
    }
}

/* fairways: the noise is calmed inside (h holds the noise part only here) */
static void fn_calm(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)p; (void)x; (void)y;
    uint8_t *calm = (uint8_t *)arg;
    float k = smooth01(8.0f, -2.0f, sd);            /* 1 inside, 0 at 8 m out */
    float c = 1.0f - 0.7f * k;
    uint8_t q = (uint8_t)(c * 255.0f);
    if (q < calm[idx]) {
        calm[idx] = q;
    }
}

typedef struct {
    float cx, cy, h0, tx, ty, bump, rise_edge;
} green_arg_t;

static void fn_green(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)p;
    green_arg_t *g = (green_arg_t *)arg;
    float dx = x - g->cx, dy = y - g->cy;
    /* a tilted plane, a soft ridge across, and the whole green a little
     * above what surrounds it */
    float plane = g->h0 + g->tx * dx + g->ty * dy +
                  g->bump * expf(-(dy * 0.9f + dx * 0.4f) * (dy * 0.9f + dx * 0.4f) / 40.0f) +
                  g->rise_edge;
    float k = smooth01(7.0f, 0.5f, sd);
    float hv = HG(w, idx);
    HS(w, idx, hv + (plane - hv) * k);
}

static void fn_tee(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)p; (void)x; (void)y;
    float top = *(float *)arg;
    float k = smooth01(3.0f, 0.3f, sd);
    float hv = HG(w, idx);
    HS(w, idx, hv + (top - hv) * k);
}

static void fn_bunker(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)x; (void)y; (void)arg;
    float depth = p->param;
    /* a raised lip just outside, the hollow inside */
    float lip  = 0.25f * smooth01(2.5f, 0.6f, sd) * smooth01(-0.4f, 0.4f, sd);
    float hole = depth * smooth01(0.2f, -1.4f, sd);
    HS(w, idx, HG(w, idx) + lip - hole);
}

static void fn_waste(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)p; (void)x; (void)y; (void)arg;
    HS(w, idx, HG(w, idx) - 0.2f * smooth01(1.0f, -2.0f, sd));
}

/* signed distance to the nearest land shape (negative on it), up to 'far' */
static float land_sd(const gf_world_t *w, float x, float y, float far)
{
    float best = far;
    for (int i = 0; i < w->npoly; i++) {
        const gf_poly_t *q = &w->poly[i];
        int k = q->kind;
        if (k != SH_FAIRWAY && k != SH_GREEN && k != SH_TEE && k != SH_BUNKER && k != GF_POLY_FRINGE) continue;
        if (x < q->x0 - far || x > q->x1 + far || y < q->y0 - far || y > q->y1 + far) continue;
        float d = gf_poly_dist(w, q, x, y);
        if (gf_poly_inside(w, q, x, y)) d = -d;
        if (d < best) best = d;
    }
    return best;
}

static void fn_water(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)arg;
    /* a lake leaves its islands where they are, with a sloping bank */
    if (p->kind == GF_POLY_LAKE) {
        float ls = land_sd(w, x, y, 3.0f);
        if (ls < 3.0f) {
            float lvl = w->water_level;
            float bed = lvl - 0.25f - 1.2f * smooth01(0.0f, -6.0f, sd);
            float t = smooth01(-0.5f, 3.0f, ls);
            float target = (lvl + 0.5f) * (1.0f - t) + bed * t;
            if (ls < 0.0f && HG(w, idx) > target) return;
            HS(w, idx, target);
            return;
        }
    }
    float lvl = w->water_level;
    /* banks: from the ground down to just under the surface over 5 m, then
     * the bed sinks */
    float bed = lvl - 0.25f - 1.2f * smooth01(0.0f, -6.0f, sd);
    float k = smooth01(5.0f, 0.0f, sd);
    float target = sd > 0.0f ? (lvl + 0.35f) : bed;
    float hv = HG(w, idx);
    float nh = hv + (target - hv) * k;
    if (sd > 0.0f && nh < lvl + 0.1f) {
        nh = lvl + 0.1f;                    /* the bank stays dry */
    }
    HS(w, idx, nh);
}

typedef struct { float min; } water_arg_t;


static void fn_water_min(gf_world_t *w, int idx, float sd, const gf_poly_t *p, float x, float y, void *arg)
{
    (void)w; (void)p; (void)x; (void)y;
    water_arg_t *a = (water_arg_t *)arg;
    if (sd > 0.0f && sd < 8.0f && HG(w, idx) < a->min) {
        a->min = HG(w, idx);
    }
}

static void build_heights(gf_world_t *w)
{
    const gf_hole_t *d = w->def;
    uint32_t seed = d->seed * 2654435761U + 17;
    float amp = 0.35f + 0.3f * (float)d->rough;         /* metres           */
    float rise = (float)d->rise * 0.01f;
    float y0 = w->tee_y, y1 = w->pin_y;
    float span = fabsf(y1 - y0) > 1.0f ? (y1 - y0) : 1.0f;

    int n = w->gw * w->gh;
    /* the calm factor per cell lives in the normals' buffer until the
     * normals are computed, at the end of the load */
    uint8_t *calm = (uint8_t *)w->nrm;
    for (int i = 0; i < n; i++) {
        calm[i] = 255;
    }
    for (int i = 0; i < w->npoly; i++) {
        if (w->poly[i].kind == SH_FAIRWAY || w->poly[i].kind == SH_TEE) {
            for_cells(w, &w->poly[i], 8.0f, fn_calm, calm);
        }
    }

    for (int iy = 0; iy < w->gh; iy++) {
        if ((iy & 15) == 0) gf_yield();
        float y = w->gy0 + (float)iy * GF_CELL;
        float t = (y - y0) / span;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float base = rise * (t * t * (3.0f - 2.0f * t));
        for (int ix = 0; ix < w->gw; ix++) {
            float x = w->gx0 + (float)ix * GF_CELL;
            float nz = gf_noise(x, y, 70.0f, seed) * 1.0f +
                       gf_noise(x, y, 31.0f, seed + 1) * 0.45f +
                       gf_noise(x, y, 13.0f, seed + 2) * 0.18f;
            int idx = iy * w->gw + ix;
            float hh = base + nz * amp * (float)calm[idx] * (1.0f / 255.0f);
            /* ground rises gently away from the playing corridor, so the
             * hole sits in a valley and the 3D view has banks to see */
            float out = 0.0f;
            if (x < d->x0 + 10) out += (float)(d->x0 + 10) - x;
            if (x > d->x1 - 10) out += x - (float)(d->x1 - 10);
            hh += out * 0.12f;
            HS(w, idx, hh);
        }
    }

    for (int i = 0; i < d->nmounds; i++) {
        const gf_mound_t *m = &d->mounds[i];
        float r = (float)m->r, hh = (float)m->h * 0.01f;
        int ix0 = (int)((m->x - r * 2 - w->gx0) / GF_CELL), ix1 = (int)((m->x + r * 2 - w->gx0) / GF_CELL);
        int iy0 = (int)((m->y - r * 2 - w->gy0) / GF_CELL), iy1 = (int)((m->y + r * 2 - w->gy0) / GF_CELL);
        for (int iy = iy0 < 0 ? 0 : iy0; iy <= iy1 && iy < w->gh; iy++) {
            for (int ix = ix0 < 0 ? 0 : ix0; ix <= ix1 && ix < w->gw; ix++) {
                float x = w->gx0 + (float)ix * GF_CELL - m->x;
                float y = w->gy0 + (float)iy * GF_CELL - m->y;
                int i2 = iy * w->gw + ix;
                HS(w, i2, HG(w, i2) + hh * expf(-(x * x + y * y) / (r * r * 0.5f)));
            }
        }
    }

    /* tees: flat tops a little above the ground */
    for (int i = 0; i < w->npoly; i++) {
        gf_poly_t *p = &w->poly[i];
        if (p->kind != SH_TEE) {
            continue;
        }
        float cx = (p->x0 + p->x1) * 0.5f, cy = (p->y0 + p->y1) * 0.5f;
        int ix = (int)((cx - w->gx0) / GF_CELL), iy = (int)((cy - w->gy0) / GF_CELL);
        float top = HG(w, iy * w->gw + ix) + 0.45f;
        for_cells(w, p, 3.0f, fn_tee, &top);
    }

    /* greens: tilted planes */
    for (int i = 0; i < w->npoly; i++) {
        gf_poly_t *p = &w->poly[i];
        if (p->kind != SH_GREEN) {
            continue;
        }
        green_arg_t g;
        g.cx = (p->x0 + p->x1) * 0.5f;
        g.cy = (p->y0 + p->y1) * 0.5f;
        int ix = (int)((g.cx - w->gx0) / GF_CELL), iy = (int)((g.cy - w->gy0) / GF_CELL);
        g.h0 = HG(w, iy * w->gw + ix);
        g.tx = (float)d->green_tx * 0.001f;
        g.ty = (float)d->green_ty * 0.001f;
        g.bump = (float)d->green_bump * 0.01f;
        g.rise_edge = 0.35f;
        for_cells(w, p, 7.0f, fn_green, &g);
    }

    for (int i = 0; i < w->npoly; i++) {
        gf_poly_t *p = &w->poly[i];
        if (p->kind == SH_BUNKER) {
            for_cells(w, p, 2.5f, fn_bunker, NULL);
        } else if (p->kind == SH_WASTE) {
            for_cells(w, p, 1.0f, fn_waste, NULL);
        }
    }

    /* water: one level for the hole, a little under the lowest bank */
    bool any_water = false;
    water_arg_t wa = { 1e9f };
    for (int i = 0; i < w->npoly; i++) {
        if (GF_IS_WATER(w->poly[i].kind)) {
            for_cells(w, &w->poly[i], 8.0f, fn_water_min, &wa);
            any_water = true;
        }
    }
    w->water_level = any_water ? wa.min - 0.6f : -100.0f;
    for (int i = 0; i < w->npoly; i++) {
        if (GF_IS_WATER(w->poly[i].kind)) {
            for_cells(w, &w->poly[i], 5.0f, fn_water, NULL);
        }
    }

    /* Nothing outside the water may lie below it: the 3D view calls any
     * ground under the water level water, and a hollow in the dunes showed
     * up as a grey pond. The mask borrows the normals' buffer, which is
     * computed after this. */
    if (any_water) {
        uint8_t *mask = (uint8_t *)w->nrm;
        memset(mask, 0, (size_t)w->gw * w->gh);
        for (int i = 0; i < w->npoly; i++) {
            const gf_poly_t *p = &w->poly[i];
            if (!GF_IS_WATER(p->kind)) continue;
            /* inside only (no distance): the sea's box is big */
            int ix0 = (int)((p->x0 - w->gx0) / GF_CELL), ix1 = (int)((p->x1 - w->gx0) / GF_CELL) + 1;
            int iy0 = (int)((p->y0 - w->gy0) / GF_CELL), iy1 = (int)((p->y1 - w->gy0) / GF_CELL) + 1;
            if (ix0 < 0) ix0 = 0;
            if (iy0 < 0) iy0 = 0;
            if (ix1 >= w->gw) ix1 = w->gw - 1;
            if (iy1 >= w->gh) iy1 = w->gh - 1;
            for (int iy = iy0; iy <= iy1; iy++) {
                if ((iy & 15) == 0) gf_yield();
                for (int ix = ix0; ix <= ix1; ix++) {
                    if (gf_poly_inside(w, p, w->gx0 + (float)ix * GF_CELL, w->gy0 + (float)iy * GF_CELL)) {
                        mask[iy * w->gw + ix] = 1;
                    }
                }
            }
        }
        float floor_h = w->water_level + 0.1f;
        for (int i = 0; i < w->gw * w->gh; i++) {
            if (!mask[i] && HG(w, i) < floor_h) HS(w, i, floor_h);
        }
    }
}

/* --------------------------------------------------------------------------
 * Loading
 * -------------------------------------------------------------------------- */

void gf_world_load(gf_world_t *w, const gf_hole_t *d, int tee_i, int pin_i)
{
    w->def   = d;
    w->gen++;
    w->tee_i = tee_i;
    w->pin_i = pin_i;
    w->tee_x = d->tee[tee_i].x;
    w->tee_y = d->tee[tee_i].y;
    w->pin_x = d->pin[pin_i].x;
    w->pin_y = d->pin[pin_i].y;
    w->length = sqrtf((w->pin_x - w->tee_x) * (w->pin_x - w->tee_x) +
                      (w->pin_y - w->tee_y) * (w->pin_y - w->tee_y));

    w->gx0 = (float)d->x0 - MARGIN;
    w->gy0 = (float)d->y0 - MARGIN;
    gf_world_grid_size(d, &w->gw, &w->gh);
    if (w->gw * w->gh > w->cells_max) {
        /* a hole bigger than the buffers: keep its width, crop its far end */
        w->gh = w->cells_max / w->gw;
    }

    /* polygons */
    w->npts  = 0;
    w->npoly = 0;
    for (int i = 0; i < d->nshapes && w->npoly < GF_MAX_POLYS; i++) {
        const gf_shape_t *s = &d->shapes[i];
        gf_poly_t *p = &w->poly[w->npoly];
        p->kind  = (s->kind == SH_WATER && (s->param & 1)) ? GF_POLY_LAKE : s->kind;
        p->src   = (uint8_t)i;
        p->first = (uint16_t)w->npts;
        int sub  = s->kind == SH_GREEN || s->kind == SH_BUNKER ? 8 : 6;
        p->n     = (uint16_t)add_spline(w, s->p, s->n, s->kind != SH_PATH, sub);
        p->param = s->kind == SH_BUNKER ? (s->param ? (float)s->param * 0.1f : 0.8f)
                                        : (float)s->param;
        poly_bbox(w, p);
        w->npoly++;
    }
    /* derived: fringe around greens, first cut around fairways. They go
     * BEFORE their source in the painting order, so they are inserted as
     * extra polygons and the painter sorts by kind. */
    int base = w->npoly;
    for (int i = 0; i < base && w->npoly < GF_MAX_POLYS; i++) {
        gf_poly_t *s = &w->poly[i];
        float off;
        int kind;
        if (s->kind == SH_GREEN) {
            off = 1.6f;
            kind = GF_POLY_FRINGE;
        } else if (s->kind == SH_FAIRWAY) {
            off = 1.8f;
            kind = GF_POLY_FIRST;
        } else {
            continue;
        }
        int first = add_offset(w, s, off);
        if (first < 0) {
            break;
        }
        gf_poly_t *p = &w->poly[w->npoly++];
        *p = *s;
        p->kind  = (uint8_t)kind;
        p->first = (uint16_t)first;
        poly_bbox(w, p);
    }

    /* 1 / length^2 of every edge: distances to an outline without a divide */
    for (int i = 0; i < w->npoly; i++) {
        const gf_poly_t *p = &w->poly[i];
        const float *v = w->pts + 2 * p->first;
        for (int k = 0; k < p->n; k++) {
            int j = k + 1 < p->n ? k + 1 : 0;
            float dx = v[2 * j] - v[2 * k], dy = v[2 * j + 1] - v[2 * k + 1];
            float l2 = dx * dx + dy * dy;
            w->inv_l2[p->first + k] = l2 > 1e-8f ? 1.0f / l2 : 0.0f;
        }
    }

    for (int i = 0; i < w->npoly; i++) {
        if (w->poly[i].kind == SH_GREEN) {
            w->green_cx = (w->poly[i].x0 + w->poly[i].x1) * 0.5f;
            w->green_cy = (w->poly[i].y0 + w->poly[i].y1) * 0.5f;
            break;
        }
    }

    build_heights(w);

    /* normals: slope per cell in 1/64 (clamped to +-2 = 63%) */
    for (int iy = 0; iy < w->gh; iy++) {
        if ((iy & 31) == 0) gf_yield();
        for (int ix = 0; ix < w->gw; ix++) {
            float dx = (hcell(w, ix + 1, iy) - hcell(w, ix - 1, iy)) * 0.5f;
            float dy = (hcell(w, ix, iy + 1) - hcell(w, ix, iy - 1)) * 0.5f;
            int a = (int)(long)roundf(dx * 64.0f), b = (int)(long)roundf(dy * 64.0f);
            if (a > 127) a = 127;
            if (a < -127) a = -127;
            if (b > 127) b = 127;
            if (b < -127) b = -127;
            w->nrm[2 * (iy * w->gw + ix)]     = (int8_t)a;
            w->nrm[2 * (iy * w->gw + ix) + 1] = (int8_t)b;
        }
    }

    /* trees: the single ones, then the forests */
    w->ntrees = 0;
    for (int i = 0; i < d->ntrees; i++) {
        const gf_tree_def_t *t = &d->trees[i];
        add_tree(w, t->x, t->y, t->kind, t->size ? t->size : 100, gf_hash((uint32_t)i * 977U + d->seed));
    }
    for (int i = 0; i < w->npoly; i++) {
        gf_poly_t *p = &w->poly[i];
        if (p->kind == SH_FOREST) {
            const gf_shape_t *s = &d->shapes[p->src];
            fill_forest(w, p, s->param ? s->param : 5, s->tree, d->seed * 31U + (uint32_t)i);
        }
    }
    for (int i = 0; i < w->ntrees; i++) {
        w->trees[i].h = gf_height(w, w->trees[i].x, w->trees[i].y);
    }
    index_trees(w);
}
