/*
 * CLAUDE JUMP - the app
 *
 * The only thing that sees LVGL, the HAL and the preferences. The game itself
 * does not know any of the three exist (see cjump.h).
 *
 * Living here:
 *   - the flush to the screen by dirty rectangles (present())
 *   - the four LVGL screens: menu, shop, pause and game over
 *   - the costume shop and its x4 magnified preview
 *   - the sensor and finger controls
 *   - the preferences: record, coins, costume worn and which are bought
 *
 * It is the system's first app born multilingual: every visible string comes
 * wrapped in _() from the first commit, instead of being written in Spanish
 * and wrapped afterwards. And the only thing drawn on the canvas is numbers,
 * precisely so the 5x7 font -which has no accents- never touches a translated
 * string.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "cjump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Preferences. A prefix of its own, as the house rules require.
 * -------------------------------------------------------------------------- */
#define KEY_HI      "cj_hi"
#define KEY_COINS   "cj_coins"
#define KEY_SKIN    "cj_skin"
#define KEY_OWNED   "cj_own"
#define KEY_CTRL    "cj_ctrl"
#define KEY_SFX     "cj_sfx"
#define KEY_AXIS    "cj_axis"
#define KEY_FPS     "cj_fps"

#define FRAME_MS        33          /* 30 frames per second                   */
#define FRAME_MAX       66          /* floor it relaxes to if it cannot keep up */

/* Shop preview: the critter in a small buffer upscaled x4. */
/* The preview shrank from 170 to 144 px when everything touchable had to move
 * above y=360 (see build_title). It was the only thing that could give: the
 * buttons and the chips cannot be made smaller without ceasing to be
 * buttons. */
#define PV_W            36
#define PV_H            36
#define PV_SCALE        4

/* Coin bonus on finishing: one every 20 metres. A collected coin is worth 2
 * and stomping a critter is worth 5, so going out to collect them pays far
 * better than climbing straight up: the coin has to reward risk, not playing
 * time. The costume prices are calibrated against this (cj_skins.c). */
#define BONUS_PER_M     20

typedef struct {
    cj_t       g;

    lv_obj_t  *root;
    lv_obj_t  *canvas;
    lv_obj_t  *touch;
    lv_obj_t  *hudbtn;
    uint16_t  *fbmem, *bgmem, *big;

    /* preview (menu and shop) */
    lv_obj_t  *preview;
    uint16_t  *pvmem, *pvbig;
    cj_buf_t   pvbuf;

    /* screens */
    lv_obj_t  *title, *shop, *pause, *over;
    lv_obj_t  *lbl_best, *lbl_coins, *lbl_wallet;
    lv_obj_t  *chip_ctrl, *chip_axis, *chip_sfx, *lbl_ayuda;
    lv_obj_t  *lbl_skin, *lbl_price, *btn_action, *lbl_action;
    lv_obj_t  *lbl_over_t, *lbl_over_s, *lbl_over_c, *lbl_over_b;

    /* app state */
    uint32_t   coins_total;
    uint32_t   owned;               /* bitmap of bought costumes               */
    int8_t     shop_idx;
    bool       want_exit;
    bool       over_shown;
    bool       closing;             /* see cjump_destroy()                     */
    uint32_t   last_gesture_ms;

    uint8_t    imu_skip;            /* see read_tilt()                         */
    uint8_t    auto_wait;           /* CJ_AUTO: frames before retrying         */
    int16_t    period;
    int16_t    real_ms;
    uint64_t   prev_ms;
    uint16_t   frames;
    uint8_t    tune_t;
    lv_timer_t *timer;
} app_t;

static bool s_sfx = true;

/* --------------------------------------------------------------------------
 * Sound
 *
 * cj_game.c calls it without knowing there is a HAL on the other side.
 * aos_hal_beep() enqueues and plays from its own task, so it does not block
 * the drawing.
 * -------------------------------------------------------------------------- */
void cj_sfx(int freq_hz, int ms)
{
    if (s_sfx) {
        aos_hal_beep(freq_hz, ms);
    }
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* --------------------------------------------------------------------------
 * Flush to the screen
 *
 * The usual three steps, and none of them touches the whole screen unless it
 * has to. It is identical to arkanos's because the problem is the same.
 * -------------------------------------------------------------------------- */

static const cj_rect_t cj_entera = { 0, 0, CJ_W, CJ_H };

static void push_rect(app_t *a, const cj_rect_t *r)
{
    cj_expand(a->fbmem, a->big, r);

    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);

    lv_area_t area;
    area.x1 = co.x1 + r->x0 * CJ_SCALE;
    area.y1 = co.y1 + r->y0 * CJ_SCALE;
    area.x2 = co.x1 + r->x1 * CJ_SCALE - 1;
    area.y2 = co.y1 + r->y1 * CJ_SCALE - 1;
    lv_obj_invalidate_area(a->canvas, &area);
}

static void push_all(app_t *a)
{
    push_rect(a, &cj_entera);
    a->g.last_area = 100;
}

