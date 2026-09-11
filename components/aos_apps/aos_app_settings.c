/* AmoledOS - Settings: brightness, volume, menu style, network and system. */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_watchface.h"
#include "aos_i18n.h"
#include "aos_wifi_qr.h"
#include "aos_pair_ui.h"

#include <stdio.h>
#include <string.h>
#ifdef AOS_SIM
#include <stdlib.h>          /* getenv, only for the audit hook */
#endif
#ifndef AOS_SIM
#include "esp_heap_caps.h"
#include "aos_dynapp.h"
#endif

typedef struct {
    lv_obj_t *net_label;
    lv_obj_t *ap_row;           /* touchable card with the AP's name           */
    lv_obj_t *ap_row_ssid;
    lv_obj_t *ap_label;         /* how to connect, with the AP up              */
    lv_obj_t *ap_box;           /* second screen: AP password and QR           */
    lv_obj_t *ap_box_ssid;
    lv_obj_t *ap_box_pass;
    lv_obj_t *ap_box_qr;
    lv_obj_t *ap_box_modo;
    char      ap_box_qr_texto[128];  /* the last thing encoded in the QR       */
    lv_obj_t *bt_label;         /* state of the link                           */
    lv_obj_t *bt_forget;        /* forget button, only if something is paired  */
    lv_obj_t *bt_box;           /* second screen: pairing                      */
    lv_obj_t *bt_box_texto;
    lv_obj_t *bt_box_codigo;
    lv_obj_t *bt_box_si, *bt_box_no;
    lv_timer_t *bt_box_timer;
    lv_obj_t *cat_box;          /* second screen: filter by category           */
    lv_obj_t *mem_label;
    lv_obj_t *clock_box;        /* date and time setting screen                */
    lv_obj_t *cal_box;          /* touch calibration screen                    */
    int       cal_paso;
    int32_t   cal_rx[5], cal_ry[5];
    lv_obj_t *raw_box;          /* raw touch view: what the digitiser reports  */
    lv_obj_t *raw_label;
    lv_obj_t *raw_dot;          /* where the stored fit puts the raw point     */
    int32_t   raw_xmin, raw_xmax, raw_ymin, raw_ymax;
    uint32_t  raw_n;
    lv_obj_t *r_day, *r_mon, *r_year, *r_hour, *r_min;
    lv_timer_t *timer;
} settings_t;

static settings_t s_set;

static void brightness_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    aos_hal_brightness_set((int)lv_slider_get_value(slider));
}

static void volume_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    aos_hal_volume_set((int)lv_slider_get_value(slider));
    aos_hal_beep(1000, 40);
}

static void style_cb(lv_event_t *event)
{
    aos_launcher_style_t style =
        (aos_launcher_style_t)(uintptr_t)lv_event_get_user_data(event);
    aos_ui_launcher_set_style(style);
    aos_ui_toast(style == AOS_LAUNCHER_LIST ? _("Menu: lista") :
                 style == AOS_LAUNCHER_GRID ? _("Menu: grilla") : _("Menu: panal"), 1200);
}

static void face_cb(lv_event_t *event)
{
    (void)event;
    aos_ui_request_watchface_picker();
}

static void aod_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    aos_hal_aod_enable(on);
    aos_ui_toast(on ? _("Siempre encendido") : _("La pantalla se apaga"), 1400);
}

static void power_saving_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    aos_hal_power_saving_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void battery_care_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    aos_hal_battery_care_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void panel_sleep_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    aos_hal_panel_sleep_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void light_sleep_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    aos_hal_light_sleep_enable(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void aod_brightness_cb(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);
    aos_hal_aod_brightness_set((int)lv_slider_get_value(slider));
}

static void sync_cb(lv_event_t *event)
{
    (void)event;
    aos_ui_toast(aos_hal_net_sync_time() ? _("Sincronizando hora...")
                                         : _("Sin conexion"), 1600);
}

static void wifi_toggle_cb(lv_event_t *event)
{
    lv_obj_t *sw = lv_event_get_target(event);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    /* Switching it off gives back ~60 KB of executable memory, which is where
     * the code of dynamic apps comes from. The two largest do not fit with the
     * radio up, so this switch is also an app switch. */
    aos_hal_net_enable(on);
    aos_ui_toast(on ? _("Wifi encendida")
                    : _("Wifi apagada, memoria liberada"), 1600);
}

/* -------------------------------------------------------------------------- */
/* Network onboarding through the access point                                 */
/* -------------------------------------------------------------------------- */

/* Bringing the AP up takes a few hundred ms and blocks the LVGL task. It is
 * deferred for a moment so the notice manages to draw before the jolt. */
