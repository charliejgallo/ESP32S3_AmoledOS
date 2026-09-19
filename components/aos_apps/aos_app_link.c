/*
 * AmoledOS - Link: the other watches around, and the one we are paired with.
 *
 * The screen of docs/LINK.md phase 2. It brings the link up while it is
 * open (and takes it down on exit: the radio is not left listening for
 * nothing), enables pairing, and shows: this watch's name, the partner
 * (with whether it is around and whether the encrypted peer answered), and
 * the neighbours that beaconed in the last five seconds with their signal.
 * Pairing is the bump: knock the two watches together while both are on
 * this screen. "Forget" drops the partner.
 */
#include "aos_apps.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>

#define LINK_ROWS 5

typedef struct {
    lv_obj_t   *me;
    lv_obj_t   *partner;
    lv_obj_t   *partner_state;
    lv_obj_t   *hint;
    lv_obj_t   *rows[LINK_ROWS];
    lv_obj_t   *forget;
    lv_timer_t *timer;
    bool        up;
    uint32_t    pair_events;
} link_app_t;

static link_app_t s_link;

/* A four-step meter in plain ASCII: the fonts in the firmware have no
 * block glyphs (they came out as boxes on the watch, 2026-09-19). */
static const char *bars(int rssi)
{
    if (rssi >= -35) return "||||";
    if (rssi >= -50) return "|||.";
    if (rssi >= -65) return "||..";
    return "|...";
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    char buf[96];

    if (!s_link.up) {
        lv_label_set_text(s_link.hint, _("El enlace no arrancó: la radio tiene que estar encendida"));
        return;
    }

    aos_link_partner_t p;
    aos_hal_link_partner(&p);
    if (p.valid) {
        snprintf(buf, sizeof(buf), "%s  %s", p.name[0] ? p.name : "?", p.seen ? bars(p.rssi) : "");
        lv_label_set_text(s_link.partner, buf);
        lv_label_set_text(s_link.partner_state,
                          !p.seen ? _("pareja guardada, no está cerca") :
                          p.confirmed ? _("pareja cerca, canal cifrado listo") :
                          _("pareja cerca"));
        lv_obj_remove_flag(s_link.forget, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_link.partner, _("sin pareja"));
        lv_label_set_text(s_link.partner_state, "");
        lv_obj_add_flag(s_link.forget, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t events = aos_hal_link_pair_events();
    if (events != s_link.pair_events) {
        s_link.pair_events = events;
        aos_hal_beep(1200, 60);
    }

    lv_label_set_text(s_link.hint, aos_hal_link_pairing()
                      ? _("Chocá los dos relojes para aparearlos")
                      : "");

    aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
    int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
    for (int i = 0; i < LINK_ROWS; i++) {
        if (i < n) {
            snprintf(buf, sizeof(buf), "%s  %s  %d dBm%s%s", bars(nb[i].rssi),
                     nb[i].name[0] ? nb[i].name : "?", nb[i].rssi,
                     nb[i].app[0] ? "  ·  " : "", nb[i].app);
            lv_label_set_text(s_link.rows[i], buf);
            lv_obj_remove_flag(s_link.rows[i], LV_OBJ_FLAG_HIDDEN);
        } else if (i == 0 && n == 0) {
            lv_label_set_text(s_link.rows[0], _("nadie cerca"));
            lv_obj_remove_flag(s_link.rows[0], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_link.rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void forget_cb(lv_event_t *event)
{
    (void)event;
    aos_hal_link_unpair();
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    lv_obj_t *page = aos_page(root);
    memset(&s_link, 0, sizeof s_link);

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", aos_hal_device_name());
    s_link.me = aos_label(page, buf, aos_font_title, AOS_C_TEXT);
    lv_obj_align(s_link.me, LV_ALIGN_TOP_MID, 0, 36);

    lv_obj_t *cap = aos_label(page, _("pareja"), aos_font_small, AOS_C_DIM);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 24, 84);
    s_link.partner = aos_label(page, "", aos_font_body, AOS_C_TEXT);
    lv_obj_align(s_link.partner, LV_ALIGN_TOP_LEFT, 24, 104);
    s_link.partner_state = aos_label(page, "", aos_font_small, AOS_C_TEAL);
    lv_obj_align(s_link.partner_state, LV_ALIGN_TOP_LEFT, 24, 130);

    s_link.hint = aos_label(page, "", aos_font_small, AOS_C_ACCENT);
    lv_obj_set_width(s_link.hint, AOS_SCREEN_W - 48);
    lv_label_set_long_mode(s_link.hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_link.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_link.hint, LV_ALIGN_TOP_MID, 0, 160);

    lv_obj_t *cap2 = aos_label(page, _("cerca"), aos_font_small, AOS_C_DIM);
    lv_obj_align(cap2, LV_ALIGN_TOP_LEFT, 24, 206);
    for (int i = 0; i < LINK_ROWS; i++) {
        s_link.rows[i] = aos_label(page, "", aos_font_small, AOS_C_TEXT);
        lv_obj_align(s_link.rows[i], LV_ALIGN_TOP_LEFT, 24, 228 + i * 24);
        lv_obj_add_flag(s_link.rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_link.forget = aos_button(page, _("Olvidar pareja"), AOS_C_CARD2, forget_cb, NULL);
    lv_obj_align(s_link.forget, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_obj_set_height(s_link.forget, 34);
    lv_obj_add_flag(s_link.forget, LV_OBJ_FLAG_HIDDEN);

    s_link.up = aos_hal_link_start();
    if (s_link.up) {
        aos_hal_link_offer("");
        aos_hal_link_pair_enable(true);
    }
    s_link.pair_events = aos_hal_link_pair_events();
    s_link.timer = lv_timer_create(refresh, 500, NULL);
    refresh(NULL);
    return &s_link;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_link.timer) {
        lv_timer_delete(s_link.timer);
        s_link.timer = NULL;
    }
    aos_hal_link_pair_enable(false);
    if (s_link.up) {
        aos_hal_link_stop();
    }
}

void aos_app_link_get(aos_app_t *app)
{
    *app = (aos_app_t){
        .desc = {
            .id       = "aos.link",
            .name     = "Enlace",
            .icon_vec = AOS_ICON_LINK,
            .color_a  = 0x0A84FF,
            .color_b  = 0x003C8A,
            .order    = 14,
            .flags    = AOS_APP_FLAG_KEEP_AWAKE,
        },
        .create  = create,
        .destroy = destroy,
    };
}
