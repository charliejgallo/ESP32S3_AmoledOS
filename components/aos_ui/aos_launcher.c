/*
 * AmoledOS - Launcher.
 *
 * Three styles:
 *   LIST       vertical list with large icons, the ends shrinking and fading
 *              (the watchOS effect).
 *   GRID       3-column grid.
 *   HONEYCOMB  hexagonal honeycomb, radial scaling from the centre.
 *
 * What it shows and in which order comes from menu.txt (aos_menu.h): apps
 * and folders at the top level, and a folder opens as a second page in the
 * same style, over the first. The object aos_launcher_create() returns is a
 * frame that holds the two pages; aos_ui.c slides and hides that frame and
 * never looks inside it.
 */
#include "aos_ui.h"
#include "aos_menu.h"
#include "aos_folder_icon.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* --------------------------------------------------------------------------
 * The launcher's state
 *
 * One launcher exists at a time, so its pieces are file statics, cleared when
 * the frame is deleted. The open folder's id is kept apart from them: the
 * launcher is rebuilt when an app arrives or the portal saves, and a folder
 * that was open when an app was launched from it has to be open again when
 * that app closes, rebuild or not.
 * -------------------------------------------------------------------------- */

static lv_obj_t *s_frame;
static lv_obj_t *s_top;                 /* the top level's page */
static lv_obj_t *s_folder_page;         /* the open folder's page, or NULL */
static aos_launcher_style_t s_built_style;
static char s_open_folder[AOS_MENU_FOLDER_ID_MAX];

#define FOLDER_ANIM_MS  220

/* One cell of a page: an app, or a folder (app NULL). */
typedef aos_menu_item_t cell_t;

static void open_folder(int folder, bool animate);

static void open_cb(lv_event_t *event)
{
    const char *id = (const char *)lv_event_get_user_data(event);
    aos_hal_activity();
    aos_ui_open(id);
}

static void folder_cb(lv_event_t *event)
{
    aos_hal_activity();
    open_folder((int)(intptr_t)lv_event_get_user_data(event), true);
}

static void folder_back_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_activity();
    aos_launcher_close_folder(true);
}

static lv_obj_t *make_cell(lv_obj_t *parent, const cell_t *c)
{
    lv_obj_t *cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    if (c->app) {
        lv_obj_add_event_cb(cell, open_cb, LV_EVENT_CLICKED, (void *)c->app->desc.id);
    } else {
        lv_obj_add_event_cb(cell, folder_cb, LV_EVENT_CLICKED, (void *)(intptr_t)c->folder);
    }
    return cell;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const cell_t *c, int32_t size)
{
    if (c->app) {
        return aos_icon_create(parent, &c->app->desc, size);
    }
    lv_obj_t *icon = aos_folder_icon_create(parent, aos_menu_folder(c->folder), size);
    if (!icon) {                            /* no memory for the pixels */
        icon = lv_obj_create(parent);
        lv_obj_remove_style_all(icon);
        lv_obj_set_size(icon, size, size);
    }
    return icon;
}

/* The app's name goes through the catalogue; a folder's is the user's own. */
static const char *cell_name(const cell_t *c)
{
    return c->app ? _(c->app->desc.name) : aos_menu_folder(c->folder)->name;
}