static void present(app_t *a)
{
    cj_t *g = &a->g;

    /* Zone change: the background is rebuilt whole and everything is pushed.
     * It is the game's only expensive frame, and it happens every few hundred
     * metres. */
    if (g->zone_changed) {
        g->zone_changed = 0;
        cj_bg_build(g);
        g->hud_dirty = 1;
        /* The new background is already copied over fb: whatever was drawn on
         * top of the old one has to be forgotten or the next frame's restore
         * would erase areas that are already correct. */
        cj_dirty_reset(&g->d_prev);
        for (int i = 0; i < MAX_PLATS; i++) g->plats[i].drawn = 0;
        for (int i = 0; i < MAX_COINS; i++) g->coins[i].drawn = 0;
        for (int i = 0; i < MAX_BUGS;  i++) g->bugs[i].drawn  = 0;
        for (int i = 0; i < MAX_PARTS; i++) g->parts[i].drawn = 0;
        g->hero_drawn = 0;
        cj_draw_movers(g);
        cj_draw_hud(g);
        push_all(a);
        g->d_prev = g->d_cur;
        return;
    }

    /* 1. restore from the background whatever we dirtied last frame */
    g->d_push = g->d_prev;

    if (g->d_push.all) {
        cj_restore(a->fbmem, a->bgmem, &cj_entera);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            cj_restore(a->fbmem, a->bgmem, &g->d_push.r[i]);
        }
    }

    /* 2. draw what moves; fills d_cur */
    cj_draw_movers(g);

    /* 3. the score, only if a number changed. It is noted in d_push and not in
     *    d_cur: it has to be pushed, not restored next frame. */
    /* The score changes number on nearly every frame while climbing, and
     * repainting it is a full-width rectangle plus its invalid area. It is
     * refreshed by the clock and not per frame, which is the rule that already
     * came out of the Game of Life and the tuner. */
    if (g->hud_dirty && (a->frames & 3) == 0) {
        cj_draw_hud(g);
    }

    /* 4. upscale and invalidate the union */
    cj_dirty_join(&g->d_push, &g->d_cur);

    if (g->d_push.all) {
        push_all(a);
    } else {
        for (int i = 0; i < g->d_push.n; i++) {
            push_rect(a, &g->d_push.r[i]);
        }
        g->last_area  = (uint16_t)(cj_dirty_area(&g->d_push) * 100 / (CJ_W * CJ_H));
        g->last_rects = g->d_push.n;
    }

    g->d_prev = g->d_cur;
}

/* --------------------------------------------------------------------------
 * Pace
 *
 * If the frame comes out more expensive than the period, the timer is always
 * overdue, the LVGL task never gets to sleep and the watchdog fires. Rather
 * than hang the board, the game relaxes.
 * -------------------------------------------------------------------------- */
static void period_tune(app_t *a)
{
    int want = a->period;

    if (a->real_ms > want + want / 3) {
        want = a->real_ms;
    } else if (a->real_ms <= want + 2 && want > FRAME_MS) {
        want -= 6;
    }
    want = clampi(want, FRAME_MS, FRAME_MAX);

    if (want != a->period) {
        aos_hal_log("cjump", "real frame %d ms: period %d -> %d ms (%d.%d fps, %u%% of the screen in %u rectangles)",
                    a->real_ms, a->period, want, a->g.fps10 / 10, a->g.fps10 % 10,
                    (unsigned)a->g.last_area, (unsigned)a->g.last_rects);
        a->period = (int16_t)want;
        lv_timer_set_period(a->timer, (uint32_t)want);
    }
}

/* --------------------------------------------------------------------------
 * Interface pieces
 *
 * The screens to read and touch are built with LVGL objects and not with the
 * 5x7 font: there vector text wins, and it also has accents.
 * -------------------------------------------------------------------------- */

static lv_obj_t *make_panel(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
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
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text,
                             int x, int y, int w, int h, uint32_t accent,
                             const lv_font_t *font, lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1C1C24), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    /* The press highlight goes by colour and not by transform_scale: scaling
     * forces LVGL to build a separate layer, and a layer that does not fit in
     * memory is a hang, not a slowdown. */
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    /* Fixed width AND height with an ellipsis: these buttons are at absolute
     * positions and German grows 20%. Ugly but contained beats drawn over the
     * neighbour. */
    lv_obj_set_size(l, w - 12, h - 8);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

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
    lv_obj_set_size(l, w - 8, 24);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

/* --------------------------------------------------------------------------
 * Preferences
 * -------------------------------------------------------------------------- */

static void prefs_save_all(app_t *a)
{
    aos_hal_pref_set_i32(KEY_HI,    (int32_t)a->g.hiscore);
    aos_hal_pref_set_i32(KEY_COINS, (int32_t)a->coins_total);
    aos_hal_pref_set_i32(KEY_SKIN,  a->g.skin);
    aos_hal_pref_set_i32(KEY_OWNED, (int32_t)a->owned);
}

static void prefs_save_opts(app_t *a)
{
    aos_hal_pref_set_i32(KEY_CTRL, a->g.control);
    aos_hal_pref_set_i32(KEY_SFX,  s_sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_AXIS, a->g.tilt_axis);
    aos_hal_pref_set_i32(KEY_FPS,  a->g.show_fps);
}

static void prefs_load(app_t *a)
{
    int32_t v = 0;

    if (aos_hal_pref_get_i32(KEY_HI, &v) && v > 0) {
        a->g.hiscore = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_COINS, &v) && v > 0) {
        a->coins_total = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_OWNED, &v) && v > 0) {
        a->owned = (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_SKIN, &v) && v >= 0 && v < CJ_SKINS) {
        a->g.skin = (uint8_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_CTRL, &v)) {
        a->g.control = (uint8_t)(v == CTRL_TOUCH ? CTRL_TOUCH : CTRL_TILT);
    }
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) {
        s_sfx = (v != 0);
    }
    if (aos_hal_pref_get_i32(KEY_AXIS, &v)) {
        a->g.tilt_axis = (uint8_t)(v & 3);
    }
    if (aos_hal_pref_get_i32(KEY_FPS, &v)) {
        a->g.show_fps = (uint8_t)(v ? 1 : 0);
    }

    /* The first three always come unlocked, even if the preference is empty or
     * was stored by an earlier version. */
    a->owned |= (1u << CJ_SKINS_FREE) - 1u;
    if (!(a->owned & (1u << a->g.skin))) {
        a->g.skin = 0;
    }
}

/* --------------------------------------------------------------------------
 * The critter's preview
 *
 * A 46x46 buffer upscaled x4 to 184x184. It is redrawn on changing costume and
 * nothing else: it is not a hot path.
 * -------------------------------------------------------------------------- */

