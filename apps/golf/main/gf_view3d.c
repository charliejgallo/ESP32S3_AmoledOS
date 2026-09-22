/*
 * GOLF - the 3D view (see gf_view3d.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_view3d.h"
#include "gf_map.h"
#include "gf_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG             0.017453293f
#define FAR_M           900.0f
#define CLOUD_H         900.0f
#define NEAR_TREE       6.5f            /* metres from the camera */

static int16_t s_top[GF_W];     /* per column: the first row the ground painted */

/* The ground is drawn column by column but the frame is stored row by row in
 * PSRAM: writing it straight made every pixel a different cache line. It is
 * drawn into this small column-major block, which stays in the cache, and
 * copied out row by row every GROUP columns. */
#define GROUP 16
static uint16_t s_gc[GROUP][GF_H], s_gd[GROUP][GF_H];
static float    s_az[GF_W], s_yh[GF_W], s_yt[GF_W];

/* Far away (FAR_MIP metres and beyond) the rays read small copies of the
 * heights (4 m a cell) and of the texture (2 m a texel): a few tens of KB
 * that stay in the cache, where the full grids missed it on nearly every
 * sample. Measured on the board: most of the 1.7 s the ground took. */
#define FAR_MIP     110.0f
#define HM          4               /* metres per coarse height cell */
#define TM          4               /* texels of the texture per coarse texel */
static int16_t  *s_hm;
static uint16_t *s_tm;
static int       s_hmw, s_hmh, s_tmw, s_tmh;
static uint32_t  s_mip_gen;
static const uint16_t *s_mip_tex;

static void build_mips(const gf_world_t *w, const gf_albedo_t *a)
{
    if (s_mip_gen == w->gen && s_mip_tex == a->tex && s_hm) return;
    int hmw = w->gw / HM + 1, hmh = w->gh / HM + 1;
    int tmw = a->tw / TM, tmh = a->th / TM;
    if (hmw * hmh > s_hmw * s_hmh || !s_hm) {
        free(s_hm);
        s_hm = (int16_t *)gf_malloc((size_t)hmw * hmh * 2);
    }
    if (tmw * tmh > s_tmw * s_tmh || !s_tm) {
        free(s_tm);
        s_tm = (uint16_t *)gf_malloc((size_t)tmw * tmh * 2);
    }
    s_hmw = hmw; s_hmh = hmh; s_tmw = tmw; s_tmh = tmh;
    if (!s_hm || !s_tm) return;
    for (int y = 0; y < hmh; y++) {
        for (int x = 0; x < hmw; x++) {
            int sx = x * HM, sy = y * HM;
            if (sx >= w->gw) sx = w->gw - 1;
            if (sy >= w->gh) sy = w->gh - 1;
            s_hm[y * hmw + x] = w->height[sy * w->gw + sx];
        }
    }
    for (int y = 0; y < tmh; y++) {
        for (int x = 0; x < tmw; x++) {
            int r = 0, g = 0, b = 0;
            for (int k = 0; k < TM; k++) {
                for (int j = 0; j < TM; j++) {
                    int rr, gg, bb;
                    gf_unpack(a->tex[(size_t)(y * TM + k) * a->tw + x * TM + j], &rr, &gg, &bb);
                    r += rr; g += gg; b += bb;
                }
            }
            s_tm[y * tmw + x] = gf_rgb(r / (TM * TM), g / (TM * TM), b / (TM * TM));
        }
    }
    s_mip_gen = w->gen;
    s_mip_tex = a->tex;
}

static float far_h(const gf_world_t *w, float x, float y)
{
    float fx = (x - w->gx0) * (1.0f / HM), fy = (y - w->gy0) * (1.0f / HM);
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > s_hmw - 1.001f) fx = s_hmw - 1.001f;
    if (fy > s_hmh - 1.001f) fy = s_hmh - 1.001f;
    int ix = (int)fx, iy = (int)fy;
    float tx = fx - ix, ty = fy - iy;
    const int16_t *p = s_hm + iy * s_hmw + ix;
    float a = p[0], b = p[1], c = p[s_hmw], d = p[s_hmw + 1];
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return (ab + (cd - ab) * ty) * 0.01f;
}

/* the swing camera, shared with tools/blender/SPEC.md: DO NOT change one
 * without the other */
#define SWING_BACK      4.80f
#define SWING_SIDE      (-0.16f)
#define SWING_UP        2.82f
#define SWING_PITCH     11.0f
#define SWING_VFOV      50.0f


/* --------------------------------------------------------------------------
 * Camera
 * -------------------------------------------------------------------------- */

void gf_cam_set(gf_cam_t *c, float x, float y, float z, float yaw, float pitch, float vfov_deg)
{
    c->x = x;
    c->y = y;
    c->z = z;
    c->yaw = yaw;
    c->pitch = pitch;
    c->cx = GF_W / 2.0f;
    c->cy = GF_H / 2.0f;
    c->f = (GF_H / 2.0f) / tanf(vfov_deg * 0.5f * DEG);
    c->fx = sinf(yaw);
    c->fy = cosf(yaw);
    c->rx = cosf(yaw);
    c->ry = -sinf(yaw);
    c->cp = cosf(pitch);
    c->sp = sinf(pitch);
}

