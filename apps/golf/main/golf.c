/*
 * GOLF - the app: life cycle, preferences, touch, the timer and the screens
 * that are LVGL panels (see gf_app.h for the map of the files)
 *
 * Every word on screen is an LVGL label wrapped in _(); the canvas only gets
 * pictures. Proper names (the holes, the course, the rivals) are not
 * translated.
 */
#include "gf_app.h"
#include "gf_audio.h"

#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_MS    33

#define KEY_COINS   "gf_coins"
#define KEY_EQ      "gf_eq"
#define KEY_UNITS   "gf_units"
#define KEY_SFX     "gf_sfx"
#define KEY_DIFF    "gf_diff"
#define KEY_COURSE  "gf_course"
static const char *const KEY_OWN[CAT_N] = { "gf_own0", "gf_own1", "gf_own2", "gf_own3", "gf_own4", "gf_own5" };
static const char *const KEY_BEST[3] = { "gf_best0", "gf_best1", "gf_best2" };

static bool s_sfx = true;

void gf_sfx(int freq, int ms)
{
    if (s_sfx) gf_audio_tone(freq, ms);
}

void gf_sound(int id)
{
    if (s_sfx) gf_snd(id);
}

void gf_fmt_dist(const app_t *a, char *buf, int n, float m)
{
    if (a->metres) {
        if (m < 20.0f) snprintf(buf, (size_t)n, "%d.%d m", (int)m, (int)(m * 10) % 10);
        else snprintf(buf, (size_t)n, "%d m", (int)(m + 0.5f));
    } else {
        if (m < 18.0f) snprintf(buf, (size_t)n, "%d ft", (int)(m * 3.281f + 0.5f));
        else snprintf(buf, (size_t)n, "%d yd", (int)(m / GF_YD + 0.5f));
    }
}

/* --------------------------------------------------------------------------
 * Interface pieces
 * -------------------------------------------------------------------------- */

lv_obj_t *gfa_panel(lv_obj_t *parent, bool dim)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    if (dim) {
        lv_obj_set_style_bg_color(p, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
    }
    return p;
}

lv_obj_t *gfa_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t *gfa_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                     uint32_t accent, const lv_font_t *font, lv_event_cb_t cb, void *data)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x12161C), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w - 10);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static lv_obj_t *all_panels(app_t *a, int i)
{
    lv_obj_t *const p[] = { a->p_menu, a->p_setup, a->p_pause, a->p_hole, a->p_round, a->p_settings, a->p_loading, a->p_shop, a->p_boot };
    return i < (int)(sizeof(p) / sizeof(p[0])) ? p[i] : NULL;
}

