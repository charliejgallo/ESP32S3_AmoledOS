/*
 * TURBO - the pseudo-3D renderer (see tb_render.h)
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "tb_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PANO_W      1024        /* the sky + backdrop panorama, wraps around */
#define TEX         64
#define TEX_NEAR    48.0f       /* textures closer than this, flat beyond     */
#define NEAR_Z      0.8f
#define NSTARS      160
#define BG_SCROLL   7.5f        /* backdrop px per (curve x metre)            */

typedef struct {
    float z;                    /* camera depth of the segment's start        */
    float scale;                /* px per metre there                          */
    float cx, sy;               /* screen x of the road's centre, screen y     */
    int16_t clip;               /* rows from here down are hidden              */
    bool  vis;
    /* the rows it paints: [ya, ybot), and what they need */
    int16_t ya, ybot;
    float sx2, sy2, w1, w2, iz1, iz2, hw;
    int16_t fk;                 /* fog, 0..256                                 */
} drawn_t;

typedef struct {
    uint16_t road[4], gl[4], gr[4], rumble, line, yellow;
    uint16_t wall;                  /* a tunnel's wall on this row           */
    bool void_l, void_r, tunnel;
} rowpal_t;

/* one sprite of the frame, placed by tb_render_prepare() */
enum { DL_PROP = 0, DL_VEH, DL_STANDIN_PROP, DL_STANDIN_CAR };
#define DL_MAX 640
typedef struct {
    uint8_t type, mirror, opa, kind;    /* kind: the prop, or the vehicle model */
    uint8_t span;
    int16_t clip, ytop, ybot, fk, lut;  /* lut: traffic index, -1 the rival    */
    int16_t cx0, cx1, cy0;              /* a tunnel's mouth it is seen through */
    float   z;                          /* its depth                           */
    float   x, y, sc, shsc, ppm;
    const void *m;                      /* tb_mip_t or tb_vmip_t              */
    const tb_mip_t *sh;
} dl_t;

struct tb_render {
    const tb_track_t *trk;
    tb_theme_t th;
    uint16_t *pano;             /* PANO_W x TB_BG_H                            */
    uint8_t   tex_road[TEX * TEX];
    uint8_t   tex_gnd[TEX * TEX];
    int16_t   star_u[NSTARS], star_v[NSTARS];
    uint8_t   star_b[NSTARS];
    float     bg_off;
    tb_lut_t  lut_rival;
    tb_paint_t paint_player, paint_rival;
    drawn_t   dr[TB_DRAW + 1];
    rowpal_t  pal[TB_DRAW + 1];
    int16_t   rowseg[TB_H];     /* which segment paints each row, -1 none      */
    tb_lut_t  car_lut[TB_TRAFFIC];
    uint32_t  lut_key[TB_TRAFFIC];  /* what each LUT was built for, ~0 none   */
    dl_t      dl[DL_MAX];           /* this frame's sprites, far to near       */
    int       ndl;
    /* the player's car, coloured once per paint (car_cache) */
    tb_sprite_t car_spr[TB_NEAR_FRAMES];
    uint32_t *tail[TB_NEAR_FRAMES];  /* its tail lights: index << 8 | light     */
    uint16_t  ntail[TB_NEAR_FRAMES];
    int       cc_car;
    bool      cc_night, cc_valid;
    tb_paint_t cc_paint;
    tb_lut_t  lut_brake;
    /* the tunnel in view (prepare), as depths and screen rectangles */
    struct {
        bool  on, inside, far;          /* far: its exit beyond the view    */
        float zt0, zt1, hc;             /* entry, exit, ceiling over camera */
        int   wx0, wx1, wy0, wy1;       /* what is seen through its mouth   */
        int   ex0, ex1, ey0, ey1;       /* its exit                         */
    } tun;
    /* the frame being drawn, from tb_render_prepare() */
    float     camz, camx, camy;
    int       base, maxy, nfar, bgo, rival_n;
    bool      void_below;
    int       carx, cary, car_fr;
    bool      car_brake;
    tb_lut_t  lut_car;
    int16_t   car_head[TB_DRAW + 1];
    int16_t   car_next[TB_TRAFFIC + 1];
    uint32_t  prof[4];
    uint32_t  cyc[TB_PROF_N];   /* cycles per part, summed until read       */
};

/* --------------------------------------------------------------------------
 * Set-up
 * -------------------------------------------------------------------------- */

static void car_cache_free(tb_render_t *r);

tb_render_t *tb_render_new(void)
{
    tb_render_t *r = (tb_render_t *)tb_calloc(1, sizeof(tb_render_t));
    if (!r) return NULL;
    r->pano = (uint16_t *)tb_malloc((size_t)PANO_W * TB_BG_H * 2);
    if (!r->pano) {
        free(r);
        return NULL;
    }
    /* textures: 4 levels of noise, a little streaky along the road */
    uint32_t s = 0xC0FFEEu;
    for (int y = 0; y < TEX; y++) {
        for (int x = 0; x < TEX; x++) {
            uint32_t n = tb_rand(&s);
            int v = (int)(n & 7) + (int)((n >> 3) & 7);            /* 0..14, peaked */
            int l = v < 4 ? 0 : (v < 7 ? 1 : (v < 11 ? 2 : 3));
            r->tex_road[y * TEX + x] = (uint8_t)l;
            int g = (int)((n >> 8) & 3);
            if (((x + (int)((n >> 12) & 3)) & 7) == 0) g = 3;      /* tufts */
            r->tex_gnd[y * TEX + x] = (uint8_t)g;
        }
    }
    for (int i = 0; i < NSTARS; i++) {
        r->star_u[i] = (int16_t)(tb_rand(&s) % PANO_W);
        r->star_v[i] = (int16_t)(tb_rand(&s) % TB_H);
        r->star_b[i] = (uint8_t)(90 + tb_rand(&s) % 166);
    }
    tb_paint_t p;
    tb_paint_get(0, &p);
    tb_render_paint(r, &p, &p);
    return r;
}

void tb_render_free(tb_render_t *r)
{
    if (!r) return;
    car_cache_free(r);
    free(r->pano);
    free(r);
}

void tb_render_paint(tb_render_t *r, const tb_paint_t *p, const tb_paint_t *rival)
{
    r->paint_player = *p;
    r->paint_rival = *rival;
    memset(r->lut_key, 0xFF, sizeof r->lut_key);
}

static uint32_t sky_at(const tb_theme_t *th, int y)
{
    int t = y <= 0 ? 0 : (y >= TB_HOR ? 256 : y * 256 / TB_HOR);
    t = t * t >> 8;                         /* stays blue longer, pales low */
    t = (t + (y <= 0 ? 0 : (y >= TB_HOR ? 256 : y * 256 / TB_HOR))) >> 1;
    return tb_mix(th->sky_top, th->sky_hor, t);
}

