/*
 * AmoledOS - Network scanner (#42)
 *
 * Sweeps the WiFi networks around and, if there is a connection, our own LAN:
 * which addresses are live, with what MAC and which TCP ports they have open.
 *
 * THE SCREEN SHOWS ONLY TOTALS, and that is deliberate: a table of thirty
 * hosts with their MACs and their ports does not fit in 368 px, and forcing it
 * would make illegible what is already stored properly. The full survey is
 * written by the HAL to the microSD and read from the portal, on a real
 * screen.
 *
 * That is why this app is so small: it does not have the sweep inside it. The
 * slow work lives in the HAL —a dynamic app has no sockets, cannot create
 * tasks and cannot block the LVGL thread— and here the state is simply asked
 * for from the tick, just as 'clima' does with its HTTP requests.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MS     200

typedef struct {
    lv_obj_t   *root;
    lv_timer_t *timer;

    lv_obj_t *lbl_fase;
    lv_obj_t *barra;
    lv_obj_t *num[3];
    lv_obj_t *boton[2];         /* 0 = wifi, 1 = hosts */
    lv_obj_t *lbl_boton[2];
    lv_obj_t *lbl_pie;

    /* The last thing written: LVGL does not compare, so writing the same text
     * to a label invalidates it all the same. Measured in the tuner:
     * repainting without comparing cost more than twice the drawing time. */
    char txt_num[3][12];
    char txt_fase[64];
    char txt_boton[2][12];
    char txt_pie[64];
    int  ultimo_pct;

    bool corriendo;
} esc_t;

static esc_t s_esc;

static const char *FASES[] = {
    [AOS_SCAN_IDLE]     = N_("listo para buscar"),
    [AOS_SCAN_PH_WIFI]  = N_("buscando redes wifi"),
    [AOS_SCAN_PH_HOSTS] = N_("barriendo la red local"),
    [AOS_SCAN_PH_PORTS] = N_("probando puertos"),
    [AOS_SCAN_DONE]     = N_("terminado"),
    [AOS_SCAN_FAILED]   = N_("cortado"),
    [AOS_SCAN_PH_MDNS]  = N_("buscando nombres"),
};
#define N_FASES ((int)(sizeof(FASES) / sizeof(FASES[0])))

/* The index comes from the firmware and this app may be running on a newer one
 * than itself: if a phase it does not know turns up, better a vague text than
 * reading past the end of the array. */
static const char *fase_txt(aos_scan_phase_t f)
{
    if ((int)f < 0 || (int)f >= N_FASES || !FASES[f]) {
        return _("trabajando");
    }
    return _(FASES[f]);
}

static void set_txt(lv_obj_t *obj, char *cache, size_t cap, const char *txt)
{
    if (!obj || !txt || strncmp(cache, txt, cap - 1) == 0) {
        return;
    }
    snprintf(cache, cap, "%s", txt);
    lv_label_set_text(obj, cache);
}

/* -------------------------------------------------------------------------- */

