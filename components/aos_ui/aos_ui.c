/*
 * AmoledOS - UI runtime.
 *
 * Structure of the screen:
 *
 *   screen
 *   +-- stage            (368x448, black)
 *   |   +-- watchface    layer 0, always alive
 *   |   +-- launcher     layer 1, created the first time it is used
 *   |   +-- app root     layer 2, one per open or backgrounded app
 *   +-- statusbar        overlaid on top
 *   +-- toast            ephemeral
 *
 * Navigation is by gesture: swipe up from the clock opens the menu, swipe
 * right goes back.
 */
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_internal.h"
#include "aos_watchface.h"
#include "aos_i18n.h"
#include "aos_notif_ui.h"
#include "aos_pair_ui.h"

#include <string.h>
#include <stdio.h>

#define STATUSBAR_H     30
#define ANIM_MS         220

AOS_BSS_PSRAM static aos_app_t  s_apps[AOS_MAX_APPS];
static int        s_app_count;

static lv_obj_t  *s_stage;
static lv_obj_t  *s_statusbar;
static lv_obj_t  *s_sb_time;
static lv_obj_t  *s_sb_batt;
static lv_obj_t  *s_sb_radios;      /* the centred row with wifi and bluetooth */
static lv_obj_t  *s_sb_wifi;
static lv_obj_t  *s_sb_bt;
static lv_obj_t  *s_launcher;
static lv_obj_t  *s_toast;

static aos_app_t *s_current;            /* foreground app, NULL = launcher/clock */
static bool       s_launcher_visible;
static bool       s_launcher_stale;   /* the contents changed with the menu open */

/* Defined further down, next to the rest of the menu handling; used from the
 * app registry, which comes before. */
static void launcher_invalidate(void);
static aos_launcher_style_t s_style = AOS_LAUNCHER_LIST;

/* The HAL reports display state changes from its own task, so we cannot touch
 * LVGL there: we note the news down and apply it on the tick, which already
 * runs with the lock held. */
static volatile aos_display_state_t s_pending_display_state;
static volatile bool s_display_state_dirty;
static bool s_picker_requested;
static char s_lang_requested[AOS_LANG_CODE_MAX];


static lv_obj_t *s_watchface;

/* -------------------------------------------------------------------------- */
/* Animations                                                                  */
/* -------------------------------------------------------------------------- */

static void anim_x_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void anim_y_cb(void *obj, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)obj, value);
}

static void slide(lv_obj_t *obj, bool vertical, int32_t from, int32_t to,
                  lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, vertical ? anim_y_cb : anim_x_cb);
    if (done) {
        lv_anim_set_completed_cb(&a, done);
    }
    lv_anim_start(&a);
}

/* -------------------------------------------------------------------------- */
/* Status bar                                                                  */
/* -------------------------------------------------------------------------- */

static void radios_refresh(void);

static void statusbar_build(lv_obj_t *parent)
{
    s_statusbar = lv_obj_create(parent);
    lv_obj_remove_style_all(s_statusbar);
    /* Covers from y=0 and with an opaque background. It used to be transparent
     * and start at y=4: since an app's root starts at STATUSBAR_H, that top
     * strip was covered by nobody and whatever was behind showed through -a
     * menu row half on its way out, or the face's date-. It is black on black,
     * so it changes nothing about how things look, it just stops letting
     * things through. */
    lv_obj_set_size(s_statusbar, lv_pct(100), STATUSBAR_H + 4);
    lv_obj_align(s_statusbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_statusbar, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_statusbar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_statusbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_statusbar, LV_OBJ_FLAG_CLICKABLE);

    s_sb_time = aos_label(s_statusbar, "--:--", aos_font_small, AOS_C_TEXT);
    lv_obj_align(s_sb_time, LV_ALIGN_LEFT_MID, 26, 0);

    s_sb_batt = aos_label(s_statusbar, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_sb_batt, LV_ALIGN_RIGHT_MID, -26, 0);

    /* The two radios go together in a centred row, and they are hidden with
     * LV_OBJ_FLAG_HIDDEN rather than by emptying their text.
     *
     * That is the whole trick: LVGL's flex layout SKIPS hidden children
     * (lv_flex.c), so with a single radio on its icon ends up centred on its
     * own, with both they end up together and centred as a block, and with
     * neither there is no gap. A label with its text set to "" would still
     * take up its place in the row and would knock the other one off centre.
     *
     * The row is content-sized and centred with lv_obj_align, which sets the
     * alignment style rather than the position: when the row changes width
     * because an icon appeared or disappeared, it re-centres itself. (Not to
     * be confused with lv_obj_align_to, which does compute once and does not
     * recompute; see the comment in aos_label_boxed.) */
    s_sb_radios = lv_obj_create(s_statusbar);
    lv_obj_remove_style_all(s_sb_radios);
    lv_obj_set_size(s_sb_radios, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_sb_radios, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(s_sb_radios, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_sb_radios, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_sb_radios, 10, 0);
    lv_obj_remove_flag(s_sb_radios, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_sb_radios, LV_OBJ_FLAG_CLICKABLE);

    s_sb_wifi = aos_label(s_sb_radios, LV_SYMBOL_WIFI, aos_font_small, AOS_C_TEXT);
    s_sb_bt   = aos_label(s_sb_radios, LV_SYMBOL_BLUETOOTH, aos_font_small, AOS_C_TEXT);
    lv_obj_add_flag(s_sb_wifi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sb_bt,   LV_OBJ_FLAG_HIDDEN);
}

void aos_ui_statusbar_refresh(void)
{
    if (!s_statusbar) {
        return;
    }

    struct tm now;
    aos_hal_time_now(&now);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", now.tm_hour, now.tm_min);
    lv_label_set_text(s_sb_time, buf);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        char text[24];
        snprintf(text, sizeof(text), "%s%d%%",
                 batt.charging ? LV_SYMBOL_CHARGE " " : "", batt.percent);
        lv_label_set_text(s_sb_batt, text);
        lv_obj_set_style_text_color(s_sb_batt,
                                    batt.percent <= 15 ? AOS_C_RED : AOS_C_DIM, 0);
    } else {
        lv_label_set_text(s_sb_batt, "");
    }

    radios_refresh();
}

