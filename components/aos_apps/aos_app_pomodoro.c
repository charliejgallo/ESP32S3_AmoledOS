/*
 * AmoledOS - Pomodoro
 *
 * What the Timer does not do: chaining. Focus, short break, and every four
 * focuses a long break, moving from one phase to the next on its own and
 * announcing each change with a different set of beeps. Without that it is a
 * timer under another name.
 *
 * The count lives in static state and not in the UI, because the app runs in
 * the background (AOS_APP_FLAG_BACKGROUND): on leaving, the runtime keeps it
 * alive and goes on giving it ticks, but destroys its LVGL objects. create()
 * rebuilds the screen from the state, never the other way round.
 *
 * Time is counted down by uptime difference, like the Timer: the runtime's
 * ticks are not exact and a tick counter would run slow.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>

/* Phases of the cycle. The order matters: it is the one next_phase() walks. */
typedef enum {
    PH_FOCUS = 0,
    PH_SHORT,
    PH_LONG,
} phase_t;

/* The three rhythms that can be chosen, in minutes: focus, short, long.
 * Cirillo's classic is the first; the second is the long rhythm people who
 * program tend to prefer; the third, for getting started when you do not feel
 * like it. */
typedef struct {
    const char *name;
    uint16_t    focus_min;
    uint16_t    short_min;
    uint16_t    long_min;
} preset_t;

static const preset_t PRESETS[] = {
    { "25 / 5",  25, 5, 15 },
    { "50 / 10", 50, 10, 20 },
    { "15 / 3",  15, 3, 10 },
};
#define PRESET_COUNT    ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))

/* Focuses to complete before the long break. */
#define SET_LEN         4

typedef struct {
    /* --- state, survives destroy() --- */
    phase_t  phase;
    bool     active;            /* a cycle is under way (even if paused) */
    bool     paused;
    int      preset;
    int      in_set;            /* completed focuses of the current set, 0..SET_LEN */
    uint32_t total_ms;          /* duration of the current phase */
    uint32_t left_ms;
    uint64_t last_ms;
    int      beeps_left;
    int      beep_freq;
    int32_t  today_key;         /* yyyymmdd of the "today" counter */
    int32_t  today_done;
    bool     loaded;            /* the preferences have been read */

    /* --- UI, rebuilt on every create() --- */
    lv_obj_t *page;
    lv_obj_t *idle_view;
    lv_obj_t *run_view;
    lv_obj_t *chip[PRESET_COUNT];
    lv_obj_t *arc;
    lv_obj_t *lbl_time;
    lv_obj_t *lbl_phase;
    lv_obj_t *dot[SET_LEN];
    lv_obj_t *lbl_today;
    lv_obj_t *lbl_today_idle;
    lv_obj_t *btn_pause;
    lv_obj_t *lbl_pause;
    lv_timer_t *timer;
} pomo_t;

static pomo_t s_pomo;

/* -------------------------------------------------------------------------- */
/* State                                                                       */

static lv_color_t phase_color(phase_t p)
{
    switch (p) {
    case PH_FOCUS: return AOS_C_RED;
    case PH_SHORT: return AOS_C_GREEN;
    default:       return AOS_C_TEAL;
    }
}

static const char *phase_name(phase_t p)
{
    switch (p) {
    case PH_FOCUS: return _("ENFOQUE");
    case PH_SHORT: return _("DESCANSO");
    default:       return _("DESCANSO LARGO");
    }
}

static uint32_t phase_minutes(phase_t p)
{
    const preset_t *pr = &PRESETS[s_pomo.preset];
    switch (p) {
    case PH_FOCUS: return pr->focus_min;
    case PH_SHORT: return pr->short_min;
    default:       return pr->long_min;
    }
}

/* yyyymmdd, to know whether today's counter is still today's. */
static int32_t day_key(void)
{
    struct tm now;
    aos_hal_time_now(&now);
    return (int32_t)((now.tm_year + 1900) * 10000 + (now.tm_mon + 1) * 100 +
                     now.tm_mday);
}

