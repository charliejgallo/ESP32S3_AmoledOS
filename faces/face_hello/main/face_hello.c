/*
 * AmoledOS - Hello: the template for a watchface of your own.
 *
 * Copy this folder, rename it (the folder, this file, the two CMakeLists and
 * the id below) and start from here; docs/FACE-GUIDE.md walks through it.
 * It shows the whole contract in a page:
 *
 *   - the time and the date, LVGL labels: the simplest way to draw, right
 *     for anything that changes once a minute;
 *   - a ball that rolls to wherever "down" is, moved in frame() with
 *     aos_watchface_gravity(): an animation that costs nothing dimmed, and
 *     that stops asking for drawing once the ball has settled;
 *   - a tap changes its colour (touch(), with AOS_FACE_FLAG_TOUCH); the long
 *     press is left to the picker, as on every face;
 *   - dimmed: grey, no ball, almost all black (an AMOLED pixel that is off
 *     costs nothing).
 *
 * It is not in the release's faces: it is the one to copy.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BALL        34
#define MARGIN      24

static const uint32_t COLOURS[] = { 0x0A84FF, 0x30D158, 0xFF9F0A, 0xFF375F, 0xBF5AF2 };
#define N_COLOURS   (int)(sizeof(COLOURS) / sizeof(COLOURS[0]))

typedef struct {
    lv_obj_t *time;
    lv_obj_t *date;
    lv_obj_t *ball;
    float     x, y;         /* the ball's centre, screen pixels */
    float     vx, vy;       /* pixels a second */
    int       colour;
    bool      aod;
    char      last[8];
} hello_t;

static void *create(lv_obj_t *root)
{
    hello_t *h = lv_malloc_zeroed(sizeof(hello_t));
    if (!h) {
        return NULL;
    }
    /* the ball first, so the time is drawn over it */
    h->ball = lv_obj_create(root);
    lv_obj_remove_style_all(h->ball);
    lv_obj_set_size(h->ball, BALL, BALL);
    lv_obj_set_style_radius(h->ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(h->ball, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(h->ball, lv_color_hex(COLOURS[0]), 0);
    h->x = AOS_SCREEN_W / 2;
    h->y = AOS_SCREEN_H - MARGIN - BALL / 2;
    lv_obj_set_pos(h->ball, (int32_t)h->x - BALL / 2, (int32_t)h->y - BALL / 2);

    /* the largest font is 48 px: scaled, it fills the screen (it changes once
     * a minute, so the cost of drawing it scaled does not matter) */
    h->time = aos_label_scaled(root, "--:--", aos_font_huge, AOS_C_TEXT, 400);
    lv_obj_align(h->time, LV_ALIGN_CENTER, 0, -30);

    h->date = aos_label(root, "", aos_font_body, AOS_C_ACCENT);
    lv_obj_align(h->date, LV_ALIGN_CENTER, 0, 40);
    return h;
}

static void refresh(void *ctx, const struct tm *now)
{
    hello_t *h = ctx;
    if (!h) {
        return;
    }
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(hhmm, h->last) == 0) {
        return;                 /* 5 times a second: only a new minute draws */
    }
    snprintf(h->last, sizeof(h->last), "%s", hhmm);
    lv_label_set_text(h->time, hhmm);
    lv_label_set_text_fmt(h->date, "%s %d %s", aos_day_name(now->tm_wday),
                          now->tm_mday, aos_month_name(now->tm_mon));
}

/* Only while the face is lit on the screen; dimmed, the host has no timer. */
static void frame(void *ctx, uint32_t dt_ms)
{
    hello_t *h = ctx;
    float gx, gy;
    if (!h || h->aod || !aos_watchface_gravity(&gx, &gy, NULL)) {
        return;
    }
    float dt = dt_ms / 1000.0f;
    /* gravity in g along the glass, pixels a second squared */
    h->vx = (h->vx + gx * 900.0f * dt) * 0.985f;
    h->vy = (h->vy + gy * 900.0f * dt) * 0.985f;
    float nx = h->x + h->vx * dt, ny = h->y + h->vy * dt;
    const float lo = MARGIN + BALL / 2;
    const float hx = AOS_SCREEN_W - lo, hy = AOS_SCREEN_H - lo;
    if (nx < lo || nx > hx) {
        h->vx = -h->vx * 0.5f;
        nx = nx < lo ? lo : hx;
    }
    if (ny < lo || ny > hy) {
        h->vy = -h->vy * 0.5f;
        ny = ny < lo ? lo : hy;
    }
    /* a ball at rest is not moved: moving an object, even by nothing, makes
     * LVGL draw its area again */
    if ((int32_t)nx != (int32_t)h->x || (int32_t)ny != (int32_t)h->y) {
        lv_obj_set_pos(h->ball, (int32_t)nx - BALL / 2, (int32_t)ny - BALL / 2);
    }
    h->x = nx;
    h->y = ny;
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    (void)x;
    (void)y;
    hello_t *h = ctx;
    if (!h || what != AOS_FACE_TOUCH_TAP) {
        return false;           /* the long press opens the picker */
    }
    h->colour = (h->colour + 1) % N_COLOURS;
    lv_obj_set_style_bg_color(h->ball, lv_color_hex(COLOURS[h->colour]), 0);
    return true;
}

static void set_aod(void *ctx, bool aod)
{
    hello_t *h = ctx;
    if (!h) {
        return;
    }
    h->aod = aod;
    lv_obj_set_style_text_color(h->time, aod ? lv_color_hex(0x8A8A8A) : AOS_C_TEXT, 0);
    lv_obj_set_style_text_color(h->date, aod ? lv_color_hex(0x5A5A5A) : AOS_C_ACCENT, 0);
    if (aod) {
        lv_obj_add_flag(h->ball, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(h->ball, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Everything the face made that is not under its root: here only the
 * context. The module is unloaded right after this returns. */
static void destroy(void *ctx)
{
    lv_free(ctx);
}

static bool hello_get(aos_watchface_t *face)
{
    face->id       = "face.hello";      /* with a dot; its catalogue is face.hello.lang */
    face->name     = N_("Hola");        /* Spanish is the source language */
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

AOS_FACE_ENTRY(hello_get);
