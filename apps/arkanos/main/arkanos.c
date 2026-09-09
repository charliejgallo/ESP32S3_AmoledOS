/*
 * ARKANOS - brick breaking for AmoledOS
 *
 * An homage to Arkanoid: paddle, ball, twelve walls and falling capsules. It
 * builds two ways from the same source:
 *
 *   .so for the board            ./tools/build_apps.sh arkanos
 *   built-in app of the simulator   the simulator builds it (AOS_SIM_BUILTIN)
 *
 * Controls: on opening it asks whether you play by dragging your finger or by
 * tilting the board. The side button launches the ball and, with the LASER
 * upgrade, fires. The bar at the top is the pause button.
 *
 * On the drawing, which is what is different about it: the game is painted
 * into a 184x224 buffer and upscaled by hand to 368x448, like 2043 and
 * claudito, because leaving the stretch to LVGL costs 129 ms per frame. But it
 * also carries a list of dirty rectangles, so on a normal frame a few thousand
 * pixels are upscaled and invalidated instead of the screen's 165 thousand. An
 * Arkanoid is the ideal case for that: the wall stands still and the only
 * things moving are the ball, the paddle and the odd capsule. See ak_draw.c.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include "arkanos.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_HI      "ak_hi"
#define KEY_CTRL    "ak_ctrl"
#define KEY_SFX     "ak_sfx"
#define KEY_FPS     "ak_fps"
#define KEY_AXIS    "ak_axis"

static bool s_sfx = true;

void ak_sfx(int freq_hz, int ms)
{
    if (s_sfx) {
        aos_hal_beep(freq_hz, ms);
    }
}

typedef struct {
    ak_t        g;
    uint16_t   *fbmem;      /* the frame, 184x224 */
    uint16_t   *bgmem;      /* the background with the bricks, 184x224 */
    uint16_t   *big;        /* upscaled x2: it is what the canvas sees */

    lv_obj_t   *canvas;
    lv_obj_t   *touch;
    lv_obj_t   *pausebtn;
    lv_timer_t *timer;

    int16_t     period;     /* current period of the timer, in ms */
    int16_t     real_ms;    /* how long a frame really takes */
    int16_t     tune_t;
    uint16_t    frames;
    uint64_t    prev_ms;

    /* panels */
    lv_obj_t   *title;
    lv_obj_t   *title_hi;
    lv_obj_t   *chip_sfx;
    lv_obj_t   *chip_fps;
    lv_obj_t   *chip_axis;
    lv_obj_t   *pause;
    lv_obj_t   *chip_sfx2;
    lv_obj_t   *chip_fps2;
    lv_obj_t   *chip_axis2;
    lv_obj_t   *over;
    lv_obj_t   *over_title;
    lv_obj_t   *over_score;
    lv_obj_t   *over_go;        /* the green button: retry or new lap */
    uint8_t     over_shown;
    uint8_t     won;

    bool        want_exit;
} app_t;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Flush to the screen
 *
 * Here is the heart of the matter. Everything the game draws goes through
 * these three steps and none of them touches the whole screen unless it has
 * to.
 * -------------------------------------------------------------------------- */

static void push_rect(app_t *a, const ak_rect_t *r)
{
    ak_expand(a->fbmem, a->big, r);

    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);

    lv_area_t area;
    area.x1 = co.x1 + r->x0 * AK_SCALE;
    area.y1 = co.y1 + r->y0 * AK_SCALE;
    area.x2 = co.x1 + r->x1 * AK_SCALE - 1;
    area.y2 = co.y1 + r->y1 * AK_SCALE - 1;
    lv_obj_invalidate_area(a->canvas, &area);
}

static const ak_rect_t ak_entera = { 0, 0, AK_W, AK_H };

/* The whole screen, for when there is nothing to save: on opening the app and
 * on coming back from a panel that covered it entirely. */
static void push_all(app_t *a)
{
    push_rect(a, &ak_entera);
    a->g.last_area = 100;
}

