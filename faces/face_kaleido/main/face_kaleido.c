/*
 * AmoledOS - Kaleidoscope: twelve mirrors around the time.
 *
 * What the mirrors reflect is a tile of colour (128x128, wrapping) under a
 * palette that cycles. Tilting the watch turns the tile behind the mirrors,
 * the way a real kaleidoscope is turned; a tap makes the colours flow into a
 * new palette; a shake of the wrist drops a new tile into the chamber. The
 * long press is the picker's, as on every face: it was the face's own at
 * first (hold for the colours), and getting out of the face turned into a
 * fight over the same gesture (tried on the board, 2026-11-01).
 *
 * Per frame it is a lookup per pixel at half resolution: where each pixel
 * falls inside its mirror's wedge is computed once (polar coordinates and
 * the fold), so a frame is a rotation, a texture read and a palette read.
 * The whole screen changes every frame, so this face asks for 20 fps, not 30,
 * and gets 19 on the board, drawing 85 % of the time (9-10 through LVGL, and
 * 15-16 when the frame was built whole before being pushed: face_common.h).
 * Dimmed it keeps only a dark ring of glass.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_common.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEX         128
#define WEDGE       (FC_PI / 6.0f)      /* a mirror every 30 degrees: 12 */
#define PAL         256
#define FLOW_MS     1400                /* a tap's flow of colours */
#define SHAKE_G     0.55f               /* a jolt this big is a shake */

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    bool       aod;

    int16_t   *uv;          /* per small pixel: the folded (u, v) */
    uint8_t   *shade;       /* per small pixel: the lens, darker at the centre */
    uint8_t   *tex;         /* TEX x TEX palette indices */
    uint16_t   pal[PAL];

    float      hue[3];      /* the cosine palette's phases */
    float      hue_goal[3];
    float      phase;       /* palette rotation, in entries */
    float      spin;        /* the tile's angle behind the mirrors */
    float      spin_vel;
    float      drift_x, drift_y;
    uint32_t   seed;

    bool       flowing;
    uint32_t   flow_until;
    float      last_mag;

    fc_grav_t  grav;
    char       last_hhmm[8];
    char       last_date[24];
} kaleido_t;

/* -------------------------------------------------------------------------- */

static void build_uv(kaleido_t *k)
{
    const float cx = FC_SW / 2.0f, cy = FC_SH / 2.0f - 8.0f;
    for (int y = 0; y < FC_SH; y++) {
        for (int x = 0; x < FC_SW; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float r = sqrtf(dx * dx + dy * dy);
            float a = atan2f(dy, dx) + FC_PI;           /* 0..2pi */
            int seg = (int)(a / WEDGE);
            float al = a - seg * WEDGE;
            if (seg & 1) {
                al = WEDGE - al;                        /* the mirror */
            }
            int16_t *p = &k->uv[(y * FC_SW + x) * 2];
            p[0] = (int16_t)(r * cosf(al) * 16);        /* Q4 */
            p[1] = (int16_t)(r * sinf(al) * 16);
            /* a dark lens under the time, opening out by r = 75 */
            float t = (r - 38.0f) / 37.0f;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            k->shade[y * FC_SW + x] = (uint8_t)(70 + 185 * t * t * (3 - 2 * t));
        }
    }
}

/* A tile that wraps on itself: sines whose frequencies are whole numbers of
 * cycles across it, and a few rings, quantised to the palette. */
static void build_tex(kaleido_t *k)
{
    float fx[4], fy[4], ph[4], amp[4];
    for (int i = 0; i < 4; i++) {
        fx[i] = (float)((int)(fc_rand(&k->seed) % 5) - 2);
        fy[i] = (float)((int)(fc_rand(&k->seed) % 5) - 2);
        if (fx[i] == 0 && fy[i] == 0) {
            fx[i] = 1 + i % 2;
        }
        ph[i] = fc_randf(&k->seed) * 6.28f;
        amp[i] = 0.5f + fc_randf(&k->seed);
    }
    float bx[3], by[3], br[3];
    for (int i = 0; i < 3; i++) {
        bx[i] = fc_randf(&k->seed) * TEX;
        by[i] = fc_randf(&k->seed) * TEX;
        br[i] = 6 + fc_randf(&k->seed) * 18;
    }
    const float w = 2 * FC_PI / TEX;
    for (int y = 0; y < TEX; y++) {
        for (int x = 0; x < TEX; x++) {
            float v = 0;
            for (int i = 0; i < 4; i++) {
                v += amp[i] * sinf(w * (fx[i] * x + fy[i] * y) + ph[i]);
            }
            for (int i = 0; i < 3; i++) {
                /* distance on the torus */
                float dx = fabsf(x - bx[i]), dy = fabsf(y - by[i]);
                if (dx > TEX / 2) dx = TEX - dx;
                if (dy > TEX / 2) dy = TEX - dy;
                float d = sqrtf(dx * dx + dy * dy);
                v += 1.6f * sinf(d / br[i] * 6.28f);
            }
            k->tex[y * TEX + x] = (uint8_t)((int)(v * 11.0f) & 0xFF);
        }
    }
}

