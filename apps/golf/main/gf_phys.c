/*
 * GOLF - the ball (see gf_phys.h)
 */
/* The .so is compiled with -Os (components/elf_loader/elf_loader.cmake) and
 * per-file CMake options do not reach that compile: this is the only way to
 * give the pixel and physics loops -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "gf_phys.h"
#include "gf_gfx.h"

#include <math.h>
#include <string.h>

#define G           9.81f
#define DT          (1.0f / 120.0f)
#define REC_EVERY   4               /* 120 / 4 = 30 samples a second          */
#define K_DRAG      0.0047f         /* 0.5 rho Cd A / m, per metre            */
#define K_LIFT      0.0031f
#define CUP_R       0.054f
#define PUTT_VMAX   5.6f
#define DEG         0.017453293f
/* spin decays as exp(-t / 7 s): one multiply per step instead of an expf,
 * which is software on the S3 and was most of the calibration's time */
#define SPIN_DECAY  0.998810708f        /* expf(-DT / 7) */

static const gf_club_t gf_clubs[CLUB_N] = {
    { "DR", 240, 11, 8 },
    { "3W", 220, 13, 9 },
    { "5W", 205, 15, 10 },
    { "4H", 190, 17, 11 },
    { "5I", 178, 18, 12 },
    { "6I", 166, 20, 13 },
    { "7I", 154, 23, 14 },
    { "8I", 142, 26, 15 },
    { "9I", 130, 29, 16 },
    { "PW", 116, 33, 17 },
    { "GW", 100, 38, 18 },
    { "SW", 84,  44, 19 },
    { "LW", 66,  50, 20 },
    { "PT", 0,   0,  0 },
};

const gf_club_t *gf_club(int i)
{
    return &gf_clubs[i];
}

/* launch speed of each club at full power, and its carry at 1/16 steps of
 * that speed */
#define VSTEPS  19                  /* 0/16 .. 18/16 */
static float s_v0[CLUB_N];
static float s_carry[CLUB_N][VSTEPS];

/* per surface: restitution, tangential keep on a bounce, rolling friction */
typedef struct { float rest, keep, mu; } surf_t;
static const surf_t SURF[LIE_KINDS] = {
    [LIE_ROUGH]   = { 0.20f, 0.50f, 0.34f },
    [LIE_FAIRWAY] = { 0.30f, 0.72f, 0.16f },
    [LIE_FIRST]   = { 0.28f, 0.64f, 0.20f },
    [LIE_TEE]     = { 0.30f, 0.72f, 0.13f },
    [LIE_FRINGE]  = { 0.27f, 0.70f, 0.095f },
    [LIE_GREEN]   = { 0.24f, 0.76f, 0.057f },
    [LIE_BUNKER]  = { 0.04f, 0.18f, 1.60f },
    [LIE_WATER]   = { 0.00f, 0.00f, 9.00f },
    [LIE_PATH]    = { 0.52f, 0.88f, 0.07f },
    [LIE_DEEP]    = { 0.10f, 0.30f, 0.80f },
    [LIE_FOREST]  = { 0.18f, 0.45f, 0.40f },
    [LIE_WASTE]   = { 0.18f, 0.48f, 0.42f },
    [LIE_OB]      = { 0.20f, 0.50f, 0.34f },
};

float gf_lie_factor(int club, int lie)
{
    bool wedge = club == CLUB_SW || club == CLUB_LW || club == CLUB_GW;
    switch (lie) {
    case LIE_FIRST:  return 0.95f;
    case LIE_ROUGH:  return 0.86f;
    case LIE_DEEP:   return 0.70f;
    case LIE_FOREST: return 0.80f;
    case LIE_BUNKER: return wedge ? 0.90f : 0.72f;
    case LIE_WASTE:  return 0.88f;
    case LIE_FRINGE: return 0.98f;
    default:         return 1.0f;
    }
}

/* --------------------------------------------------------------------------
 * Flight over flat ground: for the calibration
 * -------------------------------------------------------------------------- */

