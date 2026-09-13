/*
 * AmoledOS - Control PC: the watch as a keyboard and media controller of the
 * computer it is plugged into (USB, docs/USB.md, D3).
 *
 * Nothing here knows about USB descriptors: the HAL exposes named keys and a
 * mode switch. The screen has two faces. With the keyboard ready it is a pad
 * of buttons; otherwise it says why and offers to switch the port to KEYS,
 * which is the one thing the user can do from here. The mode is not switched
 * back on exit: whoever plugged the watch into a computer to use it as a
 * remote wants it to stay one until Settings says otherwise.
 *
 * Every button is a named key, so adding one is a row in the table. The Mac
 * mutes on "mute" but does not unmute on a second "mute" (measured); a
 * volume key unmutes, which is what people press anyway.
 *
 * The mouse (D4) is a third face: the gyroscope's rates become pointer
 * motion, 50 times a second, an air mouse. Rates and not tilt angles: an
 * angle maps to a position and a badly zeroed rest angle drifts the pointer
 * into a corner (the same lesson 2043 learnt the other way round, where
 * absolute made sense because a ship has a home). Which gyro axis is "left"
 * depends on how the wrist is held, so the three switches 2043 has are
 * here too (invert X, invert Y, swap the axes), in preferences. Tap = left
 * click, hold = right click, the side button = left click.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t   *status;
    lv_obj_t   *pad;        /* the buttons, shown when the keyboard is ready */
    lv_obj_t   *off;        /* the explanation and the switch, otherwise */
    lv_obj_t   *off_text;
    lv_obj_t   *off_button;
    lv_obj_t   *mouse;      /* the air-mouse face */
    lv_obj_t   *mouse_gain; /* its speed button, relabelled on each press */
    lv_timer_t *timer;
    lv_timer_t *mouse_timer;
    bool        ready_shown;
    bool        mouse_on;
    bool        inv_x, inv_y, swap_xy;
    int         gain;       /* 1..4 */
} pcremote_t;

#define MOUSE_PERIOD_MS 20
#define MOUSE_DEADBAND  3.0f    /* dps: the hand at rest is never at zero */

static pcremote_t s_pc;

static void key_cb(lv_event_t *event)
{
    const char *name = lv_event_get_user_data(event);
    if (!aos_hal_usb_key(name)) {
        aos_ui_toast(_("Sin teclado USB"), 1400);
        return;
    }
    aos_hal_beep(1400, 15);
}

static void enable_cb(lv_event_t *event)
{
    (void)event;
    if (!aos_hal_usb_mode_set(AOS_HAL_USB_KEYS)) {
        aos_ui_toast(_("El USB esta cambiando de modo"), 1400);
    }
}

static lv_obj_t *key_button(lv_obj_t *parent, const char *text, lv_color_t color,
                            const char *key, int x, int y, int w, int h)
{
    lv_obj_t *b = aos_button(parent, text, color, key_cb, (void *)key);
    lv_obj_set_size(b, w, h);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, x, y);
    return b;
}

/* --- the mouse face ------------------------------------------------------- */

static void mouse_tick(lv_timer_t *timer)
{
    (void)timer;
    aos_imu_t imu;
    if (!s_pc.mouse_on || !aos_hal_imu_read(&imu)) {
        return;
    }
    /* Yaw (about the axis through the glass) moves left-right, pitch
     * up-down, when the watch is held like a remote. The pitch sign was
     * measured on the wrist (2026-09-13, "had to invert Y"): the minus is
     * that measurement, so the switches start off. They fix whatever
     * another wrist does to it. */
    float rx = imu.gz, ry = -imu.gx;
    if (s_pc.swap_xy) { float t = rx; rx = ry; ry = t; }
    if (s_pc.inv_x) rx = -rx;
    if (s_pc.inv_y) ry = -ry;
    if (rx > -MOUSE_DEADBAND && rx < MOUSE_DEADBAND) rx = 0;
    if (ry > -MOUSE_DEADBAND && ry < MOUSE_DEADBAND) ry = 0;
    if (rx == 0 && ry == 0) {
        return;
    }
    /* 0.06 px per dps per report at speed 1: a lazy 100 dps turn crosses
     * 300 px a second; 2 and 3 are the ones that felt right on the wrist,
     * 4 is for a big screen far away. */
    float k = 0.06f * s_pc.gain;
    aos_hal_usb_mouse((int)(rx * k), (int)(ry * k), 0);
}

