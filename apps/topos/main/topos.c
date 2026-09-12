/*
 * TOPOS - the app
 *
 * The only file that sees LVGL, the HAL and the preferences. The game itself
 * does not know any of the three exist (see topos.h).
 *
 * Living here:
 *   - pushing the dirty rectangles to the screen (flush())
 *   - the LVGL screens: the title with the three modes, pause, game over,
 *     and a banner for the countdown and the announcements
 *   - touch -> tp_game_tap(), and the pause corner
 *   - a record per mode and the sound switch, in preferences
 *
 * Born multilingual like Claude Jump: every visible word is an LVGL label
 * wrapped in _(), and the canvas only ever gets numbers and signs.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "topos.h"
#include "tp_art.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Preferences, with a prefix of our own
 * -------------------------------------------------------------------------- */
#define KEY_SFX     "tp_sfx"
#define KEY_FPS     "tp_fps"
static const char *const KEY_HI[TP_MODES] = { "tp_hi0", "tp_hi1", "tp_hi2" };

#define FRAME_MS    33          /* 30 frames per second, LVGL's own ceiling  */
#define FRAME_MAX   66          /* what it relaxes to if it cannot keep up   */

/* The pause corner, in screen pixels. The whole corner and not just the icon:
 * it is a small target at the edge of the glass. */
#define PAUSE_W     100
#define PAUSE_H     (TP_HUD_H * TP_SCALE)

static const char *const MODE_NAME[TP_MODES] = {
    N_("Clásico"), N_("Supervivencia"), N_("Frenesí"),
};
static const char *const MODE_DESC[TP_MODES] = {
    N_("60 segundos"), N_("Tres vidas"), N_("30 segundos a fondo"),
};
static const uint32_t MODE_COLOR[TP_MODES] = { 0x30D158, 0xFF453A, 0xFF9F0A };

typedef struct {
    tp_game_t   g;

    lv_obj_t   *root;
    lv_obj_t   *canvas;
    lv_obj_t   *touch;
    lv_obj_t   *pausebtn;
    lv_obj_t   *banner;
    uint16_t   *fbmem, *bgmem, *big;

    lv_obj_t   *title, *pause, *over;
    lv_obj_t   *lbl_rec[TP_MODES];
    lv_obj_t   *lbl_snd;
    lv_obj_t   *chip_sfx, *chip_fps;
    lv_obj_t   *lbl_over_t, *lbl_over_m, *lbl_over_s, *lbl_over_b, *lbl_over_r;

    uint32_t    hi[TP_MODES];
    bool        paused;
    bool        want_exit;
    bool        leaving;        /* see app_back()                          */
    bool        closing;        /* see topos_destroy()                     */
    bool        over_shown;
    uint16_t    banner_ms;
    uint32_t    last_gesture_ms;
    uint16_t    auto_wait;

    int16_t     period;
    int16_t     real_ms;
    int16_t     fps10;
    uint64_t    prev_ms;
    uint16_t    frames;
    uint8_t     tune_t;
    lv_timer_t *timer;
} app_t;

static bool s_sfx = true;

/* --------------------------------------------------------------------------
 * Sound. tp_game.c calls it without knowing there is a HAL on the other
 * side; aos_hal_beep() queues and plays from its own task.
 * -------------------------------------------------------------------------- */
