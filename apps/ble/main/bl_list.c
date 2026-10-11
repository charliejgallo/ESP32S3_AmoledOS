/*
 * BLE - "Cerca": who is around.
 *
 * A pool of rows bound to the sorted devices on every refresh: the rows stay
 * in place and only what they show changes, so a list that reorders twice a
 * second does not rebuild LVGL objects. Each row: what it is (a glyph in its
 * class's colour), its name or what it seems to be, a second line (company,
 * Apple message, address kind, or a sensor's readings), the last 30 seconds
 * of signal as bars and the signal now.
 */
#include "bl.h"

#include <stdio.h>
#include <string.h>

#define ROWS_MAX 40

static struct {
    lv_obj_t *list, *more, *empty;
    lv_obj_t *row[ROWS_MAX];
    lv_obj_t *icon[ROWS_MAX], *glyph[ROWS_MAX], *name[ROWS_MAX], *sub[ROWS_MAX], *rssi[ROWS_MAX], *spark[ROWS_MAX], *star[ROWS_MAX];
    int dev[ROWS_MAX];
    int nrows;
    lv_obj_t *chips;
} L;

static const char *const FILT[BL_FILT_COUNT] = { N_("Todos"), N_("Con nombre"), N_("Favoritos"), N_("Conectables"),
                                                 N_("Sensores"), N_("Balizas"), "Apple" };
static const char *const SORT[BL_SORT_COUNT] = { N_("Por señal"), N_("Por nombre"), N_("Nuevos primero") };

static void row_cb(lv_event_t *e)
{
    int r = (int)(intptr_t)lv_event_get_user_data(e);
    if (r >= 0 && r < L.nrows && L.dev[r] >= 0) bl_open_detail(L.dev[r]);
}

static void spark_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target(e);
    int r = (int)(intptr_t)lv_obj_get_user_data(o);
    if (r < 0 || r >= L.nrows || L.dev[r] < 0 || L.dev[r] >= BL.ndev) return;
    const bl_dev_t *d = &BL.dev[L.dev[r]];
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    uint32_t now_s = (uint32_t)(aos_hal_uptime_ms() / 1000);
    bl_spark(lv_event_get_layer(e), &a, d->hist, 30, (int)now_s, bl_alive(d) ? bl_rssi_color(d->rssi) : AOS_C_DIM);
}

/* A row of 64: the class glyph in a circle, name over a second line, the
 * signal now on the right with its last 30 seconds as bars under it. */