static void present(app_t *a)
{
    ak_t *g = &a->g;

    /* 1. restore from the background whatever we dirtied last frame, plus
     *    whatever changed in the background itself (broken or worn bricks) */
    g->d_push = g->d_prev;
    ak_dirty_join(&g->d_push, &g->d_bg);

    if (g->d_push.all) {
        ak_restore(a->fbmem, a->bgmem, &ak_entera);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            ak_restore(a->fbmem, a->bgmem, &g->d_push.r[i]);
        }
    }

    /* 2. draw what moves; fills d_cur */
    ak_draw_movers(g);

    /* 3. the score, only if a number changed. It restores and records itself,
     *    and does not enter the next frame's list: nothing moves in that
     *    strip, so what was written still holds. */
    if (g->hud_dirty) {
        ak_draw_hud(g);
    }

    /* 4. upscale and invalidate the union */
    ak_dirty_join(&g->d_push, &g->d_cur);

    if (g->d_push.all) {
        push_all(a);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            push_rect(a, &g->d_push.r[i]);
        }
        g->last_area = (uint16_t)(ak_dirty_area(&g->d_push) * 100 / (AK_W * AK_H));
    }

    g->d_prev = g->d_cur;
    ak_dirty_reset(&g->d_bg);
}

/* --------------------------------------------------------------------------
 * Pace
 *
 * The same as in 2043: if the frame comes out more expensive than the period,
 * the timer is always overdue, the LVGL task never gets to sleep and the
 * watchdog fires. Rather than hang the board, the game relaxes. The difference
 * is that here the floor is 33 ms because LVGL does not refresh more often
 * than that.
 * -------------------------------------------------------------------------- */
static void period_tune(app_t *a)
{
    int want = a->period;

    if (a->real_ms > want + want / 3) {
        want = a->real_ms;
    } else if (a->real_ms <= want + 2 && want > AK_FRAME_MS) {
        want -= 6;
    }
    want = clampi(want, AK_FRAME_MS, AK_FRAME_MAX);

    if (want != a->period) {
        aos_hal_log("arkanos", "cuadro real %d ms: periodo %d -> %d ms (%d.%d fps, %u%% de pantalla)",
                    a->real_ms, a->period, want, a->g.fps10 / 10, a->g.fps10 % 10,
                    (unsigned)a->g.last_area);
        a->period = (int16_t)want;
        lv_timer_set_period(a->timer, (uint32_t)want);
    }
}

/* --------------------------------------------------------------------------
 * LVGL panels
 *
 * The menu, the pause and the ending are built with LVGL objects and not with
 * the 5x7 font: they are screens to read and touch, and there vector text
 * wins. The game itself is all canvas.
 * -------------------------------------------------------------------------- */

static void overlay_hide_all(app_t *a)
{
    lv_obj_t *const panels[] = { a->title, a->pause, a->over };
    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++) {
        if (panels[i]) {
            lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void overlay_show(app_t *a, lv_obj_t *panel)
{
    overlay_hide_all(a);
    if (panel) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, w, h);
    lv_obj_center(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);      /* so touches do not pass through */
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_y(l, y);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, const char *sub,
                             int x, int y, int w, int h, uint32_t accent,
                             lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C24), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* The press highlight goes by colour and not by transform_scale: scaling
     * forces LVGL to build a separate layer, and a layer that does not fit in
     * memory is a hang, not a slowdown. It is in DECISIONES. */
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, sub ? &aos_montserrat_20 : &aos_montserrat_16, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_align(l, sub ? LV_ALIGN_TOP_MID : LV_ALIGN_CENTER, 0, sub ? 10 : 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);

    if (sub) {
        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_font(s, &aos_montserrat_14, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(0x9AA3B8), 0);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s, lv_pct(100));
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    }
    return b;
}

/* Chips: flat buttons whose text states the setting. There is no room for an
 * LVGL switch with its label beside it. */
static void chip_set(lv_obj_t *chip, const char *text, bool on)
{
    if (!chip) {
        return;
    }
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(on ? 0x0A0A12 : 0x9AA3B8), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(on ? 0x30D158 : 0x1C1C24), 0);
}

/* A chip that is not a yes/no but a value that cycles. It goes in blue so it
 * does not read as "on" beside the real switches. */
static void chip_set_value(lv_obj_t *chip, const char *text)
{
    if (!chip) {
        return;
    }
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x0A4A8F), 0);
}