void tp_sfx(int freq_hz, int ms)
{
    if (s_sfx) {
        aos_hal_beep(freq_hz, ms);
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * To the screen
 *
 * tp_present() rebuilds what changed into the small buffer and lists it; we
 * upscale exactly that and invalidate exactly that. LVGL redraws by invalid
 * areas, so the saving is double.
 * -------------------------------------------------------------------------- */

static void push_rect(app_t *a, const tp_rect_t *r)
{
    tp_expand(a->fbmem, a->big, r);

    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);

    lv_area_t area;
    area.x1 = co.x1 + r->x0 * TP_SCALE;
    area.y1 = co.y1 + r->y0 * TP_SCALE;
    area.x2 = co.x1 + r->x1 * TP_SCALE - 1;
    area.y2 = co.y1 + r->y1 * TP_SCALE - 1;
    lv_obj_invalidate_area(a->canvas, &area);
}

static void flush(app_t *a)
{
    tp_game_t *g = &a->g;
    static const tp_rect_t hud = { 0, 0, TP_W, TP_HUD_H };

    tp_present(g);
    for (int i = 0; i < g->push.n; i++) {
        push_rect(a, &g->push.r[i]);
    }
    if (g->hud_push) {
        push_rect(a, &hud);
    }
    g->last_area  = (uint16_t)(tp_dirty_area(&g->push) * 100 / (TP_W * TP_H));
    g->last_rects = g->push.n;
}

/* If a frame comes out dearer than the period, the timer is always overdue,
 * LVGL's task never sleeps and the watchdog fires. Rather than that, the game
 * relaxes (Claude Jump's, verbatim). */
static void period_tune(app_t *a)
{
    int want = a->period;

    if (a->real_ms > want + want / 3) {
        want = a->real_ms;
    } else if (a->real_ms <= want + 2 && want > FRAME_MS) {
        want -= 6;
    }
    want = clampi(want, FRAME_MS, FRAME_MAX);

    if (want != a->period) {
        aos_hal_log("topos", "real frame %d ms: period %d -> %d ms (%d.%d fps, %u%% of the screen in %u rectangles)",
                    a->real_ms, a->period, want, a->fps10 / 10, a->fps10 % 10,
                    (unsigned)a->g.last_area, (unsigned)a->g.last_rects);
        a->period = (int16_t)want;
        lv_timer_set_period(a->timer, (uint32_t)want);
    }
}

/* --------------------------------------------------------------------------
 * Interface pieces (Claude Jump's)
 * -------------------------------------------------------------------------- */

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);      /* touches do not pass through */
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_y(l, y);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             int x, int y, int w, int h, uint32_t accent,
                             const lv_font_t *font, lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C24), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* the press shows by colour, never by transform_scale: a scale is a
     * layer, and a layer that does not fit is a hang */
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(l, w - 12, h - 8);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static lv_obj_t *make_chip(lv_obj_t *parent, int x, int y, int w,
                           lv_event_cb_t cb, void *data)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, 34);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x3A3A46), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, &aos_montserrat_16, 0);
    lv_obj_set_size(l, w - 8, 22);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void chip_set(lv_obj_t *chip, const char *text, bool on)
{
    if (!chip) {
        return;
    }
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(on ? 0x0A0A12 : 0x9AA3B8), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(on ? 0x30D158 : 0x1C1C24), 0);
}

/* --------------------------------------------------------------------------
 * Preferences
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    int32_t v = 0;
    for (int m = 0; m < TP_MODES; m++) {
        if (aos_hal_pref_get_i32(KEY_HI[m], &v) && v > 0) {
            a->hi[m] = (uint32_t)v;
        }
    }
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) {
        s_sfx = v != 0;
    }
    if (aos_hal_pref_get_i32(KEY_FPS, &v)) {
        a->g.show_fps = (uint8_t)(v ? 1 : 0);
    }
}

static void prefs_save(app_t *a)
{
    for (int m = 0; m < TP_MODES; m++) {
        aos_hal_pref_set_i32(KEY_HI[m], (int32_t)a->hi[m]);
    }
    aos_hal_pref_set_i32(KEY_SFX, s_sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_FPS, a->g.show_fps);
}

/* --------------------------------------------------------------------------
 * Panels and the banner
 * -------------------------------------------------------------------------- */