static void air_accel(const float *v, float wx, float wy, float sb, float ss, float *a)
{
    float rx = v[0] - wx, ry = v[1] - wy, rz = v[2];
    float sp = sqrtf(rx * rx + ry * ry + rz * rz);
    a[0] = -K_DRAG * sp * rx;
    a[1] = -K_DRAG * sp * ry;
    a[2] = -K_DRAG * sp * rz - G;
    if (sp < 0.1f) return;
    /* lift: perpendicular to the relative velocity, in its vertical plane,
     * upwards; curve: horizontal, perpendicular, to the right for ss > 0 */
    float h = sqrtf(rx * rx + ry * ry);
    if (h > 0.01f) {
        float ux = -rx / h * rz / sp, uy = -ry / h * rz / sp, uz = h / sp;   /* "up" normal */
        float L = K_LIFT * sb * sp * sp;
        a[0] += L * ux;
        a[1] += L * uy;
        a[2] += L * uz;
        float S = K_LIFT * ss * sp * sp;
        a[0] += S * (ry / h);
        a[1] += S * (-rx / h);
    }
}

static float flat_carry(float v0, float launch_deg, float sb)
{
    float v[3] = { 0, v0 * cosf(launch_deg * DEG), v0 * sinf(launch_deg * DEG) };
    float p[3] = { 0, 0, 0 };
    float s = sb;
    for (int i = 0; i < 4000; i++) {
        float a[3];
        air_accel(v, 0, 0, s, 0, a);
        for (int k = 0; k < 3; k++) {
            v[k] += a[k] * DT;
            p[k] += v[k] * DT;
        }
        s *= SPIN_DECAY;
        if (p[2] < 0 && v[2] < 0) break;
    }
    return p[1];
}

static float spin_of(int club)
{
    return (float)gf_clubs[club].spin * 0.1f;
}

#include "aos_hal.h"

void gf_phys_init(void)
{
    for (int c = 0; c < CLUB_PT; c++) {
        float target = (float)gf_clubs[c].carry_yd * GF_YD;
        float lo = 10, hi = 110;
        for (int it = 0; it < 22; it++) {
            gf_yield();
            float mid = (lo + hi) * 0.5f;
            if (flat_carry(mid, gf_clubs[c].launch, spin_of(c)) < target) lo = mid;
            else hi = mid;
        }
        s_v0[c] = (lo + hi) * 0.5f;
        /* a driver launches at ~79 m/s and a lob wedge at ~28: anything
         * outside 15..100 means the club table did not load right */
        if (s_v0[c] < 15.0f || s_v0[c] > 100.0f) {
            aos_hal_log("golf", "CALIBRATION OFF: club %d v0 %d (carry %d yd, launch %d)", c, (int)s_v0[c],
                        gf_clubs[c].carry_yd, gf_clubs[c].launch);
        }
        for (int k = 0; k < VSTEPS; k++) {
            gf_yield();
            s_carry[c][k] = k ? flat_carry(s_v0[c] * (float)k / 16.0f, gf_clubs[c].launch, spin_of(c)) : 0.0f;
        }
    }
}

/* the speed fraction that carries 'frac' of the full carry */
static float vfrac_for(int club, float frac)
{
    float target = frac * s_carry[club][16];
    for (int k = 1; k < VSTEPS; k++) {
        if (s_carry[club][k] >= target) {
            float a = s_carry[club][k - 1], b = s_carry[club][k];
            float t = b > a ? (target - a) / (b - a) : 0.0f;
            return ((float)(k - 1) + t) / 16.0f;
        }
    }
    return (float)(VSTEPS - 1) / 16.0f;
}

float gf_phys_carry(int club, float power, int lie)
{
    if (club == CLUB_PT) {
        float v = gf_putt_speed(power);
        return v * v / (2.0f * SURF[LIE_GREEN].mu * G);
    }
    return power * s_carry[club][16] * gf_lie_factor(club, lie);
}

float gf_putt_speed(float power)
{
    return power * PUTT_VMAX;
}

float gf_putt_power(float dist)
{
    return sqrtf(dist * 2.0f * SURF[LIE_GREEN].mu * G) / PUTT_VMAX;
}

/* --------------------------------------------------------------------------
 * The real shot
 * -------------------------------------------------------------------------- */

static bool is_water(const gf_world_t *w, float x, float y)
{
    for (int i = 0; i < w->npoly; i++) {
        if (w->poly[i].kind == SH_WATER && gf_poly_inside(w, &w->poly[i], x, y)) {
            return true;
        }
        if (w->poly[i].kind == GF_POLY_LAKE && gf_poly_inside(w, &w->poly[i], x, y)) {
            return gf_lie(w, x, y) == LIE_WATER;     /* unless on an island */
        }
    }
    return false;
}

