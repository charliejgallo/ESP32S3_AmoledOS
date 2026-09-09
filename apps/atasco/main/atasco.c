/*
 * ATASCO
 *
 * A sliding block puzzle in the style of Rush Hour: the red car has to get out
 * of the car park to the right, but the way is full of other cars that only
 * slide along their own axis. 25 levels out of the box, of increasing and
 * verified difficulty (see tools/at_harness.c), and adding more means adding a
 * text map to at_levels.c: there is no code per level.
 *
 * How it is put together inside:
 *
 *   at_game   the rules: parsing the map, slide range, winning. It depends on
 *             neither LVGL nor the HAL (it is bench-tested with plain 'cc').
 *   at_cars   draws each car in code: a handful of lv_obj (body, cabin,
 *             wheels, an ornament depending on the model), never bitmaps.
 *   at_levels each level's text map.
 *
 * And what is left here is what joins them: two screens (level menu and board)
 * plus the victory panel, and the drag: a transparent layer over the board
 * (just like gemas) that follows the finger along the axis of the car touched
 * and only on release decides which cell it ended up in.
 */
#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"
#include "aos_theme.h"

#include "at_cars.h"
#include "at_game.h"
#include "at_levels.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Measurements
 * -------------------------------------------------------------------------- */

#define CELL        50
#define GAP         4
#define BOARD_PX    (AT_GRID * CELL)                  /* 300 */
#define BOARD_X     ((AOS_SCREEN_W - BOARD_PX) / 2)   /* 34  */
#define HEADER_H    56
#define BOARD_Y     (HEADER_H + 18)                   /* 74  */
#define WALL        10      /* thickness of the kerb around the lot */

/* The car park's palette. The asphalt is dark grey and not black: against the
 * system's pure black, the lot stands out by itself and the gap of the exit
 * reads without needing a border drawn around it. */
#define C_ASPHALT   0x3A3A3E
#define C_LANE      0x46464C      /* the target's lane, just a shade lighter */
#define C_WALL      0x9A9AA2      /* the kerb, lit side                       */
#define C_WALL_LO   0x6A6A72      /* ... and its shadow                       */
#define C_MARK      0xD2D2D8      /* the crosses marking the bays             */
#define C_HAZARD    0xFFC81F      /* the hazard tape of the gap               */

#define KEY_UNLOCKED "atasco_unlk"

typedef enum {
    ST_MENU = 0,
    ST_PLAY,
    ST_WIN,
} at_state_t;

typedef struct {
    aos_app_t *self;
    at_state_t state;

    /* --- game --- */
    at_board_t board;
    int        level;      /* 0-based index of the current level */
    int        moves;
    int        unlocked;   /* highest playable index (0-based); completing N unlocks N+1 */

    /* --- drag --- */
    bool dragging;
    int  drag_idx;
    int  drag_start_touch;  /* px or py at the moment of pressing */
    int  drag_start_pos;    /* col*CELL or row*CELL of the car at that moment */
    int  drag_fixed_px;     /* perpendicular coordinate, already with the half gap */
    int  drag_lo_px, drag_hi_px;
    int  drag_cur_px;

    /* --- menu --- */
    lv_obj_t  *menu_page;
    lv_obj_t  *menu_grid;
    lv_obj_t **level_btn;
    int        level_btn_n;

    /* --- board --- */
    lv_obj_t *play_page;
    lv_obj_t *lbl_title;
    lv_obj_t *board_area;
    lv_obj_t *lane;         /* the lane the target leaves through          */
    lv_obj_t *apron;        /* asphalt outside the gap, for the exit       */
    lv_obj_t *wall_r_top;   /* the right-hand wall, split in two by the    */
    lv_obj_t *wall_r_bot;   /* ... gap of the exit                         */
    lv_obj_t *hazard[6];    /* the yellow and black tape marking it        */
    lv_obj_t *touch;
    lv_obj_t *hint;
    lv_obj_t *car_view[AT_MAX_CARS];

    /* --- victory --- */
    lv_obj_t *win_dim;
    lv_obj_t *win_panel;
    lv_obj_t *lbl_win_title;
    lv_obj_t *lbl_win_info;
    lv_obj_t *btn_next;
} at_app_t;

/* --------------------------------------------------------------------------
 * UI helpers (same style as gemas/g2043: lv_obj by hand, no border, no
 * scrolling, large radii)
 * -------------------------------------------------------------------------- */