static void ap_start_deferred(lv_timer_t *timer)
{
    lv_timer_delete(timer);

    if (!aos_hal_net_ap_start()) {
        aos_ui_toast(_("No se pudo levantar la red"), 2000);
        return;
    }
    if (s_set.ap_label) {
        lv_obj_remove_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_set.ap_row) {
        lv_obj_remove_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* --------------------------------------------------------------------------
 * The AP's second screen: the big password and the QR to connect with
 *
 * Typing a ten-character password on a phone keyboard while looking at a
 * 368 px screen is exactly the moment when you get it wrong, and with the
 * rotating password you have to do it every time. The QR takes that out of the
 * way: it is the format Android and iOS understand from the camera out of the
 * box.
 * -------------------------------------------------------------------------- */

/* Building the text lives in aos_ui/aos_wifi_qr.c: it is pure string escaping,
 * the rule is easy to break, and a malformed QR draws just as prettily. Over
 * there it is tested with tools/qr_harness.c; here it is only drawn. */
static void ap_box_qr_texto(char *out, size_t len)
{
    if (!aos_wifi_qr_text(out, len, aos_hal_net_ap_ssid(),
                          aos_hal_net_ap_pass())) {
        out[0] = 0;
    }
}

static void ap_box_close(void)
{
    if (s_set.ap_box) {
        lv_obj_delete(s_set.ap_box);
        s_set.ap_box = NULL;
        s_set.ap_box_ssid = NULL;
        s_set.ap_box_pass = NULL;
        s_set.ap_box_qr   = NULL;
        s_set.ap_box_modo = NULL;
        s_set.ap_box_qr_texto[0] = 0;
    }
}

static void ap_box_close_cb(lv_event_t *event)
{
    (void)event;
    ap_box_close();
}

/* Refreshes the texts and, if needed, the QR. Called by the app's timer: with
 * the rotating password, switching the AP off and back on changes it while
 * this screen is open. Re-encoding the QR is expensive, so it is only done
 * when the content really changed. */
static void ap_box_refresh(void)
{
    if (!s_set.ap_box) {
        return;
    }
    if (s_set.ap_box_ssid) {
        lv_label_set_text(s_set.ap_box_ssid, aos_hal_net_ap_ssid());
    }
    if (s_set.ap_box_pass) {
        lv_label_set_text(s_set.ap_box_pass, aos_hal_net_ap_pass());
    }
    if (s_set.ap_box_modo) {
        lv_label_set_text(s_set.ap_box_modo,
                          aos_hal_net_ap_pass_mode() == AOS_AP_PASS_ROTATING
                              ? _("Clave nueva cada vez que se levanta la red")
                              : _("Clave fija"));
    }
    if (s_set.ap_box_qr) {
        char texto[128];
        ap_box_qr_texto(texto, sizeof(texto));
        /* With empty text it hides itself rather than encoding nothing: a
         * zero-byte QR draws just the same and leads nowhere. */
        if (!texto[0]) {
            lv_obj_add_flag(s_set.ap_box_qr, LV_OBJ_FLAG_HIDDEN);
        } else if (strcmp(texto, s_set.ap_box_qr_texto) != 0) {
            snprintf(s_set.ap_box_qr_texto, sizeof(s_set.ap_box_qr_texto),
                     "%s", texto);
            lv_qrcode_update(s_set.ap_box_qr, texto, strlen(texto));
            lv_obj_remove_flag(s_set.ap_box_qr, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ap_box_open(lv_event_t *event)
{
    (void)event;
    if (s_set.ap_box) {
        return;
    }

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.ap_box = box;
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);
    /* With the QR and the two buttons it does not all fit at once in 448 px in
     * German. */
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);

    aos_label(box, _("Red de configuracion"), aos_font_small, AOS_C_DIM);

    s_set.ap_box_ssid = aos_label(box, aos_hal_net_ap_ssid(), aos_font_body,
                                  AOS_C_TEXT);
    lv_obj_set_width(s_set.ap_box_ssid, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(s_set.ap_box_ssid, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.ap_box_ssid, LV_TEXT_ALIGN_CENTER, 0);

    aos_label(box, _("clave"), aos_font_small, AOS_C_DIM);

    /* The password on a card and in a large size: it is what you look at while
     * typing on the phone. */
    lv_obj_t *card = lv_obj_create(box);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, AOS_SCREEN_W - 60);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_set.ap_box_pass = aos_label(card, aos_hal_net_ap_pass(), aos_font_body,
                                  AOS_C_GREEN);
    lv_obj_set_width(s_set.ap_box_pass, AOS_SCREEN_W - 80);
    lv_label_set_long_mode(s_set.ap_box_pass, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.ap_box_pass, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_set.ap_box_pass);

    s_set.ap_box_modo = aos_label(box, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_width(s_set.ap_box_modo, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(s_set.ap_box_modo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.ap_box_modo, LV_TEXT_ALIGN_CENTER, 0);

    /* White background and black modules, not the theme's colours: an inverted
     * QR is read by roughly half of phones, and the half that fails does not
     * say why. The white quiet zone around it is part of the format. */
    s_set.ap_box_qr = lv_qrcode_create(box);
    lv_qrcode_set_size(s_set.ap_box_qr, 138);
    lv_qrcode_set_dark_color(s_set.ap_box_qr, lv_color_black());
    lv_qrcode_set_light_color(s_set.ap_box_qr, lv_color_white());
    lv_obj_set_style_border_color(s_set.ap_box_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_set.ap_box_qr, 6, 0);

    lv_obj_t *ayuda = aos_label(box, _("Escanea el QR con la camara del celular"),
                                aos_font_small, AOS_C_DIM);
    lv_obj_set_width(ayuda, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(ayuda, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(ayuda, LV_TEXT_ALIGN_CENTER, 0);

    char url[64];
    snprintf(url, sizeof(url), "http://%s/", aos_hal_net_ap_ip());
    lv_obj_t *url_lbl = aos_label(box, url, aos_font_small, AOS_C_ACCENT);
    lv_obj_set_style_text_align(url_lbl, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(box, _("Listo"), AOS_C_CARD2, ap_box_close_cb, NULL);

    ap_box_refresh();
}

static void ap_cb(lv_event_t *event)
{
    (void)event;
    if (aos_hal_net_ap_active()) {
        aos_hal_net_ap_stop();
        ap_box_close();
        if (s_set.ap_label) {
            lv_obj_add_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_set.ap_row) {
            lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
        }
        aos_ui_toast(_("Red de configuracion apagada"), 1600);
        return;
    }
    aos_ui_toast(_("Levantando red..."), 1200);
    lv_timer_set_repeat_count(lv_timer_create(ap_start_deferred, 60, NULL), 1);
}

static void forget_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_net_forget();
    aos_ui_toast(_("Red olvidada"), 1600);
}

/* -------------------------------------------------------------------------- */
/* Setting the date and time by hand                                           */
/* -------------------------------------------------------------------------- */

static void clock_close(void)
{
    if (s_set.clock_box) {
        lv_obj_delete(s_set.clock_box);
        s_set.clock_box = NULL;
    }
}

static void clock_cancel_cb(lv_event_t *event)
{
    (void)event;
    clock_close();
}

static void clock_save_cb(lv_event_t *event)
{
    (void)event;
    struct tm t = {0};
    t.tm_mday = (int)lv_roller_get_selected(s_set.r_day) + 1;
    t.tm_mon  = (int)lv_roller_get_selected(s_set.r_mon);
    t.tm_year = (int)lv_roller_get_selected(s_set.r_year) + (2025 - 1900);
    t.tm_hour = (int)lv_roller_get_selected(s_set.r_hour);
    t.tm_min  = (int)lv_roller_get_selected(s_set.r_min);
    t.tm_sec  = 0;
    t.tm_isdst = -1;

    aos_ui_toast(aos_hal_time_set(&t) ? _("Hora ajustada")
                                      : _("No se pudo ajustar"), 1600);
    clock_close();
}

static lv_obj_t *roller(lv_obj_t *parent, const char *opts, uint16_t sel, int w)
{
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_roller_set_selected(r, sel, LV_ANIM_OFF);
    lv_obj_set_width(r, w);
    lv_obj_set_style_bg_color(r, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r, AOS_C_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_text_font(r, aos_font_body, 0);
    return r;
}

static void clock_cb(lv_event_t *event)
{
    (void)event;
    if (s_set.clock_box) {
        return;
    }

    struct tm now;
    aos_hal_time_now(&now);

    /* The lists are built once and stay static: lv_roller copies them. */
    static char dias[31 * 3 + 1];
    static char anios[11 * 5 + 1];
    static char horas[24 * 3 + 1];
    static char minutos[60 * 3 + 1];
    char *w = dias;
    for (int i = 1; i <= 31; i++)  w += sprintf(w, i > 1 ? "\n%d" : "%d", i);
    w = anios;
    for (int i = 0; i <= 10; i++)  w += sprintf(w, i ? "\n%d" : "%d", 2025 + i);
    w = horas;
    for (int i = 0; i < 24; i++)   w += sprintf(w, i ? "\n%02d" : "%02d", i);
    w = minutos;
    for (int i = 0; i < 60; i++)   w += sprintf(w, i ? "\n%02d" : "%02d", i);

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.clock_box = box;
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 10, 0);

    aos_label(box, _("Fecha y hora"), aos_font_body, AOS_C_TEXT);

    lv_obj_t *fila_f = lv_obj_create(box);
    lv_obj_remove_style_all(fila_f);
    lv_obj_set_size(fila_f, AOS_SCREEN_W - 30, 110);
    lv_obj_set_flex_flow(fila_f, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila_f, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila_f, 6, 0);
    s_set.r_day  = roller(fila_f, dias, (uint16_t)(now.tm_mday - 1), 74);
    s_set.r_mon  = roller(fila_f,
                          _("Ene\nFeb\nMar\nAbr\nMay\nJun\n"
                            "Jul\nAgo\nSep\nOct\nNov\nDic"),
                          (uint16_t)now.tm_mon, 84);
    s_set.r_year = roller(fila_f, anios,
                          (uint16_t)((now.tm_year + 1900) > 2025
                                     ? (now.tm_year + 1900) - 2025 : 0), 96);

    lv_obj_t *fila_h = lv_obj_create(box);
    lv_obj_remove_style_all(fila_h);
    lv_obj_set_size(fila_h, AOS_SCREEN_W - 30, 110);
    lv_obj_set_flex_flow(fila_h, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila_h, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila_h, 10, 0);
    s_set.r_hour = roller(fila_h, horas,    (uint16_t)now.tm_hour, 88);
    s_set.r_min  = roller(fila_h, minutos,  (uint16_t)now.tm_min,  88);

    lv_obj_t *fila_b = lv_obj_create(box);
    lv_obj_remove_style_all(fila_b);
    lv_obj_set_size(fila_b, AOS_SCREEN_W - 30, 64);
    lv_obj_set_flex_flow(fila_b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila_b, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila_b, 12, 0);
    aos_button(fila_b, _("Cancelar"), AOS_C_CARD2, clock_cancel_cb, NULL);
    aos_button(fila_b, _("Guardar"),  AOS_C_GREEN, clock_save_cb,   NULL);
}

/* -------------------------------------------------------------------------- */
/* Touch calibration                                                           */
/*                                                                             */
/* Five points: the four corners (inset 55 px so the panel's rounding does not */
/* eat them) and the centre. That is enough for a linear least-squares fit per */
/* axis, which corrects offset AND scale. A single point would only correct    */
/* offset and we would not see whether there is stretch as well.               */
/* -------------------------------------------------------------------------- */

#define CAL_PUNTOS  5

/* The four corner points are referred to AOS_TOUCH_Y_MIN / AOS_TOUCH_Y_MAX
 * and not to the screen's edges.
 *
 * The bottom two were at AOS_SCREEN_H - 55 = 393, and the sensor dies at 395:
 * the two most important points of the fit fell EXACTLY on the last pixel the
 * chip knows how to report. Hitting them meant putting a finger right on the
 * edge of the sensitive area, so they were measured in the worst possible
 * place and on the verge of not being measured at all. Raising them to
 * AOS_TOUCH_Y_MAX - 40 puts them well inside the useful range and the fit
 * comes from points the sensor reads with room to spare.
 *
 * The top two had the same problem and nobody saw it until 2026-09-11: they
 * sat at y = 55, which is where the digitiser's raw Y reaches 0. A finger a
 * few pixels above the cross reads exactly the same as one on it, so the fit
 * was being anchored on a saturated value. They now sit at AOS_TOUCH_Y_MIN +
 * 40, 40 px inside the window like their bottom counterparts.
 *
 * And beware the opposite temptation, which was the first idea on discovering
 * the ceiling: pushing them OUT so the calibration "covers" the edges of the
 * screen achieves nothing. There is nothing to cover -the chip does not report
 * there- and all you get is a point that cannot be touched and a worse fit. */
static const lv_point_t CAL_OBJETIVO[CAL_PUNTOS] = {
    { 55, AOS_TOUCH_Y_MIN + 40 }, { AOS_SCREEN_W - 55, AOS_TOUCH_Y_MIN + 40 },
    { 55, AOS_TOUCH_Y_MAX - 40 }, { AOS_SCREEN_W - 55, AOS_TOUCH_Y_MAX - 40 },
    { AOS_SCREEN_W / 2, AOS_SCREEN_H / 2 },
};

static void cal_dibujar_objetivo(void);

static void cal_cerrar(void)
{
    aos_ui_touch_raw(false);
    aos_ui_block_gestures(false);
    if (s_set.cal_box) {
        lv_obj_delete(s_set.cal_box);
        s_set.cal_box = NULL;
    }
}

/* screen = a * raw + b, by least squares over the CAL_PUNTOS pairs.
 * Returns false when the fit is not to be trusted; a and b still hold what
 * came out, for the log. */
static bool cal_ajustar(const int32_t *crudo, const int32_t *esperado,
                        float *a, float *b)
{
    float sr = 0, se = 0, sre = 0, srr = 0;
    for (int i = 0; i < CAL_PUNTOS; i++) {
        float r = (float)crudo[i], e = (float)esperado[i];
        sr += r; se += e; sre += r * e; srr += r * r;
    }
    float den = CAL_PUNTOS * srr - sr * sr;
    if (den > -0.001f && den < 0.001f) {     /* all the same: no data */
        *a = 1.0f; *b = 0.0f;
        return false;
    }
    *a = (CAL_PUNTOS * sre - sr * se) / den;
    *b = (se - *a * sr) / CAL_PUNTOS;

    /* Safety net: a fit this far from 1 is a mis-tap, not a panel. It used to
     * be 0.7..1.4 and to fall back to the identity IN SILENCE, under a
     * "Touch calibrated" toast: the v2's real Y factor is ~0.76 (a 340 px
     * window stretched over 448), so a valid measurement sat 0.06 from being
     * thrown away and replaced by a panel misplaced by 55 px, with nothing to
     * tell the two apart. Now the caller keeps the previous calibration and
     * says so; 0.5..2.0 still catches garbage. */
    if (*a < 0.5f || *a > 2.0f) {
        return false;
    }
    return true;
}

static void cal_press_cb(lv_event_t *event)
{
    (void)event;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);   /* in raw units: raw is active */

    s_set.cal_rx[s_set.cal_paso] = p.x;
    s_set.cal_ry[s_set.cal_paso] = p.y;
    s_set.cal_paso++;

    if (s_set.cal_paso < CAL_PUNTOS) {
        cal_dibujar_objetivo();
        return;
    }

    int32_t ex[CAL_PUNTOS], ey[CAL_PUNTOS];
    for (int i = 0; i < CAL_PUNTOS; i++) {
        ex[i] = CAL_OBJETIVO[i].x;
        ey[i] = CAL_OBJETIVO[i].y;
    }
    /* The five raw pairs go to the log: they ARE the measurement, and with
     * the targets known they say where the digitiser's window is. */
    aos_hal_log("touch", "calibration raw: (%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d) "
                         "for targets (%d,%d) (%d,%d) (%d,%d) (%d,%d) (%d,%d)",
                (int)s_set.cal_rx[0], (int)s_set.cal_ry[0],
                (int)s_set.cal_rx[1], (int)s_set.cal_ry[1],
                (int)s_set.cal_rx[2], (int)s_set.cal_ry[2],
                (int)s_set.cal_rx[3], (int)s_set.cal_ry[3],
                (int)s_set.cal_rx[4], (int)s_set.cal_ry[4],
                (int)ex[0], (int)ey[0], (int)ex[1], (int)ey[1],
                (int)ex[2], (int)ey[2], (int)ex[3], (int)ey[3],
                (int)ex[4], (int)ey[4]);

    float ax, bx, ay, by;
    bool ok_x = cal_ajustar(s_set.cal_rx, ex, &ax, &bx);
    bool ok_y = cal_ajustar(s_set.cal_ry, ey, &ay, &by);
    cal_cerrar();

    if (!ok_x || !ok_y) {
        /* Nothing is saved: the previous calibration is still in place,
         * because the measurement only switched the correction off (raw mode)
         * and never wiped it. */
        aos_hal_log("touch", "calibration REJECTED (x a=%d/10000 b=%d/100, "
                             "y a=%d/10000 b=%d/100): the previous one stays",
                    (int)(ax * 10000), (int)(bx * 100),
                    (int)(ay * 10000), (int)(by * 100));
        aos_ui_toast(_("Calibración descartada, repetila"), 2500);
        return;
    }

    aos_ui_touch_calibration_save(ax, bx, ay, by);
    aos_ui_toast(_("Tactil calibrado"), 1800);
}

static void cal_dibujar_objetivo(void)
{
    lv_obj_t *box = s_set.cal_box;
    lv_obj_clean(box);

    const lv_point_t *o = &CAL_OBJETIVO[s_set.cal_paso];

    /* Cross + circle, all with simple objects: nothing that builds a layer. */
    lv_obj_t *h = lv_obj_create(box);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, 34, 2);
    lv_obj_set_pos(h, o->x - 17, o->y - 1);
    lv_obj_set_style_bg_color(h, AOS_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);

    lv_obj_t *v = lv_obj_create(box);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, 2, 34);
    lv_obj_set_pos(v, o->x - 1, o->y - 17);
    lv_obj_set_style_bg_color(v, AOS_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);

    lv_obj_t *c = lv_obj_create(box);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, 16, 16);
    lv_obj_set_pos(c, o->x - 8, o->y - 8);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, AOS_C_TEXT, 0);

    char txt[64];
    snprintf(txt, sizeof(txt), _("Tocá el centro de la cruz\n%d de %d"),
             s_set.cal_paso + 1, CAL_PUNTOS);
    lv_obj_t *l = aos_label(box, txt, aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, s_set.cal_paso == 4 ? 70 : 0);
}

static void cal_cb(lv_event_t *event)
{
    (void)event;
    if (s_set.cal_box) {
        return;
    }
    s_set.cal_paso = 0;

    /* Uncorrected while measuring: otherwise we would be calibrating on top of
     * the previous correction and the error would accumulate on every pass.
     * Raw mode alone does that -the wrapper skips the fit while it is on-, so
     * the stored calibration is NOT wiped first: an attempt that is abandoned
     * (physical button, reboot) or rejected leaves the watch with the
     * calibration it had. Wiping it here is what used to leave the panel
     * uncalibrated, and misplaced by 55 px, after an interrupted attempt. */
    aos_ui_touch_raw(true);
    aos_ui_block_gestures(true);    /* a tap that slides must not be "back" */

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.cal_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, cal_press_cb, LV_EVENT_PRESSED, NULL);

    cal_dibujar_objetivo();
}

/* -------------------------------------------------------------------------- */
/* Raw touch view                                                              */
/*                                                                             */
/* What the digitiser reports with no correction on top: the live raw point   */
/* and the extremes seen since the screen opened. Run a finger around the      */
/* whole glass, edge to edge, and the four extremes ARE the chip's window. On  */
/* the v2 (CST820) the expectation is x 0..367 and y 0..447, with 0 and 447    */
/* reached well INSIDE the glass, ~55 px from the top and bottom edges. The    */
/* numbers are logged when the screen closes, so the portal's log keeps them.  */
/* -------------------------------------------------------------------------- */

static void raw_refresh(int32_t x, int32_t y)
{
    if (!s_set.raw_label) {
        return;
    }
    if (s_set.raw_n == 0) {
        lv_label_set_text(s_set.raw_label, _("Recorré todo el vidrio con el dedo"));
        return;
    }
    /* The stored fit, applied here by hand because raw mode has switched it
     * off in the wrapper: the dot is where a normal touch would land. While
     * it sits under the finger the fit is right there; where it stops
     * following the finger, the window has ended. */
    float ax, bx, ay, by;
    aos_ui_touch_calibration_get(&ax, &bx, &ay, &by);
    int32_t fx = (int32_t)(ax * (float)x + bx + 0.5f);
    int32_t fy = (int32_t)(ay * (float)y + by + 0.5f);
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > AOS_SCREEN_W - 1) fx = AOS_SCREEN_W - 1;
    if (fy > AOS_SCREEN_H - 1) fy = AOS_SCREEN_H - 1;
    if (s_set.raw_dot) {
        lv_obj_set_pos(s_set.raw_dot, fx - 7, fy - 7);
    }
    lv_label_set_text_fmt(s_set.raw_label,
                          _("crudo %d,%d  ->  %d,%d\nx %d..%d\ny %d..%d\n%u lecturas"),
                          (int)x, (int)y, (int)fx, (int)fy,
                          (int)s_set.raw_xmin, (int)s_set.raw_xmax,
                          (int)s_set.raw_ymin, (int)s_set.raw_ymax,
                          (unsigned)s_set.raw_n);
}

static void raw_close(void)
{
    if (!s_set.raw_box) {
        return;
    }
    aos_ui_touch_raw(false);
    aos_ui_block_gestures(false);
    if (s_set.raw_n) {
        aos_hal_log("touch", "raw sweep: x %d..%d  y %d..%d  (%u samples)",
                    (int)s_set.raw_xmin, (int)s_set.raw_xmax,
                    (int)s_set.raw_ymin, (int)s_set.raw_ymax,
                    (unsigned)s_set.raw_n);
    }
    lv_obj_delete(s_set.raw_box);
    s_set.raw_box   = NULL;
    s_set.raw_label = NULL;
    s_set.raw_dot   = NULL;
}

static void raw_close_cb(lv_event_t *event)
{
    (void)event;
    raw_close();
}

static void raw_touch_cb(lv_event_t *event)
{
    (void)event;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);   /* raw: the correction is off */
    if (s_set.raw_n == 0) {
        s_set.raw_xmin = s_set.raw_xmax = p.x;
        s_set.raw_ymin = s_set.raw_ymax = p.y;
    } else {
        if (p.x < s_set.raw_xmin) s_set.raw_xmin = p.x;
        if (p.x > s_set.raw_xmax) s_set.raw_xmax = p.x;
        if (p.y < s_set.raw_ymin) s_set.raw_ymin = p.y;
        if (p.y > s_set.raw_ymax) s_set.raw_ymax = p.y;
    }
    s_set.raw_n++;
    raw_refresh(p.x, p.y);
}

