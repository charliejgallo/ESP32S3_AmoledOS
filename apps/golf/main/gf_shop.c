/*
 * GOLF - the shop (see gf_app.h)
 *
 * The golfer on a turntable on the left (the "turn" sequence rendered in
 * Blender, eight angles), the category on top, the items on the right. A
 * tap on an item dresses the golfer in it at once, bought or not: the
 * button under the list says what it costs, or puts it on.
 */
#include "gf_app.h"
#include "gf_audio.h"

#include "aos_fonts.h"
#include "aos_i18n.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define STAGE_X     4
#define STAGE_Y     96
#define STAGE_W     184
#define STAGE_H     280

static void shop_list_build(app_t *a);
static void shop_refresh(app_t *a);

/* --------------------------------------------------------------------------
 * The stage: a studio backdrop drawn once into the background buffer
 * -------------------------------------------------------------------------- */

static void stage_bg(app_t *a)
{
    uint16_t *b = a->mapbuf;
    for (int y = 0; y < GF_H; y++) {
        for (int x = 0; x < GF_W; x++) {
            float dx = (x - 96) / 190.0f, dy = (y - 250) / 260.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float k = 1.0f - d;
            if (k < 0) k = 0;
            int r = (int)(14 + 40 * k), g = (int)(34 + 70 * k), bl = (int)(30 + 52 * k);
            b[y * GF_W + x] = gf_dither(r, g, bl, x, y);
        }
    }
    /* the podium */
    gf_img_t im;
    gf_img_init(&im, b, GF_W, GF_H);
    gf_shadow(&im, 96 * 16, 372 * 16, 86 * 16, 16 * 16, 150);
    gfo_set_bg(a, b);
}

static void draw_stage(app_t *a)
{
    if (a->art_busy) return;
    gfo_begin(a);
    int f = a->shop_turn;
    int x0, y0;
    const gf_sprite_t *s = gf_art_golfer(SEQ_TURN, f, &x0, &y0);
    if (s) {
        gf_img_t im;
        gf_img_init(&im, a->fb, GF_W, GF_H);
        gf_sprite(&im, s, STAGE_X + x0, STAGE_Y + y0);
        gfo_mark(a, STAGE_X + x0, STAGE_Y + y0, STAGE_X + x0 + s->w, STAGE_Y + y0 + s->h);
    } else {
        /* no art: fall back to a frame of the swing, if there is one */
        s = gf_art_golfer(SEQ_SWING, 0, &x0, &y0);
        if (s) {
            gf_img_t im;
            gf_img_init(&im, a->fb, GF_W, GF_H);
            int ox = 96 - (x0 + s->w / 2), oy = 372 - (y0 + s->h);
            gf_sprite(&im, s, x0 + ox, y0 + oy);
            gfo_mark(a, x0 + ox, y0 + oy, x0 + ox + s->w, y0 + oy + s->h);
        }
    }
    gfo_end(a);
}

/* --------------------------------------------------------------------------
 * Buying and wearing
 * -------------------------------------------------------------------------- */

static void dress(app_t *a)
{
    /* the worker colours the eight angles; the stage redraws when it is done */
    gfp_outfit(a, a->shop_eq, 1u << SEQ_TURN, true);
    a->shown_player = -1;           /* the game recolours when it next needs */
}

static void item_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    lv_obj_t *b = lv_event_get_current_target(e);
    int it = (int)(intptr_t)lv_obj_get_user_data(b);
    a->shop_item = it;
    a->shop_eq[a->shop_cat] = (uint8_t)it;
    dress(a);
    shop_refresh(a);
    gf_sfx(1000, 10);
}

static void action_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    int c = a->shop_cat, it = a->shop_item;
    const gf_item_t *item = gf_item(c, it);
    if (!gf_owns(&a->wr, c, it)) {
        if (a->wr.coins < item->price) {
            gf_sound(SND_NO);
            return;
        }
        a->wr.coins -= item->price;
        a->wr.own[c] |= 1u << it;
        gf_sound(SND_BUY);
    }
    a->wr.eq[c] = (uint8_t)it;
    gfa_prefs_save(a);
    shop_list_build(a);
    shop_refresh(a);
}

