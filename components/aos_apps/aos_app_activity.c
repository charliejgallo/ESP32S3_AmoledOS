/* AmoledOS - Activity: steps, orientation and raw QMI8658 reading. */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"

#include <stdio.h>

typedef struct {
    lv_obj_t   *steps;
    lv_obj_t   *ring;
    lv_obj_t   *accel;
    lv_obj_t   *gyro;
    lv_obj_t   *orient;
    lv_obj_t   *bubble;     /* spirit level, moves with the tilt */
    lv_timer_t *timer;
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

static void refresh(lv_timer_t *timer)
{
    (void)timer;

    uint32_t steps = aos_hal_imu_steps();
    char buf[64];
    snprintf(buf, sizeof(buf), "%u", (unsigned)steps);
    lv_label_set_text(s_act.steps, buf);
    lv_arc_set_value(s_act.ring, (int32_t)LV_MIN(100, steps * 100 / 8000));

    aos_imu_t imu;
    if (aos_hal_imu_read(&imu)) {
        snprintf(buf, sizeof(buf), _("acel  %+.2f  %+.2f  %+.2f g"),
                 imu.ax, imu.ay, imu.az);
        lv_label_set_text(s_act.accel, buf);
        snprintf(buf, sizeof(buf), _("giro  %+.0f  %+.0f  %+.0f dps"),
                 imu.gx, imu.gy, imu.gz);
        lv_label_set_text(s_act.gyro, buf);

        /* ax is the screen's vertical axis and ay the horizontal one (measured
         * on the board). Like the level, it moves as a bubble does: towards
         * the raised side. The label above is NOT touched: it prints raw ax,
         * ay, az and is the only measuring instrument there is. */
        int32_t x = (int32_t)(imu.ay * 70.0f);
        int32_t y = (int32_t)(-imu.ax * 70.0f);
        lv_obj_align(s_act.bubble, LV_ALIGN_CENTER,
                     LV_CLAMP(-60, x, 60), LV_CLAMP(-60, y, 60));
    }

    snprintf(buf, sizeof(buf), "%s", orient_name(aos_hal_imu_orientation()));
    lv_label_set_text(s_act.orient, buf);
}

static void reset_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_imu_steps_reset();
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    s_act.ring = lv_arc_create(page);
    lv_obj_remove_style(s_act.ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_act.ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_act.ring, 190, 190);
    lv_arc_set_rotation(s_act.ring, 270);
    lv_arc_set_bg_angles(s_act.ring, 0, 360);
    lv_obj_set_style_arc_width(s_act.ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_act.ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_act.ring, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_act.ring, AOS_C_PINK, LV_PART_INDICATOR);
    lv_obj_align(s_act.ring, LV_ALIGN_TOP_MID, 0, 14);

    s_act.steps = aos_label_boxed(page, "0", aos_font_huge, AOS_C_TEXT, 180, 60);
    lv_obj_align_to(s_act.steps, s_act.ring, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *unit = aos_label(page, _("pasos"), aos_font_small, AOS_C_DIM);
    lv_obj_align_to(unit, s_act.ring, LV_ALIGN_CENTER, 0, 30);

    /* spirit level */
    lv_obj_t *level = lv_obj_create(page);
    lv_obj_remove_style_all(level);
    lv_obj_set_size(level, 150, 150);
    lv_obj_set_style_radius(level, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(level, 2, 0);
    lv_obj_set_style_border_color(level, AOS_C_CARD2, 0);
    lv_obj_align(level, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_add_flag(level, LV_OBJ_FLAG_HIDDEN);   /* reserved, enabled later */

    s_act.bubble = lv_obj_create(page);
    lv_obj_remove_style_all(s_act.bubble);
    lv_obj_set_size(s_act.bubble, 14, 14);
    lv_obj_set_style_radius(s_act.bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_act.bubble, AOS_C_GREEN, 0);
    lv_obj_set_style_bg_opa(s_act.bubble, LV_OPA_60, 0);
    lv_obj_align(s_act.bubble, LV_ALIGN_CENTER, 0, 0);

    /* The button goes ABOVE the three readings, not below.
     *
     * It was at BOTTOM_MID -4, that is, y 410..443, and this board's touch
     * panel reports nothing below 395 (see AOS_TOUCH_Y_MAX in aos_hal.h): the
     * Reset never responded. The sensor readings, on the other hand, are text
     * that is only looked at, so moving them down into that strip costs
     * nothing and frees the live pixels for the button. */
    lv_obj_t *reset = aos_button(page, _("Reset"), AOS_C_CARD2, reset_cb, NULL);
    lv_obj_align(reset, LV_ALIGN_BOTTOM_MID, 0, -116);
    lv_obj_set_height(reset, 34);

    s_act.accel  = aos_label(page, "acel", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_act.accel, LV_ALIGN_BOTTOM_MID, 0, -88);
    s_act.gyro   = aos_label(page, "giro", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_act.gyro, LV_ALIGN_BOTTOM_MID, 0, -62);
    s_act.orient = aos_label(page, "-", aos_font_small, AOS_C_TEAL);
    lv_obj_align(s_act.orient, LV_ALIGN_BOTTOM_MID, 0, -36);

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
