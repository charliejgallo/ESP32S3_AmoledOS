/*
 * AmoledOS - Doom.
 *
 * Chocolate Doom through doomgeneric, running in the app's worker on core 0
 * (port/, doomgeneric/). This file is only the watch's side: the picture at
 * the top, blitted straight to the panel from LVGL's task, and a pad below.
 *
 *     y   0..229   Doom, 320x200 scaled to 368x230. Pressing it fires.
 *     y 230..447   the pad: MENU / MAP / WEAPON on a strip across it, then
 *                  a floating stick on the left and USE and FIRE on the right.
 *
 * Two fingers (v0.6.0, aos_touch_points): each finger owns whatever it
 * landed on until it lifts - the stick, a button, or the picture (fire) -
 * so the left thumb walks while the right one shoots. The stick keeps
 * working when its thumb slides out of its half. The side button still
 * fires while held. The stick and FIRE sit level with each other on
 * purpose: the chip pairs two fingers cleanly side by side, and can swap
 * them on the ↗↙ diagonal (docs/GESTURES.md).
 *
 * On hardware without a second finger (aos_gesture_multitouch() false) it
 * falls back to one finger through LVGL, as before.
 *
 * The WAD is not in the app: the card's doom/ folder is searched for the
 * full game first and the shareware DOOM1.WAD last (port/dg_system.c). The
 * config and the saves go in the same folder.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_gesture.h"
#include "lvgl.h"

#include "doom_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS     8
#define PAD_Y       DP_H

/* the stick: a base that appears where the thumb lands, inside its corner */
#define STICK_ZONE_W    190
#define STICK_TOP       278         /* below the strip of small buttons */
#define STICK_R         58          /* full deflection */
#define STICK_KNOB      26
#define TOUCH_BOTTOM    410         /* the glass reads reliably above this */

/* the buttons: a strip across the pad's top (wide enough for "WAFFE"), then
 * USE and FIRE on the right, level with the stick */
typedef struct {
    int x, y, w, h;                 /* screen box */
    bool round;
} box_t;

static const box_t BOXES[DP_BTN_N] = {
    [DP_BTN_FIRE]   = { 272, 298, 92, 92, true },
    [DP_BTN_USE]    = { 194, 318, 72, 72, true },
    [DP_BTN_MENU]   = {   6, 238, 112, 34, false },
    [DP_BTN_MAP]    = { 128, 238, 112, 34, false },
    [DP_BTN_WEAPON] = { 250, 238, 112, 34, false },
};

enum {
    OWN_NONE = -1,
    OWN_STICK = 100,
    OWN_PICTURE = 101,
};

typedef struct {
    aos_app_t  *self;
    lv_obj_t   *root;
    lv_obj_t   *canvas;             /* the simulator's way to see a frame */
    uint16_t   *cv;
    lv_obj_t   *touch;
    lv_obj_t   *msg;                /* loading, errors */
    lv_obj_t   *btn[DP_BTN_N];
    lv_obj_t   *base, *knob;
    lv_timer_t *timer;

    bool        running;
    bool        sound;
    bool        shown_any;
    bool        want_exit;
    bool        exit_on_tap;
    int         owner;              /* one finger (the fallback) */
    int         cx, cy;             /* the stick's centre */

    /* two fingers: what each slot of aos_touch_points() owns */
    bool        multi;
    uint8_t     fid[2];             /* the finger's id, 0 = none */
    int         fown[2];
    bool        hw_fire;            /* the side button, held */
    bool        btn_on[DP_BTN_N];   /* what Doom was last told */

    uint32_t    hw_press_ms;
    uint32_t    fps_ms, fps_frames, fps_blits, blits;
    uint32_t    cyc_music, cyc_total;
} app_t;

#ifdef AOS_SIM
/* The engine's globals are initialised once per process: the board gets a
 * fresh copy with every dlopen, the simulator (where the app is built in)
 * does not. A second game in one simulator run would start on the first
 * one's leftovers, so it is refused instead. */
