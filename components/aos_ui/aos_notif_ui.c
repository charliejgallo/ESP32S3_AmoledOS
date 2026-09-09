/* AmoledOS - notification overlay. See aos_notif_ui.h. */
#include "aos_notif_ui.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_text_safe.h"

#include <string.h>
#include <stdio.h>

/* How long the screen is kept awake with the notification up. After that it
 * stops kicking the timer and the watch dims as usual; on dimming, aos_ui.c
 * closes the overlay. Without that close it would hang underneath the
 * always-on face and reappear out of nowhere on the first touch. */
#define DESPIERTA_MS    20000

/* Side margin. The screen is rounded (AOS_SCREEN_RADIUS is 38 px), so text
 * against the edge gets eaten by the corners. 26 is the same one the status
 * bar uses. */
#define MARGEN          26

static lv_obj_t *s_root;
static uint32_t  s_uid;
static bool      s_can_pos, s_can_neg;
static uint32_t  s_abierta_ms;

/* -------------------------------------------------------------------------- */

static lv_color_t color_de(aos_notif_category_t c)
{
    switch (c) {
    case AOS_NOTIF_CALL_INCOMING:  return AOS_C_GREEN;
    case AOS_NOTIF_CALL_MISSED:    return AOS_C_RED;
    case AOS_NOTIF_VOICEMAIL:      return AOS_C_PURPLE;
    case AOS_NOTIF_EMAIL:          return AOS_C_TEAL;
    case AOS_NOTIF_SCHEDULE:       return AOS_C_ORANGE;
    case AOS_NOTIF_NEWS:           return AOS_C_DIM;
    case AOS_NOTIF_HEALTH:         return AOS_C_PINK;
    case AOS_NOTIF_FINANCE:        return AOS_C_YELLOW;
    case AOS_NOTIF_ENTERTAINMENT:  return AOS_C_PURPLE;
    default:                       return AOS_C_ACCENT;
    }
}

/* Glyphs from those already compiled into the fonts (the 61 of FontAwesome
 * behind LV_SYMBOL_*). There is no calendar one, so the bell doubles as a
 * wildcard. */
static const char *glifo_de(aos_notif_category_t c)
{
    switch (c) {
    case AOS_NOTIF_CALL_INCOMING:
    case AOS_NOTIF_CALL_MISSED:
    case AOS_NOTIF_VOICEMAIL:      return LV_SYMBOL_CALL;
    case AOS_NOTIF_EMAIL:          return LV_SYMBOL_ENVELOPE;
    case AOS_NOTIF_LOCATION:       return LV_SYMBOL_GPS;
    case AOS_NOTIF_ENTERTAINMENT:  return LV_SYMBOL_AUDIO;
    default:                       return LV_SYMBOL_BELL;
    }
}

/* -------------------------------------------------------------------------- */

static void cerrar_cb(lv_event_t *e)
{
    (void)e;
    aos_notif_ui_close();
}

/* Deletes it from the watch's history and closes. It says nothing to the
 * phone: it is "take it off my list", not "dismiss it on the phone". For the
 * latter there is Reject, which also makes the phone say so and removes it
 * from here by itself. */
static void borrar_cb(lv_event_t *e)
{
    (void)e;
    uint32_t uid = s_uid;
    aos_notif_ui_close();
    aos_hal_notif_remove(uid);
}

static void accion_cb(lv_event_t *e)
{
    bool positiva = lv_event_get_user_data(e) != NULL;
    uint32_t uid = s_uid;
    aos_notif_ui_close();
    aos_hal_notif_action(uid, positiva);
}

void aos_notif_ui_close(void)
{
    if (!s_root) {
        return;
    }
    lv_obj_delete(s_root);
    s_root = NULL;
    s_uid  = 0;
    s_can_pos = false;
    s_can_neg = false;
}

bool aos_notif_ui_visible(void)
{
    return s_root != NULL;
}

uint32_t aos_notif_ui_uid(void)
{
    return s_root ? s_uid : 0;
}

void aos_notif_ui_tick(void)
{
    if (!s_root) {
        return;
    }
    if (lv_tick_elaps(s_abierta_ms) < DESPIERTA_MS) {
        aos_hal_activity();
    }
}

/* -------------------------------------------------------------------------- */