/* State of the two radios. Only text, colour, opacity and visibility: nothing
 * that builds a layer for LVGL. */
static void icono(lv_obj_t *lbl, const char *glifo, lv_color_t color, lv_opa_t opa)
{
    if (!glifo) {
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl, glifo);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_opa(lbl, opa, 0);
}

static void radios_refresh(void)
{
    /* WiFi */
    if (!aos_hal_net_enabled() && !aos_hal_net_ap_active()) {
        icono(s_sb_wifi, NULL, AOS_C_TEXT, LV_OPA_COVER);
    } else if (aos_hal_net_ap_active()) {
        icono(s_sb_wifi, LV_SYMBOL_WIFI, AOS_C_ACCENT, LV_OPA_COVER);
    } else {
        switch (aos_hal_net_state()) {
        case AOS_NET_CONNECTED:
            icono(s_sb_wifi, LV_SYMBOL_WIFI, AOS_C_TEXT, LV_OPA_COVER);
            break;
        case AOS_NET_CONNECTING:
            icono(s_sb_wifi, LV_SYMBOL_WIFI, AOS_C_DIM, LV_OPA_50);
            break;
        default:
            icono(s_sb_wifi, LV_SYMBOL_WARNING, AOS_C_DIM, LV_OPA_60);
            break;
        }
    }

    /* Bluetooth, with the same colour code as WiFi: on and working in white,
     * searching in grey at half opacity, and in the accent colour when there
     * is something to look at on the screen. */
    switch (aos_hal_bt_state()) {
    case AOS_BT_CONNECTED:
        icono(s_sb_bt, LV_SYMBOL_BLUETOOTH, AOS_C_TEXT, LV_OPA_COVER);
        break;
    case AOS_BT_PAIRING:
        icono(s_sb_bt, LV_SYMBOL_BLUETOOTH, AOS_C_ACCENT, LV_OPA_COVER);
        break;
    case AOS_BT_ADVERTISING:
        icono(s_sb_bt, LV_SYMBOL_BLUETOOTH, AOS_C_DIM, LV_OPA_50);
        break;
    default:
        icono(s_sb_bt, NULL, AOS_C_TEXT, LV_OPA_COVER);
        break;
    }
}