static void raw_cb(lv_event_t *event)
{
    (void)event;
    if (s_set.raw_box) {
        return;
    }
    s_set.raw_n = 0;
    aos_ui_touch_raw(true);
    aos_ui_block_gestures(true);    /* the sweep IS a long drag: no "back" */

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.raw_box = box;
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(box, raw_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(box, raw_touch_cb, LV_EVENT_PRESSING, NULL);

    /* A frame on the very edge of the framebuffer: if a finger on it does not
     * take the extremes to 0 / 367 / 447, the chip cannot see out there. */
    lv_obj_t *frame = lv_obj_create(box);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, AOS_C_ACCENT, 0);
    aos_make_decorative(frame);

    /* A ruler: one mark every 50 px down the left edge and along the top, so
     * "the number stopped moving at the 400 mark" can be said with the eyes
     * and turned into a pixel. */
    for (int32_t y = 50; y < AOS_SCREEN_H; y += 50) {
        char t[8];
        snprintf(t, sizeof(t), "%d", (int)y);
        lv_obj_t *tick = aos_label(box, t, aos_font_small, AOS_C_DIM);
        lv_obj_set_pos(tick, 6, y - 8);
        aos_make_decorative(tick);
        lv_obj_t *mark = lv_obj_create(box);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, 14, 1);
        lv_obj_set_pos(mark, AOS_SCREEN_W - 14, y);
        lv_obj_set_style_bg_color(mark, AOS_C_DIM, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        aos_make_decorative(mark);
    }
    for (int32_t x = 50; x < AOS_SCREEN_W; x += 50) {
        char t[8];
        snprintf(t, sizeof(t), "%d", (int)x);
        lv_obj_t *tick = aos_label(box, t, aos_font_small, AOS_C_DIM);
        lv_obj_set_pos(tick, x - 10, AOS_SCREEN_H - 22);
        aos_make_decorative(tick);
    }

    s_set.raw_dot = lv_obj_create(box);
    lv_obj_remove_style_all(s_set.raw_dot);
    lv_obj_set_size(s_set.raw_dot, 14, 14);
    lv_obj_set_style_radius(s_set.raw_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_set.raw_dot, AOS_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_set.raw_dot, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_set.raw_dot, AOS_SCREEN_W / 2 - 7, AOS_SCREEN_H / 2 - 7);
    aos_make_decorative(s_set.raw_dot);

    s_set.raw_label = aos_label(box, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_text_align(s_set.raw_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_set.raw_label, LV_ALIGN_CENTER, 0, -70);
    aos_make_decorative(s_set.raw_label);
    raw_refresh(0, 0);

    /* Closes from the centre, the one place a sweep along the edges never
     * crosses. The physical button closes it too (back()). */
    lv_obj_t *btn = aos_button(box, _("Listo"), AOS_C_CARD2, raw_close_cb, NULL);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 50);
}