static lv_obj_t *make_chip(lv_obj_t *parent, int x, int y, int w,
                           lv_event_cb_t cb, void *data)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, 32);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x3A3A46), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, &aos_montserrat_14, 0);
    /* The text is clipped INSIDE the chip. These chips are at absolute
     * positions with a fixed width and there are four of them in 368 px: there
     * is no room for them to grow. With an ellipsis, a long translation looks
     * ugly but stays contained; without this it draws over the chip next to
     * it. */
    /* Width AND height: with the width alone, the label keeps its content
     * height -36 px for a line of 18- and spills above and below a chip of
     * 32. */
    lv_obj_set_size(l, w - 8, 24);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

static void prefs_save(app_t *a)
{
    aos_hal_pref_set_i32(KEY_CTRL, a->g.control);
    aos_hal_pref_set_i32(KEY_SFX, s_sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_FPS, a->g.show_fps);
    aos_hal_pref_set_i32(KEY_AXIS, a->g.tilt_axis);
}

/* The four possible sensor mappings, in the order the chip cycles them. With
 * two taps at most the right one is found without recompiling anything. */
static const char *const ak_axis_txt[4] = { N_("EJE X+"), N_("EJE X-"),
                                            N_("EJE Y+"), N_("EJE Y-") };

static void chips_refresh(app_t *a)
{
    const char *eje = _(ak_axis_txt[a->g.tilt_axis & 3]);

    chip_set(a->chip_sfx,  s_sfx ? _("SONIDO SI") : _("SONIDO NO"), s_sfx);
    chip_set(a->chip_sfx2, s_sfx ? _("SONIDO SI") : _("SONIDO NO"), s_sfx);
    chip_set(a->chip_fps,  a->g.show_fps ? _("FPS SI") : _("FPS NO"), a->g.show_fps);
    chip_set(a->chip_fps2, a->g.show_fps ? _("FPS SI") : _("FPS NO"), a->g.show_fps);
    chip_set_value(a->chip_axis,  eje);
    chip_set_value(a->chip_axis2, eje);
}

static void title_refresh(app_t *a)
{
    char buf[32];
    snprintf(buf, sizeof(buf), _("RECORD  %lu"), (unsigned long)a->g.hiscore);
    lv_label_set_text(a->title_hi, buf);
    chips_refresh(a);
}

static void start_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    int control = (int)(lv_uintptr_t)lv_obj_get_user_data(lv_event_get_target_obj(event));

    a->g.control = (uint8_t)control;
    a->over_shown = 0;
    prefs_save(a);
    overlay_hide_all(a);
    ak_game_start(&a->g);
}

static void chip_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_obj_t *chip = lv_event_get_target_obj(event);

    if (chip == a->chip_sfx || chip == a->chip_sfx2) {
        s_sfx = !s_sfx;
    } else if (chip == a->chip_fps || chip == a->chip_fps2) {
        a->g.show_fps = !a->g.show_fps;
        /* the number is drawn over the field: what was there has to be erased */
        ak_dirty_all(&a->g.d_bg);
    } else if (chip == a->chip_axis || chip == a->chip_axis2) {
        a->g.tilt_axis = (uint8_t)((a->g.tilt_axis + 1) & 3);
        a->g.tilt_zeroed = 0;       /* the zero belongs to the old axis */
    }
    prefs_save(a);
    chips_refresh(a);
    aos_hal_beep(1200, 20);
}

static void pause_show(app_t *a)
{
    a->g.hud_hold = 0;
    if (a->g.state == ST_PLAY || a->g.state == ST_READY ||
        a->g.state == ST_LOST || a->g.state == ST_CLEAR) {
        a->g.state = ST_PAUSE;
        a->g.fire_down = 0;
        a->g.fire_edge = 0;
        chips_refresh(a);
        overlay_show(a, a->pause);
    }
}

/* The score bar pauses on being HELD, not on being tapped.
 *
 * With a plain click you paused by accident: the finger flies over the screen
 * moving the paddle and any brush against the top strip cut the game short.
 * Holding it down is deliberate. */
static void pause_hold_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    aos_hal_beep(700, 30);
    pause_show(a);
}

/* A hold gesture with no feedback leaves you unsure whether it registered:
 * while the finger is down, the score lightens and says there is a pause
 * there. It is two repaints of the strip (one on press and one on release),
 * not one per frame. */
static void pause_press_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    uint8_t hold = (code == LV_EVENT_PRESSED) ? 1 : 0;

    if (a->g.hud_hold != hold) {
        a->g.hud_hold = hold;
        a->g.hud_dirty = 1;
    }
}

