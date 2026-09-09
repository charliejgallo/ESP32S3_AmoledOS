/*
 * AmoledOS - Nixie face
 *
 * Four glass tubes with the number in amber, the glow around it and, behind,
 * the unlit eight that on a real tube is always visible because the ten digits
 * sit one behind the other.
 *
 * The glow is the tube's own shadow (shadow_width with an amber colour), not
 * an image and not a layer: LVGL draws it in the same blit as the background.
 * The anode grids are three rectangles at 12% opacity.
 *
 * In dimmed mode everything that is not the number is switched off: the glow,
 * the ghost eight, the grids and the glass. On AMOLED black is a pixel
 * switched off, and a whole tube lit all night is not justified.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

#define TUBE_W      76
#define TUBE_H      148
#define TUBE_Y      148
#define COLON_W     16

#define C_NEON      lv_color_hex(0xFF8A2B)
#define C_GLASS     lv_color_hex(0x101012)
#define C_RIM       lv_color_hex(0x2A2A2E)

typedef struct {
    lv_obj_t *glass;
    lv_obj_t *ghost;
    lv_obj_t *digit;
    char      shown[4];
} tube_t;

typedef struct {
    tube_t    tube[4];
    lv_obj_t *colon[2];
    lv_obj_t *date;
    lv_obj_t *extra;
    bool      aod;
    bool      colon_on;
    char      last[8];
} nixie_t;

/* Four tubes of 76: two together, the colon's gap and another two.
 * 2*76 + 6 + 16 + 6 + 2*76 = 332, centred that leaves 18 of margin. */
static int32_t tube_x(int i)
{
    const int32_t x0 = (AOS_SCREEN_W - (4 * TUBE_W + 2 * 6 + COLON_W)) / 2;
    switch (i) {
    case 0:  return x0;
    case 1:  return x0 + TUBE_W + 6;
    case 2:  return x0 + 2 * TUBE_W + 6 + COLON_W + 6;
    default: return x0 + 3 * TUBE_W + 6 + COLON_W + 6;
    }
}