static void overlay_hide_all(app_t *a)
{
    lv_obj_t *const panels[] = { a->title, a->pause, a->over };
    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++) {
        if (panels[i]) {
            lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void overlay_show(app_t *a, lv_obj_t *panel)
{
    overlay_hide_all(a);
    if (panel) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
    }
}

static void banner_hide(app_t *a)
{
    if (a->banner) {
        lv_obj_add_flag(a->banner, LV_OBJ_FLAG_HIDDEN);
    }
    a->banner_ms = 0;
}

/* The countdown and the announcements. A label over the canvas, not text on
 * it: it is words, and the words have accents. It changes a handful of times
 * per game, so the area it invalidates does not matter. */
static void banner_show(app_t *a, const char *text, const lv_font_t *font,
                        uint32_t color, uint16_t ms)
{
    lv_label_set_text(a->banner, text);
    lv_obj_set_style_text_font(a->banner, font, 0);
    lv_obj_set_style_text_color(a->banner, lv_color_hex(color), 0);
    lv_obj_remove_flag(a->banner, LV_OBJ_FLAG_HIDDEN);
    a->banner_ms = ms;
}

/* --------------------------------------------------------------------------
 * Title
 * -------------------------------------------------------------------------- */

static void title_refresh(app_t *a)
{
    char buf[16];
    for (int m = 0; m < TP_MODES; m++) {
        if (a->hi[m]) {
            snprintf(buf, sizeof(buf), "%u", (unsigned)a->hi[m]);
        } else {
            buf[0] = '\0';
        }
        lv_label_set_text(a->lbl_rec[m], buf);
    }
    lv_label_set_text(a->lbl_snd, s_sfx ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE);
}

static void chips_refresh(app_t *a)
{
    chip_set(a->chip_sfx, _("Sonido"), s_sfx);
    chip_set(a->chip_fps, "FPS", a->g.show_fps);
}

static void go_title(app_t *a)
{
    tp_game_t *g = &a->g;
    g->state   = GS_TITLE;
    g->title_t = 0;
    a->paused     = false;
    a->over_shown = false;
    banner_hide(a);
    lv_obj_add_flag(a->pausebtn, LV_OBJ_FLAG_HIDDEN);
    tp_bg_build(g, true);
    flush(a);
    overlay_show(a, a->title);
    title_refresh(a);
}

/* --------------------------------------------------------------------------
 * A game
 * -------------------------------------------------------------------------- */

static void show_end(app_t *a);

static void handle_events(app_t *a)
{
    tp_game_t *g = &a->g;
    uint16_t ev = g->events;
    char buf[48];
    g->events = 0;

    if (ev & EV_COUNT) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)g->count_num);
        banner_show(a, buf, &aos_montserrat_48, 0xFFFFFF, 700);
    }
    if (ev & EV_GO) {
        banner_show(a, _("¡Ya!"), &aos_montserrat_48, 0x30D158, 600);
    }
    if (ev & EV_LEVEL) {
        snprintf(buf, sizeof(buf), "%s %u", _("Nivel"), (unsigned)g->level);
        banner_show(a, buf, &aos_montserrat_36, 0xFFD60A, 1100);
        tp_sfx(784, 60);
        tp_sfx(988, 60);
        tp_sfx(1175, 90);
    }
    if (ev & EV_HURRY) {
        banner_show(a, _("¡Últimos 10 segundos!"), &aos_montserrat_28, 0xFF9F0A, 1300);
    }
    if (ev & EV_TIMEUP) {
        banner_show(a, _("¡Tiempo!"), &aos_montserrat_48, 0xFFFFFF, 1500);
        tp_sfx(880, 120);
        tp_sfx(660, 120);
        tp_sfx(440, 220);
    }
    if (ev & EV_DEAD) {
        banner_show(a, _("¡Sin vidas!"), &aos_montserrat_36, 0xFF453A, 1500);
        tp_sfx(392, 150);
        tp_sfx(330, 150);
        tp_sfx(262, 260);
    }
    if (ev & EV_OVER) {
        show_end(a);
    }
}

static void game_start(app_t *a, int mode)
{
    tp_game_t *g = &a->g;
    tp_game_start(g, mode);
    tp_bg_build(g, false);
    a->paused     = false;
    a->over_shown = false;
    a->prev_ms    = 0;
    overlay_hide_all(a);
    lv_obj_remove_flag(a->pausebtn, LV_OBJ_FLAG_HIDDEN);
    handle_events(a);           /* the "3" */
    flush(a);
}