static void pintar(void)
{
    aos_scan_status_t st;
    if (!aos_hal_scan_status(&st)) {
        return;
    }

    char buf[64];

    snprintf(buf, sizeof(buf), "%d", st.wifi_found);
    set_txt(s_esc.num[0], s_esc.txt_num[0], sizeof(s_esc.txt_num[0]), buf);
    snprintf(buf, sizeof(buf), "%d", st.hosts_found);
    set_txt(s_esc.num[1], s_esc.txt_num[1], sizeof(s_esc.txt_num[1]), buf);
    snprintf(buf, sizeof(buf), "%d", st.ports_found);
    set_txt(s_esc.num[2], s_esc.txt_num[2], sizeof(s_esc.txt_num[2]), buf);

    bool activo = (st.phase == AOS_SCAN_PH_WIFI ||
                   st.phase == AOS_SCAN_PH_HOSTS ||
                   st.phase == AOS_SCAN_PH_PORTS ||
                   st.phase == AOS_SCAN_PH_MDNS);
    s_esc.corriendo = activo;

    if (activo && st.total > 0) {
        snprintf(buf, sizeof(buf), "%s  %d/%d",
                 fase_txt(st.phase), st.done, st.total);
    } else if (st.phase == AOS_SCAN_DONE) {
        snprintf(buf, sizeof(buf), _("terminado en %u s"),
                 (unsigned)(st.elapsed_ms / 1000));
    } else {
        snprintf(buf, sizeof(buf), "%s", fase_txt(st.phase));
    }
    set_txt(s_esc.lbl_fase, s_esc.txt_fase, sizeof(s_esc.txt_fase), buf);

    int pct = (st.total > 0) ? (st.done * 100 / st.total) : 0;
    if (!activo) {
        pct = (st.phase == AOS_SCAN_DONE) ? 100 : 0;
    }
    if (pct != s_esc.ultimo_pct) {
        s_esc.ultimo_pct = pct;
        lv_bar_set_value(s_esc.barra, pct, LV_ANIM_OFF);
    }

    /* The two sweeps are different questions and cost different amounts of
     * time: the networks around are two seconds and the LAN can be a minute.
     * That is why there are two buttons and not one: if all you wanted was to
     * see what wifi there is, you should not have to wait for a sweep of 254
     * addresses. */
    static const char *ETIQ[2] = { N_("WIFI"), N_("EQUIPOS") };
    for (int i = 0; i < 2; i++) {
        set_txt(s_esc.lbl_boton[i], s_esc.txt_boton[i],
                sizeof(s_esc.txt_boton[i]), activo ? _("PARAR") : _(ETIQ[i]));
        lv_obj_set_style_bg_color(s_esc.boton[i],
                                  activo ? AOS_C_RED
                                         : (i ? AOS_C_GREEN : AOS_C_TEAL), 0);
    }

    /* The footer says where the detail is, which is half the point of the app. */
    if (st.phase == AOS_SCAN_DONE || st.phase == AOS_SCAN_FAILED) {
        const char *barra_ = strrchr(st.path, '/');
        /* %.32s and not %s: st.path is 160 bytes and the footer 64. Clipping
         * it by hand is what stops the compiler being right to complain, and
         * the file's name fits whole into the bargain (it is ~20
         * characters). */
        snprintf(buf, sizeof(buf), "amoledos.local/red  -  %.32s",
                 barra_ ? barra_ + 1 : st.path);
    } else if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", _("sin red: solo se ven las wifi de alrededor"));
    } else {
        snprintf(buf, sizeof(buf), "%s", _("el detalle, en amoledos.local/red"));
    }
    set_txt(s_esc.lbl_pie, s_esc.txt_pie, sizeof(s_esc.txt_pie), buf);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    pintar();
}

static void ev_boton(lv_event_t *e)
{
    uint32_t flags = (uint32_t)(intptr_t)lv_event_get_user_data(e);
    if (s_esc.corriendo) {
        aos_hal_scan_stop();
        return;
    }
    if (!aos_hal_scan_start(flags)) {
        aos_ui_toast(_("no se pudo arrancar"), 1500);
        return;
    }
    /* While it sweeps, the screen stays on: a sweep can take a minute and
     * switching off halfway cancels nothing, but it leaves whoever is watching
     * in the dark. */
    s_esc.ultimo_pct = -1;
    pintar();
}

/* -------------------------------------------------------------------------- */