void gfa_show_panel(app_t *a, lv_obj_t *show)
{
    for (int i = 0; i < 9; i++) {
        lv_obj_t *p = all_panels(a, i);
        if (p && p != show) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    }
    if (show) {
        lv_obj_remove_flag(show, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(show);
    }
}

void gfa_hud_show(app_t *a, bool top, bool bottom)
{
    lv_obj_t *const tops[] = { a->hud_top, a->btn_pause };
    for (unsigned i = 0; i < 2; i++) {
        if (top) lv_obj_remove_flag(tops[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tops[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *const bots[] = { a->hud_bot, a->lbl_lie };
    for (unsigned i = 0; i < 2; i++) {
        if (bottom) lv_obj_remove_flag(bots[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(bots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void gfa_banner(app_t *a, const char *txt, uint32_t color, int ms)
{
    lv_label_set_text(a->banner, txt);
    lv_obj_set_style_text_color(a->banner, lv_color_hex(color), 0);
    lv_obj_remove_flag(a->banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->banner);
    lv_obj_set_user_data(a->banner, (void *)(intptr_t)ms);
}

void gfa_invalidate_all(app_t *a)
{
    lv_obj_invalidate(a->canvas);
}

/* --------------------------------------------------------------------------
 * Preferences
 * -------------------------------------------------------------------------- */

static void prefs_load(app_t *a)
{
    int32_t v;
    gf_wardrobe_default(&a->wr);
    if (aos_hal_pref_get_i32(KEY_COINS, &v)) a->wr.coins = v;
    for (int c = 0; c < CAT_N; c++) {
        if (aos_hal_pref_get_i32(KEY_OWN[c], &v)) a->wr.own[c] |= (uint32_t)v;
    }
    if (aos_hal_pref_get_i32(KEY_EQ, &v)) {
        for (int c = 0; c < CAT_N; c++) {
            int it = (int)((uint32_t)v >> (c * 4)) & 15;
            if (it < gf_item_n(c) && gf_owns(&a->wr, c, it)) a->wr.eq[c] = (uint8_t)it;
        }
    }
    a->metres = aos_hal_pref_get_i32(KEY_UNITS, &v) && v == 1;
    if (aos_hal_pref_get_i32(KEY_SFX, &v)) s_sfx = v != 0;
    a->sfx = s_sfx;
    if (aos_hal_pref_get_i32(KEY_DIFF, &v) && v >= 0 && v < DIFF_N) a->diff = v;
    else a->diff = DIFF_NORMAL;
    if (aos_hal_pref_get_i32(KEY_COURSE, &v) && v >= 0 && v < gf_course_n()) a->course = v;
    gf_course_select(a->course);
    for (int i = 0; i < 3; i++) {
        a->best[i] = aos_hal_pref_get_i32(KEY_BEST[i], &v) ? v : 999;
    }
}

void gfa_prefs_save(app_t *a)
{
    aos_hal_pref_set_i32(KEY_COINS, a->wr.coins);
    for (int c = 0; c < CAT_N; c++) aos_hal_pref_set_i32(KEY_OWN[c], (int32_t)a->wr.own[c]);
    uint32_t eq = 0;
    for (int c = 0; c < CAT_N; c++) eq |= (uint32_t)(a->wr.eq[c] & 15) << (c * 4);
    aos_hal_pref_set_i32(KEY_EQ, (int32_t)eq);
    aos_hal_pref_set_i32(KEY_UNITS, a->metres ? 1 : 0);
    aos_hal_pref_set_i32(KEY_SFX, s_sfx ? 1 : 0);
    aos_hal_pref_set_i32(KEY_DIFF, a->diff);
    aos_hal_pref_set_i32(KEY_COURSE, a->course);
    for (int i = 0; i < 3; i++) aos_hal_pref_set_i32(KEY_BEST[i], a->best[i]);
}

/* --------------------------------------------------------------------------
 * States
 * -------------------------------------------------------------------------- */

static void menu_refresh(app_t *a)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", (int)a->wr.coins);
    lv_label_set_text(a->lbl_coins, buf);
}

static void hole_end_show(app_t *a);
static void round_end_show(app_t *a);

void gfa_set_state(app_t *a, int st)
{
    int old = a->state;
    /* frames per second of the states that animate, in the log: what the
     * swing, the flight and the card really run at on the board */
    uint32_t el = (uint32_t)(aos_hal_uptime_ms() - a->st_t0);
    if (a->st_t0 && el > 1500 && (old == ST_SWING || old == ST_FLIGHT || old == ST_ROLL ||
                                  old == ST_HOLE_END || old == ST_MENU || old == ST_SHOP))
        aos_hal_log("golf", "state %d: %u frames in %u ms, %u.%u fps", old, (unsigned)a->st_frames,
                    (unsigned)el, (unsigned)(a->st_frames * 1000 / el),
                    (unsigned)(a->st_frames * 10000 / el % 10));
    a->st_t0 = aos_hal_uptime_ms();
    a->st_frames = 0;
    a->state = st;
    a->st_ms = 0;
    lv_obj_add_flag(a->btn_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->lbl_wind, LV_OBJ_FLAG_HIDDEN);
    switch (st) {
    case ST_MENU:
        gfa_hud_show(a, false, false);
        menu_refresh(a);
        if (old != ST_MENU) gfp_menu_scene(a);
        /* the menu appears when its picture is there; until then, the
         * loading screen says what is happening */
        if (a->menu_ready) {
            gfa_show_panel(a, a->p_menu);
        } else {
            if (!a->booting && a->boot_pct > 60) a->boot_pct = 60;
            a->boot_target = 92;
            lv_label_set_text((lv_obj_t *)lv_obj_get_user_data(a->p_boot), gf_course()->name);
            gfa_show_panel(a, a->p_boot);
        }
        break;
    case ST_SETUP:
    case ST_SETTINGS:
        gfa_hud_show(a, false, false);
        gfa_show_panel(a, st == ST_SETUP ? a->p_setup : a->p_settings);
        break;
    case ST_LOADING:
        gfa_hud_show(a, false, false);
        gfa_show_panel(a, a->p_loading);
        break;
    case ST_AIM:
        gfa_show_panel(a, NULL);
        gfa_hud_show(a, true, true);
        lv_obj_remove_flag(a->lbl_wind, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(a->lbl_hit, _("Golpear"));
        break;
    case ST_PUTT:
        gfa_show_panel(a, NULL);
        gfa_hud_show(a, true, true);
        lv_label_set_text(a->lbl_hit, _("Pegar"));
        break;
    case ST_SWING:
        gfa_show_panel(a, NULL);
        gfa_hud_show(a, true, false);
        break;
    case ST_FLIGHT:
    case ST_ROLL:
    case ST_RESULT:
    case ST_REMOTE:
        gfa_show_panel(a, NULL);
        gfa_hud_show(a, true, false);
        break;
    case ST_HOLE_END:
        gfa_hud_show(a, false, false);
        hole_end_show(a);
        gfa_show_panel(a, a->p_hole);
        break;
    case ST_ROUND_END:
        gfa_hud_show(a, false, false);
        round_end_show(a);
        gfa_show_panel(a, a->p_round);
        break;
    case ST_SHOP:
        gfa_hud_show(a, false, false);
        gfa_show_panel(a, a->p_shop);
        gfs_open(a);
        break;
    }
    lv_obj_move_foreground(a->banner);
    gf_audio_ambience(st >= ST_AIM && st <= ST_REMOTE);
}

/* --------------------------------------------------------------------------
 * Menu and setup
 * -------------------------------------------------------------------------- */

static const char *const MODE_NAME[MODE_N] = {
    N_("Juego rápido"), N_("Torneo"), N_("Práctica"), N_("Multijugador"), N_("Multijugador"),
};
static const char *const DIFF_NAME[DIFF_N] = { N_("Fácil"), N_("Normal"), N_("Profesional") };
static const char *const DIFF_DESC[DIFF_N] = {
    N_("Poco viento, medidor lento, ves dónde cae"),
    N_("Viento real, medidor ágil"),
    N_("Viento fuerte, medidor rápido, sin ayudas"),
};
static const uint32_t DIFF_COL[DIFF_N] = { 0x30D158, 0x0A84FF, 0xFF453A };

static void start_cb(lv_event_t *e);

typedef struct { app_t *a; int v; } cbv_t;
static cbv_t s_cbv[32];
static int s_ncbv;

static cbv_t *cbv(app_t *a, int v)
{
    if (s_ncbv >= 32) s_ncbv = 0;
    cbv_t *c = &s_cbv[s_ncbv++];
    c->a = a;
    c->v = v;
    return c;
}

static void diff_cb(lv_event_t *e)
{
    cbv_t *c = (cbv_t *)lv_event_get_user_data(e);
    c->a->diff = c->v;
    gfa_prefs_save(c->a);
    start_cb(e);
}

static void hole_pick_cb(lv_event_t *e)
{
    cbv_t *c = (cbv_t *)lv_event_get_user_data(e);
    c->a->practice_hole = c->v;
    lv_obj_t *box = c->a->setup_box;
    /* highlight the picked hole */
    for (uint32_t i = 0; i < lv_obj_get_child_count(box); i++) {
        lv_obj_t *b = lv_obj_get_child(box, (int32_t)i);
        cbv_t *bc = (cbv_t *)lv_obj_get_user_data(b);
        if (bc && bc->v >= 100) {
            bool on = bc->v - 100 == c->v;
            lv_obj_set_style_bg_color(b, lv_color_hex(on ? 0x30D158 : 0x12161C), 0);
        }
    }
}

static void players_cb(lv_event_t *e)
{
    cbv_t *c = (cbv_t *)lv_event_get_user_data(e);
    app_t *a = c->a;
    if (c->v == 0) {
        a->mode = MODE_LINK;
        a->nplayers = 2;
        gfl_begin(a);
        return;
    }
    gfl_end(a);                 /* the radio went up to look for a partner */
    a->mode = MODE_LOCAL;
    a->nplayers = c->v;
    gfp_round_start(a);
}

static void start_cb(lv_event_t *e)
{
    cbv_t *c = (cbv_t *)lv_event_get_user_data(e);
    gfp_round_start(c->a);
}

static void setup_open(app_t *a, int mode);

static void course_step(app_t *a, int d)
{
    a->course = (a->course + d + gf_course_n()) % gf_course_n();
    gf_course_select(a->course);
    if (a->practice_hole >= gf_course()->nholes) a->practice_hole = 0;
    gfa_prefs_save(a);
    lv_label_set_text(a->lbl_menu_course, gf_course()->name);
    gf_sfx(900, 15);
    setup_open(a, a->mode == MODE_LINK ? MODE_LOCAL : a->mode);
}

static void course_prev_cb(lv_event_t *e) { course_step((app_t *)lv_event_get_user_data(e), -1); }
static void course_next_cb(lv_event_t *e) { course_step((app_t *)lv_event_get_user_data(e), 1); }

static void setup_open(app_t *a, int mode)
{
    a->mode = mode;
    a->nplayers = 1;
    lv_label_set_text(a->setup_title, _(MODE_NAME[mode]));
    lv_obj_clean(a->setup_box);
    s_ncbv = 0;
    int y = 0;
    /* the course, on top of every setup screen */
    gfa_button(a->setup_box, LV_SYMBOL_LEFT, 0, 0, 44, 38, 0x8E8E93, &aos_montserrat_16, course_prev_cb, a);
    lv_obj_t *cn = gfa_label(a->setup_box, gf_course()->name, &aos_montserrat_20, 0xD8F0D8, 46, 7, 208);
    lv_label_set_long_mode(cn, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(cn, 26);
    gfa_button(a->setup_box, LV_SYMBOL_RIGHT, 256, 0, 44, 38, 0x8E8E93, &aos_montserrat_16, course_next_cb, a);
    y = 46;
    if (mode == MODE_LOCAL) {
        lv_obj_t *l = gfa_label(a->setup_box, _("En este reloj, pasándolo"), &aos_montserrat_16, 0xA0A8B8, 0, y, 300);
        (void)l;
        y += 26;
        for (int n = 2; n <= 4; n++) {
            char buf[32];
            snprintf(buf, sizeof buf, "%d %s", n, _("jugadores"));
            gfa_button(a->setup_box, buf, 0, y, 300, 44, 0x30D158, &aos_montserrat_20, players_cb, cbv(a, n));
            y += 52;
        }
        char nm[24];
        y += 6;
        if (gfl_available(a, nm, sizeof nm)) {
            char buf[48];
            snprintf(buf, sizeof buf, "%s %s", _("Contra"), nm);
            gfa_button(a->setup_box, buf, 0, y, 300, 50, 0xBF5AF2, &aos_montserrat_20, players_cb, cbv(a, 0));
        } else {
            gfa_label(a->setup_box, _("Aparea otro reloj en Enlace para jugar a distancia"), &aos_montserrat_14, 0x8A93A6, 0, y + 4, 300);
        }
    } else {
        if (mode == MODE_PRACTICE) {
            for (int h = 0; h < gf_course()->nholes; h++) {
                char buf[8];
                snprintf(buf, sizeof buf, "%d", h + 1);
                lv_obj_t *b = gfa_button(a->setup_box, buf, (h % 4) * 76, y + (h / 4) * 48, 68, 40, 0x30D158,
                                         &aos_montserrat_20, hole_pick_cb, cbv(a, h));
                lv_obj_set_user_data(b, cbv(a, 100 + h));
                if (h == a->practice_hole) lv_obj_set_style_bg_color(b, lv_color_hex(0x30D158), 0);
            }
            y += 102;
        }
        for (int d = 0; d < DIFF_N; d++) {
            lv_obj_t *b = gfa_button(a->setup_box, "", 0, y, 300, mode == MODE_PRACTICE ? 44 : 64,
                                     DIFF_COL[d], &aos_montserrat_20, diff_cb, cbv(a, d));
            lv_obj_t *l = lv_obj_get_child(b, 0);
            lv_label_set_text(l, _(DIFF_NAME[d]));
            lv_obj_align(l, LV_ALIGN_TOP_MID, 0, mode == MODE_PRACTICE ? 10 : 8);
            if (mode != MODE_PRACTICE) {
                lv_obj_t *s = gfa_label(b, _(DIFF_DESC[d]), &aos_montserrat_14, 0xA0A8B8, 6, 36, 284);
                lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
                lv_obj_set_height(s, 18);
            }
            if (d == a->diff) lv_obj_set_style_border_width(b, 4, 0);
            y += mode == MODE_PRACTICE ? 50 : 72;
        }
    }
    gfa_set_state(a, ST_SETUP);
}

static void menu_quick_cb(lv_event_t *e) { setup_open((app_t *)lv_event_get_user_data(e), MODE_QUICK); }
static void menu_tour_cb(lv_event_t *e)  { setup_open((app_t *)lv_event_get_user_data(e), MODE_TOUR); }
static void menu_prac_cb(lv_event_t *e)  { setup_open((app_t *)lv_event_get_user_data(e), MODE_PRACTICE); }
static void menu_multi_cb(lv_event_t *e) { setup_open((app_t *)lv_event_get_user_data(e), MODE_LOCAL); }
static void menu_shop_cb(lv_event_t *e)  { gfa_set_state((app_t *)lv_event_get_user_data(e), ST_SHOP); }

static void settings_refresh(app_t *a)
{
    lv_label_set_text(lv_obj_get_child(a->chip_units, 0), a->metres ? _("Metros") : _("Yardas"));
    lv_label_set_text(lv_obj_get_child(a->chip_sfx, 0), s_sfx ? _("Sonido: sí") : _("Sonido: no"));
}

static void menu_settings_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    settings_refresh(a);
    gfa_set_state(a, ST_SETTINGS);
}

static void units_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->metres = !a->metres;
    gfa_prefs_save(a);
    settings_refresh(a);
}

static void sfx_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    s_sfx = !s_sfx;
    a->sfx = s_sfx;
    gf_audio_mute(!s_sfx);
    gfa_prefs_save(a);
    settings_refresh(a);
    gf_sfx(1200, 30);
}

static void to_menu_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->paused = false;
    if (a->mode == MODE_LINK) gfl_end(a);
    gfa_set_state(a, ST_MENU);
}

static void exit_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->leaving = true;
    a->want_exit = true;
}

/* --------------------------------------------------------------------------
 * Pause, hole card, round card
 * -------------------------------------------------------------------------- */

static void pause_show(app_t *a)
{
    if (a->state < ST_AIM || a->state > ST_REMOTE || a->state == ST_HOLE_END || a->state == ST_ROUND_END) return;
    a->paused = true;
    lv_obj_remove_flag(a->p_pause, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(a->p_pause);
}

static void pause_cb(lv_event_t *e)
{
    pause_show((app_t *)lv_event_get_user_data(e));
}

static void resume_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    a->paused = false;
    a->prev_ms = 0;
    lv_obj_add_flag(a->p_pause, LV_OBJ_FLAG_HIDDEN);
}

static void hit_cb(lv_event_t *e)     { gfp_hit_pressed((app_t *)lv_event_get_user_data(e)); }
static void prev_cb(lv_event_t *e)    { gfp_club_step((app_t *)lv_event_get_user_data(e), -1); }
static void next_cb(lv_event_t *e)    { gfp_club_step((app_t *)lv_event_get_user_data(e), 1); }
static void back3d_cb(lv_event_t *e)  { gfp_back((app_t *)lv_event_get_user_data(e)); }

static void fill_table(app_t *a, lv_obj_t *t, bool final)
{
    lv_obj_clean(t);
    const gf_game_t *g = &a->game;
    char buf[64];
    int y = 0;
    int rows = g->nplayers + g->nrivals;
    /* sort: players and rivals by total */
    int order[GF_MAX_PLAYERS + GF_RIVALS], tot[GF_MAX_PLAYERS + GF_RIVALS];
    for (int i = 0; i < rows; i++) {
        order[i] = i;
        tot[i] = i < g->nplayers ? gf_game_total(g, i) : gf_game_rival_total(g, i - g->nplayers);
    }
    for (int i = 0; i < rows; i++)
        for (int j = i + 1; j < rows; j++)
            if (tot[order[j]] < tot[order[i]]) { int k = order[i]; order[i] = order[j]; order[j] = k; }
    int par = gf_game_par_so_far(g, g->hi);
    for (int r = 0; r < rows; r++) {
        int i = order[r];
        const char *nm;
        char pn[32];
        if (i < g->nplayers) {
            if (g->mode == MODE_LINK) nm = i == a->local_player ? _("Vos") : a->partner;
            else if (g->nplayers == 1) nm = _("Vos");
            else { snprintf(pn, sizeof pn, "%s %d", _("Jugador"), i + 1); nm = pn; }
        } else {
            nm = gf_rival_name(g->rival[i - g->nplayers].name);
        }
        int d = tot[i] - par;
        char rel[12];
        if (d == 0) snprintf(rel, sizeof rel, "E");
        else snprintf(rel, sizeof rel, "%+d", d);
        snprintf(buf, sizeof buf, "%d. %s", r + 1, nm);
        uint32_t col = i < g->nplayers ? 0xFFFFFF : 0xA0A8B8;
        lv_obj_t *l = gfa_label(t, buf, &aos_montserrat_16, col, 0, y, 200);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, 20);
        snprintf(buf, sizeof buf, "%d  (%s)", tot[i], rel);
        l = gfa_label(t, buf, &aos_montserrat_16, d < 0 ? 0xFF6A5A : col, 200, y, 100);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
        y += 24;
    }
    (void)final;
}

static void hole_end_show(app_t *a)
{
    const gf_game_t *g = &a->game;
    int par = gf_course()->holes[gf_game_hole(g)].par;
    int me = g->mode == MODE_LINK ? a->local_player : 0;
    int s = g->pl[me].strokes[g->hi];
    char buf[64];
    lv_label_set_text(a->lbl_hole_t, g->pl[me].picked ? _("Recogida") : _(gf_score_name(s, par)));
    lv_obj_set_style_text_color(a->lbl_hole_t, lv_color_hex(s < par ? 0xFFD60A : 0xFFFFFF), 0);
    snprintf(buf, sizeof buf, "%s %d  ·  %d %s  ·  %s %d", _("Hoyo"), gf_game_hole(g) + 1, s, _("golpes"), _("Par"), par);
    lv_label_set_text(a->lbl_hole_s, buf);
    fill_table(a, a->hole_table, false);
    if (g->mode == MODE_PRACTICE) lv_label_set_text(a->lbl_hole_c, "");
    else {
        snprintf(buf, sizeof buf, "%s %d", _("Monedas"), gf_game_coins(g));
        lv_label_set_text(a->lbl_hole_c, buf);
    }
    if (s <= par - 1) {
        gf_sound(SND_GOOD);
        gf_sound(SND_APPLAUSE);
        gfp_react_begin(a, SEQ_CHEER);
    } else if (s >= par + 2 || g->pl[me].picked) {
        gf_sound(SND_BAD);
        gfp_react_begin(a, SEQ_SAD);
    } else {
        gfp_react_begin(a, SEQ_IDLE);
    }
}

static void round_end_show(app_t *a)
{
    const gf_game_t *g = &a->game;
    int me = g->mode == MODE_LINK ? a->local_player : 0;
    char buf[80];
    int tot = gf_game_total(g, me), to = gf_game_to_par(g, me);
    int coins = gf_game_coins(g);
    a->wr.coins += coins;
    bool record = false;
    if (g->mode <= MODE_PRACTICE && g->mode != MODE_PRACTICE && to < a->best[g->mode]) {
        a->best[g->mode] = to;
        record = true;
    }
    gfa_prefs_save(a);
    if (g->mode == MODE_TOUR) {
        int place = gf_game_place(g);
        snprintf(buf, sizeof buf, "%s %d%s", _("Terminaste"), place, place == 1 ? " \xF0\x9F\x8F\x86" : "");
        lv_label_set_text(a->lbl_round_t, place == 1 ? _("¡Campeón!") : buf);
    } else if (g->mode == MODE_LINK) {
        int other = gf_game_total(g, 1 - me);
        lv_label_set_text(a->lbl_round_t, tot < other ? _("¡Ganaste!") : (tot == other ? _("Empate") : _("Perdiste")));
    } else {
        lv_label_set_text(a->lbl_round_t, record ? _("¡Nuevo récord!") : _("Fin de la vuelta"));
    }
    char rel[12];
    if (to == 0) snprintf(rel, sizeof rel, "E");
    else snprintf(rel, sizeof rel, "%+d", to);
    snprintf(buf, sizeof buf, "%d %s  (%s)", tot, _("golpes"), rel);
    lv_label_set_text(a->lbl_round_s, buf);
    fill_table(a, a->round_table, true);
    if (g->mode == MODE_PRACTICE) lv_label_set_text(a->lbl_round_c, "");
    else {
        snprintf(buf, sizeof buf, "+%d %s  ·  %s %d", coins, _("monedas"), _("Total"), (int)a->wr.coins);
        lv_label_set_text(a->lbl_round_c, buf);
    }
    gf_sound(SND_GOOD);
    if (g->mode == MODE_TOUR && gf_game_place(g) == 1) gf_sound(SND_APPLAUSE);
}

static void hole_next_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->art_busy) {
        /* the reactions are only for the card: give their memory back */
        gf_art_release(SEQ_CHEER);
        gf_art_release(SEQ_SAD);
    }
    if (a->game.mode == MODE_PRACTICE) {
        /* the same hole again */
        a->game.pl[0].strokes[0] = 0;
        gfp_hole_start(a);
        return;
    }
    if (gf_game_next_hole(&a->game)) gfp_hole_start(a);
    else gfa_set_state(a, ST_ROUND_END);
}

static void again_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->mode == MODE_LINK) {
        gfa_set_state(a, ST_MENU);
        return;
    }
    gfp_round_start(a);
}

/* --------------------------------------------------------------------------
 * Building the screens
 * -------------------------------------------------------------------------- */

static void build_hud(app_t *a, lv_obj_t *root)
{
    a->hud_top = lv_obj_create(root);
    lv_obj_remove_style_all(a->hud_top);
    lv_obj_set_size(a->hud_top, AOS_SCREEN_W, 58);
    lv_obj_set_style_bg_color(a->hud_top, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->hud_top, LV_OPA_50, 0);
    lv_obj_remove_flag(a->hud_top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->hud_top, LV_OBJ_FLAG_SCROLLABLE);
    a->lbl_hole = gfa_label(a->hud_top, "", &aos_montserrat_20, 0xFFFFFF, 60, 6, 248);
    a->lbl_info = gfa_label(a->hud_top, "", &aos_montserrat_16, 0xD8E0EA, 40, 32, 288);
    lv_label_set_long_mode(a->lbl_hole, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(a->lbl_info, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(a->lbl_hole, 24);
    lv_obj_set_height(a->lbl_info, 20);

    a->btn_pause = gfa_button(root, LV_SYMBOL_LIST, 10, 6, 52, 46, 0x8E8E93, &aos_montserrat_20, pause_cb, a);
    lv_obj_set_style_bg_opa(a->btn_pause, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(a->btn_pause, 0, 0);

    a->lbl_wind = gfa_label(root, "", &aos_montserrat_14, 0xFFFFFF, 300, 112, 72);

    a->lbl_lie = gfa_label(root, "", &aos_montserrat_16, 0xFFFFFF, 20, 312, 150);
    lv_obj_set_style_bg_color(a->lbl_lie, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->lbl_lie, LV_OPA_50, 0);
    lv_obj_set_style_radius(a->lbl_lie, 10, 0);
    lv_obj_set_style_pad_ver(a->lbl_lie, 3, 0);
    lv_obj_set_width(a->lbl_lie, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(a->lbl_lie, 10, 0);

    a->hud_bot = lv_obj_create(root);
    lv_obj_remove_style_all(a->hud_bot);
    lv_obj_set_size(a->hud_bot, AOS_SCREEN_W, 102);
    lv_obj_set_pos(a->hud_bot, 0, 346);
    lv_obj_set_style_bg_color(a->hud_bot, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->hud_bot, LV_OPA_50, 0);
    lv_obj_remove_flag(a->hud_bot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->hud_bot, LV_OBJ_FLAG_CLICKABLE);
    a->btn_prev = gfa_button(a->hud_bot, LV_SYMBOL_LEFT, 16, 6, 50, 52, 0x8E8E93, &aos_montserrat_20, prev_cb, a);
    a->lbl_club = gfa_label(a->hud_bot, "", &aos_montserrat_28, 0xFFFFFF, 68, 4, 110);
    a->lbl_carry = gfa_label(a->hud_bot, "", &aos_montserrat_14, 0xB8C2D0, 68, 38, 110);
    a->btn_next = gfa_button(a->hud_bot, LV_SYMBOL_RIGHT, 180, 6, 50, 52, 0x8E8E93, &aos_montserrat_20, next_cb, a);
    a->btn_hit = gfa_button(a->hud_bot, "", 238, 6, 114, 52, 0x30D158, &aos_montserrat_20, hit_cb, a);
    lv_obj_set_style_bg_color(a->btn_hit, lv_color_hex(0x1E7A3A), 0);
    a->lbl_hit = lv_obj_get_child(a->btn_hit, 0);

    a->btn_back = gfa_button(root, LV_SYMBOL_LEFT, 12, 66, 52, 46, 0x8E8E93, &aos_montserrat_20, back3d_cb, a);
    a->lbl_hint = gfa_label(root, "", &aos_montserrat_16, 0xFFFFFF, 34, 410, 260);
    lv_label_set_text(a->lbl_hint, _("Tocá: carga, potencia, precisión"));
    lv_obj_set_style_bg_color(a->lbl_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->lbl_hint, LV_OPA_50, 0);
    lv_obj_set_style_radius(a->lbl_hint, 10, 0);

    a->banner = lv_label_create(root);
    lv_label_set_text(a->banner, "");
    lv_obj_set_style_text_font(a->banner, &aos_montserrat_28, 0);
    lv_obj_set_style_bg_color(a->banner, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->banner, LV_OPA_60, 0);
    lv_obj_set_style_radius(a->banner, 18, 0);
    lv_obj_set_style_pad_hor(a->banner, 16, 0);
    lv_obj_set_style_pad_ver(a->banner, 8, 0);
    lv_obj_set_style_text_align(a->banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_width(a->banner, 330, 0);
    lv_label_set_long_mode(a->banner, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(a->banner, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_remove_flag(a->banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a->banner, LV_OBJ_FLAG_HIDDEN);
}

static void build_menu(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, false);
    a->p_menu = p;
    lv_obj_t *sh = gfa_label(p, "Golf", &aos_montserrat_48, 0x0A2A12, 182, 30, 180);
    (void)sh;
    gfa_label(p, "Golf", &aos_montserrat_48, 0xFFFFFF, 180, 27, 180);
    a->lbl_menu_course = gfa_label(p, gf_course()->name, &aos_montserrat_16, 0xD8F0D8, 180, 84, 180);

    lv_obj_t *coin = lv_obj_create(p);
    lv_obj_remove_style_all(coin);
    lv_obj_set_size(coin, 110, 30);
    lv_obj_set_pos(coin, 232, 108);
    lv_obj_set_style_bg_color(coin, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(coin, LV_OPA_50, 0);
    lv_obj_set_style_radius(coin, 15, 0);
    lv_obj_remove_flag(coin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *dot = lv_obj_create(coin);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 18, 18);
    lv_obj_set_pos(dot, 8, 6);
    lv_obj_set_style_radius(dot, 9, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFD60A), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(0xB8860B), 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    a->lbl_coins = gfa_label(coin, "0", &aos_montserrat_20, 0xFFD60A, 30, 3, 74);
    lv_obj_set_style_text_align(a->lbl_coins, LV_TEXT_ALIGN_LEFT, 0);

    int x = 184, w = 168;
    gfa_button(p, _("Juego rápido"), x, 146, w, 46, 0x30D158, &aos_montserrat_20, menu_quick_cb, a);
    gfa_button(p, _("Torneo"), x, 198, w, 46, 0xFFD60A, &aos_montserrat_20, menu_tour_cb, a);
    gfa_button(p, _("Práctica"), x, 250, w, 46, 0x64D2FF, &aos_montserrat_20, menu_prac_cb, a);
    gfa_button(p, _("Multijugador"), x, 302, w, 46, 0xBF5AF2, &aos_montserrat_20, menu_multi_cb, a);
    gfa_button(p, _("Tienda"), x, 356, 82, 44, 0xFF9F0A, &aos_montserrat_16, menu_shop_cb, a);
    gfa_button(p, LV_SYMBOL_SETTINGS, x + 88, 356, 80, 44, 0x8E8E93, &aos_montserrat_20, menu_settings_cb, a);
}

static void setup_back_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    gfl_end(a);                 /* if the multiplayer screen started the radio */
    gfa_set_state(a, ST_MENU);
}

static void build_setup(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, true);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    a->p_setup = p;
    a->setup_title = gfa_label(p, "", &aos_montserrat_28, 0xFFFFFF, 60, 22, 248);
    gfa_button(p, LV_SYMBOL_LEFT, 12, 18, 48, 44, 0x8E8E93, &aos_montserrat_20, setup_back_cb, a);
    a->setup_box = lv_obj_create(p);
    lv_obj_remove_style_all(a->setup_box);
    lv_obj_set_size(a->setup_box, 300, 330);
    lv_obj_set_pos(a->setup_box, 34, 78);
    lv_obj_remove_flag(a->setup_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->setup_box, LV_OBJ_FLAG_CLICKABLE);
}

static void build_settings(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, true);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    a->p_settings = p;
    gfa_label(p, _("Ajustes"), &aos_montserrat_28, 0xFFFFFF, 60, 22, 248);
    gfa_button(p, LV_SYMBOL_LEFT, 12, 18, 48, 44, 0x8E8E93, &aos_montserrat_20, setup_back_cb, a);
    gfa_label(p, _("Distancias en"), &aos_montserrat_16, 0xA0A8B8, 34, 100, 300);
    a->chip_units = gfa_button(p, "", 64, 126, 240, 48, 0x0A84FF, &aos_montserrat_20, units_cb, a);
    a->chip_sfx = gfa_button(p, "", 64, 196, 240, 48, 0x30D158, &aos_montserrat_20, sfx_cb, a);
    gfa_label(p, _("Ganás monedas al jugar: más en torneo, en difícil y con birdies. Gastalas en la tienda."),
              &aos_montserrat_14, 0x8A93A6, 44, 270, 280);
}

/* --------------------------------------------------------------------------
 * The loading screen: the first seconds, and whenever the menu's picture is
 * being prepared. A bar that always moves, so nobody thinks it hung.
 * -------------------------------------------------------------------------- */

static void splash_paint(app_t *a)
{
    /* a sky over mown grass, drawn in a few milliseconds: behind the first
     * screen instead of black */
    for (int y = 0; y < GF_H; y++) {
        uint16_t *row = a->fb + (size_t)y * GF_W;
        for (int x = 0; x < GF_W; x++) {
            int r, g, b;
            if (y < 250) {
                int t = y * 256 / 250;
                r = 52 + (186 - 52) * t / 256;
                g = 112 + (212 - 112) * t / 256;
                b = 200 + (232 - 200) * t / 256;
            } else {
                int t = (y - 250) * 256 / (GF_H - 250);
                int stripe = ((x + (y - 250) * 2) / 46) & 1 ? 10 : -6;
                r = 64 + 22 * t / 256 + stripe / 2;
                g = 140 + 34 * t / 256 + stripe;
                b = 52 + 12 * t / 256;
            }
            row[x] = gf_dither(r, g, b, x, y);
        }
    }
    lv_obj_invalidate(a->canvas);
}

void gfa_menu_ready(app_t *a)
{
    if (a->booting) return;           /* still loading the art */
    a->boot_pct = 100;
    lv_bar_set_value(a->boot_bar, 100, LV_ANIM_OFF);
    gfa_show_panel(a, a->p_menu);
    if (a->dev_card) {
        /* a birdie on the first hole, over the menu's picture */
        a->dev_card = false;
        gf_game_new(&a->game, MODE_QUICK, a->diff, 1, 1234, 0);
        a->game.holes[0] = 0;
        a->game.pl[0].strokes[0] = 3;
        a->game.pl[0].done = 1;
        gfa_set_state(a, ST_HOLE_END);
    }
}

static void boot_frame(app_t *a, int dt)
{
    (void)dt;
    /* towards the stage's target, and a creep even when it is reached, so
     * the bar never sits still */
    int want = a->boot_target;
    if (a->boot_pct < want) a->boot_pct += (want - a->boot_pct + 7) / 8;
    else if (a->boot_pct < want + 6 && a->boot_pct < 99 && (lv_tick_get() & 511) < 40) a->boot_pct++;
    lv_bar_set_value(a->boot_bar, a->boot_pct, LV_ANIM_OFF);
    static uint32_t last_dots;
    uint32_t dots = (lv_tick_get() / 400) % 4;
    if (dots != last_dots) {
        last_dots = dots;
        char buf[64];
        snprintf(buf, sizeof buf, "%s%.*s", a->booting ? _("Cargando") : _("Preparando la cancha"), (int)dots, "...");
        lv_label_set_text(a->lbl_boot_st, buf);
    }
}

static void build_boot(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, false);
    a->p_boot = p;
    lv_obj_t *sh = gfa_label(p, "Golf", &aos_montserrat_48, 0x0A2A12, 36, 113, 300);
    (void)sh;
    gfa_label(p, "Golf", &aos_montserrat_48, 0xFFFFFF, 34, 110, 300);
    lv_obj_t *cn = gfa_label(p, gf_course()->name, &aos_montserrat_20, 0xF0F8F0, 34, 172, 300);
    lv_obj_set_user_data(p, cn);
    a->lbl_boot_st = gfa_label(p, "", &aos_montserrat_16, 0xFFFFFF, 34, 318, 300);
    lv_obj_t *bar = lv_bar_create(p);
    lv_obj_set_size(bar, 220, 10);
    lv_obj_set_pos(bar, 74, 350);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    a->boot_bar = bar;
}

static void build_loading(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, true);
    a->p_loading = p;
    a->lbl_load_t = gfa_label(p, "", &aos_montserrat_20, 0xB8F0B8, 34, 150, 300);
    a->lbl_load_s = gfa_label(p, "", &aos_montserrat_28, 0xFFFFFF, 24, 184, 320);
}

static void build_pause(app_t *a, lv_obj_t *root)
{
    lv_obj_t *p = gfa_panel(root, true);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    a->p_pause = p;
    gfa_label(p, _("Pausa"), &aos_montserrat_36, 0xFFFFFF, 34, 90, 300);
    gfa_button(p, _("Seguir"), 64, 156, 240, 50, 0x30D158, &aos_montserrat_28, resume_cb, a);
    gfa_button(p, _("Menú"), 64, 216, 240, 44, 0x0A84FF, &aos_montserrat_20, to_menu_cb, a);
    gfa_button(p, _("Salir"), 64, 270, 240, 44, 0xFF453A, &aos_montserrat_20, exit_cb, a);
}

static void build_cards(app_t *a, lv_obj_t *root)
{
    /* the hole card: a box on top, the golfer reacting below it on the left
     * (the canvas), the button on the right */
    lv_obj_t *p = gfa_panel(root, false);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
    a->p_hole = p;
    lv_obj_t *box = lv_obj_create(p);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 336, 222);
    lv_obj_set_pos(box, 16, 14);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_70, 0);
    lv_obj_set_style_radius(box, 22, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    a->lbl_hole_t = gfa_label(box, "", &aos_montserrat_36, 0xFFFFFF, 8, 8, 320);
    a->lbl_hole_s = gfa_label(box, "", &aos_montserrat_16, 0xB8C2D0, 8, 52, 320);
    a->hole_table = lv_obj_create(box);
    lv_obj_remove_style_all(a->hole_table);
    lv_obj_set_size(a->hole_table, 300, 100);
    lv_obj_set_pos(a->hole_table, 18, 82);
    lv_obj_remove_flag(a->hole_table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->hole_table, LV_OBJ_FLAG_CLICKABLE);
    a->lbl_hole_c = gfa_label(box, "", &aos_montserrat_16, 0xFFD60A, 8, 190, 320);
    gfa_button(p, _("Continuar"), 206, 344, 146, 54, 0x30D158, &aos_montserrat_20, hole_next_cb, a);

    p = gfa_panel(root, true);
    lv_obj_set_style_bg_opa(p, LV_OPA_80, 0);
    a->p_round = p;
    a->lbl_round_t = gfa_label(p, "", &aos_montserrat_36, 0xFFD60A, 24, 30, 320);
    a->lbl_round_s = gfa_label(p, "", &aos_montserrat_20, 0xFFFFFF, 24, 76, 320);
    a->round_table = lv_obj_create(p);
    lv_obj_remove_style_all(a->round_table);
    lv_obj_set_size(a->round_table, 300, 110);
    lv_obj_set_pos(a->round_table, 34, 112);
    lv_obj_remove_flag(a->round_table, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(a->round_table, LV_OBJ_FLAG_CLICKABLE);
    a->lbl_round_c = gfa_label(p, "", &aos_montserrat_16, 0xFFD60A, 24, 232, 320);
    gfa_button(p, _("Otra vez"), 64, 270, 240, 50, 0x30D158, &aos_montserrat_28, again_cb, a);
    gfa_button(p, _("Menú"), 64, 330, 240, 44, 0x0A84FF, &aos_montserrat_20, to_menu_cb, a);
}

/* --------------------------------------------------------------------------
 * Touch, gestures, the frame
 * -------------------------------------------------------------------------- */

static void touch_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (a->closing || a->paused) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    lv_event_code_t code = lv_event_get_code(e);
    int ev = code == LV_EVENT_PRESSED ? 0 : (code == LV_EVENT_PRESSING ? 1 : 2);
    int x = pt.x - co.x1, y = pt.y - co.y1;
    if (ev == 0) a->aim_pre = a->aim;
    if (a->pinching) return;            /* two fingers: gfp_pinch has the map */
    if (a->state == ST_SHOP) gfs_touch(a, x, y, ev);
    else gfp_touch(a, x, y, ev);
}

static void pinch_cb(const aos_gesture_event_t *ev, void *user)
{
    app_t *a = (app_t *)user;
    if (a->closing || a->paused) return;
    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    gfp_pinch(a, ev, co.x1, co.y1);
}

static void handle_gesture(app_t *a, int dir)
{
    uint32_t now = lv_tick_get();
    if ((uint32_t)(now - a->last_gesture_ms) < 400) return;
    a->last_gesture_ms = now;
    if (dir != LV_DIR_RIGHT) return;
    if (a->state == ST_MENU) a->want_exit = true;
    else if (a->state == ST_SETUP || a->state == ST_SETTINGS || a->state == ST_SHOP) gfa_set_state(a, ST_MENU);
}

static void gesture_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (a->closing || !indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) handle_gesture(a, (int)dir);
}

static void frame(lv_timer_t *t)
{
    app_t *a = (app_t *)lv_timer_get_user_data(t);
    if (a->want_exit) {
        a->want_exit = false;
        aos_ui_back();
        return;
    }
    switch ((aos_touch_gesture_t)aos_ui_take_gesture()) {
    case AOS_TOUCH_GESTURE_RIGHT: handle_gesture(a, LV_DIR_RIGHT); break;
    default: break;
    }
    int dt = FRAME_MS;
    uint64_t now = aos_hal_uptime_ms();
    if (a->prev_ms && now > a->prev_ms) {
        uint32_t d = (uint32_t)(now - a->prev_ms);
        dt = d > 100 ? 100 : (int)d;
    }
    a->prev_ms = now;

    /* the banner's own clock */
    intptr_t bms = (intptr_t)lv_obj_get_user_data(a->banner);
    if (bms > 0) {
        bms -= dt;
        if (bms <= 0) {
            bms = 0;
            lv_obj_add_flag(a->banner, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_user_data(a->banner, (void *)bms);
    }

    if (a->mode == MODE_LINK && a->link_on) gfl_tick(a);
    gf_audio_tick();
    if (a->booting || (a->state == ST_MENU && !a->menu_ready)) {
        boot_frame(a, dt);
        if (a->booting) {
            gfp_poll(a);
            return;
        }
    }
    if (a->paused) return;
    a->st_ms += (uint32_t)dt;
    a->st_frames++;
    if (a->autoplay && a->st_ms > 2500 && a->state == ST_HOLE_END) {
        if (gf_game_next_hole(&a->game)) gfp_hole_start(a);
        else gfa_set_state(a, ST_ROUND_END);
        return;
    }
    if (a->autoplay && a->st_ms > 4000 && a->state == ST_ROUND_END) {
        gfp_round_start(a);
        return;
    }
    switch (a->state) {
    case ST_MENU:
        gfp_menu_frame(a, dt);
        break;
    case ST_HOLE_END:
        gfp_react_frame(a);
        break;
    case ST_SHOP:
        gfp_poll(a);
        gfs_frame(a, dt);
        break;
    default:
        gfp_frame(a, dt);
        break;
    }
}

/* --------------------------------------------------------------------------
 * Life cycle
 * -------------------------------------------------------------------------- */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a || a->leaving) return false;
    switch (a->state) {
    case ST_MENU:
        return false;
    case ST_SETUP:
    case ST_SETTINGS:
    case ST_SHOP:
    case ST_ROUND_END:
        gfa_set_state(a, ST_MENU);
        return true;
    case ST_LOADING:
        if (a->link_on && a->link_state == 1)      /* still in the lobby */ {
            gfl_end(a);
            gfa_set_state(a, ST_MENU);
        }
        return true;
    default:
        if (a->paused) {
            a->paused = false;
            lv_obj_add_flag(a->p_pause, LV_OBJ_FLAG_HIDDEN);
        } else if (!gfp_back(a)) {
            pause_show(a);
        }
        return true;
    }
}

static void golf_hide(aos_app_t *self, void *inst)
{
    (void)self;
    if (inst) pause_show((app_t *)inst);
}

static void free_all(app_t *a)
{
    free(a->fb);
    free(a->mapbuf);
    free(a->v3dbuf);
    free(a->zoombuf);
    a->zoombuf = NULL;
    free(a->depth);
    free(a->albedo);
    a->fb = a->mapbuf = a->v3dbuf = a->depth = a->albedo = NULL;
    gf_world_free(&a->world);
    gf_art_free();
}

/* The worker has read the pack and calibrated the clubs */
void gfa_booted(app_t *a)
{
    a->booting = false;
    a->boot_target = 92;
    a->state = -1;
    gfa_set_state(a, ST_MENU);
#ifdef AOS_SIM_BUILTIN
    /* Development switches (getenv() is NULL on the board):
     *   GF_MODE=0..3     straight into a round (quick, tour, practice, local 2p)
     *   GF_HOLE=1..8     the practice hole
     *   GF_DIFF=0..2
     *   GF_COINS=n       coins to spend in the shop
     *   GF_SCREEN=shop|settings|pause|hole|round
     */
    {
        const char *e;
        if ((e = getenv("GF_COINS")) && e[0]) a->wr.coins = atoi(e);
        if ((e = getenv("GF_AUTO")) && e[0]) a->autoplay = true;
        if ((e = getenv("GF_DIFF")) && e[0]) a->diff = atoi(e) % DIFF_N;
        if ((e = getenv("GF_COURSE")) && e[0]) {
            a->course = atoi(e) % gf_course_n();
            gf_course_select(a->course);
        }
        if ((e = getenv("GF_HOLE")) && e[0]) a->practice_hole = (atoi(e) - 1) % gf_course()->nholes;
        if ((e = getenv("GF_MODE")) && e[0]) {
            a->mode = atoi(e) % MODE_N;
            a->nplayers = a->mode == MODE_LOCAL || a->mode == MODE_LINK ? 2 : 1;
            if (a->mode == MODE_LINK) gfl_begin(a);
            else gfp_round_start(a);
        }
        if ((e = getenv("GF_SCREEN")) && e[0] == 'c') a->dev_card = true;
        if ((e = getenv("GF_SCREEN")) && e[0] == 'b') {
            /* GF_SCREEN=boot: the loading screen, held, to look at it */
            a->booting = true;
            a->boot_target = 60;
            splash_paint(a);
            gfa_show_panel(a, a->p_boot);
            return;
        }
        if ((e = getenv("GF_SCREEN")) && e[0]) {
            if (e[0] == 's' && e[1] == 'h') gfa_set_state(a, ST_SHOP);
            else if (e[0] == 's') { settings_refresh(a); gfa_set_state(a, ST_SETTINGS); }
        }
    }
#endif
}

static void *golf_create(aos_app_t *self, lv_obj_t *root)
{
    app_t *a = (app_t *)lv_malloc_zeroed(sizeof(app_t));
    if (!a) return NULL;
    a->self = self;
    uint32_t hi = 0, hp = 0;
    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("golf", "opening | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);

    size_t scr = (size_t)GF_W * GF_H * 2;
    a->fb = (uint16_t *)gf_malloc(scr);
    a->mapbuf = (uint16_t *)gf_malloc(scr);
    a->v3dbuf = (uint16_t *)gf_malloc(scr);
    a->depth = (uint16_t *)gf_malloc(scr);
    /* the grid and the ground texture are sized for the biggest hole of
     * every course (the texture is half a metre per texel) */
    int cells = 0;
    for (int c = 0; c < gf_course_n(); c++) {
        const gf_course_t *co = gf_course_get(c);
        for (int h = 0; h < co->nholes; h++) {
            int gw, gh;
            gf_world_grid_size(&co->holes[h], &gw, &gh);
            if (gw * gh > cells) cells = gw * gh;
        }
    }
    a->albedo = (uint16_t *)gf_malloc((size_t)cells * 4 * 2);
    if (!a->fb || !a->mapbuf || !a->v3dbuf || !a->depth || !a->albedo || !gf_world_init(&a->world, cells)) {
        aos_hal_log("golf", "out of memory");
        free_all(a);
        lv_free(a);
        return NULL;
    }
    prefs_load(a);
    a->root = root;
    a->shown_player = -1;
    a->practice_hole = 0;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    memset(a->fb, 0, scr);
    a->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(a->canvas, a->fb, GF_W, GF_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, GF_W, GF_H);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_RELEASED, a);
    aos_gesture_attach(a->touch, 0, pinch_cb, a);

    build_hud(a, root);
    build_menu(a, root);
    build_setup(a, root);
    build_settings(a, root);
    build_loading(a, root);
    build_boot(a, root);
    build_cards(a, root);
    gfs_build(a);
    build_pause(a, root);

    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, a);

    if (s_sfx) gf_audio_open();
    gfp_worker_start(a);
    a->timer = lv_timer_create(frame, FRAME_MS, a);
    a->state = ST_LOADING;
    a->booting = true;
    gfa_hud_show(a, false, false);
    lv_obj_add_flag(a->btn_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->lbl_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(a->lbl_wind, LV_OBJ_FLAG_HIDDEN);
    a->boot_pct = 0;
    a->boot_target = 44;
    splash_paint(a);
    gfa_show_panel(a, a->p_boot);
    gfp_boot(a);

    aos_hal_heap_info(&hi, &hp);
    aos_hal_log("golf", "ready | internal %u B, psram %u B", (unsigned)hi, (unsigned)hp);
    return a;
}

static void golf_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = (app_t *)inst;
    if (!a) return;
    if (a->timer) lv_timer_delete(a->timer);
    gfp_worker_stop();
    gf_audio_close();
    a->closing = true;
    if (a->link_on) gfl_end(a);
    if (a->root) lv_obj_clean(a->root);
    gfa_prefs_save(a);
    free_all(a);
    lv_free(a);
}