static void mode0_cb(lv_event_t *e) { game_start((app_t *)lv_event_get_user_data(e), 0); }
static void mode1_cb(lv_event_t *e) { game_start((app_t *)lv_event_get_user_data(e), 1); }
static void mode2_cb(lv_event_t *e) { game_start((app_t *)lv_event_get_user_data(e), 2); }

static void pause_show(app_t *a)
{
    tp_game_t *g = &a->g;
    if (a->paused || (g->state != GS_COUNT && g->state != GS_PLAY &&
                      g->state != GS_ENDING)) {
        return;
    }
    a->paused = true;
    banner_hide(a);
    chips_refresh(a);
    overlay_show(a, a->pause);
}

static void pause_cb(lv_event_t *e)
{
    pause_show((app_t *)lv_event_get_user_data(e));
}

static void resume_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    overlay_hide_all(a);
    a->paused  = false;
    a->prev_ms = 0;             /* the pause is not a 20-second frame */
}

static void menu_cb(lv_event_t *e)
{
    go_title((app_t *)lv_event_get_user_data(e));
}

static void again_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    game_start(a, a->g.mode);
}

static void exit_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->leaving   = true;        /* so app_back() lets the system close us */
    a->want_exit = true;        /* deferred: aos_ui_back() destroys the app */
}

static void snd_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    s_sfx = !s_sfx;
    prefs_save(a);
    title_refresh(a);
    chips_refresh(a);
    tp_sfx(1200, 30);
}

static void fps_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->g.show_fps  = (uint8_t)!a->g.show_fps;
    a->g.hud_valid = 0;
    prefs_save(a);
    chips_refresh(a);
}

static void show_end(app_t *a)
{
    tp_game_t *g = &a->g;
    char buf[64];
    bool record = g->score > a->hi[g->mode];

    a->over_shown = true;
    if (record) {
        a->hi[g->mode] = g->score;
        prefs_save(a);
    }

    lv_label_set_text(a->lbl_over_t, record ? _("¡Nuevo récord!") : _("¡Se acabó!"));
    lv_obj_set_style_text_color(a->lbl_over_t,
                                lv_color_hex(record ? 0xFFD60A : 0xFFFFFF), 0);
    lv_label_set_text(a->lbl_over_m, _(MODE_NAME[g->mode]));

    snprintf(buf, sizeof(buf), "%u", (unsigned)g->score);
    lv_label_set_text(a->lbl_over_s, buf);

    snprintf(buf, sizeof(buf), "%s %u   %s %u",
             _("Golpes"), (unsigned)g->hits,
             _("Mejor racha"), (unsigned)g->best_streak);
    lv_label_set_text(a->lbl_over_b, buf);

    snprintf(buf, sizeof(buf), "%s %u", _("Récord"), (unsigned)a->hi[g->mode]);
    lv_label_set_text(a->lbl_over_r, buf);

    banner_hide(a);
    lv_obj_add_flag(a->pausebtn, LV_OBJ_FLAG_HIDDEN);
    overlay_show(a, a->over);

    if (record) {
        tp_sfx(1047, 90);
        tp_sfx(1319, 90);
        tp_sfx(1568, 90);
        tp_sfx(2093, 180);
    }
}

/* --------------------------------------------------------------------------
 * Touch and gestures
 * -------------------------------------------------------------------------- */

/* On PRESSED, not on CLICKED: a whack-a-mole is won by the moment the finger
 * lands, and a click waits for it to lift. */
static void touch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing || a->paused) {
        return;
    }
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);

    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    tp_game_tap(&a->g, (pt.x - co.x1) / TP_SCALE, (pt.y - co.y1) / TP_SCALE);
}

/* While playing a swipe is just a sloppy tap, so it does nothing. On the
 * title a swipe right leaves, as everywhere; on the result it goes back to
 * the title. Both paths (LVGL's and the touch chip's) can arrive for the same
 * swipe, hence the 400 ms. */
