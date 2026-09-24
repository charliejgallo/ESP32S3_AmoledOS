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
#define BG_BE       0x2108              /* 0x0821 (a near black blue), byte-swapped */
#define BASE_RGB    0xA8B8D0            /* the model when the file has no colour */

static inline uint16_t be565(uint32_t r, uint32_t g, uint32_t b)
{
    uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

bool v3_scratch_alloc(v3_scratch_t *s, int nv)
{
    s->sx = malloc((size_t)nv * sizeof(float));
    s->sy = malloc((size_t)nv * sizeof(float));
    s->sz = malloc((size_t)nv * sizeof(float));
    s->nv = nv;
    if (!s->sx || !s->sy || !s->sz) {
        v3_scratch_free(s);
        return false;
    }
    return true;
}

void v3_scratch_free(v3_scratch_t *s)
{
    free(s->sx);
    free(s->sy);
    free(s->sz);
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

    int ys = (int)ceilf(y0 - 0.5f), ye = (int)ceilf(y2 - 0.5f);
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
        int xs = (int)ceilf(xa - 0.5f), xe = (int)ceilf(xb - 0.5f);
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

int v3_render(const v3_mesh_t *m, const v3_view_t *view, v3_scratch_t *s,
              uint16_t *fb, uint16_t *zb, int w, int h)
{
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) fb[i] = BG_BE;
    if (view->mode == V3_SOLID) memset(zb, 0xFF, n * 2);

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
        s->sx[i] = cx + x1 * k;
        s->sy[i] = cy - y2 * k;
        s->sz[i] = (d - (CAM_D - 1.0f)) * (65535.0f / 2.0f);
    }

    int drawn = 0;
    uint32_t base = BASE_RGB;
    for (int t = 0; t < m->nt; t++) {
        uint32_t a = m->idx[t * 3], b = m->idx[t * 3 + 1], d = m->idx[t * 3 + 2];
        float ax = s->sx[a], ay = s->sy[a], bx = s->sx[b], by = s->sy[b];
        float dx = s->sx[d], dy = s->sy[d];
        /* off screen entirely: skip */
        float minx = ax < bx ? ax : bx, maxx = ax > bx ? ax : bx;
        if (dx < minx) minx = dx;
        if (dx > maxx) maxx = dx;
        if (maxx < 0 || minx >= w) continue;
        float miny = ay < by ? ay : by, maxy = ay > by ? ay : by;
        if (dy < miny) miny = dy;
        if (dy > maxy) maxy = dy;
        if (maxy < 0 || miny >= h) continue;

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
            fill_tri(fb, zb, w, h, ax, ay, s->sz[a], bx, by, s->sz[b], dx, dy, s->sz[d], c);
        }
        drawn++;
    }
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