static void resume_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    overlay_hide_all(a);
    a->g.state = ST_PLAY;
    a->g.state_t = 0;
    a->g.fire_edge = 0;
    a->g.tilt_zeroed = 0;
    /* the screen was covered by the panel: it has to be rebuilt whole */
    ak_dirty_all(&a->g.d_bg);
    a->g.hud_dirty = 1;
}

/* The panel's green button at the end. One button for both endings: on
 * finishing the twelve levels you go back to the first more quickly, as in the
 * arcades (the score carries on and the lap shows in the HUD, "N1-2"), and if
 * it was a defeat you start again. This used to be solved by changing the
 * button's callback, and lv_obj_remove_event_cb() did not find the one that
 * was not set: two callbacks were left stacked and a new game started twice. */
static void green_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->over_shown = 0;
    overlay_hide_all(a);
    if (a->won) {
        ak_load_level(&a->g, ak_level_count());
    } else {
        ak_game_start(&a->g);
    }
}

static void menu_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->over_shown = 0;
    a->g.state = ST_TITLE;
    a->g.state_t = 0;
    title_refresh(a);
    overlay_show(a, a->title);
}

static void exit_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->want_exit = true;        /* it exits on the next frame, not here */
}

static void build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, AOS_SCREEN_W, AOS_SCREEN_H);
    a->title = p;

    lv_obj_t *card = lv_obj_create(p);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 352, 400);
    lv_obj_set_pos(card, 8, 8);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x05060F), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A3145), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    make_label(p, _("ARKANOS"), &aos_montserrat_48, 0xFF9F0A, 22);
    make_label(p, _("DOCE MUROS Y UNA BOLA"), &aos_montserrat_16, 0xFFE45E, 78);
    make_label(p, _("COMO QUERES JUGAR"), &aos_montserrat_16, 0x9AA3B8, 108);

    lv_obj_t *b1 = make_button(p, _("TACTIL"), _("arrastra el dedo"),
                               19, 134, 330, 66, 0x0A84FF, start_cb, a);
    lv_obj_set_user_data(b1, (void *)(lv_uintptr_t)CTRL_TOUCH);

    lv_obj_t *b2 = make_button(p, _("SENSOR"), _("inclina la placa"),
                               19, 208, 330, 66, 0xBF5AF2, start_cb, a);
    lv_obj_set_user_data(b2, (void *)(lv_uintptr_t)CTRL_TILT);

    make_label(p, _("BOTON LATERAL: SACA Y DISPARA"), &aos_montserrat_14,
               0x7BE9FF, 282);
    make_label(p, _("MANTENE APRETADO EL MARCADOR: PAUSA"), &aos_montserrat_14,
               0x6A6A78, 300);

    a->chip_sfx  = make_chip(p, 19, 322, 116, chip_cb, a);
    a->chip_fps  = make_chip(p, 143, 322, 82, chip_cb, a);
    a->chip_axis = make_chip(p, 233, 322, 116, chip_cb, a);

    a->title_hi = make_label(p, _("RECORD 0"), &aos_montserrat_20, 0xFFFFFF, 364);
    make_label(p, _("DESLIZA A LA DERECHA PARA SALIR"), &aos_montserrat_14,
               0x4A4A56, 392);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, 268, 330);
    a->pause = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    lv_obj_set_style_radius(p, 24, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x3A3A46), 0);
    lv_obj_set_style_border_width(p, 2, 0);

    make_label(p, _("PAUSA"), &aos_montserrat_28, 0xFFFFFF, 14);
    make_button(p, _("SEGUIR"), NULL, 24, 58, 220, 46, 0x30D158, resume_cb, a);
    a->chip_sfx2  = make_chip(p, 24, 114, 220, chip_cb, a);
    a->chip_fps2  = make_chip(p, 24, 152, 220, chip_cb, a);
    /* The axis is also changed from here and not only from the menu: if the
     * paddle goes the wrong way you find out WHILE PLAYING, and sending you
     * out and back in to correct it is mistreatment. */
    a->chip_axis2 = make_chip(p, 24, 190, 220, chip_cb, a);
    make_label(p, _("SI LA PALETA VA AL REVES, CAMBIA EL EJE"),
               &aos_montserrat_14, 0x6A6A78, 226);
    make_button(p, _("SALIR AL RELOJ"), NULL, 24, 254, 220, 46, 0xFF453A, exit_cb, a);
}