void tb_render_stage(tb_render_t *r, const tb_track_t *t)
{
    r->trk = t;
    r->th = t->theme;
    r->bg_off = (float)(t->theme.bg_start % PANO_W);
    memset(r->lut_key, 0xFF, sizeof r->lut_key);
    const tb_theme_t *th = &r->th;
    const tb_sprite_t *bd = tb_art_backdrop();
    for (int j = 0; j < TB_BG_H; j++) {
        int y = TB_HOR - TB_BG_H + j;
        uint32_t c = sky_at(th, y);
        int cr = (int)(c >> 16) & 255, cg = (int)(c >> 8) & 255, cb = (int)c & 255;
        uint16_t *row = r->pano + (size_t)j * PANO_W;
        for (int x = 0; x < PANO_W; x++) row[x] = tb_dither(cr, cg, cb, x, j);
        if (bd && bd->px) {
            int by = j - (TB_BG_H - bd->h);
            if (by >= 0 && by < bd->h) {
                for (int x = 0; x < PANO_W; x++) {
                    int bx = x % bd->w;
                    size_t i = (size_t)by * bd->w + bx;
                    int a = bd->a ? bd->a[i] : 255;
                    if (a) row[x] = tb_blend(row[x], bd->px[i], a);
                }
            }
        }
    }
    if (th->stars) {
        for (int i = 0; i < NSTARS; i++) {
            int v = r->star_v[i] * TB_BG_H / TB_H;
            uint16_t *p = r->pano + (size_t)v * PANO_W + r->star_u[i];
            int rr, gg, bb;
            tb_unpack(*p, &rr, &gg, &bb);
            if (rr + gg + bb < 200) {
                int b = r->star_b[i];
                *p = tb_rgb(b, b, b > 200 ? 255 : b + 20);
            }
        }
    }
}

void tb_render_prof(const tb_render_t *r, uint32_t out[4])
{
    memcpy(out, r->prof, sizeof(r->prof));
}

uint32_t *tb_render_cycles(tb_render_t *r)
{
    return r->cyc;
}

/* --------------------------------------------------------------------------
 * The road, row by row
 * -------------------------------------------------------------------------- */

static inline void fill16(uint16_t *p, int x0, int x1, uint16_t c)
{
    if (x0 < 0) x0 = 0;
    if (x1 > TB_W) x1 = TB_W;
    if (x0 >= x1) return;
    uint16_t *d = p + x0;
    int n = x1 - x0;
    if (((uintptr_t)d & 2) && n) {
        *d++ = c;
        n--;
    }
    uint32_t c2 = (uint32_t)c | ((uint32_t)c << 16);
    uint32_t *d32 = (uint32_t *)d;
    for (int i = 0; i < (n >> 1); i++) d32[i] = c2;
    if (n & 1) d[n - 1] = c;
}

static inline void span_tex(uint16_t *row, int x0, int x1, const uint16_t pal[4], const uint8_t *trow,
                            int32_t u, int32_t du)
{
    if (x0 < 0) {
        u += du * (0 - x0);
        x0 = 0;
    }
    if (x1 > TB_W) x1 = TB_W;
    for (int x = x0; x < x1; x++, u += du) row[x] = pal[trow[(u >> 16) & (TEX - 1)]];
}


static uint16_t fogc(uint32_t c, uint32_t fog, int fk)
{
    return tb_hex(fk > 0 ? tb_mix(c, fog, fk) : c);
}

/* inside a tunnel the lamps light what is near: darker with depth, one
 * rule for the walls beside the road, the ceiling and the exit's frame */
static inline int tunnel_dk(float z)
{
    int dk = 256 - (int)(z * (800.0f / ((float)TB_DRAW * TB_SEG_LEN)));
    return dk < 60 ? 60 : (dk > 256 ? 256 : dk);
}

static void make_pal(const tb_render_t *r, rowpal_t *p, const tb_seg_t *s, int fk, float z)
{
    const tb_theme_t *th = &r->th;
    p->tunnel = (s->flags & SF_TUNNEL) != 0;
    int band = (s->flags & SF_DARK) ? 1 : 0;
    static const int dl[4] = { -9, -3, 3, 9 };
    uint32_t rc = th->road[band];
    for (int i = 0; i < 4; i++) {
        int d = dl[i];
        int cr = (int)(rc >> 16) + d, cg = (int)((rc >> 8) & 255) + d, cb = (int)(rc & 255) + d;
        uint32_t c = ((uint32_t)(cr < 0 ? 0 : cr) << 16) | ((uint32_t)(cg < 0 ? 0 : cg) << 8) | (uint32_t)(cb < 0 ? 0 : cb);
        p->road[i] = fogc(c, th->fog, fk);
    }
    for (int side = 0; side < 2; side++) {
        int kind = side ? s->gr : s->gl;
        uint16_t *dst = side ? p->gr : p->gl;
        uint32_t gc = th->ground[kind][band];
        for (int i = 0; i < 4; i++) {
            int d = kind == GR_SEA ? (i == 3 ? 40 : (i - 1) * 3) : dl[i] / 2;
            int cr = (int)(gc >> 16) + d, cg = (int)((gc >> 8) & 255) + d, cb = (int)(gc & 255) + d;
            if (cr < 0) cr = 0;
            if (cg < 0) cg = 0;
            if (cb < 0) cb = 0;
            if (cr > 255) cr = 255;
            if (cg > 255) cg = 255;
            if (cb > 255) cb = 255;
            dst[i] = fogc(((uint32_t)cr << 16) | ((uint32_t)cg << 8) | (uint32_t)cb, th->fog, fk);
        }
        if (side) p->void_r = kind == GR_VOID;
        else p->void_l = kind == GR_VOID;
    }
    p->rumble = fogc(th->rumble[band], th->neon ? 0x000000 : th->fog, th->neon ? fk / 3 : fk);
    p->line = fogc(th->line, th->fog, th->neon ? fk / 3 : fk);
    p->yellow = fogc(0xF2C230, th->fog, fk);
    if (p->tunnel) {
        /* inside: lamp-lit, darker with depth; the ground beside the road
         * is the walkway, the wall is painted over it past 8 m */
        int dk = tunnel_dk(z);
        uint16_t walk = tb_scale(tb_hex(tb_mix(th->tunnel_wall, 0x000000, 90)), dk);
        for (int i = 0; i < 4; i++) {
            p->road[i] = tb_scale(tb_hex(tb_mix(th->road[band], 0x201408, 80)), dk - (i - 2) * 6);
            p->gl[i] = p->gr[i] = walk;
        }
        p->rumble = tb_scale(tb_hex(th->tunnel_wall), dk);
        p->line = tb_scale(tb_hex(th->line), dk);
        p->wall = tb_scale(tb_hex(th->tunnel_wall), dk);
        p->void_l = p->void_r = false;
    }
}

/* a painted line at lateral position lx (metres) of width lw, inside [xa, xb) */
static inline void paint_line(uint16_t *row, float cx, float ppm, float lx, float lw, uint16_t c, int xa, int xb)
{
    float a = cx + (lx - lw * 0.5f) * ppm, b = cx + (lx + lw * 0.5f) * ppm;
    float wpx = b - a;
    if (wpx < 0.4f) return;
    int x0 = tb_ifloor(a + 0.5f), x1 = tb_ifloor(b + 0.5f);
    if (x1 <= x0) x1 = x0 + 1;
    if (wpx < 1.0f) {
        if (x0 >= xa && x0 < xb) row[x0] = tb_blend(row[x0], c, (int)(wpx * 255.0f));
        return;
    }
    fill16(row, x0 < xa ? xa : x0, x1 > xb ? xb : x1, c);
}