static void reboot_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_reboot();
}

/* Languages found on the card. Surveyed when Settings opens and kept, because
 * the dropdown's callback only receives an index. */
static aos_lang_t s_langs[AOS_LANG_MAX];
static int        s_lang_count;

static void lang_cb(lv_event_t *event)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(event));
    if ((int)sel >= s_lang_count) {
        return;
    }
    if (strcmp(s_langs[sel].code, aos_i18n_current()) == 0) {
        return;
    }
    /* Deferred: applying it here would destroy this very screen inside its own
     * callback, and would also free the strings that draw it. */
    aos_ui_request_language(s_langs[sel].code);
}

/* -------------------------------------------------------------------------- */
/* Bluetooth and notifications                                                 */
/* -------------------------------------------------------------------------- */

/* A "text on the left, switch on the right" row. The two that already existed
 * -always-on and WiFi- are written by hand; from here on there are four more
 * and repeating them would be copying twenty lines four times. */
static lv_obj_t *switch_row(lv_obj_t *parent, const char *texto, bool puesto,
                            lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AOS_SCREEN_W - 70, 44);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    /* The text with a bounded width and line wrapping: in German
     * "Benachrichtigungen stumm" does not fit on one line beside the switch. */
    lv_obj_t *lbl = aos_label(row, texto, aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(lbl, AOS_SCREEN_W - 70 - 66);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (puesto) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static void bt_box_close(void)
{
    /* Hand the pairing request back to the system overlay: while this screen
     * was open, it kept quiet. */
    aos_pair_ui_suppress(false);

    if (s_set.bt_box_timer) {
        lv_timer_delete(s_set.bt_box_timer);
        s_set.bt_box_timer = NULL;
    }
    if (s_set.bt_box) {
        lv_obj_delete(s_set.bt_box);
        s_set.bt_box = NULL;
        s_set.bt_box_texto = NULL;
        s_set.bt_box_codigo = NULL;
        s_set.bt_box_si = NULL;
        s_set.bt_box_no = NULL;
    }
}

static void bt_box_close_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_bt_pair_cancel();
    bt_box_close();
}

