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
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <stdio.h>

typedef struct {
    lv_obj_t   *status;
    lv_obj_t   *pad;        /* the buttons, shown when the keyboard is ready */
    lv_obj_t   *off;        /* the explanation and the switch, otherwise */
    lv_obj_t   *off_text;
    lv_obj_t   *off_button;
    lv_timer_t *timer;
    bool        ready_shown;
} pcremote_t;

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
            lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_pc.off, LV_OBJ_FLAG_HIDDEN);
        } else {
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

    /* --- the pad: five rows, the last one ending at y 354, under the touch
     * ceiling (AOS_TOUCH_Y_MAX, 390). --- */
    s_pc.pad = lv_obj_create(page);
    lv_obj_remove_style_all(s_pc.pad);
    lv_obj_set_size(s_pc.pad, lv_pct(100), 320);
    lv_obj_align(s_pc.pad, LV_ALIGN_TOP_MID, 0, 44);
    lv_obj_remove_flag(s_pc.pad, LV_OBJ_FLAG_SCROLLABLE);

    /* music */
    key_button(s_pc.pad, LV_SYMBOL_PREV,  AOS_C_CARD2,  "prev",  30, 0, 74, 56);
    key_button(s_pc.pad, LV_SYMBOL_PLAY,  AOS_C_ACCENT, "play", 138, 0, 92, 56);
    key_button(s_pc.pad, LV_SYMBOL_NEXT,  AOS_C_CARD2,  "next", 264, 0, 74, 56);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MID, AOS_C_CARD2, "voldown", 30, 66, 110, 50);
    key_button(s_pc.pad, LV_SYMBOL_MUTE,       AOS_C_CARD2, "mute",   142, 73, 84, 36);
    key_button(s_pc.pad, LV_SYMBOL_VOLUME_MAX, AOS_C_CARD2, "volup", 228, 66, 110, 50);

    /* slides: page up / page down move Keynote and PowerPoint; "b" blanks */
    key_button(s_pc.pad, LV_SYMBOL_LEFT,  AOS_C_CARD2, "pgup",  30, 132, 150, 50);
    key_button(s_pc.pad, LV_SYMBOL_RIGHT, AOS_C_CARD2, "pgdn", 188, 132, 150, 50);
    key_button(s_pc.pad, _("Pantalla negra"), AOS_C_CARD2, "b",       30, 196, 150, 50);
    key_button(s_pc.pad, "cmd + tab",         AOS_C_CARD2, "cmd+tab", 188, 196, 150, 50);

    /* the three keys every dialog wants */
    key_button(s_pc.pad, "esc",     AOS_C_CARD2, "esc",    30, 260, 96, 50);
    key_button(s_pc.pad, _("espacio"), AOS_C_CARD2, "space", 136, 260, 96, 50);
    key_button(s_pc.pad, LV_SYMBOL_OK, AOS_C_ACCENT, "enter", 242, 260, 96, 50);

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
    if (s_pc.timer) {
        lv_timer_delete(s_pc.timer);
        s_pc.timer = NULL;
    }
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
    };
}