static void mouse_show(bool on)
{
    s_pc.mouse_on = on;
    if (on) {
        lv_obj_remove_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        aos_hal_imu_gyro_request(true);
        if (!s_pc.mouse_timer) {
            s_pc.mouse_timer = lv_timer_create(mouse_tick, MOUSE_PERIOD_MS, NULL);
        }
    } else {
        if (s_pc.mouse_timer) {
            lv_timer_delete(s_pc.mouse_timer);
            s_pc.mouse_timer = NULL;
        }
        aos_hal_imu_gyro_request(false);
        lv_obj_add_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);
        if (s_pc.ready_shown) {
            lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void mouse_open_cb(lv_event_t *event)  { (void)event; mouse_show(true); }
static void mouse_close_cb(lv_event_t *event) { (void)event; mouse_show(false); }

static void mouse_touch_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED) {
        if (aos_hal_usb_click(1)) aos_hal_beep(1400, 10);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (aos_hal_usb_click(2)) aos_hal_beep(1000, 20);
    }
}

static void mouse_gain_label(void)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d", _("Vel."), s_pc.gain);
    lv_label_set_text(lv_obj_get_child(s_pc.mouse_gain, 0), buf);
}

static void mouse_gain_cb(lv_event_t *event)
{
    (void)event;
    s_pc.gain = s_pc.gain % 4 + 1;
    aos_hal_pref_set_i32("pc_mouse_gain", s_pc.gain);
    mouse_gain_label();
}

static void mouse_switch_cb(lv_event_t *event)
{
    const char *key = lv_event_get_user_data(event);
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    if (!strcmp(key, "pc_mouse_invx")) s_pc.inv_x = on;
    else if (!strcmp(key, "pc_mouse_invy")) s_pc.inv_y = on;
    else s_pc.swap_xy = on;
    aos_hal_pref_set_i32(key, on ? 1 : 0);
}

