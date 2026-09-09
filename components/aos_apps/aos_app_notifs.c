/*
 * AmoledOS - Notifications: what arrived from the phone and is no longer on
 * screen.
 *
 * A notification's overlay lasts twenty seconds and closes on the first touch;
 * this app is where whatever you did not get to read ends up. The store had
 * existed since phase F1 -aos_hal_notif_at(), a ring of sixteen in PSRAM-, so
 * there is no state of its own here: what the HAL stores is what is drawn.
 *
 * Tapping one reopens it in the same overlay it arrived in, without copying a
 * line of its drawing. That includes the action buttons when the phone
 * declared them: dismissing an old message from the watch is useful, and if
 * the phone no longer has it, it answers that it could not and the interface
 * says so.
 */
#include "aos_apps.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_i18n.h"
#include "aos_notif_ui.h"
#include "aos_text_safe.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    lv_obj_t *page;
    int       dibujadas;    /* how many there were when the list was drawn */
} notifs_t;

static notifs_t s_nf;

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

/* The time if it is from today, the day if not. A notification from ten
 * minutes ago and one from the day before yesterday are indistinguishable by
 * "14:32", and in a list of sixteen that matters more than the exact minute. */
static void cuando_texto(time_t cuando, char *out, size_t cap)
{
    if (cuando <= 0) {
        out[0] = '\0';
        return;
    }
    struct tm t = *localtime(&cuando);

    struct tm ahora;
    aos_hal_time_now(&ahora);

    if (t.tm_year == ahora.tm_year && t.tm_yday == ahora.tm_yday) {
        snprintf(out, cap, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        snprintf(out, cap, "%d %s", t.tm_mday, aos_month_name(t.tm_mon));
    }
}

static void abrir_cb(lv_event_t *event)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(event);
    aos_notif_t n;
    if (aos_hal_notif_at(idx, &n)) {
        /* The same overlay it arrived in. 'repetitions' is taken off it
         * because "and 3 more" makes sense at the time and not afterwards: in
         * the list the other three are right below, each on its own line. */
        n.repeticiones = 1;
        aos_notif_ui_show(&n, true);
    }
}

static void borrar_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_notif_clear();
    s_nf.dibujadas = -1;            /* so the tick redraws it empty */
}

static void lista_construir(lv_obj_t *page)
{
    lv_obj_clean(page);

    int n = aos_hal_notif_count();
    s_nf.dibujadas = n;

    /* Who is on the other side and how much battery they have left. It is the
     * only place in the interface where that fact appears without going to
     * look for it in Settings, and this is precisely the screen for the things
     * the phone sends. */
    if (aos_hal_bt_state() == AOS_BT_CONNECTED) {
        char cab[80];
        int pila = 0;
        if (aos_hal_bt_phone_battery(&pila)) {
            snprintf(cab, sizeof(cab), "%s  %s  %d%%", aos_hal_bt_peer(),
                     LV_SYMBOL_BATTERY_FULL, pila);
        } else {
            snprintf(cab, sizeof(cab), "%s", aos_hal_bt_peer());
        }
        lv_obj_t *l = aos_label(page, cab, aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, AOS_SCREEN_W - 60);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    }

    if (n == 0) {
        lv_obj_t *vacio = aos_label(page, _("No hay notificaciones"),
                                    aos_font_body, AOS_C_DIM);
        lv_obj_set_width(vacio, AOS_SCREEN_W - 60);
        lv_label_set_long_mode(vacio, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(vacio, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(vacio, 120, 0);

        if (aos_hal_bt_state() != AOS_BT_CONNECTED) {
            lv_obj_t *pista = aos_label(page, _("El telefono no esta conectado"),
                                        aos_font_small, AOS_C_DIM);
            lv_obj_set_width(pista, AOS_SCREEN_W - 60);
            lv_label_set_long_mode(pista, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_style_text_align(pista, LV_TEXT_ALIGN_CENTER, 0);
        }
        return;
    }

    for (int i = 0; i < n; i++) {
        aos_notif_t nt;
        if (!aos_hal_notif_at(i, &nt)) {
            continue;
        }

        lv_obj_t *card = lv_obj_create(page);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, AOS_SCREEN_W - 60, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_left(card, 16, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(card, 2, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, abrir_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        /* The colour band on the left says the category without spending a
         * line of text saying it. */
        lv_obj_t *banda = lv_obj_create(card);
        lv_obj_remove_style_all(banda);
        lv_obj_set_size(banda, 4, 34);
        lv_obj_set_style_bg_color(banda, color_de(nt.category), 0);
        lv_obj_set_style_bg_opa(banda, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(banda, 2, 0);
        lv_obj_add_flag(banda, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(banda, LV_ALIGN_LEFT_MID, -10, 0);

        char app[48], titulo[80], hora[16];
        aos_text_safe(app, sizeof(app), nt.app);
        aos_text_safe(titulo, sizeof(titulo), nt.title[0] ? nt.title : nt.message);
        cuando_texto(nt.when, hora, sizeof(hora));

        char cab[80];
        snprintf(cab, sizeof(cab), "%s%s%s", app,
                 (app[0] && hora[0]) ? "  ·  " : "", hora);

        lv_obj_t *l1 = aos_label(card, cab, aos_font_small, AOS_C_DIM);
        lv_label_set_long_mode(l1, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l1, AOS_SCREEN_W - 90);

        lv_obj_t *l2 = aos_label(card, titulo, aos_font_body, AOS_C_TEXT);
        lv_label_set_long_mode(l2, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l2, AOS_SCREEN_W - 90);

        aos_make_decorative(l1);
        aos_make_decorative(l2);
        aos_make_decorative(banda);
    }

    aos_button(page, _("Borrar todo"), AOS_C_CARD2, borrar_cb, NULL);
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
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_style_pad_ver(page, 16, 0);

    s_nf.page = page;
    lista_construir(page);
    return &s_nf;
}

/* Rebuilding the whole list every time the count changes is cheap -it is
 * sixteen cards at most- and it avoids carrying the state of which is which.
 * It is only rebuilt when the number changed, so an app that is open and idle
 * draws nothing. */
static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_nf.page && aos_hal_notif_count() != s_nf.dibujadas) {
        lista_construir(s_nf.page);
    }
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    s_nf.page = NULL;
    s_nf.dibujadas = 0;
}

void aos_app_notifs_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id      = "aos.notifs",
            .name    = "Notificaciones",
            .icon    = LV_SYMBOL_BELL,
            .color_a = 0xFF375F,
            .color_b = 0x8E1738,
            .order   = 45,
        },
        .create  = create,
        .destroy = destroy,
        .tick    = tick,
    };
}
