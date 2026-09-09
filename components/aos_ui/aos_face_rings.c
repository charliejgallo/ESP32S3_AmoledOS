/*
 * AmoledOS - Rings face: battery, steps and the hour's progress.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

#define STEP_GOAL   8000

typedef struct {
    lv_obj_t *battery;
    lv_obj_t *steps;
    lv_obj_t *minutes;
    lv_obj_t *time;
    lv_obj_t *caption;
    bool      aod;
    char      last[8];
} rings_t;

static lv_obj_t *ring_create(lv_obj_t *parent, int32_t size, int32_t width,
                             lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1A1A1C), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_center(arc);
    return arc;
}

static void *create(lv_obj_t *root)
{
    rings_t *face = lv_malloc_zeroed(sizeof(rings_t));
    if (!face) {
        return NULL;
    }

    lv_obj_t *stack = lv_obj_create(root);
    lv_obj_remove_style_all(stack);
    lv_obj_set_size(stack, 300, 300);
    lv_obj_align(stack, LV_ALIGN_CENTER, 0, -10);

    face->battery = ring_create(stack, 292, 16, AOS_C_TEAL);
    face->steps   = ring_create(stack, 252, 16, AOS_C_PINK);
    face->minutes = ring_create(stack, 212, 16, AOS_C_GREEN);

    face->time = aos_label(root, "--:--", aos_font_huge, AOS_C_TEXT);
    lv_obj_align(face->time, LV_ALIGN_CENTER, 0, -10);

    face->caption = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->caption, LV_ALIGN_BOTTOM_MID, 0, -44);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    rings_t *face = (rings_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    lv_opa_t opa = aod ? LV_OPA_40 : LV_OPA_COVER;
    lv_obj_set_style_arc_opa(face->battery, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(face->steps, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(face->minutes, opa, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(face->battery, aod ? LV_OPA_0 : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(face->steps, aod ? LV_OPA_0 : LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(face->minutes, aod ? LV_OPA_0 : LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_set_style_text_color(face->time,
                                aod ? lv_color_hex(0x9A9A9A) : AOS_C_TEXT, 0);

    if (aod) {
        lv_obj_add_flag(face->caption, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->caption, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    rings_t *face = (rings_t *)ctx;
    if (!face) {
        return;
    }

    char buf[48];
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(face->last, hhmm) != 0) {
        memcpy(face->last, hhmm, sizeof(face->last));
        lv_label_set_text(face->time, hhmm);
        lv_obj_align(face->time, LV_ALIGN_CENTER, 0, -10);
        lv_arc_set_value(face->minutes, now->tm_min * 100 / 60);
    }

    if (face->aod) {
        return;
    }

    uint32_t steps = aos_hal_imu_steps();
    lv_arc_set_value(face->steps, (int32_t)LV_MIN(100, steps * 100 / STEP_GOAL));

    aos_battery_t batt;
    int percent = -1;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        percent = batt.percent;
        lv_arc_set_value(face->battery, percent);
    }

    snprintf(buf, sizeof(buf), _("%u pasos    %d%%"), (unsigned)steps, percent);
    lv_label_set_text(face->caption, buf);
}

static void destroy(void *ctx)
{
    lv_free(ctx);
}

void aos_face_rings_get(aos_watchface_t *face)
{
    face->id      = "rings";
    face->name    = N_("Anillos");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