/* the texture span, clipped to [xa, xb) */
static inline void span_tex_in(uint16_t *row, int x0, int x1, const uint16_t pal[4], const uint8_t *trow,
                               int32_t u, int32_t du, int xa, int xb)
{
    if (x0 < xa) {
        u += du * (xa - x0);
        x0 = xa;
    }
    if (x1 > xb) x1 = xb;
    span_tex(row, x0, x1, pal, trow, u, du);
}

/* one row of ground, rumble, asphalt and lines, only columns [xa, xb): the
 * headlights paint the same row in pieces with brighter palettes */
static void road_row(tb_render_t *r, uint16_t *row, float cx, float w, float z, float zw,
                     const tb_seg_t *s, const rowpal_t *p, float hw, int xa, int xb)
{
    if (xa < 0) xa = 0;
    if (xb > TB_W) xb = TB_W;
    if (xa >= xb) return;
    float ppm = w / hw;                               /* px per metre on this row */
    float rw = TB_RUMBLE_W * ppm;
    int xl = tb_ifloor(cx - w + 0.5f), xr = tb_ifloor(cx + w + 0.5f);
    int rl = tb_ifloor(cx - w - rw + 0.5f), rr = tb_ifloor(cx + w + rw + 0.5f);
#define CL(v) ((v) < xa ? xa : ((v) > xb ? xb : (v)))
    /* the texture's density halves with distance, about one texel per
     * pixel or finer, so the grain never turns into blocks nor sparkles */
    bool near = z < TEX_NEAR;
    int pi = (int)ppm;
    int dens = 2;
    while (dens * 2 <= pi && dens < 64) dens <<= 1;     /* ~1 texel per pixel */
    int v = (int)(zw * (float)dens) & (TEX - 1);
    int32_t du = (int32_t)((float)dens * 65536.0f / ppm);
    /* ground */
    if (!p->void_l) {
        if (near) span_tex_in(row, 0, rl, p->gl, r->tex_gnd + v * TEX, (int32_t)((0.0f - cx) * (float)du), du, xa, xb);
        else fill16(row, CL(0), CL(rl), p->gl[1]);
    }
    if (!p->void_r) {
        if (near) span_tex_in(row, rr, TB_W, p->gr, r->tex_gnd + v * TEX, (int32_t)(((float)rr - cx) * (float)du), du, xa, xb);
        else fill16(row, CL(rr), CL(TB_W), p->gr[1]);
    }
    /* rumble strips and asphalt */
    fill16(row, CL(rl), CL(xl), p->rumble);
    fill16(row, CL(xr), CL(rr), p->rumble);
    if (near) span_tex_in(row, xl, xr, p->road, r->tex_road + v * TEX, 0, du, xa, xb);
    else fill16(row, CL(xl), CL(xr), p->road[1]);
    if (r->th.neon && rr - rl > 2) {
        /* a glowing fringe outside the rumble */
        int g = (int)(rw * 0.6f) + 1;
        for (int x = CL(rl - g); x < CL(rl); x++) row[x] = tb_blend(row[x], p->rumble, 120 * (x - rl + g) / g);
        for (int x = CL(rr); x < CL(rr + g); x++) row[x] = tb_blend(row[x], p->rumble, 120 * (rr + g - x) / g);
    }
#undef CL
    /* lines */
    bool dash = ((int)(zw * (1.0f / 3.0f)) & 3) == 0;   /* 3 m of 12 */
    if (s->lanes == 2) {
        paint_line(row, cx, ppm, -0.12f, 0.1f, p->yellow, xa, xb);
        paint_line(row, cx, ppm, 0.12f, 0.1f, p->yellow, xa, xb);
    } else if (dash) {
        paint_line(row, cx, ppm, -TB_LANE_W * 0.5f, 0.15f, p->line, xa, xb);
        paint_line(row, cx, ppm, TB_LANE_W * 0.5f, 0.15f, p->line, xa, xb);
    }
    paint_line(row, cx, ppm, -hw + 0.3f, 0.15f, p->line, xa, xb);
    paint_line(row, cx, ppm, hw - 0.3f, 0.15f, p->line, xa, xb);
}

static void pal_scaled(rowpal_t *o, const rowpal_t *p, int k)
{
    for (int i = 0; i < 4; i++) {
        o->road[i] = tb_scale(p->road[i], k);
        o->gl[i] = tb_scale(p->gl[i], k);
        o->gr[i] = tb_scale(p->gr[i], k);
    }
    o->rumble = tb_scale(p->rumble, k);
    o->line = tb_scale(p->line, k);
    o->yellow = tb_scale(p->yellow, k);
    o->void_l = p->void_l;
    o->void_r = p->void_r;
}

/* at night the headlights light a cone of the road ahead: the row is
 * painted in five pieces, dark, half, lit, half, dark, each from its own
 * palette (scaling every pixel after painting it was 13 ms a frame) */
static void road_row_lit(tb_render_t *r, uint16_t *row, float cx, float w, float z, float zw,
                         const tb_seg_t *s, const rowpal_t *p, float hw, float carx)
{
    if (z > 70.0f || z < 1.0f) {
        road_row(r, row, cx, w, z, zw, s, p, hw, 0, TB_W);
        return;
    }
    float ppm = w / hw;
    float k = 1.0f - z / 70.0f;
    int boost = 256 + (int)(k * k * 520.0f);
    float c = cx + carx * ppm;
    float hwm = 1.4f + z * 0.18f;
    int x0 = tb_ifloor(c - hwm * ppm), x1 = tb_ifloor(c + hwm * ppm);
    int e = (int)(0.5f * ppm) + 1;
    if (x1 - x0 < 2 * e + 2) e = (x1 - x0) / 3;
    rowpal_t mid, lit;
    pal_scaled(&mid, p, 256 + (boost - 256) / 2);
    pal_scaled(&lit, p, boost);
    road_row(r, row, cx, w, z, zw, s, p, hw, 0, x0);
    road_row(r, row, cx, w, z, zw, s, &mid, hw, x0, x0 + e);
    road_row(r, row, cx, w, z, zw, s, &lit, hw, x0 + e, x1 - e);
    road_row(r, row, cx, w, z, zw, s, &mid, hw, x1 - e, x1);
    road_row(r, row, cx, w, z, zw, s, p, hw, x1, TB_W);
}

/* --------------------------------------------------------------------------
 * Stand-ins while there is no pack
 * -------------------------------------------------------------------------- */

static float prop_height(int kind)
{
    switch (kind) {
    case PR_TOWER_BRICK: case PR_TOWER_GLASS: return 60.0f;
    case PR_BUTTE: return 60.0f;
    case PR_LIGHTHOUSE: case PR_CLIFF: return 16.0f;
    case PR_LAMP: case PR_LAMP_NIGHT: return 10.0f;
    case PR_PALM_TALL: return 14.0f;
    case PR_OVERPASS: case PR_GANTRY: case PR_CHECKPOINT: case PR_FINISH: return 8.0f;
    case PR_RING_GATE: return 16.0f;
    case PR_CONE: case PR_BARRIER: case PR_GUARDRAIL: case PR_SNOWBANK: return 0.9f;
    default: return 7.0f;
    }
}

