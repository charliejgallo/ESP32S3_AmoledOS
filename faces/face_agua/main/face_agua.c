/*
 * AmoledOS - Water: the battery as water in a glass that you hold.
 *
 * The water's level is the charge. Its surface stays level with the real
 * ground, wherever the watch points, and lags behind it on a spring, so a
 * flick of the wrist makes it slosh; on top of that there is a wave along
 * the surface (a 1-D wave equation) that a tap on the glass or a shake
 * excites, and a shake also raises bubbles, as does the charger.
 *
 * Still, it draws nothing: every frame renders the scene row by row against
 * the previous one and presents only the rectangle that changed, and when
 * the spring and the wave have died down even that is skipped.
 *
 * The scene is at half resolution (face_common.h); the time and the date are
 * masks over it at full resolution.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CX          (FC_SW / 2.0f)
#define CY          (FC_SH / 2.0f)
#define WAVE_N      192             /* samples along the surface */
#define WAVE_R      150.0f          /* they span s in [-R, R] (half the diagonal) */
#define BUBBLES     28
#define DEPTH_LUT   64

typedef struct {
    float x, y, r, vy, wob;
} bubble_t;

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    bool       aod;
    bool       asleep;

    /* the surface: its angle on a spring, its level, the wave */
    float      theta, omega;        /* rad, rad/s; 0 = water at the bottom */
    float      target;              /* where gravity says it should be      */
    float      fill, fill_goal;     /* 0..1                                  */
    float      h[WAVE_N], v[WAVE_N];

    bubble_t   bub[BUBBLES];
    int        nbub;
    float      bubble_acc;          /* bubbles owed, fractional */
    uint32_t   seed;

    fc_grav_t  grav;
    float      last_mag;

    uint16_t   lut[DEPTH_LUT];      /* colour by depth below the surface */
    uint16_t   foam;
    uint16_t  *row;                 /* one row of the scene, to compare */

    char       last_hhmm[8];
    char       last_line[48];
    int        battery;
    bool       charging;
} agua_t;

/* -------------------------------------------------------------------------- */

static void build_lut(agua_t *f, bool aod)
{
    /* light at the surface, deep blue below: three stops */
    static const uint32_t stops[3] = { 0x5FD8F0, 0x1A78C8, 0x062050 };
    for (int i = 0; i < DEPTH_LUT; i++) {
        float t = (float)i / (DEPTH_LUT - 1);
        uint32_t a = t < 0.35f ? stops[0] : stops[1];
        uint32_t b = t < 0.35f ? stops[1] : stops[2];
        float u = t < 0.35f ? t / 0.35f : (t - 0.35f) / 0.65f;
        uint8_t r = (uint8_t)(((a >> 16) & 0xFF) * (1 - u) + ((b >> 16) & 0xFF) * u);
        uint8_t g = (uint8_t)(((a >> 8) & 0xFF) * (1 - u) + ((b >> 8) & 0xFF) * u);
        uint8_t bl = (uint8_t)((a & 0xFF) * (1 - u) + (b & 0xFF) * u);
        uint16_t c = fc_rgb(r, g, bl);
        f->lut[i] = aod ? fc_scale(c, 40) : c;
    }
    f->foam = aod ? fc_scale(fc_hex(0xCFF4FF), 60) : fc_hex(0xCFF4FF);
}

/* The level h0 along the "down" axis such that the fraction 'fill' of the
 * screen lies below it. The depth of a pixel, d = x sin + y cos with x and y
 * uniform over the screen, is the sum of two uniforms: its distribution is a
 * trapezoid and inverts in closed form. */
static float level_for(float fill, float s, float c)
{
    float a = fabsf(s) * FC_SW, b = fabsf(c) * FC_SH;
    if (a > b) {
        float t = a; a = b; b = t;
    }
    float q = 1.0f - fill;          /* fraction ABOVE the surface */
    float u;
    if (a < 1e-3f) {
        u = q * b;
    } else if (q < a / (2 * b)) {
        u = sqrtf(2 * a * b * q);
    } else if (q < 1 - a / (2 * b)) {
        u = (2 * b * q + a) / 2;
    } else {
        u = a + b - sqrtf(2 * a * b * (1 - q));
    }
    return u - (a + b) / 2;
}

static inline float wave_at(const agua_t *f, float s)
{
    float fi = (s + WAVE_R) * (WAVE_N - 1) / (2 * WAVE_R);
    int i = (int)fi;
    if (i < 0) return f->h[0];
    if (i >= WAVE_N - 1) return f->h[WAVE_N - 1];
    float t = fi - i;
    return f->h[i] * (1 - t) + f->h[i + 1] * t;
}

