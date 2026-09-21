/*
 * NEON SNAKES - sprites drawn by code (see ns_art.h)
 *
 * The only file of the game with floats, and only while a mode opens: some
 * 130 sprites of 2x2 cells, a few shapes each, one distance per shape and
 * pixel. After that everything is integer copies.
 */
#include "ns_art.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Shapes                                                                      */

enum { P_DISC, P_CAPS, P_CONE, P_ELL, P_ARC, P_HALF };
enum { S_TUBE, S_NEON, S_SOLID, S_DARK };

typedef struct {
    uint8_t  type, style;
    float    ax, ay, bx, by;    /* points, or centre + extra parameters      */
    float    ra, rb;            /* radii                                     */
    float    ang;               /* ellipse rotation / arc start, in degrees  */
    uint32_t rgb;
    float    fill;              /* S_NEON: how lit the inside is (0..1)      */
} prim_t;

#define MAXP 24

typedef struct {
    prim_t p[MAXP];
    int    n;
} shape_t;

static prim_t *add(shape_t *s, uint8_t type, uint8_t style, uint32_t rgb)
{
    prim_t *p = &s->p[s->n < MAXP ? s->n++ : MAXP - 1];
    memset(p, 0, sizeof *p);
    p->type  = type;
    p->style = style;
    p->rgb   = rgb;
    p->fill  = 0.3f;
    return p;
}

static void disc(shape_t *s, uint8_t st, uint32_t rgb, float x, float y, float r)
{
    prim_t *p = add(s, P_DISC, st, rgb);
    p->ax = x; p->ay = y; p->ra = r;
}

static void caps(shape_t *s, uint8_t st, uint32_t rgb, float x0, float y0, float x1, float y1, float r)
{
    prim_t *p = add(s, P_CAPS, st, rgb);
    p->ax = x0; p->ay = y0; p->bx = x1; p->by = y1; p->ra = r;
}

static void cone(shape_t *s, uint8_t st, uint32_t rgb, float x0, float y0, float r0,
                 float x1, float y1, float r1)
{
    prim_t *p = add(s, P_CONE, st, rgb);
    p->ax = x0; p->ay = y0; p->ra = r0; p->bx = x1; p->by = y1; p->rb = r1;
}

static void ell(shape_t *s, uint8_t st, uint32_t rgb, float x, float y, float rx, float ry, float deg)
{
    prim_t *p = add(s, P_ELL, st, rgb);
    p->ax = x; p->ay = y; p->ra = rx; p->rb = ry; p->ang = deg;
}

/* An arc of radius R around (x, y) from a0 to a1 degrees (clockwise from
 * three o'clock, y down), half width w, round ends. */
static void arc(shape_t *s, uint8_t st, uint32_t rgb, float x, float y, float R, float w,
                float a0, float a1)
{
    prim_t *p = add(s, P_ARC, st, rgb);
    p->ax = x; p->ay = y; p->bx = R; p->ra = w; p->ang = a0; p->by = a1;
}

/* The lower half of a disc: a melon slice. */
static void half(shape_t *s, uint8_t st, uint32_t rgb, float x, float y, float r)
{
    prim_t *p = add(s, P_HALF, st, rgb);
    p->ax = x; p->ay = y; p->ra = r;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static float seg_dist(float px, float py, float ax, float ay, float bx, float by, float *t_out)
{
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1) : 0;
    float qx = ax + dx * t - px, qy = ay + dy * t - py;
    if (t_out) *t_out = t;
    return sqrtf(qx * qx + qy * qy);
}

/* Signed distance in CELL units, and the radius that counts as "the tube"
 * at that point (for the white core). */