/* Inigo Quilez's cosine palette: 0.5 + 0.5 cos(2pi (t + phase_c)) per channel. */
static void build_pal(kaleido_t *k)
{
    for (int i = 0; i < PAL; i++) {
        float t = (float)i / PAL;
        uint8_t c[3];
        for (int ch = 0; ch < 3; ch++) {
            float v = 0.5f + 0.5f * cosf(2 * FC_PI * (t * (ch == 1 ? 2.0f : 1.0f) + k->hue[ch]));
            v = v * v;                          /* deeper, less pastel */
            c[ch] = (uint8_t)(v * 255);
        }
        k->pal[i] = fc_rgb(c[0], c[1], c[2]);
    }
}

static void new_hue_goal(kaleido_t *k)
{
    for (int i = 0; i < 3; i++) {
        k->hue_goal[i] = fc_randf(&k->seed);
    }
}

/* -------------------------------------------------------------------------- */

static void render(kaleido_t *k)
{
    int32_t cs = (int32_t)(cosf(k->spin) * 4096), sn = (int32_t)(sinf(k->spin) * 4096);
    int32_t ox = (int32_t)(k->drift_x * 16), oy = (int32_t)(k->drift_y * 16);
    uint32_t ph = (uint32_t)k->phase;
    const int16_t *uv = k->uv;
    uint16_t *out = k->fc.small;
    /* dimmed, only a ring of glass between these two radii (Q4, small px) */
    const int32_t rin2 = (88 * 16) * (88 * 16), rout2 = (100 * 16) * (100 * 16);

    for (int i = 0; i < FC_SW * FC_SH; i++, uv += 2) {
        int32_t u = uv[0], v = uv[1];
        if (k->aod) {
            int32_t r2 = u * u + v * v;
            if (r2 < rin2 || r2 > rout2) {
                out[i] = 0;
                continue;
            }
        }
        int32_t tu = ((u * cs - v * sn) >> 12) + ox;
        int32_t tv = ((u * sn + v * cs) >> 12) + oy;
        /* >> 5: a texel is two of these pixels, larger and calmer shapes */
        uint8_t idx = k->tex[((tv >> 5) & (TEX - 1)) * TEX + ((tu >> 5) & (TEX - 1))];
        uint16_t c = k->pal[(uint8_t)(idx + ph)];
        uint8_t sh = k->aod ? 120 : k->shade[i];
        out[i] = sh == 255 ? c : fc_scale(c, sh);   /* outside the lens, as is */
    }
    fc_present_all(&k->fc);
}

static void frame(void *ctx, uint32_t dt_ms)
{
    kaleido_t *k = ctx;
    if (!k || k->aod) {
        return;
    }
    float dt = dt_ms / 1000.0f;

    /* the tilt turns the tile: the in-plane angle of gravity, eased */
    fc_grav_update(&k->grav, 0.2f);
    float plane = sqrtf(k->grav.x * k->grav.x + k->grav.y * k->grav.y);
    float want = plane > 0.25f ? atan2f(k->grav.x, k->grav.y) * 1.5f : k->spin;
    float d = want - k->spin;
    while (d > FC_PI) d -= 2 * FC_PI;
    while (d < -FC_PI) d += 2 * FC_PI;
    k->spin_vel += (d * 6.0f - k->spin_vel * 3.0f) * dt;
    k->spin += k->spin_vel * dt + 0.05f * dt;      /* and a slow turn of its own */

    k->drift_x += 3.0f * dt;
    k->drift_y += 1.7f * dt;

    /* a shake: a new tile in the chamber */
    float gx, gy, gz;
    if (aos_watchface_gravity(&gx, &gy, &gz)) {
        float mag = sqrtf(gx * gx + gy * gy + gz * gz);
        if (k->last_mag > 0 && fabsf(mag - k->last_mag) > SHAKE_G) {
            build_tex(k);
            k->last_mag = 0;               /* one tile per shake, not per sample */
        } else {
            k->last_mag = mag;
        }
    }

    /* after a tap the colours flow, fast, for a moment */
    if (k->flowing && (int32_t)((uint32_t)aos_hal_uptime_ms() - k->flow_until) >= 0) {
        k->flowing = false;                /* the palette it reached stays */
    }
    float rate = k->flowing ? 2.2f : 0.6f;
    bool pal_changed = false;
    for (int i = 0; i < 3; i++) {
        float e = k->hue_goal[i] - k->hue[i];
        if (fabsf(e) > 0.001f) {
            k->hue[i] += e * LV_MIN(1.0f, rate * dt);
            pal_changed = true;
        }
    }
    if (k->flowing && fabsf(k->hue_goal[0] - k->hue[0]) < 0.02f) {
        new_hue_goal(k);                   /* keep flowing while it lasts */
    }
    if (pal_changed) {
        build_pal(k);
    }
    k->phase += (k->flowing ? 90.0f : 14.0f) * dt;

    render(k);
}