void aos_ui_statusbar_set_visible(bool visible)
{
    if (!s_statusbar) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------------------------------- */
/* Toast                                                                       */
/* -------------------------------------------------------------------------- */

static void toast_done(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
}

void aos_ui_toast(const char *text, uint32_t ms)
{
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }

    s_toast = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_color(s_toast, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 22, 0);
    lv_obj_set_style_pad_all(s_toast, 18, 0);
    lv_obj_set_width(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_toast, AOS_SCREEN_W - 80, 0);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, 120);

    lv_obj_t *label = aos_label(s_toast, text, aos_font_body, AOS_C_TEXT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    lv_timer_t *timer = lv_timer_create(toast_done, ms ? ms : 1600, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

/* -------------------------------------------------------------------------- */
/* App registry                                                                */
/* -------------------------------------------------------------------------- */

static int app_index(const char *id)
{
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i].desc.id && strcmp(s_apps[i].desc.id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static void sort_apps(void)
{
    for (int i = 1; i < s_app_count; i++) {
        aos_app_t key = s_apps[i];
        int j = i - 1;
        while (j >= 0 && s_apps[j].desc.order > key.desc.order) {
            s_apps[j + 1] = s_apps[j];
            j--;
        }
        s_apps[j + 1] = key;
    }
}

bool aos_ui_register_app(const aos_app_t *app)
{
    if (!app || !app->desc.id || s_app_count >= AOS_MAX_APPS) {
        return false;
    }
    if (app_index(app->desc.id) >= 0) {
        aos_hal_log("ui", "duplicate app: %s", app->desc.id);
        return false;
    }

    s_apps[s_app_count] = *app;
    s_apps[s_app_count].inst = NULL;
    s_apps[s_app_count].root = NULL;
    s_apps[s_app_count].running = false;
    s_app_count++;
    sort_apps();

    /* Neither destroy it nor touch s_launcher_visible here: see launcher_invalidate. */
    launcher_invalidate();
    return true;
}

bool aos_ui_unregister_app(const char *id)
{
    int index = id ? app_index(id) : -1;
    if (index < 0) {
        return false;
    }
    aos_app_t *app = &s_apps[index];
    if (s_current == app) {
        aos_ui_home();
    }
    if (app->inst && app->destroy) {
        app->destroy(app, app->inst);
    }
    if (app->root) {
        lv_obj_delete(app->root);
    }
    memmove(&s_apps[index], &s_apps[index + 1],
            (size_t)(s_app_count - index - 1) * sizeof(aos_app_t));
    s_app_count--;

    /* Neither destroy it nor touch s_launcher_visible here: see launcher_invalidate. */
    launcher_invalidate();
    return true;
}

int aos_ui_app_count(void)
{
    return s_app_count;
}

const aos_app_t *aos_ui_app_at(int index)
{
    return (index >= 0 && index < s_app_count) ? &s_apps[index] : NULL;
}

aos_app_t *aos_ui_app_find(const char *id)
{
    int index = id ? app_index(id) : -1;
    return index >= 0 ? &s_apps[index] : NULL;
}

/* -------------------------------------------------------------------------- */
/* Navigation                                                                  */
/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Watchface visibility
 *
 * The face is COVERED by the launcher and by the apps, but it stayed in the
 * draw tree, and that is paid for in two ways:
 *
 *   - The status bar strip (0..STATUSBAR_H) is not covered by the app's root,
 *     which starts just below it. Whatever the face draws up there shows on
 *     top of the app. With the word face, whose date is at y=24, the first few
 *     pixels of the letters poked out.
 *   - Worse: during the 220 ms of the slide, the part of the screen the app
 *     does not yet cover exposes the whole face, and LVGL has to redraw it on
 *     every frame. With 110 labels that does not fit in the budget and the
 *     slide dragged on for more than a second with the menu and the clock
 *     painted underneath. That was the reported symptom.
 *
 * It is shown at the start of any transition that may uncover it, and hidden
 * when the one that covers it FINISHES, so as not to change anything about how
 * the animations look. ---------------------------------------------------- */

static void watchface_show(void)
{
    if (s_watchface) {
        lv_obj_remove_flag(s_watchface, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Slide-finished callback: the state is asked for again rather than trusting
 * the one from 220 ms ago, because in between we may have left. */
static void watchface_hide_if_covered(lv_anim_t *anim)
{
    (void)anim;
    if (s_watchface && (s_current || s_launcher_visible)) {
        lv_obj_add_flag(s_watchface, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --------------------------------------------------------------------------
 * The menu's contents changed (an app was registered or removed)
 *
 * What CANNOT be done here is destroying it outright while it is on screen.
 * That left s_launcher NULL with s_launcher_visible true, and that
 * inconsistent pair hangs the interface forever:
 *
 *   - launcher_hide() starts with "if (!s_launcher || !s_launcher_visible)
 *     return", so with the pointer NULL it never closes;
 *   - aos_ui_show_launcher() calls launcher_ensure() -which recreates it
 *     hidden at y=448- and then leaves through "if (s_launcher_visible)
 *     return", so it does not open either;
 *   - and the face hides itself, because the tick covers it when it believes
 *     the menu is up. What is left is a black screen responding to nothing.
 *
 * It was only seen at power-on: aos_dynapp_scan() runs AFTER aos_ui_init(),
 * that is, with the screen and the touch panel already alive, and it registers
 * one .so every ~100 ms for a few seconds. Opening the menu in that window
 * destroyed it halfway through its entry animation. Waiting for it to finish
 * loading it never happened, and that is why it looked like "it has not
 * finished booting yet".
 *
 * The way out is not to touch it while it is on screen: it is marked and
 * rebuilt when it closes. Rebuilding it there and then -which is what
 * aos_ui_launcher_set_style() does, and there it is fine because it happens
 * once- would be twenty rebuilds with their animation in the two seconds of
 * startup.
 * -------------------------------------------------------------------------- */
static void launcher_invalidate(void)
{
    if (!s_launcher) {
        return;
    }
    if (s_launcher_visible) {
        s_launcher_stale = true;
        return;
    }
    lv_obj_delete(s_launcher);
    s_launcher = NULL;
    s_launcher_stale = false;
}

static void launcher_ensure(void)
{
    /* If it went stale and is no longer on screen, it is rebuilt now. This
     * check is here as well as in the animation-finished callback because a
     * completed_cb does NOT run if the animation is cancelled, and relying on
     * it alone would leave the old menu in place forever. */
    if (s_launcher && s_launcher_stale && !s_launcher_visible) {
        lv_obj_delete(s_launcher);
        s_launcher = NULL;
    }
    if (s_launcher) {
        return;
    }
    s_launcher_stale = false;
    s_launcher = aos_launcher_create(s_stage, s_style);
    lv_obj_add_flag(s_launcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(s_launcher, AOS_SCREEN_H);
}

static void launcher_hidden_cb(lv_anim_t *anim)
{
    (void)anim;
    if (!s_launcher_visible && s_launcher) {
        lv_obj_add_flag(s_launcher, LV_OBJ_FLAG_HIDDEN);
        if (s_launcher_stale) {
            lv_obj_delete(s_launcher);
            s_launcher = NULL;
            s_launcher_stale = false;
        }
    }
}

void aos_ui_show_launcher(void)
{
    launcher_ensure();
    if (s_launcher_visible) {
        return;
    }
    s_launcher_visible = true;
    watchface_show();               /* the launcher rises and uncovers it */
    lv_obj_remove_flag(s_launcher, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_launcher);
    slide(s_launcher, true, AOS_SCREEN_H, 0, watchface_hide_if_covered);
    aos_ui_statusbar_set_visible(true);
    lv_obj_move_foreground(s_statusbar);
}

static void launcher_hide(void)
{
    if (!s_launcher || !s_launcher_visible) {
        return;
    }
    s_launcher_visible = false;
    /* Only if what is underneath is the clock. If an app is holding the screen
     * -which is the other case where this is called- uncovering the face means
     * paying to draw it underneath something that is going to cover it anyway,
     * and with the word face (110 labels) that is precisely what made the
     * slide drag and the menu show on top of the app. */
    if (!s_current) {
        watchface_show();
    }
    slide(s_launcher, true, 0, AOS_SCREEN_H, launcher_hidden_cb);
}

bool aos_ui_open(const char *id)
{
    aos_app_t *app = aos_ui_app_find(id);
    if (!app) {
        aos_hal_log("ui", "there is no app %s", id ? id : "(null)");
        return false;
    }
    if (s_current == app) {
        return true;
    }

    /* the app that was in front goes to the background or dies */
    if (s_current) {
        if (s_current->hide) {
            s_current->hide(s_current, s_current->inst);
        }
        if (s_current->desc.flags & AOS_APP_FLAG_BACKGROUND) {
            lv_obj_add_flag(s_current->root, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_current->destroy) {
                s_current->destroy(s_current, s_current->inst);
            }
            lv_obj_delete(s_current->root);
            s_current->root = NULL;
            s_current->inst = NULL;
            s_current->running = false;
        }
        s_current = NULL;
    }

    aos_hal_log("ui", "opening app %s", app->desc.id);

    bool fullscreen = (app->desc.flags & AOS_APP_FLAG_FULLSCREEN) != 0;

    /* Before create(): that is where the app builds its UI and calls _(). It
     * goes in both paths -creating and coming back from the background-
     * because a live app goes on translating from its tick and its show(). */
    aos_i18n_app_load(app->desc.id);

    if (!app->root) {
        app->root = lv_obj_create(s_stage);
        lv_obj_remove_style_all(app->root);
        lv_obj_set_size(app->root, AOS_SCREEN_W, AOS_SCREEN_H - (fullscreen ? 0 : STATUSBAR_H));
        lv_obj_set_pos(app->root, 0, fullscreen ? 0 : STATUSBAR_H);
        lv_obj_set_style_bg_color(app->root, AOS_C_BG, 0);
        lv_obj_set_style_bg_opa(app->root, LV_OPA_COVER, 0);
        lv_obj_remove_flag(app->root, LV_OBJ_FLAG_SCROLLABLE);
        if (app->create) {
            app->inst = app->create(app, app->root);
        }
        app->running = true;
    } else {
        lv_obj_remove_flag(app->root, LV_OBJ_FLAG_HIDDEN);
    }

    aos_hal_log("ui", "  %s created (inst=%p)", app->desc.id, app->inst);

    lv_obj_move_foreground(app->root);
    slide(app->root, false, AOS_SCREEN_W, 0, watchface_hide_if_covered);
    if (app->show) {
        app->show(app, app->inst);
    }

    s_current = app;
    launcher_hide();
    aos_ui_statusbar_set_visible(!fullscreen);
    if (!fullscreen) {
        lv_obj_move_foreground(s_statusbar);
    }
    aos_hal_activity();
    return true;
}

/* The physical button is offered to the front app first: a game wants it as a
 * trigger and not as "back". If it does not consume it, the caller decides
 * (main.c on the board). In the launcher it is never offered. */
bool aos_ui_button(int action)
{
    if (s_launcher_visible || !s_current || !s_current->button) {
        return false;
    }
    return s_current->button(s_current, s_current->inst, action);
}

static void close_current(bool to_home)
{
    if (!s_current) {
        return;
    }
    aos_app_t *app = s_current;
    s_current = NULL;
    aos_i18n_app_unload();

    if (app->hide) {
        app->hide(app, app->inst);
    }

    if (app->desc.flags & AOS_APP_FLAG_BACKGROUND) {
        lv_obj_add_flag(app->root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(app->root, 0);
    } else {
        if (app->destroy) {
            app->destroy(app, app->inst);
        }
        lv_obj_delete(app->root);
        app->root = NULL;
        app->inst = NULL;
        app->running = false;
    }

    if (to_home) {
        /* Explicitly and not through launcher_hide(): if the app was opened
         * from the clock, the launcher was never visible and that function
         * does nothing. */
        watchface_show();
        launcher_hide();
        aos_watchface_refresh();
    } else {
        aos_ui_show_launcher();
    }
}

void aos_ui_back(void)
{
    if (aos_pair_ui_visible()) {
        aos_pair_ui_cancel();
        return;
    }
    if (aos_notif_ui_visible()) {
        aos_notif_ui_close();
        return;
    }
    if (aos_watchface_picker_visible()) {
        aos_watchface_close_picker();
        return;
    }
    if (s_current) {
        if (s_current->back && s_current->back(s_current, s_current->inst)) {
            return;     /* the app handled it internally */
        }
        close_current(false);
        return;
    }
    if (s_launcher_visible) {
        launcher_hide();
        aos_ui_statusbar_set_visible(false);
    }
}

void aos_ui_home(void)
{
    if (s_current) {
        close_current(true);
    }
    launcher_hide();
    aos_ui_statusbar_set_visible(false);
}

const char *aos_ui_current_app(void)
{
    return s_current ? s_current->desc.id : NULL;
}

void aos_ui_request_watchface_picker(void)
{
    s_picker_requested = true;
}

void aos_ui_request_language(const char *code)
{
    snprintf(s_lang_requested, sizeof(s_lang_requested), "%s", code ? code : "");
}

/* --- Screen capture ------------------------------------------------------
 *
 * One slot, one capture at a time. The states run IDLE -> WANTED -> READY (or
 * FAILED) -> RELEASING -> IDLE, and only the UI task ever touches s_snap_buf:
 * the HTTP task reads the pixels but never allocates or frees them.
 *
 * Deliberately not a mutex or a queue. The transitions are a single word each,
 * every one of them is made by exactly one of the two tasks, and the slot is
 * either free or it is not - a lock here would only make the failure mode
 * "the web server blocks the UI" possible, which is worse than "come back in a
 * second". */
typedef enum {
    SNAP_IDLE = 0,
    SNAP_WANTED,
    SNAP_READY,
    SNAP_FAILED,
    SNAP_RELEASING,
} snap_slot_t;

static volatile snap_slot_t  s_snap_state;
static volatile bool         s_snap_wake_wanted;
static lv_draw_buf_t        *s_snap_buf;
static uint8_t               s_snap_wake_ticks;

bool aos_ui_request_snapshot(bool wake)
{
    if (s_snap_state != SNAP_IDLE) {
        return false;
    }
    s_snap_wake_wanted = wake;
    s_snap_state = SNAP_WANTED;
    return true;
}

aos_snapshot_state_t aos_ui_snapshot_peek(aos_ui_snapshot_t *out)
{
    if (s_snap_state == SNAP_FAILED) {
        return AOS_SNAPSHOT_FAILED;
    }
    if (s_snap_state != SNAP_READY || !s_snap_buf) {
        return AOS_SNAPSHOT_PENDING;
    }
    if (out) {
        out->data   = s_snap_buf->data;
        out->stride = s_snap_buf->header.stride;
        out->w      = (uint16_t)s_snap_buf->header.w;
        out->h      = (uint16_t)s_snap_buf->header.h;
    }
    return AOS_SNAPSHOT_READY;
}

void aos_ui_snapshot_release(void)
{
    if (s_snap_state == SNAP_IDLE || s_snap_state == SNAP_WANTED) {
        return;                     /* nothing to release */
    }
    s_snap_state = SNAP_RELEASING;  /* freed by the tick, not by this task */
}

/* Composites the top layer (ARGB8888) onto the screen capture (RGB565).
 *
 * In memory, LVGL's ARGB8888 comes B, G, R, A -it is an lv_color32_t, that is,
 * little-endian- and RGB565 carries red in the high bits. Writing the order
 * the wrong way round breaks nothing visible in grey or in white: it only
 * shows in a saturated colour, which on this screen is precisely the password
 * in green. */
static void snapshot_blend_top(lv_draw_buf_t *base, const lv_draw_buf_t *top)
{
    int32_t w = LV_MIN(base->header.w, top->header.w);
    int32_t h = LV_MIN(base->header.h, top->header.h);

    for (int32_t y = 0; y < h; y++) {
        uint16_t      *d = (uint16_t *)(base->data + (size_t)y * base->header.stride);
        const uint8_t *s = top->data + (size_t)y * top->header.stride;

        for (int32_t x = 0; x < w; x++, s += 4) {
            uint8_t a = s[3];
            if (a == 0) {
                continue;                       /* empty layer: the background shows */
            }
            uint8_t sb = s[0], sg = s[1], sr = s[2];
            if (a != 255) {
                /* Replicating the high bits, the same as aos_web's BMP
                 * converter: without that the white of the background comes in
                 * as 0xF8 and a semi-transparent layer over white comes out a
                 * dirty grey. */
                uint16_t p   = d[x];
                uint8_t  r5  = (uint8_t)((p >> 11) & 0x1F);
                uint8_t  g6  = (uint8_t)((p >> 5)  & 0x3F);
                uint8_t  b5  = (uint8_t)( p        & 0x1F);
                uint8_t  dr  = (uint8_t)((r5 << 3) | (r5 >> 2));
                uint8_t  dg  = (uint8_t)((g6 << 2) | (g6 >> 4));
                uint8_t  db  = (uint8_t)((b5 << 3) | (b5 >> 2));
                sr = (uint8_t)((sr * a + dr * (255 - a)) / 255);
                sg = (uint8_t)((sg * a + dg * (255 - a)) / 255);
                sb = (uint8_t)((sb * a + db * (255 - a)) / 255);
            }
            d[x] = (uint16_t)(((sr & 0xF8) << 8) | ((sg & 0xFC) << 3) | (sb >> 3));
        }
    }
}

/* Runs from aos_ui_tick(), which is the task that owns LVGL. */
static void snapshot_tick(void)
{
    if (s_snap_state == SNAP_RELEASING) {
        if (s_snap_buf) {
            lv_draw_buf_destroy(s_snap_buf);
            s_snap_buf = NULL;
        }
        s_snap_state = SNAP_IDLE;
        return;
    }

    if (s_snap_state != SNAP_WANTED) {
        return;
    }

    /* Wake the screen and wait. The request arrives on one tick, the state
     * change is applied at the top of this same aos_ui_tick() but only on the
     * NEXT one, and the face comes back with an animation. Five ticks of the
     * main loop are one second, which is plenty; without this wait you
     * photograph the dimmed face just as if you had not woken it. */
    if (s_snap_wake_wanted) {
        s_snap_wake_wanted = false;
        aos_hal_activity();
        s_snap_wake_ticks = 5;
        return;
    }
    if (s_snap_wake_ticks) {
        s_snap_wake_ticks--;
        return;
    }

    /* RGB565 and not RGB888: it is 322 KB instead of 483, and it is also the
     * format the screen is already drawn in, so no precision the panel does
     * not have is invented. With SPIRAM_MALLOC_ALWAYSINTERNAL at 1024 this
     * lands in PSRAM on its own. */
    s_snap_buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!s_snap_buf) {
        aos_hal_log("ui", "capture: could not take it (memory?)");
        s_snap_state = SNAP_FAILED;
        return;
    }

    /* lv_screen_active() does NOT include lv_layer_top(), which is ANOTHER
     * layer. The three Settings second screens -the access point one, the date
     * and time one, the calibration one- hang off it, so without this the
     * capture photographs what is UNDERNEATH and out comes the settings list.
     *
     * It misled badly: the image is correct, it is well drawn and it is of
     * this screen, only of the wrong layer. It was discovered by asking for a
     * capture of the QR screen with the page open on the board, and the only
     * way to notice was to look at the device next to the image. */
    if (lv_obj_get_child_count(lv_layer_top()) > 0) {
        lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(),
                                              LV_COLOR_FORMAT_ARGB8888);
        if (top) {
            snapshot_blend_top(s_snap_buf, top);
            lv_draw_buf_destroy(top);
        } else {
            /* With no memory for the top layer what is underneath is delivered
             * anyway: an incomplete capture is more use than a 503, and the
             * warning stays in the log. */
            aos_hal_log("ui", "capture: the top layer did not fit");
        }
    }

    s_snap_state = SNAP_READY;
}

/* Tears down the instance of ALL apps, not just the front one.
 *
 * A backgrounded app (the timer, the music) survives aos_ui_home() with its
 * LVGL objects intact, and those objects have the text already copied inside:
 * on reopening it, it would still be speaking the previous language. Worse
 * still, if it stored a pointer returned by aos_tr() in its own state, that
 * pointer points into a catalogue we are about to free. Changing language is
 * rare and deliberate; tearing down the timers is the price, and the notice
 * says so. */
static void destroy_all_app_instances(void)
{
    for (int i = 0; i < s_app_count; i++) {
        aos_app_t *app = &s_apps[i];
        if (!app->root) {
            continue;
        }
        if (app->destroy) {
            app->destroy(app, app->inst);
        }
        lv_obj_delete(app->root);
        app->root = NULL;
        app->inst = NULL;
        app->running = false;
    }
}

/* Runs from aos_ui_tick(), never from an event callback: it frees the strings
 * the screen being looked at is built from. */
static void apply_language(const char *code)
{
    aos_ui_home();
    destroy_all_app_instances();
    aos_i18n_app_unload();

    if (!aos_i18n_set(code)) {
        aos_ui_toast(_("No pude cargar ese idioma"), 1800);
        return;
    }

    if (s_launcher) {
        lv_obj_delete(s_launcher);
        s_launcher = NULL;
        s_launcher_visible = false;
    }
    aos_watchface_refresh();
}

void aos_ui_launcher_set_style(aos_launcher_style_t style)
{
    if (style == s_style) {
        return;
    }
    s_style = style;
    aos_hal_pref_set_i32("launcher", (int32_t)style);
    if (s_launcher) {
        bool was_visible = s_launcher_visible;
        lv_obj_delete(s_launcher);
        s_launcher = NULL;
        s_launcher_visible = false;
        if (was_visible) {
            aos_ui_show_launcher();
        }
    }
}

aos_launcher_style_t aos_ui_launcher_get_style(void)
{
    return s_style;
}

/* -------------------------------------------------------------------------- */
/* Global gestures                                                             */
/* -------------------------------------------------------------------------- */

/*
 * LVGL sends the swipe gesture to the object under the finger, and only
 * propagates it to the parent if that object has LV_OBJ_FLAG_GESTURE_BUBBLE.
 * Hanging off the screen is not enough: inside an app the gesture is eaten by
 * the first button or list you touch.
 *
 * Fortunately LVGL also emits the same event to the input device
 * (lv_indev_send_event in indev_gesture), so by hooking there we see every
 * gesture, no matter where the finger started.
 */
/* The action, separated from who detected it: on the board the gesture is
 * reported by the touch controller itself, in the simulator LVGL detects it. */
/* Chip gesture saved for the front app (see aos_ui_tick). */
static aos_touch_gesture_t s_app_gesture;

int aos_ui_take_gesture(void)
{
    int g = (int)s_app_gesture;
    s_app_gesture = AOS_TOUCH_GESTURE_NONE;
    return g;
}

static void handle_gesture(lv_dir_t dir)
{
    aos_hal_log("touch", "  -> action: app=%s launcher=%d",
                s_current ? s_current->desc.id : "(none)", (int)s_launcher_visible);
    aos_hal_activity();

    /* The pairing request takes the gesture before anybody else: it is the
     * only thing on screen waiting for an answer from the other side. Swiping
     * cancels, just like the button. */
    if (aos_pair_ui_visible()) {
        aos_pair_ui_cancel();
        return;
    }

    /* With the notification on screen, any gesture closes it and that is the
     * end of it.
     *
     * This cut is needed because gesture_cb hangs off the INPUT DEVICE and not
     * off the screen (see aos_ui_init): the gesture arrives anyway even if the
     * overlay covers everything, and without this a swipe right would "go
     * back" in the app underneath, which the user is not even looking at. */
    if (aos_notif_ui_visible()) {
        aos_notif_ui_close();
        return;
    }

    switch (dir) {
    case LV_DIR_RIGHT:
        if (s_current && (s_current->desc.flags & AOS_APP_FLAG_NO_SWIPE)) {
            return;
        }
        aos_ui_back();
        break;

    case LV_DIR_TOP:
        if (!s_current && !s_launcher_visible) {
            aos_ui_show_launcher();
        }
        break;

    case LV_DIR_BOTTOM:
        if (!s_current && s_launcher_visible) {
            aos_ui_back();
        }
        break;

    default:
        break;
    }
}

static void gesture_cb(lv_event_t *event)
{
    (void)event;
    lv_indev_t *indev = lv_indev_active();
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    aos_hal_log("touch", "GESTURE dir=%d (1=left 2=right 4=up 8=down) app=%s", (int)dir,
                s_current ? s_current->desc.id : "(launcher/watch)");

    /* On release, LVGL sends LV_EVENT_CLICKED to the object that was under the
     * finger even if it has travelled half the screen: without this, swiping
     * to go back would also open the app that ended up under the finger.
     *
     * An app with AOS_APP_FLAG_LONG_DRAG asked for the opposite: that this
     * global cut leave its in-flight touch alone, because it drags objects
     * further than the 50px that fire this gesture (a car on a board, say) and
     * needs the release to reach it. Skipping it here changes nothing for the
     * rest: handle_gesture() already does nothing useful for an app with
     * NO_SWIPE (see below), so this only avoids the wait_release(). */
    if (s_current && (s_current->desc.flags & AOS_APP_FLAG_LONG_DRAG)) {
        return;
    }

    lv_indev_wait_release(indev);

    handle_gesture(dir);
}

/* Any touch counts as activity. If the screen was dimmed or off, the first
 * touch only wakes: it is discarded so it does not accidentally fire the
 * button that happened to be under the finger. */
static lv_point_t s_press_point;

/* Touch diagnostics: we wrap the driver's read_cb to count reads and presses.
 * When the screen stops responding this separates three causes that look
 * identical from outside: the driver stopped reading, the chip stopped
 * reporting the finger, or the touch arrives and we discard it ourselves. It
 * does not touch the I2C bus: polling it ourselves is precisely what set off
 * the watchdogs last time. */
/* ONE indev is wrapped, and which one is remembered.
 *
 * Wrapping them all and storing the original in a loose variable works on the
 * board by accident, because there is a single pointer there. In the simulator
 * there are two (SDL's mouse and the scripts' virtual pointer): the second
 * overwrote the stored callback and then each indev called the other's driver.
 * SDL's driver looks for its context in the indev it is given, does not find
 * it and blows up. With the indev noted down, the wrapper only calls the
 * callback that belongs to it. */
/* -------------------------------------------------------------------------- */
/* Touch calibration                                                           */
/*                                                                             */
/* The BSP shifts the panel's window by 16 px in X (bsp_display_set_x_gap) but
 * does NOT compensate the touch coordinates, and besides, each unit has its
 * own tolerance in how the glass was bonded. Rather than hard-coding a number,
 * a linear fit per axis is stored, measured by touching known points:
 *
 *      screen = a * raw + b
 *
 * With a=1 and b=0 nothing is touched, which is the factory state.            */
/* -------------------------------------------------------------------------- */

static float s_cal_ax = 1.0f, s_cal_bx = 0.0f;
static float s_cal_ay = 1.0f, s_cal_by = 0.0f;
static bool  s_cal_raw;         /* during calibration they are read raw */

/* In NVS they go as integers: 'a' x10000 and 'b' x100. */
static void cal_load(void)
{
    int32_t v;
    if (aos_hal_pref_get_i32("cal_ax", &v)) s_cal_ax = (float)v / 10000.0f;
    if (aos_hal_pref_get_i32("cal_bx", &v)) s_cal_bx = (float)v / 100.0f;
    if (aos_hal_pref_get_i32("cal_ay", &v)) s_cal_ay = (float)v / 10000.0f;
    if (aos_hal_pref_get_i32("cal_by", &v)) s_cal_by = (float)v / 100.0f;
}

void aos_ui_touch_calibration_save(float ax, float bx, float ay, float by)
{
    s_cal_ax = ax; s_cal_bx = bx;
    s_cal_ay = ay; s_cal_by = by;
    aos_hal_pref_set_i32("cal_ax", (int32_t)(ax * 10000.0f));
    aos_hal_pref_set_i32("cal_bx", (int32_t)(bx * 100.0f));
    aos_hal_pref_set_i32("cal_ay", (int32_t)(ay * 10000.0f));
    aos_hal_pref_set_i32("cal_by", (int32_t)(by * 100.0f));
    aos_hal_log("touch", "calibration saved: x = %d/10000*c + %d/100, "
                         "y = %d/10000*c + %d/100",
                (int)(ax * 10000), (int)(bx * 100),
                (int)(ay * 10000), (int)(by * 100));
}

void aos_ui_touch_calibration_reset(void)
{
    aos_ui_touch_calibration_save(1.0f, 0.0f, 1.0f, 0.0f);
}

void aos_ui_touch_raw(bool raw)
{
    s_cal_raw = raw;
}

static lv_indev_t        *s_counted_indev;
static lv_indev_read_cb_t s_orig_read_cb;
static volatile uint32_t  s_touch_reads;
static volatile uint32_t  s_touch_presses;

static void counting_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (indev != s_counted_indev) {
        return;             /* not ours: we do not invent readings */
    }
    s_touch_reads++;
    if (s_orig_read_cb) {
        s_orig_read_cb(indev, data);
    }
    if (data->state == LV_INDEV_STATE_PRESSED) {
        s_touch_presses++;

        if (!s_cal_raw) {
            float x = s_cal_ax * (float)data->point.x + s_cal_bx;
            float y = s_cal_ay * (float)data->point.y + s_cal_by;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x > AOS_SCREEN_W - 1) x = AOS_SCREEN_W - 1;
            if (y > AOS_SCREEN_H - 1) y = AOS_SCREEN_H - 1;
            data->point.x = (int32_t)(x + 0.5f);
            data->point.y = (int32_t)(y + 0.5f);
        }
    }
}

void aos_ui_touch_stats(uint32_t *reads, uint32_t *presses)
{
    if (reads)   *reads   = s_touch_reads;
    if (presses) *presses = s_touch_presses;
}

static void press_cb(lv_event_t *event)
{
    (void)event;
    lv_indev_t *indev = lv_indev_active();
    lv_indev_get_point(indev, &s_press_point);

    if (aos_hal_display_state() != AOS_DISPLAY_ACTIVE) {
        aos_hal_log("touch", "PRESS at %d,%d -> screen asleep, it only wakes",
                    (int)s_press_point.x, (int)s_press_point.y);
        aos_hal_activity();
        lv_indev_wait_release(indev);
        return;
    }
    aos_hal_log("touch", "PRESS at %d,%d", (int)s_press_point.x, (int)s_press_point.y);
    aos_hal_activity();
}

static void release_cb(lv_event_t *event)
{
    (void)event;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    aos_hal_log("touch", "RELEASE at %d,%d  (travelled %d,%d)",
                (int)p.x, (int)p.y,
                (int)(p.x - s_press_point.x), (int)(p.y - s_press_point.y));
}

static void display_state_cb(aos_display_state_t state)
{
    s_pending_display_state = state;
    s_display_state_dirty = true;
}

/* -------------------------------------------------------------------------- */
/* Phone notifications                                                         */
/* -------------------------------------------------------------------------- */

/* The HAL leaves them in a queue because the provider runs in another task and
 * cannot touch LVGL. This is where a notification turns into pixels, and it is
 * the only place: this runs in the LVGL task, with the lock held.
 *
 * Whether to alert or not is NOT decided here: it arrives already resolved in
 * 'alert' and 'sound', filled in by the HAL's policy (aos_notif.c). This file
 * only draws. */
static void notif_tick(void)
{
    /* What the phone withdrew. If it is exactly what is being looked at, it
     * closes itself: the user has already dealt with it on the other side. */
    uint32_t uid;
    while (aos_hal_notif_pop_removed(&uid)) {
        aos_hal_log("notif", "the phone withdrew #%u", (unsigned)uid);
        if (aos_notif_ui_uid() == uid) {
            aos_notif_ui_close();
        }
    }

    aos_notif_t n;
    while (aos_hal_notif_pop(&n)) {
        aos_hal_log("notif", "#%u %s / %s%s%s", (unsigned)n.uid, n.app, n.title,
                    n.alert ? "  [alerts]" : "  [history only]",
                    n.sound ? "  [sounds]" : "");
        if (!n.alert) {
            continue;
        }

        /* Wake first. With the screen dimmed or off this brings it back to
         * ACTIVE, and the state change arrives through the HAL's callback only
         * on the next tick; by then the overlay is already built, so the face
         * is not seen poking through in between. */
        aos_hal_activity();
        aos_notif_ui_show(&n, false);
        if (n.sound) {
            aos_hal_beep(1760, 70);
        }
    }

    /* The phone may accept the action and then be unable to carry it out
     * -answering a call, for instance-. Without this, the screen closes just
     * as if it had gone well and the user believes they answered. */
    if (aos_hal_notif_action_failed()) {
        aos_ui_toast(_("El telefono no pudo"), 2000);
    }

    aos_notif_ui_tick();
}

/* -------------------------------------------------------------------------- */
/* Ticks                                                                       */
/* -------------------------------------------------------------------------- */

void aos_ui_tick(void)
{
    /* Safety net: the menu cannot be "open" and not exist.
     *
     * That pair -s_launcher NULL with s_launcher_visible true- leaves the
     * watch unusable until it is restarted: the menu cannot be opened (because
     * it is believed to be open) nor closed (because there is no object), and
     * the face hides itself. It was caused by registering an app with the menu
     * on screen, which is already fixed in launcher_invalidate(); this stays
     * because the cost of the failure is too high to trust a single fix, and
     * repairing it is setting a flag to false.
     *
     * If this line shows up in the log there is a new path that breaks the
     * invariant: do not ignore it. */
    if (!s_launcher && s_launcher_visible) {
        aos_hal_log("ui", "inconsistent menu (visible with no object): repaired");
        s_launcher_visible = false;
    }

    if (s_display_state_dirty) {
        s_display_state_dirty = false;
        aos_display_state_t state = s_pending_display_state;

        if (state == AOS_DISPLAY_ACTIVE) {
            aos_watchface_set_aod(false);
        } else {
            /* on dimming we go back to the clock: the face is the only thing
             * worth leaving lit. The notification is closed: otherwise it
             * hangs underneath the always-on face and reappears out of nowhere
             * on the first touch. */
            aos_notif_ui_close();
            aos_ui_home();
            aos_watchface_set_aod(true);
        }
    }

    /* Safety net for the face's visibility.
     *
     * Hiding it depends on an animation-finished callback, and an
     * animation-finished callback does NOT run if the animation is cancelled
     * first -for instance if another one is started on the same object-. If
     * that happens, the face stays visible underneath an app and its drawing
     * is paid for forever, which is an expensive and silent bug. Here it
     * corrects itself: with something covering it and no animation in flight,
     * there is no reason for it to still be visible. Asking about the
     * animations is what avoids cutting a transition short. */
    if (s_watchface && (s_current || s_launcher_visible) &&
        !lv_obj_has_flag(s_watchface, LV_OBJ_FLAG_HIDDEN) &&
        lv_anim_count_running() == 0) {
        lv_obj_add_flag(s_watchface, LV_OBJ_FLAG_HIDDEN);
    }

    /* Gesture reported by the touch controller (the v2 board detects them
     * itself; see aos_hal_touch_gesture).
     *
     * It is ALWAYS consumed, including when the app keeps the gestures: the
     * chip's register holds only one and if nobody reads it, it stays pending
     * until somebody asks, that is, it reappears on leaving the app as a
     * phantom gesture. When the app asked for NO_SWIPE we keep it for it to
     * collect with aos_ui_take_gesture(); otherwise it is dispatched as usual.
     *
     * It is needed because during a fast swipe the chip stops sending
     * coordinates and LVGL never gathers the 50 px of its own gesture: an app
     * with horizontal pages never finds out through LV_EVENT_GESTURE. */
    aos_touch_gesture_t chip = aos_hal_touch_gesture();
    if (s_current && (s_current->desc.flags & AOS_APP_FLAG_NO_SWIPE)) {
        if (chip != AOS_TOUCH_GESTURE_NONE) {
            s_app_gesture = chip;
            aos_hal_activity();
        }
    } else {
        switch (chip) {
        case AOS_TOUCH_GESTURE_UP:    handle_gesture(LV_DIR_TOP);    break;
        case AOS_TOUCH_GESTURE_DOWN:  handle_gesture(LV_DIR_BOTTOM); break;
        case AOS_TOUCH_GESTURE_RIGHT: handle_gesture(LV_DIR_RIGHT);  break;
        case AOS_TOUCH_GESTURE_LEFT:  handle_gesture(LV_DIR_LEFT);   break;
        default: break;
        }
    }

    if (s_picker_requested) {
        s_picker_requested = false;
        aos_ui_home();
        aos_watchface_open_picker();
    }

    if (s_lang_requested[0]) {
        char code[AOS_LANG_CODE_MAX];
        snprintf(code, sizeof(code), "%s", s_lang_requested);
        s_lang_requested[0] = '\0';
        apply_language(code);
    }

    /* Before the notifications: if both arrive at once, what needs an answer
     * goes first. */
    aos_pair_ui_tick();

    notif_tick();

    /* Before the screen-off cut below: with the screen off you still want to
     * be able to look at what is drawn. */
    snapshot_tick();

    /* With the screen off there is nothing to draw. The timers of backgrounded
     * apps go on running all the same. */
    if (aos_hal_display_state() == AOS_DISPLAY_OFF) {
        return;
    }

    /* An app that asked for KEEP_AWAKE keeps the screen awake while it is in
     * the foreground: flashlight, level, anything you stare at without
     * touching the screen. */
    if (s_current && (s_current->desc.flags & AOS_APP_FLAG_KEEP_AWAKE)) {
        aos_hal_activity();
    }

    for (int i = 0; i < s_app_count; i++) {
        aos_app_t *app = &s_apps[i];
        if (!app->tick || !app->running) {
            continue;
        }
        bool foreground = (app == s_current);
        if (foreground || (app->desc.flags & AOS_APP_FLAG_BACKGROUND)) {
            app->tick(app, app->inst);
        }
    }

    if (aos_watchface_is_aod()) {
        aos_watchface_refresh();     /* the face decides how little it repaints */
        return;
    }

    aos_ui_statusbar_refresh();
    if (!s_current && !s_launcher_visible) {
        aos_watchface_refresh();
    }
}

/* -------------------------------------------------------------------------- */
/* Startup                                                                     */
/* -------------------------------------------------------------------------- */

void aos_ui_init(void)
{
    aos_theme_init();
    aos_i18n_init();

    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* A single gesture handler, at the input device level, for the whole UI. */
    for (lv_indev_t *indev = lv_indev_get_next(NULL);
         indev != NULL;
         indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_add_event_cb(indev, gesture_cb, LV_EVENT_GESTURE, NULL);
            /* 50 px by default is a lot for a 368 px screen: with the finger
             * resting the CST820 does not always send that many intermediate
             * points */
            lv_indev_set_gesture_min_distance(indev, 30);
            lv_indev_add_event_cb(indev, press_cb, LV_EVENT_PRESSED, NULL);
            lv_indev_add_event_cb(indev, release_cb, LV_EVENT_RELEASED, NULL);
            if (!s_counted_indev) {
                s_counted_indev = indev;
                cal_load();
                s_orig_read_cb  = lv_indev_get_read_cb(indev);
                lv_indev_set_read_cb(indev, counting_read_cb);
            }

            /* The port leaves the touch panel in EVENT mode: it is only read
             * when the CST820 asserts the interrupt line (GPIO21). If the chip
             * falls asleep and stops asserting it, the read NEVER runs again
             * and the screen is left unresponsive forever, drawing perfectly
             * normally. Measured: with the system idle, zero reads. In TIMER
             * mode LVGL reads on every refresh, so a lost interrupt is no
             * longer fatal. The interrupt is still enabled and fires the read
             * immediately, so we lose no responsiveness. */
            lv_indev_set_mode(indev, LV_INDEV_MODE_TIMER);
        }
    }

    s_stage = lv_obj_create(screen);
    lv_obj_remove_style_all(s_stage);
    lv_obj_set_size(s_stage, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(s_stage, 0, 0);
    lv_obj_set_style_bg_color(s_stage, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_stage, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_SCROLLABLE);

    s_watchface = aos_watchface_create(s_stage);

    statusbar_build(screen);
    aos_ui_statusbar_set_visible(false);

    int32_t saved_style = 0;
    if (aos_hal_pref_get_i32("launcher", &saved_style)) {
        s_style = (aos_launcher_style_t)saved_style;
    }

    aos_hal_set_display_state_cb(display_state_cb);
    aos_ui_statusbar_refresh();
}
