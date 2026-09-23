/*
 * AmoledOS - Quick controls (see aos_quick.h).
 *
 * Moved out of Settings for v0.5.1, when the control centre needed the same
 * six tiles and the same slider: one copy, used from both.
 */
#include "aos_quick.h"
#include "aos_ui.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_settings_glyphs.h"

#include <stdint.h>

enum { TILE_WIFI, TILE_BT, TILE_LIGHT, TILE_AOD, TILE_SAVE, TILE_DND, TILE_COUNT };

/* --------------------------------------------------------------------------
 * The actions
 * -------------------------------------------------------------------------- */

void aos_quick_set_wifi(bool on)
{
    /* Switching it off gives back ~60 KB of executable memory, which is where
     * the code of dynamic apps comes from. The two largest do not fit with the
     * radio up, so this switch is also an app switch. */
    aos_hal_net_enable(on);
    aos_ui_toast(on ? _("Wifi encendida")
                    : _("Wifi apagada, memoria liberada"), 1600);
}

void aos_quick_set_bt(bool on)
{
    aos_hal_bt_enable(on);
    /* The same warning as WiFi, and for the same reason: the BLE stack takes
     * ~30 KB of executable memory (docs/HANDOFF-BLE-ANCS.md, section 2.4). */
    aos_ui_toast(on ? _("Bluetooth encendido")
                    : _("Bluetooth apagado, memoria liberada"), 1600);
}

/* "Do not disturb" is the notifications switch the other way round: with
 * alerts off a notification is still kept in the list, it only does not
 * light the screen or sound, and a call gets through with "calls always"
 * (aos_notif.c, politica()). That is what do-not-disturb means. */
void aos_quick_set_dnd(bool dnd)
{
    aos_hal_notif_enable(!dnd);
    aos_ui_toast(dnd ? _("No molestar: el telefono sigue conectado")
                     : _("Notificaciones encendidas"), 1800);
}

/* --------------------------------------------------------------------------
 * The tiles
 * -------------------------------------------------------------------------- */

static bool tile_on(int kind)
{
    switch (kind) {
    case TILE_WIFI: return aos_hal_net_enabled();
    case TILE_BT:   return aos_hal_bt_enabled();
    case TILE_AOD:  return aos_hal_aod_enabled();
    case TILE_SAVE: return aos_hal_power_saving_enabled();
    case TILE_DND:  return !aos_hal_notif_enabled();
    default:        return false;           /* the flashlight opens an app */
    }
}

static lv_color_t tile_color(int kind)
{
    switch (kind) {
    case TILE_SAVE: return AOS_C_GREEN;
    case TILE_DND:  return lv_color_hex(0x5E5CE6);
    case TILE_AOD:  return AOS_C_PURPLE;
    default:        return AOS_C_ACCENT;
    }
}

void aos_quick_tiles_paint(lv_obj_t *tiles)
{
    if (!tiles) {
        return;
    }
    uint32_t n = lv_obj_get_child_count(tiles);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *t = lv_obj_get_child(tiles, i);
        int k = (int)(intptr_t)lv_obj_get_user_data(t);
        bool on = tile_on(k);
        lv_color_t bg = on ? tile_color(k) : AOS_C_CARD;
        /* Only what changed: this runs every second or two on two pages, and
         * setting a style invalidates the object even with the same value. */
        if (!lv_color_eq(lv_obj_get_style_bg_color(t, 0), bg)) {
            lv_obj_set_style_bg_color(t, bg, 0);
        }
        lv_color_t fg = k == TILE_LIGHT ? AOS_C_YELLOW : on ? AOS_C_TEXT : AOS_C_DIM;
        lv_obj_t *ic = lv_obj_get_child(t, 0);
        if (!lv_color_eq(lv_obj_get_style_text_color(ic, 0), fg)) {
            lv_obj_set_style_text_color(ic, fg, 0);
        }
        lv_color_t tc = on ? AOS_C_TEXT : AOS_C_DIM;
        lv_obj_t *lb = lv_obj_get_child(t, 1);
        if (!lv_color_eq(lv_obj_get_style_text_color(lb, 0), tc)) {
            lv_obj_set_style_text_color(lb, tc, 0);
        }
    }
}

