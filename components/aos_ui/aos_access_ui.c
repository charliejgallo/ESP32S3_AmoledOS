/*
 * AmoledOS - "Allow this browser?", the watch's half of the portal's login
 * on a network that is not trusted (aos_access.h, docs/SECURITY.md).
 *
 * Built like the Bluetooth pairing request (aos_pair_ui.c): an overlay on
 * lv_layer_top over whatever is open, the screen kept awake while it waits,
 * back or a swipe refuse. The code is the one the browser shows: the user
 * allows the browser in front of them, not whichever asked last.
 */
#include "aos_access.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>

#define MARGEN  26

static lv_obj_t *s_root;
static uint16_t  s_mostrado;
static bool      s_init_checked;

static void cerrar(void)
{
    if (s_root) {
        lv_obj_delete(s_root);
        s_root = NULL;
    }
    s_mostrado = 0;
}

bool aos_access_ui_visible(void)
{
    return s_root != NULL;
}

static void si_cb(lv_event_t *e)
{
    (void)e;
    cerrar();
    aos_access_answer(true);
}

static void no_cb(lv_event_t *e)
{
    (void)e;
    cerrar();
    aos_access_answer(false);
}

void aos_access_ui_cancel(void)
{
    if (s_root) {
        cerrar();
        aos_access_answer(false);
    }
}

static void abrir(uint16_t codigo, const char *desde)
{
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cuerpo = lv_obj_create(s_root);
    lv_obj_remove_style_all(cuerpo);
    lv_obj_set_size(cuerpo, AOS_SCREEN_W - 2 * MARGEN, AOS_SCREEN_H - 60);
    lv_obj_align(cuerpo, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_flex_flow(cuerpo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cuerpo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cuerpo, 14, 0);
    lv_obj_remove_flag(cuerpo, LV_OBJ_FLAG_SCROLLABLE);

    aos_label(cuerpo, LV_SYMBOL_WIFI, aos_font_body, AOS_C_ACCENT);

    lv_obj_t *titulo = aos_label(cuerpo, _("¿Permitir este navegador en el portal?"),
                                 aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(titulo, lv_pct(100));
    lv_label_set_long_mode(titulo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(titulo, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *card = lv_obj_create(cuerpo);
    lv_obj_remove_style_all(card);
    lv_obj_set_width(card, AOS_SCREEN_W - 90);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    char buf[16];
    snprintf(buf, sizeof(buf), "%04u", (unsigned)codigo);
    lv_obj_center(aos_label(card, buf, aos_font_title, AOS_C_GREEN));

    char linea[96];
    snprintf(linea, sizeof(linea), _("desde %s, en %s"), desde,
             aos_hal_net_ssid()[0] ? aos_hal_net_ssid() : "?");
    lv_obj_t *info = aos_label(cuerpo, linea, aos_font_small, AOS_C_DIM);
    lv_obj_set_width(info, lv_pct(100));
    lv_label_set_long_mode(info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);

    /* ROW_WRAP: in German the two buttons do not fit on one line */
    lv_obj_t *fila = lv_obj_create(cuerpo);
    lv_obj_remove_style_all(fila);
    lv_obj_set_width(fila, AOS_SCREEN_W - 50);
    lv_obj_set_height(fila, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(fila, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(fila, 8, 0);
    lv_obj_set_style_pad_column(fila, 10, 0);
    lv_obj_remove_flag(fila, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *no = aos_button(fila, _("Rechazar"), AOS_C_CARD2, no_cb, NULL);
    lv_obj_t *si = aos_button(fila, _("Permitir"), AOS_C_GREEN, si_cb, NULL);

    aos_make_decorative(cuerpo);
    lv_obj_add_flag(no, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(si, LV_OBJ_FLAG_CLICKABLE);
}

/* The first time this firmware runs, the network the watch is on becomes
 * trusted, so the update does not close the portal on its owner. Once: the
 * flag stays even if the user later removes that network. */
static void first_run(void)
{
    if (s_init_checked) {
        return;
    }
    int32_t done = 0;
    if (aos_hal_pref_get_i32("web_init", &done) && done) {
        s_init_checked = true;
        return;
    }
    if (aos_hal_net_state() == AOS_NET_CONNECTED && aos_hal_net_ssid()[0]) {
        aos_access_set_trusted(aos_hal_net_ssid(), true);     /* sets web_init */
        aos_hal_log("access", "first run: %s is a trusted network", aos_hal_net_ssid());
        s_init_checked = true;
    }
}

void aos_access_ui_tick(void)
{
    first_run();

    uint16_t codigo = 0;
    char desde[16];
    if (!aos_access_pending(&codigo, desde, sizeof desde)) {
        cerrar();                   /* answered, or expired */
        return;
    }
    if (s_root && codigo == s_mostrado) {
        aos_hal_activity();         /* awake while it waits for the user */
        return;
    }
    cerrar();
    s_mostrado = codigo;
    aos_hal_activity();
    abrir(codigo, desde);
    aos_hal_beep(1320, 90);
}