static float sdf(const prim_t *p, float u, float v, float *radius)
{
    switch (p->type) {
    case P_DISC: {
        float dx = u - p->ax, dy = v - p->ay;
        *radius = p->ra;
        return sqrtf(dx * dx + dy * dy) - p->ra;
    }
    case P_CAPS:
        *radius = p->ra;
        return seg_dist(u, v, p->ax, p->ay, p->bx, p->by, NULL) - p->ra;
    case P_CONE: {
        float t;
        float d = seg_dist(u, v, p->ax, p->ay, p->bx, p->by, &t);
        float r = p->ra + (p->rb - p->ra) * t;
        *radius = r;
        return d - r;
    }
    case P_ELL: {
        float a = p->ang * 0.0174533f;
        float c = cosf(a), s = sinf(a);
        float dx = u - p->ax, dy = v - p->ay;
        float x = dx * c + dy * s, y = -dx * s + dy * c;
        float k = sqrtf((x * x) / (p->ra * p->ra) + (y * y) / (p->rb * p->rb));
        float m = p->ra < p->rb ? p->ra : p->rb;
        *radius = m;
        return (k - 1.0f) * m;
    }
    case P_ARC: {
        float dx = u - p->ax, dy = v - p->ay;
        float ang = atan2f(dy, dx) * 57.29578f;
        if (ang < 0) ang += 360.0f;
        *radius = p->ra;
        if (ang >= p->ang && ang <= p->by) {
            return fabsf(sqrtf(dx * dx + dy * dy) - p->bx) - p->ra;
        }
        float a0 = p->ang * 0.0174533f, a1 = p->by * 0.0174533f;
        float ex0 = p->ax + cosf(a0) * p->bx, ey0 = p->ay + sinf(a0) * p->bx;
        float ex1 = p->ax + cosf(a1) * p->bx, ey1 = p->ay + sinf(a1) * p->bx;
        float d0 = sqrtf((u - ex0) * (u - ex0) + (v - ey0) * (v - ey0));
        float d1 = sqrtf((u - ex1) * (u - ex1) + (v - ey1) * (v - ey1));
        return (d0 < d1 ? d0 : d1) - p->ra;
    }
    case P_HALF: {
        float dx = u - p->ax, dy = v - p->ay;
        *radius = p->ra;
        float dd = sqrtf(dx * dx + dy * dy) - p->ra;
        float cut = -dy;                    /* keep y >= centre */
        return dd > cut ? dd : cut;
    }
    }
    *radius = 1;
    return 1e9f;
}

/* -------------------------------------------------------------------------- */
/* Shading                                                                     */

typedef struct { float r, g, b; } rgbf_t;