/* -------------------------------------------------------------------------- */

static void text_update(kaleido_t *k, const struct tm *now, bool force)
{
    char hhmm[8], date[24];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    snprintf(date, sizeof(date), "%s %d", aos_day_name(now->tm_wday), now->tm_mday);
    uint16_t col = k->aod ? fc_hex(0x9A9A9A) : fc_hex(0xFFFFFF);
    if (force || strcmp(hhmm, k->last_hhmm) != 0) {
        fc_mask_t old = k->fc.mask[0];
        k->fc.mask[0].a = NULL;
        snprintf(k->last_hhmm, sizeof(k->last_hhmm), "%s", hhmm);
        fc_mask_text(&k->fc.mask[0], k->root, hhmm, aos_font_huge, 1.9f,
                     FC_W / 2, FC_H / 2 - 16, col, true);
        fc_present_mask(&k->fc, &old);
        free(old.a);
        fc_present_mask(&k->fc, &k->fc.mask[0]);
    }
    if (force || strcmp(date, k->last_date) != 0) {
        fc_mask_t old = k->fc.mask[1];
        k->fc.mask[1].a = NULL;
        snprintf(k->last_date, sizeof(k->last_date), "%s", date);
        fc_mask_text(&k->fc.mask[1], k->root, date, aos_font_body, 1.0f,
                     FC_W / 2, FC_H / 2 + 40, k->aod ? fc_hex(0x6A6A6A) : fc_hex(0xFFFFFF), true);
        fc_present_mask(&k->fc, &old);
        free(old.a);
        fc_present_mask(&k->fc, &k->fc.mask[1]);
    }
}

static void *create(lv_obj_t *root)
{
    kaleido_t *k = lv_malloc_zeroed(sizeof(kaleido_t));
    if (!k) {
        return NULL;
    }
    k->uv = malloc(FC_SW * FC_SH * 2 * sizeof(int16_t));
    k->shade = malloc(FC_SW * FC_SH);
    k->tex = malloc(TEX * TEX);
    if (!k->uv || !k->shade || !k->tex || !fc_init(&k->fc, root)) {
        free(k->uv);
        free(k->shade);
        free(k->tex);
        lv_free(k);
        return NULL;
    }
    k->root = root;
    k->seed = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    build_uv(k);
    build_tex(k);
    new_hue_goal(k);
    memcpy(k->hue, k->hue_goal, sizeof(k->hue));
    build_pal(k);
    struct tm now;
    aos_hal_time_now(&now);
    text_update(k, &now, true);
    render(k);
    return k;
}

static void refresh(void *ctx, const struct tm *now)
{
    kaleido_t *k = ctx;
    if (k) {
        text_update(k, now, false);
    }
}

static void set_aod(void *ctx, bool aod)
{
    kaleido_t *k = ctx;
    if (!k || k->aod == aod) {
        return;
    }
    k->aod = aod;
    k->flowing = false;
    struct tm now;
    aos_hal_time_now(&now);
    text_update(k, &now, true);
    render(k);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    (void)x;
    (void)y;
    kaleido_t *k = ctx;
    if (!k) {
        return false;
    }
    if (what != AOS_FACE_TOUCH_TAP) {
        return false;                       /* the long press opens the picker */
    }
    k->flowing = true;
    k->flow_until = (uint32_t)aos_hal_uptime_ms() + FLOW_MS;
    new_hue_goal(k);
    return true;
}

static void destroy(void *ctx)
{
    kaleido_t *k = ctx;
    if (!k) {
        return;
    }
    fc_deinit(&k->fc);
    free(k->uv);
    free(k->shade);
    free(k->tex);
    lv_free(k);
}

static bool kaleido_get(aos_watchface_t *face)
{
    face->id       = "face.kaleido";
    face->name     = N_("Caleidoscopio");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 50;
    return true;
}

AOS_FACE_ENTRY(kaleido_get);
