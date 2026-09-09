/*
 * AmoledOS - Palette, typefaces and UI helpers.
 *
 * Pure black background on purpose: on AMOLED a black pixel is switched off,
 * so every black area is zero power.
 */
#pragma once

#include "lvgl.h"
#include "aos_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Palette (taken from iOS's colour system in dark mode) */
#define AOS_C_BG        lv_color_hex(0x000000)
#define AOS_C_CARD      lv_color_hex(0x1C1C1E)
#define AOS_C_CARD2     lv_color_hex(0x2C2C2E)
#define AOS_C_TEXT      lv_color_hex(0xFFFFFF)
#define AOS_C_DIM       lv_color_hex(0x8E8E93)
#define AOS_C_ACCENT    lv_color_hex(0x0A84FF)
#define AOS_C_GREEN     lv_color_hex(0x30D158)
#define AOS_C_RED       lv_color_hex(0xFF453A)
#define AOS_C_ORANGE    lv_color_hex(0xFF9F0A)
#define AOS_C_YELLOW    lv_color_hex(0xFFD60A)
#define AOS_C_PURPLE    lv_color_hex(0xBF5AF2)
#define AOS_C_PINK      lv_color_hex(0xFF375F)
#define AOS_C_TEAL      lv_color_hex(0x40C8E0)

/* Typefaces resolved at run time from whatever is compiled in */
extern const lv_font_t *aos_font_huge;   /* ~48 px, clocks             */
extern const lv_font_t *aos_font_title;  /* ~28 px, titles and menu    */
extern const lv_font_t *aos_font_body;   /* ~20 px                     */
extern const lv_font_t *aos_font_small;  /* ~16 px, status bar         */

void aos_theme_init(void);

/* Page container: black, no border, no scrolling, fills its parent. */
lv_obj_t *aos_page(lv_obj_t *parent);

/* Quick label. A NULL/0 'font' or 'color' uses the defaults. */
lv_obj_t *aos_label(lv_obj_t *parent, const char *text,
                    const lv_font_t *font, lv_color_t color);

/* Scaled label.
 *
 * LVGL scales from the pivot, and the default pivot is the top-left corner: a
 * centred label scaled afterwards shifts right and down. Here the pivot goes
 * to 50% on both axes, so the text grows evenly both ways and the alignment
 * you set still holds.
 *
 * 'scale' is in LVGL units: 256 = original size. */
lv_obj_t *aos_label_scaled(lv_obj_t *parent, const char *text,
                           const lv_font_t *font, lv_color_t color,
                           int32_t scale);

/* Fixed-width label with the text centred inside.
 *
 * Needed whenever a text that changes length is positioned with
 * lv_obj_align_to(): that function computes the position once and does NOT
 * recompute it when the object changes size, so a content-sized label drifts
 * as the text grows. With a fixed box, the position always holds and the text
 * centres inside it. */
lv_obj_t *aos_label_boxed(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, lv_color_t color,
                          int32_t width, int32_t height);

/* Rounded watchOS-style button. */
lv_obj_t *aos_button(lv_obj_t *parent, const char *text, lv_color_t color,
                     lv_event_cb_t cb, void *user_data);

/* Circular icon with a gradient + glyph/vector, at the size asked for in px. */
lv_obj_t *aos_icon_create(lv_obj_t *parent, const aos_app_desc_t *desc, int32_t size);

/* Takes LV_OBJ_FLAG_CLICKABLE off an object and all its children. In LVGL 9
 * every lv_obj is born clickable, so any piece of decoration eats the touch
 * meant for its container. */
void aos_make_decorative(lv_obj_t *obj);

/* Short Spanish names for dates. */
const char *aos_day_name(int wday);     /* 0 = Sunday -> "DOM" */
const char *aos_month_name(int mon);    /* 0 = January -> "ENE" */

/* Clock hand: a rectangle with its pivot at the bottom end, rotating about the
 * parent's centre. The angle is in tenths of a degree, with 0 = twelve
 * o'clock. Used by the icons and the analogue face. */
lv_obj_t *aos_hand_create(lv_obj_t *parent, int32_t width, int32_t length,
                          lv_color_t color);
void      aos_hand_set_angle(lv_obj_t *hand, int32_t deg_tenths);

#ifdef __cplusplus
}
#endif