static void cat_step(app_t *a, int d)
{
    a->shop_cat = (a->shop_cat + d + CAT_N) % CAT_N;
    /* back to what is worn in the category we leave */
    memcpy(a->shop_eq, a->wr.eq, CAT_N);
    a->shop_item = a->wr.eq[a->shop_cat];
    dress(a);
    shop_list_build(a);
    shop_refresh(a);
}

static void cat_prev_cb(lv_event_t *e) { cat_step((app_t *)lv_event_get_user_data(e), -1); }
static void cat_next_cb(lv_event_t *e) { cat_step((app_t *)lv_event_get_user_data(e), 1); }

static void shop_back_cb(lv_event_t *e)
{
    app_t *a = (app_t *)lv_event_get_user_data(e);
    if (!a->art_busy) gf_art_release(SEQ_TURN);
    a->shown_player = -1;
    gfa_set_state(a, ST_MENU);
}

/* --------------------------------------------------------------------------
 * The list
 * -------------------------------------------------------------------------- */

static void swatch(lv_obj_t *parent, int x, int y, uint32_t c)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 16, 16);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_style_radius(d, 8, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(d, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(d, 1, 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
}

static void shop_list_build(app_t *a)
{
    lv_obj_clean(a->shop_list);
    int c = a->shop_cat;
    for (int i = 0; i < gf_item_n(c); i++) {
        const gf_item_t *it = gf_item(c, i);
        lv_obj_t *b = lv_obj_create(a->shop_list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 168, 44);
        lv_obj_set_pos(b, 0, i * 50);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x12161C), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_90, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_border_width(b, i == a->shop_item ? 3 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(i == a->shop_item ? 0xFFD60A : 0x3A3F48), 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, a);
        if (c == CAT_LOOK) {
            swatch(b, 8, 6, it->c1);
            swatch(b, 8, 22, it->c3);
        } else if (!(c == CAT_HAT && it->hat < 0)) {
            swatch(b, 8, 6, it->c1);
            swatch(b, 8, 22, c == CAT_CLUBS ? it->c3 : it->c2);
        }
        lv_obj_t *l = gfa_label(b, _(it->name), &aos_montserrat_14, 0xFFFFFF, 30, 4, 132);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, 18);
        char buf[32];
        uint32_t col = 0x9AA3B8;
        if (a->wr.eq[c] == i) {
            snprintf(buf, sizeof buf, "%s %s", LV_SYMBOL_OK, _("Puesto"));
            col = 0x30D158;
        } else if (gf_owns(&a->wr, c, i)) {
            snprintf(buf, sizeof buf, "%s", _("Tuyo"));
            col = 0x64D2FF;
        } else {
            snprintf(buf, sizeof buf, "%d", it->price);
            col = a->wr.coins >= it->price ? 0xFFD60A : 0x8A6A2A;
        }
        l = gfa_label(b, buf, &aos_montserrat_14, col, 30, 22, 132);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    }
}

static void shop_refresh(app_t *a)
{
    char buf[48];
    snprintf(buf, sizeof buf, "%d", (int)a->wr.coins);
    lv_label_set_text(a->shop_coins, buf);
    lv_label_set_text(a->shop_name, _(gf_cat(a->shop_cat)));
    int c = a->shop_cat, it = a->shop_item;
    const gf_item_t *item = gf_item(c, it);
    uint32_t col;
    if (a->wr.eq[c] == it) {
        snprintf(buf, sizeof buf, "%s", _("Puesto"));
        col = 0x3A3F48;
    } else if (gf_owns(&a->wr, c, it)) {
        snprintf(buf, sizeof buf, "%s", _("Usar"));
        col = 0x0A84FF;
    } else {
        snprintf(buf, sizeof buf, "%s  %d", _("Comprar"), item->price);
        col = a->wr.coins >= item->price ? 0x30D158 : 0x5A3A1A;
    }
    lv_label_set_text(a->shop_btn_l, buf);
    lv_obj_set_style_bg_color(a->shop_btn, lv_color_hex(col), 0);
    /* the highlight follows the pick without rebuilding */
    for (uint32_t i = 0; i < lv_obj_get_child_count(a->shop_list); i++) {
        lv_obj_t *b = lv_obj_get_child(a->shop_list, (int32_t)i);
        bool on = (int)i == it;
        lv_obj_set_style_border_width(b, on ? 3 : 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(on ? 0xFFD60A : 0x3A3F48), 0);
    }
}