static void load_prefs(void)
{
    if (s_pomo.loaded) {
        return;
    }
    s_pomo.loaded = true;

    int32_t v = 0;
    if (aos_hal_pref_get_i32("pomo_preset", &v) && v >= 0 && v < PRESET_COUNT) {
        s_pomo.preset = (int)v;
    }
    if (aos_hal_pref_get_i32("pomo_day", &s_pomo.today_key) &&
        aos_hal_pref_get_i32("pomo_done", &s_pomo.today_done)) {
        if (s_pomo.today_key != day_key()) {
            s_pomo.today_key  = 0;
            s_pomo.today_done = 0;
        }
    }
}

static void count_one_today(void)
{
    int32_t today = day_key();
    if (today != s_pomo.today_key) {
        s_pomo.today_key  = today;
        s_pomo.today_done = 0;
    }
    s_pomo.today_done++;
    aos_hal_pref_set_i32("pomo_day", s_pomo.today_key);
    aos_hal_pref_set_i32("pomo_done", s_pomo.today_done);
}

static void start_phase(phase_t p)
{
    s_pomo.phase    = p;
    s_pomo.total_ms = phase_minutes(p) * 60u * 1000u;
    s_pomo.left_ms  = s_pomo.total_ms;
    s_pomo.last_ms  = aos_hal_uptime_ms();
    s_pomo.active   = true;
    s_pomo.paused   = false;
}

/* What comes after the current phase, counting the set. */
static void next_phase(bool completed)
{
    if (s_pomo.phase == PH_FOCUS) {
        if (completed) {
            s_pomo.in_set++;
            count_one_today();
        }
        start_phase(s_pomo.in_set >= SET_LEN ? PH_LONG : PH_SHORT);
    } else {
        if (s_pomo.phase == PH_LONG) {
            s_pomo.in_set = 0;
        }
        start_phase(PH_FOCUS);
    }
}

/* -------------------------------------------------------------------------- */
/* UI                                                                          */