/* A folder's name at the top of its page, which also closes it. */
static lv_obj_t *make_header(lv_obj_t *parent, int folder)
{
    lv_obj_t *hdr = lv_label_create(parent);
    char text[AOS_MENU_NAME_MAX + 8];
    snprintf(text, sizeof(text), LV_SYMBOL_LEFT "  %s", aos_menu_folder(folder)->name);
    lv_label_set_text(hdr, text);
    lv_obj_set_style_text_font(hdr, aos_font_title, 0);
    lv_obj_set_style_text_color(hdr, AOS_C_TEXT, 0);
    lv_label_set_long_mode(hdr, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(hdr, 12);
    lv_obj_add_event_cb(hdr, folder_back_cb, LV_EVENT_CLICKED, NULL);
    return hdr;
}

/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Pages, built a few cells at a time
 *
 * Measured on the board with menu.txt: a launcher cell costs ~14 ms to build
 * (the icon's objects, the label, the flex layout), 776 ms for the 54 cells
 * of a top level. Built all at once, 245 cells held the UI task for over
 * three seconds and the watchdog restarted the watch. So a page builds the
 * cells that can be on screen when it opens, and a timer adds the rest a
 * handful per tick: the screen answers the whole time, the finger can scroll
 * what is there, and the ceiling on apps is no longer the launcher.
 * -------------------------------------------------------------------------- */

#define FIRST_CELLS     18      /* more than any style shows on one screen */
#define CHUNK_CELLS     4       /* per tick afterwards: ~60 ms on the board */

typedef struct {
    aos_launcher_style_t style;
    cell_t     *cells;          /* our copy; the caller's goes away */
    int         count;
    int         next;           /* first cell not built yet */
    int         folder;         /* the folder this page shows, or -1 */
    int32_t     hx0, hy0;       /* honeycomb: where the lattice starts */
    lv_timer_t *timer;
    uint64_t    t0;
} page_t;

static void setup_list(lv_obj_t *cont, page_t *pg)
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

    if (pg->folder >= 0) {
        /* A row of its own, so the fading treats it like any other. */
        lv_obj_t *row = lv_obj_create(cont);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, AOS_SCREEN_W - 40, 56);
        lv_obj_t *hdr = make_header(row, pg->folder);
        lv_obj_set_width(hdr, AOS_SCREEN_W - 80);
        lv_obj_align(hdr, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void add_list(lv_obj_t *cont, page_t *pg, int i)
{
    const cell_t *c = &pg->cells[i];
    lv_obj_t *row = make_cell(cont, c);
    lv_obj_set_size(row, AOS_SCREEN_W - 40, ROW_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, 0);

    lv_obj_t *icon = make_icon(row, c, ICON_LIST);
    lv_obj_set_style_transform_scale(icon, 244, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(icon, ICON_LIST / 2, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_y(icon, ICON_LIST / 2, LV_STATE_PRESSED);

    const int32_t text_w = AOS_SCREEN_W - 40 - ICON_LIST - 40;
    if (c->app) {
        lv_obj_t *label = aos_label(row, cell_name(c), aos_font_title, AOS_C_TEXT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, text_w);
        return;
    }
    /* A folder says how much is inside, the one thing its icon cannot. */
    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, text_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = aos_label(col, cell_name(c), aos_font_title, AOS_C_TEXT);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, text_w);
    int n = aos_menu_folder_apps(c->folder, NULL, 0);
    char sub[32];
    snprintf(sub, sizeof(sub), "%d %s", n, n == 1 ? _("app") : _("apps"));
    aos_label(col, sub, aos_font_small, AOS_C_DIM);
}

static void setup_grid(lv_obj_t *cont, page_t *pg)
{
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW_WRAP);
    /* The rows start at the top. They were centred as a block, which with
     * more rows than fit opened the grid at its middle and left the first
     * apps above the top edge - harmless while the order was arbitrary, not
     * once it is the user's (menu.txt). */
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(cont, 18, 0);
    lv_obj_set_style_pad_column(cont, 14, 0);
    lv_obj_set_style_pad_top(cont, 56, 0);
    lv_obj_set_style_pad_bottom(cont, 40, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    if (pg->folder >= 0) {
        lv_obj_t *hdr = make_header(cont, pg->folder);
        lv_obj_set_width(hdr, AOS_SCREEN_W - 60);
        lv_obj_add_flag(hdr, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
}

static void add_grid(lv_obj_t *cont, page_t *pg, int i)
{
    const cell_t *c = &pg->cells[i];
    lv_obj_t *cell = make_cell(cont, c);
    if (i == 0 && pg->folder >= 0) {
        lv_obj_add_flag(cell, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    }
    lv_obj_set_size(cell, 106, 124);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, 8, 0);

    make_icon(cell, c, ICON_GRID);
    lv_obj_t *label = aos_label(cell, cell_name(c), aos_font_small, AOS_C_DIM);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    /* fixed one-line height: long names are clipped with an ellipsis
     * instead of splitting in two and knocking the grid out of line */
    lv_obj_set_size(label, 104, 20);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static void setup_honeycomb(lv_obj_t *cont, page_t *pg)
{
    lv_obj_set_scroll_dir(cont, LV_DIR_ALL);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(cont, honey_scroll_cb, LV_EVENT_SCROLL, NULL);

    /* hexagonal lattice: 3 columns per row, odd rows shifted half a cell,
     * centred across. Down, it is centred only while it fits: it used to be
     * centred always, which opened a long menu at its middle rows, and since
     * the order is the user's (menu.txt) the first rows are the ones that
     * must be on screen when it opens. The radial effect is measured from the
     * screen's centre either way. */
    const int per_row = 3;
    const int rows = (pg->count + per_row - 1) / per_row;

    const int32_t block_w = per_row * HONEY_STEP_X;
    const int32_t block_h = rows * HONEY_STEP_Y;
    pg->hx0 = (AOS_SCREEN_W - block_w) / 2 + (HONEY_STEP_X - ICON_HONEY) / 2
              - (rows > 1 ? HONEY_STEP_X / 4 : 0);
    pg->hy0 = (AOS_SCREEN_H - block_h) / 2;
    if (pg->hy0 < 44) {
        pg->hy0 = 44;                       /* the first row under the status bar */
    }

    if (pg->folder >= 0) {
        /* The name above the block, and the block no higher than leaves room
         * for it under the status bar. */
        if (pg->hy0 < 96) {
            pg->hy0 = 96;
        }
        lv_obj_t *hdr = make_header(cont, pg->folder);
        lv_obj_set_width(hdr, AOS_SCREEN_W - 60);
        lv_obj_set_pos(hdr, 30, pg->hy0 - 56);
    }
}

static void add_honeycomb(lv_obj_t *cont, page_t *pg, int i)
{
    const int per_row = 3;
    int row = i / per_row;
    int col = i % per_row;
    int32_t x = pg->hx0 + col * HONEY_STEP_X + (row % 2 ? HONEY_STEP_X / 2 : 0);
    int32_t y = pg->hy0 + row * HONEY_STEP_Y;

    lv_obj_t *cell = make_cell(cont, &pg->cells[i]);
    lv_obj_set_size(cell, ICON_HONEY, ICON_HONEY);
    lv_obj_set_pos(cell, x, y);
    make_icon(cell, &pg->cells[i], ICON_HONEY);
}

/* Adds up to n cells, then lets the style's effect see them. */
static void add_cells(lv_obj_t *cont, page_t *pg, int n)
{
    int end = pg->next + n;
    if (end > pg->count) {
        end = pg->count;
    }
    for (; pg->next < end; pg->next++) {
        switch (pg->style) {
        case AOS_LAUNCHER_GRID:      add_grid(cont, pg, pg->next);      break;
        case AOS_LAUNCHER_HONEYCOMB: add_honeycomb(cont, pg, pg->next); break;
        case AOS_LAUNCHER_LIST:
        default:                     add_list(cont, pg, pg->next);      break;
        }
    }
    /* apply the effect with the layout already resolved */
    lv_obj_update_layout(cont);
    lv_obj_send_event(cont, LV_EVENT_SCROLL, NULL);
}

static void page_timer_cb(lv_timer_t *t)
{
    lv_obj_t *cont = (lv_obj_t *)lv_timer_get_user_data(t);
    page_t *pg = (page_t *)lv_obj_get_user_data(cont);
    add_cells(cont, pg, CHUNK_CELLS);
    if (pg->next >= pg->count) {
        lv_timer_delete(t);
        pg->timer = NULL;
        aos_hal_log("ui", "page complete: %d cells, %u ms", pg->count,
                    (unsigned)(aos_hal_uptime_ms() - pg->t0));
    }
}

static void page_deleted_cb(lv_event_t *event)
{
    page_t *pg = (page_t *)lv_event_get_user_data(event);
    if (pg->timer) {
        lv_timer_delete(pg->timer);
    }
    free(pg->cells);
    free(pg);
}

/* A full-screen scrolling page with the cells laid out in the given style.
 * The cells are copied; the caller may free its array. */
static lv_obj_t *make_page(lv_obj_t *parent, aos_launcher_style_t style,
                           const cell_t *cells, int count, int folder)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_clip_corner(cont, true, 0);

    page_t *pg = calloc(1, sizeof(*pg));
    cell_t *copy = count > 0 ? malloc(sizeof(cell_t) * (size_t)count) : NULL;
    if (!pg || (count > 0 && !copy)) {
        free(pg);
        free(copy);
        return cont;                        /* an empty page beats a crash */
    }
    if (count > 0) {
        memcpy(copy, cells, sizeof(cell_t) * (size_t)count);
    }
    pg->style = style;
    pg->cells = copy;
    pg->count = count;
    pg->folder = folder;
    pg->t0 = aos_hal_uptime_ms();
    lv_obj_set_user_data(cont, pg);
    lv_obj_add_event_cb(cont, page_deleted_cb, LV_EVENT_DELETE, pg);

    switch (style) {
    case AOS_LAUNCHER_GRID:      setup_grid(cont, pg);      break;
    case AOS_LAUNCHER_HONEYCOMB: setup_honeycomb(cont, pg); break;
    case AOS_LAUNCHER_LIST:
    default:                     setup_list(cont, pg);      break;
    }

    add_cells(cont, pg, FIRST_CELLS);
    if (pg->next < pg->count) {
        pg->timer = lv_timer_create(page_timer_cb, 1, cont);
    }
    return cont;
}

static void frame_deleted_cb(lv_event_t *event)
{
    (void)event;
    s_frame = NULL;
    s_top = NULL;
    s_folder_page = NULL;
}

lv_obj_t *aos_launcher_create(lv_obj_t *parent, aos_launcher_style_t style)
{
    lv_obj_t *frame = lv_obj_create(parent);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(frame, 0, 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(frame, frame_deleted_cb, LV_EVENT_DELETE, NULL);
    s_frame = frame;
    s_built_style = style;
    s_folder_page = NULL;

    /* Over 1 KB: PSRAM on the board, and only for the build. */
    const int max = AOS_MAX_APPS + AOS_MENU_FOLDERS_MAX;
    aos_menu_item_t *items = malloc(sizeof(aos_menu_item_t) * (size_t)max);
    int count = items ? aos_menu_root(items, max) : 0;
    s_top = make_page(frame, style, items, count, -1);
    free(items);

    /* A folder that was open when the launcher went stale opens again,
     * without the animation: it never visibly closed. */
    int folder = aos_menu_folder_find(s_open_folder);
    if (folder >= 0) {
        open_folder(folder, false);
    } else {
        s_open_folder[0] = '\0';
    }
    return frame;
}

/* --------------------------------------------------------------------------
 * Folders
 * -------------------------------------------------------------------------- */

static void anim_x_cb(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

/* The top page stops being drawn once the folder covers it. */
static void folder_shown_cb(lv_anim_t *a)
{
    (void)a;
    if (s_top && s_folder_page) {
        lv_obj_add_flag(s_top, LV_OBJ_FLAG_HIDDEN);
    }
}

static void folder_gone_cb(lv_anim_t *a)
{
    lv_obj_delete((lv_obj_t *)a->var);
}

static void slide_x(lv_obj_t *obj, int32_t from, int32_t to, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, FOLDER_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

static void open_folder(int folder, bool animate)
{
    const aos_menu_folder_t *f = aos_menu_folder(folder);
    if (!s_frame || !f) {
        return;
    }
    if (s_folder_page) {
        lv_obj_delete(s_folder_page);
        s_folder_page = NULL;
    }

    cell_t *cells = malloc(sizeof(cell_t) * AOS_MAX_APPS);
    const aos_app_t **apps = malloc(sizeof(*apps) * AOS_MAX_APPS);
    int count = (cells && apps) ? aos_menu_folder_apps(folder, apps, AOS_MAX_APPS) : 0;
    for (int i = 0; i < count; i++) {
        cells[i].app = apps[i];
        cells[i].folder = -1;
    }
    uint64_t t0 = aos_hal_uptime_ms();
    s_folder_page = make_page(s_frame, s_built_style, cells, count, folder);
    aos_hal_log("ui", "folder %s opened: %d apps, %u ms", f->id, count,
                (unsigned)(aos_hal_uptime_ms() - t0));
    free(apps);
    free(cells);

    snprintf(s_open_folder, sizeof(s_open_folder), "%s", f->id);
    if (animate) {
        lv_obj_remove_flag(s_top, LV_OBJ_FLAG_HIDDEN);
        slide_x(s_folder_page, AOS_SCREEN_W, 0, folder_shown_cb);
    } else {
        lv_obj_add_flag(s_top, LV_OBJ_FLAG_HIDDEN);
    }
}

bool aos_launcher_close_folder(bool animate)
{
    s_open_folder[0] = '\0';
    if (!s_folder_page) {
        return false;
    }
    lv_obj_t *page = s_folder_page;
    s_folder_page = NULL;
    if (s_top) {
        lv_obj_remove_flag(s_top, LV_OBJ_FLAG_HIDDEN);
    }
    if (animate) {
        lv_anim_delete(page, anim_x_cb);
        slide_x(page, lv_obj_get_x(page), AOS_SCREEN_W, folder_gone_cb);
    } else {
        lv_obj_delete(page);
    }
    return true;
}