static void row_new(int i)
{
    int32_t w = BL.cw;
    lv_obj_t *c = bl_card(L.list, w, 64);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(c, AOS_C_CARD2, LV_STATE_PRESSED);
    lv_obj_add_event_cb(c, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    L.row[i] = c;
    lv_obj_t *ic = bl_box(c, 42, 42);
    lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ic, LV_OPA_30, 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 10, 0);
    L.icon[i] = ic;
    L.glyph[i] = aos_label(ic, "", &bl_sym_s, AOS_C_TEXT);
    lv_obj_center(L.glyph[i]);
    int32_t tx = 62, tw = w - tx - 66;
    L.name[i] = aos_label(c, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_size(L.name[i], tw, lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(L.name[i], LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(L.name[i], tx, 8);
    L.sub[i] = aos_label(c, "", aos_font_caption, AOS_C_DIM);
    lv_obj_set_size(L.sub[i], tw, lv_font_get_line_height(aos_font_caption));
    lv_label_set_long_mode(L.sub[i], LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(L.sub[i], tx, 36);
    L.star[i] = aos_label(c, AOS_SYM_STAR, &bl_sym_s, AOS_C_YELLOW);
    lv_obj_set_pos(L.star[i], 32, -2);
    L.rssi[i] = aos_label(c, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(L.rssi[i], 56);
    lv_obj_set_style_text_align(L.rssi[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(L.rssi[i], LV_ALIGN_TOP_RIGHT, -10, 8);
    L.spark[i] = bl_box(c, 48, 16);
    lv_obj_align(L.spark[i], LV_ALIGN_BOTTOM_RIGHT, -10, -9);
    lv_obj_set_user_data(L.spark[i], (void *)(intptr_t)i);
    lv_obj_add_event_cb(L.spark[i], spark_draw, LV_EVENT_DRAW_MAIN, NULL);
    aos_make_decorative(L.icon[i]);
    aos_make_decorative(L.name[i]);
    aos_make_decorative(L.sub[i]);
    aos_make_decorative(L.star[i]);
    aos_make_decorative(L.spark[i]);
    aos_make_decorative(L.rssi[i]);
}

static void set_text(lv_obj_t *l, const char *t)
{
    if (strcmp(lv_label_get_text(l), t)) lv_label_set_text(l, t);
}

void bl_list_refresh(void)
{
    if (!L.list) return;
    static int idx[BL_DEV_MAX];
    int n = bl_sorted(idx, BL_DEV_MAX, BL.filter, BL.sort);
    int show = n < ROWS_MAX ? n : ROWS_MAX;
    while (L.nrows < show) row_new(L.nrows++);
    for (int i = 0; i < L.nrows; i++) {
        if (i >= show) {
            lv_obj_add_flag(L.row[i], LV_OBJ_FLAG_HIDDEN);
            L.dev[i] = -1;
            continue;
        }
        lv_obj_remove_flag(L.row[i], LV_OBJ_FLAG_HIDDEN);
        const bl_dev_t *d = &BL.dev[idx[i]];
        L.dev[i] = idx[i];
        bool alive = bl_alive(d);
        lv_color_t cc = alive ? bl_class_color(d->cls) : AOS_C_DIM;
        lv_obj_set_style_bg_color(L.icon[i], cc, 0);
        lv_obj_set_style_text_color(L.glyph[i], cc, 0);
        set_text(L.glyph[i], bl_class_glyph(d->cls));
        set_text(L.name[i], bl_dev_name(d));
        lv_obj_set_style_text_color(L.name[i], alive ? AOS_C_TEXT : AOS_C_DIM, 0);
        char sub[120];
        bl_dev_sub(d, sub, sizeof sub);
        if (!alive) {
            char age[24];
            bl_fmt_age(age, sizeof age, (uint32_t)aos_hal_uptime_ms() - d->last_ms);
            char t[150];
            snprintf(t, sizeof t, _("hace %s · %s"), age, sub);
            set_text(L.sub[i], t);
        } else {
            set_text(L.sub[i], sub);
        }
        char r[16];
        if (alive) snprintf(r, sizeof r, "%d", d->rssi);
        else snprintf(r, sizeof r, "--");
        set_text(L.rssi[i], r);
        lv_obj_set_style_text_color(L.rssi[i], alive ? bl_rssi_color(d->rssi) : AOS_C_DIM, 0);
        if (d->fav) lv_obj_remove_flag(L.star[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(L.star[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(L.spark[i]);
    }
    if (n > show) {
        char t[64];
        snprintf(t, sizeof t, _("y %d más (filtrá o subí la señal mínima)"), n - show);
        lv_label_set_text(L.more, t);
        lv_obj_remove_flag(L.more, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(L.more, -1);
    } else {
        lv_obj_add_flag(L.more, LV_OBJ_FLAG_HIDDEN);
    }
    if (n == 0) {
        lv_label_set_text(L.empty, BL.paused ? _("En pausa.")
                                   : BL.ndev ? _("Nadie con este filtro.")
                                             : _("Escuchando... los equipos aparecen a medida que anuncian."));
        lv_obj_remove_flag(L.empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(L.empty, LV_OBJ_FLAG_HIDDEN);
    }
}

static void filter_cb(lv_event_t *e)
{
    BL.filter = (int)(intptr_t)lv_event_get_user_data(e);
    bl_settings_save();
    bl_rebuild();
}

static void sort_cb(lv_event_t *e)
{
    (void)e;
    BL.sort = (BL.sort + 1) % BL_SORT_COUNT;
    bl_settings_save();
    bl_rebuild();
}

void bl_list_build(lv_obj_t *page)
{
    memset(&L, 0, sizeof L);
    int32_t w = BL.cw, h = lv_obj_get_height(page);
    /* the filters, scrolling sideways */
    L.chips = bl_row(page, w, 44, 6);
    lv_obj_add_flag(L.chips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(L.chips, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(L.chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(L.chips, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *s = bl_chip(L.chips, _(SORT[BL.sort]), false, sort_cb, NULL);
    lv_obj_set_style_bg_color(s, BL_C_D, 0);
    lv_obj_t *on = NULL;
    for (int i = 0; i < BL_FILT_COUNT; i++) {
        lv_obj_t *c = bl_chip(L.chips, _(FILT[i]), i == BL.filter, filter_cb, (void *)(intptr_t)i);
        if (i == BL.filter) on = c;
    }
    /* the chosen filter in view, not scrolled off to the right */
    if (on && BL.filter > 1) {
        lv_obj_update_layout(L.chips);
        lv_obj_scroll_to_view(on, LV_ANIM_OFF);
    }
    L.list = bl_column(page, w, h - 50);
    lv_obj_set_y(L.list, 50);
    lv_obj_set_style_pad_row(L.list, 6, 0);
    L.empty = bl_caption(L.list, "", w);
    lv_obj_set_style_pad_top(L.empty, 24, 0);
    lv_obj_set_style_text_align(L.empty, LV_TEXT_ALIGN_CENTER, 0);
    L.more = bl_caption(L.list, "", w);
    lv_obj_set_style_text_align(L.more, LV_TEXT_ALIGN_CENTER, 0);
    for (int i = 0; i < ROWS_MAX; i++) L.dev[i] = -1;
    bl_list_refresh();
}

void bl_list_gone(void);
void bl_list_gone(void)
{
    memset(&L, 0, sizeof L);
}