static void record(gf_shot_t *s, float x, float y, float z, int ev)
{
    if (s->n >= GF_TRK_MAX) {
        return;
    }
    gf_trk_t *t = &s->trk[s->n++];
    t->x = x;
    t->y = y;
    t->z = z;
    t->ev = (uint8_t)ev;
}

/* canopy and trunk; returns true if it touched a tree this step */
static bool tree_hit(const gf_world_t *w, float *p, float *v, uint32_t *seed, uint8_t *hit_mask, int *last_tree)
{
    int cx = (int)((p[0] - w->gx0) / 16.0f), cy = (int)((p[1] - w->gy0) / 16.0f);
    for (int yy = cy - 1; yy <= cy + 1; yy++) {
        if (yy < 0 || yy >= w->tch) continue;
        for (int xx = cx - 1; xx <= cx + 1; xx++) {
            if (xx < 0 || xx >= w->tcw) continue;
            int c = yy * w->tcw + xx;
            for (int k = w->tcell_first[c]; k < w->tcell_first[c + 1]; k++) {
                int ti = w->tidx[k];
                const gf_tree_t *t = &w->trees[ti];
                float dx = p[0] - t->x, dy = p[1] - t->y;
                float d2 = dx * dx + dy * dy;
                float zr = p[2] - t->h;
                if (zr < 0 || zr > t->height) continue;
                float r = t->radius * 0.85f;
                bool canopy = zr > t->height * 0.32f && d2 < r * r;
                bool trunk = d2 < 0.35f * 0.35f;
                if (!canopy && !trunk) continue;
                if (ti == *last_tree) return false;         /* still inside it */
                *last_tree = ti;
                (void)hit_mask;
                *seed = *seed * 1664525U + 1013904223U;
                float rnd = (float)((*seed >> 8) & 0xFFFF) / 65535.0f;
                if (trunk && !canopy) {
                    /* off the trunk: back out, radially */
                    float d = sqrtf(d2) + 1e-3f;
                    float nx = dx / d, ny = dy / d;
                    float vn = v[0] * nx + v[1] * ny;
                    v[0] = (v[0] - 2 * vn * nx) * 0.45f;
                    v[1] = (v[1] - 2 * vn * ny) * 0.45f;
                } else {
                    /* through the leaves: slowed hard, turned, dropped */
                    float keep = 0.22f + 0.25f * rnd;
                    float ang = (rnd - 0.5f) * 2.2f;
                    float ca = cosf(ang), sa = sinf(ang);
                    float nvx = (v[0] * ca - v[1] * sa) * keep;
                    float nvy = (v[0] * sa + v[1] * ca) * keep;
                    v[0] = nvx;
                    v[1] = nvy;
                    v[2] = (v[2] > 0 ? v[2] * 0.2f : v[2] * 0.5f);
                }
                return true;
            }
        }
    }
    *last_tree = -1;
    return false;
}