static void build_tube(lv_obj_t *root, tube_t *t, int index)
{
    t->glass = lv_obj_create(root);
    lv_obj_remove_style_all(t->glass);
    lv_obj_set_size(t->glass, TUBE_W, TUBE_H);
    lv_obj_set_pos(t->glass, tube_x(index), TUBE_Y);
    lv_obj_set_style_radius(t->glass, 26, 0);
    lv_obj_set_style_bg_color(t->glass, C_GLASS, 0);
    lv_obj_set_style_bg_opa(t->glass, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(t->glass, 2, 0);
    lv_obj_set_style_border_color(t->glass, C_RIM, 0);
    /* The tube's glow: an amber shadow with no offset. */
    lv_obj_set_style_shadow_width(t->glass, 26, 0);
    lv_obj_set_style_shadow_color(t->glass, C_NEON, 0);
    lv_obj_set_style_shadow_opa(t->glass, LV_OPA_30, 0);
    lv_obj_remove_flag(t->glass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(t->glass, LV_OBJ_FLAG_CLICKABLE);

    /* Anode grids. */
    for (int i = 0; i < 3; i++) {
        lv_obj_t *wire = lv_obj_create(t->glass);
        lv_obj_remove_style_all(wire);
        lv_obj_set_size(wire, TUBE_W - 22, 2);
        lv_obj_set_style_bg_color(wire, C_NEON, 0);
        lv_obj_set_style_bg_opa(wire, LV_OPA_20, 0);
        lv_obj_align(wire, LV_ALIGN_CENTER, 0, (i - 1) * 40);
    }

    /* The unlit eight, behind. */
    t->ghost = aos_label_scaled(t->glass, "8", aos_font_huge, C_NEON, 330);
    lv_obj_set_style_opa(t->ghost, LV_OPA_10, 0);
    lv_obj_center(t->ghost);

    t->digit = aos_label_scaled(t->glass, "0", aos_font_huge, C_NEON, 330);
    lv_obj_center(t->digit);

    t->shown[0] = '\0';
}

static void tube_set(tube_t *t, char c)
{
    char text[2] = { c, '\0' };
    if (t->shown[0] == c) {
        return;
    }
    t->shown[0] = c;
    t->shown[1] = '\0';
    lv_label_set_text(t->digit, text);
}

/* -------------------------------------------------------------------------- */

static void *create(lv_obj_t *root)
{
    nixie_t *face = lv_malloc_zeroed(sizeof(nixie_t));
    if (!face) {
        return NULL;
    }

    face->date = aos_label(root, "", aos_font_body, C_NEON);
    lv_obj_set_style_opa(face->date, LV_OPA_70, 0);
    lv_obj_align(face->date, LV_ALIGN_TOP_MID, 0, 84);

    for (int i = 0; i < 4; i++) {
        build_tube(root, &face->tube[i], i);
    }

    const int32_t cx = (AOS_SCREEN_W - COLON_W) / 2 + COLON_W / 2;
    for (int i = 0; i < 2; i++) {
        lv_obj_t *dot = lv_obj_create(root);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, C_NEON, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(dot, 14, 0);
        lv_obj_set_style_shadow_color(dot, C_NEON, 0);
        lv_obj_set_style_shadow_opa(dot, LV_OPA_40, 0);
        lv_obj_set_pos(dot, cx - 6, TUBE_Y + (i == 0 ? 46 : TUBE_H - 58));
        face->colon[i] = dot;
    }
    face->colon_on = true;

    face->extra = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(face->extra, LV_ALIGN_BOTTOM_MID, 0, -46);

    return face;
}

static void set_aod(void *ctx, bool aod)
{
    nixie_t *face = (nixie_t *)ctx;
    if (!face) {
        return;
    }
    face->aod = aod;

    for (int i = 0; i < 4; i++) {
        tube_t *t = &face->tube[i];
        lv_obj_set_style_bg_opa(t->glass, aod ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(t->glass, aod ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_opa(t->glass, aod ? LV_OPA_TRANSP : LV_OPA_30, 0);
        lv_obj_set_style_opa(t->ghost, aod ? LV_OPA_TRANSP : LV_OPA_10, 0);
        lv_obj_set_style_text_color(t->digit,
                                    aod ? lv_color_hex(0x7A4210) : C_NEON, 0);
        uint32_t count = lv_obj_get_child_count(t->glass);
        for (uint32_t k = 0; k < count; k++) {
            lv_obj_t *child = lv_obj_get_child(t->glass, k);
            if (child != t->ghost && child != t->digit) {
                lv_obj_set_style_opa(child, aod ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_opa(face->colon[i], LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_opa(face->colon[i], aod ? LV_OPA_TRANSP : LV_OPA_40, 0);
        lv_obj_set_style_bg_color(face->colon[i],
                                  aod ? lv_color_hex(0x7A4210) : C_NEON, 0);
    }

    if (aod) {
        lv_obj_add_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(face->extra, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face->date, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(face->extra, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh(void *ctx, const struct tm *now)
{
    nixie_t *face = (nixie_t *)ctx;
    if (!face) {
        return;
    }

    tube_set(&face->tube[0], (char)('0' + now->tm_hour / 10));
    tube_set(&face->tube[1], (char)('0' + now->tm_hour % 10));
    tube_set(&face->tube[2], (char)('0' + now->tm_min / 10));
    tube_set(&face->tube[3], (char)('0' + now->tm_min % 10));

    if (face->aod) {
        return;
    }

    /* The colon pulses with the second, which is the only thing that moves. */
    bool on = (now->tm_sec % 2) == 0;
    if (on != face->colon_on) {
        face->colon_on = on;
        for (int i = 0; i < 2; i++) {
            lv_obj_set_style_bg_opa(face->colon[i],
                                    on ? LV_OPA_COVER : LV_OPA_20, 0);
            lv_obj_set_style_shadow_opa(face->colon[i],
                                        on ? LV_OPA_40 : LV_OPA_TRANSP, 0);
        }
    }

    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", now->tm_hour, now->tm_min);
    if (strcmp(face->last, hhmm) == 0) {
        return;                 /* the rest changes once a minute */
    }
    memcpy(face->last, hhmm, sizeof(face->last));

    char buf[40];
    snprintf(buf, sizeof(buf), "%s %d %s", aos_day_name(now->tm_wday),
             now->tm_mday, aos_month_name(now->tm_mon));
    lv_label_set_text(face->date, buf);

    aos_battery_t batt;
    if (aos_hal_battery_read(&batt) && batt.percent >= 0) {
        snprintf(buf, sizeof(buf), "%s%d%%   " LV_SYMBOL_LOOP " %u",
                 batt.charging ? LV_SYMBOL_CHARGE " " : "", batt.percent,
                 (unsigned)aos_hal_imu_steps());
        lv_label_set_text(face->extra, buf);
    }
}

static void destroy(void *ctx)
{
    lv_free(ctx);
}

void aos_face_nixie_get(aos_watchface_t *face)
{
    face->id      = "nixie";
    face->name    = N_("Nixie");
    face->create  = create;
    face->refresh = refresh;
    face->set_aod = set_aod;
    face->destroy = destroy;
}