void gf_cam_swing(gf_cam_t *c, const gf_world_t *w, float bx, float by, float aim)
{
    float fx = sinf(aim), fy = cosf(aim), rx = cosf(aim), ry = -sinf(aim);
    float x = bx - fx * SWING_BACK + rx * SWING_SIDE;
    float y = by - fy * SWING_BACK + ry * SWING_SIDE;
    float z = gf_height(w, bx, by) + SWING_UP;
    gf_cam_set(c, x, y, z, aim, SWING_PITCH * DEG, SWING_VFOV);
}

bool gf_cam_project(const gf_cam_t *c, float x, float y, float z, float *sx, float *sy, float *zc)
{
    float dx = x - c->x, dy = y - c->y, dz = z - c->z;
    float X = dx * c->rx + dy * c->ry;          /* right   */
    float Y = dx * c->fx + dy * c->fy;          /* forward */
    float Z = Y * c->cp - dz * c->sp;           /* depth   */
    float U = Y * c->sp + dz * c->cp;           /* up      */
    if (Z < 0.05f) {
        return false;
    }
    *sx = c->cx + c->f * X / Z;
    *sy = c->cy - c->f * U / Z;
    *zc = Z;
    return true;
}

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* the haze: close to 1 - exp(-d / 700), from a table (a division per
 * visible sample was measurable) */
static float s_fog[257];
static bool  s_fog_ready;
static void fog_init(void)
{
    if (s_fog_ready) return;
    for (int i = 0; i <= 256; i++) {
        float d = (float)i * 4.0f;
        float e = d * (1.0f / 700.0f);
        float f = e / (1.0f + e);
        s_fog[i] = f * f * 0.9f + f * 0.1f;
    }
    s_fog_ready = true;
}
static inline float fog_of(float d)
{
    float fi = d * 0.25f;
    if (fi >= 256.0f) return s_fog[256];
    int i = (int)fi;
    return s_fog[i] + (s_fog[i + 1] - s_fog[i]) * (fi - (float)i);
}

static void slope_at(const gf_world_t *w, float x, float y, float *gx, float *gy)
{
    float fx = (x - w->gx0) / GF_CELL, fy = (y - w->gy0) / GF_CELL;
    float flx = gf_floorf(fx), fly = gf_floorf(fy);
    int ix = (int)flx, iy = (int)fly;
    float tx = fx - flx, ty = fy - fly;
    if (ix < 0 || iy < 0 || ix >= w->gw - 1 || iy >= w->gh - 1) {
        *gx = *gy = 0.0f;
        return;
    }
    const int8_t *n00 = w->nrm + 2 * (iy * w->gw + ix);
    const int8_t *n10 = n00 + 2;
    const int8_t *n01 = n00 + 2 * w->gw;
    const int8_t *n11 = n01 + 2;
    float ax = n00[0] + (n10[0] - n00[0]) * tx, bx = n01[0] + (n11[0] - n01[0]) * tx;
    float ay = n00[1] + (n10[1] - n00[1]) * tx, by = n01[1] + (n11[1] - n01[1]) * tx;
    *gx = (ax + (bx - ax) * ty) * (1.0f / 64.0f);
    *gy = (ay + (by - ay) * ty) * (1.0f / 64.0f);
}

/* the ground height, continued flat-ish beyond the grid */
/* gf_height without the call and the per-corner clamps (the point is
 * clamped once, GF_CELL is 1 m) */
static inline float height_in(const gf_world_t *w, float x, float y)
{
    float fx = x - w->gx0, fy = y - w->gy0;
    int ix = (int)fx, iy = (int)fy;
    if (ix > w->gw - 2) ix = w->gw - 2;
    if (iy > w->gh - 2) iy = w->gh - 2;
    float tx = fx - (float)ix, ty = fy - (float)iy;
    const int16_t *p = w->height + iy * w->gw + ix;
    float a = p[0], b = p[1], c = p[w->gw], d = p[w->gw + 1];
    float ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return (ab + (cd - ab) * ty) * 0.01f;
}

static float ground_h(const gf_world_t *w, float x, float y, bool *inside)
{
    float gx1 = w->gx0 + (float)(w->gw - 1) * GF_CELL, gy1 = w->gy0 + (float)(w->gh - 1) * GF_CELL;
    *inside = x >= w->gx0 && y >= w->gy0 && x <= gx1 && y <= gy1;
    float cx = clampf(x, w->gx0, gx1), cy = clampf(y, w->gy0, gy1);
    float h = height_in(w, cx, cy);
    if (!*inside) {
        float d = fabsf(x - cx) + fabsf(y - cy);
        h += gf_noise(x, y, 90.0f, 5) * 3.0f * clampf(d / 60.0f, 0.0f, 1.0f) + d * 0.02f;
    }
    return h;
}

