/*
 * BLE - a Bluetooth LE scanner and analyser for AmoledOS (apps/ble/README.md),
 * ported from P4OS's and laid out again for a 368x448 watch.
 *
 * Four pages, picked from the row of glyphs on top (the comfortable band for
 * a finger), and the settings at its end:
 *
 *   Cerca     who is around: what it is, its name or company, the signal,
 *             and what a sensor says, sorted and filtered.
 *   Radar     the same on a radar, by estimated distance; a tap picks one,
 *             and the finder takes it from there (a big signal number that
 *             beeps faster as you get closer).
 *   Sensores  the readings broadcast by thermometers and the like (BTHome,
 *             pvvx/ATC, Xiaomi, Govee, Ruuvi, SwitchBot, Qingping, Inkbird,
 *             Eddystone TLM), with their last two hours; to CSV on the card.
 *   Aire      the statistics: packets a second, devices, companies, kinds of
 *             address and of advertisement, the spread of the signal.
 *
 * A device opens its detail: every AD structure explained, the raw bytes,
 * the signal over two minutes, the interval; from there the finder and,
 * when it takes connections, the GATT explorer (bl_gatt.c). The strip at the
 * bottom of the glass, where a finger does not land, says how the scan goes.
 *
 * It scans only while it is open, and keeps the screen on meanwhile: the S3
 * has one radio for the phone, the WiFi and the link, and a watch's battery. The radio is the firmware's
 * (aos_hal_ble_*, components/aos_ble/aos_ble_scan.c); the meaning of the
 * bytes is bl_decode.c's.
 */
#include "bl.h"
#include "aos_icon_ops.h"

#include <stdio.h>
#include <string.h>

bl_t BL;

/* The Bluetooth rune with three arcs of a radar on its right, on blue. */
static const uint8_t BLE_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -14, 0, 7, 50, 3, AIC_C_TEXT, 255),
    AIC_ARC(AIC_CENTER, -2, -12, 26, 0, 6, 0, 360, 270, 90, 0, AIC_C_BG, 0, AIC_C_TEXT, 255),
    AIC_ARC(AIC_CENTER, -2, 12, 26, 0, 6, 0, 360, 270, 90, 0, AIC_C_BG, 0, AIC_C_TEXT, 255),
    AIC_ARC(AIC_CENTER, 4, 0, 50, 0, 5, 0, 360, 300, 60, 0, AIC_C_BG, 0, AIC_C_TEXT, 200),
    AIC_ARC(AIC_CENTER, 4, 0, 74, 0, 5, 0, 360, 305, 55, 0, AIC_C_BG, 0, AIC_C_TEXT, 120),
    AIC_END
};


/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

lv_obj_t *bl_box(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *bl_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *c = bl_box(parent, w, h);
    lv_obj_set_style_bg_color(c, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, AOS_UI_RADIUS, 0);
    return c;
}