static bool handle_gesture(app_t *a, int dir)
{
    tp_game_t *g = &a->g;
    if (a->paused || (g->state != GS_TITLE && g->state != GS_OVER)) {
        return false;
    }
    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - a->last_gesture_ms) < 400) {
        return false;
    }
    a->last_gesture_ms = now;
    if (dir != LV_DIR_RIGHT) {
        return false;
    }
    if (g->state == GS_TITLE) {
        a->want_exit = true;
    } else {
        go_title(a);
    }
    return true;
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if ((dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) && handle_gesture(a, (int)dir)) {
        lv_indev_wait_release(indev);
    }
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);
    tp_game_t *g = &a->g;

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this 'a' no longer exists */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    switch ((aos_touch_gesture_t)aos_ui_take_gesture()) {
    case AOS_TOUCH_GESTURE_LEFT:  handle_gesture(a, LV_DIR_LEFT);  break;
    case AOS_TOUCH_GESTURE_RIGHT: handle_gesture(a, LV_DIR_RIGHT); break;
    default: break;
    }

    /* The real time between frames drives the game: the 60 seconds are 60
     * seconds even if the board draws slower than the period. */
    int dt = FRAME_MS;
    {
        uint64_t now = aos_hal_uptime_ms();
        if (a->frames < 0xFFFF) {
            a->frames++;
        }
        if (a->prev_ms && now > a->prev_ms) {
            /* narrowed to 32 bits before dividing: a 64-bit division drags
             * in __udivdi3 */
            uint32_t d = (uint32_t)(now - a->prev_ms);
            dt = d > 100 ? 100 : (int)d;
            if (a->frames > 8 && d > 0) {
                int inst = (int)(10000u / d);
                a->fps10   = (int16_t)(a->fps10 ? (a->fps10 * 7 + inst) / 8 : inst);
                a->real_ms = (int16_t)(a->real_ms ? (a->real_ms * 7 + (int)d) / 8 : (int)d);
            }
        }
        a->prev_ms = now;
    }
    /* Claude Jump's lesson: the first frames build the screen and would
     * inflate the average; the adjustment starts after 60. */
    if (a->frames > 60 && ++a->tune_t >= 20) {
        a->tune_t = 0;
        period_tune(a);
    }
    /* the fps the strip shows, by the clock and not per frame */
    if ((a->frames & 7) == 0) {
        g->fps10 = a->fps10;
    }

    if (a->banner_ms) {
        if (a->banner_ms <= dt) {
            banner_hide(a);
        } else {
            a->banner_ms = (uint16_t)(a->banner_ms - dt);
        }
    }

    if (g->state == GS_TITLE) {
        g->title_t += (uint32_t)dt;
        flush(a);
        return;
    }
    if (a->paused) {
        return;
    }
    if (g->state == GS_OVER) {
        /* TP_AUTO starts over by itself, to leave it running for an hour */
        if (g->autoplay && a->over_shown && ++a->auto_wait > 60) {
            a->auto_wait = 0;
            game_start(a, g->mode);
        }
        return;
    }

    if (g->autoplay) {
        tp_game_bot(g, dt);
    }
    tp_game_step(g, dt);
    handle_events(a);
    flush(a);
}

/* --------------------------------------------------------------------------
 * Back, hide
 * -------------------------------------------------------------------------- */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    /* 'leaving': the Exit button goes through aos_ui_back(), which asks us
     * first. Without this we would pause, and the app could never close
     * (chatarra's trap). */
    if (!a || a->leaving) {
        return false;
    }
    switch (a->g.state) {
    case GS_COUNT:
    case GS_PLAY:
    case GS_ENDING:
        if (a->paused) {
            go_title(a);
        } else {
            pause_show(a);
        }
        return true;
    case GS_OVER:
        go_title(a);
        return true;
    default:
        return false;           /* from the title, the system leaves */
    }
}

