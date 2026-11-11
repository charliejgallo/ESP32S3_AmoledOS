/*
 * AmoledOS - Aquarium: seven fish behind the time.
 *
 * Each fish wanders to a point of its own choosing and then to another;
 * seaweed sways, bubbles rise towards wherever "up" is as you turn the
 * wrist. A tap on the water drops food and the fish come for it; a tap on a
 * fish makes it dart away; a shake scatters them all. At night the water is
 * darker and they swim slowly.
 *
 * The tank itself (the water's gradient, the shafts of light, the sand and
 * the rocks) is drawn once into 'base'; a frame is a copy of it, the weeds,
 * the fish, the food and the bubbles, and only the rows that changed go to
 * the screen (face_draw.h).
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FISH        7
#define FOOD        9
#define BUBBLES     18
#define WEEDS       6
#define SAND_Y      202
#define X_MIN       12.0f
#define X_MAX       (FC_SW - 12.0f)
#define Y_MIN       26.0f
#define Y_MAX       (SAND_Y - 12.0f)

typedef struct {
    uint32_t body, tail, band;      /* band 0: none */
    float    fat;                   /* height over length */
} kind_t;

static const kind_t KIND[] = {
    { 0xFF7A1A, 0xFF9A3A, 0xFFFFFF, 0.55f },    /* clownfish */
    { 0x2A6AE0, 0xFFD23A, 0x12307A, 0.60f },    /* blue tang */
    { 0xFFD84A, 0xFFB020, 0,        0.75f },    /* a yellow, tall one */
    { 0xFF6FA0, 0xFFA8C8, 0,        0.50f },    /* a pink one */
};
#define KINDS (int)(sizeof(KIND) / sizeof(KIND[0]))

typedef struct {
    float x, y, vx, vy;
    float tx, ty;                   /* where it is going */
    float think;                    /* seconds until it picks another point */
    float flee;                     /* seconds left of a fright */
    float fx, fy;                   /* what it flees from */
    float size, speed, phase;
    uint8_t kind;
} fish_t;

typedef struct { float x, y; float life; bool on; } food_t;
typedef struct { float x, y, r, wob; bool on; } bubble_t;

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    uint16_t  *base, *work;
    bool       aod, night;
    float      t, bubble_in;
    fish_t     fish[FISH];
    food_t     food[FOOD];
    bubble_t   bub[BUBBLES];
    fc_grav_t  grav;
    float      last_mag;
    uint32_t   seed;
    char       last_hhmm[8], last_date[24];
} acuario_t;

/* -------------------------------------------------------------------------- */

static void build_base(acuario_t *a)
{
    uint16_t *b = a->base;
    const uint16_t top = fc_hex(a->night ? 0x06305A : 0x1278C0);
    const uint16_t deep = fc_hex(a->night ? 0x020A1C : 0x052A5A);
    for (int y = 0; y < FC_SH; y++) {
        uint16_t row = fd_lerp(top, deep, (float)y / SAND_Y);
        for (int x = 0; x < FC_SW; x++) {
            uint16_t c = row;
            /* shafts of light from the surface, fading with depth */
            int band = (x + y / 2 + 400) % 46;
            if (!a->night && band < 13 && y < 150) {
                c = fc_mix(c, 0xFFFF, (uint32_t)((1.0f - y / 150.0f) * 26));
            }
            b[y * FC_SW + x] = c;
        }
    }
    /* the sand, a little uneven, and two rocks */
    const uint16_t sand = fc_hex(a->night ? 0x5A4A30 : 0xD2B070);
    for (int x = 0; x < FC_SW; x++) {
        int top_y = SAND_Y + (int)(sinf(x * 0.11f) * 2.5f + sinf(x * 0.37f) * 1.2f);
        for (int y = top_y; y < FC_SH; y++) {
            b[y * FC_SW + x] = fd_dark(sand, (float)(y - top_y) / 60.0f);
        }
    }
    const uint16_t rock = fc_hex(a->night ? 0x1A1E28 : 0x4A5262);
    fd_ellipse(b, 38, SAND_Y + 2, 20, 11, 0, rock, 255);
    fd_ellipse(b, 30, SAND_Y - 3, 10, 7, 0.3f, fd_light(rock, 0.12f), 255);
    fd_ellipse(b, 152, SAND_Y + 3, 16, 8, 0, rock, 255);
}