static void preview_draw(app_t *a, int skin)
{
    if (!a->preview) {
        return;
    }
    if (skin < 0 || skin >= CJ_SKINS) {
        skin = 0;
    }

    /* A neutral card with a rule in the costume's colour. Tinting the
     * background with the costume's own colour was tried and is worse: a
     * pastel critter on its own pastel does not stand out, and there are
     * sixteen to compare. */
    cj_fill(&a->pvbuf, cj_rgb(0x0E1018));
    cj_vgrad(&a->pvbuf, 1, 1, PV_W - 2, PV_H - 2,
             cj_rgb(0x2A2E3E), cj_rgb(0x161A26));
    cj_frame(&a->pvbuf, 1, 1, PV_W - 2, PV_H - 2, cj_rgb(cj_skins[skin].body));

    /* The critter goes with the costume's box against the top edge: the hats
     * reach 14 px above the body (see cj_skins.c). */
    cj_hero_draw(&a->pvbuf, (PV_W - HERO_W) / 2, HERO_BOX_H - HERO_H,
                 skin, 0, 0, 0, false);

    cj_expand_n(a->pvmem, PV_W, PV_H, a->pvbig, PV_SCALE);
    lv_obj_invalidate(a->preview);
}

/* --------------------------------------------------------------------------
 * Screens: showing and hiding
 * -------------------------------------------------------------------------- */

static void overlay_hide_all(app_t *a)
{
    lv_obj_t *const panels[] = { a->title, a->shop, a->pause, a->over };
    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++) {
        if (panels[i]) {
            lv_obj_add_flag(panels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (a->preview) {
        lv_obj_add_flag(a->preview, LV_OBJ_FLAG_HIDDEN);
    }
}

/* There is ONE preview and the menu and the shop share it: it is 68 KB of
 * PSRAM and there is no point having two. Since it is a child of the root and
 * not of the panel, it has to be brought to the front and put where each
 * screen wants it. */
static void preview_place(app_t *a, int y)
{
    if (!a->preview) {
        return;
    }
    lv_obj_set_pos(a->preview, (AOS_SCREEN_W - PV_W * PV_SCALE) / 2, y);
    lv_obj_remove_flag(a->preview, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->preview);
}

static void overlay_show(app_t *a, lv_obj_t *panel)
{
    overlay_hide_all(a);
    if (panel) {
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(panel);
    }
}

/* --------------------------------------------------------------------------
 * Menu
 * -------------------------------------------------------------------------- */

static const char *const cj_axis_txt[4] = { N_("EJE Y-"), N_("EJE Y+"),
                                            N_("EJE X-"), N_("EJE X+") };

static void chips_refresh(app_t *a)
{
    chip_set_value(a->chip_ctrl, a->g.control == CTRL_TILT ? _("INCLINAR")
                                                           : _("DEDO"));
    chip_set_value(a->chip_axis, _(cj_axis_txt[a->g.tilt_axis & 3]));
    chip_set(a->chip_sfx, _("SONIDO"), s_sfx);

    if (a->lbl_ayuda) {
        lv_label_set_text(a->lbl_ayuda,
                          a->g.control == CTRL_TILT
                              ? _("Incliná el reloj para moverte")
                              : _("Arrastrá el dedo para moverte"));
    }
}

static void title_refresh(app_t *a)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "%s  %u m", _("Récord"), (unsigned)a->g.hiscore);
    lv_label_set_text(a->lbl_best, buf);

    snprintf(buf, sizeof(buf), "%s  %u", _("Monedas"), (unsigned)a->coins_total);
    lv_label_set_text(a->lbl_coins, buf);

    chips_refresh(a);
    preview_draw(a, a->g.skin);
    preview_place(a, 44);
}

/* --------------------------------------------------------------------------
 * Shop
 * -------------------------------------------------------------------------- */

static bool owned(const app_t *a, int i)
{
    return (a->owned & (1u << i)) != 0;
}

static void shop_refresh(app_t *a)
{
    int i = a->shop_idx;
    const cj_skin_t *s = &cj_skins[i];
    char buf[64];

    snprintf(buf, sizeof(buf), "%s  %u", _("Monedas"), (unsigned)a->coins_total);
    lv_label_set_text(a->lbl_wallet, buf);

    snprintf(buf, sizeof(buf), "%s   %d/%d", _(s->name), i + 1, CJ_SKINS);
    lv_label_set_text(a->lbl_skin, buf);

    if (!owned(a, i)) {
        snprintf(buf, sizeof(buf), "%u %s", (unsigned)s->price, _("monedas"));
        lv_label_set_text(a->lbl_price, buf);
        lv_obj_set_style_text_color(a->lbl_price,
                                    lv_color_hex(a->coins_total >= s->price
                                                 ? 0xFFD60A : 0xFF453A), 0);
        lv_label_set_text(a->lbl_action, a->coins_total >= s->price
                                         ? _("Comprar") : _("No alcanza"));
    } else if (a->g.skin == i) {
        /* Worn: there is no action at all, so the button hides itself instead
         * of repeating what the status line already says. A button saying the
         * same thing as the notice above it and doing nothing when touched
         * reads as a screen that does not respond. */
        lv_label_set_text(a->lbl_price, _("Puesto"));
        lv_obj_set_style_text_color(a->lbl_price, lv_color_hex(0x30D158), 0);
    } else {
        lv_label_set_text(a->lbl_price, _("Comprado"));
        lv_obj_set_style_text_color(a->lbl_price, lv_color_hex(0x9AA3B8), 0);
        lv_label_set_text(a->lbl_action, _("Ponerme este"));
    }
    if (owned(a, i) && a->g.skin == i) {
        lv_obj_add_flag(a->btn_action, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(a->btn_action, LV_OBJ_FLAG_HIDDEN);
    }

    preview_draw(a, i);
    preview_place(a, 80);
}

static void shop_move(app_t *a, int delta)
{
    a->shop_idx = (int8_t)((a->shop_idx + delta + CJ_SKINS) % CJ_SKINS);
    cj_sfx(900, 18);
    shop_refresh(a);
}

static void shop_action_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    int i = a->shop_idx;
    const cj_skin_t *s = &cj_skins[i];

    if (!owned(a, i)) {
        if (a->coins_total < s->price) {
            cj_sfx(200, 60);
            aos_ui_toast(_("Todavía no alcanza"), 1200);
            return;
        }
        a->coins_total -= s->price;
        a->owned |= (1u << i);
        cj_sfx(700, 40);
        cj_sfx(1100, 50);
        cj_sfx(1500, 70);
        aos_ui_toast(_("Disfraz desbloqueado"), 1200);
    }
    a->g.skin = (uint8_t)i;
    prefs_save_all(a);
    shop_refresh(a);
    cj_sfx(1300, 30);
}

