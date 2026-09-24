/*
 * VISOR 3D - the rasteriser (see v3_raster.h)
 *
 * The plainest thing that looks right: every vertex transformed once per
 * frame, every triangle filled scanline by scanline with its depth
 * interpolated along the span and tested against a 16-bit z-buffer, one
 * colour per triangle (flat shading, which is also what makes a low-poly
 * model read as faceted rather than broken).
 *
 * Both sides of a face are lit (|n.z|): STL files come with windings in
 * every state, and a model that vanishes because its triangles face "the
 * wrong way" is worse than paying to fill the back faces the z-buffer then
 * hides. The light is a headlight (from the camera) plus a little from
 * above, so the shape reads from any angle without a light to aim.
 */
#include "v3_raster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

#define CAM_D       3.0f                /* camera distance, in model radii */
#define FILL        0.72f               /* of half the smaller side, at scale 1 */
#define BASE_RGB    0xA8B8D0            /* the model when the file has no colour */

/* ceil(v - 0.5) = the first pixel centre at or after v, without a call into
 * libm (ceilf is a function on this core, and it ran twice per scanline of
 * every one of 12 000 triangles). Valid for |v| < 16384. */
static inline int px_start(float v)
{
    float x = v - 0.5f;
    int i = (int)x;                     /* truncates: ceil for x < 0 */
    return i + (x > (float)i);
}

static inline uint16_t be565(uint32_t r, uint32_t g, uint32_t b)
{
    uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

bool v3_scratch_alloc(v3_scratch_t *s, int nv)
{
    s->xyz = malloc((size_t)nv * 3 * sizeof(float));
    s->nv = nv;
    if (!s->xyz) {
        v3_scratch_free(s);
        return false;
    }
    return true;
}

void v3_scratch_free(v3_scratch_t *s)
{
    free(s->xyz);
    memset(s, 0, sizeof *s);
}

/* One triangle, filled. x, y in pixels; z in 0..65535 (0 = nearest). */
static void fill_tri(uint16_t *fb, uint16_t *zb, int w, int h,
                     float x0, float y0, float z0, float x1, float y1, float z1,
                     float x2, float y2, float z2, uint16_t c)
{
    /* sort by y: 0 top */
    if (y1 < y0) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; t = z0; z0 = z1; z1 = t; }
    if (y2 < y0) { float t; t = x0; x0 = x2; x2 = t; t = y0; y0 = y2; y2 = t; t = z0; z0 = z2; z2 = t; }
    if (y2 < y1) { float t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; t = z1; z1 = z2; z2 = t; }
    if (y2 - y0 < 1e-4f) return;

    int ys = px_start(y0), ye = px_start(y2);
    if (ys < 0) ys = 0;
    if (ye > h) ye = h;
    float inv02 = 1.0f / (y2 - y0);
    float inv01 = (y1 - y0) > 1e-4f ? 1.0f / (y1 - y0) : 0;
    float inv12 = (y2 - y1) > 1e-4f ? 1.0f / (y2 - y1) : 0;

    for (int y = ys; y < ye; y++) {
        float py = (float)y + 0.5f;
        float t = (py - y0) * inv02;
        float xa = x0 + (x2 - x0) * t, za = z0 + (z2 - z0) * t;
        float xb, zb2;
        if (py < y1) {
            float u = (py - y0) * inv01;
            xb = x0 + (x1 - x0) * u;
            zb2 = z0 + (z1 - z0) * u;
        } else {
            float u = (py - y1) * inv12;
            xb = x1 + (x2 - x1) * u;
            zb2 = z1 + (z2 - z1) * u;
        }
        if (xa > xb) { float q = xa; xa = xb; xb = q; q = za; za = zb2; zb2 = q; }
        int xs = px_start(xa), xe = px_start(xb);
        if (xs < 0) xs = 0;
        if (xe > w) xe = w;
        if (xs >= xe) continue;
        float dz = (xb - xa) > 1e-4f ? (zb2 - za) / (xb - xa) : 0;
        float z = za + ((float)xs + 0.5f - xa) * dz;
        uint16_t *pc = fb + (size_t)y * w;
        uint16_t *pz = zb + (size_t)y * w;
        for (int x = xs; x < xe; x++, z += dz) {
            int zi = (int)z;
            if (zi < 0) zi = 0;
            if (zi < pz[x]) {
                pz[x] = (uint16_t)zi;
                pc[x] = c;
            }
        }
    }
}