static uint32_t prop_color(int kind)
{
    switch (kind) {
    case PR_TOWER_BRICK: return 0x9A5A44;
    case PR_TOWER_GLASS: return 0x5A8CB0;
    case PR_BUTTE: case PR_ROCK_RED: return 0xB05A30;
    case PR_LAMP: case PR_BARRIER: case PR_GUARDRAIL: return 0x9098A0;
    case PR_CHECKPOINT: return 0xF0C020;
    case PR_FINISH: return 0xF0F0F0;
    case PR_RING_GATE: case PR_CRYSTAL: case PR_BEACON: return 0x30E0FF;
    case PR_SNOWBANK: case PR_ROCK_SNOW: return 0xE0E8F0;
    default: return 0x2E7A30;
    }
}

static void stand_in_prop(tb_img_t *im, int kind, float x, float y, float ppm, int clip, bool span, uint16_t fog, int fk)
{
    uint16_t c = tb_blend(tb_hex(prop_color(kind)), fog, fk);
    float h = prop_height(kind) * ppm;
    tb_img_t cl = *im;
    if (clip < cl.cy1) cl.cy1 = (int16_t)clip;
    if (span) {
        float hw = (kind == PR_RING_GATE ? 8.0f : 13.0f) * ppm;
        tb_rect(&cl, (int)(x - hw), (int)(y - h), (int)(hw * 2), (int)(1.2f * ppm) + 1, c);
        tb_rect(&cl, (int)(x - hw), (int)(y - h), (int)(0.8f * ppm) + 1, (int)h, c);
        tb_rect(&cl, (int)(x + hw - 0.8f * ppm), (int)(y - h), (int)(0.8f * ppm) + 1, (int)h, c);
        return;
    }
    float w = (kind == PR_TOWER_BRICK || kind == PR_TOWER_GLASS || kind == PR_BUTTE ? 20.0f : 1.6f) * ppm;
    tb_rect(&cl, (int)(x - w * 0.5f), (int)(y - h), (int)w + 1, (int)h + 1, c);
}