static void shop_prev_cb(lv_event_t *event)
{
    shop_move((app_t *)lv_event_get_user_data(event), -1);
}

static void shop_next_cb(lv_event_t *event)
{
    shop_move((app_t *)lv_event_get_user_data(event), +1);
}

/* The menu is drawn over the first zone's sky, repainted from scratch: if the
 * game's last frame were left, behind the menu you would see zone 4's sky with
 * its score and its platforms frozen. */
static void title_backdrop(app_t *a)
{
    a->g.zone    = 0;
    a->g.bg_seed = cj_rand(&a->g);
    cj_bg_build(&a->g);
    cj_dirty_reset(&a->g.d_prev);
    cj_dirty_reset(&a->g.d_push);
    push_all(a);
}

static void go_title(app_t *a)
{
    a->g.state = ST_TITLE;
    title_backdrop(a);
    overlay_show(a, a->title);
    title_refresh(a);
}

static void shop_back_cb(lv_event_t *event)
{
    go_title((app_t *)lv_event_get_user_data(event));
}

static void shop_open_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->g.state = ST_SHOP;
    a->shop_idx = (int8_t)a->g.skin;
    overlay_show(a, a->shop);
    shop_refresh(a);
}

/* --------------------------------------------------------------------------
 * A game
 * -------------------------------------------------------------------------- */

static void game_start(app_t *a)
{
    cj_game_reset(&a->g);
    a->over_shown = false;
    overlay_hide_all(a);
    /* The camera starts where it starts, so there is nothing to restore from
     * the previous frame: everything is drawn and everything is pushed, once. */
    a->g.zone_changed = 1;
    present(a);
}

static void start_cb(lv_event_t *event)
{
    game_start((app_t *)lv_event_get_user_data(event));
}

static void pause_show(app_t *a)
{
    if (a->g.state != ST_PLAY) {
        return;
    }
    a->g.state = ST_PAUSE;
    overlay_show(a, a->pause);
}

static void pause_cb(lv_event_t *event)
{
    pause_show((app_t *)lv_event_get_user_data(event));
}

static void resume_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    overlay_hide_all(a);
    a->g.state = ST_PLAY;
    /* Coming back from a panel that covered the whole screen there is nothing
     * trustworthy in fb: everything is repainted. */
    a->g.zone_changed = 1;
    a->prev_ms = 0;
    present(a);
}

static void menu_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    prefs_save_all(a);
    go_title(a);
}

static void exit_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->want_exit = true;        /* deferred: aos_ui_back() destroys the app  */
}

static void ctrl_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->g.control = (uint8_t)(a->g.control == CTRL_TILT ? CTRL_TOUCH : CTRL_TILT);
    a->g.tilt_zeroed = 0;
    prefs_save_opts(a);
    chips_refresh(a);
    cj_sfx(900, 25);
}

static void axis_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    a->g.tilt_axis = (uint8_t)((a->g.tilt_axis + 1) & 3);
    a->g.tilt_zeroed = 0;
    prefs_save_opts(a);
    chips_refresh(a);
    cj_sfx(900, 25);
}

static void sfx_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    s_sfx = !s_sfx;
    prefs_save_opts(a);
    chips_refresh(a);
    cj_sfx(1200, 30);
}

/* --------------------------------------------------------------------------
 * Game over
 * -------------------------------------------------------------------------- */

static void show_end(app_t *a)
{
    cj_t *g = &a->g;
    char buf[64];

    a->over_shown = true;

    uint32_t bonus = g->score / BONUS_PER_M;
    uint32_t total = g->coins_run + bonus;
    a->coins_total += total;
    prefs_save_all(a);

    lv_label_set_text(a->lbl_over_t, g->new_record ? _("Nuevo récord")
                                                   : _("Se acabó"));
    lv_obj_set_style_text_color(a->lbl_over_t,
                                lv_color_hex(g->new_record ? 0xFFD60A : 0xFFFFFF), 0);

    snprintf(buf, sizeof(buf), "%u m", (unsigned)g->score);
    lv_label_set_text(a->lbl_over_s, buf);

    snprintf(buf, sizeof(buf), "+%u %s", (unsigned)total, _("monedas"));
    lv_label_set_text(a->lbl_over_c, buf);

    snprintf(buf, sizeof(buf), "%s %u   %s %u",
             _("Juntadas"), (unsigned)g->coins_run,
             _("por altura"), (unsigned)bonus);
    lv_label_set_text(a->lbl_over_b, buf);

    overlay_show(a, a->over);
    cj_sfx(g->new_record ? 1400 : 500, 60);
}

/* --------------------------------------------------------------------------
 * Control
 * -------------------------------------------------------------------------- */

