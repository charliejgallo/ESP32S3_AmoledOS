/*
 * GOLF - the course seen from above (see gf_map.h)
 *
 * The picture is built in strips of STRIP rows: a strip of 8-bit colour and
 * one of coverage, so memory stays small whatever the size asked for (the
 * 3D view's texture is ~640 x 1300).
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_map.h"
#include "gf_gfx.h"
#include "gf_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STRIP   16

/* --------------------------------------------------------------------------
 * View
 * -------------------------------------------------------------------------- */

void gf_view_set(gf_view_t *v, float ox, float oy, float scx, float scy, float ppm, float ang)
{
    v->ox = ox;
    v->oy = oy;
    v->scx = scx;
    v->scy = scy;
    v->ppm = ppm;
    v->ang = ang;
    v->fx = sinf(ang);
    v->fy = cosf(ang);
    v->rx = cosf(ang);
    v->ry = -sinf(ang);
}

void gf_view_w2s(const gf_view_t *v, float x, float y, float *sx, float *sy)
{
    float dx = x - v->ox, dy = y - v->oy;
    *sx = v->scx + (dx * v->rx + dy * v->ry) * v->ppm;
    *sy = v->scy - (dx * v->fx + dy * v->fy) * v->ppm;
}

void gf_view_s2w(const gf_view_t *v, float sx, float sy, float *x, float *y)
{
    float a = (sx - v->scx) / v->ppm;       /* along right */
    float b = -(sy - v->scy) / v->ppm;      /* along up    */
    *x = v->ox + a * v->rx + b * v->fx;
    *y = v->oy + a * v->ry + b * v->fy;
}

/* --------------------------------------------------------------------------
 * The noise tile: 128 x 128, tileable, three octaves. Sampling it bilinearly
 * costs a tenth of computing value noise, and it is what every texture uses.
 * -------------------------------------------------------------------------- */

#define TEX_N   128
static int8_t s_tex[TEX_N * TEX_N];
static bool   s_tex_ready;

static float per_lattice(int ix, int iy, int per, uint32_t seed)
{
    ix = ((ix % per) + per) % per;
    iy = ((iy % per) + per) % per;
    uint32_t h = gf_hash((uint32_t)ix * 0x8da6b343U ^ (uint32_t)iy * 0xd8163841U ^ seed);
    return (float)(h & 0xFFFF) * (2.0f / 65535.0f) - 1.0f;
}