static void splash(agua_t *f, float s, float amp, int width)
{
    int c = (int)((s + WAVE_R) * (WAVE_N - 1) / (2 * WAVE_R));
    for (int k = -width; k <= width; k++) {
        int i = c + k;
        if (i < 1 || i >= WAVE_N - 1) continue;
        float w = expf(-(float)(k * k) / (width * width * 0.4f + 0.1f));
        f->v[i] += amp * w;
    }
    f->asleep = false;
}

static void spawn_bubble(agua_t *f, float sn, float cs, float h0)
{
    if (f->nbub >= BUBBLES) {
        return;
    }
    /* somewhere in the water, deep: along the surface at random, below it */
    float s = (fc_randf(&f->seed) - 0.5f) * 140.0f;
    float d = h0 + 20.0f + fc_randf(&f->seed) * 90.0f;
    bubble_t *b = &f->bub[f->nbub++];
    /* p = c + d*n + s*t, n = (sn, cs), t = (cs, -sn) */
    b->x = CX + d * sn + s * cs;
    b->y = CY + d * cs - s * sn;
    b->r = 1.0f + fc_randf(&f->seed) * 2.2f;
    b->vy = 18.0f + fc_randf(&f->seed) * 22.0f;
    b->wob = fc_randf(&f->seed) * 6.28f;
}

/* -------------------------------------------------------------------------- */

/* Renders the whole scene against the previous one and presents what
 * changed. */
static void render(agua_t *f)
{
    float sn = sinf(f->theta), cs = cosf(f->theta);
    float h0 = level_for(f->fill, sn, cs);
    int dx0 = FC_SW, dy0 = FC_SH, dx1 = -1, dy1 = -1;

    for (int y = 0; y < FC_SH; y++) {
        uint16_t *out = f->row;
        float py = y + 0.5f - CY;
        float px = 0.5f - CX;
        /* d and s are linear along the row */
        float d = px * sn + py * cs;
        float s = px * cs - py * sn;
        for (int x = 0; x < FC_SW; x++, d += sn, s += cs) {
            float surf = h0 - wave_at(f, s);
            float depth = d - surf;         /* > 0: under water */
            uint16_t col;
            if (depth < -1.0f) {
                col = 0;
            } else if (depth < 0.0f) {
                col = fc_mix(0, f->foam, (uint32_t)((depth + 1.0f) * 255));
            } else if (depth < 1.2f) {
                col = f->foam;
            } else {
                int k = (int)(depth * (DEPTH_LUT / 150.0f));
                col = f->lut[k < DEPTH_LUT ? k : DEPTH_LUT - 1];
            }
            out[x] = col;
        }
        /* the bubbles on this row */
        for (int i = 0; i < f->nbub; i++) {
            const bubble_t *b = &f->bub[i];
            float ry = (y + 0.5f) - b->y;
            if (fabsf(ry) > b->r + 0.5f) continue;
            int xa = (int)(b->x - b->r - 1), xb = (int)(b->x + b->r + 1);
            for (int x = LV_MAX(xa, 0); x <= LV_MIN(xb, FC_SW - 1); x++) {
                float rx = (x + 0.5f) - b->x;
                float r2 = rx * rx + ry * ry;
                if (r2 <= b->r * b->r && out[x]) {
                    /* a ring, lighter at the rim */
                    float k = r2 / (b->r * b->r);
                    out[x] = fc_mix(out[x], f->foam, (uint32_t)(70 + 150 * k));
                }
            }
        }
        uint16_t *dst = f->fc.small + y * FC_SW;
        if (memcmp(dst, out, FC_SW * sizeof(uint16_t)) != 0) {
            int a = 0, b = FC_SW - 1;
            while (dst[a] == out[a]) a++;
            while (dst[b] == out[b]) b--;
            memcpy(dst + a, out + a, (size_t)(b - a + 1) * sizeof(uint16_t));
            if (a < dx0) dx0 = a;
            if (b > dx1) dx1 = b;
            if (y < dy0) dy0 = y;
            dy1 = y;
        }
    }
    if (dx1 >= 0) {
        fc_present(&f->fc, dx0, dy0, dx1 + 1, dy1 + 1);
    }
}

/* -------------------------------------------------------------------------- */

