/*
 * Remoto - programmable Home Assistant remote
 *
 * Pages of buttons, accelerometer gestures and a dial driven by turning your
 * wrist. What each thing does is NOT in this file: it comes from the profile
 * edited at http://<ip>/remoto and stored on the microSD (see rc_model.h).
 * Here it is only drawn and dispatched.
 *
 * Division of labour:
 *   rc_model.c   the profile: reading and understanding it
 *   rc_tilt.c    the accelerometer: from three numbers to a gesture
 *   rc_ha.c      the network: calling services and fetching states
 *   remoto.c     this: screen, touches, and who passes what to whom
 *
 * The first three do not depend on LVGL, and the first two do not depend on
 * the HAL either: apps/remoto/tools/rc_harness.c tests them with plain 'cc'.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"
#include "aos_theme.h"

#include "rc_model.h"
#include "rc_tilt.h"
#include "rc_ha.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_ID      "aos.remoto"

/* A clock of its own: the IMU hangs off the same I2C bus as the touch panel
 * and polling it more often has already saturated CPU 0 once
 * (docs/DECISIONES.md). Ten a second is enough for the gestures and for the
 * dial to feel continuous. */
#define TICK_MS     100

/* Vertical layout of the 418 px left under the status bar. */
/* 2026-09-11: the panel reports nothing above y = 55 (AOS_TOUCH_Y_MIN), so
 * everything touchable starts at 56 and the rows above are for text. */
#define TITLE_Y     24
/* 36 and not 30: aos_font_title has a 35 px line height and the box was
 * clipping the descender of the page's title. */
#define TITLE_H     36
#define BODY_Y      58
#define BODY_H      306
#define DOTS_Y      372
#define STATUS_Y    392

#define PAD         10
#define GAP         8

/* How long an action's result is shown before going back to the fixed text. */
#define NOTE_MS     3500

/* Two reports of the same swipe: one from the touch chip and one from LVGL. On
 * the board both arrive and without this a clumsy finger would skip two
 * pages. */
#define GESTURE_GAP_MS  400

typedef struct {
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *dots[RC_MAX_PAGES];
    lv_obj_t *status;

    /* grid: the nine are created once and filled in on changing page.
     * Rebuilding them cost 111-124 ms measured on the board; filling them in,
     * 18-31. It is in the handoff and it is the difference between smooth and
     * not. */
    lv_obj_t *grid;
    lv_obj_t *cell[RC_MAX_BUTTONS];
    lv_obj_t *cell_label[RC_MAX_BUTTONS];
    lv_obj_t *cell_value[RC_MAX_BUTTONS];

    /* dial */
    lv_obj_t *dialbox;
    lv_obj_t *arc;
    lv_obj_t *knob;
    lv_obj_t *knob_label;
    lv_obj_t *dial_name;
    lv_obj_t *dial_hint;

    lv_obj_t *setup;            /* the "not configured yet" screen */
    lv_obj_t *setup_text;

    /* Accelerometer diagnostics: reached by holding the title. */
    lv_obj_t *diag;
    lv_obj_t *diag_text;
    bool      diag_on;
    int       last_gesture;
    uint32_t  last_gesture_at;

    lv_timer_t   *timer;
    rc_profile_t *prof;
    char         *tpl;
    rc_ha_t       ha;
    rc_tilt_t     tilt;
    char          err[96];

    int      page;
    int      pressed;           /* button under the finger, -1 = none */
    bool     long_fired;
    bool     finger_down;

    bool     dial_active;
    float    dial_turn;         /* degrees accumulated since the latch */
    float    dial_prev_roll;
    int      dial_v0, dial_v, dial_sent;
    uint32_t dial_last_send;

    uint32_t last_gesture_ms;
    uint32_t last_poll_ms;
    uint32_t note_until;
    int32_t  gen;               /* generation of the configuration already loaded */
    bool     want_exit;
    bool     want_reload;
    bool     closing;           /* see rc_destroy */
} rc_app_t;

static aos_app_t *s_self;       /* to touch desc.flags, see create() */

static void refresh_mode(rc_app_t *a);

/* -------------------------------------------------------------------------- */
/* Utilities                                                                   */
/* -------------------------------------------------------------------------- */

static uint32_t now_ms(void)
{
    /* aos_hal_uptime_ms() returns 64 bits and dividing a uint64_t drags
     * __udivdi3 into the .so needlessly: 32 bits is 49 days of uptime. */
    return (uint32_t)aos_hal_uptime_ms();
}

static void say(rc_app_t *a, const char *text)
{
    if (a->status) {
        lv_label_set_text(a->status, text);
    }
    a->note_until = now_ms() + NOTE_MS;
}

static const rc_page_t *cur_page(const rc_app_t *a)
{
    if (!a->prof || a->page < 0 || a->page >= a->prof->n_pages) {
        return NULL;
    }
    return &a->prof->pages[a->page];
}

/* -------------------------------------------------------------------------- */
/* Running an action                                                           */
/* -------------------------------------------------------------------------- */

static void show_page(rc_app_t *a, int page);