static lv_obj_t *fila(lv_obj_t *p, int y, const char *titulo, const char *sub,
                      lv_color_t color)
{
    lv_obj_t *num = lv_label_create(p);
    lv_label_set_text(num, "0");
    lv_obj_set_style_text_font(num, aos_font_huge, 0);
    lv_obj_set_style_text_color(num, color, 0);
    lv_obj_set_style_text_align(num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_size(num, 130, 58);
    lv_obj_set_pos(num, 18, y);

    lv_obj_t *t = lv_label_create(p);
    lv_label_set_text(t, titulo);
    lv_obj_set_style_text_font(t, aos_font_body, 0);
    lv_obj_set_style_text_color(t, AOS_C_TEXT, 0);
    lv_obj_set_pos(t, 164, y + 6);

    lv_obj_t *s = lv_label_create(p);
    lv_label_set_text(s, sub);
    lv_obj_set_style_text_font(s, aos_font_small, 0);
    lv_obj_set_style_text_color(s, AOS_C_DIM, 0);
    lv_obj_set_pos(s, 164, y + 32);

    return num;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_esc, 0, sizeof(s_esc));
    s_esc.root = root;
    s_esc.ultimo_pct = -1;

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    s_esc.lbl_fase = lv_label_create(root);
    lv_label_set_text(s_esc.lbl_fase, _("listo para buscar"));
    lv_obj_set_style_text_font(s_esc.lbl_fase, aos_font_body, 0);
    lv_obj_set_style_text_color(s_esc.lbl_fase, AOS_C_DIM, 0);
    lv_obj_set_style_text_align(s_esc.lbl_fase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_esc.lbl_fase, AOS_SCREEN_W, 26);
    lv_obj_set_pos(s_esc.lbl_fase, 0, 8);

    s_esc.barra = lv_bar_create(root);
    lv_obj_set_size(s_esc.barra, AOS_SCREEN_W - 48, 8);
    lv_obj_set_pos(s_esc.barra, 24, 40);
    lv_obj_set_style_bg_color(s_esc.barra, AOS_C_CARD2, 0);
    lv_obj_set_style_bg_color(s_esc.barra, AOS_C_ACCENT, LV_PART_INDICATOR);
    lv_bar_set_range(s_esc.barra, 0, 100);
    lv_bar_set_value(s_esc.barra, 0, LV_ANIM_OFF);

    s_esc.num[0] = fila(root, 70,  _("redes"),   _("wifi de alrededor"), AOS_C_TEAL);
    s_esc.num[1] = fila(root, 150, _("equipos"), _("en tu red local"),   AOS_C_GREEN);
    s_esc.num[2] = fila(root, 230, _("puertos"), _("abiertos, en total"), AOS_C_ORANGE);

    static const char   *ETIQ[2]  = { N_("WIFI"), N_("EQUIPOS") };
    static const uint32_t FLAGS[2] = { AOS_SCAN_WIFI,
                                       AOS_SCAN_HOSTS | AOS_SCAN_PORTS |
                                       AOS_SCAN_MDNS };
    for (int i = 0; i < 2; i++) {
        s_esc.boton[i] = lv_obj_create(root);
        lv_obj_remove_style_all(s_esc.boton[i]);
        lv_obj_set_size(s_esc.boton[i], 152, 54);
        lv_obj_set_pos(s_esc.boton[i], i ? 192 : 24, 306);
        lv_obj_set_style_bg_color(s_esc.boton[i], i ? AOS_C_GREEN : AOS_C_TEAL, 0);
        lv_obj_set_style_bg_opa(s_esc.boton[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_esc.boton[i], 27, 0);
        lv_obj_add_flag(s_esc.boton[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_esc.boton[i], ev_boton, LV_EVENT_CLICKED,
                            (void *)(intptr_t)FLAGS[i]);

        s_esc.lbl_boton[i] = lv_label_create(s_esc.boton[i]);
        lv_label_set_text(s_esc.lbl_boton[i], ETIQ[i]);
        lv_obj_set_style_text_font(s_esc.lbl_boton[i], aos_font_body, 0);
        lv_obj_set_style_text_color(s_esc.lbl_boton[i], AOS_C_BG, 0);
        lv_obj_center(s_esc.lbl_boton[i]);
        lv_obj_remove_flag(s_esc.lbl_boton[i], LV_OBJ_FLAG_CLICKABLE);
    }

    s_esc.lbl_pie = lv_label_create(root);
    lv_label_set_text(s_esc.lbl_pie, _("el detalle, en amoledos.local/red"));
    lv_obj_set_style_text_font(s_esc.lbl_pie, aos_font_small, 0);
    lv_obj_set_style_text_color(s_esc.lbl_pie, AOS_C_DIM, 0);
    lv_obj_set_style_text_align(s_esc.lbl_pie, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_esc.lbl_pie, AOS_SCREEN_W, 40);
    lv_obj_set_pos(s_esc.lbl_pie, 0, 372);

    /* Development switch: ESC_AUTO=1 starts the sweep on opening, which
     * otherwise means pressing the button on every test, and in the automated
     * simulator there is no finger. */
    if (getenv("ESC_AUTO")) {
        aos_hal_scan_start(AOS_SCAN_TODO);
    }

    pintar();
    s_esc.timer = lv_timer_create(tick, TICK_MS, NULL);
    return &s_esc;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_esc.timer) {
        lv_timer_delete(s_esc.timer);
        s_esc.timer = NULL;
    }
    /* The sweep is NOT stopped on exit, and that is deliberate: it lives in
     * the HAL, it takes a minute and it goes on writing its report. Closing
     * the app to look at something else is no reason to throw the work
     * away. */
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_esc, 0, sizeof(s_esc));
}

static bool escaner_init(aos_app_t *app)
{
    app->desc.id       = "aos.netscan";
    app->desc.name     = "Escaner";
    app->desc.icon     = "red";
    app->desc.icon_vec = AOS_ICON_RADAR;
    app->desc.color_a  = 0x30D158;
    app->desc.color_b  = 0x0B5227;
    app->desc.order    = 79;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE;

    app->create  = create;
    app->destroy = destroy;
    return true;
}

AOS_APP_ENTRY(escaner_init);
