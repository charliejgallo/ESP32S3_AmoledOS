/*
 * AmoledOS - Digital face: large time, date, battery and steps.
 * In dimmed mode it keeps only the time, in grey.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *date;
    lv_obj_t *time;
    lv_obj_t *seconds;
    lv_obj_t *extras;       /* battery and steps, hidden when dimmed */
    lv_obj_t *batt_arc;
    lv_obj_t *batt_label;
    lv_obj_t *steps_label;
    bool      aod;
    char      last_time[8];
} digital_t;

static void *create(lv_obj_t *root)
{
    digital_t *face = lv_malloc_zeroed(sizeof(digital_t));
    if (!face) {
        return NULL;
    }

    face->date = aos_label(root, "", aos_font_body, AOS_C_ACCENT);
    lv_obj_align(face->date, LV_ALIGN_TOP_MID, 0, 62);

    /* The largest compiled font is 48 px; we scale it so the time fills the
     * screen. Since it changes once a minute, the cost of rasterising the
     * transformed layer is negligible. */
    face->time = aos_label_scaled(root, "--:--", aos_font_huge, AOS_C_TEXT, 460);
    lv_obj_align(face->time, LV_ALIGN_CENTER, 0, -26);

    face->seconds = aos_label(root, "00", aos_font_body, AOS_C_DIM);
    lv_obj_align(face->seconds, LV_ALIGN_CENTER, 0, 46);

    face->extras = lv_obj_create(root);
    lv_obj_remove_style_all(face->extras);
    lv_obj_set_size(face->extras, lv_pct(100), 140);
    lv_obj_align(face->extras, LV_ALIGN_BOTTOM_MID, 0, 0);

    face->batt_arc = lv_arc_create(face->extras);
    lv_obj_remove_style(face->batt_arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(face->batt_arc, 76, 76);
    lv_arc_set_rotation(face->batt_arc, 270);
    lv_arc_set_bg_angles(face->batt_arc, 0, 360);
    lv_arc_set_value(face->batt_arc, 0);
    lv_obj_set_style_arc_width(face->batt_arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(face->batt_arc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(face->batt_arc, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(face->batt_arc, AOS_C_TEAL, LV_PART_INDICATOR);
    lv_obj_align(face->batt_arc, LV_ALIGN_BOTTOM_MID, -66, -54);

    face->batt_label = aos_label_boxed(face->extras, "--", aos_font_small,
                                       AOS_C_TEXT, 72, 20);
    lv_obj_align_to(face->batt_label, face->batt_arc, LV_ALIGN_CENTER, 0, 0);

    face->steps_label = aos_label(face->extras, LV_SYMBOL_LOOP "  0",
                                  aos_font_body, AOS_C_ORANGE);
    lv_obj_align(face->steps_label, LV_ALIGN_BOTTOM_MID, 60, -78);

    char hint_txt[32];
    snprintf(hint_txt, sizeof(hint_txt), LV_SYMBOL_UP "  %s", _("apps"));
    lv_obj_t *hint = aos_label(face->extras, hint_txt,
                               aos_font_small, AOS_C_DIM);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_opa(hint, LV_OPA_40, 0);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    digital_t *face = (digital_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    lv_obj_set_style_text_color(face->time,
                                aod ? lv_color_hex(0x9A9A9A) : AOS_C_TEXT, 0);
    lv_obj_set_style_text_color(face->date,
                                aod ? lv_color_hex(0x4A5A6A) : AOS_C_ACCENT, 0);

    if (aod) {
        lv_obj_add_flag(face->seconds, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face->extras, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->seconds, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face->extras, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    digital_t *face = (digital_t *)ctx;
    if (!face) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%s %d %s",
             aos_day_name(now->tm_wday), now->tm_mday, aos_month_name(now->tm_mon));
    lv_label_set_text(face->date, buf);

    /* A buffer of its own at the exact size: copying one of 32 into one of 8
     * makes gcc complain about truncation, and rightly so */
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(face->last_time, hhmm) != 0) {
        memcpy(face->last_time, hhmm, sizeof(face->last_time));
        lv_label_set_text(face->time, hhmm);
        lv_obj_align(face->time, LV_ALIGN_CENTER, 0, -26);
    }

    if (face->aod) {
        return;     /* in dimmed mode nothing else is drawn */
    }

    snprintf(buf, sizeof(buf), "%02d", now->tm_sec);
    lv_label_set_text(face->seconds, buf);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        lv_arc_set_value(face->batt_arc, batt.percent);
        lv_obj_set_style_arc_color(face->batt_arc,
                                   batt.charging      ? AOS_C_GREEN :
                                   batt.percent <= 15 ? AOS_C_RED : AOS_C_TEAL,
                                   LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "%d%%", batt.percent);
        lv_label_set_text(face->batt_label, buf);
    }

    snprintf(buf, sizeof(buf), LV_SYMBOL_LOOP "  %u",
             (unsigned)aos_hal_imu_steps());
    lv_label_set_text(face->steps_label, buf);
}

static void destroy(void *ctx)
{
    lv_free(ctx);
}

void aos_face_digital_get(aos_watchface_t *face)
{
    face->id      = "digital";
    face->name    = N_("Digital");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