static void read_tilt(app_t *a)
{
    /* One frame in three, that is, about ten reads a second. The IMU shares
     * the I2C with the touch panel and hammering that bus is what set off
     * watchdogs back in the day; 2043 reads one in four and the maze every
     * 100 ms. No more is needed: the control sets a TARGET and the critter
     * approaches it by itself, so the movement comes out smooth anyway. */
    if (a->imu_skip) {
        a->imu_skip--;
        return;
    }
    a->imu_skip = 2;

    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        return;
    }
    if (!a->g.tilt_zeroed) {
        a->g.zero_a = imu.ax;
        a->g.zero_b = imu.ay;
        a->g.tilt_zeroed = 1;
        return;
    }

    float da = imu.ax - a->g.zero_a;
    float db = imu.ay - a->g.zero_b;

    /* The four possible mappings, as in the maze and in 2043. Number 0 is the
     * one measured on the board: the right of the screen is -ay. The other
     * three are there to fix things without recompiling the day the board is
     * mounted differently. */
    float h;
    switch (a->g.tilt_axis & 3) {
    case 1:  h =  db; break;
    case 2:  h = -da; break;
    case 3:  h =  da; break;
    default: h = -db; break;
    }

    /* ABSOLUTE position and not velocity: mapping to velocity, a barely
     * miscalibrated zero leaves the critter drifting into an edge and there is
     * no way to stop it. With absolute, a level board is always the centre.
     * Full scale is 0.40 g, that is, some 24 degrees each way.
     *
     * The sum is done in whole hundredths right after the game's single
     * floating-point step. */
    int centi = (int)(h * 100.0f);
    centi = clampi(centi, -40, 40);
    a->g.target_x = (int32_t)(CJ_W - HERO_W) * FX / 2 +
                    (int32_t)centi * ((int32_t)(CJ_W - HERO_W) * FX / 2) / 40;
}

static void touch_event(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    if (a->closing) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    cj_t *g = &a->g;

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
    g->touch_x = (int16_t)((point.x - co.x1) / CJ_SCALE);
    g->touching = 1;

    if (g->control == CTRL_TILT) {
        if (code == LV_EVENT_PRESSED) {
            /* With the sensor the finger moves nothing: a tap recalibrates the
             * zero. It is the escape valve for when you change posture. */
            g->tilt_zeroed = 0;
            cj_sfx(1000, 25);
        }
        g->touching = 0;
        return;
    }

    /* With the finger, the control is absolute too: the critter goes where the
     * finger is. That way both controls feel the same and share the code. */
    g->target_x = clampi((int)g->touch_x - HERO_W / 2, 0, CJ_W - HERO_W) * FX;
}

/* --------------------------------------------------------------------------
 * Gestures
 *
 * They arrive by two paths: LV_EVENT_GESTURE on the root (which has to be
 * listened for on the ROOT and with GESTURE_BUBBLE taken off it, because LVGL
 * delivers it to the first ancestor that does not have it) and
 * aos_ui_take_gesture(), which is what the v2's touch chip itself detects when
 * the swipe is fast and LVGL never gathers its 50 px. Since both may arrive,
 * the second is discarded if it comes within 400 ms of the first.
 * -------------------------------------------------------------------------- */

static bool handle_gesture(app_t *a, int dir)
{
    /* While playing, a horizontal swipe is NOT a gesture: it is the control.
     * false is returned so the caller does not consume the touch -calling
     * lv_indev_wait_release() here would cut the drag short at 50 px, which is
     * exactly what AOS_APP_FLAG_LONG_DRAG exists to avoid. */
    if (a->g.state != ST_SHOP && a->g.state != ST_TITLE) {
        return false;
    }

    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - a->last_gesture_ms) < 400) {
        return false;
    }
    a->last_gesture_ms = now;

    if (a->g.state == ST_SHOP) {
        shop_move(a, dir == LV_DIR_LEFT ? +1 : -1);
        return true;
    }
    if (dir == LV_DIR_RIGHT) {
        a->want_exit = true;
        return true;
    }
    return false;
}

static void gesture_cb(lv_event_t *event)
{
    app_t *a = (app_t *)lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    if (handle_gesture(a, (int)dir)) {
        lv_indev_wait_release(indev);
    }
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */

static void frame(lv_timer_t *timer)
{
    app_t *a = (app_t *)lv_timer_get_user_data(timer);
    cj_t *g = &a->g;

    if (a->want_exit) {
        /* aos_ui_back() destroys the app: after this call 'a' no longer
         * exists, so nothing else is touched. */
        a->want_exit = false;
        aos_ui_back();
        return;
    }

    switch ((aos_touch_gesture_t)aos_ui_take_gesture()) {
    case AOS_TOUCH_GESTURE_LEFT:  handle_gesture(a, LV_DIR_LEFT);  break;
    case AOS_TOUCH_GESTURE_RIGHT: handle_gesture(a, LV_DIR_RIGHT); break;
    default: break;
    }

    /* How long a frame takes end to end. It includes LVGL's drawing of the
     * previous frame, which is precisely the part that cannot be timed from
     * here and the one that costs the most. */
    {
        uint64_t now = aos_hal_uptime_ms();
        if (a->frames < 0xFFFF) {
            a->frames++;
        }
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

    /* The adjustment does NOT start with the app. Measured on the board on
     * 2026-09-06: the first few frames include building the screen and the
     * first complete flush, the average comes out inflated, and the adjustment
     * -which on an expensive frame goes straight to the measured value- jumped
     * from 33 to 65 ms and then took two seconds to come back, dropping 6 ms
     * at a time. Two seconds of play at 15 fps right at the start of the game.
     *
     * With 60 frames of grace the transient has left the moving average and
     * the adjustment starts by looking at the steady state: 33 ms and 27.8 fps
     * at 18% of the screen, which is what this app really delivers. */
    if (a->frames > 60 && ++a->tune_t >= 20) {
        a->tune_t = 0;
        period_tune(a);
    }

    /* CJ_AUTO starts over by itself. Without this, leaving the game running
     * for an hour to hunt for dirty-rectangle trails lasts until the first
     * critter: the bot does not dodge them. */
    if (g->autoplay && g->state == ST_OVER && a->over_shown) {
        if (++a->auto_wait > 60) {
            a->auto_wait = 0;
            game_start(a);
        }
        return;
    }

    if (g->state != ST_PLAY && g->state != ST_DYING) {
        return;                 /* with a panel on top there is nothing to animate */
    }

    if (g->state == ST_PLAY && g->control == CTRL_TILT) {
        read_tilt(a);
    }

    cj_step(g);
    present(a);

    if (g->state == ST_OVER && !a->over_shown) {
        show_end(a);
    }

    /* The fps counter is refreshed by the clock and not per frame: writing it
     * on every one invalidates its box ten times a second for nothing. */
    if (g->show_fps && (a->frames & 7) == 0) {
        g->hud_dirty = 1;
    }
}

/* --------------------------------------------------------------------------
 * Back and physical button
 * -------------------------------------------------------------------------- */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return false;
    }
    switch (a->g.state) {
    case ST_PLAY:
    case ST_DYING:
        pause_show(a);
        return true;
    case ST_SHOP:
    case ST_PAUSE:
    case ST_OVER:
        go_title(a);
        return true;
    default:
        return false;           /* from the menu, let the system leave        */
    }
}