static void build_over(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root, 296, 274);
    a->over = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    lv_obj_set_style_radius(p, 24, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0xFF9F0A), 0);
    lv_obj_set_style_border_width(p, 2, 0);

    a->over_title = make_label(p, _("FIN DEL JUEGO"), &aos_montserrat_28,
                               0xFF4A3D, 18);
    a->over_score = make_label(p, "", &aos_montserrat_20, 0xFFFFFF, 64);

    a->over_go = make_button(p, _("OTRA VEZ"), NULL, 28, 132, 240, 46, 0x30D158,
                             green_cb, a);
    make_button(p, _("MENU"), NULL, 28, 188, 116, 46, 0x0A84FF, menu_cb, a);
    make_button(p, _("SALIR"), NULL, 152, 188, 116, 46, 0xFF453A, exit_cb, a);
}

/* Both endings use the same panel: the title changes and so does what the
 * green button does. One panel less to maintain. */
static void show_end(app_t *a, bool gano)
{
    char buf[48];

    a->won = gano ? 1 : 0;
    lv_label_set_text(a->over_title, gano ? _("TERMINASTE") : _("FIN DEL JUEGO"));
    lv_obj_set_style_text_color(a->over_title,
                                lv_color_hex(gano ? 0x30D158 : 0xFF4A3D), 0);

    snprintf(buf, sizeof(buf), _("%lu  (RECORD %lu)"),
             (unsigned long)a->g.score, (unsigned long)a->g.hiscore);
    lv_label_set_text(a->over_score, buf);
    lv_label_set_text(lv_obj_get_child(a->over_go, 0),
                      gano ? _("OTRA VUELTA") : _("OTRA VEZ"));

    aos_hal_pref_set_i32(KEY_HI, (int32_t)a->g.hiscore);
    overlay_show(a, a->over);
    a->over_shown = 1;
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);
    ak_t *g = &a->g;

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this call 'a' no longer
         * exists, so nothing else is touched */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    /* How long a frame takes end to end. It includes LVGL's drawing of the
     * previous frame, which is precisely the part that cannot be timed from
     * here and the one that costs the most. */
    {
        uint64_t now = aos_hal_uptime_ms();
        if (a->frames < 0xFFFF) {
            a->frames++;
        }
        /* The first few frames do not count: the first one includes building
         * the whole screen and, in the simulator, creating the window. If they
         * entered the average, the automatic adjustment starts by relaxing the
         * game and then takes a couple of seconds to come back. */
        if (a->prev_ms && now > a->prev_ms && a->frames > 8) {
            /* the subtraction is narrowed to 32 bits on purpose: a 64-bit
             * division drags in __udivdi3 */
            uint32_t dt = (uint32_t)(now - a->prev_ms);
            int inst = (int)(10000u / dt);
            g->fps10   = (int16_t)(g->fps10 ? (g->fps10 * 7 + inst) / 8 : inst);
            a->real_ms = (int16_t)(a->real_ms ? (a->real_ms * 7 + (int)dt) / 8
                                              : (int)dt);
        }
        a->prev_ms = now;
    }

    if (++a->tune_t >= 20) {
        a->tune_t = 0;
        period_tune(a);
    }

    if (g->state == ST_PAUSE || g->state == ST_TITLE ||
        g->state == ST_OVER || g->state == ST_WIN) {
        if ((g->state == ST_OVER || g->state == ST_WIN) && !a->over_shown) {
            show_end(a, g->state == ST_WIN);
        }
        return;                 /* with a panel on top there is nothing to animate */
    }

    ak_step(g);
    present(a);

    if ((g->state == ST_OVER || g->state == ST_WIN) && !a->over_shown) {
        show_end(a, g->state == ST_WIN);
    }
}

/* --------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------- */

static void touch_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    ak_t *g = &a->g;

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        g->touching = 0;
        return;
    }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    g->touch_x = (int16_t)((point.x - co.x1) * FX_ONE / AK_SCALE);
    g->touch_y = (int16_t)((point.y - co.y1) * FX_ONE / AK_SCALE);
    g->touching = 1;

    if (code == LV_EVENT_PRESSED) {
        if (g->control == CTRL_TILT) {
            /* with the sensor the finger moves nothing: a tap recalibrates the zero */
            g->tilt_zeroed = 0;
            g->touching = 0;
            aos_hal_beep(1000, 30);
        } else {
            /* touching the screen also launches the ball: with the finger
             * already on the glass, hunting for the side button to start is
             * awkward */
            g->fire_edge = 1;
        }
    }
}