static void line(uint16_t *fb, int w, int h, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; guard++) {
        if ((unsigned)x0 < (unsigned)w && (unsigned)y0 < (unsigned)h) fb[y0 * w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Where a frame's time goes, in CPU cycles (the S3's cycle counter: nothing
 * finer than a millisecond is lent to the apps), summed until v3_prof()
 * collects it: clearing, transforming, rasterising. */
static uint32_t s_prof[3];

static inline uint32_t cycles(void)
{
#if defined(__XTENSA__)
    uint32_t c;
    __asm__ volatile("rsr.ccount %0" : "=a"(c));
    return c;
#else
    return 0;
#endif
}

void v3_prof(uint32_t out[3])
{
    memcpy(out, s_prof, sizeof s_prof);
    memset(s_prof, 0, sizeof s_prof);
}

int v3_render(const v3_mesh_t *m, const v3_view_t *view, v3_scratch_t *s,
              uint16_t *fb, uint16_t *zb, int w, int h)
{
    uint32_t c0 = cycles();
    size_t n = (size_t)w * h;
    uint16_t bg = be565((view->bg >> 16) & 255, (view->bg >> 8) & 255, view->bg & 255);
    for (size_t i = 0; i < n; i++) fb[i] = bg;
    if (view->mode == V3_SOLID) memset(zb, 0xFF, n * 2);
    uint32_t c1 = cycles();

    float cyw = cosf(view->yaw), syw = sinf(view->yaw);
    float cp = cosf(view->pitch), sp = sinf(view->pitch);
    float half = (float)(w < h ? w : h) * 0.5f;
    float f = FILL * half * CAM_D * view->scale;
    float cx = w * 0.5f + view->px, cy = h * 0.5f + view->py;

    /* vertices: rotate (yaw about Y, then pitch about X), project */
    for (int i = 0; i < m->nv; i++) {
        const float *v = &m->v[i * 3];
        float x1 = cyw * v[0] + syw * v[2];
        float z1 = -syw * v[0] + cyw * v[2];
        float y2 = cp * v[1] - sp * z1;
        float z2 = sp * v[1] + cp * z1;
        float d = CAM_D - z2;               /* distance from the camera */
        float k = f / d;
        float *o = &s->xyz[i * 3];
        o[0] = cx + x1 * k;
        o[1] = cy - y2 * k;
        o[2] = (d - (CAM_D - 1.0f)) * (65535.0f / 2.0f);
    }

    uint32_t c2 = cycles();
    int drawn = 0;
    uint32_t base = BASE_RGB;
    bool cull = m->closed && view->mode == V3_SOLID;
    for (int t = 0; t < m->nt; t++) {
        uint32_t a = m->idx[t * 3], b = m->idx[t * 3 + 1], d = m->idx[t * 3 + 2];
        const float *pa = &s->xyz[a * 3], *pb = &s->xyz[b * 3], *pd = &s->xyz[d * 3];
        float ax = pa[0], ay = pa[1], bx = pb[0], by = pb[1];
        float dx = pd[0], dy = pd[1];
        /* off screen entirely: skip */
        float minx = ax < bx ? ax : bx, maxx = ax > bx ? ax : bx;
        if (dx < minx) minx = dx;
        if (dx > maxx) maxx = dx;
        if (maxx < 0 || minx >= w) continue;
        float miny = ay < by ? ay : by, maxy = ay > by ? ay : by;
        if (dy < miny) miny = dy;
        if (dy > maxy) maxy = dy;
        if (maxy < 0 || miny >= h) continue;
        /* A solid's back faces. Wound outwards (counter-clockwise seen from
         * outside, y up), a front face turns CLOCKWISE on a screen whose y
         * grows down - a negative cross product. The first version skipped
         * those and drew the solids from the inside, which a round model
         * hides and a cube does not. */
        if (cull && (bx - ax) * (dy - ay) - (by - ay) * (dx - ax) >= 0) continue;

        /* the face normal, rotated like the vertices */
        const float *nn = &m->fn[t * 3];
        float nx1 = cyw * nn[0] + syw * nn[2];
        float nz1 = -syw * nn[0] + cyw * nn[2];
        float ny2 = cp * nn[1] - sp * nz1;
        float nz2 = sp * nn[1] + cp * nz1;
        (void)nx1;
        float li = 0.20f + 0.70f * fabsf(nz2) + 0.15f * (ny2 > 0 ? ny2 : 0);
        if (li > 1.0f) li = 1.0f;

        uint32_t rgb = base;
        if (m->fc) {
            uint16_t c = m->fc[t];
            rgb = ((uint32_t)((c >> 11) & 31) << 19) | ((uint32_t)((c >> 5) & 63) << 10) |
                  ((uint32_t)(c & 31) << 3);
        }
        uint32_t r = (uint32_t)(((rgb >> 16) & 255) * li);
        uint32_t g = (uint32_t)(((rgb >> 8) & 255) * li);
        uint32_t bl = (uint32_t)((rgb & 255) * li);
        uint16_t c = be565(r, g, bl);

        if (view->mode == V3_WIRE) {
            line(fb, w, h, (int)ax, (int)ay, (int)bx, (int)by, c);
            line(fb, w, h, (int)bx, (int)by, (int)dx, (int)dy, c);
            line(fb, w, h, (int)dx, (int)dy, (int)ax, (int)ay, c);
        } else {
            fill_tri(fb, zb, w, h, ax, ay, pa[2], bx, by, pb[2], dx, dy, pd[2], c);
        }
        drawn++;
    }
    uint32_t c3 = cycles();
    s_prof[0] += c1 - c0;
    s_prof[1] += c2 - c1;
    s_prof[2] += c3 - c2;
    return drawn;
}

void v3_upscale2(const uint16_t *small, uint16_t *fb, int w, int h)
{
    int sw = w / 2;
    for (int y = 0; y < h / 2; y++) {
        const uint16_t *s = small + (size_t)y * sw;
        uint16_t *d = fb + (size_t)(y * 2) * w;
        for (int x = 0; x < sw; x++) {
            d[x * 2] = d[x * 2 + 1] = s[x];
        }
        memcpy(d + w, d, (size_t)w * 2);
    }
}
