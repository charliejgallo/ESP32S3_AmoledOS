/*
 * AmoledOS - Launcher.
 *
 * Three styles:
 *   LIST       vertical list with large icons, the ends shrinking and fading
 *              (the watchOS effect).
 *   GRID       3-column grid.
 *   HONEYCOMB  hexagonal honeycomb, radial scaling from the centre.
 */
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_internal.h"

#include <stdlib.h>

#define ROW_H           92
#define ICON_LIST       66
#define ICON_GRID       82
#define ICON_HONEY      74
#define HONEY_STEP_X    88
#define HONEY_STEP_Y    76

/* --------------------------------------------------------------------------
 * Scaling effect: the further from the useful centre, the smaller and more
 * transparent
 * -------------------------------------------------------------------------- */

/*
 * The ends effect, the cheap version.
 *
 * The first version applied transform_scale and opa to the whole ROW. In LVGL
 * that turns each row into a layer: a 328x92 buffer (60 KB) that has to be
 * allocated, cleared with memset, drawn separately and only then composited.
 * Eight rows per frame, executing out of PSRAM. On the Mac it is unnoticeable;
 * on the board it saturated the CPU and the system froze the moment you opened
 * the menu (measured: "task_wdt ... CPU 0: taskLVGL" and a dead screen).
 *
 * Now the scaling goes only on the icon (66x66, an 8 KB layer instead of 60)
 * and the opacity only on the text, which LVGL blends directly without
 * creating any layer.
 */
static void apply_falloff(lv_obj_t *row, int32_t distance, int32_t fade_start,
                          int32_t fade_span, int32_t min_scale)
{
    int32_t scale = 256;
    int32_t opa   = LV_OPA_COVER;

    if (distance > fade_start) {
        int32_t over = distance - fade_start;
        if (over > fade_span) {
            over = fade_span;
        }
        scale = 256 - (256 - min_scale) * over / fade_span;
        opa   = LV_OPA_COVER - (LV_OPA_COVER - 40) * over / fade_span;
    }

    /* Opacity only. Verified in LVGL's calculate_layer_type(): transform_* and
     * opa_layered force a LAYER (separate buffer + memset + render +
     * compositing); ordinary opacity is multiplied into each drawing operation
     * and costs nothing. Dragging the menu recomputes this for every row on
     * every frame, so there cannot be a single layer here. */
    (void)scale;

    /* And it is only written when it CHANGES. lv_obj_set_style_opa() does not
     * compare: it sets the property and invalidates the object every time,
     * even when handed the same value. Since this runs for every row on every
     * scroll event, with 27 apps that was ~54 invalidations per frame, and
     * LVGL's invalid-area buffer (LV_INV_BUF_SIZE, 32) overflowed: on
     * overflowing, LVGL throws everything away and repaints the WHOLE SCREEN.
     * Which means the ends effect, which touches three or four rows, was
     * forcing a full redraw on every frame of the drag. By looking at the
     * current value first, only the rows that really are fading get
     * invalidated. */
    uint32_t count = lv_obj_get_child_count(row);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(row, i);
        if (lv_obj_get_style_opa(child, 0) == (lv_opa_t)opa) {
            continue;
        }
        lv_obj_set_style_opa(child, (lv_opa_t)opa, 0);
    }
}

static void list_scroll_cb(lv_event_t *event)
{
    lv_obj_t *cont = lv_event_get_target(event);
    lv_area_t cont_area;
    lv_obj_get_coords(cont, &cont_area);

    const int32_t top    = cont_area.y1;
    const int32_t bottom = cont_area.y2;

    uint32_t count = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_area_t area;
        lv_obj_get_coords(child, &area);
        int32_t center = (area.y1 + area.y2) / 2;

        int32_t distance = 0;
        if (center < top + ROW_H) {
            distance = (top + ROW_H) - center;
        } else if (center > bottom - ROW_H) {
            distance = center - (bottom - ROW_H);
        }
        apply_falloff(child, distance, 0, ROW_H + 40, 130);
    }
}

static void honey_scroll_cb(lv_event_t *event)
{
    lv_obj_t *cont = lv_event_get_target(event);
    lv_area_t cont_area;
    lv_obj_get_coords(cont, &cont_area);

    const int32_t cx = (cont_area.x1 + cont_area.x2) / 2;
    const int32_t cy = (cont_area.y1 + cont_area.y2) / 2;

    uint32_t count = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(cont, i);
        lv_area_t area;
        lv_obj_get_coords(child, &area);
        int32_t dx = (area.x1 + area.x2) / 2 - cx;
        int32_t dy = (area.y1 + area.y2) / 2 - cy;
        int32_t distance = (int32_t)lv_sqrt32((uint32_t)(dx * dx + dy * dy));
        apply_falloff(child, distance, 70, 150, 96);
    }
}