static void new_target(acuario_t *a, fish_t *f)
{
    f->tx = X_MIN + fc_randf(&a->seed) * (X_MAX - X_MIN);
    f->ty = Y_MIN + fc_randf(&a->seed) * (Y_MAX - Y_MIN);
    f->think = 2.5f + fc_randf(&a->seed) * 5.0f;
}

static void spawn_bubble(acuario_t *a, float x, float y)
{
    for (int i = 0; i < BUBBLES; i++) {
        if (!a->bub[i].on) {
            a->bub[i] = (bubble_t){ x, y, 1.0f + fc_randf(&a->seed) * 1.8f,
                                    fc_randf(&a->seed) * 6.28f, true };
            return;
        }
    }
}

static void scare(acuario_t *a, float x, float y, float radius)
{
    for (int i = 0; i < FISH; i++) {
        fish_t *f = &a->fish[i];
        float dx = f->x - x, dy = f->y - y;
        if (dx * dx + dy * dy < radius * radius) {
            f->flee = 1.0f + fc_randf(&a->seed) * 0.6f;
            f->fx = x;
            f->fy = y;
        }
    }
}

/* -------------------------------------------------------------------------- */

static void step(acuario_t *a, float dt)
{
    float slow = a->night ? 0.45f : 1.0f;
    for (int i = 0; i < FISH; i++) {
        fish_t *f = &a->fish[i];
        float gx = f->tx, gy = f->ty, speed = f->speed * slow;

        /* the nearest crumb of food wins over wandering */
        food_t *near = NULL;
        float best = 1e9f;
        for (int k = 0; k < FOOD; k++) {
            if (!a->food[k].on) continue;
            float dx = a->food[k].x - f->x, dy = a->food[k].y - f->y;
            float d = dx * dx + dy * dy;
            if (d < best) { best = d; near = &a->food[k]; }
        }
        if (f->flee > 0) {
            f->flee -= dt;
            float dx = f->x - f->fx, dy = f->y - f->fy;
            float d = sqrtf(dx * dx + dy * dy) + 0.01f;
            gx = f->x + dx / d * 40;
            gy = f->y + dy / d * 40;
            speed = f->speed * 3.4f;
        } else if (near) {
            gx = near->x;
            gy = near->y;
            speed = f->speed * 1.9f;
            if (best < 16.0f) {
                near->on = false;           /* eaten */
                spawn_bubble(a, f->x, f->y - 2);
            }
        } else {
            f->think -= dt;
            float dx = f->tx - f->x, dy = f->ty - f->y;
            if (f->think <= 0 || dx * dx + dy * dy < 36.0f) {
                new_target(a, f);
            }
        }
        float dx = gx - f->x, dy = gy - f->y;
        float d = sqrtf(dx * dx + dy * dy) + 0.01f;
        float k = LV_MIN(1.0f, dt * (f->flee > 0 ? 6.0f : 1.6f));
        f->vx += (dx / d * speed - f->vx) * k;
        f->vy += (dy / d * speed * 0.6f - f->vy) * k;      /* fish swim flatter than they turn */
        f->x += f->vx * dt;
        f->y += f->vy * dt;
        f->x = LV_CLAMP(X_MIN - 6, f->x, X_MAX + 6);
        f->y = LV_CLAMP(Y_MIN - 4, f->y, Y_MAX + 4);
        f->phase += dt * (5.0f + sqrtf(f->vx * f->vx + f->vy * f->vy) * 0.35f);
    }

    for (int k = 0; k < FOOD; k++) {
        food_t *p = &a->food[k];
        if (!p->on) continue;
        if (p->y < SAND_Y - 2) {
            p->y += 9.0f * dt;
            p->x += sinf(a->t * 3.0f + k) * 2.0f * dt;
        } else if ((p->life -= dt) <= 0) {
            p->on = false;                  /* nobody wanted it */
        }
    }

    /* bubbles go against gravity: turn the wrist and "up" turns with it */
    float ux = -a->grav.x, uy = -a->grav.y;
    float ul = sqrtf(ux * ux + uy * uy);
    if (ul < 0.3f) { ux = 0; uy = -1; } else { ux /= ul; uy /= ul; }
    a->bubble_in -= dt;
    if (a->bubble_in <= 0) {
        spawn_bubble(a, 150 + fc_randf(&a->seed) * 4, SAND_Y - 6);
        a->bubble_in = 0.35f + fc_randf(&a->seed) * 0.9f;
    }
    for (int i = 0; i < BUBBLES; i++) {
        bubble_t *u = &a->bub[i];
        if (!u->on) continue;
        u->wob += dt * 5.0f;
        float sp = 22.0f + u->r * 6.0f;
        u->x += (ux * sp + -uy * sinf(u->wob) * 5.0f) * dt;
        u->y += (uy * sp + ux * sinf(u->wob) * 5.0f) * dt;
        if (u->y < 2 || u->y > FC_SH || u->x < -4 || u->x > FC_SW + 4) {
            u->on = false;
        }
    }
}