static void run_action(rc_app_t *a, const rc_action_t *act, const char *origen)
{
    if (!act || act->type == RC_ACT_NONE) {
        return;
    }
    if (act->type == RC_ACT_PAGE) {
        show_page(a, act->page);
        aos_hal_beep(1400, 12);
        return;
    }
    aos_hal_beep(2000, 18);
    if (rc_ha_call(&a->ha, act)) {
        char line[64];
        snprintf(line, sizeof(line), "%s%s", origen ? origen : "", act->service);
        say(a, line);
    } else {
        say(a, a->ha.last);
        aos_hal_beep(400, 90);
    }
}

/* -------------------------------------------------------------------------- */
/* Grid                                                                        */
/* -------------------------------------------------------------------------- */

static void cell_event(lv_event_t *e)
{
    rc_app_t *a   = lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (a->closing) {
        return;
    }

    int idx = -1;
    for (int i = 0; i < RC_MAX_BUTTONS; i++) {
        if (a->cell[i] == obj) {
            idx = i;
            break;
        }
    }
    const rc_page_t *pg = cur_page(a);
    if (idx < 0 || !pg || idx >= pg->count) {
        return;
    }

    switch (code) {
    case LV_EVENT_PRESSED:
        a->pressed     = idx;
        a->long_fired  = false;
        a->finger_down = true;
        break;

    case LV_EVENT_LONG_PRESSED:
        if (pg->btn[idx].hold.type != RC_ACT_NONE) {
            a->long_fired = true;
            run_action(a, &pg->btn[idx].hold, "");
        }
        break;

    case LV_EVENT_CLICKED:
        /* LVGL sends CLICKED on release even after having sent LONG_PRESSED.
         * Without this, a hold fires both actions. */
        if (!a->long_fired) {
            run_action(a, &pg->btn[idx].tap, "");
        }
        a->long_fired = false;
        break;

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        a->pressed     = -1;
        a->finger_down = false;
        break;

    default:
        break;
    }
}