/* bilinear from the RGB565 texture, in 8-bit channels */
static void tex_sample(const gf_albedo_t *a, const gf_world_t *w, float x, float y, bool bil, float *rgb)
{
    float im = 1.0f / a->mpp;                   /* a constant: folded by the caller */
    float u = (x - w->gx0) * im - 0.5f, v = (y - w->gy0) * im - 0.5f;
    if (u < 0) u = 0;
    if (v < 0) v = 0;
    if (u > a->tw - 1.001f) u = a->tw - 1.001f;
    if (v > a->th - 1.001f) v = a->th - 1.001f;
    int iu = (int)u, iv = (int)v;
    const uint16_t *p = a->tex + (size_t)iv * a->tw + iu;
    int r, g, b;
    if (!bil) {
        gf_unpack(p[0], &r, &g, &b);
        rgb[0] = (float)r; rgb[1] = (float)g; rgb[2] = (float)b;
        return;
    }
    float tu = u - (float)iu, tv = v - (float)iv;
    float acc[3] = { 0, 0, 0 };
    const float wts[4] = { (1 - tu) * (1 - tv), tu * (1 - tv), (1 - tu) * tv, tu * tv };
    const uint16_t px[4] = { p[0], p[1], p[a->tw], p[a->tw + 1] };
    for (int k = 0; k < 4; k++) {
        gf_unpack(px[k], &r, &g, &b);
        acc[0] += wts[k] * r;
        acc[1] += wts[k] * g;
        acc[2] += wts[k] * b;
    }
    rgb[0] = acc[0]; rgb[1] = acc[1]; rgb[2] = acc[2];
}

/* --------------------------------------------------------------------------
 * Sky, clouds and the horizon
 * -------------------------------------------------------------------------- */

static const float HAZE[3] = { 186, 208, 226 };

static void sky_color(const gf_cam_t *c, float sx, float sy, float *rgb)
{
    /* direction of the pixel */
    float X = (sx - c->cx) / c->f, U = -(sy - c->cy) / c->f;
    /* camera -> world: forward Y, up Z */
    float Y = c->cp + U * c->sp;
    float Z = U * c->cp - c->sp;
    float l = sqrtf(X * X + Y * Y + Z * Z);
    float el = Z / l;                                   /* sin(elevation) */
    float t = clampf(el * 2.2f, 0.0f, 1.0f);
    t = sqrtf(t);
    rgb[0] = HAZE[0] + (64 - HAZE[0]) * t;
    rgb[1] = HAZE[1] + (122 - HAZE[1]) * t;
    rgb[2] = HAZE[2] + (206 - HAZE[2]) * t;
    if (el > 0.012f) {
        /* clouds on a plane CLOUD_H above: perspective for free */
        float k = CLOUD_H / Z;
        float wx = c->x + (X * c->rx + Y * c->fx) * k;
        float wy = c->y + (X * c->ry + Y * c->fy) * k;
        float n = gf_tex(wx * 0.0025f, wy * 0.0025f) * 0.65f + gf_tex(wx * 0.009f + 30, wy * 0.009f) * 0.35f;
        float cov = clampf((n - 0.05f) * 2.4f, 0.0f, 1.0f);
        cov *= clampf((el - 0.012f) * 18.0f, 0.0f, 1.0f);      /* thin out at the horizon */
        if (cov > 0) {
            float lit = 0.86f + 0.14f * clampf(gf_tex(wx * 0.009f + 5, wy * 0.009f + 9) + 0.3f, 0.0f, 1.0f);
            float cr = 250 * lit, cg = 251 * lit, cb = 255 * lit;
            /* shaded undersides where the cloud is thick */
            float thick = clampf((n - 0.35f) * 2.0f, 0.0f, 1.0f);
            cr -= 40 * thick; cg -= 34 * thick; cb -= 22 * thick;
            rgb[0] += (cr - rgb[0]) * cov;
            rgb[1] += (cg - rgb[1]) * cov;
            rgb[2] += (cb - rgb[2]) * cov;
        }
    }
}

/* elevation (radians) of the far hills and of the treeline in front of
 * them, as a function of the absolute heading: turning shows other hills */
static float hills_el(float az)
{
    float a = az * 3.0f;
    return (1.9f + 1.4f * sinf(a * 1.0f + 0.7f) * sinf(a * 0.37f + 2.0f) +
            0.6f * sinf(a * 2.3f + 1.1f) + 0.25f * sinf(a * 5.1f)) * DEG;
}

static float treeline_el(float az)
{
    float a = az * 40.0f;
    float bumps = fabsf(sinf(a)) * 0.35f + fabsf(sinf(a * 2.7f + 1.0f)) * 0.25f + fabsf(sinf(a * 6.1f)) * 0.12f;
    return (0.55f + 0.35f * sinf(az * 5.0f + 0.4f) + bumps) * DEG;
}

/* --------------------------------------------------------------------------
 * Trees drawn by code, for when there is no art
 * -------------------------------------------------------------------------- */

static gf_sprite_t s_code_tree[GF_TREE_KINDS];
static bool        s_code_ready;

