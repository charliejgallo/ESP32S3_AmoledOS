/*
 * AmoledOS - Home Assistant sensor panel (#46)
 *
 * Up to four numeric sensors, each with its large value and its chart of the
 * last hour. WHICH sensors are chosen in the portal, at /sensores, with a
 * search over the 250 numeric sensors Home Assistant returns: on the screen
 * that cannot be chosen, and 'clima' had already taught the lesson.
 *
 * IT SHARES REMOTO'S CONFIGURATION. Home Assistant's address and token are
 * already in NVS (rc_url / rc_token) because /remoto stores them, and there is
 * no point asking for them twice: it is the same server. If Remoto is not
 * configured, this app says so and sends you to the portal.
 *
 * The chart is built from what the app measures itself, and NOT by asking Home
 * Assistant for the history. The reason is measured and written down in
 * sn_api.h: the history costs 67 bytes per point and one power sensor returned
 * 901 points in an hour.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include "sn_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREF_MS     3000
#define KEY_LIST    "sn_list"
#define KEY_GEN     "sn_gen"
#define KEY_URL     "rc_url"        /* from Remoto: it is the same Home Assistant */
#define KEY_TOKEN   "rc_token"

typedef struct {
    lv_obj_t   *root;
    lv_obj_t   *pie;

    /* One card per sensor, created ONCE. */
    lv_obj_t          *lbl_nom[SN_MAX];
    lv_obj_t          *lbl_val[SN_MAX];
    lv_obj_t          *grafico[SN_MAX];
    lv_chart_series_t *serie[SN_MAX];
    char               txt_val[SN_MAX][24];
    char               txt_pie[72];

    sn_sensor_t s[SN_MAX];
    int         n;

    char base[96];      /* rc_url's URL is up to 79 + "/api/template" */
    char hdr[320];
    char tpl[512];
    bool configurado;

    int32_t  gen;
    uint32_t pref_ms;
    uint32_t muestra_ms;
    int      req;
    bool     hubo;
    int      ultimo_error;
    uint32_t retry_ms;
} sn_t;

static sn_t s_sn;

static void set_txt(lv_obj_t *obj, char *cache, size_t cap, const char *txt)
{
    if (!obj || !txt || strncmp(cache, txt, cap - 1) == 0) {
        return;
    }
    snprintf(cache, cap, "%s", txt);
    lv_label_set_text(obj, cache);
}

/* -------------------------------------------------------------------------- */

/* How often it measures. It is SN_PERIODO_MS unless something else is asked
 * for through the environment: with a minute between samples, watching the
 * chart fill up in the simulator would take three hours. SN_PERIODO=2 does it
 * in six minutes. */
static uint32_t periodo_ms(void)
{
    const char *dev = getenv("SN_PERIODO");
    if (dev && dev[0]) {
        int seg = atoi(dev);
        if (seg >= 1 && seg <= 3600) {
            return (uint32_t)seg * 1000u;
        }
    }
    return SN_PERIODO_MS;
}

static void cargar_config(void)
{
    char lista[400] = {0};
    const char *dev = getenv("SN_LIST");
    if (dev && dev[0]) {
        snprintf(lista, sizeof(lista), "%s", dev);
    } else {
        aos_hal_pref_get_str(KEY_LIST, lista, sizeof(lista));
    }
    s_sn.n = sn_parse_config(lista, s_sn.s, SN_MAX);

    char url[80] = {0}, token[280] = {0};
    aos_hal_pref_get_str(KEY_URL, url, sizeof(url));
    aos_hal_pref_get_str(KEY_TOKEN, token, sizeof(token));
    for (size_t k = strlen(url); k > 0 && url[k - 1] == '/'; k = strlen(url)) {
        url[k - 1] = 0;
    }
    bool esquema = (strncmp(url, "http://", 7) == 0) ||
                   (strncmp(url, "https://", 8) == 0);

    s_sn.configurado = esquema && token[0] && s_sn.n > 0 &&
                       sn_build_template(s_sn.s, s_sn.n, s_sn.tpl, sizeof(s_sn.tpl));
    if (s_sn.configurado) {
        snprintf(s_sn.base, sizeof(s_sn.base), "%s/api/template", url);
        snprintf(s_sn.hdr, sizeof(s_sn.hdr), "Authorization: Bearer %s\r\n", token);
    }
}

/* -------------------------------------------------------------------------- */
/* Drawing                                                                     */
/* -------------------------------------------------------------------------- */