static lv_obj_t *panel_base(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Ornament: a plain rectangle that does NOT receive touches. In LVGL 9 every
 * lv_obj is born clickable, so without taking the flag off, every mark on the
 * floor would eat the touch meant for the drag layer. */
static lv_obj_t *decor(lv_obj_t *parent, uint32_t color, int32_t radius)
{
    lv_obj_t *o = panel_base(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

/* A stretch of the kerb around the lot. The gradient gives it the volume of
 * concrete without costing a layer. */
static lv_obj_t *wall(lv_obj_t *parent)
{
    lv_obj_t *w = decor(parent, C_WALL, 3);
    lv_obj_set_style_bg_grad_color(w, lv_color_hex(C_WALL_LO), 0);
    lv_obj_set_style_bg_grad_dir(w, LV_GRAD_DIR_VER, 0);
    return w;
}

static lv_obj_t *round_btn(lv_obj_t *parent, const char *symbol, uint32_t color,
                           lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *b = panel_base(parent);
    lv_obj_set_size(b, 36, 36);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, aos_font_small, 0);
    lv_obj_set_style_text_color(lbl, AOS_C_TEXT, 0);
    lv_obj_center(lbl);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static lv_obj_t *text_btn(lv_obj_t *parent, const char *text, uint32_t color,
                          lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *b = panel_base(parent);
    lv_obj_set_size(b, 138, 48);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    }
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, aos_font_body, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
    lv_obj_center(lbl);
    lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

/* --------------------------------------------------------------------------
 * Board: placing and (re)creating the cars
 * -------------------------------------------------------------------------- */

static void place_car_view(at_app_t *a, int i)
{
    const at_car_t *c = &a->board.cars[i];
    lv_obj_set_pos(a->car_view[i], c->col * CELL + GAP / 2, c->row * CELL + GAP / 2);
}

static void clear_car_views(at_app_t *a)
{
    for (int i = 0; i < AT_MAX_CARS; i++) {
        if (a->car_view[i]) {
            /* Before deleting the object its exit animation has to be removed
             * if it had one in flight: otherwise its finished callback would
             * run on a freed object. */
            lv_anim_delete(a->car_view[i], NULL);
            lv_obj_delete(a->car_view[i]);
            a->car_view[i] = NULL;
        }
    }
}

/* The exit is not always on the same row, so the gap in the wall, the lane and
 * the tape are repositioned when each level is loaded. */
static void layout_exit(at_app_t *a)
{
    const int row   = a->board.cars[a->board.target_idx].row;
    const int gap_y = BOARD_Y + row * CELL;     /* top edge of the gap */

    lv_obj_set_pos(a->lane, 0, row * CELL);
    lv_obj_set_size(a->lane, BOARD_PX, CELL);

    /* Asphalt on the other side of the gap: without this the car drives out
     * into the void. */
    lv_obj_set_pos(a->apron, BOARD_X + BOARD_PX, gap_y);
    lv_obj_set_size(a->apron, AOS_SCREEN_W - (BOARD_X + BOARD_PX), CELL);

    /* The right-hand wall reaches the gap and carries on after it. */
    lv_obj_set_pos(a->wall_r_top, BOARD_X + BOARD_PX, BOARD_Y - WALL);
    lv_obj_set_size(a->wall_r_top, WALL, row * CELL + WALL);

    const int bot_y = gap_y + CELL;
    lv_obj_set_pos(a->wall_r_bot, BOARD_X + BOARD_PX, bot_y);
    lv_obj_set_size(a->wall_r_bot, WALL, (BOARD_Y + BOARD_PX + WALL) - bot_y);

    /* Three stretches of tape on either side of the gap, alternating yellow
     * and black: it is what makes it obvious where the car has to come out. */
    const int seg = 10;
    for (int k = 0; k < 3; k++) {
        lv_obj_t *up = a->hazard[k];
        lv_obj_set_style_bg_color(up, lv_color_hex((k % 2) ? 0x1C1C1E : C_HAZARD), 0);
        lv_obj_set_pos(up, BOARD_X + BOARD_PX, gap_y - (k + 1) * seg);
        lv_obj_set_size(up, WALL, seg);

        lv_obj_t *dn = a->hazard[3 + k];
        lv_obj_set_style_bg_color(dn, lv_color_hex((k % 2) ? 0x1C1C1E : C_HAZARD), 0);
        lv_obj_set_pos(dn, BOARD_X + BOARD_PX, gap_y + CELL + k * seg);
        lv_obj_set_size(dn, WALL, seg);
    }
}

static void load_level(at_app_t *a, int idx)
{
    int n = at_level_count();
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    a->level = idx;
    a->moves = 0;

    clear_car_views(a);
    const at_level_t *lvl = at_level_get(idx);
    at_parse_level(lvl->rows, &a->board);

    for (int i = 0; i < a->board.count; i++) {
        a->car_view[i] = at_car_view_create(a->board_area, &a->board.cars[i], CELL, GAP);
        place_car_view(a, i);
    }
    lv_obj_move_foreground(a->touch);
    layout_exit(a);

    lv_obj_remove_flag(a->hint, LV_OBJ_FLAG_HIDDEN);
    a->dragging = false;
}

static void update_hud(at_app_t *a)
{
    char buf[64];
    snprintf(buf, sizeof(buf), _("NIVEL %d/%d - Movs %d"),
             a->level + 1, at_level_count(), a->moves);
    lv_label_set_text(a->lbl_title, buf);
}

/* --------------------------------------------------------------------------
 * Navigation between screens
 * -------------------------------------------------------------------------- */

static void refresh_menu_tiles(at_app_t *a)
{
    for (int i = 0; i < a->level_btn_n; i++) {
        lv_obj_t *btn = a->level_btn[i];
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);

        uint32_t line;      /* the painted line of the bay */
        if (i > a->unlocked) {
            line = 0x4A4A52;                /* locked      */
        } else if (i < a->unlocked) {
            line = 0x30D158;                /* completed   */
        } else {
            line = C_HAZARD;                /* the one you touch */
        }
        lv_obj_set_style_border_color(btn, lv_color_hex(line), 0);
        lv_obj_set_style_border_width(btn, (i == a->unlocked) ? 3 : 2, 0);

        char buf[12];
        if (i > a->unlocked) {
            lv_label_set_text(lbl, LV_SYMBOL_EYE_CLOSE);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x6A6A72), 0);
        } else {
            snprintf(buf, sizeof(buf), "%d", i + 1);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, lv_color_hex(line), 0);
        }
    }
}