static void draw_fish(uint16_t *b, const fish_t *f, float dim)
{
    const kind_t *k = &KIND[f->kind];
    float dir = f->vx >= 0 ? 1.0f : -1.0f;
    float s = f->size, h = s * k->fat;
    float tilt = LV_CLAMP(-0.5f, f->vy / (fabsf(f->vx) + 6.0f), 0.5f) * dir;
    float wag = sinf(f->phase) * s * 0.45f;
    uint16_t body = fc_scale(fc_hex(k->body), (uint32_t)(dim * 255));
    uint16_t tail = fc_scale(fc_hex(k->tail), (uint32_t)(dim * 255));

    float tx = f->x - dir * s * 0.75f, ty = f->y - tilt * s * 0.6f * dir;
    fd_tri(b, tx, ty, tx - dir * s * 0.95f, ty - h * 0.9f + wag, tx - dir * s * 0.95f, ty + h * 0.9f + wag,
           tail, 255);
    fd_tri(b, f->x - dir * s * 0.2f, f->y - h * 0.7f, f->x + dir * s * 0.3f, f->y - h * 0.8f,
           f->x - dir * s * 0.45f, f->y - h * 1.5f, tail, 255);
    fd_ellipse(b, f->x, f->y, s, h, tilt, body, 255);
    if (k->band) {
        uint16_t band = fc_scale(fc_hex(k->band), (uint32_t)(dim * 255));
        fd_ellipse(b, f->x + dir * s * 0.25f, f->y, s * 0.13f, h * 0.92f, tilt, band, 255);
        fd_ellipse(b, f->x - dir * s * 0.35f, f->y, s * 0.11f, h * 0.75f, tilt, band, 255);
    }
    float ex = f->x + dir * s * 0.58f, ey = f->y - h * 0.18f + tilt * s * 0.5f * dir;
    fd_circle(b, ex, ey, s * 0.2f, fc_scale(0xFFFF, (uint32_t)(dim * 255)), 255);
    fd_circle(b, ex + dir * s * 0.05f, ey, s * 0.1f, 0x0000, 255);
}