static bool s_sim_ran;
#endif

/* --------------------------------------------------------------------------
 * The pad
 * -------------------------------------------------------------------------- */

static lv_obj_t *pad_button(lv_obj_t *root, const box_t *b, const char *text)
{
    lv_obj_t *o = lv_obj_create(root);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, b->x, b->y);
    lv_obj_set_size(o, b->w, b->h);
    lv_obj_set_style_radius(o, b->round ? LV_RADIUS_CIRCLE : 10, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x3A1010), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0x8A2A1A), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xF0D0B0), 0);
    lv_obj_set_style_text_font(l, aos_font_small, 0);
    lv_obj_center(l);
    return o;
}

static void pad_lit(app_t *a, int btn, bool on)
{
    if (btn < 0 || btn >= DP_BTN_N || !a->btn[btn]) return;
    lv_obj_set_style_bg_color(a->btn[btn], lv_color_hex(on ? 0xB03018 : 0x3A1010), 0);
}

static void stick_show(app_t *a, bool on)
{
    if (on) {
        lv_obj_remove_flag(a->base, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(a->base, a->cx - STICK_R, a->cy - STICK_R);
        lv_obj_set_pos(a->knob, a->cx - STICK_KNOB, a->cy - STICK_KNOB);
    } else {
        /* the resting stick, drawn in the middle of its half */
        a->cx = STICK_ZONE_W / 2;
        a->cy = (STICK_TOP + TOUCH_BOTTOM) / 2;
        lv_obj_set_pos(a->base, a->cx - STICK_R, a->cy - STICK_R);
        lv_obj_set_pos(a->knob, a->cx - STICK_KNOB, a->cy - STICK_KNOB);
    }
}

static void build_pad(app_t *a)
{
    lv_obj_t *r = a->root;
    a->base = lv_obj_create(r);
    lv_obj_remove_style_all(a->base);
    lv_obj_set_size(a->base, STICK_R * 2, STICK_R * 2);
    lv_obj_set_style_radius(a->base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(a->base, lv_color_hex(0x8A2A1A), 0);
    lv_obj_set_style_border_width(a->base, 3, 0);
    lv_obj_set_style_bg_color(a->base, lv_color_hex(0x200808), 0);
    lv_obj_set_style_bg_opa(a->base, LV_OPA_COVER, 0);
    lv_obj_remove_flag(a->base, LV_OBJ_FLAG_CLICKABLE);

    a->knob = lv_obj_create(r);
    lv_obj_remove_style_all(a->knob);
    lv_obj_set_size(a->knob, STICK_KNOB * 2, STICK_KNOB * 2);
    lv_obj_set_style_radius(a->knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a->knob, lv_color_hex(0x9A3420), 0);
    lv_obj_set_style_bg_opa(a->knob, LV_OPA_COVER, 0);
    lv_obj_remove_flag(a->knob, LV_OBJ_FLAG_CLICKABLE);
    stick_show(a, false);

    a->btn[DP_BTN_FIRE]   = pad_button(r, &BOXES[DP_BTN_FIRE],   _("FUEGO"));
    a->btn[DP_BTN_USE]    = pad_button(r, &BOXES[DP_BTN_USE],    _("USAR"));
    a->btn[DP_BTN_MENU]   = pad_button(r, &BOXES[DP_BTN_MENU],   _("MENÚ"));
    a->btn[DP_BTN_MAP]    = pad_button(r, &BOXES[DP_BTN_MAP],    _("MAPA"));
    a->btn[DP_BTN_WEAPON] = pad_button(r, &BOXES[DP_BTN_WEAPON], _("ARMA"));
}

static int hit_button(int x, int y)
{
    /* a little slack around each one: thumbs are wider than the drawing */
    for (int i = 0; i < DP_BTN_N; i++) {
        const box_t *b = &BOXES[i];
        if (x >= b->x - 6 && x < b->x + b->w + 6 && y >= b->y - 6 && y < b->y + b->h + 6) {
            return i;
        }
    }
    return -1;
}

static void stick_update(app_t *a, int x, int y)
{
    int dx = x - a->cx, dy = y - a->cy;
    int d2 = dx * dx + dy * dy;
    int kx = dx, ky = dy;
    if (d2 > STICK_R * STICK_R) {
        /* the knob stays on the rim; the value is full deflection */
        int d = 1;
        while (d * d < d2) d++;
        kx = dx * STICK_R / d;
        ky = dy * STICK_R / d;
    }
    lv_obj_set_pos(a->knob, a->cx + kx - STICK_KNOB, a->cy + ky - STICK_KNOB);
    dp_stick(kx * 100 / STICK_R, ky * 100 / STICK_R);
}

static void release_owner(app_t *a)
{
    if (a->owner == OWN_STICK) {
        dp_stick(0, 0);
        stick_show(a, false);
    } else if (a->owner == OWN_PICTURE) {
        dp_button(DP_BTN_FIRE, false);
    } else if (a->owner >= 0 && a->owner < DP_BTN_N) {
        dp_button(a->owner, false);
        pad_lit(a, a->owner, false);
    }
    a->owner = OWN_NONE;
}

static void touch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED && a->exit_on_tap) {
        a->want_exit = true;
        return;
    }
    if (a->multi) {
        return;                         /* the pad is polled: pad_poll() */
    }

    if (code == LV_EVENT_PRESSED) {
        if (!a->running) return;
        release_owner(a);
        int b = hit_button(p.x, p.y);
        if (p.y < PAD_Y) {
            a->owner = OWN_PICTURE;
            dp_button(DP_BTN_FIRE, true);
        } else if (b >= 0) {
            a->owner = b;
            dp_button(b, true);
            pad_lit(a, b, true);
        } else if (p.x < STICK_ZONE_W + 10 && p.y >= STICK_TOP - 6) {
            a->owner = OWN_STICK;
            /* the base comes to the thumb, kept whole inside the pad */
            int cx = p.x, cy = p.y;
            if (cx < STICK_R + 2) cx = STICK_R + 2;
            if (cx > STICK_ZONE_W - 4) cx = STICK_ZONE_W - 4;
            if (cy < STICK_TOP + STICK_R + 2) cy = STICK_TOP + STICK_R + 2;
            if (cy > TOUCH_BOTTOM - STICK_R / 2) cy = TOUCH_BOTTOM - STICK_R / 2;
            a->cx = cx;
            a->cy = cy;
            stick_show(a, true);
            stick_update(a, p.x, p.y);
        }
    } else if (code == LV_EVENT_PRESSING) {
        if (a->owner == OWN_STICK) stick_update(a, p.x, p.y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        release_owner(a);
    }
}

/* --------------------------------------------------------------------------
 * Two fingers
 *
 * Polled on every frame (8 ms) instead of LVGL's events, which only know
 * one finger. Each finger claims what it landed on, exactly as the one
 * finger did; Doom is then told the union: a button is down while any
 * finger (or, for FIRE, the side button) holds it.
 * -------------------------------------------------------------------------- */

static int claim(app_t *a, int x, int y, int other_owner)
{
    int b = hit_button(x, y);
    if (y < PAD_Y) {
        return OWN_PICTURE;
    }
    if (b >= 0) {
        return b;
    }
    if (x < STICK_ZONE_W + 10 && y >= STICK_TOP - 6 && other_owner != OWN_STICK) {
        /* the base comes to the thumb, kept whole inside the pad */
        int cx = x, cy = y;
        if (cx < STICK_R + 2) cx = STICK_R + 2;
        if (cx > STICK_ZONE_W - 4) cx = STICK_ZONE_W - 4;
        if (cy < STICK_TOP + STICK_R + 2) cy = STICK_TOP + STICK_R + 2;
        if (cy > TOUCH_BOTTOM - STICK_R / 2) cy = TOUCH_BOTTOM - STICK_R / 2;
        a->cx = cx;
        a->cy = cy;
        stick_show(a, true);
        return OWN_STICK;
    }
    return OWN_NONE;
}

static void pad_apply(app_t *a)
{
    bool want[DP_BTN_N] = { false };
    for (int i = 0; i < 2; i++) {
        if (a->fown[i] == OWN_PICTURE) {
            want[DP_BTN_FIRE] = true;
        } else if (a->fown[i] >= 0 && a->fown[i] < DP_BTN_N) {
            want[a->fown[i]] = true;
        }
    }
    if (a->hw_fire) {
        want[DP_BTN_FIRE] = true;
    }
    for (int b = 0; b < DP_BTN_N; b++) {
        if (want[b] != a->btn_on[b]) {
            a->btn_on[b] = want[b];
            dp_button(b, want[b]);
            pad_lit(a, b, want[b]);
        }
    }
}

static void pad_release_all(app_t *a)
{
    for (int i = 0; i < 2; i++) {
        if (a->fown[i] == OWN_STICK) {
            dp_stick(0, 0);
            stick_show(a, false);
        }
        a->fown[i] = OWN_NONE;
        a->fid[i] = 0;
    }
    a->hw_fire = false;
    pad_apply(a);
}

static void pad_poll(app_t *a)
{
    aos_touch_point_t pts[2];
    aos_touch_points(pts);
    for (int i = 0; i < 2; i++) {
        uint8_t id = pts[i].down ? pts[i].id : 0;
        if (id != a->fid[i]) {
            if (a->fown[i] == OWN_STICK) {      /* the finger that had it left */
                dp_stick(0, 0);
                stick_show(a, false);
            }
            a->fown[i] = id ? claim(a, (int)pts[i].x, (int)pts[i].y, a->fown[1 - i])
                            : OWN_NONE;
            a->fid[i] = id;
        }
        if (id && a->fown[i] == OWN_STICK) {
            stick_update(a, (int)pts[i].x, (int)pts[i].y);
        }
    }
    pad_apply(a);
}

/* --------------------------------------------------------------------------
 * Messages over the picture
 * -------------------------------------------------------------------------- */

static void show_msg(app_t *a, const char *text, bool tap_to_exit)
{
    lv_label_set_text(a->msg, text);
    lv_obj_remove_flag(a->msg, LV_OBJ_FLAG_HIDDEN);
    a->exit_on_tap = tap_to_exit;
}

static void no_wad(app_t *a)
{
    char text[400];
    snprintf(text, sizeof text, "%s\n\n%s\n%s\n\n%s",
             _("No hay ningún WAD"),
             _("Copiá doom1.wad (shareware) o doom.wad a la carpeta"),
             dp_data_dir(),
             _("Tocá para salir"));
    show_msg(a, text, true);
}

/* --------------------------------------------------------------------------
 * The frame loop
 * -------------------------------------------------------------------------- */

static void push_frame(app_t *a)
{
    const uint16_t *f = dp_frame_take();
    if (!f) return;
    if (!a->shown_any) {
        /* the "loading" label goes before the first blit, drawn away now:
         * LVGL's own redraw of that area would land on top of the frame */
        a->shown_any = true;
        lv_obj_add_flag(a->msg, LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(NULL);
    }
    if (!aos_hal_display_blit(0, 0, DP_W, DP_H, f)) {
        /* the simulator (or the panel asleep): through the canvas */
        memcpy(a->cv, f, (size_t)DP_W * DP_H * 2);
        lv_obj_invalidate(a->canvas);
    }
    dp_frame_blitted();
    a->blits++;
}

static void frame(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;
    }
    if (!a->running) return;

    dp_state_t st = dp_state();
    if (st == DP_QUIT) {
        a->running = false;
        aos_ui_back();
        return;
    }
    if (st == DP_ERROR || st == DP_STOPPED) {
        a->running = false;
        release_owner(a);
        if (a->multi) pad_release_all(a);
        char text[300];
        snprintf(text, sizeof text, "%s\n\n%s\n\n%s", _("Doom se detuvo"), dp_error(),
                 _("Tocá para salir"));
        show_msg(a, text, true);
        return;
    }
    if (a->multi) {
        pad_poll(a);
    }
    push_frame(a);

    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (!a->fps_ms) {
        a->fps_ms = now;
        a->fps_frames = dp_frames();
        a->fps_blits = a->blits;
    } else if (now - a->fps_ms >= 5000) {
        uint32_t fr = dp_frames() - a->fps_frames, bl = a->blits - a->fps_blits;
        uint32_t fi = 0, fp = 0;
        aos_hal_heap_info(&fi, &fp);
        /* the mixer's share of core 0: cycles over 240 MHz */
        uint32_t cm, ct;
        dp_audio_cycles(&cm, &ct);
        uint32_t dt = now - a->fps_ms;
        unsigned pm = (unsigned)((uint64_t)(cm - a->cyc_music) * 1000 / 240000 / dt);
        unsigned pt = (unsigned)((uint64_t)(ct - a->cyc_total) * 1000 / 240000 / dt);
        a->cyc_music = cm;
        a->cyc_total = ct;
        aos_hal_log("doom", "%s: %u.%u fps rendered, %u.%u shown | audio %u.%u%% of core 0 "
                    "(music %u.%u%%) | internal %u B, psram %u B",
                    dp_where(),
                    (unsigned)(fr * 10000 / (now - a->fps_ms) / 10),
                    (unsigned)(fr * 10000 / (now - a->fps_ms) % 10),
                    (unsigned)(bl * 10000 / (now - a->fps_ms) / 10),
                    (unsigned)(bl * 10000 / (now - a->fps_ms) % 10),
                    pt / 10, pt % 10, pm / 10, pm % 10,
                    (unsigned)fi, (unsigned)fp);
        a->fps_ms = now;
        a->fps_frames = dp_frames();
        a->fps_blits = a->blits;
    }
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static bool doom_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a || !a->running) return false;    /* the button leaves, as everywhere */
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    bool no_finger = a->multi ? (!a->fid[0] && !a->fid[1]) : a->owner == OWN_NONE;
    if (action == AOS_BUTTON_PRESS) {
        a->hw_press_ms = now;
        if (a->multi) { a->hw_fire = true; pad_apply(a); }
        else          dp_button(DP_BTN_FIRE, true);
    } else {
        if (a->multi) { a->hw_fire = false; pad_apply(a); }
        else          dp_button(DP_BTN_FIRE, false);
        /* the way out if the game ever stops answering: five seconds held
         * with no finger on the glass. Doom's own Quit Game is the usual one */
        if (action == AOS_BUTTON_LONG && no_finger && now - a->hw_press_ms > 5000) {
            a->want_exit = true;
        }
    }
    return true;
}