static void physics(agua_t *f, float dt)
{
    /* gravity, filtered; its direction in the glass' plane */
    float rx, ry, rz;
    if (!aos_watchface_gravity(&rx, &ry, &rz)) {
        rx = 0; ry = 1; rz = 0;
    }
    float mag = sqrtf(rx * rx + ry * ry + rz * rz);
    fc_grav_update(&f->grav, 0.35f);
    float plane = sqrtf(f->grav.x * f->grav.x + f->grav.y * f->grav.y);
    /* flat on the table there is no "down" in the glass' plane: the water
     * keeps the angle it had, like water does in a glass laid flat */
    if (plane > 0.30f) {
        float t = atan2f(f->grav.x, f->grav.y);
        float d = t - f->target;
        while (d > FC_PI) d -= 2 * FC_PI;
        while (d < -FC_PI) d += 2 * FC_PI;
        f->target += d;
    }

    /* a shake: the size of the jolt, from the raw magnitude */
    float jolt = fabsf(mag - f->last_mag);
    f->last_mag = mag;
    if (jolt > 0.12f) {
        splash(f, (fc_randf(&f->seed) - 0.5f) * 160.0f, jolt * 22.0f, 6);
        f->bubble_acc += jolt * 6.0f;
    }

    /* the slosh: the surface's angle on a spring towards gravity's */
    float err = f->target - f->theta;
    float acc = 38.0f * err - 4.2f * f->omega;
    f->omega += acc * dt;
    f->theta += f->omega * dt;
    /* the spring's swing feeds the wave a little, at the ends */
    float kick = acc * dt * 0.9f;
    f->v[8] += kick;
    f->v[WAVE_N - 9] -= kick;

    /* the level eases towards the battery's */
    f->fill += (f->fill_goal - f->fill) * LV_MIN(1.0f, dt * 1.5f);

    /* the wave: two substeps, damped, ends reflecting */
    float energy = 0;
    for (int sub = 0; sub < 2; sub++) {
        for (int i = 1; i < WAVE_N - 1; i++) {
            f->v[i] += 0.45f * (f->h[i - 1] + f->h[i + 1] - 2 * f->h[i]);
            f->v[i] *= 0.988f;
        }
        for (int i = 1; i < WAVE_N - 1; i++) {
            f->h[i] += f->v[i];
            f->h[i] *= 0.999f;
        }
        f->h[0] = f->h[1];
        f->h[WAVE_N - 1] = f->h[WAVE_N - 2];
    }
    for (int i = 0; i < WAVE_N; i++) {
        energy += fabsf(f->h[i]) + fabsf(f->v[i]);
    }

    /* bubbles: owed by shakes, and a steady stream on the charger */
    if (f->charging) {
        f->bubble_acc += dt * 2.5f;
    }
    float sn = sinf(f->theta), cs = cosf(f->theta);
    float h0 = level_for(f->fill, sn, cs);
    while (f->bubble_acc >= 1.0f) {
        f->bubble_acc -= 1.0f;
        spawn_bubble(f, sn, cs, h0);
    }
    for (int i = 0; i < f->nbub; i++) {
        bubble_t *b = &f->bub[i];
        b->wob += dt * 5.0f;
        float up = b->vy * dt;
        /* up is -n; the wobble along t */
        float w = sinf(b->wob) * 0.6f;
        b->x += -sn * up + cs * w;
        b->y += -cs * up - sn * w;
        float d = (b->x - CX) * sn + (b->y - CY) * cs;
        float s = (b->x - CX) * cs - (b->y - CY) * sn;
        if (d < h0 - wave_at(f, s) + 1.5f) {
            /* reached the surface: a tiny ripple, and it is gone */
            splash(f, s, -0.5f * b->r, 2);
            f->bub[i--] = f->bub[--f->nbub];
        }
    }

    bool still = fabsf(err) < 0.002f && fabsf(f->omega) < 0.004f &&
                 energy < 0.6f && f->nbub == 0 && fabsf(f->fill_goal - f->fill) < 0.0005f;
    if (still) {
        memset(f->v, 0, sizeof(f->v));
    }
    f->asleep = still;
}

static void frame(void *ctx, uint32_t dt_ms)
{
    agua_t *f = ctx;
    if (!f || f->aod) {
        return;
    }
    bool was_asleep = f->asleep;
    physics(f, dt_ms / 1000.0f);
    if (!f->asleep || !was_asleep) {
        render(f);
    }
}

/* -------------------------------------------------------------------------- */