static lv_obj_t *mouse_switch(lv_obj_t *parent, const char *text, const char *key, bool on, int x, int y)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 100, 36);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *lbl = aos_label(row, text, aos_font_small, AOS_C_TEXT);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, mouse_switch_cb, LV_EVENT_VALUE_CHANGED, (void *)key);
    return row;
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    aos_hal_usb_mode_t mode = aos_hal_usb_mode();
    bool ready = aos_hal_usb_keys_ready();
    char buf[96];

    if (aos_hal_usb_busy()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_REFRESH "  %s", _("cambiando..."));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ORANGE, 0);
    } else if (ready) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB "  %s", _("teclado y red USB listos"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ACCENT, 0);
    } else if (mode == AOS_HAL_USB_KEYS) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB "  %s", _("esperando a la computadora"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_ORANGE, 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CLOSE "  %s", _("teclado USB apagado"));
        lv_obj_set_style_text_color(s_pc.status, AOS_C_DIM, 0);
    }
    lv_label_set_text(s_pc.status, buf);

    if (ready != s_pc.ready_shown) {
        s_pc.ready_shown = ready;
        if (ready) {
            if (!s_pc.mouse_on) lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_pc.off, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_pc.mouse_on) mouse_show(false);
            lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_pc.off, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!ready) {
        /* Two different reasons, two different things to do. */
        if (mode == AOS_HAL_USB_KEYS) {
            lv_label_set_text(s_pc.off_text, _("Conecta el reloj a la computadora con el cable USB"));
            lv_obj_add_flag(s_pc.off_button, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_pc.off_text,
                              mode == AOS_HAL_USB_DISK
                                  ? _("El puerto esta en modo disco. Pasarlo a teclado saca la tarjeta de la computadora.")
                                  : _("El puerto USB es la consola. Como teclado, el reloj es ademas una red: el portal en http://192.168.7.1"));
            lv_obj_remove_flag(s_pc.off_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);

    s_pc.status = aos_label_boxed(page, "", aos_font_small, AOS_C_DIM, AOS_SCREEN_W, 22);
    lv_obj_align(s_pc.status, LV_ALIGN_TOP_MID, 0, 14);

    /* --- the pad: six rows of 46 px, the last one ending at y 360, under
     * the touch ceiling (AOS_TOUCH_Y_MAX, 390). --- */
    s_pc.pad = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.pad);
    lv_obj_set_size(s_pc.pad, lv_pct(100), 320);
    lv_obj_align(s_pc.pad, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_SCROLLABLE);

    /* music */
    key_button(s_pc.pad, LV_SYMBOL_PREV,  AOS_C_CARD2,  "prev",  30, 0, 74, 46);
    key_button(s_pc.pad, LV_SYMBOL_PLAY,  AOS_C_ACCENT, "play", 138, 0, 92, 46);
    key_button(s_pc.pad, LV_SYMBOL_NEXT,  AOS_C_CARD2,  "next", 264, 0, 74, 46);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MID, AOS_C_CARD2, "voldown", 30, 54, 110, 46);
    key_button(s_pc.pad, LV_SYMBOL_MUTE,       AOS_C_CARD2, "mute",   146, 59, 76, 36);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MAX, AOS_C_CARD2, "volup", 228, 54, 110, 46);

    /* slides: page up / page down move Keynote and PowerPoint; "b" blanks */
    key_button(s_pc.pad, LV_SYMBOL_LEFT,  AOS_C_CARD2, "pgup",  30, 108, 150, 46);
    key_button(s_pc.pad, LV_SYMBOL_RIGHT, AOS_C_CARD2, "pgdn", 188, 108, 150, 46);
    key_button(s_pc.pad, _("Pantalla negra"), AOS_C_CARD2, "b",       30, 162, 150, 46);
    key_button(s_pc.pad, "cmd + tab",         AOS_C_CARD2, "cmd+tab", 188, 162, 150, 46);

    /* the three keys every dialog wants */
    key_button(s_pc.pad, "esc",     AOS_C_CARD2, "esc",    30, 216, 96, 46);
    key_button(s_pc.pad, _("espacio"), AOS_C_CARD2, "space", 136, 216, 96, 46);
    key_button(s_pc.pad, LV_SYMBOL_OK, AOS_C_ACCENT, "enter", 242, 216, 96, 46);

    /* the mouse face */
    lv_obj_t *mouse_btn = aos_button(s_pc.pad, _("Mouse por inclinacion"), AOS_C_CARD2, mouse_open_cb, NULL);
    lv_obj_set_size(mouse_btn, 308, 46);
    lv_obj_align(mouse_btn, LV_ALIGN_TOP_LEFT, 30, 270);

    /* --- the mouse: a touch surface with the switches along the bottom --- */
    int32_t v;
    s_pc.inv_x   = aos_hal_pref_get_i32("pc_mouse_invx", &v) && v;
    s_pc.inv_y   = aos_hal_pref_get_i32("pc_mouse_invy", &v) && v;
    s_pc.swap_xy = aos_hal_pref_get_i32("pc_mouse_xy",   &v) && v;
    s_pc.gain    = aos_hal_pref_get_i32("pc_mouse_gain", &v) && v >= 1 && v <= 4 ? (int)v : 2;

    s_pc.mouse = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.mouse);
    lv_obj_set_size(s_pc.mouse, lv_pct(100), 320);
    lv_obj_align(s_pc.mouse, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.mouse, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *touch = lv_obj_create(s_pc.mouse);
    lv_obj_remove_style_all(touch);
    lv_obj_set_style_bg_color(touch, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(touch, 18, 0);
    lv_obj_set_size(touch, AOS_SCREEN_W - 40, 170);
    lv_obj_align(touch, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch, mouse_touch_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(touch, mouse_touch_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *hint = aos_label(touch, _("Inclina el reloj para mover el puntero.\nToca: clic. Mantene: clic derecho."),
                               aos_font_small, AOS_C_DIM);
    lv_obj_set_width(hint, AOS_SCREEN_W - 70);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(hint);
    aos_make_decorative(hint);      /* the taps belong to the surface, not the text */

    mouse_switch(s_pc.mouse, _("Inv X"), "pc_mouse_invx", s_pc.inv_x, 30, 184);
    mouse_switch(s_pc.mouse, _("Inv Y"), "pc_mouse_invy", s_pc.inv_y, 134, 184);
    mouse_switch(s_pc.mouse, _("Ejes"),  "pc_mouse_xy",   s_pc.swap_xy, 238, 184);

    s_pc.mouse_gain = aos_button(s_pc.mouse, "", AOS_C_CARD2, mouse_gain_cb, NULL);
    lv_obj_set_size(s_pc.mouse_gain, 120, 46);
    lv_obj_align(s_pc.mouse_gain, LV_ALIGN_TOP_LEFT, 30, 240);
    mouse_gain_label();
    lv_obj_t *back = aos_button(s_pc.mouse, _("Teclas"), AOS_C_ACCENT, mouse_close_cb, NULL);
    lv_obj_set_size(back, 170, 46);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 168, 240);
    lv_obj_add_flag(s_pc.mouse, LV_OBJ_FLAG_HIDDEN);

    /* --- the other face --- */
    s_pc.off = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.off);
    lv_obj_set_size(s_pc.off, lv_pct(100), 320);
    lv_obj_align(s_pc.off, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.off, LV_OBJ_FLAG_SCROLLABLE);

    s_pc.off_text = aos_label(s_pc.off, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(s_pc.off_text, AOS_SCREEN_W - 60);
    lv_label_set_long_mode(s_pc.off_text, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_pc.off_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_pc.off_text, LV_ALIGN_TOP_MID, 0, 60);

    s_pc.off_button = aos_button(s_pc.off, _("Activar teclado USB"), AOS_C_ACCENT, enable_cb, NULL);
    lv_obj_set_size(s_pc.off_button, AOS_SCREEN_W - 90, 56);
    lv_obj_align(s_pc.off_button, LV_ALIGN_TOP_MID, 0, 200);

    /* Start on the "off" face; refresh() flips it if the keyboard is ready. */
    lv_obj_add_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
    s_pc.ready_shown = false;

    s_pc.timer = lv_timer_create(refresh, 500, NULL);
    refresh(NULL);
    return &s_pc;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_pc.mouse_on) {
        mouse_show(false);      /* the gyro request and the 20 ms timer */
    }
    if (s_pc.timer) {
        lv_timer_delete(s_pc.timer);
        s_pc.timer = NULL;
    }
}

static bool button(aos_app_t *self, void *inst, int action)
{
    (void)self; (void)inst;
    if (s_pc.mouse_on && action == AOS_BUTTON_CLICK) {
        aos_hal_usb_click(1);
        return true;
    }
    return false;
}

void aos_app_pcremote_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.pcremote",
            .name     = "Control PC",
            .icon     = LV_SYMBOL_USB,
            .color_a  = 0x5E5CE6,
            .color_b  = 0x2C2A80,
            .order    = 47,
        },
        .create  = create,
        .destroy = destroy,
        .button  = button,
    };
}