static float per_noise(float x, float y, int per, uint32_t seed)
{
    int ix = (int)gf_floorf(x), iy = (int)gf_floorf(y);
    float tx = x - (float)ix, ty = y - (float)iy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    float a = per_lattice(ix, iy, per, seed), b = per_lattice(ix + 1, iy, per, seed);
    float c = per_lattice(ix, iy + 1, per, seed), d = per_lattice(ix + 1, iy + 1, per, seed);
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

void gf_tex_init(void)
{
    if (s_tex_ready) {
        return;
    }
    for (int y = 0; y < TEX_N; y++) {
        for (int x = 0; x < TEX_N; x++) {
            float v = per_noise(x / 16.0f, y / 16.0f, TEX_N / 16, 11) * 0.55f +
                      per_noise(x / 8.0f, y / 8.0f, TEX_N / 8, 23) * 0.30f +
                      per_noise(x / 4.0f, y / 4.0f, TEX_N / 4, 37) * 0.15f;
            int q = (int)(long)roundf(v * 127.0f * 1.4f);
            if (q > 127) q = 127;
            if (q < -127) q = -127;
            s_tex[y * TEX_N + x] = (int8_t)q;
        }
    }
    s_tex_ready = true;
}

/* one unit of (x, y) = one texel; the tile repeats every 128 */
float gf_tex(float x, float y)
{
    float flx = gf_floorf(x), fly = gf_floorf(y);
    int ix = (int)flx & (TEX_N - 1), iy = (int)fly & (TEX_N - 1);
    int jx = (ix + 1) & (TEX_N - 1), jy = (iy + 1) & (TEX_N - 1);
    float tx = x - flx, ty = y - fly;
    float a = s_tex[iy * TEX_N + ix], b = s_tex[iy * TEX_N + jx];
    float c = s_tex[jy * TEX_N + ix], d = s_tex[jy * TEX_N + jx];
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return (ab + (cd - ab) * ty) * (1.0f / 127.0f);
}

/* --------------------------------------------------------------------------
 * Tree art
 * -------------------------------------------------------------------------- */

static const gf_tree_art_t *s_tree_art;

void gf_map_set_tree_art(const gf_tree_art_t *art)
{
    s_tree_art = art;
}

/* --------------------------------------------------------------------------
 * Textures: colour of each surface at a world point
 * -------------------------------------------------------------------------- */

typedef struct {
    const gf_world_t *w;
    float ws;               /* metres per pixel: for antialiased stripes     */
    int   px, py;           /* pixel, for the fine grain                     */
} tctx_t;

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float grain(int x, int y)
{
    uint32_t h = gf_hash((uint32_t)x * 1664525U ^ (uint32_t)y * 22695477U);
    return (float)(h & 255) * (1.0f / 128.0f) - 1.0f;
}

/* alternating bands of 'width' metres along coordinate u, antialiased for a
 * pixel 'ws' metres wide: returns -1..1 */
static float bands(float u, float width, float ws)
{
    float t = u / (2.0f * width);
    t -= gf_floorf(t);
    float tri = fabsf(t * 2.0f - 1.0f);         /* 0..1..0 over two bands */
    float e = ws / (2.0f * width) + 0.02f;
    float s = clampf((tri - 0.5f) / e + 0.5f, 0.0f, 1.0f);
    return s * 2.0f - 1.0f;
}

static void rgb_mul(float *c, float k)
{
    c[0] *= k;
    c[1] *= k;
    c[2] *= k;
}

static void tex_base(int kind, const tctx_t *t, float x, float y, float *c)
{
    float n1, n2, b;
    switch (kind) {
    default:
    case -1: /* rough */
        n1 = gf_tex(x * 0.09f, y * 0.09f);
        n2 = gf_tex(x * 0.33f + 40.0f, y * 0.33f + 17.0f);
        c[0] = 62; c[1] = 124; c[2] = 44;
        {
            /* dry patches: towards olive */
            float dry = clampf(gf_tex(x * 0.02f + 70.0f, y * 0.02f + 9.0f) * 1.6f, 0.0f, 1.0f);
            c[0] += 22 * dry; c[1] += 6 * dry; c[2] += 2 * dry;
        }
        rgb_mul(c, 1.0f + 0.10f * n1 + 0.07f * n2 + 0.035f * grain(t->px, t->py));
        break;
    case SH_FOREST:
        n1 = gf_tex(x * 0.15f + 11.0f, y * 0.15f);
        n2 = gf_tex(x * 0.6f, y * 0.6f + 33.0f);
        c[0] = 48; c[1] = 90; c[2] = 38;
        if (n2 > 0.45f) {                       /* pine needles, dead leaves */
            c[0] = 92; c[1] = 88; c[2] = 52;
        }
        rgb_mul(c, 1.0f + 0.14f * n1 + 0.05f * grain(t->px, t->py));
        break;
    case SH_DEEP:
        n1 = gf_tex(x * 0.2f, y * 0.2f + 5.0f);
        n2 = gf_tex(x * 0.7f + 3.0f, y * 0.7f);
        c[0] = 88; c[1] = 118; c[2] = 46;
        rgb_mul(c, 1.0f + 0.14f * n1 + 0.10f * n2 + 0.05f * grain(t->px, t->py));
        break;
    case SH_WASTE:
        n1 = gf_tex(x * 0.25f, y * 0.25f);
        c[0] = 204; c[1] = 186; c[2] = 140;
        {
            /* scrub: dark tufts on a 1.3 m lattice */
            float gx = x / 1.3f, gy = y / 1.3f;
            float fx = gx - gf_floorf(gx) - 0.5f, fy = gy - gf_floorf(gy) - 0.5f;
            uint32_t h = gf_hash((uint32_t)(int)gf_floorf(gx) * 7919U ^ (uint32_t)(int)gf_floorf(gy) * 104729U);
            float r = 0.12f + (float)(h & 63) / 300.0f;
            if ((h >> 8) % 3 == 0 && fx * fx + fy * fy < r * r) {
                c[0] = 78; c[1] = 102; c[2] = 48;
            }
        }
        rgb_mul(c, 1.0f + 0.07f * n1 + 0.06f * grain(t->px, t->py));
        break;
    case GF_POLY_FIRST:
        n1 = gf_tex(x * 0.12f, y * 0.12f);
        c[0] = 72; c[1] = 146; c[2] = 52;
        rgb_mul(c, 1.0f + 0.06f * n1 + 0.025f * grain(t->px, t->py));
        break;
    case SH_FAIRWAY:
        n1 = gf_tex(x * 0.08f + 9.0f, y * 0.08f);
        b = bands(y, 9.0f, t->ws) * 0.065f + bands(x, 9.0f, t->ws) * 0.025f;
        c[0] = 84; c[1] = 164; c[2] = 58;
        rgb_mul(c, 1.0f + b + 0.04f * n1 + 0.02f * grain(t->px, t->py));
        break;
    case SH_TEE:
        b = bands(x, 1.6f, t->ws) * 0.05f;
        c[0] = 92; c[1] = 178; c[2] = 66;
        rgb_mul(c, 1.0f + b + 0.02f * grain(t->px, t->py));
        break;
    case GF_POLY_FRINGE:
        n1 = gf_tex(x * 0.2f, y * 0.2f);
        c[0] = 82; c[1] = 168; c[2] = 60;
        rgb_mul(c, 1.0f + 0.04f * n1 + 0.02f * grain(t->px, t->py));
        break;
    case SH_GREEN:
        n1 = gf_tex(x * 0.25f + 50.0f, y * 0.25f);
        b = bands(x + y * 0.35f, 3.2f, t->ws) * 0.045f;
        c[0] = 98; c[1] = 190; c[2] = 72;
        rgb_mul(c, 1.0f + b + 0.03f * n1 + 0.012f * grain(t->px, t->py));
        break;
    case SH_BUNKER:
        n1 = gf_tex(x * 0.4f, y * 0.4f + 21.0f);
        c[0] = 234; c[1] = 216; c[2] = 168;
        {
            /* rake marks: faint wavy lines */
            float r = sinf((x * 0.8f + y * 2.6f + gf_tex(x * 0.3f, y * 0.3f) * 2.0f) * 2.2f);
            rgb_mul(c, 1.0f + 0.025f * r);
        }
        rgb_mul(c, 1.0f + 0.04f * n1 + 0.06f * grain(t->px, t->py));
        break;
    case SH_WATER: {
        float d = t->w->water_level - gf_height(t->w, x, y);
        float k = clampf(d / 1.4f, 0.0f, 1.0f);
        k = sqrtf(k);
        c[0] = 70 + (22 - 70) * k;
        c[1] = 156 + (80 - 156) * k;
        c[2] = 166 + (126 - 166) * k;
        n1 = gf_tex(x * 0.18f + 7.0f, y * 0.45f);
        n2 = gf_tex(x * 0.5f, y * 1.1f + 60.0f);
        float hl = clampf((n1 * 0.6f + n2 * 0.5f - 0.35f) * 3.0f, 0.0f, 1.0f);
        c[0] += hl * 60; c[1] += hl * 60; c[2] += hl * 55;
        rgb_mul(c, 1.0f + 0.05f * n1);
        break;
    }
    case SH_PATH:
        n1 = gf_tex(x * 0.5f, y * 0.5f);
        c[0] = 196; c[1] = 188; c[2] = 168;
        rgb_mul(c, 1.0f + 0.05f * n1 + 0.05f * grain(t->px, t->py));
        break;
    }
}


/* The coast and the lakes repaint a few surfaces over the woods' look */
static void tex_color(int kind, const tctx_t *t, float x, float y, float *c)
{
    if (kind == GF_POLY_LAKE) kind = SH_WATER;
    int theme = GF_THEME(t->w->def);
    if (theme == THEME_WOODS) {
        tex_base(kind, t, x, y, c);
        return;
    }
    float n1, n2;
    if (theme == THEME_COAST) {
        switch (kind) {
        case -1: {  /* links rough: thinner, straw among the green */
            n1 = gf_tex(x * 0.09f, y * 0.09f);
            n2 = gf_tex(x * 0.4f + 40.0f, y * 0.4f + 17.0f);
            float dry = clampf(gf_tex(x * 0.03f + 70.0f, y * 0.03f + 9.0f) * 1.2f + 0.35f, 0.0f, 1.0f);
            c[0] = 84 + 52 * dry; c[1] = 128 + 18 * dry; c[2] = 54 + 14 * dry;
            rgb_mul(c, 1.0f + 0.10f * n1 + 0.08f * n2 + 0.04f * grain(t->px, t->py));
            return;
        }
        case SH_DEEP: {  /* marram grass on the dunes: beige with green tufts */
            n1 = gf_tex(x * 0.25f, y * 0.25f + 5.0f);
            n2 = gf_tex(x * 1.1f + 3.0f, y * 0.9f);
            c[0] = 184; c[1] = 170; c[2] = 118;
            if (n2 > 0.15f) { c[0] = 122; c[1] = 142; c[2] = 74; }
            rgb_mul(c, 1.0f + 0.12f * n1 + 0.06f * grain(t->px, t->py));
            return;
        }
        case SH_WASTE: {  /* the beach: pale sand, wet and darker by the sea */
            n1 = gf_tex(x * 0.3f, y * 0.3f);
            c[0] = 238; c[1] = 224; c[2] = 184;
            uint32_t h = gf_hash((uint32_t)(int)gf_floorf(x * 2.0f) * 7919U ^ (uint32_t)(int)gf_floorf(y * 2.0f) * 104729U);
            if ((h & 63) == 0) { c[0] = 250; c[1] = 246; c[2] = 236; }     /* shells */
            rgb_mul(c, 1.0f + 0.05f * n1 + 0.05f * grain(t->px, t->py));
            return;
        }
        case SH_BUNKER:
            tex_base(kind, t, x, y, c);
            c[0] *= 1.02f; c[1] *= 1.04f; c[2] *= 1.10f;       /* whiter sand */
            return;
        case SH_FAIRWAY:
            tex_base(kind, t, x, y, c);
            c[0] *= 1.10f; c[1] *= 1.00f; c[2] *= 0.95f;       /* firm, yellower */
            return;
        case SH_WATER: {  /* the sea: deep blue, green-blue on the sand, foam at the edge */
            float d = t->w->water_level - gf_height(t->w, x, y);
            float k = sqrtf(clampf(d / 1.6f, 0.0f, 1.0f));
            c[0] = 58 + (14 - 58) * k;
            c[1] = 170 + (66 - 170) * k;
            c[2] = 176 + (122 - 176) * k;
            n1 = gf_tex(x * 0.12f + 7.0f, y * 0.35f);
            n2 = gf_tex(x * 0.45f, y * 1.3f + 60.0f);
            float wave = clampf((n1 * 0.7f + n2 * 0.4f - 0.30f) * 3.0f, 0.0f, 1.0f);
            c[0] += wave * 50; c[1] += wave * 55; c[2] += wave * 50;
            if (d < 0.35f) {    /* the surf */
                float f = clampf(1.0f - d / 0.35f, 0.0f, 1.0f) * (0.6f + 0.4f * n2);
                c[0] += (245 - c[0]) * f; c[1] += (250 - c[1]) * f; c[2] += (250 - c[2]) * f;
            }
            return;
        }
        default:
            tex_base(kind, t, x, y, c);
            return;
        }
    }
    /* THEME_LAKES */
    switch (kind) {
    case -1:
        tex_base(kind, t, x, y, c);
        c[0] *= 0.92f; c[1] *= 1.05f; c[2] *= 0.95f;           /* lusher */
        return;
    case SH_WATER: {  /* park ponds: greener, with water lilies */
        float d = t->w->water_level - gf_height(t->w, x, y);
        float k = sqrtf(clampf(d / 1.3f, 0.0f, 1.0f));
        c[0] = 78 + (26 - 78) * k;
        c[1] = 150 + (86 - 150) * k;
        c[2] = 130 + (96 - 130) * k;
        n1 = gf_tex(x * 0.18f + 7.0f, y * 0.45f);
        float hl = clampf((n1 - 0.35f) * 3.0f, 0.0f, 1.0f);
        c[0] += hl * 40; c[1] += hl * 45; c[2] += hl * 45;
        /* lilies: pads on a 2.5 m lattice, near the banks */
        float gx = x / 2.5f, gy = y / 2.5f;
        float fx = gx - gf_floorf(gx) - 0.5f, fy = gy - gf_floorf(gy) - 0.5f;
        uint32_t h = gf_hash((uint32_t)(int)gf_floorf(gx) * 7919U ^ (uint32_t)(int)gf_floorf(gy) * 104729U);
        float r = 0.18f + (float)(h & 31) / 250.0f;
        if (d < 0.9f && (h >> 8) % 4 == 0 && fx * fx + fy * fy < r * r) {
            c[0] = 60; c[1] = 128; c[2] = 52;
            if (((h >> 12) & 7) == 0 && fx * fx + fy * fy < r * r * 0.15f) { c[0] = 240; c[1] = 190; c[2] = 220; }
        }
        return;
    }
    default:
        tex_base(kind, t, x, y, c);
        return;
    }
}

/* --------------------------------------------------------------------------
 * Coverage rasterisation, one strip at a time
 * -------------------------------------------------------------------------- */

/* the polygons in screen space, filled per render */
static float s_spts[GF_MAX_PTS * 2];
typedef struct { float x0, y0, x1, y1; } sbox_t;
static sbox_t s_sbox[GF_MAX_POLYS];

#define SUB 4           /* sub-scanlines per row */

static void cover_poly(const gf_poly_t *p, int w, int sy0, int rows, uint8_t *cov, int *minx, int *maxx)
{
    const float *v = s_spts + 2 * p->first;
    int n = p->n;
    float xs[64];

    for (int r = 0; r < rows; r++) {
        uint8_t *row = cov + r * w;
        for (int s = 0; s < SUB; s++) {
            float y = (float)(sy0 + r) + ((float)s + 0.5f) / SUB;
            int nx = 0;
            for (int i = 0, j = n - 1; i < n; j = i++) {
                float yi = v[2 * i + 1], yj = v[2 * j + 1];
                if ((yi > y) != (yj > y)) {
                    float xi = v[2 * i], xj = v[2 * j];
                    if (nx < 64) {
                        xs[nx++] = xj + (y - yj) * (xi - xj) / (yi - yj);
                    }
                }
            }
            /* insertion sort: a handful of crossings */
            for (int i = 1; i < nx; i++) {
                float k = xs[i];
                int j = i - 1;
                while (j >= 0 && xs[j] > k) {
                    xs[j + 1] = xs[j];
                    j--;
                }
                xs[j + 1] = k;
            }
            for (int i = 0; i + 1 < nx; i += 2) {
                float xa = xs[i], xb = xs[i + 1];
                if (xb <= 0.0f || xa >= (float)w) continue;
                if (xa < 0.0f) xa = 0.0f;
                if (xb > (float)w) xb = (float)w;
                int ia = (int)xa, ib = (int)xb;
                if (ib >= w) ib = w - 1;
                const int unit = 256 / SUB;     /* 64 */
                if (ia == ib) {
                    int add = (int)((xb - xa) * unit);
                    int vv = row[ia] + add;
                    row[ia] = (uint8_t)(vv > 255 ? 255 : vv);
                } else {
                    int add = (int)(((float)(ia + 1) - xa) * unit);
                    int vv = row[ia] + add;
                    row[ia] = (uint8_t)(vv > 255 ? 255 : vv);
                    for (int x = ia + 1; x < ib; x++) {
                        vv = row[x] + unit;
                        row[x] = (uint8_t)(vv > 255 ? 255 : vv);
                    }
                    add = (int)((xb - (float)ib) * unit);
                    vv = row[ib] + add;
                    row[ib] = (uint8_t)(vv > 255 ? 255 : vv);
                }
                if (ia < *minx) *minx = ia;
                if (ib > *maxx) *maxx = ib;
            }
        }
    }
}

/* open polyline of half-width hw pixels */
static void cover_path(const gf_poly_t *p, int w, int sy0, int rows, float hw, uint8_t *cov, int *minx, int *maxx)
{
    const float *v = s_spts + 2 * p->first;
    int n = p->n;
    for (int r = 0; r < rows; r++) {
        float py = (float)(sy0 + r) + 0.5f;
        uint8_t *row = cov + r * w;
        for (int i = 0; i + 1 < n; i++) {
            float ax = v[2 * i], ay = v[2 * i + 1], bx = v[2 * i + 2], by = v[2 * i + 3];
            float lo = (ay < by ? ay : by) - hw - 1, hi = (ay > by ? ay : by) + hw + 1;
            if (py < lo || py > hi) continue;
            int xa = (int)gf_floorf((ax < bx ? ax : bx) - hw - 1), xb = (int)gf_ceilf((ax > bx ? ax : bx) + hw + 1);
            if (xa < 0) xa = 0;
            if (xb >= w) xb = w - 1;
            float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            for (int x = xa; x <= xb; x++) {
                float px = (float)x + 0.5f;
                float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                float qx = ax + t * dx - px, qy = ay + t * dy - py;
                float d = sqrtf(qx * qx + qy * qy);
                float c = hw - d + 0.5f;
                if (c <= 0) continue;
                int cv = c >= 1.0f ? 255 : (int)(c * 255.0f);
                if (cv > row[x]) row[x] = (uint8_t)cv;
                if (x < *minx) *minx = x;
                if (x > *maxx) *maxx = x;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * The renderer
 * -------------------------------------------------------------------------- */

static const uint8_t ORDER[] = {
    SH_FOREST, SH_DEEP, GF_POLY_LAKE, SH_WASTE, GF_POLY_FIRST, SH_FAIRWAY, SH_TEE,
    GF_POLY_FRINGE, SH_GREEN, SH_BUNKER, SH_WATER, SH_PATH,
};

/* the per-cell slope, interpolated: smooth light at any zoom */
static void slope_at(const gf_world_t *w, float x, float y, float *gx, float *gy)
{
    float fx = (x - w->gx0) / GF_CELL, fy = (y - w->gy0) / GF_CELL;
    float flx = gf_floorf(fx), fly = gf_floorf(fy);
    int ix = (int)flx, iy = (int)fly;
    float tx = fx - flx, ty = fy - fly;
    if (ix < 0) { ix = 0; tx = 0; }
    if (iy < 0) { iy = 0; ty = 0; }
    if (ix >= w->gw - 1) { ix = w->gw - 2; tx = 1; }
    if (iy >= w->gh - 1) { iy = w->gh - 2; ty = 1; }
    const int8_t *n00 = w->nrm + 2 * (iy * w->gw + ix);
    const int8_t *n10 = n00 + 2;
    const int8_t *n01 = n00 + 2 * w->gw;
    const int8_t *n11 = n01 + 2;
    float ax = n00[0] + (n10[0] - n00[0]) * tx, bx = n01[0] + (n11[0] - n01[0]) * tx;
    float ay = n00[1] + (n10[1] - n00[1]) * tx, by = n01[1] + (n11[1] - n01[1]) * tx;
    *gx = (ax + (bx - ax) * ty) * (1.0f / 64.0f);
    *gy = (ay + (by - ay) * ty) * (1.0f / 64.0f);
}

typedef struct {
    const gf_world_t *w;
    const gf_view_t  *v;
    int               W, H;
    unsigned          flags;
    bool              albedo;
    float             mpp;          /* albedo: metres per texel              */
} rctx_t;

static void pix_world(const rctx_t *rc, float sx, float sy, float *x, float *y)
{
    if (rc->albedo) {
        *x = rc->w->gx0 + sx * rc->mpp;
        *y = rc->w->gy0 + sy * rc->mpp;
    } else {
        gf_view_s2w(rc->v, sx, sy, x, y);
    }
}

static uint32_t s_prof_base, s_prof_layers, s_prof_light;

static void render_strips(const rctx_t *rc, uint16_t *out)
{
    s_prof_base = s_prof_layers = s_prof_light = 0;
    const gf_world_t *w = rc->w;
    int W = rc->W, H = rc->H;
    float *rgb   = (float *)gf_malloc((size_t)W * STRIP * 3 * sizeof(float));
    uint8_t *cov = (uint8_t *)gf_malloc((size_t)W * STRIP);
    uint8_t *wat = (uint8_t *)gf_malloc((size_t)W * STRIP);
    if (!rgb || !cov || !wat) {
        free(rgb);
        free(cov);
        free(wat);
        return;
    }

    /* the polygons to screen space */
    for (int i = 0; i < w->npoly; i++) {
        const gf_poly_t *p = &w->poly[i];
        float *o = s_spts + 2 * p->first;
        const float *v = w->pts + 2 * p->first;
        sbox_t *b = &s_sbox[i];
        b->x0 = b->y0 = 1e9f;
        b->x1 = b->y1 = -1e9f;
        for (int k = 0; k < p->n; k++) {
            float sx, sy;
            if (rc->albedo) {
                sx = (v[2 * k] - w->gx0) / rc->mpp;
                sy = (v[2 * k + 1] - w->gy0) / rc->mpp;
            } else {
                gf_view_w2s(rc->v, v[2 * k], v[2 * k + 1], &sx, &sy);
            }
            o[2 * k] = sx;
            o[2 * k + 1] = sy;
            if (sx < b->x0) b->x0 = sx;
            if (sx > b->x1) b->x1 = sx;
            if (sy < b->y0) b->y0 = sy;
            if (sy > b->y1) b->y1 = sy;
        }
    }

    float ppm = rc->albedo ? 1.0f / rc->mpp : rc->v->ppm;
    float path_hw = 1.2f * ppm;
    if (path_hw < 0.8f) path_hw = 0.8f;

    /* the light, in screen terms: from the upper left, 45 degrees up */
    const float lr = -0.7071f * 0.7071f, lu = 0.7071f * 0.7071f, lz = 0.7071f;
    const float exag = 2.6f;

    for (int sy0 = 0; sy0 < H; sy0 += STRIP) {
        int rows = H - sy0 < STRIP ? H - sy0 : STRIP;
        gf_yield();
        uint32_t ts = gf_clock();
        memset(wat, 0, (size_t)W * rows);

        /* base: rough */
        for (int r = 0; r < rows; r++) {
            float x, y, x1, y1;
            pix_world(rc, 0.5f, (float)(sy0 + r) + 0.5f, &x, &y);
            pix_world(rc, 1.5f, (float)(sy0 + r) + 0.5f, &x1, &y1);
            float dx = x1 - x, dy = y1 - y;
            tctx_t t = { w, 1.0f / ppm, 0, sy0 + r };
            float *c = rgb + (size_t)r * W * 3;
            for (int px = 0; px < W; px++, x += dx, y += dy, c += 3) {
                t.px = px;
                tex_color(-1, &t, x, y, c);
            }
        }

        s_prof_base += gf_clock() - ts;
        ts = gf_clock();
        /* the layers */
        for (unsigned li = 0; li < sizeof(ORDER); li++) {
            int kind = ORDER[li];
            for (int pi = 0; pi < w->npoly; pi++) {
                const gf_poly_t *p = &w->poly[pi];
                if (p->kind != kind) continue;
                const sbox_t *b = &s_sbox[pi];
                float pad = kind == SH_PATH ? path_hw + 1 : 1;
                if (b->y1 + pad < (float)sy0 || b->y0 - pad > (float)(sy0 + rows) ||
                    b->x1 + pad < 0 || b->x0 - pad > (float)W) {
                    continue;
                }
                int minx = W, maxx = -1;
                int cx0 = (int)gf_floorf(b->x0 - pad), cx1 = (int)gf_ceilf(b->x1 + pad);
                if (cx0 < 0) cx0 = 0;
                if (cx1 >= W) cx1 = W - 1;
                for (int r = 0; r < rows; r++) {
                    memset(cov + (size_t)r * W + cx0, 0, (size_t)(cx1 - cx0 + 1));
                }
                if (kind == SH_PATH) {
                    cover_path(p, W, sy0, rows, path_hw, cov, &minx, &maxx);
                } else {
                    cover_poly(p, W, sy0, rows, cov, &minx, &maxx);
                }
                if (maxx < minx) continue;
                if (minx < cx0) minx = cx0;
                if (maxx > cx1) maxx = cx1;

                for (int r = 0; r < rows; r++) {
                    uint8_t *cr = cov + (size_t)r * W;
                    float *c = rgb + (size_t)r * W * 3;
                    tctx_t t = { w, 1.0f / ppm, 0, sy0 + r };
                    for (int px = minx; px <= maxx; px++) {
                        int a = cr[px];
                        if (a == 0) continue;
                        float x, y, lc[3];
                        pix_world(rc, (float)px + 0.5f, (float)(sy0 + r) + 0.5f, &x, &y);
                        t.px = px;
                        tex_color(kind, &t, x, y, lc);
                        if (kind == SH_BUNKER) {
                            /* a dark line where the grass lip meets the sand */
                            float d = gf_poly_dist(w, p, x, y);
                            float e = clampf(1.0f - d / 0.45f, 0.0f, 1.0f);
                            if (e > 0) {
                                lc[0] += (120 - lc[0]) * e * 0.55f;
                                lc[1] += (112 - lc[1]) * e * 0.55f;
                                lc[2] += (80 - lc[2]) * e * 0.55f;
                            }
                        }
                        float f = a >= 255 ? 1.0f : (float)a / 255.0f;
                        float *o = c + px * 3;
                        o[0] += (lc[0] - o[0]) * f;
                        o[1] += (lc[1] - o[1]) * f;
                        o[2] += (lc[2] - o[2]) * f;
                        if (GF_IS_WATER(kind)) {
                            wat[r * W + px] = (uint8_t)a;
                        } else if (wat[r * W + px]) {
                            int wv = wat[r * W + px] - a;
                            wat[r * W + px] = (uint8_t)(wv < 0 ? 0 : wv);
                        }
                    }
                }
            }
        }

        s_prof_layers += gf_clock() - ts;
        ts = gf_clock();
        /* the light, and out */
        for (int r = 0; r < rows; r++) {
            const float *c = rgb + (size_t)r * W * 3;
            uint16_t *o = out + (size_t)(sy0 + r) * W;
            int py = sy0 + r;
            if (rc->albedo) {
                for (int px = 0; px < W; px++, c += 3) {
                    int R = (int)c[0], G = (int)c[1], B = (int)c[2];
                    if (R > 255) R = 255;
                    if (G > 255) G = 255;
                    if (B > 255) B = 255;
                    if (R < 0) R = 0;
                    if (G < 0) G = 0;
                    if (B < 0) B = 0;
                    o[px] = gf_rgb(R, G, B);
                }
                continue;
            }
            float x, y, x1, y1;
            pix_world(rc, 0.5f, (float)py + 0.5f, &x, &y);
            pix_world(rc, 1.5f, (float)py + 0.5f, &x1, &y1);
            float dx = x1 - x, dy = y1 - y;
            const gf_view_t *v = rc->v;
            for (int px = 0; px < W; px++, c += 3, x += dx, y += dy) {
                float k = 1.0f;
                if (rc->flags & MAP_SHADE) {
                    float gx, gy;
                    slope_at(w, x, y, &gx, &gy);
                    float gr = gx * v->rx + gy * v->ry;
                    float gu = gx * v->fx + gy * v->fy;
                    float nx = -gr * exag, nu = -gu * exag;
                    float s2 = nx * nx + nu * nu;
                    float inv = s2 < 0.6f ? gf_inv_len1(s2) : 1.0f / sqrtf(s2 + 1.0f);
                    float d = (nx * lr + nu * lu + lz) * inv * (1.0f / 0.7071f);   /* flat = 1 */
                    k = 0.30f + 0.70f * d;
                    float wf = wat[r * W + px] * (1.0f / 255.0f);
                    k += (1.0f - k) * wf;
                    k = clampf(k, 0.45f, 1.35f);
                }
                float R = c[0] * k, G = c[1] * k, B = c[2] * k;
                if (!gf_in_bounds(w, x, y) && !wat[r * W + px]) {
                    /* out of bounds: duller and darker, a line of white
                     * stakes along the edge */
                    float l = (R + G + B) * (1.0f / 3.0f);
                    R = (R * 0.55f + l * 0.45f) * 0.78f;
                    G = (G * 0.55f + l * 0.45f) * 0.78f;
                    B = (B * 0.55f + l * 0.45f) * 0.78f;
                }
                o[px] = gf_dither((int)R, (int)G, (int)B, px, py);
            }
        }
        s_prof_light += gf_clock() - ts;
    }
    free(rgb);
    free(cov);
    free(wat);
    gf_prof_set(2, s_prof_base);
    gf_prof_set(3, s_prof_layers);
    gf_prof_set(4, s_prof_light);
}

/* --------------------------------------------------------------------------
 * Trees seen from above
 * -------------------------------------------------------------------------- */

static const uint8_t TREE_RGB[GF_TREE_KINDS][3] = {
    { 34, 82, 50 },         /* pine: dark, bluish                            */
    { 58, 110, 40 },        /* oak                                           */
    { 84, 126, 46 },        /* poplar: lighter, yellower                     */
    { 70, 128, 58 },        /* palm                                          */
    { 64, 116, 44 },        /* bush                                          */
};

static void draw_canopy(gf_img_t *im, float cx, float cy, float r, int kind, int shade, uint32_t seed)
{
    int x0 = (int)gf_floorf(cx - r * 1.3f - 1), x1 = (int)gf_ceilf(cx + r * 1.3f + 1);
    int y0 = (int)gf_floorf(cy - r * 1.3f - 1), y1 = (int)gf_ceilf(cy + r * 1.3f + 1);
    if (x0 < im->cx0) x0 = im->cx0;
    if (y0 < im->cy0) y0 = im->cy0;
    if (x1 >= im->cx1) x1 = im->cx1 - 1;
    if (y1 >= im->cy1) y1 = im->cy1 - 1;
    float ph = (float)(seed & 255) / 40.0f;
    float tint = 0.9f + (float)shade / 60.0f;
    const uint8_t *base = TREE_RGB[kind];
    float so = (float)(seed >> 8 & 127);
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = im->px + (size_t)y * im->w;
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f - cx) / r, dy = ((float)y + 0.5f - cy) / r;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > 1.35f) continue;
            float ang = atan2f(dy, dx);
            float rr;
            switch (kind) {
            case GF_TREE_PINE:
                rr = 0.86f + 0.14f * cosf(ang * 9.0f + ph) + 0.05f * cosf(ang * 23.0f + ph * 2);
                break;
            case GF_TREE_PALM: {
                float f = cosf(ang * 7.0f + ph);
                rr = 0.35f + 0.75f * powf(clampf(f, 0.0f, 1.0f), 3.0f);
                break;
            }
            default:
                rr = 0.9f + 0.08f * cosf(ang * 5.0f + ph) + 0.06f * cosf(ang * 11.0f + ph * 3) +
                     0.05f * gf_tex(dx * 3 + so, dy * 3);
                break;
            }
            float cov = (rr - d) * r + 0.5f;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            float dn = d / (rr > 0.2f ? rr : 0.2f);
            if (dn > 1) dn = 1;
            float nz = sqrtf(1.0f - dn * dn * 0.85f);
            float light = (-dx * 0.55f - dy * 0.55f) / (d > 0.001f ? 1.0f : 1.0f) * 0.9f + nz * 0.6f;
            float clump = gf_tex(dx * 4.0f + so, dy * 4.0f + so) * 0.22f +
                          gf_tex(dx * 9.0f, dy * 9.0f + so) * 0.10f;
            float k = clampf(0.50f + 0.55f * light + clump, 0.30f, 1.40f) * tint;
            if (kind == GF_TREE_PINE) {
                /* a darker core between the whorls */
                k *= 0.85f + 0.2f * cosf(d * 14.0f);
            }
            int R = (int)(base[0] * k), G = (int)(base[1] * k), B = (int)(base[2] * k);
            if (R > 255) R = 255;
            if (G > 255) G = 255;
            if (B > 255) B = 255;
            uint16_t col = gf_dither(R, G, B, x, y);
            row[x] = gf_blend(row[x], col, (int)(cov * 255));
        }
    }
}

static int tree_cmp_y(const void *a, const void *b)
{
    float ya = ((const float *)a)[1], yb = ((const float *)b)[1];
    return ya < yb ? -1 : (ya > yb ? 1 : 0);
}

static void draw_trees(const gf_world_t *w, const gf_view_t *v, gf_img_t *im)
{
    /* visible trees, sorted top to bottom of the screen */
    static float list[GF_MAX_TREES][3];
    int n = 0;
    float ppm = v->ppm;
    for (int i = 0; i < w->ntrees; i++) {
        const gf_tree_t *t = &w->trees[i];
        float sx, sy;
        gf_view_w2s(v, t->x, t->y, &sx, &sy);
        float r = t->radius * ppm;
        float sh = t->height * ppm * 0.45f;
        if (sx + r + sh < 0 || sx - r > im->w || sy + r + sh < 0 || sy - r > im->h) continue;
        list[n][0] = sx;
        list[n][1] = sy;
        list[n][2] = (float)i;
        n++;
    }
    qsort(list, (size_t)n, sizeof(list[0]), tree_cmp_y);

    /* shadows first, all of them, towards the lower right */
    for (int k = 0; k < n; k++) {
        const gf_tree_t *t = &w->trees[(int)list[k][2]];
        float r = t->radius * ppm;
        float off = t->height * ppm * 0.32f;
        if (s_tree_art && gf_art_tree_top(s_tree_art, t->kind)) {
            gf_art_tree_shadow(s_tree_art, im, t->kind, list[k][0], list[k][1], ppm,
                               t->height / gf_art_tree_height(t->kind));
        } else {
            gf_shadow(im, (int)((list[k][0] + off) * 16), (int)((list[k][1] + off) * 16),
                      (int)(r * 1.15f * 16), (int)(r * 1.0f * 16), 120);
        }
    }
    for (int k = 0; k < n; k++) {
        const gf_tree_t *t = &w->trees[(int)list[k][2]];
        float r = t->radius * ppm;
        if (r < 0.8f) r = 0.8f;
        if (s_tree_art && gf_art_tree_top(s_tree_art, t->kind)) {
            gf_art_tree_draw_top(s_tree_art, im, t->kind, list[k][0], list[k][1], ppm,
                                 t->height / gf_art_tree_height(t->kind), 226 + t->shade * 4);
        } else {
            draw_canopy(im, list[k][0], list[k][1], r, t->kind, t->shade, gf_hash((uint32_t)(int)list[k][2] * 131U + 7));
        }
    }
}

/* --------------------------------------------------------------------------
 * Markers and the slope grid
 * -------------------------------------------------------------------------- */

static void draw_markers(const gf_world_t *w, const gf_view_t *v, gf_img_t *im)
{
    float sx, sy, ppm = v->ppm;

    /* tee markers: two balls across the tee */
    gf_view_w2s(v, w->tee_x - 3.0f, w->tee_y + 1.0f, &sx, &sy);
    float rr = 0.25f * ppm;
    if (rr < 1.4f) rr = 1.4f;
    static const uint32_t TEEC[3] = { 0x2A5BD7, 0xF2F2F2, 0xE0A020 };
    uint16_t tc = gf_hex(TEEC[w->tee_i % 3]);
    gf_disc(im, (int)(sx * 16), (int)(sy * 16), (int)(rr * 16), tc, 255);
    gf_view_w2s(v, w->tee_x + 3.0f, w->tee_y + 1.0f, &sx, &sy);
    gf_disc(im, (int)(sx * 16), (int)(sy * 16), (int)(rr * 16), tc, 255);

    /* the cup and the flag */
    gf_view_w2s(v, w->pin_x, w->pin_y, &sx, &sy);
    float cup = 0.054f * ppm;
    if (cup < 1.3f) cup = 1.3f;
    gf_disc(im, (int)(sx * 16), (int)(sy * 16), (int)(cup * 16), gf_rgb(24, 40, 22), 230);
    float pole = 2.2f * ppm;
    if (pole < 16) pole = 16;
    if (pole > 60) pole = 60;
    /* the pole's shadow, then the pole, then the flag */
    gf_line(im, (int)(sx * 16), (int)(sy * 16), (int)((sx + pole * 0.35f) * 16), (int)((sy + pole * 0.35f) * 16), 20, gf_rgb(20, 40, 16), 90);
    gf_line(im, (int)(sx * 16), (int)(sy * 16), (int)(sx * 16), (int)((sy - pole) * 16), 22, gf_rgb(245, 245, 240), 255);
    float fw = pole * 0.45f, fh = pole * 0.28f;
    for (int yy = 0; yy < (int)fh; yy++) {
        float frac = 1.0f - (float)yy / fh * 0.0f;
        int len = (int)(fw * (1.0f - fabsf((float)yy - fh / 2) / fh * 0.9f) * frac);
        for (int xx = 1; xx <= len; xx++) {
            int X = (int)sx + xx, Y = (int)(sy - pole) + yy;
            if (X < im->cx0 || Y < im->cy0 || X >= im->cx1 || Y >= im->cy1) continue;
            float wave = sinf((float)xx * 0.35f) * 0.12f;
            int R = (int)(230 * (0.9f + wave)), G = (int)(40 * (0.9f + wave)), B = (int)(36 * (0.9f + wave));
            im->px[Y * im->w + X] = gf_rgb(R > 255 ? 255 : R, G, B);
        }
    }
}

static void draw_slope_grid(const gf_world_t *w, const gf_view_t *v, gf_img_t *im)
{
    float step = 1.5f;              /* metres between chevrons */
    const gf_poly_t *green = NULL;
    for (int i = 0; i < w->npoly; i++) {
        if (w->poly[i].kind == SH_GREEN) {
            green = &w->poly[i];
            break;
        }
    }
    if (!green) return;
    for (float y = gf_floorf(green->y0 / step) * step; y <= green->y1; y += step) {
        for (float x = gf_floorf(green->x0 / step) * step; x <= green->x1; x += step) {
            if (!gf_poly_inside(w, green, x, y)) continue;
            float gx, gy;
            gf_normal(w, x, y, &gx, &gy);
            float s = sqrtf(gx * gx + gy * gy);
            if (s < 0.004f) continue;
            /* downhill = -gradient, in screen terms */
            float dxw = -gx / s, dyw = -gy / s;
            float sx, sy, ex, ey;
            gf_view_w2s(v, x, y, &sx, &sy);
            gf_view_w2s(v, x + dxw * 0.5f, y + dyw * 0.5f, &ex, &ey);
            float ux = ex - sx, uy = ey - sy;
            float l = sqrtf(ux * ux + uy * uy);
            if (l < 0.01f) continue;
            ux /= l;
            uy /= l;
            float len = 4.0f;
            int a = (int)clampf(s * 2400.0f, 50.0f, 200.0f);
            uint16_t col = gf_rgb(255, 255, 255);
            float tx = sx + ux * len * 0.5f, ty = sy + uy * len * 0.5f;
            float bx1 = tx - ux * len - uy * len * 0.6f, by1 = ty - uy * len + ux * len * 0.6f;
            float bx2 = tx - ux * len + uy * len * 0.6f, by2 = ty - uy * len - ux * len * 0.6f;
            gf_line(im, (int)(bx1 * 16), (int)(by1 * 16), (int)(tx * 16), (int)(ty * 16), 18, col, a);
            gf_line(im, (int)(bx2 * 16), (int)(by2 * 16), (int)(tx * 16), (int)(ty * 16), 18, col, a);
        }
    }
}

/* -------------------------------------------------------------------------- */

void gf_map_render(const gf_world_t *wd, const gf_view_t *v, uint16_t *out, int w, int h, unsigned flags)
{
    gf_tex_init();
    rctx_t rc = { wd, v, w, h, flags, false, 0.0f };
    uint32_t t0 = gf_clock();
    render_strips(&rc, out);
    uint32_t t1 = gf_clock();

    gf_img_t im;
    gf_img_init(&im, out, w, h);
    if (flags & MAP_GRID) {
        draw_slope_grid(wd, v, &im);
    }
    if (flags & MAP_TREES) {
        draw_trees(wd, v, &im);
    }
    uint32_t t2 = gf_clock();
    if (flags & MAP_MARKERS) {
        draw_markers(wd, v, &im);
    }
    gf_prof_set(0, t1 - t0);
    gf_prof_set(1, t2 - t1);
}

void gf_map_albedo(const gf_world_t *wd, uint16_t *out, int w, int h, float mpp)
{
    gf_tex_init();
    rctx_t rc = { wd, NULL, w, h, 0, true, mpp };
    render_strips(&rc, out);
}
