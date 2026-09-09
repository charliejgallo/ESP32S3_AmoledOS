/* AmoledOS - pairing request overlay. See aos_pair_ui.h. */
#include "aos_pair_ui.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>

#define MARGEN  26

static lv_obj_t *s_root;
static lv_obj_t *s_codigo;
static uint32_t  s_mostrado;
static bool      s_suprimido;

static void cerrar(void)
{
    if (s_root) {
        lv_obj_delete(s_root);
        s_root = NULL;
        s_codigo = NULL;
    }
    s_mostrado = 0;
}

bool aos_pair_ui_visible(void)
{
    return s_root != NULL;
}

void aos_pair_ui_suppress(bool suprimir)
{
    s_suprimido = suprimir;
    if (suprimir) {
        cerrar();
    }
}

static void si_cb(lv_event_t *e)
{
    (void)e;
    cerrar();
    aos_hal_bt_pair_confirm(true);
}

static void no_cb(lv_event_t *e)
{
    (void)e;
    cerrar();
    aos_hal_bt_pair_confirm(false);
}

void aos_pair_ui_cancel(void)
{
    if (s_root) {
        cerrar();
        aos_hal_bt_pair_confirm(false);
    }
}

static void abrir(uint32_t codigo)
{
    /* Black and opaque background, no entry animation and no opacity on the
     * container: the same three reasons as in the notification overlay (see
     * docs/HANDOFF-BLE-ANCS.md section 14). */
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
    lv_obj_set_style_pad_row(cuerpo, 16, 0);
    lv_obj_remove_flag(cuerpo, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cab = aos_label(cuerpo, LV_SYMBOL_BLUETOOTH, aos_font_body,
                              AOS_C_ACCENT);
    (void)cab;

    lv_obj_t *titulo = aos_label(cuerpo, _("¿El telefono muestra este numero?"),
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
    snprintf(buf, sizeof(buf), "%06u", (unsigned)codigo);
    s_codigo = aos_label(card, buf, aos_font_title, AOS_C_GREEN);
    lv_obj_center(s_codigo);

    /* ROW_WRAP for the same reason as in Settings: in German the two buttons
     * do not fit on one line and without this they are drawn on top of each
     * other. */
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

    lv_obj_t *no = aos_button(fila, _("Cancelar"), AOS_C_CARD2, no_cb, NULL);
    lv_obj_t *si = aos_button(fila, _("Coincide"), AOS_C_GREEN, si_cb, NULL);

    aos_make_decorative(cuerpo);
    lv_obj_add_flag(no, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(si, LV_OBJ_FLAG_CLICKABLE);
}

void aos_pair_ui_tick(void)
{
    if (s_suprimido) {
        return;
    }

    uint32_t codigo = aos_hal_bt_pair_code();

    if (!codigo) {
        cerrar();                   /* confirmed, cancelled, or dropped */
        return;
    }
    if (s_root && codigo == s_mostrado) {
        /* While the code is on screen the display stays awake: what is being
         * asked is for the user to compare two numbers, and they cannot do
         * that if the watch dims after fifteen seconds. */
        aos_hal_activity();
        return;
    }

    cerrar();
    s_mostrado = codigo;
    aos_hal_activity();
    abrir(codigo);
    aos_hal_beep(1320, 90);
}