static void code_trees(void)
{
    if (s_code_ready) return;
    static const int WH[GF_TREE_KINDS][2] = { { 60, 128 }, { 108, 128 }, { 40, 128 }, { 76, 128 }, { 96, 60 } };
    static const uint8_t COL[GF_TREE_KINDS][3] = {
        { 38, 88, 54 }, { 62, 116, 42 }, { 88, 130, 48 }, { 74, 134, 60 }, { 66, 118, 46 },
    };
    for (int k = 0; k < GF_TREE_KINDS; k++) {
        int W = WH[k][0], H = WH[k][1];
        gf_sprite_t *s = &s_code_tree[k];
        s->w = (int16_t)W;
        s->h = (int16_t)H;
        s->ox = (int16_t)(W / 2);
        s->oy = (int16_t)(H - 1);
        s->px = (uint16_t *)gf_malloc((size_t)W * H * 2);
        s->a = (uint8_t *)gf_calloc((size_t)W * H, 1);
        if (!s->px || !s->a) continue;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float u = ((float)x + 0.5f - W / 2.0f) / (W / 2.0f);   /* -1..1 */
                float v = 1.0f - ((float)y + 0.5f) / H;                 /* 0 bottom .. 1 top */
                float cov = 0, lx = 0, ly = 0, lz = 1;
                bool trunk = false;
                switch (k) {
                case GF_TREE_OAK: {
                    if (v < 0.42f && fabsf(u) < 0.07f + (0.42f - v) * 0.05f) trunk = true;
                    float cy = 0.64f, ry = 0.36f;
                    float dx = u / 0.98f, dy = (v - cy) / ry;
                    float d = sqrtf(dx * dx + dy * dy);
                    float ang = atan2f(dy, dx);
                    float rr = 0.92f + 0.07f * sinf(ang * 7) + 0.05f * gf_tex(u * 6 + 40, v * 6);
                    if (d < rr) { cov = clampf((rr - d) * 20, 0, 1); lx = dx; ly = dy; lz = sqrtf(clampf(1 - d * d / (rr * rr), 0, 1)); }
                    break;
                }
                case GF_TREE_PINE: {
                    if (v < 0.2f && fabsf(u) < 0.08f) trunk = true;
                    if (v >= 0.1f) {
                        float t = (v - 0.1f) / 0.9f;
                        float tier = t * 6.0f;
                        float saw = 1.0f - (tier - gf_floorf(tier)) * 0.35f;
                        float hw = (1.0f - t) * saw * 0.98f + 0.02f;
                        hw += 0.05f * gf_tex(u * 8, v * 12);
                        if (fabsf(u) < hw) { cov = clampf((hw - fabsf(u)) * 20, 0, 1); lx = u / (hw + 0.01f); ly = 0.3f; lz = sqrtf(clampf(1 - lx * lx, 0, 1)); }
                    }
                    break;
                }
                case GF_TREE_POPLAR: {
                    if (v < 0.12f && fabsf(u) < 0.12f) trunk = true;
                    float cy = 0.56f, ry = 0.46f;
                    float dx = u / 0.95f, dy = (v - cy) / ry;
                    float d = sqrtf(dx * dx + dy * dy) + 0.08f * gf_tex(u * 5, v * 14 + 9);
                    if (d < 1) { cov = clampf((1 - d) * 16, 0, 1); lx = dx; ly = dy; lz = sqrtf(clampf(1 - d * d, 0, 1)); }
                    break;
                }
                case GF_TREE_PALM: {
                    float bend = 0.18f * v * v;
                    if (v < 0.82f && fabsf(u - bend) < 0.06f) trunk = true;
                    float dx = (u - 0.18f * 0.8f), dy = (v - 0.84f);
                    float ang = atan2f(dy, dx), d = sqrtf(dx * dx + dy * dy * 4);
                    float frond = powf(clampf(cosf(ang * 4.0f), 0, 1), 6.0f);
                    float reach = 0.25f + 0.75f * frond;
                    if (d < reach && dy > -0.18f - 0.5f * d) { cov = clampf((reach - d) * 12, 0, 1); lx = dx * 2; ly = dy * 2; lz = 0.6f; }
                    break;
                }
                case GF_TREE_BUSH: {
                    float dx = u, dy = (v - 0.45f) / 0.55f;
                    float d = sqrtf(dx * dx + dy * dy);
                    float rr = 0.9f + 0.08f * sinf(atan2f(dy, dx) * 6) + 0.06f * gf_tex(u * 7, v * 7);
                    if (d < rr && v > 0.02f) { cov = clampf((rr - d) * 14, 0, 1); lx = dx; ly = dy; lz = sqrtf(clampf(1 - d * d / (rr * rr), 0, 1)); }
                    break;
                }
                }
                size_t i = (size_t)y * W + x;
                if (cov > 0) {
                    /* lit from the upper left and a bit from the front */
                    float light = clampf(-lx * 0.45f + ly * 0.35f + lz * 0.65f, 0.0f, 1.2f);
                    float clump = gf_tex(u * 10 + k * 20, v * 14) * 0.25f;
                    float kk = clampf(0.40f + 0.65f * light + clump, 0.25f, 1.35f);
                    int R = (int)(COL[k][0] * kk), G = (int)(COL[k][1] * kk), B = (int)(COL[k][2] * kk);
                    s->px[i] = gf_rgb(R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B);
                    s->a[i] = (uint8_t)(cov * 255);
                } else if (trunk) {
                    float kk = 0.7f + 0.3f * (u < 0 ? 1.0f : 0.5f);
                    s->px[i] = gf_rgb((int)(96 * kk), (int)(70 * kk), (int)(48 * kk));
                    s->a[i] = 255;
                }
            }
        }
    }
    s_code_ready = true;
}

/* A billboard, scaled, fogged, tested against the ground's depth. The anchor
 * (trunk base) goes to (bx, by); 'scale' is screen px per sprite px. */