static void show_menu(at_app_t *a)
{
    a->dragging = false;
    a->state    = ST_MENU;
    lv_obj_add_flag(a->win_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->win_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->play_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->menu_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->menu_page);
    refresh_menu_tiles(a);
}

static void show_play(at_app_t *a)
{
    a->state = ST_PLAY;
    lv_obj_add_flag(a->menu_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->win_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->win_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->play_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->play_page);
    update_hud(a);
}

static void start_level(at_app_t *a, int idx)
{
    load_level(a, idx);
    show_play(a);
}

static void show_win(at_app_t *a)
{
    if (a->level + 1 > a->unlocked) {
        a->unlocked = a->level + 1;
        if (a->unlocked > at_level_count()) {
            a->unlocked = at_level_count();
        }
        aos_hal_pref_set_i32(KEY_UNLOCKED, (int32_t)a->unlocked);
    }

    bool last = (a->level + 1 >= at_level_count());
    lv_label_set_text(a->lbl_win_title, last ? _("Estacionamiento despejado")
                                             : _("Auto afuera"));
    char buf[64];
    /* Two whole phrases and not "movimiento%s" with the "s" glued on
     * separately: that concatenation is SPANISH's plural rule put into the
     * code. This way the translator receives both complete forms and builds
     * each one as their language requires. */
    snprintf(buf, sizeof(buf),
             a->moves == 1 ? _("Nivel %d completado en %d movimiento")
                           : _("Nivel %d completado en %d movimientos"),
             a->level + 1, a->moves);
    lv_label_set_text(a->lbl_win_info, buf);
    if (last) {
        lv_obj_add_flag(a->btn_next, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(a->btn_next, LV_OBJ_FLAG_HIDDEN);
    }

    a->state = ST_WIN;
    lv_obj_remove_flag(a->win_dim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(a->win_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->win_dim);
    lv_obj_move_foreground(a->win_panel);
    aos_hal_beep(1500, 40);
    aos_hal_beep(2000, 70);
}

/* --------------------------------------------------------------------------
 * Drag: a transparent layer over the board, just like gemas. It follows the
 * finger ONLY along the axis of the car touched; the valid range is computed
 * once, on pressing, because the other cars do not move during the drag.
 *
 * Split out of touch_event() into three pure functions (they do not touch LVGL
 * except to move the car) so they can be tested from ATASCO_SELFTEST without
 * needing a real indev: see the development switch at the end of the file.
 * -------------------------------------------------------------------------- */

static void drag_press(at_app_t *a, int idx, int touch_axis)
{
    const at_car_t *c = &a->board.cars[idx];
    int lo, hi;
    at_slide_range(&a->board, idx, &lo, &hi);

    a->drag_idx         = idx;
    a->drag_lo_px       = lo * CELL;
    a->drag_hi_px       = hi * CELL;
    a->drag_start_pos   = (c->horizontal ? c->col : c->row) * CELL;
    a->drag_cur_px      = a->drag_start_pos;
    a->drag_start_touch = touch_axis;
    a->drag_fixed_px    = (c->horizontal ? c->row : c->col) * CELL + GAP / 2;
    a->dragging         = true;
}

static void drag_move(at_app_t *a, int touch_axis)
{
    if (!a->dragging) {
        return;
    }
    const at_car_t *c = &a->board.cars[a->drag_idx];
    int delta = touch_axis - a->drag_start_touch;
    int pos   = a->drag_start_pos + delta;
    if (pos < a->drag_lo_px) pos = a->drag_lo_px;
    if (pos > a->drag_hi_px) pos = a->drag_hi_px;
    a->drag_cur_px = pos;

    int ox = c->horizontal ? pos + GAP / 2 : a->drag_fixed_px;
    int oy = c->horizontal ? a->drag_fixed_px : pos + GAP / 2;
    lv_obj_set_pos(a->car_view[a->drag_idx], ox, oy);
}

static void anim_x_cb(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void drive_out_done(lv_anim_t *anim)
{
    at_app_t *a = lv_anim_get_user_data(anim);
    /* If the player went to the menu while the car was leaving, there is
     * nothing to celebrate: the victory panel would appear over the menu. */
    if (a->state == ST_WIN) {
        show_win(a);
    }
}

/* The car drives out through the gap and only then is the level declared won.
 *
 * The object's x position is animated, which LVGL resolves by moving the blit;
 * no transformations, which would be a layer (docs/DECISIONES.md). The board
 * has LV_OBJ_FLAG_OVERFLOW_VISIBLE precisely so the car goes on being drawn
 * once it has passed the edge. */
static void drive_out(at_app_t *a)
{
    lv_obj_t *car = a->car_view[a->board.target_idx];

    /* No more playing: while it is leaving, touches must not move anything. */
    a->state = ST_WIN;
    lv_obj_add_flag(a->hint, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, car);
    lv_anim_set_exec_cb(&an, anim_x_cb);
    lv_anim_set_values(&an, lv_obj_get_x(car), AOS_SCREEN_W - BOARD_X + 12);
    lv_anim_set_duration(&an, 420);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&an, drive_out_done);
    lv_anim_set_user_data(&an, a);
    lv_anim_start(&an);

    aos_hal_beep(900, 25);
}

/* Returns true if the car ended up in a different cell from the one it had. */
static bool drag_release(at_app_t *a)
{
    if (!a->dragging) {
        return false;
    }
    a->dragging = false;

    int  snapped = (a->drag_cur_px + CELL / 2) / CELL;
    bool moved   = at_apply_move(&a->board, a->drag_idx, snapped);
    place_car_view(a, a->drag_idx);

    if (moved) {
        a->moves++;
        update_hud(a);
        lv_obj_add_flag(a->hint, LV_OBJ_FLAG_HIDDEN);
        aos_hal_beep(1200, 18);
    }

    /* Arriving flush against the exit's wall already counts as having got out:
     * to get that far the way must have been clear. See at_target_at_exit(),
     * which explains why at_is_solved() is not enough. */
    if (a->drag_idx == a->board.target_idx && at_target_at_exit(&a->board)) {
        drive_out(a);
    }
    return moved;
}

static void touch_event(lv_event_t *event)
{
    at_app_t      *a    = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (a->state != ST_PLAY) {
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (getenv("ATASCO_DEBUG_TOUCH")) {
        printf("[ATASCO_TOUCH] code=%d point=(%d,%d) dragging=%d idx=%d\n",
               (int)code, (int)p.x, (int)p.y, a->dragging, a->drag_idx);
    }
    lv_area_t area;
    lv_obj_get_coords(a->touch, &area);
    int px = p.x - area.x1;
    int py = p.y - area.y1;

    if (code == LV_EVENT_PRESSED) {
        a->dragging = false;
        if (px < 0 || py < 0 || px >= BOARD_PX || py >= BOARD_PX) {
            return;
        }
        int idx = at_car_at(&a->board, py / CELL, px / CELL);
        if (idx < 0) {
            return;
        }
        const at_car_t *c = &a->board.cars[idx];
        drag_press(a, idx, c->horizontal ? px : py);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        drag_release(a);
        return;
    }

    /* LV_EVENT_PRESSING */
    if (!a->dragging) {
        return;
    }
    const at_car_t *c = &a->board.cars[a->drag_idx];
    drag_move(a, c->horizontal ? px : py);
}

/* --------------------------------------------------------------------------
 * Button callbacks
 * -------------------------------------------------------------------------- */

static void cb_restart(lv_event_t *e)
{
    at_app_t *a = lv_event_get_user_data(e);
    load_level(a, a->level);
    update_hud(a);
}

static void cb_open_menu(lv_event_t *e)
{
    show_menu((at_app_t *)lv_event_get_user_data(e));
}

static void cb_win_menu(lv_event_t *e)
{
    show_menu((at_app_t *)lv_event_get_user_data(e));
}

static void cb_win_next(lv_event_t *e)
{
    at_app_t *a = lv_event_get_user_data(e);
    start_level(a, a->level + 1);
}

static void cb_level_tile(lv_event_t *e)
{
    at_app_t *a   = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx > a->unlocked) {
        aos_hal_beep(300, 35);
        return;
    }
    start_level(a, idx);
}

/* --------------------------------------------------------------------------
 * Building the screens
 * -------------------------------------------------------------------------- */

static void build_menu_page(at_app_t *a, lv_obj_t *root)
{
    a->menu_page = panel_base(root);
    lv_obj_set_size(a->menu_page, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->menu_page, 0, 0);

    /* The header is a strip of asphalt with the yellow line painted along the
     * bottom: the menu has to belong to the same world as the board, not be
     * just any list of buttons. */
    lv_obj_t *sign = decor(a->menu_page, C_ASPHALT, 16);
    lv_obj_set_size(sign, AOS_SCREEN_W - 28, 88);
    lv_obj_set_pos(sign, 14, 14);

    lv_obj_t *title = lv_label_create(sign);
    lv_label_set_text(title, _("ATASCO"));
    lv_obj_set_style_text_font(title, aos_font_title, 0);
    lv_obj_set_style_text_color(title, AOS_C_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *sub = lv_label_create(sign);
    lv_label_set_text(sub, _("saca el auto rojo"));
    lv_obj_set_style_text_font(sub, aos_font_small, 0);
    lv_obj_set_style_text_color(sub, AOS_C_DIM, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 48);

    for (int k = 0; k < 7; k++) {
        lv_obj_t *dash = decor(sign, C_HAZARD, 2);
        lv_obj_set_size(dash, 22, 4);
        lv_obj_set_pos(dash, 16 + k * 46, 76);
    }

    a->menu_grid = lv_obj_create(a->menu_page);
    lv_obj_remove_style_all(a->menu_grid);
    lv_obj_set_style_bg_opa(a->menu_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_size(a->menu_grid, 336, AOS_SCREEN_H - 122);
    lv_obj_align(a->menu_grid, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_flex_flow(a->menu_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(a->menu_grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(a->menu_grid, 8, 0);
    lv_obj_set_style_pad_row(a->menu_grid, 8, 0);

    int n = at_level_count();
    a->level_btn   = lv_malloc_zeroed(sizeof(lv_obj_t *) * (size_t)n);
    a->level_btn_n = n;
    for (int i = 0; i < n; i++) {
        /* Each level is a bay: asphalt with the line painted around it. The
         * colour of that line is what says whether it is played, available or
         * still locked. */
        lv_obj_t *btn = panel_base(a->menu_grid);
        lv_obj_set_size(btn, 56, 56);
        lv_obj_set_style_radius(btn, 12, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_ASPHALT), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 2, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, cb_level_tile, LV_EVENT_CLICKED, a);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, aos_font_body, 0);
        lv_obj_center(lbl);
        lv_obj_remove_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        a->level_btn[i] = btn;
    }
}

static void build_play_page(at_app_t *a, lv_obj_t *root)
{
    a->play_page = panel_base(root);
    lv_obj_set_size(a->play_page, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->play_page, 0, 0);
    lv_obj_add_flag(a->play_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_menu = round_btn(a->play_page, LV_SYMBOL_LIST, 0x2C2C2E, cb_open_menu, a);
    lv_obj_set_pos(btn_menu, 10, 10);

    lv_obj_t *btn_restart = round_btn(a->play_page, LV_SYMBOL_REFRESH, 0x2C2C2E,
                                      cb_restart, a);
    lv_obj_set_pos(btn_restart, AOS_SCREEN_W - 36 - 10, 10);

    a->lbl_title = lv_label_create(a->play_page);
    lv_obj_set_style_text_font(a->lbl_title, aos_font_body, 0);
    lv_obj_set_style_text_color(a->lbl_title, AOS_C_TEXT, 0);
    lv_obj_set_style_text_align(a->lbl_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a->lbl_title, 220);
    lv_obj_set_pos(a->lbl_title, (AOS_SCREEN_W - 220) / 2, 18);

    /* The asphalt outside the gap. It goes BEFORE the board so it ends up
     * underneath: the target, on leaving, has to pass over it. */
    a->apron = decor(a->play_page, C_ASPHALT, 0);

    a->board_area = panel_base(a->play_page);
    lv_obj_set_size(a->board_area, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(a->board_area, BOARD_X, BOARD_Y);
    lv_obj_set_style_bg_color(a->board_area, lv_color_hex(C_ASPHALT), 0);
    lv_obj_set_style_bg_opa(a->board_area, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(a->board_area, 6, 0);
    /* Without this the departing car is clipped right at the board's edge:
     * LVGL clips children to the parent unless told otherwise, and that was
     * why the red car "half came out". */
    lv_obj_add_flag(a->board_area, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* The target's lane: a strip just a shade lighter, so it is clear where it
     * has to come out. */
    a->lane = decor(a->board_area, C_LANE, 0);

    /* The bays' crosses, at the grid's interior intersections. They are
     * created ONCE (not per level): they are 50 objects and rebuilding them on
     * every load would cost more than all the cars together. */
    for (int r = 1; r < AT_GRID; r++) {
        for (int c = 1; c < AT_GRID; c++) {
            lv_obj_t *hbar = decor(a->board_area, C_MARK, 0);
            lv_obj_set_size(hbar, 13, 2);
            lv_obj_set_pos(hbar, c * CELL - 6, r * CELL - 1);

            lv_obj_t *vbar = decor(a->board_area, C_MARK, 0);
            lv_obj_set_size(vbar, 2, 13);
            lv_obj_set_pos(vbar, c * CELL - 1, r * CELL - 6);
        }
    }

    /* The kerb around the lot: four stretches, with the right-hand one split
     * in two to leave the gap of the exit (layout_exit sizes it). */
    lv_obj_t *wall_top = wall(a->play_page);
    lv_obj_set_pos(wall_top, BOARD_X - WALL, BOARD_Y - WALL);
    lv_obj_set_size(wall_top, BOARD_PX + 2 * WALL, WALL);

    lv_obj_t *wall_bot = wall(a->play_page);
    lv_obj_set_pos(wall_bot, BOARD_X - WALL, BOARD_Y + BOARD_PX);
    lv_obj_set_size(wall_bot, BOARD_PX + 2 * WALL, WALL);

    lv_obj_t *wall_left = wall(a->play_page);
    lv_obj_set_pos(wall_left, BOARD_X - WALL, BOARD_Y - WALL);
    lv_obj_set_size(wall_left, WALL, BOARD_PX + 2 * WALL);

    a->wall_r_top = wall(a->play_page);
    a->wall_r_bot = wall(a->play_page);

    for (int k = 0; k < 6; k++) {
        a->hazard[k] = decor(a->play_page, C_HAZARD, 0);
    }

    a->touch = panel_base(a->board_area);
    lv_obj_set_size(a->touch, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_set_style_bg_opa(a->touch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESS_LOST, a);

    a->hint = lv_label_create(a->play_page);
    lv_label_set_text(a->hint, _("Desliza los autos para sacar al rojo"));
    lv_obj_set_style_text_font(a->hint, aos_font_small, 0);
    lv_obj_set_style_text_color(a->hint, AOS_C_DIM, 0);
    lv_obj_set_style_text_align(a->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a->hint, BOARD_PX);
    lv_obj_set_pos(a->hint, BOARD_X, BOARD_Y + BOARD_PX + 14);
}

static void build_win_overlay(at_app_t *a, lv_obj_t *root)
{
    a->win_dim = panel_base(root);
    lv_obj_set_size(a->win_dim, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->win_dim, 0, 0);
    lv_obj_set_style_bg_color(a->win_dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->win_dim, LV_OPA_60, 0);
    lv_obj_add_flag(a->win_dim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    a->win_panel = panel_base(root);
    lv_obj_set_size(a->win_panel, 300, 190);
    lv_obj_align(a->win_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(a->win_panel, lv_color_hex(0x1C1C1E), 0);
    lv_obj_set_style_bg_opa(a->win_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(a->win_panel, 26, 0);
    lv_obj_add_flag(a->win_panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);

    a->lbl_win_title = lv_label_create(a->win_panel);
    lv_obj_set_style_text_font(a->lbl_win_title, aos_font_body, 0);
    lv_obj_set_style_text_color(a->lbl_win_title, AOS_C_GREEN, 0);
    lv_obj_set_style_text_align(a->lbl_win_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a->lbl_win_title, 260);
    lv_obj_set_pos(a->lbl_win_title, 20, 22);

    a->lbl_win_info = lv_label_create(a->win_panel);
    lv_obj_set_style_text_font(a->lbl_win_info, aos_font_small, 0);
    lv_obj_set_style_text_color(a->lbl_win_info, AOS_C_DIM, 0);
    lv_obj_set_style_text_align(a->lbl_win_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(a->lbl_win_info, 260);
    lv_obj_set_pos(a->lbl_win_info, 20, 66);

    lv_obj_t *btn_menu = text_btn(a->win_panel, _("Menu"), 0x8E8E93, cb_win_menu, a);
    lv_obj_set_pos(btn_menu, 14, 122);
    a->btn_next = text_btn(a->win_panel, _("Siguiente"), 0x30D158, cb_win_next, a);
    lv_obj_set_pos(a->btn_next, 148, 122);
}

/* --------------------------------------------------------------------------
 * The app's life cycle
 * -------------------------------------------------------------------------- */

static bool at_back(aos_app_t *self, void *inst)
{
    (void)self;
    at_app_t *a = inst;
    if (a->state == ST_PLAY || a->state == ST_WIN) {
        show_menu(a);
        return true;
    }
    return false;   /* in the menu: let the system close the app */
}

static void at_tick(aos_app_t *self, void *inst)
{
    (void)self;
    at_app_t *a = inst;
    /* Drain the touch chip's gesture even though we do not use it: if nobody
     * reads it, it stays pending and reappears as a phantom gesture on leaving
     * the app (see docs/HANDOFF-APPS.md, the traps section). */
    aos_ui_take_gesture();
    (void)a;
}

/* --------------------------------------------------------------------------
 * Development switches (see docs/HANDOFF-APPS.md): on the board getenv()
 * always returns NULL and they do no harm. They are for testing without
 * uncovering the menu by hand on every simulator run.
 *
 *   ATASCO_LEVEL=N     starts straight on level N (1-based), unlocked
 *   ATASCO_AUTOWIN=1   forces the target out as soon as it loads (to see the
 *                      victory panel without solving anything)
 *   ATASCO_SELFTEST=1  runs a few drag calculations against known values and
 *                      prints them, without depending on an indev
 * -------------------------------------------------------------------------- */

static void run_selftest(at_app_t *a)
{
    bool ok = true;
    int  lo, hi, col;

    load_level(a, 0);   /* level 1: an empty board except for the target */
    at_slide_range(&a->board, a->board.target_idx, &lo, &hi);
    printf("[ATASCO_SELFTEST] nivel1 lo=%d hi=%d (esperado 0,%d)\n", lo, hi, AT_GRID);
    ok = ok && lo == 0 && hi == AT_GRID;

    /* The test that matters, and the one that was missing: a drag of the
     * length that REALLY fits on the screen. The finger presses on the car
     * (20 px) and reaches 230, that is, 210 px of travel; for 'col' to reach
     * AT_GRID you would have to drag the board's whole 300 px, which do not
     * exist on a 368 screen. Before at_target_at_exit(), this left the car
     * flush against the wall with the level uncompleted: it is the "half comes
     * out". */
    a->state = ST_PLAY;
    drag_press(a, a->board.target_idx, 20);
    drag_move(a, 230);
    bool moved = drag_release(a);
    col = a->board.cars[a->board.target_idx].col;
    printf("[ATASCO_SELFTEST] nivel1 arrastre de 210 px: col=%d moved=%d "
           "en_salida=%d estado=%d (esperado 4,1,1,%d)\n",
           col, moved, at_target_at_exit(&a->board), a->state, ST_WIN);
    ok = ok && moved && col == 4 && at_target_at_exit(&a->board) && a->state == ST_WIN;

    load_level(a, 1);   /* level 2: a vertical blocker stops the target */
    at_slide_range(&a->board, a->board.target_idx, &lo, &hi);
    printf("[ATASCO_SELFTEST] nivel2 lo=%d hi=%d (esperado 0,2)\n", lo, hi);
    ok = ok && lo == 0 && hi == 2;

    drag_press(a, a->board.target_idx, 0);
    drag_move(a, 10000);
    moved = drag_release(a);
    col = a->board.cars[a->board.target_idx].col;
    printf("[ATASCO_SELFTEST] nivel2 tras chocar contra el bloqueador: col=%d "
           "solved=%d (esperado 2,0)\n", col, at_is_solved(&a->board));
    ok = ok && moved && col == 2 && !at_is_solved(&a->board);

    printf("[ATASCO_SELFTEST] %s\n", ok ? "ALL OK" : "FAILED");
}

static void *at_create(aos_app_t *self, lv_obj_t *root)
{
    at_app_t *a = lv_malloc_zeroed(sizeof(at_app_t));
    if (!a) {
        return NULL;
    }
    a->self = self;

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);

    build_menu_page(a, root);
    build_play_page(a, root);
    build_win_overlay(a, root);

    int32_t unlocked = 0;
    aos_hal_pref_get_i32(KEY_UNLOCKED, &unlocked);
    if (unlocked < 0) unlocked = 0;
    /* 'unlocked' == at_level_count() is a valid state: it means they have all
     * been completed, not that one is left to "unlock". */
    if (unlocked > at_level_count()) unlocked = at_level_count();
    a->unlocked = (int)unlocked;

    const char *selftest = getenv("ATASCO_SELFTEST");
    if (selftest && selftest[0]) {
        run_selftest(a);
    }

    const char *lvl_env = getenv("ATASCO_LEVEL");
    if (lvl_env && lvl_env[0]) {
        int want = atoi(lvl_env) - 1;
        if (want < 0) want = 0;
        if (want > at_level_count() - 1) want = at_level_count() - 1;
        if (want > a->unlocked) {
            a->unlocked = want;
        }
        start_level(a, want);
        const char *autowin = getenv("ATASCO_AUTOWIN");
        if (autowin && autowin[0]) {
            /* It takes it out by the same path as the player -up to the wall
             * and from there the animation- so the switch tests what is really
             * used and not a shortcut that could go on working with the game
             * broken. */
            int len = a->board.cars[a->board.target_idx].len;
            at_apply_move(&a->board, a->board.target_idx, AT_GRID - len);
            place_car_view(a, a->board.target_idx);
            if (at_target_at_exit(&a->board)) {
                drive_out(a);
            } else {
                show_win(a);    /* the way was blocked from the start */
            }
        }
    } else {
        show_menu(a);
    }
    return a;
}

static void at_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    at_app_t *a = inst;

    /* If the app closes while the car was leaving, the animation has to die
     * HERE: its finished callback uses the context we are about to free. */
    for (int i = 0; i < AT_MAX_CARS; i++) {
        if (a->car_view[i]) {
            lv_anim_delete(a->car_view[i], NULL);
        }
    }
    if (a->level_btn) {
        lv_free(a->level_btn);
    }
    lv_free(a);
}

static bool at_init(aos_app_t *app)
{
    app->desc.id       = "demo.atasco";
    app->desc.name     = "Atasco";
    app->desc.icon     = "A";
    app->desc.icon_vec = AOS_ICON_GAMEPAD;
    app->desc.color_a  = 0xFF453A;
    app->desc.color_b  = 0x48484A;
    app->desc.order    = 148;
    app->desc.flags    = AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_LONG_DRAG;

    app->create  = at_create;
    app->destroy = at_destroy;
    app->back    = at_back;
    app->tick    = at_tick;
    return true;
}

AOS_APP_ENTRY(at_init);