static bool doom_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    /* the back gesture is off (NO_SWIPE); while playing, Doom's MENU is it */
    return a && a->running;
}

static void *doom_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    a->root = root;
    a->owner = OWN_NONE;
    a->multi = aos_gesture_multitouch();
    a->fown[0] = a->fown[1] = OWN_NONE;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("doom", "opening | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    a->cv = (uint16_t *)malloc((size_t)DP_W * DP_H * 2);
    if (a->cv) {
        memset(a->cv, 0, (size_t)DP_W * DP_H * 2);
        a->canvas = lv_canvas_create(root);
        lv_canvas_set_buffer(a->canvas, a->cv, DP_W, DP_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(a->canvas, 0, 0);
        lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    }

    build_pad(a);

    a->msg = lv_label_create(root);
    lv_obj_set_width(a->msg, AOS_SCREEN_W - 40);
    lv_obj_set_pos(a->msg, 20, 40);
    lv_label_set_long_mode(a->msg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(a->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(a->msg, lv_color_hex(0xF0D0B0), 0);
    lv_obj_set_style_text_font(a->msg, aos_font_body, 0);
    lv_obj_remove_flag(a->msg, LV_OBJ_FLAG_CLICKABLE);

    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESS_LOST, a);

    a->timer = lv_timer_create(frame, TICK_MS, a);

    char wad[128];
#ifdef AOS_SIM
    if (s_sim_ran) {
        show_msg(a, _("Doom ya corrió en este simulador: reinicialo para jugar otra vez"), true);
        return a;
    }
#endif
    if (!dp_find_wad(wad, sizeof wad)) {
        aos_hal_log("doom", "no WAD in %s", dp_data_dir());
        no_wad(a);
        return a;
    }
    aos_hal_log("doom", "WAD %s", wad);

    a->sound = aos_hal_spk_open(16000);
    dp_sound_enable(a->sound);
    if (!a->sound) aos_hal_log("doom", "no speaker: playing silent");

    show_msg(a, _("Cargando..."), false);
#ifdef AOS_SIM
    bool swap = false;
    s_sim_ran = true;
#else
    bool swap = true;                   /* the panel is big-endian */
#endif
    if (!dp_start(wad, swap)) {
        char text[300];
        snprintf(text, sizeof text, "%s\n\n%s\n\n%s", _("Doom no pudo arrancar"),
                 dp_error()[0] ? dp_error() : _("Falta memoria"), _("Tocá para salir"));
        show_msg(a, text, true);
        return a;
    }
    a->running = true;

    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("doom", "started | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    return a;
}

static void doom_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    if (a->timer) lv_timer_delete(a->timer);
    a->timer = NULL;
    dp_stop();                          /* waits for the engine, frees its frames */
    if (a->sound) aos_hal_spk_close();
    dp_sound_enable(false);
    if (a->root) lv_obj_clean(a->root);
    free(a->cv);
    lv_free(a);
}

/* The launcher icon: a cacodemon, red, one green eye, a mouthful of teeth. */
static const uint8_t DOOM_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -20, -24, 10, 18, 4,          AIC_C_LIT(0xE8DCC0), 255),
    AIC_ROT(-250),
    AIC_RECT(AIC_CENTER,  20, -24, 10, 18, 4,          AIC_C_LIT(0xE8DCC0), 255),
    AIC_ROT(250),
    AIC_RECT(AIC_CENTER,   0,   4, 62, 58, AIC_CIRCLE, AIC_C_LIT(0xC4221A), 255),
    AIC_GRAD(AIC_C_LIT(0x6A0C08), AIC_GRAD_VER),
    AIC_RECT(AIC_CENTER,   0,  -6, 24, 20, AIC_CIRCLE, AIC_C_LIT(0xF4F0D8), 255),
    AIC_INTO,
    AIC_RECT(AIC_CENTER,   0,   0, 11, 13, AIC_CIRCLE, AIC_C_LIT(0x28B040), 255),
    AIC_OUT,
    AIC_RECT(AIC_CENTER,   0,  20, 34, 11, 5,          AIC_C_LIT(0x2A0404), 255),
    AIC_INTO,
    AIC_RECT(AIC_TOP_MID,  -9,   0,  5,  5, 1,          AIC_C_LIT(0xF4F0D8), 255),
    AIC_RECT(AIC_TOP_MID,   0,   0,  5,  5, 1,          AIC_C_LIT(0xF4F0D8), 255),
    AIC_RECT(AIC_TOP_MID,   9,   0,  5,  5, 1,          AIC_C_LIT(0xF4F0D8), 255),
    AIC_OUT,
    AIC_END
};

static bool doom_init(aos_app_t *app)
{
    app->desc.id       = "demo.doom";
    app->desc.name     = "Doom";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, DOOM_ICON, sizeof DOOM_ICON);
    app->desc.color_a  = 0x5A1208;
    app->desc.color_b  = 0x140404;
    app->desc.order    = 162;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;

    app->create  = doom_create;
    app->destroy = doom_destroy;
    app->back    = doom_back;
    app->button  = doom_button;
    return true;
}

AOS_APP_ENTRY(doom_init);