static void text_update(agua_t *f, const struct tm *now, bool force)
{
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        f->battery = batt.percent;
        f->charging = batt.charging;
    }
    f->fill_goal = LV_CLAMP(8, f->battery, 100) / 100.0f * 0.92f;
    if (fabsf(f->fill_goal - f->fill) > 0.0005f) {
        f->asleep = false;
    }

    char line[48];
    snprintf(line, sizeof(line), "%s %d %s  \xC2\xB7  %d%%%s",
             aos_day_name(now->tm_wday), now->tm_mday, aos_month_name(now->tm_mon),
             f->battery, f->charging ? " +" : "");

    uint16_t text_col = f->aod ? fc_hex(0x8A8A8A) : fc_hex(0xFFFFFF);
    bool redo_time = force || strcmp(hhmm, f->last_hhmm) != 0;
    bool redo_line = force || strcmp(line, f->last_line) != 0;
    if (redo_time) {
        fc_mask_t old = f->fc.mask[0];
        f->fc.mask[0].a = NULL;             /* keep its area to repaint */
        snprintf(f->last_hhmm, sizeof(f->last_hhmm), "%s", hhmm);
        fc_mask_text(&f->fc.mask[0], f->root, hhmm, aos_font_huge, 2.3f,
                     FC_W / 2, 150, text_col, true);
        fc_present_mask(&f->fc, &old);
        free(old.a);
        fc_present_mask(&f->fc, &f->fc.mask[0]);
    }
    if (redo_line) {
        fc_mask_t old = f->fc.mask[1];
        f->fc.mask[1].a = NULL;
        snprintf(f->last_line, sizeof(f->last_line), "%s", line);
        fc_mask_text(&f->fc.mask[1], f->root, line, aos_font_body, 1.0f,
                     FC_W / 2, 238, f->aod ? fc_hex(0x6A6A6A) : fc_hex(0xE8F6FF), true);
        fc_present_mask(&f->fc, &old);
        free(old.a);
        fc_present_mask(&f->fc, &f->fc.mask[1]);
    }
}

static void *create(lv_obj_t *root)
{
    agua_t *f = lv_malloc_zeroed(sizeof(agua_t));
    if (!f) {
        return NULL;
    }
    f->row = malloc(FC_SW * sizeof(uint16_t));
    if (!f->row || !fc_init(&f->fc, root)) {
        free(f->row);
        lv_free(f);
        return NULL;
    }
    f->root = root;
    f->seed = (uint32_t)aos_hal_uptime_ms() | 1u;
    f->battery = 80;
    build_lut(f, false);

    /* start level with gravity, at the battery's level, already still */
    float gx, gy;
    if (aos_watchface_gravity(&gx, &gy, NULL) && gx * gx + gy * gy > 0.09f) {
        f->target = f->theta = atan2f(gx, gy);
    }
    f->last_mag = 1.0f;
    struct tm now;
    aos_hal_time_now(&now);
    text_update(f, &now, true);
    f->fill = f->fill_goal;
    render(f);
    fc_present_all(&f->fc);
    return f;
}

static void refresh(void *ctx, const struct tm *now)
{
    agua_t *f = ctx;
    if (f) {
        text_update(f, now, false);
    }
}

static void set_aod(void *ctx, bool aod)
{
    agua_t *f = ctx;
    if (!f || f->aod == aod) {
        return;
    }
    f->aod = aod;
    build_lut(f, aod);
    if (aod) {
        /* dimmed: the water at rest, dark, and nothing moving */
        memset(f->h, 0, sizeof(f->h));
        memset(f->v, 0, sizeof(f->v));
        f->nbub = 0;
        f->omega = 0;
        f->theta = f->target;
        f->fill = f->fill_goal;
    }
    struct tm now;
    aos_hal_time_now(&now);
    text_update(f, &now, true);
    render(f);
    fc_present_all(&f->fc);
    f->asleep = false;
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    agua_t *f = ctx;
    if (!f || what != AOS_FACE_TOUCH_TAP) {
        return false;
    }
    /* a drop where the finger was, projected onto the surface */
    float sn = sinf(f->theta), cs = cosf(f->theta);
    float px = x / 2.0f - CX, py = y / 2.0f - CY;
    float s = px * cs - py * sn;
    splash(f, s, -3.2f, 5);
    f->bubble_acc += 2.0f;
    return true;
}

static void destroy(void *ctx)
{
    agua_t *f = ctx;
    if (!f) {
        return;
    }
    fc_deinit(&f->fc);
    free(f->row);
    lv_free(f);
}

static bool agua_get(aos_watchface_t *face)
{
    face->id       = "face.agua";
    face->name     = N_("Agua");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 33;
    return true;
}

AOS_FACE_ENTRY(agua_get);