/* Leaving mid-game should not lose the game: it pauses. */
static void topos_hide(aos_app_t *self, void *inst)
{
    (void)self;
    if (inst) {
        pause_show((app_t *)inst);
    }
}

/* --------------------------------------------------------------------------
 * Building the screens
 * -------------------------------------------------------------------------- */

static void make_mode_button(app_t *a, lv_obj_t *parent, int m, int y,
                             lv_event_cb_t cb)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 300, 44);
    lv_obj_set_pos(b, 34, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x15151C), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(MODE_COLOR[m]), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(MODE_COLOR[m]), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, a);

    /* Name and description on the left, record on the right. Fixed boxes
     * with an ellipsis: these are absolute positions, and German grows. */
    lv_obj_t *n = lv_label_create(b);
    lv_label_set_text(n, _(MODE_NAME[m]));
    lv_obj_set_style_text_font(n, &aos_montserrat_20, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(n, 14, 2);
    lv_obj_set_size(n, 190, 24);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(n, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *d = lv_label_create(b);
    lv_label_set_text(d, _(MODE_DESC[m]));
    lv_obj_set_style_text_font(d, &aos_montserrat_14, 0);
    lv_obj_set_style_text_color(d, lv_color_hex(0xA0A8B8), 0);
    lv_obj_set_pos(d, 14, 24);
    lv_obj_set_size(d, 190, 17);
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *r = lv_label_create(b);
    lv_label_set_text(r, "");
    lv_obj_set_style_text_font(r, &aos_montserrat_20, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(0xFFD60A), 0);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r, 200, 9);
    lv_obj_set_size(r, 86, 24);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    a->lbl_rec[m] = r;
}

static void build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->title = p;

    /* The name, with a dark shadow under it: white alone does not read on
     * sunlit grass. The big mole and the lawn behind are the canvas. */
    lv_obj_t *sh = make_label(p, _("Topos"), &aos_montserrat_36, 0x1E4A12, 13);
    lv_obj_set_width(sh, 300);
    lv_obj_set_x(sh, 36);
    lv_label_set_long_mode(sh, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *t = make_label(p, _("Topos"), &aos_montserrat_36, 0xFFFFFF, 10);
    lv_obj_set_width(t, 300);
    lv_obj_set_x(t, 34);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *snd = make_button(p, LV_SYMBOL_VOLUME_MAX, 306, 58, 48, 40,
                                0x8E8E93, &aos_montserrat_20, snd_cb, a);
    a->lbl_snd = lv_obj_get_child(snd, 0);

    make_mode_button(a, p, MODE_CLASSIC,  232, mode0_cb);
    make_mode_button(a, p, MODE_SURVIVAL, 282, mode1_cb);
    make_mode_button(a, p, MODE_FRENZY,   332, mode2_cb);

    /* The strip under the last button is read, not touched: how to play. */
    lv_obj_t *h = make_label(p, _("Tocá los topos; los de casco, dos veces. ¡Las bombas no!"),
                             &aos_montserrat_14, 0xFFFFFF, 386);
    lv_obj_set_width(h, 300);
    lv_obj_set_x(h, 34);
    lv_obj_set_style_bg_color(h, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_40, 0);
    lv_obj_set_style_radius(h, 10, 0);
    lv_obj_set_style_pad_hor(h, 8, 0);
    lv_obj_set_style_pad_ver(h, 4, 0);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->pause = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);

    make_label(p, _("Pausa"), &aos_montserrat_36, 0xFFFFFF, 100);
    make_button(p, _("Seguir"), 64, 162, 240, 50, 0x30D158,
                &aos_montserrat_28, resume_cb, a);
    make_button(p, _("Menú"), 64, 220, 240, 40, 0x0A84FF,
                &aos_montserrat_20, menu_cb, a);
    make_button(p, _("Salir"), 64, 268, 240, 40, 0xFF453A,
                &aos_montserrat_20, exit_cb, a);
    a->chip_sfx = make_chip(p, 64, 322, 116, snd_cb, a);
    a->chip_fps = make_chip(p, 188, 322, 116, fps_cb, a);
}