static void construir(void)
{
    lv_obj_clean(s_sn.root);
    memset(s_sn.txt_val, 0, sizeof(s_sn.txt_val));
    memset(s_sn.lbl_nom, 0, sizeof(s_sn.lbl_nom));
    memset(s_sn.lbl_val, 0, sizeof(s_sn.lbl_val));
    memset(s_sn.grafico, 0, sizeof(s_sn.grafico));
    memset(s_sn.serie, 0, sizeof(s_sn.serie));
    s_sn.txt_pie[0] = 0;

    s_sn.pie = lv_label_create(s_sn.root);
    lv_label_set_text(s_sn.pie, "");
    lv_obj_set_style_text_font(s_sn.pie, aos_font_small, 0);
    lv_obj_set_style_text_color(s_sn.pie, AOS_C_DIM, 0);
    lv_obj_set_style_text_align(s_sn.pie, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(s_sn.pie, AOS_SCREEN_W, 36);
    /* 380 and not 384: with a height of 36 it ended at 420 inside the root,
     * which is 2 px below the screen's edge. */
    lv_obj_set_pos(s_sn.pie, 0, 380);

    if (s_sn.n <= 0) {
        return;
    }

    /* The cards share out the available height: with a single sensor the chart
     * takes half the screen, with four they are four strips. */
    const int alto_total = 376;
    const int alto = alto_total / s_sn.n;

    for (int i = 0; i < s_sn.n; i++) {
        lv_obj_t *tarjeta = lv_obj_create(s_sn.root);
        lv_obj_remove_style_all(tarjeta);
        lv_obj_set_size(tarjeta, AOS_SCREEN_W - 24, alto - 8);
        lv_obj_set_pos(tarjeta, 12, 4 + i * alto);
        lv_obj_set_style_bg_color(tarjeta, AOS_C_CARD, 0);
        lv_obj_set_style_bg_opa(tarjeta, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tarjeta, 14, 0);
        lv_obj_remove_flag(tarjeta, LV_OBJ_FLAG_SCROLLABLE);

        s_sn.lbl_nom[i] = lv_label_create(tarjeta);
        lv_label_set_text(s_sn.lbl_nom[i], s_sn.s[i].nombre);
        lv_obj_set_style_text_font(s_sn.lbl_nom[i], aos_font_small, 0);
        lv_obj_set_style_text_color(s_sn.lbl_nom[i], AOS_C_DIM, 0);
        lv_label_set_long_mode(s_sn.lbl_nom[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_sn.lbl_nom[i], AOS_SCREEN_W - 24 - 24);
        lv_obj_set_pos(s_sn.lbl_nom[i], 12, 6);

        s_sn.lbl_val[i] = lv_label_create(tarjeta);
        lv_label_set_text(s_sn.lbl_val[i], "--");
        lv_obj_set_style_text_font(s_sn.lbl_val[i], aos_font_title, 0);
        lv_obj_set_style_text_color(s_sn.lbl_val[i], AOS_C_TEXT, 0);
        lv_obj_set_pos(s_sn.lbl_val[i], 12, 24);

        /* The chart goes to the RIGHT of the number and not below it: with
         * four sensors the card is 86 px and the two do not fit stacked. */
        int gw = AOS_SCREEN_W - 24 - 170;
        int gh = alto - 8 - 30;
        if (gh < 24) {
            gh = 24;
        }
        s_sn.grafico[i] = lv_chart_create(tarjeta);
        lv_obj_remove_style_all(s_sn.grafico[i]);
        lv_obj_set_size(s_sn.grafico[i], gw, gh);
        lv_obj_set_pos(s_sn.grafico[i], 162, (alto - 8 - gh) / 2);
        lv_chart_set_type(s_sn.grafico[i], LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(s_sn.grafico[i], SN_POINTS);
        lv_chart_set_div_line_count(s_sn.grafico[i], 0, 0);
        lv_chart_set_update_mode(s_sn.grafico[i], LV_CHART_UPDATE_MODE_SHIFT);
        /* No points: with 180 samples in 170 px, the little circles are a
         * smudge. The line alone reads. */
        lv_obj_set_style_size(s_sn.grafico[i], 0, 0, LV_PART_INDICATOR);
        lv_obj_set_style_line_width(s_sn.grafico[i], 2, LV_PART_ITEMS);
        s_sn.serie[i] = lv_chart_add_series(s_sn.grafico[i], AOS_C_GREEN,
                                            LV_CHART_AXIS_PRIMARY_Y);
    }
}

/* Rebuilds the whole series from the ring. It is called when data arrives,
 * that is, once every twenty seconds: there is no need to economise here. */
static void pintar_graficos(void)
{
    for (int i = 0; i < s_sn.n; i++) {
        if (!s_sn.grafico[i] || !s_sn.serie[i]) {
            continue;
        }
        int32_t lo, hi;
        if (!sn_range(&s_sn.s[i], &lo, &hi)) {
            continue;
        }
        lv_chart_set_axis_range(s_sn.grafico[i], LV_CHART_AXIS_PRIMARY_Y, lo, hi);

        int32_t *y = lv_chart_get_series_y_array(s_sn.grafico[i], s_sn.serie[i]);
        int cuantos = s_sn.s[i].n;
        for (int p = 0; p < SN_POINTS; p++) {
            /* The positions not yet measured go as LV_CHART_POINT_NONE and
             * LVGL does not draw them: that way the chart grows from the right
             * instead of starting with a false flat line stuck to the
             * floor. */
            int desde_atras = SN_POINTS - 1 - p;
            int idx = cuantos - 1 - desde_atras;
            y[p] = (idx >= 0) ? sn_at(&s_sn.s[i], idx) : LV_CHART_POINT_NONE;
        }
        lv_chart_refresh(s_sn.grafico[i]);
    }
}

static void pintar(void)
{
    char num[24], buf[72];

    /* In the background the root is hidden: it goes on measuring, but not a
     * single repaint is spent on something nobody sees. On reopening it,
     * create() does not run again —the app was never destroyed— so the repaint
     * is fired by show(), which the runtime calls when it comes back to the
     * foreground: without that the screen would show the values from a while
     * ago until the next sample landed, that is, for a whole minute. */
    if (s_sn.root && lv_obj_has_flag(s_sn.root, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    for (int i = 0; i < s_sn.n; i++) {
        if (!s_sn.lbl_val[i]) {
            continue;
        }
        if (s_sn.s[i].n == 0) {
            set_txt(s_sn.lbl_val[i], s_sn.txt_val[i], sizeof(s_sn.txt_val[i]), "--");
        } else {
            snprintf(buf, sizeof(buf), "%s %s",
                     sn_valor(s_sn.s[i].ultimo, num, sizeof(num)), s_sn.s[i].unidad);
            set_txt(s_sn.lbl_val[i], s_sn.txt_val[i], sizeof(s_sn.txt_val[i]), buf);
        }
        /* A sensor that has gone down turns grey instead of disappearing: the
         * last value is still information, but you have to see that it is
         * stale. */
        lv_obj_set_style_text_color(s_sn.lbl_val[i],
                                    (s_sn.s[i].n && !s_sn.s[i].ok) ? AOS_C_DIM
                                                                   : AOS_C_TEXT, 0);
    }

    if (!s_sn.configurado) {
        if (s_sn.n <= 0) {
            snprintf(buf, sizeof(buf), "%s", _("elegi los sensores en /sensores"));
        } else {
            snprintf(buf, sizeof(buf), "%s", _("falta configurar Home Assistant en /remoto"));
        }
    } else if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        snprintf(buf, sizeof(buf), "%s", _("sin conexion"));
    } else if (s_sn.ultimo_error == AOS_HTTP_ERR_SIN_HORA) {
        snprintf(buf, sizeof(buf), "%s", _("esperando la hora de la red..."));
    } else if (s_sn.ultimo_error == 401 || s_sn.ultimo_error == 403) {
        snprintf(buf, sizeof(buf), "%s", _("Home Assistant rechazo el token"));
    } else if (s_sn.ultimo_error != 0 && !s_sn.hubo) {
        snprintf(buf, sizeof(buf), _("no pude preguntar (%d)"), s_sn.ultimo_error);
    } else if (!s_sn.hubo) {
        snprintf(buf, sizeof(buf), "%s", _("midiendo..."));
    } else {
        /* How much chart there is: it is the first thing you wonder on seeing
         * a short line, and saying it stops you thinking something broke. */
        int seg = s_sn.s[0].n * (int)(periodo_ms() / 1000);
        if (seg < 120) {
            snprintf(buf, sizeof(buf), _("grafico: %d s   -   elegir en /sensores"), seg);
        } else {
            snprintf(buf, sizeof(buf), _("grafico: %d min   -   elegir en /sensores"),
                     seg / 60);
        }
    }
    set_txt(s_sn.pie, s_sn.txt_pie, sizeof(s_sn.txt_pie), buf);
}

/* -------------------------------------------------------------------------- */
/* Network                                                                     */
/* -------------------------------------------------------------------------- */

static void medir(void)
{
    if (s_sn.req > 0 || !s_sn.configurado) {
        return;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        return;
    }
    s_sn.retry_ms  = 0;
    s_sn.muestra_ms = (uint32_t)aos_hal_uptime_ms();
    s_sn.req = aos_hal_http_request("POST", s_sn.base, s_sn.hdr, s_sn.tpl,
                                    "application/json", SN_BUF_BYTES);
}

static void mirar_respuesta(void)
{
    if (s_sn.req <= 0) {
        return;
    }
    if (aos_hal_http_state(s_sn.req) == AOS_HTTP_BUSY) {
        return;
    }

    int estado = aos_hal_http_status(s_sn.req);
    if (aos_hal_http_state(s_sn.req) == AOS_HTTP_DONE) {
        const char *b = aos_hal_http_body(s_sn.req);
        int len = aos_hal_http_len(s_sn.req);
        int buenos = b ? sn_push_values(b, len, s_sn.s, s_sn.n) : 0;
        s_sn.ultimo_error = 0;
        if (buenos > 0) {
            s_sn.hubo = true;
        }
        aos_hal_log("sensores", "%d bytes, %d de %d con numero", len, buenos, s_sn.n);
    } else {
        s_sn.ultimo_error = estado;
        aos_hal_log("sensores", "fallo: %d", estado);
        if (estado == AOS_HTTP_ERR_SIN_HORA) {
            s_sn.retry_ms = (uint32_t)aos_hal_uptime_ms() + 3000;
        }
    }

    aos_hal_http_release(s_sn.req);
    s_sn.req = 0;
    pintar_graficos();
    pintar();
}

static void mirar_pref(void)
{
    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (ahora - s_sn.pref_ms < PREF_MS) {
        return;
    }
    s_sn.pref_ms = ahora;

    int32_t gen = 0;
    aos_hal_pref_get_i32(KEY_GEN, &gen);
    if (gen == s_sn.gen) {
        return;
    }
    aos_hal_log("sensores", "la lista cambio (gen %ld -> %ld)",
                (long)s_sn.gen, (long)gen);
    s_sn.gen = gen;
    /* Changing the list THROWS AWAY the rings, and that is right: the samples
     * belong to a sensor, not to a position, and keeping them would show the
     * old sensor's chart under the new name. It is the same lesson clima's
     * cached forecast left behind when the city changed. */
    cargar_config();
    construir();
    pintar();
    medir();
}

/* The RUNTIME's tick and not an lv_timer of our own: it is the only one that
 * goes on being called when the app moves to the background (aos_ui.c looks at
 * AOS_APP_FLAG_BACKGROUND to decide whose turn it is). An lv_timer would stay
 * alive too, but then there would be two mechanisms for the same thing and the
 * one that rules the life cycle is this one. */
static void tick(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    mirar_respuesta();
    mirar_pref();

    uint32_t ahora = (uint32_t)aos_hal_uptime_ms();
    if (s_sn.retry_ms && (int32_t)(ahora - s_sn.retry_ms) >= 0) {
        s_sn.retry_ms = 0;
        medir();
    } else if (s_sn.req <= 0 && ahora - s_sn.muestra_ms >= periodo_ms()) {
        medir();
    }
}

/* It came back to the foreground. While it was hidden it went on measuring but
 * drew nothing, so the number and the chart have to be brought up to date
 * BEFORE they are seen. */
static void mostrar(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    pintar_graficos();
    pintar();
}

/* -------------------------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_sn, 0, sizeof(s_sn));
    s_sn.root = root;

    lv_obj_set_style_bg_color(root, AOS_C_BG, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    aos_hal_pref_get_i32(KEY_GEN, &s_sn.gen);
    cargar_config();
    construir();
    pintar();
    medir();
    return &s_sn;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    /* With AOS_APP_FLAG_BACKGROUND the runtime does NOT call destroy() on
     * exit: it hides the root and the app goes on measuring. This only runs if
     * one day it is really closed. */
    if (s_sn.req > 0) {
        aos_hal_http_release(s_sn.req);
        s_sn.req = 0;
    }
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    memset(&s_sn, 0, sizeof(s_sn));
}

static bool sensores_init(aos_app_t *app)
{
    app->desc.id       = "aos.sensores";
    app->desc.name     = "Sensores";
    app->desc.icon     = "~";
    app->desc.icon_vec = AOS_ICON_CHART;
    app->desc.color_a  = 0x0A84FF;
    app->desc.color_b  = 0x063C74;
    app->desc.order    = 77;
    /* It goes on measuring with the screen off: without this the chart never
     * fills up, because the watch goes back to the face after thirty seconds.
     * The cost is one query a minute to Home Assistant from the first time the
     * app is opened. */
    app->desc.flags    = AOS_APP_FLAG_BACKGROUND;

    app->create  = create;
    app->destroy = destroy;
    app->show    = mostrar;
    app->tick    = tick;
    return true;
}

AOS_APP_ENTRY(sensores_init);