/* With AOS_APP_FLAG_NO_SWIPE the back gesture is handled by the app. While
 * playing it does not count: dragging your finger IS the control, and LVGL's
 * gesture threshold is 50 px, that is, half the field. For leaving mid-game
 * there is the pause. */
static void touch_gesture(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    ak_t *g = &a->g;

    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }
    if (g->state != ST_TITLE) {
        return;
    }
    lv_indev_wait_release(indev);
    a->want_exit = true;
}

/* The side button. While playing it is the trigger and nothing else: if it let
 * the long press through, holding it for a second would take you out of the
 * game. For leaving there is the pause bar. */
static bool app_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    ak_t *g = &a->g;

    if (g->state != ST_PLAY && g->state != ST_READY && g->state != ST_LOST &&
        g->state != ST_CLEAR) {
        return false;       /* let the runtime do the usual */
    }

    if (action == AOS_BUTTON_PRESS) {
        g->fire_down = 1;
        g->fire_edge = 1;
    } else {
        g->fire_down = 0;
    }
    return true;
}

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    if (a->g.state == ST_PLAY || a->g.state == ST_READY ||
        a->g.state == ST_LOST || a->g.state == ST_CLEAR) {
        pause_show(a);
        return true;        /* the pause panel already has its own exit button */
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    ak_t *g = &a->g;
    int32_t v = 0;

    if (aos_hal_pref_get_i32(KEY_HI, &v) && v > 0) {
        g->hiscore = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_CTRL, &v)) {
        g->control = (uint8_t)(v == CTRL_TILT ? CTRL_TILT : CTRL_TOUCH);
    }
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) {
        s_sfx = (v != 0);
    }
    if (aos_hal_pref_get_i32(KEY_FPS, &v)) {
        g->show_fps = (uint8_t)(v ? 1 : 0);
    }
    if (aos_hal_pref_get_i32(KEY_AXIS, &v)) {
        g->tilt_axis = (uint8_t)(v & 3);
    }
}

static void *arkanos_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }

    /* A trail on the serial port. If the board hangs or restarts, the last
     * line says which stage it was in. */
    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("arkanos", "abriendo | interna %u B, psram %u B",
                (unsigned)heap_int, (unsigned)heap_psram);

    /* Three buffers, all through malloc() and not lv_malloc(): with
     * CONFIG_SPIRAM_USE_MALLOC they land in PSRAM, which is where they belong.
     * It is 82 + 82 + 330 = 494 KB. The background is the price of the dirty
     * rectangles: without a clean copy of what is behind each moving thing,
     * there is no way to erase it without redrawing the whole screen. */
    size_t chico = (size_t)AK_W * AK_H * sizeof(uint16_t);
    a->fbmem = (uint16_t *)malloc(chico);
    a->bgmem = (uint16_t *)malloc(chico);
    a->big   = (uint16_t *)malloc(chico * AK_SCALE * AK_SCALE);
    if (!a->fbmem || !a->bgmem || !a->big) {
        aos_hal_log("arkanos", "sin memoria para los buffers (%u B)",
                    (unsigned)(chico * (2 + AK_SCALE * AK_SCALE)));
        free(a->fbmem);
        free(a->bgmem);
        free(a->big);
        lv_free(a);
        return NULL;
    }
    memset(a->fbmem, 0, chico);
    memset(a->bgmem, 0, chico);
    memset(a->big, 0, chico * AK_SCALE * AK_SCALE);

    ak_buf_init(&a->g.fb, a->fbmem, AK_W, AK_H);
    ak_buf_init(&a->g.bg, a->bgmem, AK_W, AK_H);
    /* Y- by default, both halves measured on the board on 28/08: imu.ax turned
     * out to be the PITCH axis (tilting the screen forwards and backwards
     * moved the paddle), so the paddle's is imu.ay; and the sign that feels
     * right is the negative one. The AXIS chip is there all the same. */
    a->g.tilt_axis = 3;
    a->g.rng = (uint32_t)aos_hal_uptime_ms() | 1u;

    prefs_load(a);

    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    /* The canvas receives the ALREADY upscaled buffer and is drawn 1:1: no STRETCH */
    lv_canvas_set_buffer(a->canvas, a->big, AK_W * AK_SCALE, AK_H * AK_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, AK_W * AK_SCALE, AK_H * AK_SCALE);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* Touch layer: in LVGL 9 every object is born clickable, so without this
     * the canvas would eat the finger. It starts below the score, which is the
     * pause button. */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H - AK_HUD_H * AK_SCALE);
    lv_obj_set_pos(a->touch, 0, AK_HUD_H * AK_SCALE);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESS_LOST, a);
    lv_obj_add_event_cb(a->touch, touch_gesture, LV_EVENT_GESTURE, a);

    a->pausebtn = lv_obj_create(root);
    lv_obj_remove_style_all(a->pausebtn);
    lv_obj_set_size(a->pausebtn, AOS_SCREEN_W, AK_HUD_H * AK_SCALE);
    lv_obj_set_pos(a->pausebtn, 0, 0);
    lv_obj_add_flag(a->pausebtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->pausebtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->pausebtn, pause_hold_cb, LV_EVENT_LONG_PRESSED, a);
    lv_obj_add_event_cb(a->pausebtn, pause_press_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->pausebtn, pause_press_cb, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->pausebtn, pause_press_cb, LV_EVENT_PRESS_LOST, a);

    build_title(a, root);
    build_pause(a, root);
    build_over(a, root);

    a->g.state = ST_TITLE;
    a->g.level = 0;
    ak_bg_build(&a->g);         /* the menu lets the first wall show through behind */
    ak_draw_hud(&a->g);
    push_all(a);                /* without this the canvas starts black */
    ak_dirty_reset(&a->g.d_bg);
    title_refresh(a);
    overlay_show(a, a->title);

