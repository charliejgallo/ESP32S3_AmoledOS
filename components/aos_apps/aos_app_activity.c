/*
 * AmoledOS - Activity: today's steps against a goal, the week behind it,
 * and the raw QMI8658 reading. (The spirit level is its own app.)
 *
 * The count itself is the HAL's (aos_hal_steps_get: today, kept across
 * restarts and cut at midnight, seven days of history, the goal). This
 * screen only shows it. The goal is changed by tapping the ring: it walks
 * through a short list of common goals and is stored by the HAL.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"

#include <stdio.h>

#define STRIDE_M        0.72f       /* an average stride: the km are an estimate */
#define BAR_W           26
#define BAR_H           44
#define BAR_GAP         8
#define BARS_Y          214

static const uint32_t GOALS[] = { 4000, 6000, 8000, 10000, 12000, 15000 };

typedef struct {
    lv_obj_t   *steps;
    lv_obj_t   *goal;
    lv_obj_t   *ring;
    lv_obj_t   *km;
    lv_obj_t   *bars[AOS_STEPS_DAYS + 1];   /* six days back, then today */
    lv_obj_t   *accel;
    lv_obj_t   *gyro;
    lv_obj_t   *orient;
    lv_timer_t *timer;
    uint32_t    shown_today;
    uint32_t    shown_goal;
} activity_t;

static activity_t s_act;

static const char *orient_name(aos_orientation_t orientation)
{
    switch (orientation) {
    case AOS_ORIENT_UP:        return "vertical";
    case AOS_ORIENT_DOWN:      return "invertida";
    case AOS_ORIENT_LEFT:      return "izquierda";
    case AOS_ORIENT_RIGHT:     return "derecha";
    case AOS_ORIENT_FACE_UP:   return _("boca arriba");
    case AOS_ORIENT_FACE_DOWN: return _("boca abajo");
    default:                   return "?";
    }
}

static void show_steps(const aos_steps_info_t *info)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%u", (unsigned)info->today);
    lv_label_set_text(s_act.steps, buf);
    snprintf(buf, sizeof(buf), _("de %u"), (unsigned)info->goal);
    lv_label_set_text(s_act.goal, buf);
    lv_arc_set_value(s_act.ring, (int32_t)LV_MIN(100, (uint64_t)info->today * 100 / info->goal));
    lv_obj_set_style_arc_color(s_act.ring, info->today >= info->goal ? AOS_C_GREEN : AOS_C_PINK,
                               LV_PART_INDICATOR);

    float km = (float)info->today * STRIDE_M / 1000.0f;
    snprintf(buf, sizeof(buf), _("%.1f km"), km);
    lv_label_set_text(s_act.km, buf);

    /* The week: six days back and today, scaled to the best of them. */
    uint32_t top = info->today;
    for (int i = 0; i < AOS_STEPS_DAYS - 1; i++) {
        top = LV_MAX(top, info->history[i]);
    }
    top = LV_MAX(top, info->goal / 4);
    for (int i = 0; i < AOS_STEPS_DAYS; i++) {
        uint32_t v = i == AOS_STEPS_DAYS - 1 ? info->today : info->history[AOS_STEPS_DAYS - 2 - i];
        int32_t h = LV_MAX(2, (int32_t)((uint64_t)v * BAR_H / top));
        lv_obj_set_height(s_act.bars[i], h);
        lv_obj_set_y(s_act.bars[i], BARS_Y + BAR_H - h);     /* grows upwards */
        lv_obj_set_style_bg_color(s_act.bars[i],
                                  v >= info->goal ? AOS_C_GREEN :
                                  i == AOS_STEPS_DAYS - 1 ? AOS_C_PINK : AOS_C_DIM, 0);
    }
    s_act.shown_today = info->today;
    s_act.shown_goal  = info->goal;
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    aos_steps_info_t info;
    if (aos_hal_steps_get(&info) &&
        (info.today != s_act.shown_today || info.goal != s_act.shown_goal)) {
        show_steps(&info);
    }

    char buf[64];
    aos_imu_t imu;
    if (aos_hal_imu_read(&imu)) {
        snprintf(buf, sizeof(buf), _("acel  %+.2f  %+.2f  %+.2f g"),
                 imu.ax, imu.ay, imu.az);
        lv_label_set_text(s_act.accel, buf);
        snprintf(buf, sizeof(buf), _("giro  %+.0f  %+.0f  %+.0f dps"),
                 imu.gx, imu.gy, imu.gz);
        lv_label_set_text(s_act.gyro, buf);
    }

    snprintf(buf, sizeof(buf), "%s", orient_name(aos_hal_imu_orientation()));
    lv_label_set_text(s_act.orient, buf);
}