/* Writes into an already created button whatever belongs to the current page. */
static void fill_cell(rc_app_t *a, int i, const rc_button_t *b,
                      int32_t w, int32_t h, int32_t x, int32_t y)
{
    lv_obj_t *c = a->cell[i];
    lv_obj_remove_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);

    lv_color_t col = lv_color_hex(b->color);
    lv_obj_set_style_bg_color(c, col, 0);
    lv_obj_set_style_bg_color(c, col, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(c, col, 0);

    const bool on = (b->st_mode == RC_ST_ONOFF) &&
                    rc_state_is_on(a->prof, b->slot);
    /* A button that is "on" is painted filled and one that is off, tinted. On
     * AMOLED that is also the difference between spending battery and not
     * spending it. */
    lv_obj_set_style_bg_opa(c, on ? LV_OPA_COVER : LV_OPA_30, 0);

    /* Over a filled button white text is not always readable: a yellow or a
     * cyan at full opacity makes it disappear. It is decided by luminance,
     * with the usual weights (30/59/11), so the user can pick whatever colour
     * they like without having to think about this. Switched off the
     * background is nearly black and white always works. */
    uint32_t lum = (((b->color >> 16) & 0xFF) * 30 +
                    ((b->color >>  8) & 0xFF) * 59 +
                    ( b->color        & 0xFF) * 11) / 100;
    lv_color_t txt = (on && lum > 150) ? lv_color_hex(0x000000) : AOS_C_TEXT;
    lv_obj_set_style_text_color(a->cell_label[i], txt, 0);
    lv_obj_set_style_text_color(a->cell_value[i], txt, 0);

    const lv_font_t *font = (w >= 150) ? aos_font_body : aos_font_small;
    lv_obj_set_style_text_font(a->cell_label[i], font, 0);
    lv_label_set_text(a->cell_label[i], b->label[0] ? b->label : " ");
    lv_obj_set_width(a->cell_label[i], w - 12);

    char value[RC_LEN_VALUE + RC_LEN_UNIT + 2] = {0};
    if (b->st_mode == RC_ST_VALUE || b->st_mode == RC_ST_ATTR) {
        const char *v = rc_state_value(a->prof, b->slot);
        if (!v[0] || strcmp(v, "None") == 0 || strcmp(v, "unknown") == 0 ||
            strcmp(v, "unavailable") == 0) {
            snprintf(value, sizeof(value), "--");
        } else {
            snprintf(value, sizeof(value), "%s%s", v, b->unit);
        }
    }

    if (value[0]) {
        lv_obj_align(a->cell_label[i], LV_ALIGN_CENTER, 0, -h / 6);
        lv_label_set_text(a->cell_value[i], value);
        lv_obj_set_width(a->cell_value[i], w - 8);
        lv_obj_align(a->cell_value[i], LV_ALIGN_CENTER, 0, h / 5);
        lv_obj_remove_flag(a->cell_value[i], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_align(a->cell_label[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(a->cell_value[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void layout_grid(rc_app_t *a)
{
    const rc_page_t *pg = cur_page(a);
    if (!pg) {
        return;
    }
    int32_t cw = (368 - 2 * PAD - (pg->cols - 1) * GAP) / pg->cols;
    int32_t ch = (BODY_H - (pg->rows - 1) * GAP) / pg->rows;

    for (int i = 0; i < RC_MAX_BUTTONS; i++) {
        if (i >= pg->count) {
            lv_obj_add_flag(a->cell[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int col = i % pg->cols;
        int row = i / pg->cols;
        fill_cell(a, i, &pg->btn[i], cw, ch,
                  PAD + col * (cw + GAP), row * (ch + GAP));
    }
}

/* -------------------------------------------------------------------------- */
/* Dial                                                                        */
/* -------------------------------------------------------------------------- */

static void dial_paint(rc_app_t *a)
{
    const rc_page_t *pg = cur_page(a);
    if (!pg || pg->kind != RC_PAGE_DIAL) {
        return;
    }
    lv_arc_set_range(a->arc, pg->dial.min, pg->dial.max);
    lv_arc_set_value(a->arc, a->dial_v);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", a->dial_v);
    lv_label_set_text(a->knob_label, buf);
}

/* The value Home Assistant says it has now, mapped into the dial's range. */
static int dial_from_state(const rc_app_t *a, const rc_dial_t *d)
{
    const char *v = rc_state_value(a->prof, d->slot);
    if (!v[0] || v[0] < '0' || v[0] > '9') {
        return a->dial_v;               /* None, unavailable, switched off... */
    }
    long raw = strtol(v, NULL, 10);
    long val = raw * (d->max - d->min) / (d->st_full ? d->st_full : 100) + d->min;
    if (val < d->min) val = d->min;
    if (val > d->max) val = d->max;
    return (int)val;
}

static void dial_send(rc_app_t *a, bool force)
{
    const rc_page_t *pg = cur_page(a);
    if (!pg || pg->kind != RC_PAGE_DIAL) {
        return;
    }
    if (!force && a->dial_v == a->dial_sent) {
        return;
    }
    if (rc_ha_call_value(&a->ha, pg->dial.service, pg->dial.entity,
                         pg->dial.field, a->dial_v)) {
        a->dial_sent      = a->dial_v;
        a->dial_last_send = now_ms();
        char line[48];
        snprintf(line, sizeof(line), "%s = %d", pg->dial.field, a->dial_v);
        say(a, line);
    } else {
        say(a, a->ha.last);
    }
}

static void knob_event(lv_event_t *e)
{
    rc_app_t *a = lv_event_get_user_data(e);
    if (a->closing) {
        return;
    }
    const rc_page_t *pg = cur_page(a);
    if (!pg || pg->kind != RC_PAGE_DIAL) {
        return;
    }

    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED: {
        aos_imu_t imu;
        a->finger_down = true;
        if (!aos_hal_imu_read(&imu) || !rc_roll_usable(imu.ax, imu.ay)) {
            lv_label_set_text(a->dial_hint, _("poner la placa de pie"));
            return;
        }
        /* Relative, not absolute: zero is where you were when you pressed.
         * That way the dial is used the same lying on the sofa as sitting at
         * the table, and no vertical has to be calibrated. */
        a->dial_turn      = 0.0f;
        a->dial_prev_roll = rc_roll_deg(imu.ax, imu.ay);
        a->dial_v0        = a->dial_v;
        a->dial_active    = true;
        aos_hal_beep(1600, 10);
        lv_label_set_text(a->dial_hint, _("girar - soltar para fijar"));
        break;
    }

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        a->finger_down = false;
        if (a->dial_active) {
            a->dial_active = false;
            dial_send(a, false);
            aos_hal_beep(2200, 14);
            lv_label_set_text(a->dial_hint, _("mantener y girar la muneca"));
        }
        break;

    default:
        break;
    }
}

static void dial_step(lv_event_t *e)
{
    rc_app_t *a = lv_event_get_user_data(e);
    const rc_page_t *pg = cur_page(a);
    if (!pg || pg->kind != RC_PAGE_DIAL) {
        return;
    }
    /* Which of the two buttons it was goes in its user_data: -1 or +1. */
    lv_obj_t *btn   = lv_event_get_target(e);
    int       delta = (int)(intptr_t)lv_obj_get_user_data(btn);

    int range = pg->dial.max - pg->dial.min;
    int inc   = range / 20;
    if (inc < 1) {
        inc = 1;
    }
    a->dial_v += delta * inc;
    if (a->dial_v < pg->dial.min) a->dial_v = pg->dial.min;
    if (a->dial_v > pg->dial.max) a->dial_v = pg->dial.max;
    dial_paint(a);
    dial_send(a, false);
}

/* -------------------------------------------------------------------------- */
/* Navigation                                                                  */
/* -------------------------------------------------------------------------- */

static void refresh_dots(rc_app_t *a)
{
    for (int i = 0; i < RC_MAX_PAGES; i++) {
        if (!a->dots[i]) {
            continue;
        }
        if (!a->prof || i >= a->prof->n_pages) {
            lv_obj_add_flag(a->dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(a->dots[i], LV_OBJ_FLAG_HIDDEN);
        bool here = (i == a->page);
        lv_obj_set_style_bg_opa(a->dots[i], here ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_set_size(a->dots[i], here ? 10 : 7, here ? 10 : 7);
    }

    /* The dots are centred by hand because they change size: a flex would
     * reposition them but would also reorder them, and here the order is the
     * position. */
    int n = a->prof ? a->prof->n_pages : 0;
    int32_t total = n * 14;
    for (int i = 0; i < n; i++) {
        lv_obj_set_pos(a->dots[i], 184 - total / 2 + i * 14 + (i == a->page ? 0 : 1),
                       DOTS_Y + (i == a->page ? 0 : 2));
    }
}

static void show_page(rc_app_t *a, int page)
{
    if (!a->prof || a->prof->n_pages == 0) {
        return;
    }
    if (page < 0) {
        page = 0;
    }
    if (page >= a->prof->n_pages) {
        page = a->prof->n_pages - 1;
    }
    a->page = page;

    const rc_page_t *pg = &a->prof->pages[page];
    lv_label_set_text(a->title, pg->name[0] ? pg->name : _("Remoto"));

    a->dial_active = false;
    if (pg->kind == RC_PAGE_DIAL) {
        lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->dialbox, LV_OBJ_FLAG_HIDDEN);
        a->dial_v    = dial_from_state(a, &pg->dial);
        if (a->dial_v < pg->dial.min || a->dial_v > pg->dial.max) {
            a->dial_v = pg->dial.min;
        }
        a->dial_sent = a->dial_v;
        lv_label_set_text(a->dial_name,
                          pg->dial.entity[0] ? pg->dial.entity : pg->dial.service);
        lv_label_set_text(a->dial_hint, _("mantener y girar la muneca"));
        dial_paint(a);
    } else {
        lv_obj_add_flag(a->dialbox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
        layout_grid(a);
    }
    refresh_dots(a);
}

static void navigate(rc_app_t *a, int dir)      /* +1 next, -1 previous */
{
    uint32_t t = now_ms();
    if (t - a->last_gesture_ms < GESTURE_GAP_MS) {
        return;
    }
    a->last_gesture_ms = t;

    if (!a->prof) {
        return;
    }
    if (dir < 0 && a->page == 0) {
        /* Going back from the first page is leaving. It is the app's only
         * gesture exit -it keeps the swipe for changing page-, so it has to
         * exist: the physical button is the other. */
        a->want_exit = true;
        return;
    }
    int next = a->page + dir;
    if (next < 0 || next >= a->prof->n_pages) {
        return;
    }
    show_page(a, next);
    aos_hal_beep(1200, 8);
}

static void touch_gesture(lv_event_t *e)
{
    rc_app_t *a = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    /* It is the simulator's path and that of slow swipes on the board; the
     * fast ones arrive through aos_ui_take_gesture(), in the timer. */
    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT:  navigate(a, +1); break;
    case LV_DIR_RIGHT: navigate(a, -1); break;
    default: break;
    }
}

/* -------------------------------------------------------------------------- */
/* Loading the profile                                                         */
/* -------------------------------------------------------------------------- */

/* The screen stays lit only if there are gestures configured.
 *
 * A gesture cannot be recognised with the app closed, and the runtime closes
 * the app when the screen dims: without this, the remote stops listening
 * fifteen seconds after you last touch it, which is exactly when you go to use
 * a gesture. In exchange it draws power, so the profile decides and not us:
 * with no gestures, the screen switches off as in any other app.
 *
 * desc.flags can be touched from here because 'self' IS the copy the runtime
 * keeps in its table and consults on every tick, not a duplicate. */
static void aplicar_keep_awake(rc_app_t *a)
{
    if (!s_self) {
        return;
    }
    if (a->prof && a->prof->gcfg.enabled) {
        s_self->desc.flags |= AOS_APP_FLAG_KEEP_AWAKE;
    } else {
        s_self->desc.flags &= ~(uint32_t)AOS_APP_FLAG_KEEP_AWAKE;
    }
}

static void profile_path(char *out, int len)
{
    snprintf(out, len, "%s/remoto.json", aos_hal_path_data());
}

static uint8_t gesture_mask(const rc_profile_t *p)
{
    uint8_t m = 0;
    for (int g = 0; g < RC_G_COUNT; g++) {
        if (p->gest[g].type != RC_ACT_NONE) {
            m |= RC_G_BIT(g);
        }
    }
    return m;
}

static void load_profile(rc_app_t *a)
{
    if (a->prof) {
        rc_free(a->prof);
        a->prof = NULL;
    }
    free(a->tpl);
    a->tpl = NULL;

    char path[160];
    profile_path(path, sizeof(path));

    const char *demo = getenv("REMOTO_PERFIL");
    if (demo && demo[0]) {
        snprintf(path, sizeof(path), "%s", demo);
    }

    a->prof = rc_load(path, a->err, sizeof(a->err));
    a->gen  = 0;
    aos_hal_pref_get_i32(RC_KEY_GEN, &a->gen);

    if (!a->prof) {
        aos_hal_log("remoto", "no profile: %s", a->err);
        return;
    }
    a->tpl = rc_build_template(a->prof);
    rc_tilt_reset(&a->tilt, &a->prof->gcfg, gesture_mask(a->prof));
    a->page = 0;
    aos_hal_log("remoto", "profile: %d pages, %d states",
                a->prof->n_pages, a->prof->n_states);
}

/* What is shown: the help screen or the remote. */
static void refresh_mode(rc_app_t *a)
{
    bool ready = a->prof && a->ha.configured;

    if (!ready) {
        lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(a->dialbox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->setup, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(a->title, _("Remoto"));

        char msg[420];
        const char *ip = aos_hal_net_ip();
        snprintf(msg, sizeof(msg),
                 _("Todavia no esta configurado.\n\n"
                   "Abri desde la compu\n\n"
                   "  http://%s/remoto\n\n"
                   "y arma ahi las paginas, los botones y los gestos.\n\n"
                   "%s%s\n"
                   "%s"),
                 (ip && ip[0]) ? ip : _("<ip de la placa>"),
                 a->prof ? "" : _("Perfil: "),
                 a->prof ? "" : a->err,
                 a->ha.configured ? "" : a->ha.last);
        lv_label_set_text(a->setup_text, msg);
        refresh_dots(a);
        return;
    }

    lv_obj_add_flag(a->setup, LV_OBJ_FLAG_HIDDEN);
    show_page(a, a->page);
}

/* -------------------------------------------------------------------------- */
/* The app's clock                                                             */
/* -------------------------------------------------------------------------- */

static void repaint_states(rc_app_t *a)
{
    const rc_page_t *pg = cur_page(a);
    if (!pg) {
        return;
    }
    if (pg->kind == RC_PAGE_DIAL) {
        if (!a->dial_active) {
            a->dial_v    = dial_from_state(a, &pg->dial);
            a->dial_sent = a->dial_v;
            dial_paint(a);
        }
        return;
    }
    layout_grid(a);
}

/* --------------------------------------------------------------------------
 * Accelerometer diagnostics
 *
 * It is the tool for choosing the thresholds, and it has to be ON THE BOARD:
 * the simulator's numbers come from a formula, the board's from a sensor
 * hanging off a shared bus and moving with the hand holding it. You tilt the
 * board while watching the screen until the gesture's name appears, and the
 * shake figure at the top says where to put the threshold: shake it, read the
 * peak, set something below it.
 *
 * It also prints raw ax, ay and az, which is the only thing you can diagnose a
 * reversed axis with. Never "fix" that line.
 * -------------------------------------------------------------------------- */

static void diag_refresh(rc_app_t *a)
{
    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        lv_label_set_text(a->diag_text, _("el acelerometro no contesta"));
        return;
    }
    uint32_t t = now_ms();
    rc_tilt_feed(&a->tilt, imu.ax, imu.ay, imu.az, t);

    char buf[520];
    char last[64];
    if (a->last_gesture_at) {
        snprintf(last, sizeof(last), _("%s  hace %u s"),
                 rc_gesture_name(a->last_gesture),
                 (unsigned)((t - a->last_gesture_at) / 1000));
    } else {
        snprintf(last, sizeof(last), "%s", _("todavia ninguno"));
    }

    snprintf(buf, sizeof(buf),
             _("acel  %+.2f  %+.2f  %+.2f g\n"
               "modulo %.2f g   %s\n"
               "\n"
               "giro     %+6.1f   %s\n"
               "cabeceo  %+6.1f   %s\n"
               "reposo   %+6.1f  %+6.1f\n"
               "\n"
               "sacudida %5d  de %d\n"
               "umbral inclinacion %d grados\n"
               "tiempo muerto %d ms\n"
               "\n"
               "ultimo gesto: %s\n"
               "\n"
               "inclinala hasta que aparezca el gesto.\n"
               "manten el titulo para volver."),
             (double)imu.ax, (double)imu.ay, (double)imu.az,
             (double)sqrtf(imu.ax * imu.ax + imu.ay * imu.ay + imu.az * imu.az),
             a->tilt.stable ? _("quieta") : _("en movimiento"),
             (double)a->tilt.roll,  a->tilt.roll_ok  ? _("sirve") : _("de canto no"),
             (double)a->tilt.pitch, a->tilt.pitch_ok ? _("sirve") : _("de canto no"),
             (double)a->tilt.rest_roll, (double)a->tilt.rest_pitch,
             (int)a->tilt.jerk, a->tilt.cfg.shake_mg,
             a->tilt.cfg.tilt_deg, a->tilt.cfg.cool_ms,
             last);
    lv_label_set_text(a->diag_text, buf);
}

static void title_event(lv_event_t *e)
{
    rc_app_t *a = lv_event_get_user_data(e);
    a->diag_on = !a->diag_on;

    if (a->diag_on) {
        lv_obj_add_flag(a->grid, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(a->dialbox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(a->setup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->diag, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(a->title, _("Sensor"));
        /* With the app in diagnostics mode the gestures fire nothing, but the
         * recogniser has to go on running: it is what is being watched. */
        if (a->prof) {
            rc_tilt_reset(&a->tilt, &a->prof->gcfg, RC_G_ALL);
        }
        diag_refresh(a);
    } else {
        lv_obj_add_flag(a->diag, LV_OBJ_FLAG_HIDDEN);
        if (a->prof) {
            rc_tilt_reset(&a->tilt, &a->prof->gcfg, gesture_mask(a->prof));
        }
        refresh_mode(a);
    }
    aos_hal_beep(1000, 15);
}

static void tick_cb(lv_timer_t *timer)
{
    rc_app_t *a = lv_timer_get_user_data(timer);
    uint32_t t = now_ms();

    /* Leaving is deferred: aos_ui_back() destroys the app, and calling it from
     * an event's callback is destroying it while it runs. */
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;                     /* after this the context no longer exists */
    }

    /* --- swipes LVGL did not see --- */
    switch ((aos_touch_gesture_t)aos_ui_take_gesture()) {
    case AOS_TOUCH_GESTURE_LEFT:  navigate(a, +1); break;
    case AOS_TOUCH_GESTURE_RIGHT: navigate(a, -1); break;
    default: break;
    }
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    /* --- the configuration changed from the portal --- */
    if (a->want_reload) {
        a->want_reload = false;
        rc_ha_abort(&a->ha);
        rc_ha_load(&a->ha);
        load_profile(a);
        aplicar_keep_awake(a);
        refresh_mode(a);
        say(a, _("configuracion recargada"));
        return;
    }
    int32_t gen = 0;
    if (aos_hal_pref_get_i32(RC_KEY_GEN, &gen) && gen != a->gen) {
        /* Re-reading the preference now and then and comparing is crude and it
         * is the right thing: there is no way for the firmware to notify a
         * .so, and when the app is not loaded there is nobody to notify. */
        a->want_reload = true;
        return;
    }

    if (a->diag_on) {
        diag_refresh(a);
        return;                         /* no network and no gestures meanwhile */
    }

    if (!a->prof || !a->ha.configured) {
        if (a->setup_text && (t / 1000) % 5 == 0) {
            refresh_mode(a);            /* the IP may arrive later */
        }
        return;
    }

    /* --- accelerometer --- */
    aos_imu_t imu;
    if (aos_hal_imu_read(&imu)) {
        if (a->finger_down) {
            /* With a finger down, moving the board is holding it. */
            rc_tilt_suspend(&a->tilt, t);
        } else {
            int g = rc_tilt_feed(&a->tilt, imu.ax, imu.ay, imu.az, t);
            if (g >= 0) {
                a->last_gesture    = g;
                a->last_gesture_at = t;
            }
            if (g >= 0 && a->prof->gest[g].type != RC_ACT_NONE) {
                char origen[28];
                snprintf(origen, sizeof(origen), "%s: ", rc_gesture_name(g));
                aos_hal_activity();
                aos_ui_toast(rc_gesture_name(g), 900);
                run_action(a, &a->prof->gest[g], origen);
            }
        }

        if (a->dial_active) {
            const rc_page_t *pg = cur_page(a);
            if (pg && rc_roll_usable(imu.ax, imu.ay)) {
                float turned = rc_dial_turn(&a->dial_turn, &a->dial_prev_roll,
                                            rc_roll_deg(imu.ax, imu.ay));
                int v = rc_dial_value(a->dial_v0, turned, pg->dial.span_deg,
                                      pg->dial.min, pg->dial.max,
                                      pg->dial.invert);
                if (v != a->dial_v) {
                    a->dial_v = v;
                    dial_paint(a);
                }
                if (pg->dial.live_ms && v != a->dial_sent &&
                    t - a->dial_last_send >= pg->dial.live_ms) {
                    dial_send(a, false);
                }
                aos_hal_activity();
            }
        }
    }

    /* --- network --- */
    if (rc_ha_pump(&a->ha, a->prof)) {
        repaint_states(a);
    }
    if (a->ha.status == RC_HA_OK || a->ha.status == RC_HA_ERROR) {
        say(a, a->ha.last);
        a->ha.status = RC_HA_IDLE;
    }

    uint32_t due = a->ha.next_poll_ms ? a->ha.next_poll_ms
                                      : a->last_poll_ms + (uint32_t)a->prof->poll_s * 1000;
    if (a->prof->poll_s && a->tpl && (int32_t)(t - due) >= 0) {
        if (rc_ha_poll(&a->ha, a->tpl)) {
            a->last_poll_ms  = t;
            a->ha.next_poll_ms = 0;
        }
    }

    /* --- the bottom line, when there is nothing to report --- */
    if ((int32_t)(t - a->note_until) >= 0) {
        const rc_page_t *pg = cur_page(a);
        char line[64];
        if (aos_hal_net_state() != AOS_NET_CONNECTED) {
            snprintf(line, sizeof(line), "%s", _("sin wifi"));
        } else if (!a->prof->st_valid && a->tpl) {
            snprintf(line, sizeof(line), "%s", _("leyendo estados..."));
        } else {
            snprintf(line, sizeof(line), "%d/%d   %s", a->page + 1,
                     a->prof->n_pages, pg && pg->name[0] ? pg->name : "");
        }
        lv_label_set_text(a->status, line);
        a->note_until = t + NOTE_MS;
    }
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */
/* -------------------------------------------------------------------------- */

static lv_obj_t *make_cell(rc_app_t *a, lv_obj_t *parent, int i)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_30, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_opa(c, LV_OPA_60, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    /* Only the codes that are used, and not LV_EVENT_ALL. With ALL, the
     * LV_EVENT_DELETE LVGL sends when deleting the object also comes in here,
     * and the runtime deletes the objects AFTER calling destroy(): the
     * callback ran with the context already freed. AddressSanitizer found it
     * in the simulator; on the board it would have been an unexplained
     * restart. */
    static const lv_event_code_t codigos[] = {
        LV_EVENT_PRESSED, LV_EVENT_LONG_PRESSED, LV_EVENT_CLICKED,
        LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST,
    };
    for (unsigned k = 0; k < sizeof(codigos) / sizeof(codigos[0]); k++) {
        lv_obj_add_event_cb(c, cell_event, codigos[k], a);
    }

    a->cell_label[i] = aos_label(c, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_text_align(a->cell_label[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(a->cell_label[i], LV_LABEL_LONG_DOT);
    lv_obj_center(a->cell_label[i]);

    a->cell_value[i] = aos_label(c, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_text_align(a->cell_value[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(a->cell_value[i], LV_OPA_80, 0);
    lv_obj_add_flag(a->cell_value[i], LV_OBJ_FLAG_HIDDEN);

    /* Every lv_obj is born clickable in LVGL 9: without this the two labels
     * eat the touch meant for the button containing them. */
    lv_obj_remove_flag(a->cell_label[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->cell_value[i], LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static lv_obj_t *make_step_button(rc_app_t *a, lv_obj_t *parent,
                                  const char *text, int delta, int32_t x)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 64, 44);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_bg_color(b, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -6);
    lv_obj_set_user_data(b, (void *)(intptr_t)delta);
    lv_obj_add_event_cb(b, dial_step, LV_EVENT_CLICKED, a);

    lv_obj_t *l = aos_label(b, text, aos_font_title, AOS_C_TEXT);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void build_dial(rc_app_t *a, lv_obj_t *parent)
{
    a->dialbox = lv_obj_create(parent);
    lv_obj_remove_style_all(a->dialbox);
    lv_obj_set_size(a->dialbox, 368, BODY_H);
    lv_obj_set_pos(a->dialbox, 0, BODY_Y);
    lv_obj_remove_flag(a->dialbox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->dialbox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->dialbox, LV_OBJ_FLAG_HIDDEN);

    a->arc = lv_arc_create(a->dialbox);
    lv_obj_set_size(a->arc, 216, 216);
    lv_obj_align(a->arc, LV_ALIGN_TOP_MID, 0, 0);
    lv_arc_set_rotation(a->arc, 135);
    lv_arc_set_bg_angles(a->arc, 0, 270);
    lv_obj_remove_style(a->arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a->arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a->arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a->arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a->arc, AOS_C_ORANGE, LV_PART_INDICATOR);
    /* The arc is an indicator, not a control: what moves the value is the
     * wrist. Two ways of dragging over the same widget fight each other. */
    lv_obj_remove_flag(a->arc, LV_OBJ_FLAG_CLICKABLE);

    a->knob = lv_obj_create(a->dialbox);
    lv_obj_remove_style_all(a->knob);
    lv_obj_set_size(a->knob, 138, 138);
    lv_obj_set_style_radius(a->knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(a->knob, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(a->knob, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(a->knob, AOS_C_ORANGE, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(a->knob, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_align(a->knob, LV_ALIGN_TOP_MID, 0, 39);   /* concentric with the arc */
    lv_obj_remove_flag(a->knob, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->knob, knob_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->knob, knob_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->knob, knob_event, LV_EVENT_PRESS_LOST, a);

    a->knob_label = aos_label(a->knob, "0", aos_font_huge, AOS_C_TEXT);
    lv_obj_center(a->knob_label);
    lv_obj_remove_flag(a->knob_label, LV_OBJ_FLAG_CLICKABLE);

    a->dial_name = aos_label_boxed(a->dialbox, "", aos_font_small, AOS_C_DIM,
                                   340, 22);
    lv_obj_align(a->dial_name, LV_ALIGN_TOP_MID, 0, 222);
    lv_obj_remove_flag(a->dial_name, LV_OBJ_FLAG_CLICKABLE);

    a->dial_hint = aos_label_boxed(a->dialbox, "", aos_font_small, AOS_C_DIM,
                                   340, 22);
    lv_obj_align(a->dial_hint, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_remove_flag(a->dial_hint, LV_OBJ_FLAG_CLICKABLE);

    make_step_button(a, a->dialbox, "-", -1, -120);
    make_step_button(a, a->dialbox, "+", +1, +120);
}

static void *rc_create(aos_app_t *self, lv_obj_t *root)
{
    s_self = self;

    rc_app_t *a = lv_malloc_zeroed(sizeof(rc_app_t));
    if (!a) {
        return NULL;
    }
    a->root    = root;
    a->pressed = -1;
    a->page    = 0;

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* The gesture is listened for on the ROOT, not on a separate layer.
     *
     * LVGL does not send LV_EVENT_GESTURE to the object under the finger: it
     * climbs through the parents as long as it finds LV_OBJ_FLAG_GESTURE_BUBBLE,
     * which every lv_obj carries by default, and delivers it to the FIRST one
     * that does not have it (lv_indev.c, indev_gesture). A transparent layer at
     * the bottom of the z-order is a sibling of the grid, not an ancestor, so a
     * swipe starting on a button -that is, nearly all of them- never reached
     * it.
     *
     * Taking the flag off the root is what makes the climb end here; without
     * that the gesture carries straight on to the screen. */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, touch_gesture, LV_EVENT_GESTURE, a);

    a->title = aos_label_boxed(root, _("Remoto"), aos_font_title, AOS_C_TEXT,
                               368, TITLE_H);
    lv_obj_set_pos(a->title, 0, TITLE_Y);
    lv_obj_add_flag(a->title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(a->title, title_event, LV_EVENT_LONG_PRESSED, a);

    a->grid = lv_obj_create(root);
    lv_obj_remove_style_all(a->grid);
    lv_obj_set_size(a->grid, 368, BODY_H);
    lv_obj_set_pos(a->grid, 0, BODY_Y);
    lv_obj_remove_flag(a->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->grid, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < RC_MAX_BUTTONS; i++) {
        a->cell[i] = make_cell(a, a->grid, i);
        lv_obj_add_flag(a->cell[i], LV_OBJ_FLAG_HIDDEN);
    }

    build_dial(a, root);

    a->setup = lv_obj_create(root);
    lv_obj_remove_style_all(a->setup);
    lv_obj_set_size(a->setup, 368, BODY_H);
    lv_obj_set_pos(a->setup, 0, BODY_Y);
    lv_obj_remove_flag(a->setup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->setup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->setup, LV_OBJ_FLAG_HIDDEN);
    a->setup_text = aos_label(a->setup, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_width(a->setup_text, 330);
    lv_obj_set_style_text_align(a->setup_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a->setup_text, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_remove_flag(a->setup_text, LV_OBJ_FLAG_CLICKABLE);

    a->diag = lv_obj_create(root);
    lv_obj_remove_style_all(a->diag);
    lv_obj_set_size(a->diag, 368, BODY_H + 20);
    lv_obj_set_pos(a->diag, 0, BODY_Y - 10);
    lv_obj_remove_flag(a->diag, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->diag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->diag, LV_OBJ_FLAG_HIDDEN);
    a->diag_text = aos_label(a->diag, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(a->diag_text, 344);
    lv_obj_set_style_text_align(a->diag_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(a->diag_text, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(a->diag_text, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < RC_MAX_PAGES; i++) {
        a->dots[i] = lv_obj_create(root);
        lv_obj_remove_style_all(a->dots[i]);
        lv_obj_set_size(a->dots[i], 7, 7);
        lv_obj_set_style_radius(a->dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(a->dots[i], AOS_C_TEXT, 0);
        lv_obj_set_style_bg_opa(a->dots[i], LV_OPA_30, 0);
        lv_obj_add_flag(a->dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(a->dots[i], LV_OBJ_FLAG_CLICKABLE);
    }

    a->status = aos_label_boxed(root, "", aos_font_small, AOS_C_DIM, 368, 22);
    lv_obj_set_pos(a->status, 0, STATUS_Y);
    lv_obj_remove_flag(a->status, LV_OBJ_FLAG_CLICKABLE);

    rc_ha_load(&a->ha);
    load_profile(a);

    aplicar_keep_awake(a);
    refresh_mode(a);
    a->last_poll_ms = now_ms();
    a->note_until   = now_ms();
    /* The first refresh goes out immediately and not one period later:
     * otherwise the app opens with every button off and takes five seconds to
     * admit that the living-room light was on. */
    a->ha.next_poll_ms = a->last_poll_ms + 200;
    a->timer = lv_timer_create(tick_cb, TICK_MS, a);
    return a;
}

static void rc_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    rc_app_t *a = inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
        a->timer = NULL;
    }

    /* The objects are deleted HERE and not left to the runtime.
     *
     * close_current() in aos_ui.c calls destroy() and ONLY THEN deletes the
     * root, so any event LVGL sends during that deletion -DELETE, and
     * PRESS_LOST if a finger was down- reaches a callback whose context no
     * longer exists. By deleting them with the context still alive, and with
     * 'closing' set so the callbacks do nothing useful, no window is left. The
     * empty root the runtime inherits it deletes all the same. */
    a->closing = true;
    if (a->root) {
        lv_obj_clean(a->root);
    }

    rc_ha_abort(&a->ha);        /* including with the request in flight */
    rc_free(a->prof);
    free(a->tpl);
    lv_free(a);
}

static bool rc_back(aos_app_t *self, void *inst)
{
    (void)self;
    rc_app_t *a = inst;
    if (a && a->prof && a->page > 0) {
        show_page(a, a->page - 1);
        return true;
    }
    return false;               /* from the first page, leave */
}

static bool rc_init(aos_app_t *app)
{
    app->desc.id       = APP_ID;
    app->desc.name     = "Remoto";
    app->desc.icon     = LV_SYMBOL_POWER;
    app->desc.icon_vec = AOS_ICON_REMOTE;
    app->desc.color_a  = 0x30D158;
    app->desc.color_b  = 0x0A7A32;
    app->desc.order    = 45;
    /* NO_SWIPE because the horizontal swipe is how pages are changed. In
     * exchange there are two exits and both are used without thinking: swiping
     * right on the first page, and the physical button. */
    app->desc.flags    = AOS_APP_FLAG_NO_SWIPE;

    app->create  = rc_create;
    app->destroy = rc_destroy;
    app->back    = rc_back;
    return true;
}

AOS_APP_ENTRY(rc_init);