static void build_over(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->over = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);

    a->lbl_over_t = make_label(p, "", &aos_montserrat_28, 0xFFFFFF, 60);
    a->lbl_over_m = make_label(p, "", &aos_montserrat_16, 0x9AA3B8, 96);
    a->lbl_over_s = make_label(p, "", &aos_montserrat_48, 0xFFFFFF, 118);
    a->lbl_over_b = make_label(p, "", &aos_montserrat_16, 0xDDE3EE, 182);
    a->lbl_over_r = make_label(p, "", &aos_montserrat_16, 0xFFD60A, 206);

    make_button(p, _("Otra vez"), 64, 244, 240, 50, 0x30D158,
                &aos_montserrat_28, again_cb, a);
    make_button(p, _("Menú"), 64, 302, 240, 40, 0x0A84FF,
                &aos_montserrat_20, menu_cb, a);
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void free_buffers(app_t *a)
{
    free(a->fbmem);
    free(a->bgmem);
    free(a->big);
    a->fbmem = a->bgmem = a->big = NULL;
    tp_art_free();
}

static void *topos_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }

    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("topos", "opening | internal %u B, psram %u B",
                (unsigned)heap_int, (unsigned)heap_psram);

    /* Three buffers through malloc(), which sends them to PSRAM: 82 + 82 KB
     * for the game and 330 KB for the upscaled one. Plus ~60 KB of sprites. */
    size_t small = (size_t)TP_W * TP_H * sizeof(uint16_t);
    a->fbmem = (uint16_t *)malloc(small);
    a->bgmem = (uint16_t *)malloc(small);
    a->big   = (uint16_t *)malloc(small * TP_SCALE * TP_SCALE);
    uint64_t t0 = aos_hal_uptime_ms();
    if (!a->fbmem || !a->bgmem || !a->big || !tp_art_init()) {
        aos_hal_log("topos", "out of memory for the buffers or the sprites");
        free_buffers(a);
        lv_free(a);
        return NULL;
    }
    aos_hal_log("topos", "sprites rendered in %u ms",
                (unsigned)(uint32_t)(aos_hal_uptime_ms() - t0));
    memset(a->big, 0, small * TP_SCALE * TP_SCALE);

    tp_buf_init(&a->g.fb, a->fbmem, TP_W, TP_H);
    tp_buf_init(&a->g.bg, a->bgmem, TP_W, TP_H);
    tp_game_init(&a->g, (uint32_t)aos_hal_uptime_ms());
    prefs_load(a);

    a->root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    /* the canvas gets the ALREADY upscaled buffer and is drawn 1:1 */
    lv_canvas_set_buffer(a->canvas, a->big, TP_W * TP_SCALE, TP_H * TP_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, TP_W * TP_SCALE, TP_H * TP_SCALE);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* The touch layer: the whole field under the score's strip. In LVGL 9
     * every object is born clickable, so without it the canvas would eat the
     * finger. Only PRESSED is registered: see touch_cb(). */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H - PAUSE_H);
    lv_obj_set_pos(a->touch, 0, PAUSE_H);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);

    a->pausebtn = lv_obj_create(root);
    lv_obj_remove_style_all(a->pausebtn);
    lv_obj_set_size(a->pausebtn, PAUSE_W, PAUSE_H);
    lv_obj_set_pos(a->pausebtn, 0, 0);
    lv_obj_add_flag(a->pausebtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->pausebtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->pausebtn, pause_cb, LV_EVENT_CLICKED, a);
    lv_obj_add_flag(a->pausebtn, LV_OBJ_FLAG_HIDDEN);

    a->banner = lv_label_create(root);
    lv_label_set_text(a->banner, "");
    lv_obj_set_style_bg_color(a->banner, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->banner, LV_OPA_60, 0);
    lv_obj_set_style_radius(a->banner, 18, 0);
    lv_obj_set_style_pad_hor(a->banner, 18, 0);
    lv_obj_set_style_pad_ver(a->banner, 6, 0);
    lv_obj_set_style_text_align(a->banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(a->banner, 330, 0);
    lv_label_set_long_mode(a->banner, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(a->banner, LV_ALIGN_TOP_MID, 0, 176);
    lv_obj_remove_flag(a->banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->banner, LV_OBJ_FLAG_HIDDEN);

    build_title(a, root);
    build_pause(a, root);
    build_over(a, root);

    /* The gesture is listened for on the ROOT with GESTURE_BUBBLE taken off:
     * LVGL hands it to the first ancestor without the flag. */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    a->period = FRAME_MS;
    a->timer  = lv_timer_create(frame, FRAME_MS, a);

    go_title(a);

#ifdef AOS_SIM_BUILTIN
    /* Development switches. On the board getenv() always returns NULL.
     *
     *   TP_AUTO=1         the bot plays, and starts over after each game
     *   TP_MODE=0|1|2     straight into that mode
     *   TP_FPS=1          frames per second in the score's strip
     *   TP_REC=500        fake records, to see them on the title
     *   TP_SCREEN=pause|over   straight into that panel: the layout audit
     *                     skips hidden objects, and both are born hidden
     */
    {
        const char *env;
        int mode = -1;
        if ((env = getenv("TP_FPS")) && env[0]) {
            a->g.show_fps = 1;
        }
        if ((env = getenv("TP_REC")) && env[0]) {
            for (int m = 0; m < TP_MODES; m++) {
                a->hi[m] = (uint32_t)atoi(env) * (uint32_t)(m + 1);
            }
            title_refresh(a);
        }
        if ((env = getenv("TP_MODE")) && env[0]) {
            mode = clampi(atoi(env), 0, TP_MODES - 1);
        }
        if ((env = getenv("TP_AUTO")) && env[0]) {
            a->g.autoplay = 1;
            if (mode < 0) {
                mode = MODE_CLASSIC;
            }
        }
        if ((env = getenv("TP_SCREEN")) && env[0]) {
            if (mode < 0) {
                mode = MODE_CLASSIC;
            }
        }
        if (mode >= 0) {
            game_start(a, mode);
        }
        if ((env = getenv("TP_SCREEN")) && env[0]) {
            if (env[0] == 'p') {
                pause_show(a);
            } else if (env[0] == 'o') {
                a->g.score       = 1234;
                a->g.hits        = 57;
                a->g.best_streak = 14;
                a->g.state       = GS_OVER;
                show_end(a);
            }
        }
    }
#endif

    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("topos", "ready | internal %u B, psram %u B | field %dx%d x%d",
                (unsigned)heap_int, (unsigned)heap_psram, TP_W, TP_H, TP_SCALE);
    return a;
}

static void topos_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }

    /* The objects are deleted HERE and not left to the runtime: it calls
     * destroy() and only then deletes the root, so LV_EVENT_PRESS_LOST from
     * a finger still down would reach touch_cb with the context freed. */
    a->closing = true;
    if (a->root) {
        lv_obj_clean(a->root);
    }

    prefs_save(a);
    aos_hal_log("topos", "closing | records %u / %u / %u",
                (unsigned)a->hi[0], (unsigned)a->hi[1], (unsigned)a->hi[2]);

    free_buffers(a);
    lv_free(a);
}

static bool topos_init(aos_app_t *app)
{
    app->desc.id       = "demo.topos";
    app->desc.name     = "Topos";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_MOLE;
    /* Lawn over dirt. Darkish at both ends: the icon's shape is drawn in
     * white and the colour is the app's. */
    app->desc.color_a  = 0x3E8E2E;
    app->desc.color_b  = 0x6B4020;
    app->desc.order    = 149;           /* among the games */
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE;

    app->create  = topos_create;
    app->destroy = topos_destroy;
    app->hide    = topos_hide;
    app->back    = app_back;
    return true;
}

AOS_APP_ENTRY(topos_init);