lv_obj_t *bl_vcard(lv_obj_t *parent, int32_t w, int32_t pad, int32_t gap)
{
    lv_obj_t *c = bl_card(parent, w, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(c, pad, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, gap, 0);
    return c;
}

lv_obj_t *bl_column(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_style_pad_bottom(c, 40, 0);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

lv_obj_t *bl_row(lv_obj_t *parent, int32_t w, int32_t h, int32_t gap)
{
    lv_obj_t *r = bl_box(parent, w, h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

lv_obj_t *bl_wrap(lv_obj_t *parent, int32_t w, int32_t gap)
{
    lv_obj_t *r = bl_box(parent, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(r, gap, 0);
    return r;
}

lv_obj_t *bl_pill(lv_obj_t *parent, const char *glyph, const char *text, lv_color_t bg, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = bl_box(parent, LV_SIZE_CONTENT, 46);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 23, 0);
    lv_obj_set_style_pad_hor(b, 16, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    aos_make_decorative(aos_label(b, glyph ? glyph : "", &bl_sym_s, lv_color_white()));
    aos_make_decorative(aos_label(b, text ? text : "", aos_font_body, lv_color_white()));
    if (!glyph || !glyph[0]) lv_obj_add_flag(lv_obj_get_child(b, 0), LV_OBJ_FLAG_HIDDEN);
    return b;
}

void bl_pill_set(lv_obj_t *b, const char *glyph, const char *text, lv_color_t bg)
{
    if (!b) return;
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_label_set_text(lv_obj_get_child(b, 0), glyph);
    lv_label_set_text(lv_obj_get_child(b, 1), text);
}

lv_obj_t *bl_chip(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *c = bl_box(parent, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_radius(c, 20, 0);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_bg_color(c, on ? BL_C_KEYTOP : AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, LV_STATE_PRESSED);
    if (cb) {
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, ud);
    }
    lv_obj_t *l = aos_label(c, text, aos_font_small, on ? lv_color_hex(0x1C1C1E) : AOS_C_TEXT);
    lv_obj_center(l);
    aos_make_decorative(l);
    return c;
}

lv_obj_t *bl_caption(lv_obj_t *parent, const char *text, int32_t w)
{
    lv_obj_t *l = aos_label(parent, text, aos_font_caption, AOS_C_DIM);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

lv_obj_t *bl_section(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = aos_label(parent, text, aos_font_caption, AOS_C_DIM);
    lv_obj_set_style_pad_left(t, 6, 0);
    lv_obj_set_style_pad_top(t, 4, 0);
    return t;
}

lv_obj_t *bl_round_icon(lv_obj_t *parent, const char *glyph, lv_color_t col, int32_t size)
{
    lv_obj_t *c = bl_box(parent, size, size);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, col, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_30, 0);
    lv_obj_center(aos_label(c, glyph, size >= 60 ? &bl_sym_l : &bl_sym_s, col));
    aos_make_decorative(c);
    return c;
}

/* A title with a "<" that goes back, for the pages over a tab. */
lv_obj_t *bl_back_bar(lv_obj_t *parent, int32_t w, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = bl_box(parent, w, 48);
    lv_obj_t *b = bl_box(bar, 48, 48);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(b, 24, 0);
    lv_obj_set_style_bg_color(b, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_center(aos_label(b, AOS_SYM_CHEVRON_LEFT, &bl_sym_s, AOS_C_TEXT));
    lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t = aos_label(bar, title, aos_font_body, AOS_C_TEXT);
    lv_obj_set_size(t, w - 60, lv_font_get_line_height(aos_font_body));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 58, 0);
    return bar;
}

lv_obj_t *bl_kv(lv_obj_t *parent, int32_t w, const char *key, const char *val)
{
    lv_obj_t *r = bl_box(parent, w, LV_SIZE_CONTENT);
    int32_t kw = w * 36 / 100;
    lv_obj_t *k = aos_label(r, key, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(k, kw);
    lv_label_set_long_mode(k, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *v = aos_label(r, val, aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(v, w - kw - 8);
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(v, kw + 8);
    return r;
}

lv_obj_t *bl_canvas_obj(lv_obj_t *parent, int32_t w, int32_t h, lv_event_cb_t draw_cb)
{
    lv_obj_t *o = bl_box(parent, w, h);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    return o;
}

void bl_draw_text(lv_layer_t *layer, const char *t, const lv_font_t *f, lv_color_t c, int32_t x, int32_t y, int32_t w,
                  lv_text_align_t al)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = t;
    d.text_local = 1;           /* LVGL draws later, in its own threads */
    d.font = f;
    d.color = c;
    d.align = al;
    lv_area_t a = { x, y, x + w, y + lv_font_get_line_height(f) };
    lv_draw_label(layer, &d, &a);
}

void bl_draw_line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, int32_t w, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    d.color = c;
    d.width = w;
    d.opa = opa;
    d.round_start = d.round_end = 1;
    lv_draw_line(layer, &d);
}

void bl_fill(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, lv_opa_t opa, int32_t r)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = r;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

lv_color_t bl_rssi_color(int rssi)
{
    return rssi >= -60 ? AOS_C_GREEN : rssi >= -75 ? BL_C : rssi >= -88 ? AOS_C_YELLOW : AOS_C_ORANGE;
}

/* The strongest of each second as bars, oldest on the left; n seconds
 * ending at 'newest' (a second number). -100 at the bottom, -30 at the top. */
void bl_spark(lv_layer_t *layer, const lv_area_t *a, const int8_t *hist, int n, int newest, lv_color_t c)
{
    int32_t w = lv_area_get_width(a), h = lv_area_get_height(a);
    if (n <= 0 || w <= 0) return;
    float bw = (float)w / n;
    for (int k = 0; k < n; k++) {
        int v = hist[((uint32_t)(newest - (n - 1 - k)) + BL_HIST * 1000) % BL_HIST];
        if (v == BL_NO_RSSI) continue;
        float f = (v + 100) / 70.0f;
        if (f < 0.04f) f = 0.04f;
        if (f > 1) f = 1;
        int32_t x1 = a->x1 + (int32_t)(k * bw);
        int32_t x2 = a->x1 + (int32_t)((k + 1) * bw) - (bw > 3 ? 1 : 0);
        if (x2 < x1) x2 = x1;
        bl_fill(layer, x1, a->y2 - (int32_t)(f * h), x2, a->y2, c, LV_OPA_80, 0);
    }
}

/* -------------------------------------------------------------------------- */
/* The text field                                                              */
/* -------------------------------------------------------------------------- */

static void (*s_text_done)(const char *);
static lv_obj_t *s_ta;

void bl_overlay_close(void)
{
    if (BL.overlay) lv_obj_delete(BL.overlay);
    BL.overlay = NULL;
    s_ta = NULL;
}

static void text_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c != LV_EVENT_READY && c != LV_EVENT_CANCEL) return;
    char v[520];
    snprintf(v, sizeof v, "%s", lv_textarea_get_text(s_ta));
    void (*done)(const char *) = c == LV_EVENT_READY ? s_text_done : NULL;
    bl_overlay_close();
    if (done) done(v);
}


/* The field on top and the keyboard under it, all above the strip of glass a
 * finger does not reach. */
void bl_text_entry(const char *title, const char *value, bool hex_kb, void (*done)(const char *))
{
    bl_overlay_close();
    s_text_done = done;
    BL.overlay = bl_box(BL.root, BL.W, BL.H);
    lv_obj_set_style_bg_color(BL.overlay, lv_color_hex(0x121216), 0);
    lv_obj_set_style_bg_opa(BL.overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(BL.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = aos_label(BL.overlay, title, aos_font_small, AOS_C_DIM);
    lv_obj_set_size(t, BL.W - 2 * AOS_UI_PAD, lv_font_get_line_height(aos_font_small));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, AOS_UI_PAD, 4);
    s_ta = lv_textarea_create(BL.overlay);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 510);
    lv_textarea_set_text(s_ta, value ? value : "");
    lv_obj_set_size(s_ta, BL.W - 2 * AOS_UI_PAD, 46);
    lv_obj_align(s_ta, LV_ALIGN_TOP_LEFT, AOS_UI_PAD, 26);
    lv_obj_set_style_text_font(s_ta, aos_font_body, 0);
    lv_obj_set_style_bg_color(s_ta, AOS_C_CARD, 0);
    lv_obj_set_style_text_color(s_ta, AOS_C_TEXT, 0);
    lv_obj_set_style_border_width(s_ta, 0, 0);
    lv_obj_set_style_radius(s_ta, 14, 0);
    lv_obj_set_style_pad_hor(s_ta, 12, 0);
    lv_obj_set_style_pad_ver(s_ta, 10, 0);
    lv_obj_t *kb = lv_keyboard_create(BL.overlay);
    lv_obj_set_size(kb, BL.W, BL.H - 80 - 34);
    lv_obj_align(kb, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_text_font(kb, aos_font_small, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x121216), 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 4, 0);
    lv_obj_set_style_pad_gap(kb, 4, 0);
    lv_obj_set_style_bg_color(kb, AOS_C_CARD2, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, AOS_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    if (hex_kb) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_add_event_cb(kb, text_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, text_cb, LV_EVENT_CANCEL, NULL);
}

/* -------------------------------------------------------------------------- */
/* Settings                                                                    */
/* -------------------------------------------------------------------------- */

static int pref_int(const char *k, int def)
{
    int32_t v;
    return aos_hal_pref_get_i32(k, &v) ? (int)v : def;
}

static void settings_load(void)
{
    BL.tab = pref_int("ble_tab", BL_TAB_LIST);
    if (BL.tab < 0 || BL.tab >= BL_TAB_COUNT) BL.tab = BL_TAB_LIST;
    BL.sort = pref_int("ble_sort", BL_SORT_RSSI);
    if (BL.sort < 0 || BL.sort >= BL_SORT_COUNT) BL.sort = BL_SORT_RSSI;
    BL.filter = pref_int("ble_filt", BL_FILT_ALL);
    if (BL.filter < 0 || BL.filter >= BL_FILT_COUNT) BL.filter = BL_FILT_ALL;
    BL.env = pref_int("ble_env", 1);
    BL.min_rssi = pref_int("ble_minr", -100);
    BL.active = pref_int("ble_act", 1) != 0;
    /* all the time by default: whoever opens a scanner wants to hear
     * everything, and on the watch it did not slow the WiFi down (measured,
     * README) */
    BL.duty = pref_int("ble_duty", 100);
    BL.sound = pref_int("ble_snd", 1) != 0;
    BL.log_csv = pref_int("ble_csv", 0) != 0;
    BL.hide_gone = pref_int("ble_hide", 0) != 0;
}

void bl_settings_save(void)
{
    aos_hal_pref_set_i32("ble_tab", BL.tab);
    aos_hal_pref_set_i32("ble_sort", BL.sort);
    aos_hal_pref_set_i32("ble_filt", BL.filter);
    aos_hal_pref_set_i32("ble_env", BL.env);
    aos_hal_pref_set_i32("ble_minr", BL.min_rssi);
    aos_hal_pref_set_i32("ble_act", BL.active);
    aos_hal_pref_set_i32("ble_duty", BL.duty);
    aos_hal_pref_set_i32("ble_snd", BL.sound);
    aos_hal_pref_set_i32("ble_csv", BL.log_csv);
    aos_hal_pref_set_i32("ble_hide", BL.hide_gone);
}

void bl_scan_status(char *out, size_t n)
{
    int alive = 0;
    for (int i = 0; i < BL.ndev; i++) alive += bl_alive(&BL.dev[i]);
    char pps[16];
    bl_fmt_num(pps, sizeof pps, BL.air.pps, BL.air.pps < 10 ? 1 : 0);
    if (BL.bt_off) snprintf(out, n, "%s", _("Bluetooth apagado"));
    else if (BL.paused) snprintf(out, n, _("%d cerca · en pausa"), alive);
    else snprintf(out, n, _("%d cerca · %s paq/s · %d %%"), alive, pps, BL.duty);
}

static void bt_on_cb(lv_event_t *e)
{
    (void)e;
    aos_hal_bt_enable(true);
    aos_ui_toast(_("Prendiendo Bluetooth..."), 1500);
    BL.bt_off = false;
    bl_scan_apply();
    bl_rebuild();
}

lv_obj_t *bl_bt_off_card(lv_obj_t *parent, int32_t w)
{
    lv_obj_t *c = bl_vcard(parent, w, 16, 12);
    lv_obj_t *r = bl_row(c, w - 32, 52, 12);
    bl_round_icon(r, AOS_SYM_BLUETOOTH_OFF, AOS_C_DIM, 52);
    lv_obj_t *t = aos_label(r, _("Bluetooth está apagado"), aos_font_body, AOS_C_TEXT);
    lv_obj_set_flex_grow(t, 1);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    bl_caption(c, _("Para escuchar lo que anuncian los equipos de alrededor hace falta el Bluetooth. Se puede apagar de nuevo en Ajustes."), w - 32);
    bl_pill(c, AOS_SYM_BLUETOOTH, _("Prender Bluetooth"), BL_C, bt_on_cb, NULL);
    return c;
}

/* ---- the settings sheet ---- */

static void sheet_build(void);
static int32_t s_sheet_scroll;

static void sheet_close_cb(lv_event_t *e) { (void)e; s_sheet_scroll = 0; bl_overlay_close(); bl_rebuild(); }

static void opt_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    int what = v >> 8, val = (int8_t)(v & 0xFF);
    switch (what) {
    case 0: BL.active = val != 0; break;
    case 1: BL.duty = val; break;
    case 2: BL.env = val; break;
    case 3: BL.min_rssi = val; break;
    case 4: BL.hide_gone = val != 0; break;
    case 5: BL.sound = val != 0; break;
    case 6: BL.log_csv = val != 0; break;
    case 7: BL.paused = val != 0; break;
    }
    bl_settings_save();
    bl_scan_apply();
    sheet_build();
}

#define OPT(what, val) (void *)(intptr_t)(((what) << 8) | ((val) & 0xFF))

static void forget_cb(lv_event_t *e)
{
    (void)e;
    bl_forget_all();
    aos_ui_toast(_("Lista vaciada (los favoritos quedan)"), 1500);
    s_sheet_scroll = 0;
    bl_overlay_close();
    bl_rebuild();
}

static void sheet_opts(lv_obj_t *col, int32_t w, const char *title, const char *hint, int what,
                       const char *const *names, const int *vals, int n, int cur)
{
    lv_obj_t *c = bl_vcard(col, w, 14, 8);
    lv_obj_t *t = aos_label(c, title, aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(t, w - 28);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    if (hint) bl_caption(c, hint, w - 28);
    lv_obj_t *r = bl_wrap(c, w - 28, 8);
    for (int i = 0; i < n; i++) bl_chip(r, names[i], vals[i] == cur, opt_cb, OPT(what, vals[i]));
}

static void sheet_scroll_cb(lv_event_t *e)
{
    s_sheet_scroll = lv_obj_get_scroll_y(lv_event_get_target_obj(e));
}

static void sheet_build(void)
{
    if (BL.overlay) lv_obj_delete(BL.overlay);
    BL.overlay = bl_box(BL.root, BL.W, BL.H);
    lv_obj_set_style_bg_color(BL.overlay, lv_color_hex(0x0B0B0F), 0);
    lv_obj_set_style_bg_opa(BL.overlay, LV_OPA_COVER, 0);
    lv_obj_add_flag(BL.overlay, LV_OBJ_FLAG_CLICKABLE);
    int32_t w = BL.W - 2 * AOS_UI_PAD;
    lv_obj_t *col = bl_column(BL.overlay, w, BL.H);
    lv_obj_set_x(col, AOS_UI_PAD);
    lv_obj_set_style_pad_top(col, 6, 0);
    lv_obj_set_style_pad_bottom(col, 80, 0);
    bl_back_bar(col, w, _("Ajustes del escaneo"), sheet_close_cb);

    static const char *const RUN[] = { N_("Escuchando"), N_("En pausa") };
    const char *run[2] = { _(RUN[0]), _(RUN[1]) };
    static const int RUN_V[] = { 0, 1 };
    sheet_opts(col, w, _("Escaneo"), NULL, 7, run, RUN_V, 2, BL.paused);
    static const char *const MODE[] = { N_("Activo"), N_("Pasivo") };
    const char *mode[2] = { _(MODE[0]), _(MODE[1]) };
    static const int MODE_V[] = { 1, 0 };
    sheet_opts(col, w, _("Modo"), _("Activo le pide a cada equipo su respuesta de escaneo, donde muchos dicen su nombre. Pasivo sólo escucha: no se anuncia ante nadie."),
               0, mode, MODE_V, 2, BL.active);
    static const char *const DUTY[] = { "10 %", "30 %", "60 %", "100 %" };
    static const int DUTY_V[] = { 10, 30, 60, 100 };
    sheet_opts(col, w, _("Tiempo escuchando"), _("La radio es una sola para el Wi-Fi, el teléfono y el Enlace: escuchar todo el tiempo oye más paquetes. Si algo de eso se corta, bajalo."),
               1, DUTY, DUTY_V, 4, BL.duty);
    static const char *const ENV[] = { N_("Al aire libre"), N_("Casa"), N_("Oficina") };
    const char *env[3] = { _(ENV[0]), _(ENV[1]), _(ENV[2]) };
    static const int ENV_V[] = { 0, 1, 2 };
    sheet_opts(col, w, _("Entorno, para la distancia"), _("La distancia sale de cuánto se debilita la señal, y eso depende de las paredes y la gente: es una estimación."),
               2, env, ENV_V, 3, BL.env);
    static const char *const MIN[] = { N_("Todos"), "-90", "-80", "-70" };
    const char *mn[4] = { _(MIN[0]), MIN[1], MIN[2], MIN[3] };
    static const int MIN_V[] = { -100, -90, -80, -70 };
    sheet_opts(col, w, _("Señal mínima (dBm)"), _("Esconde los que están lejos (los favoritos se ven siempre)."), 3, mn, MIN_V, 4, BL.min_rssi);
    static const char *const YN[] = { N_("Sí"), N_("No") };
    const char *yn[2] = { _(YN[0]), _(YN[1]) };
    static const int YN_V[] = { 1, 0 };
    sheet_opts(col, w, _("Esconder los que se fueron"), _("Los que no se oyen hace 30 segundos."), 4, yn, YN_V, 2, BL.hide_gone);
    sheet_opts(col, w, _("Sonido del buscador"), NULL, 5, yn, YN_V, 2, BL.sound);
    sheet_opts(col, w, _("Guardar los sensores en CSV"), _("Una línea por minuto y por sensor en ble/sensores-<día>.csv de la tarjeta, mientras la app está abierta."),
               6, yn, YN_V, 2, BL.log_csv);
    lv_obj_t *c = bl_vcard(col, w, 14, 8);
    aos_label(c, _("Lista"), aos_font_body, AOS_C_TEXT);
    bl_caption(c, _("Olvida lo escuchado hasta ahora. Los favoritos y sus nombres quedan."), w - 28);
    bl_pill(c, AOS_SYM_DELETE, _("Vaciar la lista"), AOS_C_CARD2, forget_cb, NULL);
    /* a choice rebuilds the sheet: it stays where the finger was */
    lv_obj_update_layout(col);
    lv_obj_scroll_to_y(col, s_sheet_scroll, LV_ANIM_OFF);
    lv_obj_add_event_cb(col, sheet_scroll_cb, LV_EVENT_SCROLL_END, NULL);
}

/* -------------------------------------------------------------------------- */
/* Pages                                                                       */
/* -------------------------------------------------------------------------- */

/* The row of glyphs on top, and where it ends. */
#define TABS_H      52
#define STATUS_H    30          /* the strip at the bottom, read only */
#define TAB_SETTINGS BL_TAB_COUNT

static const char *const TAB_GLYPH[BL_TAB_COUNT + 1] = {
    AOS_SYM_FORMAT_LIST_BULLETED, AOS_SYM_RADAR, AOS_SYM_THERMOMETER, AOS_SYM_CHART_AREASPLINE, AOS_SYM_COG,
};

static lv_obj_t *s_status;

/* ---- the links the scan may disturb ---- */

static uint8_t links_up(void)
{
    uint8_t m = 0;
    if (aos_hal_bt_state() == AOS_BT_CONNECTED) m |= BL_LOST_PHONE;
    if (aos_hal_net_state() == AOS_NET_CONNECTED) m |= BL_LOST_WIFI;
    return m;
}

/* Once a second. Only a link that was up and went down while the scan was
 * running counts; what happens with the scan stopped is none of its doing. */
static bool links_watch(void)
{
    uint8_t up = links_up();
    bool scanning = aos_hal_ble_scanning() && !BL.paused;
    uint8_t gone = scanning ? (uint8_t)(BL.was_up & ~up) : 0;
    BL.was_up = up;
    if (!gone) return false;
    bool fresh = (gone & ~BL.lost) != 0;
    BL.lost |= gone;
    BL.lost_ms = (uint32_t)aos_hal_uptime_ms();
    aos_hal_log("ble", "while scanning, lost %s%s", gone & BL_LOST_PHONE ? "phone " : "",
                gone & BL_LOST_WIFI ? "wifi" : "");
    return fresh;
}

void bl_lost_text(char *out, size_t n)
{
    const char *w[2];
    int k = 0;
    if (BL.lost & BL_LOST_PHONE) w[k++] = _("el teléfono");
    if (BL.lost & BL_LOST_WIFI) w[k++] = _("el Wi-Fi");
    out[0] = 0;
    size_t l = 0;
    for (int i = 0; i < k && l < n; i++)
        l += snprintf(out + l, n - l, "%s%s", i == 0 ? "" : _(" y "), w[i]);
}

static void lost_ok_cb(lv_event_t *e)
{
    (void)e;
    BL.lost = 0;
    bl_rebuild();
}

/* The notice on top of the page. */
static lv_obj_t *lost_card(lv_obj_t *page)
{
    lv_obj_t *c = bl_vcard(page, BL.cw, 12, 8);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x3A2A10), 0);
    lv_obj_t *r = bl_row(c, BL.cw - 24, LV_SIZE_CONTENT, 8);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    aos_label(r, AOS_SYM_ALERT_OUTLINE, &bl_sym_s, AOS_C_ORANGE);
    char what[64], t[320], age[24], when[40];
    bl_lost_text(what, sizeof what);
    uint32_t ago = (uint32_t)aos_hal_uptime_ms() - BL.lost_ms;
    bl_fmt_age(age, sizeof age, ago);
    if (ago < 60000) snprintf(when, sizeof when, "%s", _("recién"));
    else snprintf(when, sizeof when, _("hace %s"), age);
    snprintf(t, sizeof t, _("Mientras escaneaba se desconectó %s (%s). La radio es una sola: escuchar menos tiempo (en los ajustes) les deja más aire."),
             what, when);
    lv_obj_t *l = aos_label(r, t, aos_font_small, AOS_C_TEXT);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    bl_chip(c, _("Entendido"), false, lost_ok_cb, NULL);
    return c;
}

void bl_list_gone(void);
void bl_radar_gone(void);
void bl_sens_gone(void);
void bl_air_gone(void);
void bl_detail_gone(void);
void bl_gatt_gone(void);

void bl_page_gone(void)
{
    bl_list_gone();
    bl_radar_gone();
    bl_sens_gone();
    bl_air_gone();
    bl_detail_gone();
    bl_gatt_gone();
}

static void status_set(void)
{
    if (!s_status) return;
    char s[96];
    bl_scan_status(s, sizeof s);
    if (strcmp(lv_label_get_text(s_status), s)) lv_label_set_text(s_status, s);
}

static void tabs_paint(void)
{
    for (int i = 0; i <= BL_TAB_COUNT; i++) {
        if (!BL.tabs[i]) continue;
        bool on = i < BL_TAB_COUNT && i == BL.tab && BL.page == BL_PAGE_TAB;
        lv_obj_set_style_bg_opa(BL.tabs[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(BL.tabs[i], 0), on ? lv_color_white() : AOS_C_DIM, 0);
    }
}

void bl_rebuild(void)
{
    bl_page_gone();
    lv_obj_clean(BL.content);
    tabs_paint();
    status_set();
    lv_obj_t *page = bl_box(BL.content, BL.cw, BL.ch);
    /* the builders size things by their parent's height: it has to be laid out */
    lv_obj_update_layout(page);
    if (BL.page == BL_PAGE_DETAIL) { bl_detail_build(page); return; }
    if (BL.page == BL_PAGE_FINDER) { bl_finder_build(page); return; }
    if (BL.page == BL_PAGE_GATT) { bl_gatt_build(page); return; }
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 8, 0);
    int32_t used = 0;
    if (BL.lost) {
        lv_obj_t *lc = lost_card(page);
        lv_obj_update_layout(page);
        used += lv_obj_get_height(lc) + 8;
    }
    lv_obj_t *body = bl_box(page, BL.cw, BL.ch - used);
    lv_obj_update_layout(page);
    if (BL.bt_off && BL.tab != BL_TAB_AIR) {
        bl_bt_off_card(body, BL.cw);
        return;
    }
    if (BL.tab == BL_TAB_LIST) bl_list_build(body);
    else if (BL.tab == BL_TAB_RADAR) bl_radar_build(body);
    else if (BL.tab == BL_TAB_SENS) bl_sens_build(body);
    else bl_air_build(body);
}

static bool sel_resolve(void)
{
    BL.sel = bl_find(BL.sel_addr);
    return BL.sel >= 0;
}

static void open_page(int idx, int page)
{
    if (idx < 0 || idx >= BL.ndev) return;
    BL.sel = idx;
    memcpy(BL.sel_addr, BL.dev[idx].addr, 6);
    if (BL.page == BL_PAGE_GATT && page != BL_PAGE_GATT) bl_gatt_close();
    BL.page = page;
    bl_rebuild();
}

void bl_open_detail(int idx) { open_page(idx, BL_PAGE_DETAIL); }
void bl_open_finder(int idx) { open_page(idx, BL_PAGE_FINDER); }
void bl_open_gatt(int idx) { open_page(idx, BL_PAGE_GATT); }

void bl_go_tab(void)
{
    if (BL.page == BL_PAGE_GATT) bl_gatt_close();
    BL.page = BL_PAGE_TAB;
    bl_rebuild();
}

static void tab_cb(lv_event_t *e)
{
    int t = (int)(intptr_t)lv_event_get_user_data(e);
    if (t == TAB_SETTINGS) {
        sheet_build();
        return;
    }
    bl_overlay_close();
    if (BL.page == BL_PAGE_GATT) bl_gatt_close();
    BL.tab = t;
    BL.page = BL_PAGE_TAB;
    bl_settings_save();
    bl_rebuild();
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    BL.ticks++;
    bl_scan_drain();
    if (BL.ticks % 10 == 0) {
        bl_log_tick();
        bl_names_poll();
        if (links_watch() && !BL.hidden && !BL.overlay && BL.page == BL_PAGE_TAB) bl_rebuild();
        /* Bluetooth came on (here or in Settings) or the scan stopped by
         * itself: start it again */
        bool want = !BL.paused && !BL.hidden;
        bool was_off = BL.bt_off;
        if (want && !aos_hal_ble_scanning()) bl_scan_apply();
        if (want && was_off != BL.bt_off && !BL.overlay && BL.page == BL_PAGE_TAB && !BL.hidden) bl_rebuild();
    }
    if (BL.hidden || BL.overlay) return;
    if (BL.page == BL_PAGE_FINDER) bl_finder_beep();
    /* the radar sweeps at 10 fps, the finder at 5, the rest twice a second */
    if (BL.page == BL_PAGE_TAB && BL.tab == BL_TAB_RADAR && !BL.bt_off) bl_radar_refresh();
    if (BL.page == BL_PAGE_FINDER && BL.ticks % 2 == 0) {
        if (sel_resolve()) bl_finder_refresh();
    }
    if (BL.page == BL_PAGE_GATT) bl_gatt_refresh();
    if (BL.ticks % 5) return;
    status_set();
    if (BL.page == BL_PAGE_DETAIL) {
        if (sel_resolve()) bl_detail_refresh();
        else bl_go_tab();
        return;
    }
    if (BL.page != BL_PAGE_TAB || BL.bt_off) return;
    if (BL.tab == BL_TAB_LIST) bl_list_refresh();
    else if (BL.tab == BL_TAB_SENS) bl_sens_refresh();
    else if (BL.tab == BL_TAB_AIR) bl_air_refresh();
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */
/* -------------------------------------------------------------------------- */

void bl_names_first(void);

static void layout(lv_obj_t *root)
{
    BL.root = root;
    BL.overlay = NULL;
    /* root's size is 0 until LVGL lays it out */
    lv_obj_update_layout(root);
    BL.W = lv_obj_get_width(root);
    BL.H = lv_obj_get_height(root);
    BL.hidden = false;
    BL.cw = BL.W - 2 * AOS_UI_PAD;

    /* the row of glyphs: four pages and the settings */
    BL.tabbar = bl_box(root, BL.W, TABS_H);
    lv_obj_set_flex_flow(BL.tabbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(BL.tabbar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(BL.tabbar, 10, 0);
    for (int i = 0; i <= BL_TAB_COUNT; i++) {
        lv_obj_t *t = bl_box(BL.tabbar, 60, 44);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(t, 22, 0);
        lv_obj_set_style_bg_color(t, i == TAB_SETTINGS ? AOS_C_CARD2 : BL_C_D, 0);
        lv_obj_set_style_bg_opa(t, i == TAB_SETTINGS ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(t, AOS_C_CARD2, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_center(aos_label(t, TAB_GLYPH[i], &bl_sym_s, AOS_C_DIM));
        lv_obj_add_event_cb(t, tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        BL.tabs[i] = t;
    }

    BL.content = bl_box(root, BL.W, BL.H - TABS_H - STATUS_H);
    lv_obj_set_y(BL.content, TABS_H);
    lv_obj_set_style_pad_hor(BL.content, AOS_UI_PAD, 0);
    lv_obj_set_style_pad_top(BL.content, 2, 0);
    lv_obj_update_layout(BL.content);
    BL.ch = lv_obj_get_content_height(BL.content);

    /* the strip of glass a finger does not land on: what the scan does */
    s_status = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_size(s_status, BL.W - 2 * 40, lv_font_get_line_height(aos_font_small));
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -8);
    aos_make_decorative(s_status);

    if (BL.page != BL_PAGE_TAB && !sel_resolve()) BL.page = BL_PAGE_TAB;
    bl_rebuild();
}

static void *ble_create(aos_app_t *self, lv_obj_t *root)
{
    memset(&BL, 0, sizeof BL);
    BL.sel = -1;
    BL.self = self;
    if (!bl_scan_init()) {
        lv_obj_center(aos_label(root, _("No hay memoria para abrir BLE"), aos_font_body, AOS_C_DIM));
        return NULL;
    }
    settings_load();
    bl_names_first();
    BL.was_up = links_up();
    BL.started_ms = (uint32_t)aos_hal_uptime_ms();
    layout(root);
    bl_scan_apply();
    BL.timer = lv_timer_create(timer_cb, 100, NULL);
    return &BL;
}

static void ble_destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (BL.timer) lv_timer_delete(BL.timer);
    BL.timer = NULL;
    bl_page_gone();
    BL.overlay = NULL;
    BL.content = NULL;
    s_status = NULL;
    memset(BL.tabs, 0, sizeof BL.tabs);
    bl_gatt_close();
    aos_hal_ble_scan_stop();
    bl_scan_free();
    BL.page = BL_PAGE_TAB;
    BL.self = NULL;
}

static void ble_hide(aos_app_t *self, void *inst)
{
    (void)self;
    (void)inst;
    BL.hidden = true;
    bl_overlay_close();
    if (BL.page == BL_PAGE_GATT) {
        bl_gatt_close();
        BL.page = BL_PAGE_DETAIL;
    }
    bl_scan_apply();
}

static void ble_show(aos_app_t *self, void *inst)
{
    (void)self;
    (void)inst;
    BL.hidden = false;
    bl_scan_apply();
    if (BL.page != BL_PAGE_TAB && !sel_resolve()) BL.page = BL_PAGE_TAB;
    bl_rebuild();
}

static bool ble_back(aos_app_t *self, void *inst)
{
    (void)self;
    (void)inst;
    if (!BL.content) return false;
    if (BL.overlay) {
        s_sheet_scroll = 0;
        bl_overlay_close();
        bl_rebuild();
        return true;
    }
    if (BL.page == BL_PAGE_GATT || BL.page == BL_PAGE_FINDER) {
        if (BL.page == BL_PAGE_GATT) bl_gatt_close();
        BL.page = sel_resolve() ? BL_PAGE_DETAIL : BL_PAGE_TAB;
            bl_rebuild();
        return true;
    }
    if (BL.page == BL_PAGE_DETAIL) {
        bl_go_tab();
        return true;
    }
    return false;
}

static bool ble_init(aos_app_t *app)
{
    app->desc.id = "aos.ble";
    app->desc.name = "BLE";
    app->desc.icon = LV_SYMBOL_BLUETOOTH;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a = 0x3B82F6;
    app->desc.color_b = 0x1E3A8A;
    app->desc.order = 172;
    /* The screen stays on while it is open: the watch closes whatever is
     * open when the screen dims, and a scanner is something you look at
     * without touching (the list, the finder, the sensors). It scans only
     * while open, so leaving it -the button- stops the radio too. */
    app->desc.flags = AOS_APP_FLAG_KEEP_AWAKE;
    aos_icon_set_ops(app, BLE_ICON, sizeof BLE_ICON);
    app->create = ble_create;
    app->destroy = ble_destroy;
    app->hide = ble_hide;
    app->show = ble_show;
    app->back = ble_back;
    return true;
}

AOS_APP_ENTRY(ble_init);