#ifdef AOS_SIM_BUILTIN
    /* Shortcuts for designing without playing for twenty minutes. They only
     * exist in the simulator: on the board there are no environment variables.
     *
     *   ARK_LEVEL=8  starts on that level
     *   ARK_AUTO=1   the paddle plays itself, ideal for leaving it running
     *   ARK_LIVES=1  to reach the ending panel quickly
     *   ARK_FPS=1    shows frames per second and % of screen pushed
     */
    if (getenv("ARK_FPS")) {
        a->g.show_fps = 1;
    }
    const char *env_level = getenv("ARK_LEVEL");
    if (env_level || getenv("ARK_AUTO")) {
        a->g.control = CTRL_TOUCH;
        a->g.autoplay = getenv("ARK_AUTO") ? 1 : 0;
        overlay_hide_all(a);
        ak_game_start(&a->g);
        if (env_level) {
            ak_load_level(&a->g, atoi(env_level));
        }
        if (getenv("ARK_LIVES")) {
            a->g.lives = (int8_t)atoi(getenv("ARK_LIVES"));
            a->g.hud_dirty = 1;
        }
    }
#endif

    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("arkanos", "listo | interna %u B, psram %u B | campo %dx%d x%d",
                (unsigned)heap_int, (unsigned)heap_psram, AK_W, AK_H, AK_SCALE);

    a->period = AK_FRAME_START;
    a->timer = lv_timer_create(frame, AK_FRAME_START, a);
    return a;
}

static void arkanos_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }
    aos_hal_pref_set_i32(KEY_HI, (int32_t)a->g.hiscore);
    free(a->fbmem);
    free(a->bgmem);
    free(a->big);
    lv_free(a);
}

/* Leaving mid-bounce should not cost a ball: it pauses. */
static void arkanos_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a) {
        pause_show(a);
    }
}

static bool arkanos_init(aos_app_t *app)
{
    app->desc.id      = "demo.arkanos";
    app->desc.name    = "ARKANOS";
    app->desc.icon    = LV_SYMBOL_STOP;
    app->desc.icon_vec = AOS_ICON_BRICKS;
    app->desc.color_a = 0xFF9F0A;
    app->desc.color_b = 0x0A84FF;
    app->desc.order   = 147;
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                        AOS_APP_FLAG_NO_SWIPE;

    app->create  = arkanos_create;
    app->destroy = arkanos_destroy;
    app->hide    = arkanos_hide;
    app->back    = app_back;
    app->button  = app_button;
    return true;
}

AOS_APP_ENTRY(arkanos_init);
