/*
 * AmoledOS - Settings.
 *
 * A short first page -six quick tiles, the brightness, and the categories
 * with their current value- and one page per category, built when it opens.
 * It used to be a single page nine screens long with every control of every
 * category on it: to change the language you scrolled past brightness,
 * power, the network and the rest. The category pages slide in like the
 * launcher's folders, and back closes them before it closes the app.
 *
 * The second screens that already existed (the access point's password and
 * QR, Bluetooth pairing, the notification categories, setting the clock by
 * hand, the touch calibration and raw view) are unchanged: they hang off
 * lv_layer_top over whichever page opened them.
 */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_watchface.h"
#include "aos_i18n.h"
#include "aos_wifi_qr.h"
#include "aos_pair_ui.h"
#include "aos_settings_glyphs.h"
#include "aos_quick.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef AOS_SIM
#include <stdlib.h>          /* getenv and atoi, only for the audit hooks */
#endif
#ifndef AOS_SIM
#include "esp_heap_caps.h"
#include "aos_dynapp.h"
#endif

/* What the Battery and Diagnostics pages refresh. Kept apart so closing
 * either page forgets all of it with one memset. */
typedef struct {
    lv_obj_t *pct, *bar, *state, *left;            /* Battery: the top card     */
    lv_obj_t *batt_chart, *batt_empty;
    lv_chart_series_t *dis, *chg;
    lv_obj_t *volt, *drain, *charger, *cycles, *lifetime;
    lv_obj_t *mem_val[3], *mem_bar[3], *apps;      /* Diagnostics              */
    lv_obj_t *mhz, *cpu_val[2], *cpu_bar[2];
    lv_obj_t *cpu_chart, *cpu_empty;
    lv_chart_series_t *cpu_ser[2];
    lv_obj_t *temp_val[3];
    lv_obj_t *temp_chart, *temp_empty;
    lv_chart_series_t *temp_ser[3];
    lv_obj_t *uptime;
    uint32_t  charts_at_min;                       /* the minute they were drawn */
} stats_ui_t;

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
    lv_obj_t *usb_label;        /* what the USB port is right now              */
    lv_obj_t *usb_check[4];     /* its mode, as four rows with a tick          */
    lv_obj_t *bt_label;         /* state of the link                           */
    lv_obj_t *bt_forget;        /* forget button, only if something is paired  */
    lv_obj_t *bt_box;           /* second screen: pairing                      */
    lv_obj_t *bt_box_texto;
    lv_obj_t *bt_box_codigo;
    lv_obj_t *bt_box_si, *bt_box_no;
    lv_timer_t *bt_box_timer;
    lv_obj_t *cat_box;          /* second screen: filter by category           */
    lv_obj_t *clock_box;        /* date and time setting screen                */
    lv_obj_t *cal_box;          /* touch calibration screen                    */
    int       cal_paso;
    int32_t   cal_rx[5], cal_ry[5];
    lv_obj_t *raw_box;          /* raw touch view: what the digitiser reports  */
    lv_obj_t *raw_label;
    lv_obj_t *raw_dot;          /* where the stored fit puts the raw point     */
    int32_t   raw_xmin, raw_xmax, raw_ymin, raw_ymax;
    uint32_t  raw_n;
    /* Register probe (pinch-probe fork): does a second finger ever show up? */
    lv_obj_t   *probe_label;
    lv_obj_t   *probe_dot2;     /* point 2 from 0x09..0x0C, if the chip fills it */
    lv_timer_t *probe_timer;
    uint32_t    probe_seq;
    uint8_t     probe_last[AOS_TOUCH_REGS];   /* last burst logged */
    uint32_t    probe_reads, probe_two, probe_p2;
    uint32_t    rate_ms, rate_n;    /* time with a finger down, samples in it */
    uint32_t    rate_last_ms;
    uint8_t     probe_max_fingers;
    lv_obj_t *r_day, *r_mon, *r_year, *r_hour, *r_min;
    lv_timer_t *timer;

    /* The pages (see the second half of this file) */
    lv_obj_t *root;
    lv_obj_t *main;             /* the first page                              */
    lv_obj_t *sub;              /* the open category's page, or NULL           */
    int       sub_kind;
    lv_obj_t *tiles;            /* the six quick tiles (aos_quick.h)            */
    lv_obj_t *value[13];        /* the right-hand text of each category row    */
    lv_obj_t *lang_check[AOS_LANG_MAX];
    lv_obj_t *style_card[3];
    lv_obj_t *time_label, *date_label;
    stats_ui_t st;              /* Battery and Diagnostics                     */
    lv_obj_t *dnd_from_val, *dnd_to_val;   /* Notifications: the schedule */
    lv_obj_t *dnd_box;                     /* second screen: picking a time */
    lv_obj_t *r_dh, *r_dm;
    int       dnd_which;                   /* 0 from, 1 to */
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
    aos_quick_set_wifi(lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
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
/* The rows are their own numbers and not derived from the window constants:
 * the map trusts the fit between its anchors (60 and 350, aos_ui.c) and the
 * crosses have to be measured inside that span. */
#define CAL_TOP_Y      96
#define CAL_BOTTOM_Y   350

static const lv_point_t CAL_OBJETIVO[CAL_PUNTOS] = {
    { 55, CAL_TOP_Y },    { AOS_SCREEN_W - 55, CAL_TOP_Y },
    { 55, CAL_BOTTOM_Y }, { AOS_SCREEN_W - 55, CAL_BOTTOM_Y },
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
    int32_t fx, fy;
    aos_ui_touch_map(x, y, &fx, &fy);
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

/* The register probe, refreshed on a timer and not on touch events: with two
 * fingers down LVGL may see nothing new at all, and that is precisely the
 * case to watch.
 *
 * Measured on 2026-09-24, first session: the CST820 DOES report a second
 * point, at 0x07..0x0A (XH XL YH YL, same format as point 1), while the
 * finger count at 0x02 stays at 1. With one finger 0x07..0x0A read 00 and
 * 0x0B..0x0E read FF. The first version of this view read point 2 at
 * 0x09..0x0C (the FocalTech 6-byte stride) and got nonsense.
 *
 * Every read that changes while a finger is down goes to /api/log, compact,
 * so a pinch of a few seconds fits in the 16 KB ring. */
static void probe_tick(lv_timer_t *t)
{
    (void)t;
    uint8_t r[AOS_TOUCH_REGS];
    uint32_t seq = aos_hal_touch_regs(r);
    if (!s_set.probe_label) {
        return;
    }
    if (seq == 0) {
        lv_label_set_text(s_set.probe_label, "probe: n/a (no CST820)");
        return;
    }
    if (seq == s_set.probe_seq) {
        return;
    }
    uint32_t now = lv_tick_get();
    s_set.probe_reads += seq - s_set.probe_seq;
    /* The chip's own refresh rate: new samples per second of touching.
     * aos_hal_touch_regs' counter only moves when the registers change, so
     * with a finger moving it IS the chip's rate, not ours. */
    if (s_set.probe_seq && (r[2] & 0x0F) && (s_set.probe_last[2] & 0x0F) &&
        now - s_set.rate_last_ms < 500) {
        s_set.rate_ms += now - s_set.rate_last_ms;
        s_set.rate_n  += seq - s_set.probe_seq;
    }
    s_set.rate_last_ms = now;
    s_set.probe_seq = seq;

    uint8_t fingers = r[2] & 0x0F;
    int32_t x1 = (r[3] & 0x0F) << 8 | r[4], y1 = (r[5] & 0x0F) << 8 | r[6];
    int32_t x2 = (r[7] & 0x0F) << 8 | r[8], y2 = (r[9] & 0x0F) << 8 | r[10];
    bool p2 = r[7] || r[8] || r[9] || r[10];

    if (fingers > s_set.probe_max_fingers) s_set.probe_max_fingers = fingers;
    if (fingers >= 2) s_set.probe_two++;
    if (p2)           s_set.probe_p2++;

    int32_t dist = 0;
    if (p2) {
        int32_t dx = x2 - x1, dy = y2 - y1;
        dist = (int32_t)(sqrtf((float)(dx * dx + dy * dy)) + 0.5f);
    }

    if (s_set.probe_dot2) {
        if (p2) {
            int32_t fx, fy;
            aos_ui_touch_map(x2, y2, &fx, &fy);
            lv_obj_set_pos(s_set.probe_dot2, fx - 9, fy - 9);
            lv_obj_remove_flag(s_set.probe_dot2, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_set.probe_dot2, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* The raw view's own dot follows LVGL's events, which may not fire while
     * two fingers are down: keep it on point 1 from here too. */
    if (s_set.raw_dot && fingers) {
        int32_t fx, fy;
        aos_ui_touch_map(x1, y1, &fx, &fy);
        lv_obj_set_pos(s_set.raw_dot, fx - 7, fy - 7);
    }

    char hex[AOS_TOUCH_REGS * 3 + 1];
    for (int i = 0; i < AOS_TOUCH_REGS; i++) {
        snprintf(hex + i * 3, 4, "%02X ", r[i]);
    }
    lv_label_set_text_fmt(s_set.probe_label,
                          "g=%02X  dedos=%u (max %u)\n"
                          "P1 %d,%d   P2 %d,%d\n"
                          "d = %d\n"
                          "%.24s\n%s\n"
                          "con P2: %u de %u   chip %u Hz",
                          r[1], fingers, s_set.probe_max_fingers,
                          (int)x1, (int)y1, (int)x2, (int)y2, (int)dist,
                          hex, hex + 24,
                          (unsigned)s_set.probe_p2, (unsigned)s_set.probe_reads,
                          (unsigned)(s_set.rate_ms ? s_set.rate_n * 1000u / s_set.rate_ms : 0));

    bool changed = memcmp(r, s_set.probe_last, sizeof(r)) != 0;
    bool worth   = fingers || p2 || (s_set.probe_last[2] & 0x0F) ||
                   s_set.probe_last[7] || s_set.probe_last[8] ||
                   s_set.probe_last[9] || s_set.probe_last[10];
    if (changed && worth) {
        aos_hal_log("touch", "tp %u %d,%d %d,%d d%d g%02X %02X%02X%02X%02X",
                    fingers, (int)x1, (int)y1, (int)x2, (int)y2, (int)dist,
                    r[1], r[11], r[12], r[13], r[14]);
        memcpy(s_set.probe_last, r, sizeof(r));
    }
}

static void raw_close(void)
{
    if (!s_set.raw_box) {
        return;
    }
    if (s_set.probe_timer) {
        lv_timer_delete(s_set.probe_timer);
        s_set.probe_timer = NULL;
    }
    if (s_set.probe_seq) {
        aos_hal_log("touch", "probe summary: %u samples, max fingers %u, "
                    "%u with fingers>=2, %u with a point 2 (0x07..0x0A), "
                    "chip rate %u Hz over %u ms of touching",
                    (unsigned)s_set.probe_reads, s_set.probe_max_fingers,
                    (unsigned)s_set.probe_two, (unsigned)s_set.probe_p2,
                    (unsigned)(s_set.rate_ms ? s_set.rate_n * 1000u / s_set.rate_ms : 0),
                    (unsigned)s_set.rate_ms);
    }
    s_set.probe_label = NULL;
    s_set.probe_dot2  = NULL;
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
    s_set.probe_seq = s_set.probe_reads = s_set.probe_two = s_set.probe_p2 = 0;
    s_set.probe_max_fingers = 0;
    s_set.rate_ms = s_set.rate_n = s_set.rate_last_ms = 0;
    memset(s_set.probe_last, 0, sizeof(s_set.probe_last));

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.raw_box = box;
    lv_obj_remove_style_all(box);
    /* A dot pushed against the edge (14-18 px wide, centred on the finger)
     * sticks out of the box, the box then has content to scroll, and the
     * sweep drags the whole view sideways. It never altered the numbers -
     * those are the chip's registers- but it repainted the screen on every
     * move and moved the dots off the fingers. */
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
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
    lv_obj_align(s_set.raw_label, LV_ALIGN_CENTER, 0, -5);
    aos_make_decorative(s_set.raw_label);
    raw_refresh(0, 0);

    /* Point 2, in another colour, only if the chip ever fills 0x09..0x0C. */
    s_set.probe_dot2 = lv_obj_create(box);
    lv_obj_remove_style_all(s_set.probe_dot2);
    lv_obj_set_size(s_set.probe_dot2, 18, 18);
    lv_obj_set_style_radius(s_set.probe_dot2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_set.probe_dot2, lv_color_hex(0xFF3B6B), 0);
    lv_obj_set_style_bg_opa(s_set.probe_dot2, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_set.probe_dot2, LV_OBJ_FLAG_HIDDEN);
    aos_make_decorative(s_set.probe_dot2);

    s_set.probe_label = aos_label(box, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_style_text_align(s_set.probe_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_set.probe_label, LV_ALIGN_TOP_MID, 0, 30);
    aos_make_decorative(s_set.probe_label);
    s_set.probe_timer = lv_timer_create(probe_tick, 30, NULL);
    probe_tick(NULL);

    /* Closes from the centre, the one place a sweep along the edges never
     * crosses. The physical button closes it too (back()). */
    lv_obj_t *btn = aos_button(box, _("Listo"), AOS_C_CARD2, raw_close_cb, NULL);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 95);
}

/* Held, not tapped: a restart is one brush of a finger away from the bottom
 * of a scrolling page. A tap says how. */
static void reboot_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) {
        aos_hal_reboot();
    } else {
        aos_ui_toast(_("Mantene apretado para reiniciar"), 1600);
    }
}

/* Languages found on the card. Surveyed when Settings opens and kept, because
 * the dropdown's callback only receives an index. */
static aos_lang_t s_langs[AOS_LANG_MAX];
static int        s_lang_count;

static void lang_cb(lv_event_t *event)
{
    int sel = (int)(intptr_t)lv_event_get_user_data(event);
    if (sel >= s_lang_count) {
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
/* USB (docs/USB.md)                                                           */
/* -------------------------------------------------------------------------- */

static void usb_checks(int mode)
{
    for (int i = 0; i < 4; i++) {
        if (s_set.usb_check[i]) {
            if (i == mode) {
                lv_obj_remove_flag(s_set.usb_check[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_set.usb_check[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* The rows' order IS aos_hal_usb_mode_t's order: the row's index rides in the
 * event's user data. */
static void usb_mode_cb(lv_event_t *event)
{
    int sel = (int)(intptr_t)lv_event_get_user_data(event);
    if ((aos_hal_usb_mode_t)sel == aos_hal_usb_mode()) {
        return;
    }
    if (!aos_hal_usb_mode_set((aos_hal_usb_mode_t)sel)) {
        aos_ui_toast(_("El USB esta cambiando de modo"), 1400);
        sel = (int)aos_hal_usb_mode();
    }
    usb_checks(sel);
}

static void usb_refresh(void)
{
    if (!s_set.usb_label) {
        return;
    }
    const char *txt;
    if (aos_hal_usb_busy()) {
        txt = _("cambiando...");
    } else {
        switch (aos_hal_usb_mode()) {
        case AOS_HAL_USB_KEYS:
            /* The address on a line of its own: wrapped, it broke after
             * "http:" and looked like two addresses. */
            txt = aos_hal_usb_keys_ready() ? _("teclado y red listos\n192.168.7.1")
                                           : _("esperando a la computadora");
            break;
        case AOS_HAL_USB_DISK:
            txt = aos_hal_usb_card_away() ? _("la computadora tiene la tarjeta")
                                          : _("la tarjeta volvio al reloj");
            break;
        case AOS_HAL_USB_HOST:
            txt = _("esperando un pendrive");
            break;
        default:
            txt = _("consola y grabacion del firmware");
            break;
        }
        usb_checks((int)aos_hal_usb_mode());
    }
    lv_label_set_text(s_set.usb_label, txt);
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

/* The pairing screen goes with Bluetooth: switched off, there is nothing
 * left for it to wait for. */
static void set_bt(bool on)
{
    aos_quick_set_bt(on);
    if (!on) {
        bt_box_close();
    }
}

static void bt_toggle_cb(lv_event_t *event)
{
    set_bt(lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
}

static void bt_forget_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_bt_forget();
    bt_box_close();
    aos_ui_toast(_("Telefono olvidado"), 1600);
}

static void dnd_cb(lv_event_t *event)
{
    aos_quick_set_dnd(lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
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

/* ==========================================================================
 * The pages
 * ========================================================================== */

#define PAD_SIDE    18
#define CONTENT_W   (AOS_SCREEN_W - 2 * PAD_SIDE)
#define ROW_H       58
#define SUB_ANIM_MS 220

/* The categories, in the order of the first page. Their index is also the
 * index of s_set.value[]. */
typedef enum {
    SUB_WIFI, SUB_BT, SUB_USB,
    SUB_DISPLAY, SUB_SOUND, SUB_NOTIF, SUB_MENU, SUB_TIME, SUB_LANG,
    SUB_ENERGY, SUB_TOUCH, SUB_ABOUT,
    SUB_DIAG,                   /* reached from About, not from the first page */
    SUB_COUNT
} sub_t;


static void open_sub(int kind, bool animate);
static void close_sub(bool animate);
static void refresh(lv_timer_t *timer);
static void raise_wake_cb(lv_event_t *event);
static void dnd_values(void);
static void dnd_sched_cb(lv_event_t *event);
static void dnd_time_cb(lv_event_t *event);

static const char *sub_title(int kind)
{
    switch (kind) {
    case SUB_WIFI:    return _("Wifi");
    case SUB_BT:      return _("Bluetooth");
    case SUB_USB:     return _("USB");
    case SUB_DISPLAY: return _("Pantalla");
    case SUB_SOUND:   return _("Sonido");
    case SUB_NOTIF:   return _("Notificaciones");
    case SUB_MENU:    return _("Menu");
    case SUB_TIME:    return _("Hora");
    case SUB_LANG:    return _("Idioma");
    case SUB_ENERGY:  return _("Batería");
    case SUB_TOUCH:   return _("Tactil");
    case SUB_ABOUT:   return _("Acerca del reloj");
    case SUB_DIAG:    return _("Diagnóstico");
    default:          return "";
    }
}

/* --------------------------------------------------------------------------
 * Building blocks
 * -------------------------------------------------------------------------- */

/* A page: full size, black, scrolling down, content centred in a column. */
static lv_obj_t *column(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(p, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(p, AOS_C_TEXT, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(p, PAD_SIDE, 0);
    lv_obj_set_style_pad_top(p, 4, 0);
    lv_obj_set_style_pad_bottom(p, 44, 0);
    lv_obj_set_style_pad_row(p, 10, 0);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    return p;
}

static lv_obj_t *glyph(lv_obj_t *parent, const char *g, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, g);
    lv_obj_set_style_text_font(l, &aos_settings_font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* A small grey caption over a card, left-aligned with the card's text. */
static lv_obj_t *caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = aos_label(parent, text, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(l, CONTENT_W - 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_top(l, 6, 0);
    return l;
}

/* Grey explanatory text under a card. */
static lv_obj_t *note(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = aos_label(parent, text, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(l, CONTENT_W - 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return l;
}

/* A rounded card that stacks rows. No clip_corner: the rows are transparent,
 * so there is nothing to clip, and clipping would cost a layer. */
static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_width(c, CONTENT_W);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 18, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* One row of a card: a hairline above every row but the first. */
static lv_obj_t *row_base(lv_obj_t *c)
{
    lv_obj_t *r = lv_obj_create(c);
    lv_obj_remove_style_all(r);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(r, ROW_H, 0);
    lv_obj_set_style_pad_hor(r, 14, 0);
    lv_obj_set_style_pad_ver(r, 8, 0);
    lv_obj_set_style_pad_column(r, 10, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (lv_obj_get_child_count(c) > 1) {
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_set_style_border_color(r, AOS_C_CARD2, 0);
    }
    return r;
}

static void make_touchable(lv_obj_t *row, lv_event_cb_t cb, void *user_data,
                           lv_event_code_t code)
{
    aos_make_decorative(row);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, AOS_C_CARD2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, cb, code, user_data);
}

/* A category row: coloured dot with the icon, the name, the current value,
 * a chevron. */
static lv_obj_t *nav_row(lv_obj_t *c, const char *g, lv_color_t color,
                         const char *text, lv_obj_t **value_out,
                         lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *r = row_base(c);
    if (g) {
        lv_obj_t *dot = lv_obj_create(r);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 36, 36);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, color, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_center(glyph(dot, g, AOS_C_TEXT));
    }
    lv_obj_t *name = aos_label(r, text, aos_font_body, AOS_C_TEXT);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);

    lv_obj_t *val = aos_label(r, "", aos_font_small, AOS_C_DIM);
    lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_DOTS);
    /* The name wins: a long value ("searching for the phone") is what gets
     * the dots, never "Bluetooth". */
    lv_obj_set_style_max_width(val, 112, 0);
    if (value_out) {
        *value_out = val;
    }
    glyph(r, AOS_SG_CHEVRON_RIGHT, lv_color_hex(0x636366));
    make_touchable(r, cb, user_data, LV_EVENT_CLICKED);
    return r;
}

/* A tap anywhere on a switch row flips the switch: the switch alone is a
 * small target on a watch. */
static void switch_row_click_cb(lv_event_t *event)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_user_data(event);
    if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, NULL);
}

/* A switch row, with an optional line of explanation under the name. */
static lv_obj_t *switch_row2(lv_obj_t *c, const char *text, const char *desc,
                             bool on, lv_event_cb_t cb)
{
    lv_obj_t *r = row_base(c);
    lv_obj_t *col = lv_obj_create(r);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *name = aos_label(col, text, aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(name, lv_pct(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_WRAP);
    if (desc) {
        lv_obj_t *d = aos_label(col, desc, aos_font_small, AOS_C_DIM);
        lv_obj_set_width(d, lv_pct(100));
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
    }

    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 52, 30);
    lv_obj_set_style_bg_color(sw, AOS_C_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);

    aos_make_decorative(col);
    lv_obj_add_event_cb(r, switch_row_click_cb, LV_EVENT_CLICKED, sw);
    return sw;
}

/* A choice among several, as rows with a tick on the chosen one. Returns the
 * tick, hidden unless chosen. */
static lv_obj_t *radio_row(lv_obj_t *c, const char *text, bool chosen,
                           lv_event_cb_t cb, int index)
{
    lv_obj_t *r = row_base(c);
    lv_obj_t *name = aos_label(r, text, aos_font_body, AOS_C_TEXT);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);
    lv_obj_t *tick = aos_label(r, LV_SYMBOL_OK, aos_font_body, AOS_C_ACCENT);
    if (!chosen) {
        lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);
    }
    make_touchable(r, cb, (void *)(intptr_t)index, LV_EVENT_CLICKED);
    return tick;
}

/* A segmented control. The callback gets the chosen index; it is kept in the
 * container's user data so the one click handler serves every control. */
typedef void (*seg_cb_t)(int index);

static void seg_paint(lv_obj_t *cont, int sel)
{
    uint32_t n = lv_obj_get_child_count(cont);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *b = lv_obj_get_child(cont, i);
        lv_obj_set_style_bg_opa(b, (int)i == sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(b, (int)i == sel ? AOS_C_TEXT : AOS_C_DIM, 0);
    }
}

static void seg_click_cb(lv_event_t *event)
{
    lv_obj_t *b = lv_event_get_current_target(event);
    lv_obj_t *cont = lv_obj_get_parent(b);
    int idx = (int)(intptr_t)lv_event_get_user_data(event);
    seg_paint(cont, idx);
    seg_cb_t cb = (seg_cb_t)lv_obj_get_user_data(cont);
    if (cb) {
        cb(idx);
    }
}

static lv_obj_t *segmented(lv_obj_t *parent, const char *const *labels, int n,
                           int sel, seg_cb_t cb)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, CONTENT_W, 46);
    lv_obj_set_style_bg_color(cont, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, 14, 0);
    lv_obj_set_style_pad_all(cont, 3, 0);
    lv_obj_set_style_pad_column(cont, 3, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(cont, (void *)cb);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_obj_create(cont);
        lv_obj_remove_style_all(b);
        lv_obj_set_height(b, lv_pct(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 11, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x3A3A3C), 0);
        lv_obj_t *l = aos_label(b, labels[i], aos_font_small, AOS_C_DIM);
        lv_obj_center(l);
        /* aos_label pins a colour; the label must inherit the one
         * seg_paint() sets on the segment instead. */
        lv_obj_remove_local_style_prop(l, LV_STYLE_TEXT_COLOR, 0);
        aos_make_decorative(b);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, seg_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    seg_paint(cont, sel);
    return cont;
}

/* A QR to one of the portal's pages, with the address beside it. Only with
 * an address to give: on the home network, or the setup access point. */
static void portal_qr(lv_obj_t *parent, const char *path, const char *what)
{
    const char *ip = NULL;
    if (aos_hal_net_state() == AOS_NET_CONNECTED) {
        ip = aos_hal_net_ip();
    } else if (aos_hal_net_ap_active()) {
        ip = aos_hal_net_ap_ip();
    }
    lv_obj_t *c = card(parent);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_column(c, 14, 0);

    if (!ip) {
        lv_obj_t *l = aos_label(c, _("Conecta el wifi para usar el portal"),
                                aos_font_small, AOS_C_DIM);
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        return;
    }
    char url[64];
    snprintf(url, sizeof(url), "http://%s%s", ip, path);

    /* Black on white with a white margin: see the AP screen for why. The IP
     * and not <name>.local: not every phone resolves mDNS. */
    lv_obj_t *qr = lv_qrcode_create(c);
    lv_qrcode_set_size(qr, 92);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 5, 0);
    lv_qrcode_update(qr, url, strlen(url));

    lv_obj_t *col = lv_obj_create(c);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_t *t = aos_label(col, what, aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    char shown[64];
    snprintf(shown, sizeof(shown), "%s.local%s", aos_hal_device_name(), path);
    lv_obj_t *u = aos_label(col, shown, aos_font_small, AOS_C_ACCENT);
    lv_obj_set_width(u, lv_pct(100));
    lv_label_set_long_mode(u, LV_LABEL_LONG_MODE_WRAP);
}

/* --------------------------------------------------------------------------
 * The first page: each category's current value
 * -------------------------------------------------------------------------- */

static const char *style_name(aos_launcher_style_t st)
{
    return st == AOS_LAUNCHER_GRID ? _("Grilla")
         : st == AOS_LAUNCHER_HONEYCOMB ? _("Panal") : _("Lista");
}

static const char *usb_name(int mode)
{
    switch (mode) {
    case AOS_HAL_USB_KEYS: return _("Teclado y red");
    case AOS_HAL_USB_DISK: return _("Disco (la tarjeta)");
    case AOS_HAL_USB_HOST: return _("Host (un pendrive)");
    default:               return _("Consola");
    }
}

static const char *face_name(void)
{
    const char *id = aos_watchface_current();
    for (int i = 0; id && i < aos_watchface_count(); i++) {
        const aos_watchface_t *f = aos_watchface_at(i);
        if (f && f->id && strcmp(f->id, id) == 0) {
            return _(f->name);
        }
    }
    return "";
}

static void set_value(int kind, const char *text)
{
    lv_obj_t *v = s_set.value[kind];
    /* lv_label_set_text redraws even with the same text, and this runs every
     * two seconds for twelve rows. */
    if (v && strcmp(lv_label_get_text(v), text) != 0) {
        lv_label_set_text(v, text);
    }
}

static void duration(char *out, size_t len, uint32_t seconds);

static void values_refresh(void)
{
    if (!s_set.main) {
        return;
    }
    char buf[48];

    switch (aos_hal_net_state()) {
    case AOS_NET_CONNECTED:  set_value(SUB_WIFI, aos_hal_net_ssid()); break;
    case AOS_NET_CONNECTING: set_value(SUB_WIFI, _("conectando...")); break;
    default:
        set_value(SUB_WIFI, !aos_hal_net_enabled() ? _("apagada")
                          : aos_hal_net_has_credentials() ? _("sin conexion")
                                                          : _("sin red"));
        break;
    }

    switch (aos_hal_bt_state()) {
    case AOS_BT_CONNECTED:   set_value(SUB_BT, aos_hal_bt_peer()); break;
    case AOS_BT_ADVERTISING: set_value(SUB_BT, aos_hal_bt_bonded() ? _("buscando")
                                                                   : _("sin telefono")); break;
    case AOS_BT_PAIRING:     set_value(SUB_BT, _("emparejando")); break;
    default:                 set_value(SUB_BT, _("apagado")); break;
    }

    set_value(SUB_USB, usb_name((int)aos_hal_usb_mode()));
    set_value(SUB_DISPLAY, face_name());
    snprintf(buf, sizeof(buf), "%d %%", aos_hal_volume_get());
    set_value(SUB_SOUND, buf);
    {
        bool prog;
        aos_hal_notif_dnd_schedule_get(&prog, NULL, NULL);
        set_value(SUB_NOTIF, aos_hal_notif_dnd_active() ? _("No molestar")
                           : prog ? _("programado") : _("activas"));
    }
    set_value(SUB_MENU, style_name(aos_ui_launcher_get_style()));

    struct tm now;
    aos_hal_time_now(&now);
    snprintf(buf, sizeof(buf), "%02d:%02d", now.tm_hour, now.tm_min);
    set_value(SUB_TIME, buf);

    for (int i = 0; i < s_lang_count; i++) {
        if (strcmp(s_langs[i].code, aos_i18n_current()) == 0) {
            set_value(SUB_LANG, s_langs[i].name);
        }
    }

    /* The charge and, on battery, how long it has left: "66 % · 1h 35m". */
    aos_battery_t b;
    aos_power_info_t pi;
    if (aos_hal_battery_read(&b) && b.percent >= 0) {
        char left[16];
        if (b.charging) {
            snprintf(buf, sizeof(buf), "%d %%  " LV_SYMBOL_CHARGE, b.percent);
        } else if (!b.usb_present && aos_hal_power_info(&pi) && !isnan(pi.hours_left)) {
            duration(left, sizeof(left), (uint32_t)(pi.hours_left * 3600.0f));
            snprintf(buf, sizeof(buf), "%d %%  ·  %s", b.percent, left);
        } else {
            snprintf(buf, sizeof(buf), "%d %%", b.percent);
        }
        set_value(SUB_ENERGY, buf);
    }

    /* "0.5.0-3-g1234abc-dirty" says too much for a row: up to the first dash. */
    snprintf(buf, sizeof(buf), "%s", aos_hal_firmware_version());
    char *dash = strchr(buf + 1, '-');
    if (dash) {
        *dash = '\0';
    }
    set_value(SUB_ABOUT, buf);
}

static void open_sub_cb(lv_event_t *event)
{
    aos_hal_activity();
    open_sub((int)(intptr_t)lv_event_get_user_data(event), true);
}

static void brightness_value_cb(lv_event_t *event)
{
    brightness_cb(event);
}

static void build_main(lv_obj_t *root)
{
    lv_obj_t *p = column(root);
    s_set.main = p;

    lv_obj_t *title = aos_label(p, _("Ajustes"), aos_font_title, AOS_C_TEXT);
    lv_obj_set_width(title, CONTENT_W - 8);

    s_set.tiles = aos_quick_tiles_create(p, CONTENT_W, 84);

    aos_quick_slider(p, AOS_SG_WHITE_BALANCE_SUNNY, aos_hal_brightness_get(), 5, 100, CONTENT_W, 48, brightness_value_cb);

    static const struct {
        int kind;
        const char *g;
        uint32_t color;
    } ROWS[] = {
        { SUB_WIFI,    AOS_SG_WIFI,                  0x0A84FF },
        { SUB_BT,      AOS_SG_BLUETOOTH,             0x0A84FF },
        { SUB_USB,     AOS_SG_USB,                   0x636366 },
        { SUB_DISPLAY, AOS_SG_BRIGHTNESS_6,          0x0A84FF },
        { SUB_SOUND,   AOS_SG_VOLUME_HIGH,           0xFF375F },
        { SUB_NOTIF,   AOS_SG_BELL_OUTLINE,          0xFF453A },
        { SUB_MENU,    AOS_SG_VIEW_GRID_OUTLINE,     0xBF5AF2 },
        { SUB_TIME,    AOS_SG_CLOCK_OUTLINE,         0xFF9F0A },
        { SUB_LANG,    AOS_SG_TRANSLATE,             0x5E5CE6 },
        { SUB_ENERGY,  AOS_SG_BATTERY_HEART_VARIANT, 0x30D158 },
        { SUB_TOUCH,   AOS_SG_GESTURE_TAP,           0x40C8E0 },
        { SUB_ABOUT,   AOS_SG_INFORMATION_OUTLINE,   0x636366 },
    };
    lv_obj_t *c = NULL;
    for (size_t i = 0; i < sizeof(ROWS) / sizeof(ROWS[0]); i++) {
        int k = ROWS[i].kind;
        if (k == SUB_WIFI || k == SUB_DISPLAY || k == SUB_ENERGY) {
            caption(p, k == SUB_WIFI ? _("CONEXIONES")
                     : k == SUB_DISPLAY ? _("RELOJ") : _("SISTEMA"));
            c = card(p);
        }
        nav_row(c, ROWS[i].g, lv_color_hex(ROWS[i].color), sub_title(k),
                &s_set.value[k], open_sub_cb, (void *)(intptr_t)k);
    }
    values_refresh();
}

/* --------------------------------------------------------------------------
 * The category pages
 * -------------------------------------------------------------------------- */

static void back_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_activity();
    close_sub(true);
}

static void build_wifi(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    switch_row2(c, _("Wifi encendida"), NULL, aos_hal_net_enabled(), wifi_toggle_cb);

    s_set.net_label = aos_label(p, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(s_set.net_label, CONTENT_W);
    lv_label_set_long_mode(s_set.net_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.net_label, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(p, _("Configurar red"), AOS_C_ACCENT, ap_cb, NULL);

    /* A touchable card with the AP's name, visible only with the network up.
     * It is the door to the second screen: that is where the password and the
     * QR are. The chevron is what says it can be touched; without it, nobody
     * tries. */
    s_set.ap_row = lv_obj_create(p);
    lv_obj_remove_style_all(s_set.ap_row);
    lv_obj_set_size(s_set.ap_row, CONTENT_W, 62);
    lv_obj_set_style_bg_color(s_set.ap_row, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(s_set.ap_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_set.ap_row, 18, 0);
    lv_obj_set_style_pad_hor(s_set.ap_row, 14, 0);
    lv_obj_remove_flag(s_set.ap_row, LV_OBJ_FLAG_SCROLLABLE);
    s_set.ap_row_ssid = aos_label(s_set.ap_row, aos_hal_net_ap_ssid(),
                                  aos_font_small, AOS_C_TEXT);
    lv_obj_align(s_set.ap_row_ssid, LV_ALIGN_LEFT_MID, 0, -10);
    lv_obj_t *hint = aos_label(s_set.ap_row, _("ver clave y QR"), aos_font_small, AOS_C_DIM);
    lv_obj_align(hint, LV_ALIGN_LEFT_MID, 0, 12);
    lv_obj_align(glyph(s_set.ap_row, AOS_SG_CHEVRON_RIGHT, AOS_C_DIM), LV_ALIGN_RIGHT_MID, 0, 0);
    /* In LVGL 9 every lv_obj is born clickable: without this the labels eat
     * the touch meant for the card. */
    aos_make_decorative(s_set.ap_row);
    lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_set.ap_row, ap_box_open, LV_EVENT_CLICKED, NULL);

    /* What to do once connected; the name and the password are on the card's
     * second screen, not in plain sight here. */
    char ap_buf[160];
    snprintf(ap_buf, sizeof(ap_buf), _("Ya conectado, abri\nhttp://%s/wifi"),
             aos_hal_net_ap_ip());
    s_set.ap_label = aos_label(p, ap_buf, aos_font_small, AOS_C_DIM);
    lv_obj_set_style_text_align(s_set.ap_label, LV_TEXT_ALIGN_CENTER, 0);
    if (!aos_hal_net_ap_active()) {
        lv_obj_add_flag(s_set.ap_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_set.ap_row, LV_OBJ_FLAG_HIDDEN);
    }

    if (aos_hal_net_has_credentials()) {
        aos_button(p, _("Olvidar red"), AOS_C_CARD2, forget_cb, NULL);
    }
}

static void build_bt(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    switch_row2(c, _("Bluetooth"), NULL, aos_hal_bt_enabled(), bt_toggle_cb);

    s_set.bt_label = aos_label(p, "", aos_font_small, AOS_C_TEXT);
    lv_obj_set_width(s_set.bt_label, CONTENT_W);
    lv_label_set_long_mode(s_set.bt_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_set.bt_label, LV_TEXT_ALIGN_CENTER, 0);

    aos_button(p, _("Emparejar telefono"), AOS_C_ACCENT, bt_pair_cb, NULL);
    s_set.bt_forget = aos_button(p, _("Olvidar telefono"), AOS_C_CARD2, bt_forget_cb, NULL);
}

static void build_usb(lv_obj_t *p)
{
    caption(p, _("QUE ES EL PUERTO USB"));
    lv_obj_t *c = card(p);
    int mode = (int)aos_hal_usb_mode();
    for (int i = 0; i < 4; i++) {
        s_set.usb_check[i] = radio_row(c, usb_name(i), i == mode, usb_mode_cb, i);
    }
    s_set.usb_label = note(p, "");
    lv_obj_set_style_text_align(s_set.usb_label, LV_TEXT_ALIGN_CENTER, 0);
    usb_refresh();
}

/* Display: the two timeouts */
static const uint32_t ACTIVE_S[] = { 15, 30, 60, 120 };
static const uint32_t AOD_S[]    = { 60, 300, 600, 0 };

static void active_seg_cb(int i)
{
    uint32_t a, d;
    aos_hal_screen_timeouts_get(&a, &d);
    aos_hal_screen_timeouts_set(ACTIVE_S[i], d);
}

static void aod_seg_cb(int i)
{
    uint32_t a, d;
    aos_hal_screen_timeouts_get(&a, &d);
    aos_hal_screen_timeouts_set(a, AOD_S[i]);
}

static void build_display(lv_obj_t *p)
{
    aos_quick_slider(p, AOS_SG_WHITE_BALANCE_SUNNY, aos_hal_brightness_get(), 5, 100, CONTENT_W, 48, brightness_value_cb);

    lv_obj_t *c = card(p);
    lv_obj_t *face_val = NULL;
    nav_row(c, NULL, AOS_C_CARD, _("Esfera"), &face_val, face_cb, NULL);
    lv_label_set_text(face_val, face_name());
    switch_row2(c, _("Siempre encendido"),
                _("la hora sigue a la vista, tenue, con la pantalla atenuada"),
                aos_hal_aod_enabled(), aod_cb);
    switch_row2(c, _("Levantar la muñeca"),
                _("girar la muñeca hacia vos enciende la pantalla"),
                aos_hal_raise_wake_enabled(), raise_wake_cb);

    caption(p, _("BRILLO ATENUADA"));
    aos_quick_slider(p, AOS_SG_BRIGHTNESS_6, aos_hal_aod_brightness_get(), 1, 40, CONTENT_W, 48, aod_brightness_cb);

    uint32_t act_s, aod_s;
    aos_hal_screen_timeouts_get(&act_s, &aod_s);
    if (act_s == 0) {
        act_s = aos_hal_aod_enabled() ? 60 : 30;    /* what "never chose" means */
    }
    int ai = 2, di = 1;
    for (int i = 0; i < 4; i++) {
        if (ACTIVE_S[i] == act_s) ai = i;
        if (AOD_S[i] == aod_s) di = i;
    }
    static const char *act_lbl[4];
    act_lbl[0] = "15 s"; act_lbl[1] = "30 s"; act_lbl[2] = "1 min"; act_lbl[3] = "2 min";
    static const char *aod_lbl[4];
    aod_lbl[0] = "1 min"; aod_lbl[1] = "5 min"; aod_lbl[2] = "10 min";
    aod_lbl[3] = _("nunca");

    caption(p, _("SE ATENUA DESPUES DE"));
    segmented(p, act_lbl, 4, ai, active_seg_cb);
    caption(p, _("ATENUADA, SE APAGA DESPUES DE"));
    segmented(p, aod_lbl, 4, di, aod_seg_cb);
    note(p, _("Sin \"Siempre encendido\" la pantalla se apaga en vez de atenuarse. "
              "Con la bateria por debajo del 15 % se apaga igual."));
}

static void raise_wake_cb(lv_event_t *event)
{
    aos_hal_raise_wake_enable(lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
}

static void build_sound(lv_obj_t *p)
{
    aos_quick_slider(p, AOS_SG_VOLUME_HIGH, aos_hal_volume_get(), 5, 100, CONTENT_W, 48, volume_cb);
    lv_obj_t *c = card(p);
    switch_row2(c, _("Sonido de los avisos"), NULL, aos_hal_notif_sound(), notif_sound_cb);
}

static void build_notif(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    switch_row2(c, _("No molestar"),
                _("los avisos quedan en la lista, pero no encienden la pantalla ni suenan"),
                !aos_hal_notif_enabled(), dnd_cb);
    switch_row2(c, _("Llamadas siempre"),
                _("una llamada entra aunque este No molestar"),
                aos_hal_notif_calls_always(), notif_calls_cb);
    caption(p, _("NO MOLESTAR PROGRAMADO"));
    bool on;
    int from, to;
    aos_hal_notif_dnd_schedule_get(&on, &from, &to);
    lv_obj_t *c3 = card(p);
    switch_row2(c3, _("Todos los días"), NULL, on, dnd_sched_cb);
    nav_row(c3, NULL, AOS_C_CARD, _("Desde"), &s_set.dnd_from_val, dnd_time_cb, (void *)0);
    nav_row(c3, NULL, AOS_C_CARD, _("Hasta"), &s_set.dnd_to_val, dnd_time_cb, (void *)1);
    dnd_values();

    lv_obj_t *c2 = card(p);
    nav_row(c2, NULL, AOS_C_CARD, _("Categorias"), NULL, cat_cb, NULL);
}

/* --------------------------------------------------------------------------
 * Do not disturb on a schedule: the two times, and a second screen with two
 * rollers to pick one (quarter hours: a minute-by-minute roller of sixty is
 * a lot of scrolling for a bedtime).
 * -------------------------------------------------------------------------- */

static void dnd_values(void)
{
    bool on;
    int from, to;
    aos_hal_notif_dnd_schedule_get(&on, &from, &to);
    char buf[16];
    if (s_set.dnd_from_val) {
        snprintf(buf, sizeof(buf), "%02d:%02d", from / 60, from % 60);
        lv_label_set_text(s_set.dnd_from_val, buf);
    }
    if (s_set.dnd_to_val) {
        snprintf(buf, sizeof(buf), "%02d:%02d", to / 60, to % 60);
        lv_label_set_text(s_set.dnd_to_val, buf);
    }
}

static void dnd_sched_cb(lv_event_t *event)
{
    bool on = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    bool was;
    int from, to;
    aos_hal_notif_dnd_schedule_get(&was, &from, &to);
    aos_hal_notif_dnd_schedule_set(on, from, to);
}

static void dnd_box_close(void)
{
    if (s_set.dnd_box) {
        lv_obj_delete_async(s_set.dnd_box);     /* its own buttons close it */
        s_set.dnd_box = NULL;
    }
}

static void dnd_box_cancel_cb(lv_event_t *event)
{
    (void)event;
    dnd_box_close();
}

static void dnd_box_save_cb(lv_event_t *event)
{
    (void)event;
    int m = (int)lv_roller_get_selected(s_set.r_dh) * 60 +
            (int)lv_roller_get_selected(s_set.r_dm) * 15;
    bool on;
    int from, to;
    aos_hal_notif_dnd_schedule_get(&on, &from, &to);
    if (s_set.dnd_which == 0) {
        from = m;
    } else {
        to = m;
    }
    aos_hal_notif_dnd_schedule_set(on, from, to);
    dnd_values();
    dnd_box_close();
}

static void dnd_time_cb(lv_event_t *event)
{
    if (s_set.dnd_box) {
        return;
    }
    s_set.dnd_which = (int)(intptr_t)lv_event_get_user_data(event);
    bool on;
    int from, to;
    aos_hal_notif_dnd_schedule_get(&on, &from, &to);
    int cur = s_set.dnd_which == 0 ? from : to;

    static char horas[24 * 3 + 1];
    char *w = horas;
    for (int i = 0; i < 24; i++) w += sprintf(w, i ? "\n%02d" : "%02d", i);

    lv_obj_t *box = lv_obj_create(lv_layer_top());
    s_set.dnd_box = box;
    lv_obj_set_size(box, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_style_bg_color(box, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 14, 0);

    aos_label(box, s_set.dnd_which == 0 ? _("No molestar desde") : _("No molestar hasta"),
              aos_font_body, AOS_C_TEXT);
    lv_obj_t *fila = lv_obj_create(box);
    lv_obj_remove_style_all(fila);
    lv_obj_set_size(fila, AOS_SCREEN_W - 30, 110);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fila, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila, 10, 0);
    s_set.r_dh = roller(fila, horas, (uint16_t)(cur / 60), 88);
    s_set.r_dm = roller(fila, "00\n15\n30\n45", (uint16_t)((cur % 60) / 15), 88);

    lv_obj_t *fila_b = lv_obj_create(box);
    lv_obj_remove_style_all(fila_b);
    lv_obj_set_size(fila_b, AOS_SCREEN_W - 30, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fila_b, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(fila_b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(fila_b, 12, 0);
    lv_obj_set_style_pad_row(fila_b, 8, 0);
    aos_button(fila_b, _("Cancelar"), AOS_C_CARD2, dnd_box_cancel_cb, NULL);
    aos_button(fila_b, _("Guardar"),  AOS_C_GREEN, dnd_box_save_cb,   NULL);
}

/* Menu: the three styles as cards with a sketch of each. */
static void style_paint(void)
{
    aos_launcher_style_t cur = aos_ui_launcher_get_style();
    for (int i = 0; i < 3; i++) {
        if (s_set.style_card[i]) {
            lv_obj_set_style_border_color(s_set.style_card[i],
                                          i == (int)cur ? AOS_C_ACCENT : AOS_C_CARD, 0);
        }
    }
}

static void style_pick_cb(lv_event_t *event)
{
    aos_ui_launcher_set_style((aos_launcher_style_t)(intptr_t)lv_event_get_user_data(event));
    style_paint();
    set_value(SUB_MENU, style_name(aos_ui_launcher_get_style()));
}

static lv_obj_t *dot(lv_obj_t *parent, int32_t d, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void build_menu(lv_obj_t *p)
{
    caption(p, _("ESTILO"));
    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, CONTENT_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    static const aos_launcher_style_t ORDER[3] = {
        AOS_LAUNCHER_LIST, AOS_LAUNCHER_GRID, AOS_LAUNCHER_HONEYCOMB
    };
    const int32_t w = (CONTENT_W - 20) / 3;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *cd = lv_obj_create(row);
        lv_obj_remove_style_all(cd);
        lv_obj_set_size(cd, w, 128);
        lv_obj_set_style_radius(cd, 18, 0);
        lv_obj_set_style_bg_color(cd, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(cd, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cd, 2, 0);
        lv_obj_remove_flag(cd, LV_OBJ_FLAG_SCROLLABLE);

        /* the sketch: a few dots placed like that style places icons */
        lv_obj_t *sk = lv_obj_create(cd);
        lv_obj_remove_style_all(sk);
        lv_obj_set_size(sk, 64, 64);
        lv_obj_align(sk, LV_ALIGN_TOP_MID, 0, 14);
        if (ORDER[i] == AOS_LAUNCHER_LIST) {
            for (int j = 0; j < 3; j++) {
                lv_obj_set_pos(dot(sk, 14, 0x0A84FF), 4, 4 + j * 22);
                lv_obj_t *bar = lv_obj_create(sk);
                lv_obj_remove_style_all(bar);
                lv_obj_set_size(bar, 34, 6);
                lv_obj_set_pos(bar, 24, 8 + j * 22);
                lv_obj_set_style_radius(bar, 3, 0);
                lv_obj_set_style_bg_color(bar, lv_color_hex(0x636366), 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            }
        } else if (ORDER[i] == AOS_LAUNCHER_GRID) {
            for (int j = 0; j < 9; j++) {
                lv_obj_set_pos(dot(sk, 16, 0x30D158), 2 + (j % 3) * 22, 2 + (j / 3) * 22);
            }
        } else {
            /* three, two in the gaps, three: the honeycomb's offset rows */
            static const uint8_t HX[8] = { 2, 24, 46, 13, 35, 2, 24, 46 };
            static const uint8_t HY[8] = { 2, 2, 2, 22, 22, 42, 42, 42 };
            for (int j = 0; j < 8; j++) {
                lv_obj_set_pos(dot(sk, 16, 0xFF9F0A), HX[j], HY[j]);
            }
        }
        lv_obj_t *l = aos_label(cd, style_name(ORDER[i]), aos_font_small, AOS_C_TEXT);
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -10);

        aos_make_decorative(cd);
        lv_obj_add_flag(cd, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cd, style_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ORDER[i]);
        s_set.style_card[ORDER[i]] = cd;
    }
    style_paint();

    caption(p, _("ORDEN Y CARPETAS"));
    portal_qr(p, "/menu", _("Se arman desde el portal: escanea el QR con el telefono"));
}

static void build_time(lv_obj_t *p)
{
    s_set.time_label = aos_label(p, "", aos_font_huge, AOS_C_TEXT);
    s_set.date_label = aos_label(p, "", aos_font_small, AOS_C_DIM);
    lv_obj_t *c = card(p);
    nav_row(c, NULL, AOS_C_CARD, _("Sincronizar ahora"), NULL, sync_cb, NULL);
    nav_row(c, NULL, AOS_C_CARD, _("Ajustar a mano"), NULL, clock_cb, NULL);
    note(p, aos_hal_time_is_valid()
                ? _("La hora llega sola por wifi y por el telefono.")
                : _("La hora todavia no se ajusto: conecta el wifi o el telefono, o ajustala a mano."));
}

static void build_lang(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    int sel = 0;
    for (int i = 0; i < s_lang_count; i++) {
        bool cur = strcmp(s_langs[i].code, aos_i18n_current()) == 0;
        if (cur) {
            sel = i;
        }
        s_set.lang_check[i] = radio_row(c, s_langs[i].name, cur, lang_cb, i);
    }
    /* With no card -or with no /lang on it- the only option is the source
     * code's Spanish. Saying so is more useful than a one-row list with no
     * explanation. */
    if (s_lang_count <= 1) {
        note(p, _("sin packs en la tarjeta"));
        return;
    }
    /* Where the current language came from. It matters: a pack on the card
     * beats the one shipped in the firmware, and finding that out by looking
     * at the screen is far quicker than deducing it when a translation you
     * swear you fixed keeps showing up wrong. */
    char base[64], cov[96];
    snprintf(base, sizeof(base), _("%d cadenas, %d apps cubiertas"),
             aos_i18n_count(), s_langs[sel].apps);
    snprintf(cov, sizeof(cov), "%s\n%s", base,
             s_langs[sel].origin == AOS_LANG_EMBEDDED ? C_("origen del idioma", "firmware")
                                                      : C_("origen del idioma", "tarjeta"));
    lv_obj_t *l = note(p, aos_i18n_count() ? cov : _("espanol del codigo fuente"));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

/* --------------------------------------------------------------------------
 * Battery and Diagnostics (v0.5.1)
 *
 * Battery used to be an app of its own, with an arc and a block of text.
 * Now it is this page: the charge up top, the last 24 hours as a graph, the
 * details, and the switches that stretch it. Diagnostics gained bars and
 * graphs for memory, the two cores and the three thermometers. The numbers
 * come from the HAL (aos_stats.c), which samples on its own: nothing here
 * measures anything, so an open page costs only its drawing.
 * -------------------------------------------------------------------------- */

/* "3h 20m" or "45m" */
static void duration(char *out, size_t len, uint32_t seconds)
{
    uint32_t minutes = seconds / 60;
    if (minutes >= 60) {
        snprintf(out, len, "%uh %02um", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
    } else {
        snprintf(out, len, "%um", (unsigned)minutes);
    }
}

static const char *charge_state_text(aos_charge_state_t state)
{
    switch (state) {
    case AOS_CHG_TRICKLE:   return _("goteo");
    case AOS_CHG_PRECHARGE: return _("precarga");
    case AOS_CHG_CC:        return _("corriente constante");
    case AOS_CHG_CV:        return _("tensión constante");
    case AOS_CHG_DONE:      return _("carga completa");
    default:                return _("en espera");
    }
}

static lv_color_t batt_color(int pct, bool charging)
{
    return charging ? AOS_C_GREEN : pct <= 15 ? AOS_C_RED : AOS_C_TEAL;
}

static lv_obj_t *bar_new(lv_obj_t *parent, int32_t h, lv_color_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, lv_pct(100), h);
    lv_bar_set_range(bar, 0, 1000);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    aos_make_decorative(bar);
    return bar;
}

/* Only when it changed: setting a value invalidates the bar even if equal. */
static void bar_to(lv_obj_t *bar, uint64_t part, uint64_t whole)
{
    int32_t v = whole ? (int32_t)(part * 1000 / whole) : 0;
    if (lv_bar_get_value(bar) != v) {
        lv_bar_set_value(bar, v, LV_ANIM_OFF);
    }
}

static void text_to(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) != 0) {
        lv_label_set_text(l, text);
    }
}

/* A card padded for its own layout rather than for rows. */
static lv_obj_t *padded_card(lv_obj_t *parent, int32_t gap)
{
    lv_obj_t *c = card(parent);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, gap, 0);
    return c;
}

/* Name on the left, value on the right, in one line; returns the value. */
static lv_obj_t *pair(lv_obj_t *parent, const char *name, lv_color_t name_color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    /* One line: the name gets the dots when a translation runs long. */
    lv_obj_t *n = aos_label(r, name, aos_font_small, name_color);
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(n, lv_font_get_line_height(aos_font_small));
    lv_obj_set_flex_grow(n, 1);
    lv_obj_t *v = aos_label(r, "", aos_font_small, AOS_C_TEXT);
    aos_make_decorative(r);
    return v;
}

/* A bare line chart: no points, no border, faint horizontal guides. */
static lv_obj_t *chart_new(lv_obj_t *parent, int32_t h, uint32_t points)
{
    lv_obj_t *ch = lv_chart_create(parent);
    lv_obj_set_size(ch, lv_pct(100), h);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, points);
    lv_chart_set_div_line_count(ch, 3, 0);
    lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_pad_all(ch, 0, 0);
    lv_obj_set_style_line_color(ch, AOS_C_CARD2, LV_PART_MAIN);
    lv_obj_set_style_line_width(ch, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(ch, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(ch, 0, LV_PART_INDICATOR);
    aos_make_decorative(ch);
    return ch;
}

/* The time axis under a chart: oldest, middle, now. */
static void chart_axis(lv_obj_t *parent, const char *a, const char *b, const char *c)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    aos_label(r, a, aos_font_small, lv_color_hex(0x636366));
    aos_label(r, b, aos_font_small, lv_color_hex(0x636366));
    aos_label(r, c, aos_font_small, lv_color_hex(0x636366));
    aos_make_decorative(r);
}

/* Said over an empty chart: a line needs two points, and a history that
 * has just started has fewer. On the card's colour, so the guides do not
 * run through the text. */
static lv_obj_t *chart_hint(lv_obj_t *chart, const char *text)
{
    lv_obj_t *l = aos_label(chart, text, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(l, lv_pct(90));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(l, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_ver(l, 4, 0);
    lv_obj_center(l);
    return l;
}

static void hint_show(lv_obj_t *hint, bool show)
{
    if (show) {
        lv_obj_remove_flag(hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- Battery ------------------------------------------------------------ */

/* 288 five-minute samples drawn as 144: one per two pixels is all the
 * width there is, and half the segments is half the drawing on a scroll. */
#define BATT_POINTS (AOS_BATT_HIST_LEN / 2)

static void batt_chart_fill(void)
{
    stats_ui_t *st = &s_set.st;
    static uint8_t pct[AOS_BATT_HIST_LEN], flags[AOS_BATT_HIST_LEN];
    int n = aos_hal_batt_history(pct, flags, AOS_BATT_HIST_LEN);
    int prev_kind = -1;      /* 0 discharging, 1 charging */
    int valid = 0;
    for (int i = 0; i < BATT_POINTS; i++) {
        int a = 2 * i, b = 2 * i + 1;
        int v = -1, chg = 0;
        if (b < n && pct[b] != AOS_BATT_HIST_NONE) {
            v = pct[b];
            chg = flags[b] & AOS_BATT_HIST_CHARGING;
        } else if (a < n && pct[a] != AOS_BATT_HIST_NONE) {
            v = pct[a];
            chg = flags[a] & AOS_BATT_HIST_CHARGING;
        }
        int32_t none = LV_CHART_POINT_NONE;
        lv_chart_set_series_value_by_id(st->batt_chart, st->dis, i, v >= 0 && !chg ? v : none);
        lv_chart_set_series_value_by_id(st->batt_chart, st->chg, i, v >= 0 && chg ? v : none);
        /* Where it switches, the new colour starts from the old one's last
         * point, so the line does not break at the plug. */
        int kind = v < 0 ? -1 : chg ? 1 : 0;
        valid += kind >= 0;
        if (i > 0 && kind >= 0 && prev_kind >= 0 && kind != prev_kind) {
            lv_chart_series_t *s = kind ? st->chg : st->dis;
            lv_chart_set_series_value_by_id(st->batt_chart, s, i - 1,
                lv_chart_get_series_y_array(st->batt_chart, kind ? st->dis : st->chg)[i - 1]);
        }
        prev_kind = kind;
    }
    hint_show(st->batt_empty, valid < 2);
    lv_chart_refresh(st->batt_chart);
}

static void energy_refresh(void)
{
    stats_ui_t *st = &s_set.st;
    if (!st->pct) {
        return;
    }
    aos_battery_t b;
    aos_power_info_t pi;
    bool have_b = aos_hal_battery_read(&b) && b.percent >= 0;
    bool have_pi = aos_hal_power_info(&pi);
    char buf[96], t[24];

    if (!have_b) {
        text_to(st->pct, "--");
        text_to(st->state, _("sin datos de la batería"));
        return;
    }
    snprintf(buf, sizeof(buf), "%d %%", b.percent);
    text_to(st->pct, buf);
    bar_to(st->bar, (uint64_t)b.percent, 100);
    lv_color_t col = batt_color(b.percent, b.charging);
    if (!lv_color_eq(lv_obj_get_style_bg_color(st->bar, LV_PART_INDICATOR), col)) {
        lv_obj_set_style_bg_color(st->bar, col, LV_PART_INDICATOR);
    }

    /* The icons go as arguments: glued to a literal, the catalogue key would
     * carry their bytes and never match. */
    if (b.charging) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %s", have_pi ? charge_state_text(pi.charge_state)
                                                             : _("cargando"));
    } else if (b.usb_present) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_USB " %s", have_pi ? charge_state_text(pi.charge_state)
                                                          : _("conectado"));
    } else if (have_pi && pi.on_battery_s > 0) {
        duration(t, sizeof(t), pi.on_battery_s);
        snprintf(buf, sizeof(buf), _("a batería hace %s"), t);
    } else {
        snprintf(buf, sizeof(buf), "%s", _("a batería"));
    }
    text_to(st->state, buf);

    if (b.charging) {
        text_to(st->left, _("cargando"));
    } else if (b.usb_present) {
        text_to(st->left, _("conectado"));
    } else if (have_pi && !isnan(pi.hours_left)) {
        duration(t, sizeof(t), (uint32_t)(pi.hours_left * 3600.0f));
        snprintf(buf, sizeof(buf), _("quedan ~%s"), t);
        text_to(st->left, buf);
    } else {
        text_to(st->left, _("midiendo..."));
    }

    snprintf(buf, sizeof(buf), "%.2f V", (double)b.voltage);
    text_to(st->volt, buf);
    if (have_pi) {
        if (isnan(pi.drain_pct_per_hour)) {
            snprintf(buf, sizeof(buf), "%s", _("midiendo..."));
        } else {
            snprintf(buf, sizeof(buf), _("%.1f %%/h"), (double)pi.drain_pct_per_hour);
        }
        text_to(st->drain, buf);
        snprintf(buf, sizeof(buf), "%d mA  ·  %.2f V", pi.charge_ma,
                 (double)pi.charge_target_mv / 1000.0);
        text_to(st->charger, buf);
        snprintf(buf, sizeof(buf), "%u", (unsigned)pi.charge_cycles);
        text_to(st->cycles, buf);
        duration(t, sizeof(t), pi.battery_minutes_total * 60);
        text_to(st->lifetime, t);
    }
}

static void build_energy(lv_obj_t *p)
{
    stats_ui_t *st = &s_set.st;

    /* The charge: the number, where it is going, and a bar in its colour. */
    lv_obj_t *c = padded_card(p, 10);
    lv_obj_t *top = lv_obj_create(c);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 14, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    st->pct = aos_label(top, "", aos_font_huge, AOS_C_TEXT);
    lv_obj_t *col = lv_obj_create(top);
    lv_obj_remove_style_all(col);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    st->state = aos_label(col, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_width(st->state, lv_pct(100));
    lv_label_set_long_mode(st->state, LV_LABEL_LONG_MODE_WRAP);
    st->left = aos_label(col, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(st->left, lv_pct(100));
    lv_label_set_long_mode(st->left, LV_LABEL_LONG_MODE_DOTS);
    aos_make_decorative(top);
    st->bar = bar_new(c, 10, AOS_C_TEAL);

    /* The last day, discharging in the bar's teal and charging in green. */
    caption(p, _("ÚLTIMAS 24 H"));
    lv_obj_t *g = padded_card(p, 6);
    st->batt_chart = chart_new(g, 96, BATT_POINTS);
    lv_chart_set_axis_range(st->batt_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    st->dis = lv_chart_add_series(st->batt_chart, AOS_C_TEAL, LV_CHART_AXIS_PRIMARY_Y);
    st->chg = lv_chart_add_series(st->batt_chart, AOS_C_GREEN, LV_CHART_AXIS_PRIMARY_Y);
    st->batt_empty = chart_hint(st->batt_chart, _("una muestra cada 5 minutos: se va llenando"));
    chart_axis(g, _("-24 h"), _("-12 h"), _("ahora"));
    batt_chart_fill();

    lv_obj_t *d = padded_card(p, 8);
    st->volt     = pair(d, _("Tensión"), AOS_C_DIM);
    st->drain    = pair(d, _("Consumo"), AOS_C_DIM);
    st->charger  = pair(d, _("Cargador"), AOS_C_DIM);
    st->cycles   = pair(d, _("Ciclos"), AOS_C_DIM);
    st->lifetime = pair(d, _("Uso a batería"), AOS_C_DIM);

    caption(p, _("AHORRO"));
    lv_obj_t *c2 = card(p);
    switch_row2(c2, _("Ahorro de energia"),
                _("CPU a 80 MHz y wifi dormida con la pantalla apagada. Se prende sola bajo el 20 %"),
                aos_hal_power_saving_enabled(), power_saving_cb);
    switch_row2(c2, _("Cuidar la bateria"),
                _("carga hasta 4,1 V y a media corriente: rinde menos por carga y dura mas anos"),
                aos_hal_battery_care_enabled(), battery_care_cb);
    switch_row2(c2, _("Apagar el panel a fondo"),
                _("la pantalla en reposo profundo al apagarse; despierta en una decima"),
                aos_hal_panel_sleep_enabled(), panel_sleep_cb);
    switch_row2(c2, _("Dormir el chip"),
                _("con la pantalla apagada el procesador duerme entre avisos"),
                aos_hal_light_sleep_enabled(), light_sleep_cb);

    st->charts_at_min = (uint32_t)(aos_hal_uptime_ms() / 60000);
    energy_refresh();
}

static void build_touch(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    nav_row(c, NULL, AOS_C_CARD, _("Calibrar"), NULL, cal_cb, NULL);
    nav_row(c, NULL, AOS_C_CARD, _("Ver crudo"), NULL, raw_cb, NULL);
    note(p, _("Calibrar pide tocar cinco cruces. \"Ver crudo\" muestra lo que lee el "
              "chip tactil, sin correccion."));
}

static void build_about(lv_obj_t *p)
{
    lv_obj_t *c = card(p);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_pad_row(c, 2, 0);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    aos_label(c, aos_hal_device_name(), aos_font_title, AOS_C_TEXT);
    char buf[96];
    snprintf(buf, sizeof(buf), "AmoledOS %s", aos_hal_firmware_version());
    aos_label(c, buf, aos_font_small, AOS_C_DIM);
    aos_label(c, aos_hal_board_name(), aos_font_small, AOS_C_DIM);

    uint64_t total = 0, libre = 0;
    if (aos_hal_sd_usage(&total, &libre) && total) {
        lv_obj_t *c2 = card(p);
        lv_obj_set_style_pad_all(c2, 14, 0);
        lv_obj_set_style_pad_row(c2, 6, 0);
        aos_label(c2, _("Tarjeta"), aos_font_small, AOS_C_TEXT);
        lv_obj_t *bar = lv_bar_create(c2);
        lv_obj_set_size(bar, lv_pct(100), 8);
        lv_bar_set_range(bar, 0, 1000);
        lv_bar_set_value(bar, (int32_t)((total - libre) * 1000 / total), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, AOS_C_ACCENT, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), _("%.1f de %.1f GB  ·  %d apps"),
                 (double)(total - libre) / 1e9, (double)total / 1e9, aos_ui_app_count());
        aos_label(c2, buf, aos_font_small, AOS_C_DIM);
    }

    lv_obj_t *c3 = card(p);
    nav_row(c3, AOS_SG_CHART_BOX_OUTLINE, lv_color_hex(0x636366), _("Diagnóstico"),
            NULL, open_sub_cb, (void *)(intptr_t)SUB_DIAG);

    caption(p, _("PORTAL"));
    portal_qr(p, "/", _("Todo esto y mas, desde el navegador"));

    lv_obj_t *btn = aos_button(p, _("Mantene para reiniciar"), lv_color_hex(0x3A1D1B), NULL, NULL);
    lv_obj_set_style_text_color(btn, AOS_C_RED, 0);
    lv_obj_add_event_cb(btn, reboot_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, reboot_cb, LV_EVENT_LONG_PRESSED, NULL);
}

/* ---- Diagnostics -------------------------------------------------------- */

static const uint32_t TEMP_COLOR[3] = { 0xFF9F0A, 0xFFD60A, 0x40C8E0 };
static const uint32_t CPU_COLOR[2]  = { 0x0A84FF, 0xBF5AF2 };

/* "7.2 M" or "147 K" */
static void kb_text(char *out, size_t len, uint32_t bytes)
{
    if (bytes >= 1024 * 1024) {
        snprintf(out, len, "%.1f M", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, len, "%u K", (unsigned)(bytes / 1024));
    }
}

/* Fills a chart from the HAL's minute ring. With 'autoscale' the Y range
 * follows the data, with two degrees of margin, so a tenth of a degree is
 * visible; without it, it stays where it was set. */
static int minute_series(lv_obj_t *ch, lv_chart_series_t *ser, aos_hist_t which,
                         int32_t *lo, int32_t *hi)
{
    int16_t v[AOS_MIN_HIST_LEN];
    int n = aos_hal_minute_history(which, v, AOS_MIN_HIST_LEN);
    int valid = 0;
    for (int i = 0; i < n; i++) {
        if (v[i] == AOS_HIST_NONE) {
            lv_chart_set_series_value_by_id(ch, ser, i, LV_CHART_POINT_NONE);
            continue;
        }
        lv_chart_set_series_value_by_id(ch, ser, i, v[i]);
        valid++;
        if (lo && v[i] < *lo) *lo = v[i];
        if (hi && v[i] > *hi) *hi = v[i];
    }
    return valid;
}

static void diag_charts_fill(void)
{
    stats_ui_t *st = &s_set.st;
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    int valid = 0;
    for (int k = 0; k < 3; k++) {
        int v = minute_series(st->temp_chart, st->temp_ser[k],
                              (aos_hist_t)(AOS_HIST_CHIP_T + k), &lo, &hi);
        valid = v > valid ? v : valid;
    }
    hint_show(st->temp_empty, valid < 2);
    if (lo > hi) {
        lo = 200; hi = 500;
    }
    lv_chart_set_axis_range(st->temp_chart, LV_CHART_AXIS_PRIMARY_Y,
                            lo / 10 * 10 - 20, (hi + 9) / 10 * 10 + 20);
    lv_chart_refresh(st->temp_chart);
    valid = 0;
    for (int k = 0; k < 2; k++) {
        int v = minute_series(st->cpu_chart, st->cpu_ser[k],
                              (aos_hist_t)(AOS_HIST_CPU0 + k), NULL, NULL);
        valid = v > valid ? v : valid;
    }
    hint_show(st->cpu_empty, valid < 2);
    lv_chart_refresh(st->cpu_chart);
}

static void diag_refresh(void)
{
    stats_ui_t *st = &s_set.st;
    if (!st->mhz) {
        return;
    }
    aos_sys_stats_t s;
    char buf[96], a[16];
    if (!aos_hal_sys_stats(&s)) {
        return;
    }

    /* "147 K libres" on the line, the total is what the bar is out of. */
    const uint32_t fr[3] = { s.int_free, s.psram_free, s.exec_free };
    const uint32_t to[3] = { s.int_total, s.psram_total, s.exec_total };
    for (int k = 0; k < 3; k++) {
        kb_text(a, sizeof(a), fr[k]);
        snprintf(buf, sizeof(buf), _("%s libres"), a);
        text_to(st->mem_val[k], buf);
        bar_to(st->mem_bar[k], to[k] - fr[k], to[k]);
    }

    /* The largest hole next to the total: running out of memory and running
     * out of a hole big enough are different problems. */
    char apps[64];
#ifndef AOS_SIM
    if (aos_dynapp_code_in_psram()) {
        snprintf(apps, sizeof(apps), _("%d apps cargadas, con el código en PSRAM"),
                 aos_dynapp_loaded_list(NULL, 0));
    } else {
        uint32_t p_libre = 0, p_mayor = 0;
        int      p_usados = 0;
        aos_dynapp_pool_info(&p_libre, &p_mayor, &p_usados);
        snprintf(apps, sizeof(apps), _("reserva apps: %u K libres (mayor %u K, %d en uso)"),
                 (unsigned)(p_libre / 1024), (unsigned)(p_mayor / 1024), p_usados);
    }
#else
    snprintf(apps, sizeof(apps), _("%d apps cargadas, con el código en PSRAM"), 0);
#endif
    kb_text(a, sizeof(a), s.exec_largest);
    char note_txt[128];
    snprintf(note_txt, sizeof(note_txt), _("bloque mayor para código: %s\n%s"), a, apps);
    text_to(st->apps, note_txt);

    aos_power_info_t pi;
    if (aos_hal_power_info(&pi)) {
        snprintf(buf, sizeof(buf), "%d MHz%s%s", pi.cpu_mhz,
                 pi.power_saving_active ? "  ·  " : "",
                 pi.power_saving_active ? _("ahorro") : "");
        text_to(st->mhz, buf);
    }
    for (int k = 0; k < 2; k++) {
        if (s.cpu_load[k] >= 0) {
            snprintf(buf, sizeof(buf), "%d %%", s.cpu_load[k]);
            text_to(st->cpu_val[k], buf);
            bar_to(st->cpu_bar[k], (uint64_t)s.cpu_load[k], 100);
        }
    }

    const float temps[3] = { s.chip_c, s.pmu_c, s.board_c };
    for (int k = 0; k < 3; k++) {
        if (isnan(temps[k])) {
            snprintf(buf, sizeof(buf), "--");
        } else {
            snprintf(buf, sizeof(buf), "%.1f °C", (double)temps[k]);
        }
        text_to(st->temp_val[k], buf);
    }

    duration(buf, sizeof(buf), (uint32_t)(aos_hal_uptime_ms() / 1000));
    text_to(st->uptime, buf);
}

static void build_diag(lv_obj_t *p)
{
    stats_ui_t *st = &s_set.st;

    caption(p, _("MEMORIA"));
    lv_obj_t *c = padded_card(p, 6);
    static const char *const MEM_NAME[3] = {
        N_("Interna"), N_("PSRAM"), N_("Código de apps"),
    };
    static const uint32_t MEM_COLOR[3] = { 0x0A84FF, 0x30D158, 0xFF9F0A };
    for (int k = 0; k < 3; k++) {
        if (k) {
            lv_obj_t *gap = lv_obj_create(c);
            lv_obj_remove_style_all(gap);
            lv_obj_set_size(gap, 1, 4);
        }
        st->mem_val[k] = pair(c, _(MEM_NAME[k]), AOS_C_TEXT);
        lv_obj_set_style_text_color(st->mem_val[k], AOS_C_DIM, 0);
        st->mem_bar[k] = bar_new(c, 8, lv_color_hex(MEM_COLOR[k]));
    }
    st->apps = note(p, "");

    caption(p, _("PROCESADOR"));
    lv_obj_t *c2 = padded_card(p, 6);
    st->mhz = pair(c2, _("Frecuencia"), AOS_C_TEXT);
    for (int k = 0; k < 2; k++) {
        char name[24];
        snprintf(name, sizeof(name), _("Núcleo %d"), k);
        st->cpu_val[k] = pair(c2, name, lv_color_hex(CPU_COLOR[k]));
        st->cpu_bar[k] = bar_new(c2, 8, lv_color_hex(CPU_COLOR[k]));
    }
    st->cpu_chart = chart_new(c2, 60, AOS_MIN_HIST_LEN);
    lv_obj_set_style_margin_top(st->cpu_chart, 6, 0);
    lv_chart_set_axis_range(st->cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    for (int k = 0; k < 2; k++) {
        st->cpu_ser[k] = lv_chart_add_series(st->cpu_chart, lv_color_hex(CPU_COLOR[k]),
                                             LV_CHART_AXIS_PRIMARY_Y);
    }
    st->cpu_empty = chart_hint(st->cpu_chart, _("una muestra por minuto: se va llenando"));
    chart_axis(c2, _("-60 min"), _("-30 min"), _("ahora"));

    caption(p, _("TEMPERATURAS"));
    lv_obj_t *c3 = padded_card(p, 6);
    static const char *const TEMP_NAME[3] = { N_("Procesador"), N_("PMU"), N_("Placa") };
    for (int k = 0; k < 3; k++) {
        st->temp_val[k] = pair(c3, _(TEMP_NAME[k]), lv_color_hex(TEMP_COLOR[k]));
    }
    st->temp_chart = chart_new(c3, 90, AOS_MIN_HIST_LEN);
    lv_obj_set_style_margin_top(st->temp_chart, 6, 0);
    for (int k = 0; k < 3; k++) {
        st->temp_ser[k] = lv_chart_add_series(st->temp_chart, lv_color_hex(TEMP_COLOR[k]),
                                              LV_CHART_AXIS_PRIMARY_Y);
    }
    st->temp_empty = chart_hint(st->temp_chart, _("una muestra por minuto: se va llenando"));
    chart_axis(c3, _("-60 min"), _("-30 min"), _("ahora"));
    diag_charts_fill();

    caption(p, _("SISTEMA"));
    lv_obj_t *c4 = padded_card(p, 8);
    st->uptime = pair(c4, _("Encendido hace"), AOS_C_DIM);
    lv_label_set_text(pair(c4, _("Arranque"), AOS_C_DIM), aos_hal_boot_reason());
    aos_power_info_t pi;
    if (aos_hal_power_info(&pi)) {
        lv_obj_t *v;
        v = pair(c4, _("PMU encendido por"), AOS_C_DIM);
        lv_label_set_text(v, pi.power_on_reason ? pi.power_on_reason : "--");
        v = pair(c4, _("Último apagado"), AOS_C_DIM);
        lv_label_set_text(v, pi.power_off_reason ? pi.power_off_reason : "--");
    }

    st->charts_at_min = (uint32_t)(aos_hal_uptime_ms() / 60000);
    diag_refresh();
}

/* --------------------------------------------------------------------------
 * Opening and closing a category page
 * -------------------------------------------------------------------------- */

/* What refresh() writes into and a category page owns: forgotten when the
 * page goes, or the timer would write into freed objects. */
static void forget_sub_pointers(void)
{
    s_set.net_label = s_set.ap_row = s_set.ap_row_ssid = s_set.ap_label = NULL;
    s_set.usb_label = s_set.bt_label = s_set.bt_forget = NULL;
    s_set.time_label = s_set.date_label = NULL;
    memset(&s_set.st, 0, sizeof(s_set.st));
    s_set.dnd_from_val = s_set.dnd_to_val = NULL;
    memset(s_set.usb_check, 0, sizeof(s_set.usb_check));
    memset(s_set.lang_check, 0, sizeof(s_set.lang_check));
    memset(s_set.style_card, 0, sizeof(s_set.style_card));
}

static void anim_x_cb(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void sub_shown_cb(lv_anim_t *a)
{
    (void)a;
    if (s_set.sub && s_set.main) {
        lv_obj_add_flag(s_set.main, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sub_gone_cb(lv_anim_t *a)
{
    lv_obj_delete((lv_obj_t *)a->var);
}

static void slide(lv_obj_t *obj, int32_t from, int32_t to, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, SUB_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

static void open_sub(int kind, bool animate)
{
    if (s_set.sub) {
        forget_sub_pointers();
        lv_obj_delete_async(s_set.sub);     /* maybe inside its own callback */
        s_set.sub = NULL;
    }
    lv_obj_t *p = column(s_set.root);
    s_set.sub = p;
    s_set.sub_kind = kind;

    /* The header: the chevron and the name, which also close the page. */
    lv_obj_t *hdr = lv_obj_create(p);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, CONTENT_W, 44);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 6, 0);
    glyph(hdr, AOS_SG_CHEVRON_LEFT, AOS_C_ACCENT);
    lv_obj_t *t = aos_label(hdr, sub_title(kind), aos_font_title, AOS_C_TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(t, 1);
    aos_make_decorative(hdr);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hdr, back_cb, LV_EVENT_CLICKED, NULL);

    uint64_t t0 = aos_hal_uptime_ms();
    switch (kind) {
    case SUB_WIFI:    build_wifi(p);    break;
    case SUB_BT:      build_bt(p);      break;
    case SUB_USB:     build_usb(p);     break;
    case SUB_DISPLAY: build_display(p); break;
    case SUB_SOUND:   build_sound(p);   break;
    case SUB_NOTIF:   build_notif(p);   break;
    case SUB_MENU:    build_menu(p);    break;
    case SUB_TIME:    build_time(p);    break;
    case SUB_LANG:    build_lang(p);    break;
    case SUB_ENERGY:  build_energy(p);  break;
    case SUB_TOUCH:   build_touch(p);   break;
    case SUB_ABOUT:   build_about(p);   break;
    case SUB_DIAG:    build_diag(p);    break;
    default: break;
    }
    aos_hal_log("settings", "page %d built in %u ms", kind,
                (unsigned)(aos_hal_uptime_ms() - t0));
    refresh(NULL);

    if (animate) {
        slide(p, AOS_SCREEN_W, 0, sub_shown_cb);
    } else if (s_set.main) {
        lv_obj_add_flag(s_set.main, LV_OBJ_FLAG_HIDDEN);
    }
}

static void close_sub(bool animate)
{
    if (!s_set.sub) {
        return;
    }
    lv_obj_t *p = s_set.sub;
    int kind = s_set.sub_kind;
    s_set.sub = NULL;
    forget_sub_pointers();

    /* Diagnostics is a page of About: back returns there, not to the top. */
    if (kind == SUB_DIAG) {
        lv_obj_delete_async(p);
        open_sub(SUB_ABOUT, false);
        return;
    }
    if (s_set.main) {
        lv_obj_remove_flag(s_set.main, LV_OBJ_FLAG_HIDDEN);
    }
    values_refresh();
    aos_quick_tiles_paint(s_set.tiles);
    if (animate) {
        lv_anim_delete(p, anim_x_cb);
        slide(p, lv_obj_get_x(p), AOS_SCREEN_W, sub_gone_cb);
    } else {
        lv_obj_delete_async(p);
    }
}

/* --------------------------------------------------------------------------
 * The two-second refresh
 * -------------------------------------------------------------------------- */

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    usb_refresh();
    aos_quick_tiles_paint(s_set.tiles);
    values_refresh();

    if (s_set.time_label) {
        struct tm now;
        aos_hal_time_now(&now);
        lv_label_set_text_fmt(s_set.time_label, "%02d:%02d", now.tm_hour, now.tm_min);
        char d[32];
        strftime(d, sizeof(d), "%d/%m/%Y", &now);
        lv_label_set_text(s_set.date_label, d);
    }

    energy_refresh();
    diag_refresh();
    /* The graphs move once a minute: redrawn then, not every two seconds. */
    uint32_t minute = (uint32_t)(aos_hal_uptime_ms() / 60000);
    if (minute != s_set.st.charts_at_min) {
        s_set.st.charts_at_min = minute;
        if (s_set.st.batt_chart) {
            batt_chart_fill();
        }
        if (s_set.st.temp_chart) {
            diag_charts_fill();
        }
    }

    if (s_set.net_label) {
        char buf[96];
        switch (aos_hal_net_state()) {
        case AOS_NET_CONNECTED:
            snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s  (%d dBm)\n%s  ·  %s.local",
                     aos_hal_net_ssid(), aos_hal_net_rssi(), aos_hal_net_ip(),
                     aos_hal_device_name());
            break;
        case AOS_NET_CONNECTING:
            /* The icon goes as an argument and not glued to the literal. Glued,
             * the string aos_tr() sees at run time starts with the glyph's
             * bytes, but gen_lang.py -which reads the source- only sees the
             * text: the catalogue key would never match, and silently so. */
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
    s_set.tiles = NULL;
    memset(s_set.value, 0, sizeof(s_set.value));
    forget_sub_pointers();
    s_set.sub = NULL;
    s_set.root = root;

    /* Surveyed once: the first page shows the current language's name and
     * the Language page lists them. */
    s_lang_count = aos_i18n_scan(s_langs, AOS_LANG_MAX);

    uint64_t t0 = aos_hal_uptime_ms();
    build_main(root);
    aos_hal_log("settings", "first page built in %u ms",
                (unsigned)(aos_hal_uptime_ms() - t0));

    s_set.timer = lv_timer_create(refresh, 2000, NULL);
    refresh(NULL);

#ifdef AOS_SIM
    /* AOS_SIM_SUB=<n> opens that category page straight away (sub_t order:
     * 0 wifi ... 11 about, 12 diagnostics), for tools/audit_layout.sh and for
     * screenshots. */
    const char *sim_sub = getenv("AOS_SIM_SUB");
    if (sim_sub && *sim_sub) {
        open_sub(atoi(sim_sub), false);
        /* AOS_SIM_SCROLL=<px> scrolls it, for screenshots of a long page. */
        const char *sim_scroll = getenv("AOS_SIM_SCROLL");
        if (sim_scroll && s_set.sub) {
            lv_obj_update_layout(s_set.sub);
            lv_obj_scroll_to_y(s_set.sub, atoi(sim_scroll), LV_ANIM_OFF);
        }
    }
#endif

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
    if (s_set.dnd_box) {
        dnd_box_close();
        return true;
    }
    if (s_set.sub) {
        close_sub(true);
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
    if (s_set.dnd_box) {
        lv_obj_delete(s_set.dnd_box);
        s_set.dnd_box = NULL;
    }
    /* The objects go with the app's root; only the pointers stay behind. */
    forget_sub_pointers();
    s_set.tiles = NULL;
    memset(s_set.value, 0, sizeof(s_set.value));
    s_set.main = NULL;
    s_set.sub  = NULL;
    s_set.root = NULL;
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