/* A tap on the ring walks the goal through the list. */
static void goal_cb(lv_event_t *event)
{
    (void)event;
    aos_steps_info_t info;
    if (!aos_hal_steps_get(&info)) {
        return;
    }
    unsigned next = 0;
    for (unsigned i = 0; i < sizeof GOALS / sizeof GOALS[0]; i++) {
        if (GOALS[i] > info.goal) {
            next = i;
            break;
        }
    }
    aos_hal_steps_set_goal(GOALS[next]);
    s_act.shown_goal = 0;               /* redraw on the next tick */
}

static void reset_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_steps_reset_today();
    s_act.shown_goal = 0;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    /* --- the ring, the count and the goal --- */
    s_act.ring = lv_arc_create(page);
    lv_obj_remove_style(s_act.ring, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_act.ring, 176, 176);
    lv_arc_set_rotation(s_act.ring, 270);
    lv_arc_set_bg_angles(s_act.ring, 0, 360);
    lv_obj_set_style_arc_width(s_act.ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_act.ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_act.ring, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_act.ring, AOS_C_PINK, LV_PART_INDICATOR);
    lv_obj_align(s_act.ring, LV_ALIGN_TOP_MID, 0, 8);
    /* The arc must not be draggable (it is a value, not a slider) but it is
     * the goal's button: a click, not a drag, and no knob to grab. */
    lv_obj_add_flag(s_act.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_act.ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_act.ring, goal_cb, LV_EVENT_CLICKED, NULL);

    s_act.steps = aos_label_boxed(page, "0", aos_font_huge, AOS_C_TEXT, 170, 60);
    lv_obj_align_to(s_act.steps, s_act.ring, LV_ALIGN_CENTER, 0, -14);
    lv_obj_remove_flag(s_act.steps, LV_OBJ_FLAG_CLICKABLE);

    s_act.goal = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_align_to(s_act.goal, s_act.ring, LV_ALIGN_CENTER, 0, 24);
    lv_obj_remove_flag(s_act.goal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *unit = aos_label(page, _("pasos"), aos_font_small, AOS_C_DIM);
    lv_obj_align_to(unit, s_act.ring, LV_ALIGN_CENTER, 0, 46);
    lv_obj_remove_flag(unit, LV_OBJ_FLAG_CLICKABLE);

    s_act.km = aos_label(page, "", aos_font_small, AOS_C_TEAL);
    lv_obj_align(s_act.km, LV_ALIGN_TOP_MID, 0, 190);

    /* --- the week --- */
    int32_t total = AOS_STEPS_DAYS * BAR_W + (AOS_STEPS_DAYS - 1) * BAR_GAP;
    int32_t x0 = (AOS_SCREEN_W - total) / 2;
    for (int i = 0; i < AOS_STEPS_DAYS; i++) {
        lv_obj_t *bar = lv_obj_create(page);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, BAR_W, 2);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, AOS_C_DIM, 0);
        lv_obj_set_style_radius(bar, 3, 0);
        lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(bar, x0 + i * (BAR_W + BAR_GAP), BARS_Y + BAR_H - 2);
        s_act.bars[i] = bar;
    }
    lv_obj_t *base = lv_obj_create(page);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, total, 1);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(base, AOS_C_CARD2, 0);
    lv_obj_align(base, LV_ALIGN_TOP_MID, 0, BARS_Y + BAR_H + 1);
    lv_obj_remove_flag(base, LV_OBJ_FLAG_CLICKABLE);

    /* The button goes ABOVE the three readings, not below: the touch panel
     * reports nothing in the bottom strip (see AOS_TOUCH_Y_MAX in aos_hal.h),
     * and the readings are only looked at. */
    lv_obj_t *reset = aos_button(page, _("Reset"), AOS_C_CARD2, reset_cb, NULL);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_MID, 0, -116);
    lv_obj_set_height(reset, 34);

    s_act.accel  = aos_label(page, "acel", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_act.accel, LV_ALIGN_BOTTOM_MID, 0, -88);
    s_act.gyro   = aos_label(page, "giro", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_act.gyro, LV_ALIGN_BOTTOM_MID, 0, -62);
    s_act.orient = aos_label(page, "-", aos_font_small, AOS_C_TEAL);
    lv_obj_align(s_act.orient, LV_ALIGN_BOTTOM_MID, 0, -36);

    aos_hal_imu_gyro_request(true);
    s_act.shown_goal = 0;
    s_act.timer = lv_timer_create(refresh, 120, NULL);
    refresh(NULL);
    return &s_act;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_act.timer) {
        lv_timer_delete(s_act.timer);
        s_act.timer = NULL;
    }
    aos_hal_imu_gyro_request(false);
}

void aos_app_activity_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.activity",
            .name     = "Actividad",
            .icon_vec = AOS_ICON_ACTIVITY,
            .color_a  = 0x1C1C1E,
            .color_b  = 0x000000,
            .order    = 10,
        },
        .create  = create,
        .destroy = destroy,
    };
}