/* --------------------------------------------------------------------------
 * Building the screens
 * -------------------------------------------------------------------------- */

static void build_title(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->title = p;

    make_label(p, "Claude Jump", &aos_montserrat_28, 0xFFFFFF, 6);

    /* The record and the coins go on ONE line. They were two and were merged
     * to gain the 26 px needed when the chips moved up. */
    a->lbl_best  = make_label(p, "", &aos_montserrat_20, 0xFFFFFF, 192);
    a->lbl_coins = make_label(p, "", &aos_montserrat_20, 0xFFD60A, 192);
    /* Fixed width and height with an ellipsis: they are two absolute boxes in
     * a row, and German grows 20% over Spanish. */
    lv_obj_set_size(a->lbl_best, 168, 26);
    lv_obj_set_x(a->lbl_best, 8);
    lv_label_set_long_mode(a->lbl_best, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(a->lbl_best, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(a->lbl_coins, 168, 26);
    lv_obj_set_x(a->lbl_coins, 192);
    lv_label_set_long_mode(a->lbl_coins, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(a->lbl_coins, LV_TEXT_ALIGN_LEFT, 0);

    make_button(p, _("Jugar"), 64, 224, 240, 48, 0x30D158,
                &aos_montserrat_28, start_cb, a);
    make_button(p, _("Tienda"), 64, 278, 240, 38, 0xBF5AF2,
                &aos_montserrat_20, shop_open_cb, a);

    /* Three chips in a 368 px row. With absolute positions and a fixed width,
     * the layout is measured against the longest text in ANY language and the
     * label clips with an ellipsis: it is what was learned with 2043's chips,
     * where "AUTO OFF" ran off the screen for having been measured in Spanish.
     *
     * And they go at y=322, no lower. Reported from the board: below about
     * 380 px the touch panel does not respond, and in the simulator it touches
     * perfectly. You do not need to know why -the CST816's usable area, the
     * bezel, or both- to draw the rule: NO app in this system puts anything
     * touchable below y=354 (arkanos has its chips at 322..354 and 2043 at
     * 302..334), so that is the proven floor and this app fits itself there.
     * They were at 400..432, fifty pixels below anything that works. */
    a->chip_ctrl = make_chip(p,  10, 322, 112, ctrl_cb, a);
    a->chip_axis = make_chip(p, 128, 322, 112, axis_cb, a);
    a->chip_sfx  = make_chip(p, 246, 322, 112, sfx_cb,  a);

    /* The bottom strip cannot be touched, so it is filled with something that
     * does not need touching: how to play, according to the chosen control. It
     * is the only way to make use of those 90 px without putting a dead button
     * there. */
    a->lbl_ayuda = make_label(p, "", &aos_montserrat_16, 0x6E7A8C, 372);
}

static void build_shop(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->shop = p;

    /* "Back" at the top left and not at the bottom: below y=380 the board's
     * touch panel does not respond (see the menu's chips), and the button for
     * leaving a screen is precisely the one that cannot be dead. */
    make_button(p, LV_SYMBOL_LEFT, 8, 8, 66, 36, 0x8E8E93,
                &aos_montserrat_20, shop_back_cb, a);

    make_label(p, _("Tienda"), &aos_montserrat_28, 0xFFFFFF, 8);
    a->lbl_wallet = make_label(p, "", &aos_montserrat_20, 0xFFD60A, 50);

    /* The arrows go on either side of the preview, which takes the centre. */
    make_button(p, "<", 4, 108, 44, 76, 0x0A84FF, &aos_montserrat_28,
                shop_prev_cb, a);
    make_button(p, ">", 320, 108, 44, 76, 0x0A84FF, &aos_montserrat_28,
                shop_next_cb, a);

    a->lbl_skin  = make_label(p, "", &aos_montserrat_28, 0xFFFFFF, 232);
    a->lbl_price = make_label(p, "", &aos_montserrat_20, 0x9AA3B8, 270);

    a->btn_action = make_button(p, "", 64, 304, 240, 46, 0x30D158,
                                &aos_montserrat_20, shop_action_cb, a);
    a->lbl_action = lv_obj_get_child(a->btn_action, 0);

    /* Same idea as in the menu: the bottom strip is not touched, so a notice
     * goes there. Here it also teaches the gesture, which otherwise is not
     * discovered. */
    make_label(p, _("Deslizá para ver los demás"), &aos_montserrat_16,
               0x6E7A8C, 374);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->pause = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);

    make_label(p, _("Pausa"), &aos_montserrat_36, 0xFFFFFF, 116);

    make_button(p, _("Seguir"), 64, 178, 240, 50, 0x30D158,
                &aos_montserrat_28, resume_cb, a);
    make_button(p, _("Menú"), 64, 236, 240, 40, 0x0A84FF,
                &aos_montserrat_20, menu_cb, a);
    make_button(p, _("Salir"), 64, 284, 240, 40, 0xFF453A,
                &aos_montserrat_20, exit_cb, a);
}

static void build_over(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = make_panel(root);
    a->over = p;
    lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);

    a->lbl_over_t = make_label(p, "", &aos_montserrat_28, 0xFFFFFF, 62);
    a->lbl_over_s = make_label(p, "", &aos_montserrat_48, 0xFFFFFF, 106);
    a->lbl_over_c = make_label(p, "", &aos_montserrat_28, 0xFFD60A, 174);
    a->lbl_over_b = make_label(p, "", &aos_montserrat_16, 0x9AA3B8, 214);

    make_button(p, _("Otra vez"), 64, 250, 240, 50, 0x30D158,
                &aos_montserrat_28, start_cb, a);
    make_button(p, _("Menú"), 64, 308, 240, 40, 0x0A84FF,
                &aos_montserrat_20, menu_cb, a);
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static void *cjump_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;

    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) {
        return NULL;
    }

    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("cjump", "abriendo | interna %u B, psram %u B",
                (unsigned)heap_int, (unsigned)heap_psram);

    /* Five buffers, all through malloc() and not lv_malloc(): with
     * CONFIG_SPIRAM_USE_MALLOC they land in PSRAM, which is where they belong.
     * It is 82 + 82 + 330 KB for the game plus 4 + 68 KB for the preview. */
    size_t chico = (size_t)CJ_W * CJ_H * sizeof(uint16_t);
    a->fbmem = (uint16_t *)malloc(chico);
    a->bgmem = (uint16_t *)malloc(chico);
    a->big   = (uint16_t *)malloc(chico * CJ_SCALE * CJ_SCALE);
    a->pvmem = (uint16_t *)malloc((size_t)PV_W * PV_H * sizeof(uint16_t));
    a->pvbig = (uint16_t *)malloc((size_t)PV_W * PV_H * PV_SCALE * PV_SCALE *
                                  sizeof(uint16_t));
    if (!a->fbmem || !a->bgmem || !a->big || !a->pvmem || !a->pvbig) {
        aos_hal_log("cjump", "out of memory for the buffers");
        free(a->fbmem);
        free(a->bgmem);
        free(a->big);
        free(a->pvmem);
        free(a->pvbig);
        lv_free(a);
        return NULL;
    }
    memset(a->fbmem, 0, chico);
    memset(a->bgmem, 0, chico);
    memset(a->big, 0, chico * CJ_SCALE * CJ_SCALE);
    memset(a->pvmem, 0, (size_t)PV_W * PV_H * sizeof(uint16_t));
    memset(a->pvbig, 0, (size_t)PV_W * PV_H * PV_SCALE * PV_SCALE *
                        sizeof(uint16_t));

    cj_buf_init(&a->g.fb, a->fbmem, CJ_W, CJ_H);
    cj_buf_init(&a->g.bg, a->bgmem, CJ_W, CJ_H);
    cj_buf_init(&a->pvbuf, a->pvmem, PV_W, PV_H);
    a->g.rng = (uint32_t)aos_hal_uptime_ms() | 1u;
    a->owned = (1u << CJ_SKINS_FREE) - 1u;

    prefs_load(a);

    a->root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    a->canvas = lv_canvas_create(root);
    /* The canvas receives the ALREADY upscaled buffer and is drawn 1:1: no STRETCH */
    lv_canvas_set_buffer(a->canvas, a->big, CJ_W * CJ_SCALE, CJ_H * CJ_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, CJ_W * CJ_SCALE, CJ_H * CJ_SCALE);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    /* Touch layer: in LVGL 9 every object is born clickable, so without this
     * the canvas would eat the finger. It starts below the score, which is the
     * pause button. */
    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H - CJ_HUD_H * CJ_SCALE);
    lv_obj_set_pos(a->touch, 0, CJ_HUD_H * CJ_SCALE);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_event, LV_EVENT_PRESS_LOST, a);

    a->hudbtn = lv_obj_create(root);
    lv_obj_remove_style_all(a->hudbtn);
    lv_obj_set_size(a->hudbtn, AOS_SCREEN_W, CJ_HUD_H * CJ_SCALE);
    lv_obj_set_pos(a->hudbtn, 0, 0);
    lv_obj_add_flag(a->hudbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->hudbtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->hudbtn, pause_cb, LV_EVENT_CLICKED, a);

    /* The preview. It is a child of the root and not of a panel because the
     * menu and the shop share it; preview_place() brings it forward and
     * positions it. */
    a->preview = lv_canvas_create(root);
    lv_canvas_set_buffer(a->preview, a->pvbig, PV_W * PV_SCALE, PV_H * PV_SCALE,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->preview, PV_W * PV_SCALE, PV_H * PV_SCALE);
    lv_image_set_antialias(a->preview, false);
    lv_obj_remove_flag(a->preview, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->preview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(a->preview, LV_OBJ_FLAG_HIDDEN);

    build_title(a, root);
    build_shop(a, root);
    build_pause(a, root);
    build_over(a, root);

    /* The gesture is listened for on the ROOT and with GESTURE_BUBBLE taken
     * off it: LVGL does not send LV_EVENT_GESTURE to the object under the
     * finger, it climbs through the parents as long as it finds the flag and
     * gives it to the first one that does not have it. A transparent layer at
     * the bottom of the z-order is never on that path. */
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    /* The menu lets the first zone's sky show through behind. */
    go_title(a);

#ifdef AOS_SIM_BUILTIN
    /* Development switches. They only exist in the simulator: on the board
     * getenv() always returns NULL.
     *
     *   CJ_AUTO=1     the critter plays itself, for leaving it running and
     *                 hunting trails from badly recorded dirty rectangles
     *   CJ_FPS=1      frames per second and % of screen pushed
     *   CJ_COINS=500  coins, for testing the shop without playing
     *   CJ_SKIN=11    costume worn
     *   CJ_OWN=1      every costume unlocked
     *   CJ_SHOP=1     opens straight into the shop
     *   CJ_ZONE=3     starts the game in that zone
     *   CJ_SCREEN=pause|over   opens straight into that panel
     *
     * CJ_SCREEN exists for the layout audit: audit_layout.sh opens each app
     * with AOS_SIM_VIEW and audits what it sees, and what it sees is the menu
     * -the other three panels are born hidden and the auditor skips what is
     * hidden-. With this they can be audited one at a time:
     *
     *   AOS_SIM_AUDIT=de/cjump.over CJ_SCREEN=over AOS_SIM_VIEW=demo.cjump ...
     */
    const char *env;
    if ((env = getenv("CJ_FPS")) && env[0]) {
        a->g.show_fps = 1;
    }
    if ((env = getenv("CJ_COINS")) && env[0]) {
        a->coins_total = (uint32_t)atoi(env);
    }
    if ((env = getenv("CJ_OWN")) && env[0]) {
        a->owned = 0xFFFFu;
    }
    if ((env = getenv("CJ_SKIN")) && env[0]) {
        int v = atoi(env);
        if (v >= 0 && v < CJ_SKINS) {
            a->g.skin = (uint8_t)v;
            a->owned |= (1u << v);
        }
    }
    if ((env = getenv("CJ_SHOP")) && env[0]) {
        a->shop_idx = (int8_t)a->g.skin;
        a->g.state = ST_SHOP;
        overlay_show(a, a->shop);
        shop_refresh(a);
    } else if ((env = getenv("CJ_AUTO")) && env[0]) {
        a->g.autoplay = 1;
        a->g.control  = CTRL_TOUCH;
        game_start(a);
    }
    if ((env = getenv("CJ_SCREEN")) && env[0]) {
        if (env[0] == 'p') {                    /* pause */
            game_start(a);
            pause_show(a);
        } else if (env[0] == 'o') {             /* over */
            game_start(a);
            a->g.score      = 1234;
            a->g.coins_run  = 27;
            a->g.new_record = 1;
            a->g.state      = ST_OVER;
            show_end(a);
        }
    }
    if ((env = getenv("CJ_ZONE")) && env[0]) {
        int v = atoi(env);
        if (v > 0 && v < CJ_ZONES && a->g.state == ST_PLAY) {
            /* The height is faked: the zone comes from the metres, so moving
             * the origin is the only way to get into a real zone. */
            a->g.start_y += (int32_t)cj_zones[v].from_m * 4 * FX;
            a->g.score = cj_zones[v].from_m;
            a->g.zone  = (uint8_t)v;
            a->g.zone_changed = 1;
        }
    }
    /* Only if the switches have not already started on another screen: without
     * this guard, title_refresh() shows the menu's preview again on top of the
     * game CJ_AUTO has just started. */
    if (a->g.state == ST_TITLE) {
        title_refresh(a);
    }
#endif

    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("cjump", "listo | interna %u B, psram %u B | campo %dx%d x%d",
                (unsigned)heap_int, (unsigned)heap_psram, CJ_W, CJ_H, CJ_SCALE);

    a->period = FRAME_MS;
    a->timer  = lv_timer_create(frame, FRAME_MS, a);
    return a;
}