static void render(acuario_t *a)
{
    uint16_t *b = a->work;
    memcpy(b, a->base, FC_SW * FC_SH * sizeof(uint16_t));
    float dim = a->aod ? 0.28f : 1.0f;

    /* weeds: each a chain of short lines, leaning more towards its tip */
    for (int w = 0; w < WEEDS; w++) {
        static const uint8_t WX[WEEDS] = { 14, 58, 70, 112, 126, 172 };
        static const uint8_t WH[WEEDS] = { 46, 30, 52, 38, 26, 44 };
        float x = WX[w], y = SAND_Y + 2;
        uint16_t green = fc_scale(fc_hex(w & 1 ? 0x2FA860 : 0x1F8A4A), (uint32_t)(dim * (a->night ? 140 : 255)));
        int segs = WH[w] / 7;
        for (int s = 0; s < segs; s++) {
            float lean = sinf(a->t * 1.3f + w * 1.7f + s * 0.5f) * (s + 1) * 0.9f;
            float nx = WX[w] + lean, ny = y - 7;
            fd_line(b, x, y, nx, ny, 4.0f - s * 0.35f, green, 255);
            x = nx;
            y = ny;
        }
    }
    for (int k = 0; k < FOOD; k++) {
        if (a->food[k].on) {
            fd_circle(b, a->food[k].x, a->food[k].y, 1.6f, fc_hex(0xB8743A), 255);
        }
    }
    for (int i = 0; i < FISH; i++) {
        draw_fish(b, &a->fish[i], dim * (a->night ? 0.7f : 1.0f));
    }
    for (int i = 0; i < BUBBLES; i++) {
        const bubble_t *u = &a->bub[i];
        if (u->on) {
            fd_circle(b, u->x, u->y, u->r, fc_hex(0xCFF0FF), 60);
            fd_circle(b, u->x - u->r * 0.3f, u->y - u->r * 0.3f, u->r * 0.4f, 0xFFFF, 200);
        }
    }
    if (a->aod) {
        /* dimmed: the tank goes dark around the shapes */
        for (int i = 0; i < FC_SW * FC_SH; i++) {
            b[i] = fc_scale(b[i], 70);
        }
    }
    fd_show(&a->fc, b);
}

/* -------------------------------------------------------------------------- */

static void text_update(acuario_t *a, const struct tm *now, bool force)
{
    char hhmm[8], date[24];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    snprintf(date, sizeof(date), "%s %d %s", aos_day_name(now->tm_wday), now->tm_mday,
             aos_month_name(now->tm_mon));
    if (force || strcmp(hhmm, a->last_hhmm) != 0) {
        fc_mask_t old = a->fc.mask[0];
        a->fc.mask[0].a = NULL;
        snprintf(a->last_hhmm, sizeof(a->last_hhmm), "%s", hhmm);
        fc_mask_text(&a->fc.mask[0], a->root, hhmm, aos_font_huge, 2.0f, FC_W / 2, 120,
                     fc_hex(a->aod ? 0x8A8A8A : 0xFFFFFF), true);
        fc_present_mask(&a->fc, &old);
        free(old.a);
        fc_present_mask(&a->fc, &a->fc.mask[0]);
    }
    if (force || strcmp(date, a->last_date) != 0) {
        fc_mask_t old = a->fc.mask[1];
        a->fc.mask[1].a = NULL;
        snprintf(a->last_date, sizeof(a->last_date), "%s", date);
        fc_mask_text(&a->fc.mask[1], a->root, date, aos_font_body, 1.0f, FC_W / 2, 196,
                     fc_hex(a->aod ? 0x5A5A5A : 0xE8F6FF), true);
        fc_present_mask(&a->fc, &old);
        free(old.a);
        fc_present_mask(&a->fc, &a->fc.mask[1]);
    }
}

/* The hour the tank goes by: the clock's, or ACUARIO_HORA's while developing
 * (on the board getenv() is always NULL). */
static bool is_night(const struct tm *now)
{
    const char *h = getenv("ACUARIO_HORA");
    int hour = h ? atoi(h) : now->tm_hour;
    return hour >= 22 || hour < 7;
}

static void day_update(acuario_t *a, const struct tm *now)
{
    bool night = is_night(now);
    if (night != a->night) {
        a->night = night;
        build_base(a);
    }
}