static void stand_in_car(tb_img_t *im, const tb_lut_t *lut, float x, float y, float ppm, int clip, bool truck)
{
    tb_img_t cl = *im;
    if (clip < cl.cy1) cl.cy1 = (int16_t)clip;
    float w = (truck ? 2.5f : 1.9f) * ppm, h = (truck ? 3.3f : 1.3f) * ppm;
    int x0 = (int)(x - w * 0.5f);
    tb_rect(&cl, x0, (int)(y - h), (int)w + 1, (int)h + 1, lut->c[RG_PAINT_A][24]);
    if (!truck) tb_rect(&cl, x0 + (int)(w * 0.15f), (int)(y - h), (int)(w * 0.7f) + 1, (int)(h * 0.35f) + 1, lut->c[RG_GLASS][20]);
    tb_rect(&cl, x0, (int)(y - h * 0.55f), (int)(w * 0.18f) + 1, (int)(h * 0.15f) + 1, lut->c[RG_TAIL][28]);
    tb_rect(&cl, x0 + (int)(w * 0.82f), (int)(y - h * 0.55f), (int)(w * 0.18f) + 1, (int)(h * 0.15f) + 1, lut->c[RG_TAIL][28]);
    tb_rect(&cl, x0, (int)(y - h * 0.12f), (int)w + 1, (int)(h * 0.12f) + 1, lut->c[RG_TYRE][10]);
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */

/* The player's car in colour, once per car, paint and night: seven yaw
 * frames coloured through the LUT into plain sprites with opaque runs, so
 * drawing it is mostly memcpy (through the LUT it was 6.6 ms a frame on the
 * board). The tail lights stay a list of pixels, lit through a LUT when
 * braking. ~600 KB of PSRAM. */
static void car_cache_free(tb_render_t *r)
{
    for (int f = 0; f < TB_NEAR_FRAMES; f++) {
        tb_sprite_free(&r->car_spr[f]);
        free(r->tail[f]);
        r->tail[f] = NULL;
        r->ntail[f] = 0;
    }
    r->cc_valid = false;
}

void tb_render_car_drop(tb_render_t *r)
{
    car_cache_free(r);
}

static void car_cache(tb_render_t *r, int car)
{
    bool night = r->th.night;
    if (r->cc_valid && r->cc_car == car && r->cc_night == night &&
        !memcmp(&r->cc_paint, &r->paint_player, sizeof(tb_paint_t))) return;
    car_cache_free(r);
    /* the frames as ids and light, read again if they were dropped */
    tb_art_load_near(car);
    tb_lut_t lut;
    tb_lut_build(&lut, &r->paint_player, 0, 0, false, night);
    for (int f = 0; f < TB_NEAR_FRAMES; f++) {
        const tb_vspr_t *v = tb_art_near(car, f);
        if (!v || !v->px) continue;
        size_t np = (size_t)v->w * v->h;
        tb_sprite_t *sp = &r->car_spr[f];
        memset(sp, 0, sizeof(*sp));
        sp->px = (uint16_t *)tb_malloc(np * 2);
        sp->a = (uint8_t *)tb_malloc(np);
        if (!sp->px || !sp->a) {
            tb_sprite_free(sp);
            continue;
        }
        sp->w = v->w;
        sp->h = v->h;
        sp->ox = v->ox;
        sp->oy = v->oy;
        int nt = 0;
        for (size_t i = 0; i < np; i++) {
            uint16_t q = v->px[i];
            int a4 = (q >> 8) & 15;
            sp->a[i] = (uint8_t)(a4 * 17);
            sp->px[i] = a4 ? lut.c[q >> 12][(q & 255) >> 3] : 0;
            if (a4 && (q >> 12) == RG_TAIL) nt++;
        }
        r->tail[f] = nt ? (uint32_t *)tb_malloc((size_t)nt * 4) : NULL;
        if (r->tail[f]) {
            int k = 0;
            for (size_t i = 0; i < np; i++) {
                uint16_t q = v->px[i];
                if (((q >> 8) & 15) && (q >> 12) == RG_TAIL) r->tail[f][k++] = ((uint32_t)i << 8) | (q & 255);
            }
            r->ntail[f] = (uint16_t)nt;
        }
        tb_sprite_runs(sp);
        tb_yield();
    }
    r->cc_car = car;
    r->cc_night = night;
    r->cc_paint = r->paint_player;
    r->cc_valid = true;
    tb_lut_build(&r->lut_brake, &r->paint_player, 0, 0, true, night);
    /* coloured: the id + light frames are not needed until the paint changes */
    tb_art_drop_near();
}

/* the road's centre (screen x), its px per metre and the floor's row at a
 * camera depth, from the projected segments */
static void at_depth(const tb_render_t *r, float z, float *cx, float *k, float *fy)
{
    int n = 0;
    while (n < r->nfar && r->dr[n + 1].vis && r->dr[n + 1].z <= z) n++;
    const drawn_t *a = &r->dr[n], *b = &r->dr[n + 1];
    float f = 0;
    if (n < r->nfar && b->vis && b->z > a->z) f = (z - a->z) / (b->z - a->z);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    *cx = a->cx + (b->cx - a->cx) * f;
    *fy = a->sy + (b->sy - a->sy) * f;
    *k = TB_F / (z > NEAR_Z ? z : NEAR_Z);
}

static int clampi(float v, int lo, int hi)
{
    int i = tb_ifloor(v + 0.5f);
    return i < lo ? lo : (i > hi ? hi : i);
}

/* The nearest tunnel that is not behind: where its mouth and its exit fall
 * on the screen. Tunnels are flat inside (tb_track.c), so the ceiling is
 * one height over the camera. */
static void tunnel_prepare(tb_render_t *r, const tb_track_t *t, float camz, float camy)
{
    r->tun.on = false;
    float far = r->dr[r->nfar].z;
    for (int i = 0; i < t->ntunnels; i++) {
        float zt0 = (float)t->tunnel_s[i] * TB_SEG_LEN - camz;
        float zt1 = (float)t->tunnel_e[i] * TB_SEG_LEN - camz;
        if (zt1 <= NEAR_Z || zt0 >= far) continue;
        r->tun.on = true;
        r->tun.zt0 = zt0;
        r->tun.zt1 = zt1;
        r->tun.hc = tb_seg(t, t->tunnel_s[i])->y + TB_TUNNEL_H - camy;
        r->tun.inside = zt0 <= NEAR_Z;
        float cx, k, fy;
        if (r->tun.inside) {
            r->tun.wx0 = 0; r->tun.wx1 = TB_W; r->tun.wy0 = 0; r->tun.wy1 = TB_H;
        } else {
            at_depth(r, zt0, &cx, &k, &fy);
            r->tun.wx0 = clampi(cx - TB_TUNNEL_HW * k, 0, TB_W);
            r->tun.wx1 = clampi(cx + TB_TUNNEL_HW * k, 0, TB_W);
            r->tun.wy0 = clampi((float)TB_HOR - r->tun.hc * k, 0, TB_H);
            r->tun.wy1 = clampi(fy, 0, TB_H);
        }
        r->tun.far = zt1 > far;
        float ze = r->tun.far ? far : zt1;
        at_depth(r, ze, &cx, &k, &fy);
        r->tun.ex0 = clampi(cx - TB_TUNNEL_HW * k, 0, TB_W);
        r->tun.ex1 = clampi(cx + TB_TUNNEL_HW * k, 0, TB_W);
        r->tun.ey0 = clampi((float)TB_HOR - r->tun.hc * k, 0, TB_H);
        r->tun.ey1 = clampi(fy, 0, TB_H);
        return;
    }
}

/* The tunnel's ceiling and the walls above the road, over a band: a row
 * above the exit looks up at the ceiling at depth hc * F / (HOR - y), and
 * the ceiling spans the road's width there, walls either side; the rows of
 * the exit show the outside through it and walls around it. */
static void tunnel_band(tb_render_t *r, tb_img_t *im, int y0, int y1)
{
    const tb_theme_t *th = &r->th;
    int ya = y0 > r->tun.wy0 ? y0 : r->tun.wy0;
    int yb = y1 < r->tun.ey1 ? y1 : r->tun.ey1;
    if (yb > r->tun.wy1) yb = r->tun.wy1;
    int wx0 = r->tun.wx0, wx1 = r->tun.wx1;
    for (int y = ya; y < yb; y++) {
        uint16_t *row = im->px + (size_t)y * TB_W;
        if (y < r->tun.ey0 && y < TB_HOR && r->tun.hc > 0.1f) {
            float zc = r->tun.hc * TB_F / (float)(TB_HOR - y);
            float cx, k, fy;
            at_depth(r, zc, &cx, &k, &fy);
            int dk = tunnel_dk(zc);
            uint16_t wall = tb_scale(tb_hex(th->tunnel_wall), dk);
            uint16_t ceil = tb_scale(tb_hex(th->tunnel_ceiling), dk);
            int c0 = clampi(cx - TB_TUNNEL_HW * k, wx0, wx1), c1 = clampi(cx + TB_TUNNEL_HW * k, wx0, wx1);
            fill16(row, wx0, c0, wall);
            fill16(row, c1, wx1, wall);
            fill16(row, c0, c1, ceil);
            /* a lamp every 12 m: a strip down the middle third */
            float zw = r->camz + zc;
            if (zw - (float)tb_ifloor(zw * (1.0f / 12.0f)) * 12.0f < 1.4f) {
                int l0 = clampi(cx - TB_TUNNEL_HW * k * 0.3f, wx0, wx1), l1 = clampi(cx + TB_TUNNEL_HW * k * 0.3f, wx0, wx1);
                fill16(row, l0, l1, tb_scale(tb_hex(th->tunnel_lamp), dk + 60));
            }
        } else if (y >= r->tun.ey0) {
            int dk = tunnel_dk(r->tun.zt1);
            uint16_t wall = tb_scale(tb_hex(th->tunnel_wall), dk);
            fill16(row, wx0, r->tun.ex0 > wx0 ? r->tun.ex0 : wx0, wall);
            fill16(row, r->tun.ex1 < wx1 ? r->tun.ex1 : wx1, wx1, wall);
            if (r->tun.far) fill16(row, r->tun.ex0, r->tun.ex1, tb_scale(tb_hex(th->tunnel_ceiling), 60));
        }
    }
}

/* the frame's geometry, once: where every segment lands, which rows each
 * paints, the colours of the cars; the bands then only fill pixels */
void tb_render_prepare(tb_render_t *r, const tb_game_t *g, float dt)
{
    const tb_track_t *t = r->trk;
    const tb_theme_t *th = &r->th;
    if (!t) return;
    float camz = g->z - TB_CAM_BACK;
    /* 2 m over the road under the car, but never below the road under the
     * camera itself: in a dip that road is higher, and the nearest rows
     * then folded up over the farther ones */
    float ry_car = tb_track_y(t, g->z + 2.2f), ry_cam = tb_track_y(t, camz);
    float camy = (ry_car > ry_cam ? ry_car : ry_cam) + TB_CAM_H;
    float camx = g->cam_x;
    int base = tb_ifloor(camz / TB_SEG_LEN);
    float frac = camz / TB_SEG_LEN - (float)base;
    const tb_seg_t *s0 = tb_seg(t, base);
    r->camz = camz;
    r->camx = camx;
    r->camy = camy;
    r->base = base;
    r->void_below = th->stars && s0->gl == GR_VOID;

    /* the backdrop turns with the road */
    const tb_seg_t *sp = tb_seg(t, (int)(g->z / TB_SEG_LEN));
    r->bg_off += sp->curve * g->v * dt * BG_SCROLL;
    while (r->bg_off < 0) r->bg_off += PANO_W;
    while (r->bg_off >= PANO_W) r->bg_off -= PANO_W;
    r->bgo = (int)r->bg_off;

    for (int y = 0; y < TB_H; y++) r->rowseg[y] = -1;
    float x = 0, dx = -(s0->curve * frac);
    int maxy = TB_H;
    int nfar = 0;
    float fs = (float)th->fog_start * TB_SEG_LEN;
    for (int n = 0; n < TB_DRAW; n++) {
        const tb_seg_t *s = tb_seg(t, base + n);
        const tb_seg_t *sn = tb_seg(t, base + n + 1);
        float z1 = (float)(base + n) * TB_SEG_LEN - camz;
        float z2 = z1 + TB_SEG_LEN;
        float x1 = x, x2 = x + dx;
        float y1 = s->y, y2 = sn->y;
        x += dx;
        dx += s->curve;
        drawn_t *d = &r->dr[n];
        d->vis = false;
        d->clip = (int16_t)maxy;
        d->ya = d->ybot = 0;
        if (z2 <= NEAR_Z) continue;
        if (z1 < NEAR_Z) {
            float f = (NEAR_Z - z1) / TB_SEG_LEN;
            x1 += (x2 - x1) * f;
            y1 += (y2 - y1) * f;
            z1 = NEAR_Z;
        }
        float k1 = TB_F / z1, k2 = TB_F / z2;
        float sx1 = (float)TB_CX + (x1 - camx) * k1, sx2 = (float)TB_CX + (x2 - camx) * k2;
        float sy1 = (float)TB_HOR - (y1 - camy) * k1, sy2 = (float)TB_HOR - (y2 - camy) * k2;
        float hw = tb_road_hw(s);
        d->z = z1;
        d->scale = k1;
        d->cx = sx1;
        d->sy = sy1;
        d->sx2 = sx2;
        d->sy2 = sy2;
        d->w1 = hw * k1;
        d->w2 = hw * k2;
        d->iz1 = 1.0f / z1;
        d->iz2 = 1.0f / z2;
        d->hw = hw;
        d->vis = true;
        nfar = n;
        float zm = (z1 + z2) * 0.5f;
        int fk = zm <= fs ? 0 : (int)((zm - fs) * 256.0f / ((float)TB_DRAW * TB_SEG_LEN - fs));
        d->fk = (int16_t)(fk > 256 ? 256 : fk);
        int ytop = tb_ifloor(sy2 + 0.5f), ybot = tb_ifloor(sy1 + 0.5f);
        if (ytop >= maxy || ybot <= ytop) continue;
        if (ybot > maxy) ybot = maxy;
        int ya = ytop < 0 ? 0 : ytop;
        d->ya = (int16_t)ya;
        d->ybot = (int16_t)ybot;
        make_pal(r, &r->pal[n], s, d->fk, zm);
        for (int yy = ya; yy < ybot; yy++) r->rowseg[yy] = (int16_t)n;
        maxy = ya;
        if (maxy <= 0) break;
    }
    r->maxy = maxy;
    r->nfar = nfar;

    /* the cars in each segment, and their colours: a LUT is rebuilt only
     * when its fog step, the brake or the night changes */
    for (int n = 0; n <= nfar; n++) r->car_head[n] = -1;
    for (int i = 0; i < g->ntraffic; i++) {
        int n = tb_ifloor(g->traffic[i].z / TB_SEG_LEN) - base;
        if (n < 0 || n >= nfar) continue;
        r->car_next[i] = r->car_head[n];
        r->car_head[n] = (int16_t)i;
        const tb_traffic_t *c = &g->traffic[i];
        int fk = r->dr[n].fk > 230 ? 230 : r->dr[n].fk;
        if (th->neon) fk /= 2;
        fk &= ~7;
        uint32_t key = (uint32_t)c->paint | ((uint32_t)fk << 8) | ((uint32_t)c->braking << 17) |
                       ((uint32_t)th->night << 18) | ((uint32_t)c->ghost << 19);
        if (r->lut_key[i] != key) {
            tb_paint_t pt;
            tb_paint_traffic(c->paint, &pt);
            if (c->ghost) {
                /* a ghost: pale and cold all over, drawn see-through */
                for (int q = 1; q < RG_N; q++) pt.c[q] = 0xB8E6F4;
                pt.c[RG_GLASS] = 0x5C8898;
                pt.c[RG_TYRE] = 0x6C8C98;
                pt.c[RG_TAIL] = 0x9CFFD8;
            }
            tb_lut_build(&r->car_lut[i], &pt, th->fog, fk, c->braking, th->night);
            r->lut_key[i] = key;
        }
    }
    r->rival_n = -1;
    /* the other watch's car only when it is ahead of ours: one right behind
     * us drew itself huge over the bottom of the screen */
    if (g->rival_on && g->rival_z > g->z + 1.0f) {
        int n = tb_ifloor(g->rival_z / TB_SEG_LEN) - base;
        if (n >= 0 && n < nfar) {
            r->rival_n = n;
            tb_lut_build(&r->lut_rival, &r->paint_rival, th->fog, r->dr[n].fk > 230 ? 230 : r->dr[n].fk, false, th->night);
        }
    }

    /* the draw list, back to front, each with the rows it can touch */
    r->ndl = 0;
    for (int n = nfar - 1; n >= 0 && r->ndl < DL_MAX - 24; n--) {
        const drawn_t *d = &r->dr[n];
        const drawn_t *dn = &r->dr[n + 1];
        if (!d->vis || d->clip <= 0) continue;
        const tb_seg_t *s = tb_seg(t, base + n);
        int fk = d->fk > 230 ? 230 : d->fk;
        if (th->neon) fk /= 2;
        /* nothing on a segment reaches much below its road row: shadows and
         * the wheels under a vehicle's anchor, a metre and a half at most */
        int below = (int)(d->sy + d->scale * 1.5f + 4.0f);
        for (int p = 0; p < s->nprop && r->ndl < DL_MAX - 4; p++) {
            const tb_prop_t *pr = &t->prop[s->prop0 + p];
            float px = d->cx + (float)pr->x10 * 0.1f * d->scale;
            if (px < -600 || px > TB_W + 600) continue;
            const tb_mip_t *m = tb_art_prop(pr->kind);
            dl_t *e = &r->dl[r->ndl];
            e->x = px;
            e->y = d->sy;
            e->clip = d->clip;
            e->fk = (int16_t)fk;
            e->kind = pr->kind;
            e->mirror = (pr->flags & PF_MIRROR) != 0;
            e->span = (pr->flags & PF_SPAN) != 0;
            e->z = d->z;
            if (!m || !m->n) {
                e->type = DL_STANDIN_PROP;
                e->sc = d->scale;
                e->ytop = (int16_t)(d->sy - 70.0f * d->scale - 2.0f);
                e->ybot = (int16_t)below;
                r->ndl++;
                continue;
            }
            float sc = d->scale / m->ppm;
            float hpx = (float)m->lv[0].h * sc;
            /* under 4 px tall it is a speck: its draw costs more than it shows */
            if (hpx < 4.0f) continue;
            float top = d->sy - (float)m->lv[0].oy * sc;
            if (top >= (float)d->clip) continue;
#ifdef TB_HARNESS
            {
                extern double tb_stat_area[64];
                extern int tb_stat_n[64];
                tb_stat_area[pr->kind] += (double)(m->lv[0].w * sc * hpx);
                tb_stat_n[pr->kind]++;
            }
#endif
            e->type = DL_PROP;
            e->z = d->z;
            e->m = m;
            e->sc = sc;
            e->sh = fk < 200 ? tb_art_prop_shadow(pr->kind) : NULL;
            e->shsc = sc;
            e->ytop = (int16_t)(top < -32000 ? -32000 : top - 1.0f);
            e->ybot = (int16_t)below;
            r->ndl++;
        }
        for (int i = r->car_head[n]; i >= 0 && r->ndl < DL_MAX; i = r->car_next[i]) {
            const tb_traffic_t *c = &g->traffic[i];
            float f = c->z / TB_SEG_LEN - (float)(base + n);
            float cx = d->cx + (dn->cx - d->cx) * f;
            float sy = d->sy + (dn->sy - d->sy) * f;
            float sc = d->scale + (dn->scale - d->scale) * f;
            float sx = cx + c->x * sc;
            if (sx < -200 || sx > TB_W + 200) continue;
            dl_t *e = &r->dl[r->ndl++];
            e->x = sx;
            e->y = sy;
            e->z = c->z - camz;
            e->ppm = sc;
            e->clip = d->clip;
            e->lut = (int16_t)i;
            e->opa = c->ghost ? 120 : 255;
            e->kind = c->model;
            e->ytop = (int16_t)(sy - 4.0f * sc - 2.0f);
            e->ybot = (int16_t)(sy + 1.5f * sc + 4.0f);
            e->type = DL_VEH;
        }
        if (n == r->rival_n && r->ndl < DL_MAX) {
            float f = g->rival_z / TB_SEG_LEN - (float)(base + n);
            float cx = d->cx + (dn->cx - d->cx) * f;
            float sy = d->sy + (dn->sy - d->sy) * f;
            float sc = d->scale + (dn->scale - d->scale) * f;
            dl_t *e = &r->dl[r->ndl++];
            e->x = cx + g->rival_x * sc;
            e->y = sy;
            e->z = g->rival_z - camz;
            e->ppm = sc;
            e->clip = d->clip;
            e->lut = -1;
            e->opa = 150;
            e->kind = (uint8_t)g->rival_car;
            e->ytop = (int16_t)(sy - 4.0f * sc - 2.0f);
            e->ybot = (int16_t)(sy + 1.5f * sc + 4.0f);
            e->type = DL_VEH;
        }
    }
    /* a tunnel in view: its mouth and its exit, and what is seen through them */
    tunnel_prepare(r, t, camz, camy);
    for (int k = 0; k < r->ndl; k++) {
        dl_t *e = &r->dl[k];
        e->cx0 = 0;
        e->cx1 = TB_W;
        e->cy0 = 0;
        if (!r->tun.on || (e->type == DL_PROP && e->kind == PR_PORTAL)) continue;
        if (e->z > r->tun.zt1) {
            /* beyond the exit: only through it (and through the mouth) */
            e->cx0 = (int16_t)(r->tun.ex0 > r->tun.wx0 ? r->tun.ex0 : r->tun.wx0);
            e->cx1 = (int16_t)(r->tun.ex1 < r->tun.wx1 ? r->tun.ex1 : r->tun.wx1);
            e->cy0 = (int16_t)(r->tun.ey0 > r->tun.wy0 ? r->tun.ey0 : r->tun.wy0);
        } else if (e->z >= r->tun.zt0 - 1.0f) {
            e->cx0 = (int16_t)r->tun.wx0;
            e->cx1 = (int16_t)r->tun.wx1;
            e->cy0 = (int16_t)r->tun.wy0;
        }
    }

    /* the vehicles' view, sprite and shadow */
    for (int k = 0; k < r->ndl; k++) {
        dl_t *e = &r->dl[k];
        if (e->type != DL_VEH) continue;
        float ang = (e->x - (float)TB_CX) / TB_F;
        int view = ang < -0.12f ? 0 : (ang > 0.12f ? 2 : 1);
        const tb_vmip_t *vm = tb_art_far(e->kind, view);
        if (!vm || !vm->n) {
            e->type = DL_STANDIN_CAR;
            continue;
        }
        e->m = vm;
        e->sc = e->ppm / vm->ppm;
        e->sh = tb_art_far_shadow(e->kind, view);
        /* the shadows are stored at half the car's resolution */
        e->shsc = e->sh && e->sh->n && e->sh->ppm > 0 ? e->ppm / e->sh->ppm : e->sc;
    }

    /* the player's car */
    car_cache(r, g->car);
    r->car_brake = g->in_brake && g->v > 1.0f;
    float yaw = g->yaw;
    if (g->crash > 0) yaw = sinf(g->spin);
    int fr = (int)(yaw * 3.0f + (yaw >= 0 ? 0.5f : -0.5f)) + 3;
    r->car_fr = fr < 0 ? 0 : (fr > 6 ? 6 : fr);
    int bob = 0;
    if (g->bump > 0) bob = (int)(g->bump * 4.0f * sinf((float)tb_clock() * 0.06f));
    else if (g->v > 5.0f) bob = ((int)(g->z * 3.0f) & 7) == 0 ? 1 : 0;
    r->carx = TB_CX + (int)((g->x - g->cam_x) * TB_F / TB_CAM_BACK);
    r->cary = TB_CAR_Y + bob;
}

/* rows [y0, y1) of the frame; im's clip must be those rows (the band) */
void tb_render_band(tb_render_t *r, tb_img_t *im, const tb_game_t *g, int y0, int y1)
{
    const tb_track_t *t = r->trk;
    const tb_theme_t *th = &r->th;
    if (!t) return;
    int base = r->base;
    int bgo = r->bgo;
    uint32_t c0 = tb_cycles();

    /* space: nothing under the road but stars */
    if (r->void_below && y1 > TB_HOR) {
        for (int y = y0 > TB_HOR ? y0 : TB_HOR; y < y1; y++) memset(im->px + (size_t)y * TB_W, 0, TB_W * 2);
        for (int i = 0; i < NSTARS; i++) {
            int y = r->star_v[i];
            if (y < TB_HOR) y = TB_HOR + (y * (TB_H - TB_HOR)) / TB_HOR;
            if (y < y0 || y >= y1) continue;
            int x = (r->star_u[i] - bgo * 2) & (PANO_W - 1);
            if (x < TB_W) {
                int b = r->star_b[i];
                im->px[(size_t)y * TB_W + x] = tb_rgb(b, b, b);
            }
        }
    }

    /* the sky above what the road covered, far ground below the horizon */
    /* over the void the sky goes on behind the road down to the horizon:
     * the road's rows do not paint beside it, and those rows showed
     * whatever the buffer held before */
    int sky_end = r->void_below && r->maxy < TB_HOR ? TB_HOR : r->maxy;
    int ys = y1 < sky_end ? y1 : sky_end;
    for (int y = y0; y < ys; y++) {
        uint16_t *row = im->px + (size_t)y * TB_W;
        if (y < TB_HOR) {
            int j = y - (TB_HOR - TB_BG_H);
            const uint16_t *src = r->pano + (size_t)j * PANO_W;
            int n1 = PANO_W - bgo < TB_W ? PANO_W - bgo : TB_W;
            memcpy(row, src + bgo, (size_t)n1 * 2);
            if (n1 < TB_W) memcpy(row + n1, src, (size_t)(TB_W - n1) * 2);
        } else if (!r->void_below) {
            fill16(row, 0, TB_W, tb_hex(th->fog));
        }
    }

    uint32_t c1 = tb_cycles();
    r->cyc[TB_PROF_SKY] += c1 - c0;
    /* the road */
    for (int yy = y0 > r->maxy ? y0 : r->maxy; yy < y1; yy++) {
        int n = r->rowseg[yy];
        if (n < 0) continue;
        const drawn_t *d = &r->dr[n];
        const tb_seg_t *s = tb_seg(t, base + n);
        float span = d->sy - d->sy2;
        if (span < 0.001f) span = 0.001f;
        float tt = (d->sy - ((float)yy + 0.5f)) / span;
        if (tt < 0) tt = 0;
        if (tt > 1) tt = 1;
        float cx = d->cx + (d->sx2 - d->cx) * tt;
        float w = d->w1 + (d->w2 - d->w1) * tt;
        float z = 1.0f / (d->iz1 + (d->iz2 - d->iz1) * tt);
        uint16_t *row = im->px + (size_t)yy * TB_W;
        if (th->night) road_row_lit(r, row, cx, w, z, r->camz + z, s, &r->pal[n], d->hw, g->x);
        else road_row(r, row, cx, w, z, r->camz + z, s, &r->pal[n], d->hw, 0, TB_W);
        if (r->pal[n].tunnel) {
            /* the walls, 8 m either side */
            float wk = TB_TUNNEL_HW * w / d->hw;
            fill16(row, 0, tb_ifloor(cx - wk + 0.5f), r->pal[n].wall);
            fill16(row, tb_ifloor(cx + wk + 0.5f), TB_W, r->pal[n].wall);
        }
    }
    if (r->tun.on) tunnel_band(r, im, y0, y1);

    uint32_t c2 = tb_cycles();
    r->cyc[TB_PROF_ROAD] += c2 - c1;
    uint32_t cprops = 0;
    /* the sprites, back to front */
    uint16_t fog565 = tb_hex(th->fog);
#ifdef TB_HARNESS
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("TB_DBG") ? atoi(getenv("TB_DBG")) : 0;
    if (dbg == 1) return;
#endif
    tb_img_t whole = *im;
    for (int k = 0; k < r->ndl; k++) {
        const dl_t *e = &r->dl[k];
        if (e->ytop >= y1 || e->ybot < y0 || e->clip <= y0) continue;
        /* seen through a tunnel's mouth: clipped to it */
        *im = whole;
        if (e->cx0 > im->cx0) im->cx0 = e->cx0;
        if (e->cx1 < im->cx1) im->cx1 = e->cx1;
        if (e->cy0 > im->cy0) im->cy0 = e->cy0;
        if (im->cx0 >= im->cx1 || im->cy0 >= im->cy1) continue;
        switch (e->type) {
        case DL_PROP: {
            uint32_t cp = tb_cycles();
            if (e->sh && e->sh->n) tb_mip_shadow(im, e->sh, e->x, e->y, e->shsc, e->clip, 160);
            tb_mip_draw(im, (const tb_mip_t *)e->m, e->x, e->y, e->sc, e->clip, e->mirror, fog565, e->fk, 256);
            cprops += tb_cycles() - cp;
            break;
        }
        case DL_VEH: {
            const tb_lut_t *lut = e->lut >= 0 ? &r->car_lut[e->lut] : &r->lut_rival;
            if (e->sh && e->sh->n) tb_mip_shadow(im, e->sh, e->x, e->y, e->shsc, e->clip, 200);
            tb_vmip_draw(im, (const tb_vmip_t *)e->m, lut, e->x, e->y, e->sc, e->clip, e->opa);
            break;
        }
        case DL_STANDIN_PROP:
            stand_in_prop(im, e->kind, e->x, e->y, e->sc, e->clip, e->span, fog565, e->fk);
            break;
        case DL_STANDIN_CAR: {
            const tb_lut_t *lut = e->lut >= 0 ? &r->car_lut[e->lut] : &r->lut_rival;
            stand_in_car(im, lut, e->x, e->y, e->ppm, e->clip, e->kind == VH_TRUCK);
            break;
        }
        }
    }
    *im = whole;

    uint32_t c3 = tb_cycles();
    r->cyc[TB_PROF_PROPS] += cprops;
    r->cyc[TB_PROF_TRAFFIC] += (c3 - c2) - cprops;
    /* the player's car */
    int carx = r->carx, cary = r->cary;
    const tb_sprite_t *cs = r->cc_valid ? &r->car_spr[r->car_fr] : NULL;
    if (cs && cs->px) {
        if (cary - cs->oy >= y1 + 40 || cary - cs->oy + cs->h + 40 < y0) {
            r->cyc[TB_PROF_CAR] += tb_cycles() - c3;
            return;
        }
        const tb_sprite_t *sh = tb_art_near_shadow(g->car, r->car_fr);
        if (sh && sh->a) {
            int sx0 = carx - sh->ox, sy0 = TB_CAR_Y - sh->oy;
            for (int yy = 0; yy < sh->h; yy++) {
                int y = sy0 + yy;
                if (y < im->cy0 || y >= im->cy1) continue;
                uint16_t *row = im->px + (size_t)y * TB_W;
                const uint8_t *a = sh->a + (size_t)yy * sh->w;
                int xa = sx0 < 0 ? -sx0 : 0, xb = sx0 + sh->w > TB_W ? TB_W - sx0 : sh->w;
                for (int xx = xa; xx < xb; xx++) {
                    if (a[xx] < 6) continue;
                    row[sx0 + xx] = tb_darken(row[sx0 + xx], 256 - (a[xx] * 220 >> 8));
                }
            }
        }
        tb_sprite(im, cs, carx, cary);
        if (r->car_brake && r->tail[r->car_fr]) {
            /* the brake lights, over the coloured car */
            const uint32_t *tl = r->tail[r->car_fr];
            int sx0 = carx - cs->ox, sy0 = cary - cs->oy;
            for (int k = 0; k < r->ntail[r->car_fr]; k++) {
                uint32_t i = tl[k] >> 8;
                int x = sx0 + (int)(i % (uint32_t)cs->w), y = sy0 + (int)(i / (uint32_t)cs->w);
                if (y < im->cy0 || y >= im->cy1 || x < 0 || x >= TB_W) continue;
                uint16_t *p = im->px + (size_t)y * TB_W + x;
                *p = tb_blend(*p, r->lut_brake.c[RG_TAIL][(tl[k] & 255) >> 3], cs->a[i]);
            }
        }
        r->cyc[TB_PROF_CAR] += tb_cycles() - c3;
    } else {
        tb_lut_build(&r->lut_car, &r->paint_player, 0, 0, false, th->night);
        tb_shadow(im, carx, TB_CAR_Y, 100, 14, 150);
        stand_in_car(im, &r->lut_car, (float)carx, (float)cary, TB_F / TB_CAM_BACK, TB_H, false);
    }
    if (th->night) {
        tb_glow(im, carx - 70, cary - 55, 18, 0xFF2010, g->in_brake ? 200 : 90);
        tb_glow(im, carx + 70, cary - 55, 18, 0xFF2010, g->in_brake ? 200 : 90);
    }
}

void tb_render_world(tb_render_t *r, tb_img_t *im, const tb_game_t *g, float dt)
{
    uint32_t t0 = tb_clock();
    tb_render_prepare(r, g, dt);
    uint32_t t1 = tb_clock();
    tb_render_band(r, im, g, 0, TB_H);
    r->prof[0] = t1 - t0;
    r->prof[1] = tb_clock() - t1;
}