/* --------------------------------------------------------------------------
 * Build, open, frame, touch
 * -------------------------------------------------------------------------- */

void gfs_build(app_t *a)
{
    lv_obj_t *p = gfa_panel(a->root, false);
    a->p_shop = p;
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);      /* the stage takes drags */
    gfa_button(p, LV_SYMBOL_LEFT, 12, 14, 48, 44, 0x8E8E93, &aos_montserrat_20, shop_back_cb, a);
    gfa_label(p, _("Tienda"), &aos_montserrat_28, 0xFFFFFF, 64, 18, 150);

    lv_obj_t *coin = lv_obj_create(p);
    lv_obj_remove_style_all(coin);
    lv_obj_set_size(coin, 116, 32);
    lv_obj_set_pos(coin, 232, 20);
    lv_obj_set_style_bg_color(coin, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(coin, LV_OPA_60, 0);
    lv_obj_set_style_radius(coin, 16, 0);
    lv_obj_remove_flag(coin, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *dot = lv_obj_create(coin);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 18, 18);
    lv_obj_set_pos(dot, 9, 7);
    lv_obj_set_style_radius(dot, 9, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFD60A), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    a->shop_coins = gfa_label(coin, "", &aos_montserrat_20, 0xFFD60A, 32, 4, 80);
    lv_obj_set_style_text_align(a->shop_coins, LV_TEXT_ALIGN_LEFT, 0);

    gfa_button(p, LV_SYMBOL_LEFT, 190, 62, 40, 38, 0x8E8E93, &aos_montserrat_16, cat_prev_cb, a);
    a->shop_name = gfa_label(p, "", &aos_montserrat_16, 0xFFFFFF, 230, 71, 88);
    lv_label_set_long_mode(a->shop_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(a->shop_name, 20);
    gfa_button(p, LV_SYMBOL_RIGHT, 318, 62, 40, 38, 0x8E8E93, &aos_montserrat_16, cat_next_cb, a);

    a->shop_list = lv_obj_create(p);
    lv_obj_remove_style_all(a->shop_list);
    lv_obj_set_size(a->shop_list, 168, 238);
    lv_obj_set_pos(a->shop_list, 190, 108);
    lv_obj_set_scroll_dir(a->shop_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(a->shop_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(a->shop_list, LV_OBJ_FLAG_SCROLLABLE);

    a->shop_btn = gfa_button(p, "", 190, 354, 168, 50, 0x30D158, &aos_montserrat_16, action_cb, a);
    a->shop_btn_l = lv_obj_get_child(a->shop_btn, 0);
}

void gfs_open(app_t *a)
{
    memcpy(a->shop_eq, a->wr.eq, CAT_N);
    a->shop_item = a->wr.eq[a->shop_cat];
    a->shop_turn = 0;
    a->shop_rot = 0;
    stage_bg(a);
    dress(a);
    shop_list_build(a);
    shop_refresh(a);
}

void gfs_frame(app_t *a, int dt)
{
    int n = gf_art_frames(SEQ_TURN);
    if (n <= 0) return;
    if (a->art_dirty && !a->art_busy) {
        a->art_dirty = false;
        draw_stage(a);
    }
    if (!a->dragging) {
        a->shop_rot += (float)dt / 900.0f;          /* one angle every 0.9 s */
    }
    int f = ((int)floorf(a->shop_rot) % n + n) % n;
    if (f != a->shop_turn) {
        a->shop_turn = f;
        draw_stage(a);
    }
}

void gfs_touch(app_t *a, int x, int y, int ev)
{
    static int last_x;
    if (x > STAGE_X + STAGE_W || y < STAGE_Y - 20) {
        a->dragging = false;
        return;
    }
    if (ev == 0) {
        a->dragging = true;
        last_x = x;
    } else if (ev == 1 && a->dragging) {
        a->shop_rot -= (float)(x - last_x) / 28.0f;
        last_x = x;
    } else {
        a->dragging = false;
    }
}