static void frame(void *ctx, uint32_t dt_ms)
{
    acuario_t *a = ctx;
    if (!a || a->aod) {
        return;
    }
    float dt = dt_ms / 1000.0f;
    a->t += dt;
    float gx, gy, gz;
    if (aos_watchface_gravity(&gx, &gy, &gz)) {
        float mag = sqrtf(gx * gx + gy * gy + gz * gz);
        if (a->last_mag > 0 && fabsf(mag - a->last_mag) > 0.6f) {
            scare(a, FC_SW / 2.0f, FC_SH / 2.0f, 400);     /* a shake: everyone */
        }
        a->last_mag = mag;
    }
    fc_grav_update(&a->grav, 0.15f);
    step(a, dt);
    render(a);
}

static void *create(lv_obj_t *root)
{
    acuario_t *a = lv_malloc_zeroed(sizeof(acuario_t));
    if (!a) {
        return NULL;
    }
    a->base = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    a->work = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    if (!a->base || !a->work || !fc_init(&a->fc, root)) {
        free(a->base);
        free(a->work);
        lv_free(a);
        return NULL;
    }
    a->root = root;
    a->seed = (uint32_t)aos_hal_uptime_ms() * 2246822519u | 1u;
    struct tm now;
    aos_hal_time_now(&now);
    a->night = is_night(&now);
    build_base(a);
    for (int i = 0; i < FISH; i++) {
        fish_t *f = &a->fish[i];
        f->kind = (uint8_t)(i % KINDS);
        f->size = 6.0f + fc_randf(&a->seed) * 5.0f;
        f->speed = 14.0f + fc_randf(&a->seed) * 12.0f;
        f->x = X_MIN + fc_randf(&a->seed) * (X_MAX - X_MIN);
        f->y = Y_MIN + fc_randf(&a->seed) * (Y_MAX - Y_MIN);
        f->vx = (i & 1) ? f->speed : -f->speed;
        new_target(a, f);
    }
    fc_grav_update(&a->grav, 1.0f);
    text_update(a, &now, true);
    render(a);
    return a;
}

static void refresh(void *ctx, const struct tm *now)
{
    acuario_t *a = ctx;
    if (!a) {
        return;
    }
    day_update(a, now);
    text_update(a, now, false);
}

static void set_aod(void *ctx, bool aod)
{
    acuario_t *a = ctx;
    if (!a || a->aod == aod) {
        return;
    }
    a->aod = aod;
    struct tm now;
    aos_hal_time_now(&now);
    text_update(a, &now, true);
    render(a);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    acuario_t *a = ctx;
    if (!a || what != AOS_FACE_TOUCH_TAP || a->aod) {
        return false;
    }
    float sx = x / 2.0f, sy = y / 2.0f;
    for (int i = 0; i < FISH; i++) {
        const fish_t *f = &a->fish[i];
        float dx = f->x - sx, dy = f->y - sy, r = f->size + 9;
        if (dx * dx + dy * dy < r * r) {
            scare(a, sx, sy, 34);           /* on a fish: it darts, and its neighbours */
            return true;
        }
    }
    /* on the water: a pinch of food, sinking from there */
    for (int k = 0, made = 0; k < FOOD && made < 3; k++) {
        if (!a->food[k].on) {
            a->food[k] = (food_t){ sx + (made - 1) * 5.0f, LV_MIN(sy, SAND_Y - 20.0f) + made * 3.0f,
                                   6.0f, true };
            made++;
        }
    }
    return true;
}

static void destroy(void *ctx)
{
    acuario_t *a = ctx;
    if (!a) {
        return;
    }
    fc_deinit(&a->fc);
    free(a->base);
    free(a->work);
    lv_free(a);
}

static bool acuario_get(aos_watchface_t *face)
{
    face->id       = "face.acuario";
    face->name     = N_("Acuario");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 45;
    return true;
}

AOS_FACE_ENTRY(acuario_get);