static void billboard(uint16_t *out, const uint16_t *depth, const gf_sprite_t *s, float bx, float by,
                      float scale, float zc, float fog)
{
    int dw = (int)(s->w * scale + 0.5f), dh = (int)(s->h * scale + 0.5f);
    if (dw < 1 || dh < 1) return;
    float x0f = bx - s->ox * scale, y0f = by - s->oy * scale;
    int x0 = (int)gf_floorf(x0f), y0 = (int)gf_floorf(y0f);
    int x1 = x0 + dw, y1 = y0 + dh;
    int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0;
    int cx1 = x1 > GF_W ? GF_W : x1, cy1 = y1 > GF_H ? GF_H : y1;
    uint16_t dz = (uint16_t)clampf(zc * 10.0f, 0, 65000);
    uint16_t hz = gf_rgb((int)HAZE[0], (int)HAZE[1], (int)HAZE[2]);
    int fg = (int)(fog * 255);
    float inv = 1.0f / scale;
    for (int y = cy0; y < cy1; y++) {
        int sy = (int)(((float)y + 0.5f - y0f) * inv);
        if (sy < 0 || sy >= s->h) continue;
        const uint16_t *sp = s->px + (size_t)sy * s->w;
        const uint8_t  *sa = s->a + (size_t)sy * s->w;
        for (int x = cx0; x < cx1; x++) {
            if (depth && depth[y * GF_W + x] < dz) continue;
            int sx = (int)(((float)x + 0.5f - x0f) * inv);
            if (sx < 0 || sx >= s->w) continue;
            int a = sa[sx];
            if (!a) continue;
            uint16_t c = sp[sx];
            if (fg > 0) c = gf_blend(c, hz, fg);
            out[y * GF_W + x] = gf_blend(out[y * GF_W + x], c, a);
        }
    }
}

typedef struct { float zc; int idx; } tsort_t;

static int tsort_cmp(const void *a, const void *b)
{
    float za = ((const tsort_t *)a)->zc, zb = ((const tsort_t *)b)->zc;
    return za > zb ? -1 : (za < zb ? 1 : 0);        /* far first */
}

/* --------------------------------------------------------------------------
 * The render
 * -------------------------------------------------------------------------- */