static void bt_box_confirm_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_bt_pair_confirm(true);
    bt_box_close();
    aos_ui_toast(_("Telefono emparejado"), 1800);
}

/* The pairing screen watches itself, and that is why it has a timer of its own.
 *
 * Settings refreshes every 2 seconds, which is fine for the network state but
 * is an eternity when waiting for a code the phone has just shown: you would
 * see the "look for AmoledOS" notice for up to two seconds after the code was
 * already there. */
static void bt_box_refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_set.bt_box) {
        return;
    }

    uint32_t codigo = aos_hal_bt_pair_code();
    if (!codigo) {
        lv_label_set_text(s_set.bt_box_texto,
                          _("Abri Ajustes -> Bluetooth en el telefono y elegi "
                            "AmoledOS"));
        lv_obj_add_flag(s_set.bt_box_codigo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_set.bt_box_si, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Numeric comparison: the same six-digit number on both sides and each one
     * confirms that they match. Without the big number and without the
     * explicit question this is an "accept whatever" button, which is exactly
     * what the method exists in order not to be. */
    char buf[16];
    snprintf(buf, sizeof(buf), "%06u", (unsigned)codigo);
    lv_label_set_text(s_set.bt_box_codigo, buf);
    lv_label_set_text(s_set.bt_box_texto, _("¿El telefono muestra este numero?"));
    lv_obj_remove_flag(s_set.bt_box_codigo, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_set.bt_box_si, LV_OBJ_FLAG_HIDDEN);
}

static void bt_pair_cb(lv_event_t *event)
{
    (void)event;
    if (s_set.bt_box) {
        return;
    }
    aos_hal_bt_pair_begin();
    /* Two screens saying the same thing, one on top of the other, is worse
     * than none: while this one is open, the system overlay keeps quiet. */
    aos_pair_ui_suppress(true);

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.bt_box = box;
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 16, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 14, 0);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);

    aos_label(box, _("Emparejar telefono"), aos_font_small, AOS_C_DIM);

    s_set.bt_box_texto = aos_label(box, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(s_set.bt_box_texto, AOS_SCREEN_W - 60);
    lv_label_set_long_mode(s_set.bt_box_texto, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.bt_box_texto, LV_TEXT_ALIGN_CENTER, 0);

    /* The code on a card and in a large size, just like the access point's
     * password: it is what you compare against the phone. */
    lv_obj_t *card = lv_obj_create(box);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, AOS_SCREEN_W - 90);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_set.bt_box_codigo = aos_label(card, "", aos_font_title, AOS_C_GREEN);
    lv_obj_center(s_set.bt_box_codigo);

    /* ROW_WRAP and content height, not a fixed 54 px row.
     *
     * With the fixed row, in German "Abbrechen" and "Stimmt uberein" did not
     * fit together and **were drawn on top of each other**. The layout audit
     * does not catch it -it measures clipped, off-screen and overflowing text,
     * not overlaps- and in Spanish it looks perfect, so it only shows up by
     * looking at the screen in the other language.
     *
     * Wrapping fixes it for any language without having to watch the length of
     * every translation: if the two buttons fit, they go side by side; if not,
     * the second drops down. */
    lv_obj_t *fila = lv_obj_create(box);
    lv_obj_remove_style_all(fila);
    lv_obj_set_width(fila, AOS_SCREEN_W - 50);
    lv_obj_set_height(fila, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(fila, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(fila, 8, 0);
    lv_obj_set_style_pad_column(fila, 10, 0);
    lv_obj_remove_flag(fila, LV_OBJ_FLAG_SCROLLABLE);
    s_set.bt_box_no = aos_button(fila, _("Cancelar"), AOS_C_CARD2,
                                 bt_box_close_cb, NULL);
    s_set.bt_box_si = aos_button(fila, _("Coincide"), AOS_C_GREEN,
                                 bt_box_confirm_cb, NULL);

    s_set.bt_box_timer = lv_timer_create(bt_box_refresh, 200, NULL);
    bt_box_refresh(NULL);
}

static void bt_toggle_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    aos_hal_bt_enable(on);
    if (!on) {
        bt_box_close();
    }
    /* The same warning as WiFi, and for the same reason: the BLE stack takes
     * ~30 KB of executable memory, which is where the code of dynamic apps
     * comes from (measured in docs/HANDOFF-BLE-ANCS.md, section 2.4). */
    aos_ui_toast(on ? _("Bluetooth encendido")
                    : _("Bluetooth apagado, memoria liberada"), 1600);
}

static void bt_forget_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_bt_forget();
    bt_box_close();
    aos_ui_toast(_("Telefono olvidado"), 1600);
}

static void notif_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    aos_hal_notif_enable(on);
    aos_ui_toast(on ? _("Notificaciones encendidas")
                    : _("No molestar: el telefono sigue conectado"), 1800);
}

static void notif_sound_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    aos_hal_notif_sound_set(on);
    if (on) {
        aos_hal_beep(1760, 70);
    }
}

static void notif_calls_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    aos_hal_notif_calls_always_set(on);
}

/* The names of the twelve ANCS categories. They live here and not in the HAL
 * because they are text that gets translated, and the HAL speaks no language. */
