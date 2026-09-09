/*
 * AmoledOS - Minimal face: hours and minutes stacked, left-aligned.
 *
 * It is the kindest one to always-on: nearly the whole screen stays pure
 * black, which on AMOLED is a pixel switched off.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *battery;
    bool      aod;
    char      last[8];
} minimal_t;

static void *create(lv_obj_t *root)
{
    minimal_t *face = lv_malloc_zeroed(sizeof(minimal_t));
    if (!face) {
        return NULL;
    }

    face->hour = aos_label_scaled(root, "--", aos_font_huge, AOS_C_TEXT, 560);
    lv_obj_align(face->hour, LV_ALIGN_CENTER, 0, -76);

    face->minute = aos_label_scaled(root, "--", aos_font_huge, AOS_C_ACCENT, 560);
    lv_obj_align(face->minute, LV_ALIGN_CENTER, 0, 76);

    face->date = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->date, LV_ALIGN_TOP_LEFT, 40, 54);

    face->battery = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->battery, LV_ALIGN_BOTTOM_LEFT, 40, -54);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    minimal_t *face = (minimal_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    lv_obj_set_style_text_color(face->hour,
                                aod ? lv_color_hex(0x8A8A8A) : AOS_C_TEXT, 0);
    lv_obj_set_style_text_color(face->minute,
                                aod ? lv_color_hex(0x30506E) : AOS_C_ACCENT, 0);

    if (aod) {
        lv_obj_add_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face->battery, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face->battery, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    minimal_t *face = (minimal_t *)ctx;
    if (!face) {
        return;
    }

    char buf[32];
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(face->last, hhmm) == 0) {
        return;             /* it only changes once a minute */
    }
    memcpy(face->last, hhmm, sizeof(face->last));

    snprintf(buf, sizeof(buf), "%02d", now->tm_hour);
    lv_label_set_text(face->hour, buf);
    lv_obj_align(face->hour, LV_ALIGN_CENTER, 0, -76);

    snprintf(buf, sizeof(buf), "%02d", now->tm_min);
    lv_label_set_text(face->minute, buf);
    lv_obj_align(face->minute, LV_ALIGN_CENTER, 0, 76);

    if (face->aod) {
        return;
    }

    snprintf(buf, sizeof(buf), "%s %d %s",
             aos_day_name(now->tm_wday), now->tm_mday, aos_month_name(now->tm_mon));
    lv_label_set_text(face->date, buf);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        snprintf(buf, sizeof(buf), "%s%d%%",
                 batt.charging ? LV_SYMBOL_CHARGE " " : "", batt.percent);
        lv_label_set_text(face->battery, buf);
    }
}

static void destroy(void *ctx)
{
    lv_free(ctx);
}

void aos_face_minimal_get(aos_watchface_t *face)
{
    face->id      = "minimal";
    face->name    = N_("Minima");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