void aos_notif_ui_show(const aos_notif_t *n, bool desde_historial)
{
    if (!n) {
        return;
    }
    aos_notif_ui_close();               /* the last thing that arrived is what is seen */

    s_uid        = n->uid;
    s_can_pos    = n->can_positive;
    s_can_neg    = n->can_negative;
    s_abierta_ms = lv_tick_get();

    lv_color_t color = color_de(n->category);

    /* BLACK AND OPAQUE background, and no animating transformations or opacity
     * on the container.
     *
     * This is not an aesthetic preference: LVGL builds a separate layer buffer
     * for any object WITH CHILDREN that is given opacity or a transformation,
     * and at full screen that is 322 KB to allocate and clear per frame. It is
     * what hung the launcher with the watchdog back in the day (see
     * DECISIONES.md, "El piso de dibujo de LVGL"). On AMOLED, besides, black
     * is a pixel switched off: the background comes free.
     *
     * Nor does it come in with an animation. A full-screen rectangle costs
     * 25 ms measured, so sliding it would give about ten frames per second. A
     * notification has to appear, not come crawling in. */
    s_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, AOS_SCREEN_W, AOS_SCREEN_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_root, cerrar_cb, LV_EVENT_CLICKED, NULL);

    /* Header: the category's glyph and the app's name. */
    char app[48];
    aos_text_safe(app, sizeof(app), n->app[0] ? n->app : "");

    lv_obj_t *cab = lv_obj_create(s_root);
    lv_obj_remove_style_all(cab);
    lv_obj_set_size(cab, AOS_SCREEN_W - 2 * MARGEN, 34);
    lv_obj_align(cab, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_flex_flow(cab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cab, 8, 0);
    lv_obj_remove_flag(cab, LV_OBJ_FLAG_SCROLLABLE);

    aos_label(cab, glifo_de(n->category), aos_font_body, color);
    if (app[0]) {
        aos_label(cab, app, aos_font_small, AOS_C_DIM);
    }
    /* In LVGL 9 every lv_obj is born clickable, so without this the header
     * eats the touch meant for the background and the notification does not
     * close if you happen to touch there. It holds for everything drawn on
     * top. */
    aos_make_decorative(cab);

    /* The body goes in a column centred vertically between the header and the
     * footer. With absolute positions, a two-line message left half the screen
     * empty below and looked unbalanced; this way both a one-line notification
     * and a ten-line one end up in the middle. */
    /* With the buttons wrapped onto two lines the footer is twice as tall, and
     * the body has to give it the room or they overlap. */
    bool con_botones = n->can_positive || n->can_negative || desde_historial;
    int32_t alto_pie = con_botones ? 134 : 74;
    int32_t alto_cuerpo = AOS_SCREEN_H - 76 - alto_pie;

    lv_obj_t *cuerpo = lv_obj_create(s_root);
    lv_obj_remove_style_all(cuerpo);
    lv_obj_set_size(cuerpo, AOS_SCREEN_W - 2 * MARGEN, alto_cuerpo);
    lv_obj_align(cuerpo, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_flex_flow(cuerpo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cuerpo, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cuerpo, 18, 0);
    lv_obj_remove_flag(cuerpo, LV_OBJ_FLAG_SCROLLABLE);

    /* Title: two lines at most. It is the line read at a glance and is nearly
     * always a name; if it eats the screen there is no room left for the
     * message, which is what you want to read next. */
    char titulo[128];
    aos_text_safe(titulo, sizeof(titulo), n->title);
    lv_obj_t *lbl_titulo = NULL;
    if (titulo[0]) {
        lbl_titulo = lv_label_create(cuerpo);
        lv_label_set_text(lbl_titulo, titulo);
        lv_label_set_long_mode(lbl_titulo, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(lbl_titulo, aos_font_title, 0);
        lv_obj_set_style_text_color(lbl_titulo, AOS_C_TEXT, 0);
        lv_obj_set_style_text_align(lbl_titulo, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_titulo, lv_pct(100));
        lv_obj_set_height(lbl_titulo, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(lbl_titulo, 76, 0);
    }

    /* "and 3 more": what turns a burst of WhatsApp into one notification
     * instead of eight. The text shown is that of the last message, which is
     * the one you want to read; this says how many were left behind. */
    if (n->repeticiones > 1) {
        char cuantas[32];
        snprintf(cuantas, sizeof(cuantas), _("y %u mas"),
                 (unsigned)(n->repeticiones - 1));
        lv_obj_t *lbl = aos_label(cuerpo, cuantas, aos_font_small, AOS_C_DIM);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* A little rule in the category's colour, so the block does not float. */
    lv_obj_t *raya = lv_obj_create(cuerpo);
    lv_obj_remove_style_all(raya);
    lv_obj_set_size(raya, 48, 3);
    lv_obj_set_style_bg_color(raya, color, 0);
    lv_obj_set_style_bg_opa(raya, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(raya, 2, 0);

    /* Message. Deliberately no scrolling: with scrolling, the drag to read and
     * the tap to close collide. */
    char mensaje[320];
    aos_text_safe(mensaje, sizeof(mensaje), n->message);
    lv_obj_t *lbl_msg = NULL;
    if (mensaje[0]) {
        lbl_msg = lv_label_create(cuerpo);
        lv_label_set_text(lbl_msg, mensaje);
        lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(lbl_msg, aos_font_body, 0);
        lv_obj_set_style_text_color(lbl_msg, AOS_C_TEXT, 0);
        lv_obj_set_style_text_align(lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(lbl_msg, lv_pct(100));
        lv_obj_set_height(lbl_msg, LV_SIZE_CONTENT);
    }

    /* And here is where it is decided whether clipping is needed, by measuring
     * instead of estimating.
     *
     * With LONG_WRAP the label grows as much as it needs and a long message
     * would spill off the screen without saying anything. The column is built,
     * how much it really takes is measured, and only if it does not fit is it
     * switched to LONG_DOT with the height that is left: that way the cut is
     * visible -it ends in an ellipsis- instead of disappearing below the
     * edge. */
    if (lbl_msg) {
        lv_obj_update_layout(s_root);
        int32_t sobra = alto_cuerpo - 3 - 36;       /* the rule and the two gaps */
        if (lbl_titulo) {
            sobra -= lv_obj_get_height(lbl_titulo);
        }
        if (lv_obj_get_height(lbl_msg) > sobra && sobra > 24) {
            lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_DOT);
            lv_obj_set_height(lbl_msg, sobra);
        }
    }

    aos_make_decorative(cuerpo);

    if (con_botones) {
        /* Answer and hang up. It is the only thing on this screen that does
         * anything beyond closing itself, and that is why they are buttons and
         * not a gesture. */
        /* ROW_WRAP: if the two buttons do not fit on one line -in German they
         * only just do-, the second drops down instead of being drawn on top
         * of the first. See the long comment about the same change in
         * aos_app_settings.c. */
        lv_obj_t *fila = lv_obj_create(s_root);
        lv_obj_remove_style_all(fila);
        lv_obj_set_width(fila, AOS_SCREEN_W - 2 * MARGEN);
        lv_obj_set_height(fila, LV_SIZE_CONTENT);
        lv_obj_align(fila, LV_ALIGN_BOTTOM_MID, 0, -40);
        lv_obj_set_flex_flow(fila, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(fila, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(fila, 8, 0);
        lv_obj_set_style_pad_column(fila, 10, 0);
        lv_obj_remove_flag(fila, LV_OBJ_FLAG_SCROLLABLE);

        /* One button for each action the phone said it had, and not one more.
         * An incoming call announces both and both work; a WhatsApp message
         * announces only the negative one, and asking it for the positive
         * returns 0xA3. Who decides what is drawn is the phone, not us. */
        lv_obj_t *no = NULL, *si = NULL, *borrar = NULL;
        if (n->can_negative) {
            no = aos_button(fila, _("Rechazar"), AOS_C_RED, accion_cb, NULL);
        }
        if (n->can_positive) {
            si = aos_button(fila, _("Aceptar"), AOS_C_GREEN, accion_cb, (void *)1);
        }
        if (desde_historial) {
            borrar = aos_button(fila, _("Borrar"), AOS_C_CARD2, borrar_cb, NULL);
        }

        /* The whole row stops eating the touches -so tapping the gap between
         * the two buttons closes, as anywhere else on the screen- and then the
         * touch is given back to the buttons. */
        aos_make_decorative(fila);
        if (no) lv_obj_add_flag(no, LV_OBJ_FLAG_CLICKABLE);
        if (si) lv_obj_add_flag(si, LV_OBJ_FLAG_CLICKABLE);
        if (borrar) lv_obj_add_flag(borrar, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_t *pista = aos_label(s_root, _("tocá para cerrar"),
                                    aos_font_small, AOS_C_DIM);
        lv_obj_align(pista, LV_ALIGN_BOTTOM_MID, 0, -34);
        aos_make_decorative(pista);
    }
}