static const char *categoria_nombre(int c)
{
    switch (c) {
    case AOS_NOTIF_CALL_INCOMING: return _("Llamadas");
    case AOS_NOTIF_CALL_MISSED:   return _("Llamadas perdidas");
    case AOS_NOTIF_VOICEMAIL:     return _("Buzon de voz");
    case AOS_NOTIF_SOCIAL:        return _("Mensajes");
    case AOS_NOTIF_SCHEDULE:      return _("Agenda");
    case AOS_NOTIF_EMAIL:         return _("Correo");
    case AOS_NOTIF_NEWS:          return _("Noticias");
    case AOS_NOTIF_HEALTH:        return _("Salud");
    case AOS_NOTIF_FINANCE:       return _("Finanzas");
    case AOS_NOTIF_LOCATION:      return _("Ubicacion");
    case AOS_NOTIF_ENTERTAINMENT: return _("Entretenimiento");
    default:                      return _("Otras");
    }
}

static void cat_box_close(void)
{
    if (s_set.cat_box) {
        lv_obj_delete(s_set.cat_box);
        s_set.cat_box = NULL;
    }
}

static void cat_box_close_cb(lv_event_t *event)
{
    (void)event;
    cat_box_close();
}

static void cat_toggle_cb(lv_event_t *event)
{
    int c = (int)(intptr_t)lv_event_get_user_data(event);
    bool puesto = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);

    uint32_t mask = aos_hal_notif_categories();
    if (puesto) {
        mask |= (1u << c);
    } else {
        mask &= ~(1u << c);
    }
    aos_hal_notif_categories_set(mask);
}