static void cjump_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) {
        return;
    }
    if (a->timer) {
        lv_timer_delete(a->timer);
    }

    /* The objects are deleted HERE and not left to the runtime. close_current()
     * calls destroy() and only then deletes the root, so everything LVGL sends
     * on deleting -LV_EVENT_DELETE, and LV_EVENT_PRESS_LOST if a finger was
     * down- would reach callbacks with the context already freed. This app
     * listens for PRESS_LOST on its touch layer, so it is affected. With the
     * flag set and the deletion done here, those last events are harmless and
     * the empty root the runtime inherits it deletes all the same. */
    a->closing = true;
    if (a->root) {
        lv_obj_clean(a->root);
    }

    /* The coins of a half-played game are not lost: if you leave with the
     * gesture mid-flight, what was collected up to then already counts. */
    if (a->g.state == ST_PLAY || a->g.state == ST_DYING ||
        a->g.state == ST_PAUSE) {
        a->coins_total += a->g.coins_run;
        if (a->g.score > a->g.hiscore) {
            a->g.hiscore = a->g.score;
        }
    }
    prefs_save_all(a);
    prefs_save_opts(a);

    aos_hal_log("cjump", "cerrando | record %u m, %u monedas, disfraz %d",
                (unsigned)a->g.hiscore, (unsigned)a->coins_total, a->g.skin);

    free(a->fbmem);
    free(a->bgmem);
    free(a->big);
    free(a->pvmem);
    free(a->pvbig);
    lv_free(a);
}

/* Leaving mid-jump should not cost the game: it pauses. */
static void cjump_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (a) {
        pause_show(a);
    }
}

static bool cjump_init(aos_app_t *app)
{
    app->desc.id      = "demo.cjump";
    app->desc.name    = "Claude Jump";
    app->desc.icon    = LV_SYMBOL_UP;
    app->desc.icon_vec = AOS_ICON_JUMP;
    /* Sky over grass, which is the game's first zone. The gradient has to be
     * darkish at both ends: the icon catalogue draws the shapes in white, so
     * the colour is set by the app. The first attempt was Claude orange over
     * green and the middle came out brown. */
    app->desc.color_a = 0x4A9DF5;
    app->desc.color_b = 0x2E9E52;
    app->desc.order   = 141;        /* next to Claudito, who is the same critter */
    app->desc.flags   = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                        AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;

    app->create  = cjump_create;
    app->destroy = cjump_destroy;
    app->hide    = cjump_hide;
    app->back    = app_back;
    return true;
}

AOS_APP_ENTRY(cjump_init);