/* The launcher icon: a flag on a green with the ball beside the cup. */
static const uint8_t GOLF_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER,   0,  26, 78, 22, AIC_CIRCLE, AIC_C_LIT(0x3FAE48), 255),
    AIC_RECT(AIC_CENTER,  -6,  -6,  4, 60, 2,          AIC_C_TEXT,          255),
    AIC_RECT(AIC_CENTER,  10, -26, 30, 20, 3,          AIC_C_LIT(0xFF3B30), 255),
    AIC_RECT(AIC_CENTER,  -6,  24, 14,  6, AIC_CIRCLE, AIC_C_LIT(0x103018), 255),
    AIC_RECT(AIC_CENTER,  20,  20, 12, 12, AIC_CIRCLE, AIC_C_TEXT,          255),
    AIC_END
};

static bool golf_init(aos_app_t *app)
{
    app->desc.id       = "demo.golf";
    app->desc.name     = "Golf";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, GOLF_ICON, sizeof GOLF_ICON);
    app->desc.color_a  = 0x1E6B34;
    app->desc.color_b  = 0x0E3A5C;
    app->desc.order    = 158;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;

    app->create  = golf_create;
    app->destroy = golf_destroy;
    app->hide    = golf_hide;
    app->back    = app_back;
    return true;
}

AOS_APP_ENTRY(golf_init);