static void cat_cb(lv_event_t *event)
{
    (void)event;
    if (s_set.cat_box) {
        return;
    }

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.cat_box = box;
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 4, 0);
    /* Twelve categories plus the title and the button do not fit in 448 px in
     * any language: this screen is born scrolling. */
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);

    aos_label(box, _("Que notificaciones mostrar"), aos_font_small, AOS_C_DIM);

    uint32_t mask = aos_hal_notif_categories();
    for (int c = 0; c < AOS_NOTIF_CATEGORY_COUNT; c++) {
        lv_obj_t *cb = lv_checkbox_create(box);
        lv_checkbox_set_text(cb, categoria_nombre(c));
        /* Fixed width, otherwise each checkbox centres itself with its own
         * text and the left edge of the list comes out ragged. With the same
         * width for all of them, the twelve boxes land in the same column. */
        lv_obj_set_width(cb, AOS_SCREEN_W - 110);
        lv_obj_set_style_text_font(cb, aos_font_small, 0);
        lv_obj_set_style_text_color(cb, AOS_C_TEXT, 0);
        lv_obj_set_style_bg_color(cb, AOS_C_GREEN,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_border_color(cb, AOS_C_DIM, LV_PART_INDICATOR);
        if (mask & (1u << c)) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(cb, cat_toggle_cb, LV_EVENT_VALUE_CHANGED,
                            (void *)(intptr_t)c);
    }

    /* What tools/nt_harness.c's bench says: a call with "calls always" on gets
     * through even if its category is unticked. Saying so here heads off the
     * question "why does the phone keep ringing?". */
    lv_obj_t *pie = aos_label(box, _("Las llamadas pasan igual si esta puesto "
                                     "\"Llamadas siempre\""),
                              aos_font_small, AOS_C_DIM);
    lv_obj_set_width(pie, AOS_SCREEN_W - 50);
    lv_label_set_long_mode(pie, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(pie, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(box, _("Listo"), AOS_C_CARD2, cat_box_close_cb, NULL);
}

static lv_obj_t *section(lv_obj_t *parent, const char *title)
{
    lv_obj_t *label = aos_label(parent, title, aos_font_small, AOS_C_DIM);
    lv_obj_set_style_pad_top(label, 10, 0);
    return label;
}

static lv_obj_t *slider(lv_obj_t *parent, int value, lv_event_cb_t cb)
{
    lv_obj_t *obj = lv_slider_create(parent);
    lv_obj_set_width(obj, AOS_SCREEN_W - 90);
    lv_obj_set_height(obj, 16);
    lv_slider_set_range(obj, 5, 100);
    lv_slider_set_value(obj, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(obj, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, AOS_C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(obj, AOS_C_TEXT, LV_PART_KNOB);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return obj;
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    if (!s_set.net_label) {
        return;
    }
    char buf[96];
    switch (aos_hal_net_state()) {
    case AOS_NET_CONNECTED:
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s  (%d dBm)\n%s",
                 aos_hal_net_ssid(), aos_hal_net_rssi(), aos_hal_net_ip());
        break;
    case AOS_NET_CONNECTING:
        /* The icon goes as an argument and not glued to the literal. Glued,
         * the string aos_tr() sees at run time starts with the glyph's bytes,
         * but gen_lang.py -which reads the source- only sees the text: the
         * catalogue key would never match, and silently so. */
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", _("conectando..."));
        break;
    case AOS_NET_FAILED:
        snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING "  %s", _("fallo la conexion"));
        break;
    default: {
        bool tiene = aos_hal_net_has_credentials();
        snprintf(buf, sizeof(buf), "%s  %s",
                 tiene ? LV_SYMBOL_CLOSE : LV_SYMBOL_WARNING,
                 tiene ? _("wifi apagado") : _("sin red configurada"));
        break;
    }
    }
    lv_label_set_text(s_set.net_label, buf);

    if (s_set.mem_label) {
#ifndef AOS_SIM
        /* 'huecos' is the number of separate free blocks. With a high total
         * and a small largest, that number says how scattered the memory has
         * become: it is the difference between "there is none" and "there is,
         * but in pieces". */
        multi_heap_info_t ej;
        heap_caps_get_info(&ej, MALLOC_CAP_EXEC);

        uint32_t p_libre = 0, p_mayor = 0;
        int      p_usados = 0;
        aos_dynapp_pool_info(&p_libre, &p_mayor, &p_usados);

        char mem[200];
        snprintf(mem, sizeof(mem),
                 _("reserva apps: %u K libres (mayor %u K, %d en uso)\n"
                   "ejecutable %u K  (mayor %u K, %u huecos)\n"
                   "interna %u K   psram %u K"),
                 (unsigned)(p_libre / 1024), (unsigned)(p_mayor / 1024), p_usados,
                 (unsigned)(ej.total_free_bytes / 1024),
                 (unsigned)(ej.largest_free_block / 1024),
                 (unsigned)ej.free_blocks,
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        lv_label_set_text(s_set.mem_label, mem);
#else
        lv_label_set_text(s_set.mem_label, _("memoria: no disponible en el simulador"));
#endif
    }

    if (aos_hal_net_ap_active()) {
        if (s_set.ap_label) {
            char ap[160];
            snprintf(ap, sizeof(ap), _("Ya conectado, abri\nhttp://%s/wifi"),
                     aos_hal_net_ap_ip());
            lv_label_set_text(s_set.ap_label, ap);
            lv_obj_remove_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_set.ap_row) {
            lv_label_set_text(s_set.ap_row_ssid, aos_hal_net_ap_ssid());
            lv_obj_remove_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        /* The AP can be switched off from the browser (POST /api/ap/estado),
         * not only with this screen's button. Without this branch the card and
         * the notice stayed visible until you left and came back in, and
         * touching the card opened the screen of a network that no longer
         * existed. */
        if (s_set.ap_label) {
            lv_obj_add_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_set.ap_row) {
            lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
        }
        ap_box_close();
    }

    if (s_set.bt_label) {
        char bt[128];
        switch (aos_hal_bt_state()) {
        case AOS_BT_CONNECTED: {
            int pila = 0;
            if (aos_hal_bt_phone_battery(&pila)) {
                snprintf(bt, sizeof(bt), "%s  %s  %s  %d%%",
                         LV_SYMBOL_BLUETOOTH, aos_hal_bt_peer(),
                         LV_SYMBOL_BATTERY_FULL, pila);
            } else {
                snprintf(bt, sizeof(bt), "%s  %s", LV_SYMBOL_BLUETOOTH,
                         aos_hal_bt_peer());
            }
            break;
        }
        case AOS_BT_PAIRING:
            snprintf(bt, sizeof(bt), "%s  %s", LV_SYMBOL_BLUETOOTH,
                     _("emparejando..."));
            break;
        case AOS_BT_ADVERTISING:
            snprintf(bt, sizeof(bt), "%s  %s", LV_SYMBOL_BLUETOOTH,
                     aos_hal_bt_bonded() ? _("buscando el telefono...")
                                         : _("sin telefono emparejado"));
            break;
        default:
            snprintf(bt, sizeof(bt), "%s  %s", LV_SYMBOL_CLOSE,
                     _("bluetooth apagado"));
            break;
        }
        lv_label_set_text(s_set.bt_label, bt);
    }

    /* The forget button only makes sense if there is something to forget, and
     * the pairing can also be undone from the phone. */
    if (s_set.bt_forget) {
        if (aos_hal_bt_bonded()) {
            lv_obj_remove_flag(s_set.bt_forget, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_set.bt_forget, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* The name and the password can change from the portal while this screen
     * is open: that is the normal case, because the portal is used precisely
     * while connected to this AP. */
    ap_box_refresh();
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(page, 12, 0);
    lv_obj_set_style_pad_ver(page, 20, 0);

    section(page, _("BRILLO"));
    slider(page, aos_hal_brightness_get(), brightness_cb);

    section(page, _("VOLUMEN"));
    slider(page, aos_hal_volume_get(), volume_cb);

    section(page, _("PANTALLA"));
    aos_button(page, _("Cambiar esfera"), AOS_C_CARD2, face_cb, NULL);

    lv_obj_t *aod_row = lv_obj_create(page);
    lv_obj_remove_style_all(aod_row);
    lv_obj_set_size(aod_row, AOS_SCREEN_W - 90, 40);
    lv_obj_remove_flag(aod_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *aod_label = aos_label(aod_row, _("Siempre encendido"),
                                    aos_font_small, AOS_C_TEXT);
    lv_obj_align(aod_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *aod_sw = lv_switch_create(aod_row);
    lv_obj_set_size(aod_sw, 56, 30);
    lv_obj_align(aod_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(aod_sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (aos_hal_aod_enabled()) {
        lv_obj_add_state(aod_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(aod_sw, aod_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *aod_slider = slider(page, aos_hal_aod_brightness_get(), aod_brightness_cb);
    lv_slider_set_range(aod_slider, 1, 40);
    lv_slider_set_value(aod_slider, aos_hal_aod_brightness_get(), LV_ANIM_OFF);

    section(page, _("ENERGIA"));
    switch_row(page, _("Ahorro de energia"), aos_hal_power_saving_enabled(), power_saving_cb);
    switch_row(page, _("Cuidar la bateria"), aos_hal_battery_care_enabled(), battery_care_cb);
    switch_row(page, _("Dormir el panel apagado"), aos_hal_panel_sleep_enabled(), panel_sleep_cb);
    switch_row(page, _("Dormir el chip apagado"), aos_hal_light_sleep_enabled(), light_sleep_cb);

    section(page, _("IDIOMA"));
    s_lang_count = aos_i18n_scan(s_langs, AOS_LANG_MAX);

    lv_obj_t *lang_dd = lv_dropdown_create(page);
    lv_obj_set_width(lang_dd, AOS_SCREEN_W - 90);
    lv_obj_set_style_text_font(lang_dd, aos_font_small, 0);
    lv_obj_set_style_bg_color(lang_dd, AOS_C_CARD2, 0);
    lv_obj_set_style_border_width(lang_dd, 0, 0);

    char opts[AOS_LANG_MAX * (AOS_LANG_NAME_MAX + 2)];
    opts[0] = '\0';
    int sel = 0;
    for (int i = 0; i < s_lang_count; i++) {
        if (i) {
            strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        }
        strncat(opts, s_langs[i].name, sizeof(opts) - strlen(opts) - 1);
        if (strcmp(s_langs[i].code, aos_i18n_current()) == 0) {
            sel = i;
        }
    }
    lv_dropdown_set_options(lang_dd, opts);
    lv_dropdown_set_selected(lang_dd, (uint32_t)sel);
    lv_obj_add_event_cb(lang_dd, lang_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* With no card -or with no /lang on it- the only option is the source
     * code's Spanish. Saying so is more useful than a one-item dropdown with
     * no explanation. */
    if (s_lang_count <= 1) {
        aos_label(page, _("sin packs en la tarjeta"), aos_font_small, AOS_C_DIM);
    } else {
        /* Where the current language came from. It matters: a pack on the card
         * beats the one shipped in the firmware, and finding that out by
         * looking at the screen is far quicker than deducing it when a
         * translation you swear you fixed keeps showing up wrong. */
        char cov[96];
        char base[64];
        snprintf(base, sizeof(base), _("%d cadenas, %d apps cubiertas"),
                 aos_i18n_count(), s_langs[sel].apps);
        /* A separate line and not " - firmware" glued on the end: on a single
         * line the text runs past 368 px with the pseudolocalisation pack,
         * which is exactly what that pack exists for. */
        snprintf(cov, sizeof(cov), "%s\n%s", base,
                 s_langs[sel].origin == AOS_LANG_EMBEDDED
                     ? C_("origen del idioma", "firmware")
                     : C_("origen del idioma", "tarjeta"));
        /* Explicit centring: the label has TWO lines since the origin dropped
         * onto its own line, and without this the second one is stuck to the
         * left while the whole rest of the screen centres. It is the same
         * thing net_label does a few sections below, for the same reason. */
        lv_obj_t *cov_lbl = aos_label(page,
                                      aos_i18n_count() ? cov
                                                       : _("espanol del codigo fuente"),
                                      aos_font_small, AOS_C_DIM);
        lv_obj_set_style_text_align(cov_lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    section(page, _("MENU"));
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AOS_SCREEN_W - 60, 56);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    aos_button(row, _("Lista"),  AOS_C_CARD2, style_cb, (void *)AOS_LAUNCHER_LIST);
    aos_button(row, _("Grilla"), AOS_C_CARD2, style_cb, (void *)AOS_LAUNCHER_GRID);
    aos_button(row, _("Panal"),  AOS_C_CARD2, style_cb, (void *)AOS_LAUNCHER_HONEYCOMB);

    section(page, _("RED"));

    lv_obj_t *wifi_row = lv_obj_create(page);
    lv_obj_remove_style_all(wifi_row);
    lv_obj_set_size(wifi_row, AOS_SCREEN_W - 70, 44);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    aos_label(wifi_row, _("Wifi encendida"), aos_font_body, AOS_C_TEXT);
    lv_obj_t *wifi_sw = lv_switch_create(wifi_row);
    lv_obj_set_size(wifi_sw, 56, 30);
    lv_obj_set_style_bg_color(wifi_sw, AOS_C_GREEN,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (aos_hal_net_enabled()) {
        lv_obj_add_state(wifi_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(wifi_sw, wifi_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_set.net_label = aos_label(page, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_text_align(s_set.net_label, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(page, _("Configurar red"), AOS_C_ACCENT, ap_cb, NULL);

    /* A touchable card with the AP's name, visible only with the network up.
     * It is the door to the second screen: that is where the password and the
     * QR are. The chevron is what says it can be touched; without it, nobody
     * tries. */
    s_set.ap_row = lv_obj_create(page);
    lv_obj_remove_style_all(s_set.ap_row);
    lv_obj_set_size(s_set.ap_row, AOS_SCREEN_W - 70, 62);
    lv_obj_set_style_bg_color(s_set.ap_row, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(s_set.ap_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_set.ap_row, 14, 0);
    lv_obj_set_style_pad_hor(s_set.ap_row, 14, 0);
    lv_obj_remove_flag(s_set.ap_row, LV_OBJ_FLAG_SCROLLABLE);

    s_set.ap_row_ssid = aos_label(s_set.ap_row, aos_hal_net_ap_ssid(),
                                  aos_font_small, AOS_C_TEXT);
    lv_obj_align(s_set.ap_row_ssid, LV_ALIGN_LEFT_MID, 0, -10);
    lv_obj_t *ap_row_hint = aos_label(s_set.ap_row, _("ver clave y QR"),
                                      aos_font_small, AOS_C_DIM);
    lv_obj_align(ap_row_hint, LV_ALIGN_LEFT_MID, 0, 12);
    lv_obj_t *ap_row_chev = aos_label(s_set.ap_row, LV_SYMBOL_RIGHT,
                                      aos_font_small, AOS_C_DIM);
    lv_obj_align(ap_row_chev, LV_ALIGN_RIGHT_MID, 0, 0);

    /* In LVGL 9 every lv_obj is born clickable: without this the three labels
     * eat the touch meant for the card and touching the name -which is exactly
     * what you do- opens nothing. */
    aos_make_decorative(s_set.ap_row);
    lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_set.ap_row, ap_box_open, LV_EVENT_CLICKED, NULL);

    /* What to do once connected. The name and the password no longer go here:
     * the card above says them, and it also has the QR. Repeating them filled
     * four lines and left the password in plain sight of anybody walking past
     * while you go through the settings. */
    char ap_buf[160];
    snprintf(ap_buf, sizeof(ap_buf),
             _("Ya conectado, abri\nhttp://%s/wifi"), aos_hal_net_ap_ip());
    s_set.ap_label = aos_label(page, ap_buf, aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(s_set.ap_label, LV_TEXT_ALIGN_CENTER, 0);
    if (!aos_hal_net_ap_active()) {
        lv_obj_add_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
    }

    if (aos_hal_net_has_credentials()) {
        aos_button(page, _("Olvidar red"), AOS_C_CARD2, forget_cb, NULL);
    }

    section(page, _("BLUETOOTH"));

    switch_row(page, _("Bluetooth"), aos_hal_bt_enabled(), bt_toggle_cb);

    s_set.bt_label = aos_label(page, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(s_set.bt_label, AOS_SCREEN_W - 50);
    lv_label_set_long_mode(s_set.bt_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.bt_label, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(page, _("Emparejar telefono"), AOS_C_ACCENT, bt_pair_cb, NULL);

    s_set.bt_forget = aos_button(page, _("Olvidar telefono"), AOS_C_CARD2,
                                 bt_forget_cb, NULL);

    section(page, _("NOTIFICACIONES"));

    switch_row(page, _("Notificaciones"), aos_hal_notif_enabled(), notif_cb);
    switch_row(page, _("Sonido"), aos_hal_notif_sound(), notif_sound_cb);
    switch_row(page, _("Llamadas siempre"), aos_hal_notif_calls_always(),
               notif_calls_cb);

    aos_button(page, _("Categorias"), AOS_C_CARD2, cat_cb, NULL);

    section(page, _("HORA"));
    aos_button(page, _("Ajustar a mano"), AOS_C_CARD2, clock_cb, NULL);
    aos_button(page, _("Sincronizar hora"), AOS_C_ACCENT, sync_cb, NULL);

    section(page, _("TACTIL"));
    aos_button(page, _("Calibrar"), AOS_C_CARD2, cal_cb, NULL);
    aos_button(page, _("Ver crudo"), AOS_C_CARD2, raw_cb, NULL);

    section(page, _("SISTEMA"));
    char buf[96];
    snprintf(buf, sizeof(buf), "AmoledOS %s\n%s",
             aos_hal_firmware_version(), aos_hal_board_name());
    lv_obj_t *about = aos_label(page, buf, aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(about, LV_TEXT_ALIGN_CENTER, 0);

    /* Live memory. The 'executable' one is what matters for dynamic apps:
     * their code comes out of it. The largest contiguous block is shown as
     * well, because running out of total and running out of a hole are two
     * different problems and are fixed differently. */
    s_set.mem_label = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(s_set.mem_label, LV_TEXT_ALIGN_CENTER, 0);
    /* Fixed width and line wrapping: these are several lines of diagnostics
     * and in Spanish they already graze the edge. Without this the label grows
     * with the text and runs off the screen on both sides at once. */
    lv_obj_set_width(s_set.mem_label, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(s_set.mem_label, LV_LABEL_LONG_MODE_WRAP);
    aos_button(page, _("Reiniciar"), AOS_C_RED, reboot_cb, NULL);

    s_set.timer = lv_timer_create(refresh, 2000, NULL);
    refresh(NULL);

#ifdef AOS_SIM
    /* AOS_SIM_AP=2 opens the AP screen straight away. It is the only way for
     * tools/audit_layout.sh to look at it: the audit walks each app's screen,
     * and this one hangs off lv_layer_top behind a scroll and a touch, which a
     * keystroke script does not hit twice in a row. With this, German -which
     * is where the texts run long- is reviewed like the rest. */
    const char *sim_ap = getenv("AOS_SIM_AP");
    if (sim_ap && sim_ap[0] == '2') {
        ap_box_open(NULL);
    }
    /* Same reason as the one above: the two new screens hang off lv_layer_top
     * behind a long scroll and a touch, and a keystroke script does not hit
     * them twice in a row. AOS_SIM_BT=1 opens the pairing one and =2 the
     * categories one, so audit_layout.sh looks at them in the three languages
     * like the rest. */
    const char *sim_bt = getenv("AOS_SIM_BT");
    if (sim_bt && sim_bt[0] == '1') {
        bt_pair_cb(NULL);
    } else if (sim_bt && sim_bt[0] == '2') {
        cat_cb(NULL);
    }
    /* AOS_SIM_TOUCH=1 opens the raw view, =2 the calibration screen. */
    const char *sim_touch = getenv("AOS_SIM_TOUCH");
    if (sim_touch && sim_touch[0] == '1') {
        raw_cb(NULL);
    } else if (sim_touch && sim_touch[0] == '2') {
        cal_cb(NULL);
    }
#endif
    return &s_set;
}

/* Back: first close whichever second screen is open, and only then leave
 * Settings. Without this, the back gesture from the AP screen takes the whole
 * app with it and you have to go through the entire scroll again, which is
 * exactly what you do not want with the rotating password in front of you. */
static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_set.ap_box) {
        ap_box_close();
        return true;
    }
    if (s_set.clock_box) {
        clock_close();
        return true;
    }
    if (s_set.cal_box) {
        cal_cerrar();
        return true;
    }
    if (s_set.raw_box) {
        raw_close();
        return true;
    }
    if (s_set.bt_box) {
        bt_box_close_cb(NULL);
        return true;
    }
    if (s_set.cat_box) {
        cat_box_close();
        return true;
    }
    return false;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_set.timer) {
        lv_timer_delete(s_set.timer);
        s_set.timer = NULL;
    }
    clock_close();
    cal_cerrar();
    raw_close();
    ap_box_close();
    bt_box_close();
    cat_box_close();
    s_set.bt_label    = NULL;
    s_set.bt_forget   = NULL;
    s_set.net_label   = NULL;
    s_set.ap_label    = NULL;
    s_set.ap_row      = NULL;
    s_set.ap_row_ssid = NULL;
    s_set.mem_label   = NULL;
}

void aos_app_settings_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.settings",
            .name     = "Ajustes",
            .icon     = LV_SYMBOL_SETTINGS,
            .color_a  = 0x8E8E93,
            .color_b  = 0x3A3A3C,
            .order    = 90,
        },
        .create  = create,
        .destroy = destroy,
        .back    = back,
    };
}