static void show_view(bool running)
{
    if (!s_pomo.idle_view) {
        return;
    }
    if (running) {
        lv_obj_add_flag(s_pomo.idle_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pomo.run_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_pomo.idle_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pomo.run_view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_chips(void)
{
    for (int i = 0; i < PRESET_COUNT; i++) {
        if (!s_pomo.chip[i]) {
            continue;
        }
        lv_obj_set_style_bg_color(s_pomo.chip[i],
                                  i == s_pomo.preset ? AOS_C_RED : AOS_C_CARD2, 0);
    }
}

static void refresh_today(void)
{
    char buf[40];
    /* GCC counts up to 11 characters per %d even when the number is small, and
     * the IDF's warnings are errors: the box is deliberately roomy. */
    snprintf(buf, sizeof(buf), _("hoy: %d"), (int)s_pomo.today_done);
    if (s_pomo.lbl_today) {
        lv_label_set_text(s_pomo.lbl_today, buf);
    }
    if (s_pomo.lbl_today_idle) {
        lv_label_set_text(s_pomo.lbl_today_idle, buf);
    }
}

static void refresh(void)
{
    if (!s_pomo.lbl_time) {
        return;
    }

    unsigned seconds = (unsigned)((s_pomo.left_ms + 999) / 1000);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02u:%02u", seconds / 60, seconds % 60);
    lv_label_set_text(s_pomo.lbl_time, buf);

    int32_t value = s_pomo.total_ms ?
        (int32_t)(s_pomo.left_ms / 10 / (s_pomo.total_ms / 1000)) : 0;
    lv_arc_set_value(s_pomo.arc, value);

    lv_color_t color = phase_color(s_pomo.phase);
    lv_obj_set_style_arc_color(s_pomo.arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(s_pomo.lbl_phase, color, 0);
    lv_label_set_text(s_pomo.lbl_phase,
                      s_pomo.paused ? _("EN PAUSA") : phase_name(s_pomo.phase));

    for (int i = 0; i < SET_LEN; i++) {
        if (!s_pomo.dot[i]) {
            continue;
        }
        bool done = i < s_pomo.in_set;
        lv_obj_set_style_bg_color(s_pomo.dot[i], done ? AOS_C_RED : AOS_C_CARD2, 0);
    }

    lv_label_set_text(s_pomo.lbl_pause, s_pomo.paused ? _("Seguir") : _("Pausa"));
    refresh_today();
}

/* -------------------------------------------------------------------------- */
/* Callbacks                                                                   */

static void chip_cb(lv_event_t *event)
{
    s_pomo.preset = (int)(intptr_t)lv_event_get_user_data(event);
    aos_hal_pref_set_i32("pomo_preset", s_pomo.preset);
    refresh_chips();
    aos_hal_beep(1200, 20);
}

static void start_cb(lv_event_t *event)
{
    (void)event;
    s_pomo.in_set = 0;
    start_phase(PH_FOCUS);
    aos_hal_beep(1500, 40);
    show_view(true);
    refresh();
}

static void pause_cb(lv_event_t *event)
{
    (void)event;
    s_pomo.paused  = !s_pomo.paused;
    s_pomo.last_ms = aos_hal_uptime_ms();
    refresh();
}

/* Skipping the phase without counting it as completed: an abandoned focus does
 * not count. */
static void skip_cb(lv_event_t *event)
{
    (void)event;
    next_phase(false);
    aos_hal_beep(900, 30);
    refresh();
}

static void stop_cb(lv_event_t *event)
{
    (void)event;
    s_pomo.active     = false;
    s_pomo.paused     = false;
    s_pomo.left_ms    = 0;
    s_pomo.total_ms   = 0;
    s_pomo.beeps_left = 0;
    show_view(false);
    refresh_today();
}

/* -------------------------------------------------------------------------- */
/* Engine                                                                      */

static void advance(void)
{
    if (!s_pomo.active || s_pomo.paused) {
        s_pomo.last_ms = aos_hal_uptime_ms();
        return;
    }

    uint64_t now = aos_hal_uptime_ms();
    uint32_t delta = (uint32_t)(now - s_pomo.last_ms);   /* in 32 bits: dividing
                                                            a uint64 drags in
                                                            __udivdi3 */
    s_pomo.last_ms = now;

    if (delta < s_pomo.left_ms) {
        s_pomo.left_ms -= delta;
        return;
    }

    /* End of phase: the announcement differs depending on where we are going,
     * which is the only clue there is with the screen off. */
    bool was_focus = (s_pomo.phase == PH_FOCUS);
    next_phase(true);
    s_pomo.beeps_left = was_focus ? 4 : 2;
    s_pomo.beep_freq  = was_focus ? 2100 : 1500;
    aos_ui_toast(was_focus ? _("A descansar") : _("A enfocar"), 2500);
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    advance();
    refresh();
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static void build_idle(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    s_pomo.idle_view = v;

    lv_obj_t *title = aos_label(v, _("Pomodoro"), aos_font_title, AOS_C_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *hint = aos_label(v, _("enfoque / descanso"), aos_font_small, AOS_C_DIM);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 58);

    /* Three pills of 104 with 12 of spacing: 336, centred in 368. */
    for (int i = 0; i < PRESET_COUNT; i++) {
        lv_obj_t *chip = aos_button(v, PRESETS[i].name, AOS_C_CARD2, chip_cb,
                                    (void *)(intptr_t)i);
        lv_obj_set_size(chip, 104, 56);
        lv_obj_set_pos(chip, 16 + i * 116, 104);
        s_pomo.chip[i] = chip;
    }

    lv_obj_t *start = aos_button(v, _("Empezar"), AOS_C_RED, start_cb, NULL);
    lv_obj_set_size(start, 200, 200);
    lv_obj_align(start, LV_ALIGN_CENTER, 0, 42);

    s_pomo.lbl_today_idle = aos_label_boxed(v, _("hoy: 0"), aos_font_small,
                                            AOS_C_DIM, 200, 24);
    lv_obj_align(s_pomo.lbl_today_idle, LV_ALIGN_BOTTOM_MID, 0, -22);

    refresh_chips();
}

static void build_run(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v, LV_OBJ_FLAG_HIDDEN);
    s_pomo.run_view = v;

    /* Fixed box: the phase's name varies in length ("ENFOQUE" against
     * "DESCANSO LARGO") and a content-sized label would shift. */
    s_pomo.lbl_phase = aos_label_boxed(v, "", aos_font_body, AOS_C_RED,
                                       AOS_SCREEN_W, 26);
    lv_obj_align(s_pomo.lbl_phase, LV_ALIGN_TOP_MID, 0, 6);

    s_pomo.arc = lv_arc_create(v);
    lv_obj_remove_style(s_pomo.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_pomo.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_pomo.arc, 228, 228);
    lv_arc_set_rotation(s_pomo.arc, 270);
    lv_arc_set_bg_angles(s_pomo.arc, 0, 360);
    lv_arc_set_range(s_pomo.arc, 0, 100);
    lv_obj_set_style_arc_width(s_pomo.arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_pomo.arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_pomo.arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_align(s_pomo.arc, LV_ALIGN_TOP_MID, 0, 38);

    s_pomo.lbl_time = aos_label_boxed(v, "00:00", aos_font_huge, AOS_C_TEXT,
                                      220, 60);
    lv_obj_align_to(s_pomo.lbl_time, s_pomo.arc, LV_ALIGN_CENTER, 0, 0);

    /* The set's four focuses: four dots of 14 every 30 px. */
    for (int i = 0; i < SET_LEN; i++) {
        lv_obj_t *dot = lv_obj_create(v);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, AOS_C_CARD2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_MID, (i - (SET_LEN - 1) / 2) * 30 - 15, 280);
        s_pomo.dot[i] = dot;
    }

    s_pomo.lbl_today = aos_label_boxed(v, _("hoy: 0"), aos_font_small, AOS_C_DIM,
                                       200, 24);
    lv_obj_align(s_pomo.lbl_today, LV_ALIGN_TOP_MID, 0, 304);

    s_pomo.btn_pause = aos_button(v, _("Pausa"), AOS_C_ORANGE, pause_cb, NULL);
    lv_obj_set_size(s_pomo.btn_pause, 112, 58);
    lv_obj_align(s_pomo.btn_pause, LV_ALIGN_BOTTOM_LEFT, 12, -26);
    s_pomo.lbl_pause = lv_obj_get_child(s_pomo.btn_pause, 0);

    lv_obj_t *skip = aos_button(v, _("Saltar"), AOS_C_CARD2, skip_cb, NULL);
    lv_obj_set_size(skip, 112, 58);
    lv_obj_align(skip, LV_ALIGN_BOTTOM_MID, 0, -26);

    lv_obj_t *stop = aos_button(v, _("Parar"), AOS_C_CARD2, stop_cb, NULL);
    lv_obj_set_size(stop, 112, 58);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_RIGHT, -12, -26);
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    load_prefs();

    s_pomo.page = aos_page(root);
    build_idle(s_pomo.page);
    build_run(s_pomo.page);

    show_view(s_pomo.active);
    s_pomo.timer = lv_timer_create(tick_cb, 200, NULL);
    refresh();
    return &s_pomo;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_pomo.timer) {
        lv_timer_delete(s_pomo.timer);
        s_pomo.timer = NULL;
    }
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    /* Only the object pointers: the cycle's state has to survive, which is
     * what the app runs in the background for. */
    s_pomo.page = NULL;
    s_pomo.idle_view = NULL;
    s_pomo.run_view = NULL;
    s_pomo.arc = NULL;
    s_pomo.lbl_time = NULL;
    s_pomo.lbl_phase = NULL;
    s_pomo.lbl_today = NULL;
    s_pomo.lbl_today_idle = NULL;
    s_pomo.btn_pause = NULL;
    s_pomo.lbl_pause = NULL;
    memset(s_pomo.chip, 0, sizeof(s_pomo.chip));
    memset(s_pomo.dot, 0, sizeof(s_pomo.dot));
}

/* With the app closed the lv_timer does not run: the runtime's ticks are what
 * keep the count going and sound the phase change. */
static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    advance();
    if (s_pomo.beeps_left > 0) {
        aos_hal_beep(s_pomo.beep_freq, 130);
        s_pomo.beeps_left--;
    }
}

static void hide(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_pomo.timer) {
        lv_timer_pause(s_pomo.timer);
    }
}

static void show(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_pomo.timer) {
        lv_timer_resume(s_pomo.timer);
    }
    show_view(s_pomo.active);
    refresh();
}

void aos_app_pomodoro_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.pomodoro",
            .name     = "Pomodoro",
            .icon_vec = AOS_ICON_POMODORO,
            .color_a  = 0xFF453A,
            .color_b  = 0x992018,
            .flags    = AOS_APP_FLAG_BACKGROUND,
            .order    = 32,
        },
        .create  = create,
        .destroy = destroy,
        .show    = show,
        .hide    = hide,
        .tick    = tick,
    };
}