static void tile_cb(lv_event_t *event)
{
    lv_obj_t *t = lv_event_get_current_target(event);
    int kind = (int)(intptr_t)lv_obj_get_user_data(t);
    bool on = tile_on(kind);
    aos_hal_activity();
    switch (kind) {
    case TILE_WIFI:  aos_quick_set_wifi(!on); break;
    case TILE_BT:    aos_quick_set_bt(!on);   break;
    case TILE_LIGHT:
        /* Deferred: opening an app from here would destroy the page this
         * tile lives on inside its own callback. */
        aos_ui_request_open("aos.flashlight");
        return;
    case TILE_AOD:
        aos_hal_aod_enable(!on);
        aos_ui_toast(!on ? _("Siempre encendido") : _("La pantalla se apaga"), 1400);
        break;
    case TILE_SAVE:
        aos_hal_power_saving_enable(!on);
        aos_ui_toast(!on ? _("Ahorro de energia") : _("Ahorro apagado"), 1400);
        break;
    case TILE_DND:   aos_quick_set_dnd(!on);  break;
    }
    aos_quick_tiles_paint(lv_obj_get_parent(t));
}

static void tile_new(lv_obj_t *parent, int kind, const char *g, const char *text,
                     int32_t w, int32_t h)
{
    lv_obj_t *t = lv_obj_create(parent);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, w, h);
    lv_obj_set_style_radius(t, 20, 0);
    lv_obj_set_style_bg_color(t, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_pad_ver(t, 6, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(t, 3, 0);

    lv_obj_t *ic = lv_label_create(t);
    lv_label_set_text(ic, g);
    lv_obj_set_style_text_font(ic, &aos_settings_font, 0);
    lv_obj_set_style_text_color(ic, AOS_C_DIM, 0);

    lv_obj_t *l = aos_label(t, text, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(l, w - 8);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);

    aos_make_decorative(t);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(t, (void *)(intptr_t)kind);
    lv_obj_add_event_cb(t, tile_cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *aos_quick_tiles_create(lv_obj_t *parent, int32_t width, int32_t tile_h)
{
    lv_obj_t *tiles = lv_obj_create(parent);
    lv_obj_remove_style_all(tiles);
    lv_obj_set_size(tiles, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(tiles, 10, 0);
    lv_obj_set_style_pad_column(tiles, 10, 0);
    lv_obj_remove_flag(tiles, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t w = (width - 20) / 3;
    tile_new(tiles, TILE_WIFI,  AOS_SG_WIFI,                 _("Wifi"), w, tile_h);
    tile_new(tiles, TILE_BT,    AOS_SG_BLUETOOTH,            _("Bluetooth"), w, tile_h);
    tile_new(tiles, TILE_LIGHT, AOS_SG_FLASHLIGHT,           _("Linterna"), w, tile_h);
    tile_new(tiles, TILE_AOD,   AOS_SG_WATCH_VARIANT,        _("Siempre encendido"), w, tile_h);
    tile_new(tiles, TILE_SAVE,  AOS_SG_LEAF,                 _("Ahorro"), w, tile_h);
    tile_new(tiles, TILE_DND,   AOS_SG_MOON_WANING_CRESCENT, _("No molestar"), w, tile_h);
    aos_quick_tiles_paint(tiles);
    return tiles;
}

/* --------------------------------------------------------------------------
 * The pill slider
 * -------------------------------------------------------------------------- */

static void pill_value_cb(lv_event_t *event)
{
    lv_obj_t *sl = lv_event_get_target(event);
    lv_obj_t *val = (lv_obj_t *)lv_event_get_user_data(event);
    lv_label_set_text_fmt(val, "%d %%", (int)lv_slider_get_value(sl));
}

lv_obj_t *aos_quick_slider(lv_obj_t *parent, const char *g, int value,
                           int min, int max, int32_t width, int32_t height,
                           lv_event_cb_t cb)
{
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_size(sl, width, height);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(sl, height / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, height / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, AOS_C_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x48484A), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(sl, 6);

    lv_obj_t *ic = lv_label_create(sl);
    lv_label_set_text(ic, g);
    lv_obj_set_style_text_font(ic, &aos_settings_font, 0);
    lv_obj_set_style_text_color(ic, AOS_C_TEXT, 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_t *val = aos_label(sl, "", aos_font_small, AOS_C_TEXT);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_label_set_text_fmt(val, "%d %%", value);
    aos_make_decorative(ic);
    aos_make_decorative(val);

    lv_obj_add_event_cb(sl, pill_value_cb, LV_EVENT_VALUE_CHANGED, val);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sl;
}
