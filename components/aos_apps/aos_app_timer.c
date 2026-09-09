/* AmoledOS - Timer. Countdown with an arc, stays alive in the background. */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>

typedef struct {
    lv_obj_t   *presets;
    lv_obj_t   *running_view;
    lv_obj_t   *arc;
    lv_obj_t   *label;
    lv_obj_t   *action_btn;
    lv_obj_t   *action_label;
    lv_timer_t *timer;

    bool     active;
    bool     paused;
    uint32_t total_s;
    uint32_t remaining_ms;
    uint64_t last_ms;
    int      beeps_left;
} timer_app_t;

static timer_app_t s_timer;

static const uint32_t PRESETS_S[] = { 60, 180, 300, 600, 900, 1800 };

static void show_view(bool running)
{
    if (!s_timer.presets) {
        return;
    }
    if (running) {
        lv_obj_add_flag(s_timer.presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_timer.running_view, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_timer.presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_timer.running_view, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void)
{
    if (!s_timer.label) {
        return;
    }
    /* uint32_t is 'unsigned long' on xtensa: we cast so the format strings are
     * portable between the simulator and the board */
    unsigned seconds = (unsigned)((s_timer.remaining_ms + 999) / 1000);
    char buf[24];
    if (seconds >= 3600) {
        snprintf(buf, sizeof(buf), "%u:%02u:%02u", seconds / 3600,
                 (seconds / 60) % 60, seconds % 60);
    } else {
        snprintf(buf, sizeof(buf), "%02u:%02u", seconds / 60, seconds % 60);
    }
    lv_label_set_text(s_timer.label, buf);

    int32_t value = s_timer.total_s ?
        (int32_t)(s_timer.remaining_ms / 10 / s_timer.total_s) : 0;
    lv_arc_set_value(s_timer.arc, value);
}

/* Advances the timer. Called both from the UI's lv_timer and from the
 * runtime's tick when the app is in the background. */
static void advance(void)
{
    if (!s_timer.active || s_timer.paused) {
        s_timer.last_ms = aos_hal_uptime_ms();
        return;
    }

    uint64_t now = aos_hal_uptime_ms();
    uint64_t delta = now - s_timer.last_ms;
    s_timer.last_ms = now;

    if (delta >= s_timer.remaining_ms) {
        s_timer.remaining_ms = 0;
        s_timer.active = false;
        s_timer.beeps_left = 6;
        aos_ui_toast(_("Se acabo el tiempo"), 3000);
    } else {
        s_timer.remaining_ms -= (uint32_t)delta;
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    advance();
    refresh();
}

static void preset_cb(lv_event_t *event)
{
    uint32_t seconds = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    s_timer.total_s      = seconds;
    s_timer.remaining_ms = seconds * 1000;
    s_timer.active       = true;
    s_timer.paused       = false;
    s_timer.last_ms      = aos_hal_uptime_ms();
    aos_hal_beep(1400, 30);
    show_view(true);
    refresh();
}

static void action_cb(lv_event_t *event)
{
    (void)event;
    if (s_timer.active) {
        s_timer.paused = !s_timer.paused;
        s_timer.last_ms = aos_hal_uptime_ms();
        lv_label_set_text(s_timer.action_label,
                          s_timer.paused ? _("Seguir") : _("Pausa"));
    } else {
        /* finished -> back to the presets */
        s_timer.total_s = 0;
        show_view(false);
    }
}

static void cancel_cb(lv_event_t *event)
{
    (void)event;
    s_timer.active = false;
    s_timer.paused = false;
    s_timer.remaining_ms = 0;
    s_timer.total_s = 0;
    show_view(false);
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    /* --- presets view --- */
    s_timer.presets = lv_obj_create(page);
    lv_obj_remove_style_all(s_timer.presets);
    lv_obj_set_size(s_timer.presets, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_timer.presets, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_timer.presets, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_timer.presets, 16, 0);
    lv_obj_set_style_pad_column(s_timer.presets, 16, 0);

    for (unsigned i = 0; i < sizeof(PRESETS_S) / sizeof(PRESETS_S[0]); i++) {
        char label[16];
        unsigned seconds = (unsigned)PRESETS_S[i];
        if (seconds < 3600) {
            snprintf(label, sizeof(label), _("%u min"), seconds / 60);
        } else {
            snprintf(label, sizeof(label), _("%u h"), seconds / 3600);
        }
        lv_obj_t *btn = aos_button(s_timer.presets, label, AOS_C_CARD2,
                                   preset_cb, (void *)(uintptr_t)seconds);
        lv_obj_set_size(btn, 140, 78);
    }

    /* --- running view --- */
    s_timer.running_view = lv_obj_create(page);
    lv_obj_remove_style_all(s_timer.running_view);
    lv_obj_set_size(s_timer.running_view, lv_pct(100), lv_pct(100));

    s_timer.arc = lv_arc_create(s_timer.running_view);
    lv_obj_remove_style(s_timer.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_timer.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_timer.arc, 250, 250);
    lv_arc_set_rotation(s_timer.arc, 270);
    lv_arc_set_bg_angles(s_timer.arc, 0, 360);
    lv_arc_set_range(s_timer.arc, 0, 100);
    lv_obj_set_style_arc_width(s_timer.arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_timer.arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_timer.arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_timer.arc, AOS_C_ORANGE, LV_PART_INDICATOR);
    lv_obj_align(s_timer.arc, LV_ALIGN_TOP_MID, 0, 24);

    s_timer.label = aos_label_boxed(s_timer.running_view, "00:00", aos_font_huge,
                                    AOS_C_TEXT, 240, 60);
    lv_obj_align_to(s_timer.label, s_timer.arc, LV_ALIGN_CENTER, 0, 0);

    s_timer.action_btn = aos_button(s_timer.running_view, _("Pausa"), AOS_C_ORANGE,
                                    action_cb, NULL);
    lv_obj_set_size(s_timer.action_btn, 130, 62);
    lv_obj_align(s_timer.action_btn, LV_ALIGN_BOTTOM_LEFT, 26, -26);
    s_timer.action_label = lv_obj_get_child(s_timer.action_btn, 0);

    lv_obj_t *cancel = aos_button(s_timer.running_view, _("Cancelar"), AOS_C_CARD2,
                                  cancel_cb, NULL);
    lv_obj_set_size(cancel, 130, 62);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_RIGHT, -26, -26);

    show_view(s_timer.active || s_timer.remaining_ms > 0);
    s_timer.timer = lv_timer_create(tick_cb, 100, NULL);
    refresh();
    return &s_timer;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_timer.timer) {
        lv_timer_delete(s_timer.timer);
        s_timer.timer = NULL;
    }
    s_timer.presets = NULL;
    s_timer.running_view = NULL;
    s_timer.label = NULL;
    s_timer.arc = NULL;
    s_timer.action_btn = NULL;
    s_timer.action_label = NULL;
}

/* In the background the lv_timer is paused: the runtime gives us 200 ms ticks
 * to go on counting down and to sound the alarm. */
static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    advance();
    if (s_timer.beeps_left > 0) {
        aos_hal_beep(2000, 120);
        s_timer.beeps_left--;
    }
}

static void hide(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_timer.timer) {
        lv_timer_pause(s_timer.timer);
    }
}

static void show(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_timer.timer) {
        lv_timer_resume(s_timer.timer);
    }
    show_view(s_timer.active || s_timer.remaining_ms > 0);
    refresh();
}

void aos_app_timer_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.timer",
            .name     = "Temporizador",
            .icon_vec = AOS_ICON_TIMER,
            .color_a  = 0xFF9F0A,
            .color_b  = 0xE07000,
            .flags    = AOS_APP_FLAG_BACKGROUND,
            .order    = 30,
        },
        .create  = create,
        .destroy = destroy,
        .show    = show,
        .hide    = hide,
        .tick    = tick,
    };
}