/* -------------------------------------------------------------------------- */

static void open_cb(lv_event_t *event)
{
    const char *id = (const char *)lv_event_get_user_data(event);
    aos_hal_activity();
    aos_ui_open(id);
}

static lv_obj_t *make_cell(lv_obj_t *parent, const aos_app_desc_t *desc)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cell, open_cb, LV_EVENT_CLICKED, (void *)desc->id);
    return cell;
}

/* -------------------------------------------------------------------------- */

static lv_obj_t *build_list(lv_obj_t *cont)
{
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 6, 0);
    lv_obj_set_style_pad_top(cont, 44, 0);
    lv_obj_set_style_pad_bottom(cont, 60, 0);
    lv_obj_set_style_pad_left(cont, 40, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(cont, list_scroll_cb, LV_EVENT_SCROLL, NULL);

    int count = aos_ui_app_count();
    for (int i = 0; i < count; i++) {
        const aos_app_t *app = aos_ui_app_at(i);
        lv_obj_t *row = make_cell(cont, &app->desc);
        lv_obj_set_size(row, AOS_SCREEN_W - 40, ROW_H);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 20, 0);

        lv_obj_t *icon = aos_icon_create(row, &app->desc, ICON_LIST);
        lv_obj_set_style_transform_scale(icon, 244, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_x(icon, ICON_LIST / 2, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_y(icon, ICON_LIST / 2, LV_STATE_PRESSED);

        lv_obj_t *label = aos_label(row, _(app->desc.name), aos_font_title, AOS_C_TEXT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, AOS_SCREEN_W - 40 - ICON_LIST - 40);
    }

    return cont;
}

static lv_obj_t *build_grid(lv_obj_t *cont)
{
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 18, 0);
    lv_obj_set_style_pad_column(cont, 14, 0);
    lv_obj_set_style_pad_top(cont, 56, 0);
    lv_obj_set_style_pad_bottom(cont, 40, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    int count = aos_ui_app_count();
    for (int i = 0; i < count; i++) {
        const aos_app_t *app = aos_ui_app_at(i);
        lv_obj_t *cell = make_cell(cont, &app->desc);
        lv_obj_set_size(cell, 106, 124);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 8, 0);

        aos_icon_create(cell, &app->desc, ICON_GRID);
        lv_obj_t *label = aos_label(cell, _(app->desc.name), aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        /* fixed one-line height: long names are clipped with an ellipsis
         * instead of splitting in two and knocking the grid out of line */
        lv_obj_set_size(label, 104, 20);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    return cont;
}

static lv_obj_t *build_honeycomb(lv_obj_t *cont)
{
    lv_obj_set_scroll_dir(cont, LV_DIR_ALL);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(cont, honey_scroll_cb, LV_EVENT_SCROLL, NULL);

    int count = aos_ui_app_count();
    /* hexagonal lattice: 3 columns per row, odd rows shifted half a cell. The
     * whole thing ends up centred so the radial scaling effect makes sense the
     * moment the menu opens. */
    const int per_row = 3;
    const int rows = (count + per_row - 1) / per_row;

    const int32_t block_w = per_row * HONEY_STEP_X;
    const int32_t block_h = rows * HONEY_STEP_Y;
    const int32_t x0 = (AOS_SCREEN_W - block_w) / 2 + (HONEY_STEP_X - ICON_HONEY) / 2
                       - (rows > 1 ? HONEY_STEP_X / 4 : 0);
    const int32_t y0 = (AOS_SCREEN_H - block_h) / 2;

    for (int i = 0; i < count; i++) {
        const aos_app_t *app = aos_ui_app_at(i);
        int row = i / per_row;
        int col = i % per_row;

        int32_t x = x0 + col * HONEY_STEP_X + (row % 2 ? HONEY_STEP_X / 2 : 0);
        int32_t y = y0 + row * HONEY_STEP_Y;

        lv_obj_t *cell = make_cell(cont, &app->desc);
        lv_obj_set_size(cell, ICON_HONEY, ICON_HONEY);
        lv_obj_set_pos(cell, x, y);
        aos_icon_create(cell, &app->desc, ICON_HONEY);
    }
    return cont;
}

/* -------------------------------------------------------------------------- */

lv_obj_t *aos_launcher_create(lv_obj_t *parent, aos_launcher_style_t style)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_clip_corner(cont, true, 0);

    switch (style) {
    case AOS_LAUNCHER_GRID:      build_grid(cont);      break;
    case AOS_LAUNCHER_HONEYCOMB: build_honeycomb(cont); break;
    case AOS_LAUNCHER_LIST:
    default:                     build_list(cont);      break;
    }

    /* apply the effect once, with the layout already resolved */
    lv_obj_update_layout(cont);
    lv_obj_send_event(cont, LV_EVENT_SCROLL, NULL);
    return cont;
}