static rgbf_t unpack(uint32_t rgb)
{
    rgbf_t c = { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
    return c;
}

static rgbf_t mixc(rgbf_t a, rgbf_t b, float t)
{
    rgbf_t c = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return c;
}

static rgbf_t scalec(rgbf_t a, float k)
{
    rgbf_t c = { a.r * k, a.g * k, a.b * k };
    return c;
}

static void maxc(rgbf_t *acc, rgbf_t c)
{
    if (c.r > acc->r) acc->r = c.r;
    if (c.g > acc->g) acc->g = c.g;
    if (c.b > acc->b) acc->b = c.b;
}

static const rgbf_t WHITE = { 1, 1, 1 };

/* Draws a shape list into an S x S sprite whose middle cell is C px, with the
 * glow scaled by 'glow'. */
static void render(uint16_t *out, const shape_t *sh, int C, int S, float glow, float zoom)
{
    /* the glow's reach, px: half a cell, less when the shape is zoomed so
     * that shape + glow still end inside the sprite */
    const float G    = C * (zoom > 1.0f ? 0.5f - (zoom - 1.0f) * 0.3f : 0.5f);
    const float rimw = C * 0.09f > 1.2f ? C * 0.09f : 1.2f;
    for (int py = 0; py < S; py++) {
        for (int px = 0; px < S; px++) {
            float u = (px + 0.5f - S * 0.5f) / (C * zoom);
            float v = (py + 0.5f - S * 0.5f) / (C * zoom);
            rgbf_t acc = { 0, 0, 0 };
            for (int i = 0; i < sh->n; i++) {
                const prim_t *p = &sh->p[i];
                float rad;
                float sd = sdf(p, u, v, &rad) * C * zoom;  /* px */
                if (sd >= G) continue;
                rgbf_t col = unpack(p->rgb);
                float cover = clampf(0.5f - sd, 0, 1);
                rgbf_t in = { 0, 0, 0 };
                if (cover > 0) {
                    switch (p->style) {
                    case S_TUBE: {
                        float t = clampf(-sd / (rad * C * zoom), 0, 1);
                        in = mixc(col, WHITE, 0.78f * t * t);
                        break;
                    }
                    case S_NEON: {
                        rgbf_t rim  = mixc(col, WHITE, 0.35f);
                        rgbf_t body = scalec(col, p->fill);
                        float k = clampf(-sd - rimw + 0.5f, 0, 1);
                        in = mixc(rim, body, k);
                        break;
                    }
                    case S_SOLID:
                        in = mixc(col, WHITE, 0.2f);
                        break;
                    case S_DARK:
                        acc = mixc(acc, col, cover);
                        continue;
                    }
                    in = scalec(in, cover);
                }
                if (p->style == S_DARK) continue;
                if (sd > 0) {
                    float f = 1.0f - sd / G;
                    float k = (p->style == S_SOLID ? 0.35f : 0.55f) * glow * f * f;
                    maxc(&acc, scalec(col, k));
                }
                maxc(&acc, in);
            }
            uint32_t r = (uint32_t)(clampf(acc.r, 0, 1) * 255.0f + 0.5f);
            uint32_t g = (uint32_t)(clampf(acc.g, 0, 1) * 255.0f + 0.5f);
            uint32_t b = (uint32_t)(clampf(acc.b, 0, 1) * 255.0f + 0.5f);
            out[py * S + px] = ns_565((r << 16) | (g << 8) | b);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Snakes                                                                      */

typedef struct { uint32_t a, b; } duo_t;

static const duo_t SNAKE_RGB[NS_COLOURS] = {
    { 0x19F5FF, 0x6A7CFF },     /* cyan and electric blue   */
    { 0xFF2FD0, 0xB65CFF },     /* magenta and violet       */
    { 0x8CFF2E, 0x2EF0A8 },     /* lime and mint            */
    { 0xFFC21A, 0xFF6A3A },     /* amber and red-orange     */
    { 0xFFFFFF, 0xC8C8FF },     /* the flash                */
};

uint32_t ns_snake_rgb(int colour) { return SNAKE_RGB[colour % NS_COLOURS].a; }

/* Rotates a canonical point (facing up) to face 'dir', clockwise. */
static void rot(int dir, float x, float y, float *ox, float *oy)
{
    for (int k = 0; k < (dir & 3); k++) {
        float t = x;
        x = -y;
        y = t;
    }
    *ox = x;
    *oy = y;
}

static const float BODY_R = 0.34f;

/* A stripe: the tube in one colour with a bright bead of the other. */
static void body_shape(shape_t *s, int mask, duo_t c, int stripe)
{
    uint32_t tube = stripe ? c.b : c.a, bead = stripe ? c.a : c.b;
    for (int d = 0; d < 4; d++) {
        if (!(mask & (1 << d))) continue;
        caps(s, S_TUBE, tube, 0, 0, NS_DX[d] * 0.52f, NS_DY[d] * 0.52f, BODY_R);
    }
    disc(s, S_SOLID, bead, 0, 0, 0.11f);
}

static void tail_shape(shape_t *s, int dir_to_body, duo_t c, int stripe)
{
    uint32_t tube = stripe ? c.b : c.a;
    int d = dir_to_body;
    cone(s, S_TUBE, tube, NS_DX[d] * 0.52f, NS_DY[d] * 0.52f, BODY_R,
         -NS_DX[d] * 0.3f, -NS_DY[d] * 0.3f, 0.09f);
}

static void head_shape(shape_t *s, int facing, duo_t c)
{
    float x, y, x2, y2;
    /* neck back to the body, then the skull a bit forward */
    rot(facing, 0, 0.52f, &x, &y);
    caps(s, S_TUBE, c.a, 0, 0, x, y, BODY_R);
    rot(facing, 0, -0.05f, &x, &y);
    disc(s, S_TUBE, c.a, x, y, 0.49f);
    /* the tongue, forked */
    rot(facing, 0, -0.52f, &x, &y);
    rot(facing, 0, -0.7f, &x2, &y2);
    caps(s, S_SOLID, 0xFF3060, x, y, x2, y2, 0.04f);
    float fx, fy;
    rot(facing, -0.08f, -0.8f, &fx, &fy);
    caps(s, S_SOLID, 0xFF3060, x2, y2, fx, fy, 0.03f);
    rot(facing, 0.08f, -0.8f, &fx, &fy);
    caps(s, S_SOLID, 0xFF3060, x2, y2, fx, fy, 0.03f);
    /* eyes: white, then the pupils painted over */
    for (int side = -1; side <= 1; side += 2) {
        rot(facing, side * 0.22f, -0.14f, &x, &y);
        disc(s, S_SOLID, 0xFFFFFF, x, y, 0.14f);
    }
    for (int side = -1; side <= 1; side += 2) {
        rot(facing, side * 0.22f, -0.2f, &x, &y);
        disc(s, S_DARK, 0x000010, x, y, 0.075f);
    }
}

int ns_body_shape(int mask)
{
    switch (mask) {
    case 1 | 4: return 0;
    case 2 | 8: return 1;
    case 1 | 2: return 2;
    case 2 | 4: return 3;
    case 4 | 8: return 4;
    case 8 | 1: return 5;
    }
    return -1;
}

static const int BODY_MASKS[6] = { 1 | 4, 2 | 8, 1 | 2, 2 | 4, 4 | 8, 8 | 1 };

/* -------------------------------------------------------------------------- */
/* Fruits                                                                      */

#define LEAF    0x3CFF6A
#define STEM    0x5CFF5C

static const uint32_t FRUIT_RGB[NS_FRUIT_COUNT] = {
    0xFF2040, 0x7CFF4A, 0xFFE63A, 0xB44BFF, 0xFF2D6A, 0xFF8A1A, 0xFF3A6A,
};

uint32_t ns_fruit_rgb(int kind)
{
    if (kind < NS_FRUIT_COUNT) return FRUIT_RGB[kind];
    return ns_snake_rgb(kind - NS_SPARK_0);
}

static void fruit_shape(shape_t *s, int kind)
{
    switch (kind) {
    case NS_FRUIT_CHERRY:
        caps(s, S_SOLID, STEM, -0.15f, 0.08f, 0.04f, -0.38f, 0.03f);
        caps(s, S_SOLID, STEM, 0.18f, 0.12f, 0.04f, -0.38f, 0.03f);
        ell(s, S_NEON, LEAF, 0.2f, -0.34f, 0.15f, 0.06f, -20);
        disc(s, S_NEON, 0xFF2040, -0.16f, 0.16f, 0.2f);
        disc(s, S_NEON, 0xFF2040, 0.18f, 0.21f, 0.19f);
        disc(s, S_SOLID, 0xFFD0DA, -0.23f, 0.09f, 0.045f);
        disc(s, S_SOLID, 0xFFD0DA, 0.11f, 0.14f, 0.04f);
        break;
    case NS_FRUIT_APPLE:
        disc(s, S_NEON, 0x7CFF4A, -0.1f, 0.07f, 0.3f);
        disc(s, S_NEON, 0x7CFF4A, 0.1f, 0.07f, 0.3f);
        caps(s, S_SOLID, 0xFF9A3A, 0, -0.18f, 0.05f, -0.38f, 0.035f);
        ell(s, S_NEON, LEAF, 0.18f, -0.33f, 0.13f, 0.055f, -25);
        disc(s, S_SOLID, 0xF0FFE0, -0.17f, -0.05f, 0.06f);
        break;
    case NS_FRUIT_BANANA:
        arc(s, S_NEON, 0xFFE63A, 0.08f, -0.22f, 0.4f, 0.11f, 35, 165);
        disc(s, S_SOLID, 0xA0702A, 0.08f + 0.4f * 0.819f, -0.22f + 0.4f * 0.574f, 0.05f);
        disc(s, S_SOLID, 0xA0702A, 0.08f - 0.4f * 0.966f, -0.22f + 0.4f * 0.259f, 0.05f);
        break;
    case NS_FRUIT_GRAPES:
        caps(s, S_SOLID, STEM, 0, -0.2f, 0.06f, -0.4f, 0.03f);
        ell(s, S_NEON, LEAF, -0.15f, -0.33f, 0.13f, 0.055f, 20);
        disc(s, S_NEON, 0xB44BFF, -0.2f, -0.1f, 0.12f);
        disc(s, S_NEON, 0xB44BFF, 0.0f, -0.1f, 0.12f);
        disc(s, S_NEON, 0xB44BFF, 0.2f, -0.1f, 0.12f);
        disc(s, S_NEON, 0xB44BFF, -0.1f, 0.09f, 0.12f);
        disc(s, S_NEON, 0xB44BFF, 0.1f, 0.09f, 0.12f);
        disc(s, S_NEON, 0xB44BFF, 0.0f, 0.28f, 0.12f);
        break;
    case NS_FRUIT_STRAWBERRY:
        cone(s, S_NEON, 0xFF2D6A, 0, -0.1f, 0.3f, 0, 0.32f, 0.07f);
        disc(s, S_SOLID, 0xFFE14A, -0.13f, -0.02f, 0.032f);
        disc(s, S_SOLID, 0xFFE14A, 0.11f, -0.04f, 0.032f);
        disc(s, S_SOLID, 0xFFE14A, 0.0f, 0.09f, 0.032f);
        disc(s, S_SOLID, 0xFFE14A, -0.07f, 0.19f, 0.03f);
        disc(s, S_SOLID, 0xFFE14A, 0.08f, 0.17f, 0.03f);
        ell(s, S_NEON, LEAF, -0.14f, -0.31f, 0.14f, 0.05f, 25);
        ell(s, S_NEON, LEAF, 0.14f, -0.31f, 0.14f, 0.05f, -25);
        ell(s, S_NEON, LEAF, 0.0f, -0.35f, 0.05f, 0.11f, 0);
        break;
    case NS_FRUIT_ORANGE:
        disc(s, S_NEON, 0xFF8A1A, 0, 0.05f, 0.35f);
        caps(s, S_SOLID, 0xFFB050, -0.1f, 0.3f, 0.1f, 0.3f, 0.015f);
        ell(s, S_NEON, LEAF, 0.13f, -0.34f, 0.13f, 0.055f, -20);
        disc(s, S_SOLID, 0x3CFF6A, 0.0f, -0.28f, 0.04f);
        disc(s, S_SOLID, 0xFFE8C0, -0.14f, -0.08f, 0.055f);
        break;
    case NS_FRUIT_MELON: {
        half(s, S_NEON, 0x3CFF6A, 0, -0.14f, 0.44f);
        s->p[s->n - 1].fill = 0.5f;
        half(s, S_NEON, 0xFF3A6A, 0, -0.14f, 0.34f);
        s->p[s->n - 1].fill = 0.6f;
        disc(s, S_DARK, 0x100008, -0.13f, 0.0f, 0.035f);
        disc(s, S_DARK, 0x100008, 0.13f, 0.0f, 0.035f);
        disc(s, S_DARK, 0x100008, 0.0f, 0.09f, 0.035f);
        break;
    }
    default: {                  /* a spark: a four-pointed star of its snake */
        uint32_t c = ns_snake_rgb(kind - NS_SPARK_0);
        ell(s, S_SOLID, c, 0, 0, 0.3f, 0.06f, 0);
        ell(s, S_SOLID, c, 0, 0, 0.06f, 0.3f, 0);
        disc(s, S_TUBE, c, 0, 0, 0.12f);
        break;
    }
    }
}

/* -------------------------------------------------------------------------- */
/* Building                                                                    */

static const float PULSE[NS_PULSES] = { 0.55f, 0.85f, 1.15f };
#define FRUIT_ZOOM 1.32f

bool ns_art_build(ns_art_t *a, int cell, int colours)
{
    memset(a, 0, sizeof *a);
    if (colours > NS_MAX_SNAKES) colours = NS_MAX_SNAKES;
    a->cell    = cell;
    a->size    = cell * 2;
    a->colours = (uint8_t)colours;
    size_t one = (size_t)a->size * a->size;
    size_t count = (size_t)(colours + 1) * NS_SNAKE_SPRITES + (size_t)NS_KIND_COUNT * NS_PULSES;
    a->block = malloc(one * count * sizeof(uint16_t));
    if (!a->block) {
        return false;
    }
    uint16_t *next = a->block;
    shape_t sh;
    for (int ci = 0; ci <= colours; ci++) {
        int colour = ci == colours ? NS_WHITE : ci;
        duo_t c = SNAKE_RGB[colour];
        for (int k = 0; k < NS_SNAKE_SPRITES; k++) {
            sh.n = 0;
            if (k < 12)      body_shape(&sh, BODY_MASKS[k % 6], c, k / 6);
            else if (k < 20) tail_shape(&sh, (k - 12) % 4, c, (k - 12) / 4);
            else             head_shape(&sh, k - 20, c);
            render(next, &sh, cell, a->size, 1.0f, 1.0f);
            a->snake[colour][k] = next;
            next += one;
        }
    }
    for (int kind = 0; kind < NS_KIND_COUNT; kind++) {
        sh.n = 0;
        fruit_shape(&sh, kind);
        for (int p = 0; p < NS_PULSES; p++) {
            /* fruits are drawn a third bigger than their cell: at 10 px a
             * cherry the size of a body segment does not read */
            render(next, &sh, cell, a->size, PULSE[p], FRUIT_ZOOM);
            a->fruit[kind][p] = next;
            next += one;
        }
    }
    return true;
}

void ns_art_free(ns_art_t *a)
{
    free(a->block);
    memset(a, 0, sizeof *a);
}

/* -------------------------------------------------------------------------- */
/* The title: tube letters on a 4 x 6 grid                                      */

typedef struct { int8_t x0, y0, x1, y1; } stroke_t;

/* Polylines split into strokes; half units allowed through x2. Coordinates
 * are doubled: 0..8 across, 0..12 down. */
static const stroke_t L_N[] = { {0,12,0,0}, {0,0,8,12}, {8,12,8,0} };
static const stroke_t L_E[] = { {8,0,0,0}, {0,0,0,12}, {0,12,8,12}, {0,6,6,6} };
static const stroke_t L_O[] = { {2,0,6,0}, {6,0,8,2}, {8,2,8,10}, {8,10,6,12}, {6,12,2,12},
                                {2,12,0,10}, {0,10,0,2}, {0,2,2,0} };
static const stroke_t L_S[] = { {8,2,6,0}, {6,0,2,0}, {2,0,0,2}, {0,2,0,4}, {0,4,2,6}, {2,6,6,6},
                                {6,6,8,8}, {8,8,8,10}, {8,10,6,12}, {6,12,2,12}, {2,12,0,10} };
static const stroke_t L_A[] = { {0,12,0,4}, {0,4,4,0}, {4,0,8,4}, {8,4,8,12}, {0,8,8,8} };
static const stroke_t L_K[] = { {0,0,0,12}, {8,0,0,8}, {3,5,8,12} };

typedef struct { const stroke_t *s; int n; } glyph_t;
#define G(x) { x, (int)(sizeof(x) / sizeof(x[0])) }

static glyph_t glyph(char ch)
{
    static const glyph_t N = G(L_N), E = G(L_E), O = G(L_O), S = G(L_S), A = G(L_A), K = G(L_K);
    switch (ch) {
    case 'N': return N;
    case 'E': return E;
    case 'O': return O;
    case 'S': return S;
    case 'A': return A;
    case 'K': return K;
    }
    glyph_t none = { NULL, 0 };
    return none;
}

static void word(uint16_t *buf, int stride, int h, const char *w, int cy, float unit,
                 float gap, uint32_t rgb)
{
    int n = (int)strlen(w);
    float lw = unit * 4;                        /* a letter's width */
    float total = n * lw + (n - 1) * gap;
    float x0 = (stride - total) * 0.5f;
    float top = cy - unit * 3;
    const float r = unit * 0.3f, G = unit * 1.4f;
    rgbf_t col = unpack(rgb);
    for (int i = 0; i < n; i++) {
        glyph_t gl = glyph(w[i]);
        float lx = x0 + i * (lw + gap);
        int bx0 = (int)(lx - G - r), bx1 = (int)(lx + lw + G + r) + 1;
        int by0 = (int)(top - G - r), by1 = (int)(top + unit * 6 + G + r) + 1;
        for (int y = by0; y < by1; y++) {
            if (y < 0 || y >= h) continue;
            for (int x = bx0; x < bx1; x++) {
                if (x < 0 || x >= stride) continue;
                float px = x + 0.5f, py = y + 0.5f, best = 1e9f;
                for (int k = 0; k < gl.n; k++) {
                    const stroke_t *s = &gl.s[k];
                    float d = seg_dist(px, py, lx + s->x0 * unit * 0.5f, top + s->y0 * unit * 0.5f,
                                       lx + s->x1 * unit * 0.5f, top + s->y1 * unit * 0.5f, NULL);
                    if (d < best) best = d;
                }
                float sd = best - r;
                if (sd >= G) continue;
                rgbf_t c = { 0, 0, 0 };
                float cover = clampf(0.5f - sd, 0, 1);
                if (cover > 0) {
                    float t = clampf(-sd / r, 0, 1);
                    c = scalec(mixc(col, WHITE, 0.85f * t * t), cover);
                }
                if (sd > 0) {
                    float f = 1.0f - sd / G;
                    maxc(&c, scalec(col, 0.6f * f * f));
                }
                uint32_t rr = (uint32_t)(clampf(c.r, 0, 1) * 255), gg = (uint32_t)(clampf(c.g, 0, 1) * 255),
                         bb = (uint32_t)(clampf(c.b, 0, 1) * 255);
                uint16_t *d = &buf[y * stride + x];
                *d = ns_max565(*d, ns_565((rr << 16) | (gg << 8) | bb));
            }
        }
    }
}

void ns_art_title(uint16_t *buf, int stride, int h, int y_top)
{
    word(buf, stride, h, "NEON", y_top + 36, 10.0f, 18.0f, 0xFF2FD0);
    word(buf, stride, h, "SNAKES", y_top + 112, 8.0f, 14.0f, 0x19F5FF);
}