void gf_phys_shot(const gf_world_t *w, gf_shot_t *s)
{
    s->n = 0;
    s->result = RES_OK;
    s->carry = 0;
    s->apex = 0;
    float z0 = gf_height(w, s->x0, s->y0);
    float p[3] = { s->x0, s->y0, z0 + 0.02f };
    float v[3];
    float sb = 0, ss = 0;
    /* the spin that grips the ground on landing: unlike sb (which the air
     * slowly takes away) it only halves at each bounce */
    float grip = 0;
    bool rolling;
    uint32_t seed = (uint32_t)(s->x0 * 1000.0f) * 2654435761U ^ (uint32_t)(s->y0 * 1000.0f) ^
                    (uint32_t)(s->power * 10000.0f) * 40503U ^ (uint32_t)(s->aim * 10000.0f) ^ (uint32_t)s->club;
    float dir = s->aim;

    if (s->club == CLUB_PT) {
        float sp = gf_putt_speed(s->power);
        dir += s->acc * 1.5f * DEG;
        v[0] = sp * sinf(dir);
        v[1] = sp * cosf(dir);
        v[2] = 0;
        rolling = true;
    } else {
        const gf_club_t *c = &gf_clubs[s->club];
        float lf = gf_lie_factor(s->club, s->lie0);
        float frac = s->power * lf;
        float vf = vfrac_for(s->club, frac > 1.12f ? 1.12f : frac);
        float sp = s_v0[s->club] * vf;
        float launch = (float)c->launch;
        if (s->lie0 == LIE_BUNKER) launch += 4;
        dir += s->acc * 2.0f * DEG;
        float hz = sp * cosf(launch * DEG);
        v[0] = hz * sinf(dir);
        v[1] = hz * cosf(dir);
        v[2] = sp * sinf(launch * DEG);
        sb = spin_of(s->club);
        if (s->lie0 == LIE_ROUGH || s->lie0 == LIE_DEEP || s->lie0 == LIE_FOREST) {
            sb *= 0.65f;                    /* a flier: less spin */
        }
        grip = sb;
        ss = s->acc * 0.55f * (0.6f + sb * 0.5f);
        rolling = false;
    }

    float last_dry_x = s->x0, last_dry_y = s->y0;
    bool landed = false;
    int last_tree = -1;
    int bounces = 0;
    record(s, p[0], p[1], p[2], TK_AIR);

    for (int step = 0; step < GF_TRK_MAX * REC_EVERY; step++) {
        int ev = rolling ? TK_ROLL : TK_AIR;

        if (!rolling) {
            float a[3];
            air_accel(v, s->wind_x, s->wind_y, sb, ss, a);
            sb *= SPIN_DECAY;
            ss *= SPIN_DECAY;
            for (int k = 0; k < 3; k++) {
                v[k] += a[k] * DT;
                p[k] += v[k] * DT;
            }
            if (p[2] - z0 > s->apex) s->apex = p[2] - z0;
            if (tree_hit(w, p, v, &seed, NULL, &last_tree)) {
                ev = TK_TREE;
            }
            if (!is_water(w, p[0], p[1]) && gf_in_bounds(w, p[0], p[1])) {
                last_dry_x = p[0];
                last_dry_y = p[1];
            }
            float gh = gf_height(w, p[0], p[1]);
            if (p[2] <= gh && v[2] < 0) {
                int lie = gf_lie(w, p[0], p[1]);
                p[2] = gh;
                if (!landed) {
                    landed = true;
                    s->land_x = p[0];
                    s->land_y = p[1];
                    float dx = p[0] - s->x0, dy = p[1] - s->y0;
                    s->carry = sqrtf(dx * dx + dy * dy);
                }
                if (lie == LIE_WATER) {
                    record(s, p[0], p[1], w->water_level, TK_SPLASH);
                    s->result = RES_WATER;
                    break;
                }
                float cdx = p[0] - w->pin_x, cdy = p[1] - w->pin_y;
                float hsp = sqrtf(v[0] * v[0] + v[1] * v[1]);
                if (cdx * cdx + cdy * cdy < CUP_R * CUP_R * 1.6f && hsp < 6.0f) {
                    p[0] = w->pin_x;
                    p[1] = w->pin_y;
                    record(s, p[0], p[1], gh - 0.05f, TK_CUP);
                    s->result = RES_HOLED;
                    break;
                }
                /* bounce off the slope */
                float gx, gy;
                gf_normal(w, p[0], p[1], &gx, &gy);
                float nl = sqrtf(gx * gx + gy * gy + 1);
                float nx = -gx / nl, ny = -gy / nl, nz = 1.0f / nl;
                float vn = v[0] * nx + v[1] * ny + v[2] * nz;
                float tx = v[0] - vn * nx, ty = v[1] - vn * ny, tz = v[2] - vn * nz;
                const surf_t *sf = &SURF[lie];
                float keep = sf->keep;
                {
                    /* backspin bites, harder on the short grass; what is left of it
                     * after each bounce keeps biting */
                    float bite = (lie == LIE_GREEN || lie == LIE_FRINGE) ? 0.36f : 0.28f;
                    keep *= 1.0f - bite * grip;
                    if (keep < 0.15f) keep = 0.15f;
                }
                float rest = sf->rest;
                v[0] = tx * keep - vn * rest * nx;
                v[1] = ty * keep - vn * rest * ny;
                v[2] = tz * keep - vn * rest * nz;
                sb *= 0.5f;
                grip *= 0.5f;
                ss *= 0.3f;
                bounces++;
                ev = TK_BOUNCE;
                if (v[2] < 0.9f || bounces > 6) {
                    rolling = true;
                    v[2] = 0;
                }
            }
        } else {
            /* rolling */
            float gh = gf_height(w, p[0], p[1]);
            p[2] = gh;
            int lie = gf_lie(w, p[0], p[1]);
            if (lie == LIE_WATER) {
                record(s, p[0], p[1], w->water_level, TK_SPLASH);
                s->result = RES_WATER;
                break;
            }
            if (!landed) {
                landed = true;
                s->land_x = p[0];
                s->land_y = p[1];
            }
            last_dry_x = p[0];
            last_dry_y = p[1];
            float gx, gy;
            gf_normal(w, p[0], p[1], &gx, &gy);
            float sp = sqrtf(v[0] * v[0] + v[1] * v[1]);
            float mu = SURF[lie].mu * (1.0f + 2.5f * grip);
            grip *= 0.985f;         /* the check from the spin fades as it rolls */
            float slope_ax = -G * gx, slope_ay = -G * gy;
            if (sp < 0.04f) {
                /* resting? only if the slope cannot beat static friction */
                float sl = sqrtf(slope_ax * slope_ax + slope_ay * slope_ay);
                if (sl < mu * G * 1.4f) {
                    break;
                }
            }
            float fx = 0, fy = 0;
            if (sp > 1e-4f) {
                fx = -mu * G * v[0] / sp;
                fy = -mu * G * v[1] / sp;
            }
            float nvx = v[0] + (fx + slope_ax) * DT, nvy = v[1] + (fy + slope_ay) * DT;
            /* friction must not reverse the motion on its own */
            if (nvx * v[0] + nvy * v[1] < 0 && sqrtf(slope_ax * slope_ax + slope_ay * slope_ay) < mu * G * 1.4f) {
                v[0] = v[1] = 0;
                break;
            }
            v[0] = nvx;
            v[1] = nvy;
            p[0] += v[0] * DT;
            p[1] += v[1] * DT;

            float cdx = p[0] - w->pin_x, cdy = p[1] - w->pin_y;
            if (cdx * cdx + cdy * cdy < CUP_R * CUP_R) {
                float spn = sqrtf(v[0] * v[0] + v[1] * v[1]);
                if (spn < 1.35f) {
                    p[0] = w->pin_x;
                    p[1] = w->pin_y;
                    record(s, p[0], p[1], gh - 0.05f, TK_CUP);
                    s->result = RES_HOLED;
                    break;
                }
                if (spn < 1.9f && ev != TK_LIP) {
                    /* round the lip: turned and slowed */
                    seed = seed * 1664525U + 1013904223U;
                    float ang = (((seed >> 8) & 1) ? 1.0f : -1.0f) * (0.5f + (float)((seed >> 9) & 255) / 400.0f);
                    float ca = cosf(ang), sa = sinf(ang);
                    float ax = (v[0] * ca - v[1] * sa) * 0.55f, ay = (v[0] * sa + v[1] * ca) * 0.55f;
                    v[0] = ax;
                    v[1] = ay;
                    ev = TK_LIP;
                }
            }
        }

        if (ev == TK_BOUNCE || ev == TK_TREE || ev == TK_LIP || (step % REC_EVERY) == 0) {
            record(s, p[0], p[1], p[2], ev);
        }
    }

    if (s->result != RES_HOLED && s->result != RES_WATER) {
        record(s, p[0], p[1], p[2], TK_ROLL);
    }
    s->x = p[0];
    s->y = p[1];
    s->lie = gf_lie(w, s->x, s->y);
    if (s->result == RES_OK && s->lie == LIE_OB) {
        s->result = RES_OB;
    }
    if (s->result == RES_WATER) {
        /* the drop: where it last was over dry land, two metres further back
         * towards where it came from, and not in a hazard */
        float dx = s->x0 - last_dry_x, dy = s->y0 - last_dry_y;
        float d = sqrtf(dx * dx + dy * dy);
        float bx = last_dry_x, by = last_dry_y;
        for (int k = 0; k < 20; k++) {
            float back = 2.0f + (float)k * 1.5f;
            if (d > 0.01f) {
                bx = last_dry_x + dx / d * (back < d ? back : d);
                by = last_dry_y + dy / d * (back < d ? back : d);
            }
            int l = gf_lie(w, bx, by);
            if (l != LIE_WATER && l != LIE_OB && l != LIE_BUNKER) break;
        }
        s->drop_x = bx;
        s->drop_y = by;
    }
    float dx = s->x - s->x0, dy = s->y - s->y0;
    s->total = sqrtf(dx * dx + dy * dy);
    if (!landed) {
        s->land_x = s->x;
        s->land_y = s->y;
    }
}