void gf_view3d_render(const gf_world_t *w, const gf_cam_t *c, const gf_albedo_t *alb,
                      uint16_t *out, uint16_t *depth_out)
{
    gf_tex_init();
    code_trees();

    uint16_t *depth = depth_out;
    uint16_t *own_depth = NULL;
    if (!depth) {
        own_depth = (uint16_t *)gf_malloc((size_t)GF_W * GF_H * 2);
        depth = own_depth;
    }

    uint32_t t0 = gf_clock();
    fog_init();
    build_mips(w, alb);
    /* 1. the ground, column by column */
    /* light: from behind the camera, to its left, 50 degrees up */
    float Lx = -c->rx * 0.45f - c->fx * 0.55f, Ly = -c->ry * 0.45f - c->fy * 0.55f, Lz = 0.85f;
    {
        float l = sqrtf(Lx * Lx + Ly * Ly + Lz * Lz);
        Lx /= l; Ly /= l; Lz /= l;
    }
    float invLz = 1.0f / Lz;
    bool coast = GF_THEME(w->def) == THEME_COAST;
    float inv_tm = 1.0f / (alb->mpp * TM);
    float ground_ref = c->z - gf_height(w, c->x, c->y);
    /* the highest ground anywhere, for the early exit */
    float hmax = -1e9f;
    for (int i = 0; i < w->gw * w->gh; i += 7) {
        float hh = (float)w->height[i] * 0.01f;
        if (hh > hmax) hmax = hh;
    }
    hmax += 14.0f;              /* beyond the grid the ground climbs a little */
    int horizon = (int)(c->cy - c->f * c->sp / c->cp);
    uint32_t n_samp = 0, n_vis = 0;
    if (ground_ref < 0.5f) ground_ref = 0.5f;

    for (int x0 = 0; x0 < GF_W; x0 += GROUP) {
        gf_yield();
        int gn = GF_W - x0 < GROUP ? GF_W - x0 : GROUP;
        float cu[GROUP], cprev[GROUP][3];
        int cy_min[GROUP];
        uint8_t chave[GROUP], cdone[GROUP];
        int ndone = 0;
        for (int k = 0; k < gn; k++) {
            cu[k] = ((float)(x0 + k) + 0.5f - c->cx) / c->f;
            cy_min[k] = GF_H;               /* rows below this are painted */
            chave[k] = 0;
            cdone[k] = 0;
        }
        /* the group's rays walk together: at each distance their samples
         * are metres apart and share the cache's lines */
        for (float d = 2.5f; d < FAR_M && ndone < gn;
             d += d < FAR_MIP ? 0.015f + d * 0.0055f : d * 0.0095f) {
            for (int k = 0; k < gn; k++) {
                if (cdone[k]) continue;
                int sx = x0 + k;
                /* the column's ray on a flat ground at the camera's height */
                float lat = cu[k] * (d * c->cp + ground_ref * c->sp);
                float x = c->x + c->fx * d + c->rx * lat;
                float y = c->y + c->fy * d + c->ry * lat;
                n_samp++;
                bool inside;
                bool far = d >= FAR_MIP && s_hm && s_tm;
                float h;
                if (far) {
                    float gx1 = w->gx0 + (float)(w->gw - 1) * GF_CELL, gy1 = w->gy0 + (float)(w->gh - 1) * GF_CELL;
                    inside = x >= w->gx0 && y >= w->gy0 && x <= gx1 && y <= gy1;
                    h = inside ? far_h(w, x, y) : ground_h(w, x, y, &inside);
                } else {
                    h = ground_h(w, x, y, &inside);
                }
                /* on the coast, what is beyond the course is the sea */
                bool sea = coast && !inside;
                if (sea) h = w->water_level > -50.0f ? w->water_level : h - 1.0f;
                bool water = sea || (inside && h < w->water_level + 0.02f);
                if (water) h = w->water_level;
                float dz = h - c->z;
                float Z = d * c->cp - dz * c->sp;
                float U = d * c->sp + dz * c->cp;
                float syf = c->cy - c->f * U / Z;
                int sy = (int)gf_ceilf(syf - 0.5f);
                if (sy < cy_min[k]) {
                    n_vis++;
                    float col[3];
                    if (inside && far) {
                        int tu = (int)((x - w->gx0) * inv_tm), tv = (int)((y - w->gy0) * inv_tm);
                        if (tu < 0) tu = 0;
                        if (tv < 0) tv = 0;
                        if (tu >= s_tmw) tu = s_tmw - 1;
                        if (tv >= s_tmh) tv = s_tmh - 1;
                        int rr, gg, bb;
                        gf_unpack(s_tm[tv * s_tmw + tu], &rr, &gg, &bb);
                        col[0] = (float)rr; col[1] = (float)gg; col[2] = (float)bb;
                    } else if (inside) {
                        tex_sample(alb, w, x, y, true, col);
                    } else if (sea) {
                        float n = gf_tex(x * 0.02f, y * 0.06f);
                        col[0] = 22 + 10 * n; col[1] = 84 + 16 * n; col[2] = 132 + 12 * n;
                    } else {
                        float n = gf_tex(x * 0.05f, y * 0.05f);
                        col[0] = 60 * (1 + 0.1f * n); col[1] = 116 * (1 + 0.1f * n); col[2] = 44 * (1 + 0.1f * n);
                    }
                    /* fine grass grain near the camera, in world space */
                    if (d < 40.0f && !water) {
                        float g = gf_tex(x * 2.3f, y * 2.3f) * 0.07f * (1.0f - d / 40.0f);
                        col[0] *= 1 + g; col[1] *= 1 + g; col[2] *= 1 + g;
                    }
                    float kl;
                    if (water) {
                        /* reflect the sky, more at a grazing angle */
                        float sky[3];
                        sky_color(c, (float)sx + 0.5f, 2.0f * (c->cy - c->f * tanf(c->pitch)) - syf, sky);
                        float fr = clampf(0.35f + 0.6f * (1.0f - clampf(-dz / (d + 0.1f) * 3.0f, 0, 1)), 0.3f, 0.92f);
                        if (coast) fr *= 0.5f;          /* the sea stays blue */
                        for (int kk = 0; kk < 3; kk++) col[kk] += (sky[kk] - col[kk]) * fr;
                        kl = 1.0f;
                    } else {
                        float gx, gy;
                        if (far) {
                            gx = (far_h(w, x + HM, y) - far_h(w, x - HM, y)) * (0.5f / HM);
                            gy = (far_h(w, x, y + HM) - far_h(w, x, y - HM)) * (0.5f / HM);
                        } else {
                            slope_at(w, x, y, &gx, &gy);
                        }
                        float nx = -gx * 1.6f, ny = -gy * 1.6f, nz = 1.0f;
                        float s2 = nx * nx + ny * ny;
                        float inv = s2 < 0.6f ? gf_inv_len1(s2) : 1.0f / sqrtf(s2 + 1.0f);
                        float dot = (nx * Lx + ny * Ly + nz * Lz) * inv * invLz;
                        kl = clampf(0.25f + 0.75f * dot, 0.35f, 1.3f);
                    }
                    float fog = fog_of(d);
                    float R = col[0] * kl, G = col[1] * kl, B = col[2] * kl;
                    R += (HAZE[0] - R) * fog;
                    G += (HAZE[1] - G) * fog;
                    B += (HAZE[2] - B) * fog;
                    int top = sy < 0 ? 0 : sy;
                    uint16_t dd = (uint16_t)clampf(Z * 10.0f, 0, 65000);
                    /* blend from the previous sample down the span: no bands
                     * near the camera where one sample covers many rows */
                    int span = cy_min[k] - top;
                    for (int yy = top; yy < cy_min[k]; yy++) {
                        float r = R, g = G, b = B;
                        if (chave[k] && span > 2) {
                            float t = (float)(yy - top) / (float)span;
                            r += (cprev[k][0] - r) * t;
                            g += (cprev[k][1] - g) * t;
                            b += (cprev[k][2] - b) * t;
                        }
                        s_gc[k][yy] = gf_dither((int)r, (int)g, (int)b, sx, yy);
                        s_gd[k][yy] = dd;
                    }
                    cprev[k][0] = R; cprev[k][1] = G; cprev[k][2] = B;
                    chave[k] = true;
                    cy_min[k] = top;
                }
                /* nothing further can show: the highest ground there is would
                 * project under what is already painted */
                if (cy_min[k] <= 0) {
                    cdone[k] = 1;
                    ndone++;
                } else if (hmax > c->z) {
                    float dz = hmax - c->z;
                    float Zt = d * c->cp - dz * c->sp;
                    if (Zt > 0.1f && c->cy - c->f * (d * c->sp + dz * c->cp) / Zt >= (float)cy_min[k]) { cdone[k] = 1; ndone++; }
                } else if (cy_min[k] <= horizon) {
                    { cdone[k] = 1; ndone++; }
                }
            }
        }
        for (int k = 0; k < gn; k++) s_top[x0 + k] = (int16_t)cy_min[k];
        {
            int n = gn, top = GF_H;
            for (int k = 0; k < n; k++) {
                if (s_top[x0 + k] < top) top = s_top[x0 + k];
            }
            for (int y = top; y < GF_H; y++) {
                uint16_t *o = out + (size_t)y * GF_W + x0;
                uint16_t *dp = depth + (size_t)y * GF_W + x0;
                for (int k = 0; k < n; k++) {
                    if (y >= s_top[x0 + k]) {
                        o[k] = s_gc[k][y];
                        dp[k] = s_gd[k][y];
                    }
                }
            }
        }
    }

    uint32_t t1 = gf_clock();
    gf_prof_set(5, n_samp);
    gf_prof_set(6, n_vis);
    /* 2. sky, hills and treeline, only above the ground each column
     *    already painted: the ground is ~70 % of the picture */
    float hill_rgb[3] = { 128, 150, 172 };
    float line_rgb[3] = { 70, 96, 70 };
    int maxtop = 0;
    for (int sx = 0; sx < GF_W; sx++) {
        float X = ((float)sx + 0.5f - c->cx) / c->f;
        s_az[sx] = c->yaw + atan2f(X, 1.0f);
        /* elevation -> screen row: angle above the axis is el + pitch */
        s_yh[sx] = c->cy - c->f * tanf(hills_el(s_az[sx]) + c->pitch);
        s_yt[sx] = c->cy - c->f * tanf(treeline_el(s_az[sx]) + c->pitch);
        if (coast) {
            /* no hills and no woods: the sea meets the sky */
            s_yh[sx] = s_yt[sx] = c->cy - c->f * tanf(c->pitch);
        }
        if (s_top[sx] > maxtop) maxtop = s_top[sx];
    }
    for (int sy = 0; sy < maxtop; sy++) {
        if ((sy & 15) == 0) gf_yield();
        float fy = (float)sy + 0.5f;
        uint16_t *orow = out + (size_t)sy * GF_W;
        uint16_t *drow = depth + (size_t)sy * GF_W;
        for (int sx = 0; sx < GF_W; sx++) {
            if (sy >= s_top[sx]) continue;
            float rgb[3];
            float az = s_az[sx], yh = s_yh[sx], yt = s_yt[sx];
            if (coast && fy >= yt) {
                /* the far sea: haze at the horizon, deeper blue below, a
                 * glitter of the sun */
                float t = clampf((fy - yt) / 40.0f, 0.0f, 1.0f);
                rgb[0] = HAZE[0] + (30 - HAZE[0]) * t;
                rgb[1] = HAZE[1] + (96 - HAZE[1]) * t;
                rgb[2] = HAZE[2] + (146 - HAZE[2]) * t;
                uint32_t hs = gf_hash((uint32_t)sx * 73856093U ^ (uint32_t)sy * 19349663U);
                if ((hs & 127) == 0) { rgb[0] += 60; rgb[1] += 60; rgb[2] += 50; }
            } else if (fy >= yt) {
                /* the treeline: dark, a little hazed, bumpy top edge */
                float t = gf_tex(az * 60.0f, fy * 0.3f) * 0.12f;
                rgb[0] = line_rgb[0] * (1 + t); rgb[1] = line_rgb[1] * (1 + t); rgb[2] = line_rgb[2] * (1 + t);
                float e = clampf(fy - yt, 0, 1);
                if (e < 1) {
                    float s2[3];
                    sky_color(c, (float)sx + 0.5f, fy, s2);
                    for (int k = 0; k < 3; k++) rgb[k] = s2[k] + (rgb[k] - s2[k]) * e;
                }
            } else if (fy >= yh) {
                /* the far hills, bluish, lighter towards the ridge */
                float t = clampf((fy - yh) / 30.0f, 0, 1);
                float n = gf_tex(az * 25.0f + 50, fy * 0.08f) * 0.05f;
                for (int k = 0; k < 3; k++) rgb[k] = (hill_rgb[k] + (HAZE[k] - hill_rgb[k]) * 0.35f * (1 - t)) * (1 + n);
                float e = clampf(fy - yh, 0, 1);
                if (e < 1) {
                    float s2[3];
                    sky_color(c, (float)sx + 0.5f, fy, s2);
                    for (int k = 0; k < 3; k++) rgb[k] = s2[k] + (rgb[k] - s2[k]) * e;
                }
            } else {
                sky_color(c, (float)sx + 0.5f, fy, rgb);
            }
            orow[sx] = gf_dither((int)rgb[0], (int)rgb[1], (int)rgb[2], sx, sy);
            drow[sx] = 0xFFFF;
        }
    }

    uint32_t t2 = gf_clock();
    /* 3. trees: every one in front of the camera, far to near */
    static tsort_t list[GF_MAX_TREES];
    int n = 0;
    for (int i = 0; i < w->ntrees; i++) {
        const gf_tree_t *t = &w->trees[i];
        float sx, sy, zc;
        if (!gf_cam_project(c, t->x, t->y, t->h, &sx, &sy, &zc)) continue;
        float hpx = t->height * c->f / zc;
        float wpx = t->radius * 2.4f * c->f / zc;
        if (sx + wpx < 0 || sx - wpx > GF_W || sy - hpx > GF_H || sy < 0 - 10) continue;
        list[n].zc = zc;
        list[n].idx = i;
        n++;
    }
    qsort(list, (size_t)n, sizeof(list[0]), tsort_cmp);
    gf_img_t im;
    gf_img_init(&im, out, GF_W, GF_H);
    uint16_t haze = gf_rgb((int)HAZE[0], (int)HAZE[1], (int)HAZE[2]);
    for (int k = 0; k < n; k++) {
        if ((k & 31) == 0) gf_yield();
        const gf_tree_t *t = &w->trees[list[k].idx];
        float sx, sy, zc;
        gf_cam_project(c, t->x, t->y, t->h, &sx, &sy, &zc);
        float hpx = t->height * c->f / zc;
        float fog = fog_of(zc);
        /* a soft shadow on the ground, towards the front right; not for the
         * trees right by the camera, whose ellipses would stack into rings */
        if (hpx > 3 && zc > 14.0f) {
            float k = clampf((zc - 14.0f) / 10.0f, 0.0f, 1.0f);
            int rx = (int)(t->radius * 0.9f * c->f / zc * 16), ry = (int)(t->radius * 0.3f * c->f / zc * 16);
            gf_shadow(&im, (int)((sx + rx / 32.0f) * 16), (int)((sy + ry / 64.0f) * 16), rx, ry, (int)(55 * (1 - fog) * k));
        }
        const gf_mip_t *m = gf_art_tree_side(t->kind);
        uint16_t dz = (uint16_t)clampf((zc + t->radius) * 10.0f, 0, 65000);
        /* a tree between the camera and the golfer would be a green wall:
         * nearer than NEAR_TREE it is not drawn, and it fades in over the
         * next few metres (the ball inside a wood still shows the wood) */
        int opa = (int)clampf((zc - NEAR_TREE) * (255.0f / 6.0f), 0.0f, 255.0f);
        if (opa <= 0) continue;
        if (m) {
            float ratio = t->height / gf_art_tree_height(t->kind);
            float scale = c->f / zc * ratio / m->ppm;
            gf_mip_draw(&im, m, sx, sy + 1, scale, opa, 232 + t->shade * 3, depth, dz, haze, (int)(fog * 255));
        } else {
            const gf_sprite_t *s = &s_code_tree[t->kind];
            if (!s->px) continue;
            billboard(out, depth, s, sx, sy + 1, hpx / (float)s->h, zc + t->radius, fog);
        }
    }

    uint32_t t3 = gf_clock();
    gf_prof_set(0, t1 - t0);
    gf_prof_set(1, t2 - t1);
    gf_prof_set(2, t3 - t2);
    /* 4. the flag: pole and cloth, never smaller than readable */
    {
        float sx, sy, zc, tx, ty, tz;
        float ph = gf_height(w, w->pin_x, w->pin_y);
        if (gf_cam_project(c, w->pin_x, w->pin_y, ph, &sx, &sy, &zc) &&
            gf_cam_project(c, w->pin_x, w->pin_y, ph + 2.2f, &tx, &ty, &tz)) {
            float len = sy - ty;
            float minlen = 18.0f;
            if (len < minlen) len = minlen;
            uint16_t dz = (uint16_t)clampf(zc * 10.0f, 0, 65000);
            int vis = (int)sy >= 0 && (int)sy < GF_H && (int)sx >= 0 && (int)sx < GF_W &&
                      depth[(int)sy * GF_W + (int)sx] + 30 >= dz;
            const gf_mip_t *fm = gf_art_flag(0);
            if (vis && fm) {
                float scale = len / 2.36f / fm->ppm;
                gf_mip_draw(&im, fm, sx, sy, scale, 255, 256, NULL, 0, 0, 0);
            } else if (vis) {
                int w16 = len > 40 ? 24 : 18;
                gf_line(&im, (int)(sx * 16), (int)(sy * 16), (int)(sx * 16), (int)((sy - len) * 16), w16, gf_rgb(250, 250, 245), 255);
                float fw = len * 0.42f, fh = len * 0.26f;
                for (int yy = 0; yy < (int)(fh + 1); yy++) {
                    int lenx = (int)(fw * (1.0f - fabsf((float)yy - fh / 2) / fh * 0.9f));
                    for (int xx = 1; xx <= lenx; xx++) {
                        int X = (int)sx + xx, Y = (int)(sy - len) + yy + (int)(sinf(xx * 0.4f) * 1.2f);
                        if (X < 0 || Y < 0 || X >= GF_W || Y >= GF_H) continue;
                        float wv = 0.85f + 0.15f * sinf(xx * 0.4f + 1.0f);
                        out[Y * GF_W + X] = gf_rgb((int)(235 * wv), (int)(44 * wv), (int)(40 * wv));
                    }
                }
            }
        }
    }

    free(own_depth);
}

