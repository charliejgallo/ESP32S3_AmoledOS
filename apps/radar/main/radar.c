/*
 * RADAR - where is the other watch (docs/LINK.md, v0.4.2)
 *
 * Two answers to the same question, one cheap and one exact:
 *
 *  - Signal strength. Both watches ping each other ten times a second over
 *    the link's fast channel; every ping that arrives carries the RSSI the
 *    radio saw it at. A smoothed RSSI, a path-loss model (d = 10^((P0 -
 *    rssi) / 10n), P0 calibrated by holding the watches one metre apart and
 *    tapping the button) and the partner becomes a dot on the rings. RSSI is
 *    a poor ruler -a hand over the antenna is worth three metres- but it is
 *    free, it works on every board, and it says "closer" and "further" well.
 *
 *  - Time of flight. FTM (IEEE 802.11mc): the watch with the lower MAC
 *    brings its softAP up as FTM responder and tells the other its BSSID and
 *    channel in the pings; the other runs a 16-frame session every second and
 *    a half and gets a distance in centimetres from the driver, which it
 *    sends back in its own pings so both screens show it. Behind
 *    CONFIG_ESP_WIFI_FTM_ENABLE; the simulator says "sin soporte".
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RD_PROTO        1
#define RD_TICK_MS      100
#define RD_PING_MS      100
#define RD_LOST_MS      2000            /* no ping for this long: out of reach */
#define RD_FTM_MS       1500            /* between sessions */
#define RD_FTM_FRAMES   16
#define RD_P0_DEFAULT   (-45)           /* RSSI at one metre, until calibrated */
#define RD_N10          22              /* path-loss exponent x10: 2.2, indoors with a body around */

#define RING_CX         184
#define RING_CY         226
#define RING_R          100             /* the outer ring */
#define RSSI_NEAR       (-30)           /* at the centre */
#define RSSI_FAR        (-90)           /* at the outer ring */

/* Rings and a dot: the radar. */
static const uint8_t RADAR_ICON[] = {
    AIC_HEADER,
    AIC_RING(58, 3, AIC_C_TEXT, 110),
    AIC_RING(38, 3, AIC_C_TEXT, 190),
    AIC_RECT(AIC_CENTER,  0,   0, 10, 10, AIC_CIRCLE, AIC_C_ACCENT, 255),
    AIC_RECT(AIC_CENTER, 13, -15,  9,  9, AIC_CIRCLE, AIC_C_GREEN, 255),
    AIC_END
};

enum { ROLE_NONE = 0, ROLE_RESPONDER = 1, ROLE_INITIATOR = 2 };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;               /* 'R', RD_PROTO */
    int8_t   rssi_of_you;               /* what I see of your pings, smoothed */
    uint8_t  role;
    uint8_t  ap_mac[6];                 /* responder: my softAP's BSSID */
    uint8_t  ap_ch;
    uint16_t ftm_cm;                    /* initiator: my last measurement, 0xFFFF none */
    uint8_t  ftm_state;                 /* 0 nothing yet, 1 valid, 2 unsupported here, 3 failing */
} rd_ping_t;

typedef struct {
    lv_obj_t   *root;
    lv_timer_t *timer;
    lv_obj_t   *title, *dist, *rssi_lbl, *ftm_lbl, *hint;
    lv_obj_t   *ring[3], *me, *dot, *pulse, *cal;
    bool        up, have_partner;
    uint8_t     partner_mac[6];
    char        pname[AOS_LINK_NAME_MAX + 1];
    uint8_t     role;
    /* signal */
    float       rssi_f;                 /* smoothed */
    int8_t      rssi_last, rssi_theirs;
    uint32_t    rx_ms, tx_ms, pings_in, pings_out;
    int         p0;                     /* RSSI at one metre */
    int         ftm0;                   /* what FTM reads at one metre, minus 100: the offset */
    /* FTM */
    bool        ftm_ok;                 /* this board can */
    uint8_t     resp_mac[6], resp_ch;   /* what the responder told me */
    bool        resp_known;
    uint32_t    ftm_next_ms;
    uint16_t    ftm_cm;                 /* 0xFFFF none */
    uint8_t     ftm_state;
    uint16_t    ftm_cm_theirs;
    uint8_t     ftm_state_theirs;
    uint32_t    ftm_sessions, ftm_failures, ftm_raw_cm;
    uint16_t    ftm_hist[5];            /* the last sessions, averaged: one is 15 cm of RTT quantum */
    uint8_t     ftm_hist_n, ftm_hist_i;
    /* animation */
    uint32_t    pulse_ms;
    uint32_t    log_ms;
    char        shown[4][64];
} radar_t;

static radar_t s_r;

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

/* d = 10^((P0 - rssi) / (10 n)), in centimetres; capped so a dead spot does
 * not print a kilometre. */
static int rssi_to_cm(float rssi)
{
    float x = ((float)s_r.p0 - rssi) / (float)RD_N10;
    if (x > 2.0f) x = 2.0f;             /* 100 m */
    if (x < -1.0f) x = -1.0f;
    return (int)(powf(10.0f, x) * 100.0f);
}

static void set_text(int slot, lv_obj_t *lbl, const char *text)
{
    if (strncmp(s_r.shown[slot], text, sizeof s_r.shown[slot] - 1) == 0) return;
    snprintf(s_r.shown[slot], sizeof s_r.shown[slot], "%s", text);
    lv_label_set_text(lbl, text);
}

static void send_ping(void)
{
    rd_ping_t p = { .type = 'R', .proto = RD_PROTO,
                    .rssi_of_you = (int8_t)s_r.rssi_f, .role = s_r.role,
                    .ftm_cm = s_r.ftm_cm, .ftm_state = s_r.ftm_state };
    if (s_r.role == ROLE_RESPONDER) {
        aos_hal_ftm_responder_info(p.ap_mac, &p.ap_ch);
    }
    if (aos_hal_link_send_partner(&p, sizeof p)) {
        s_r.pings_out++;
    }
    s_r.tx_ms = now_ms();
}

static void on_ping(const aos_link_frame_t *f)
{
    const rd_ping_t *p = (const rd_ping_t *)f->data;
    if (s_r.pings_in == 0) {
        s_r.rssi_f = (float)f->rssi;
    } else {
        s_r.rssi_f += ((float)f->rssi - s_r.rssi_f) * 0.2f;
    }
    s_r.rssi_last   = f->rssi;
    s_r.rssi_theirs = p->rssi_of_you;
    s_r.rx_ms       = now_ms();
    s_r.pings_in++;
    if (p->role == ROLE_RESPONDER && (p->ap_mac[0] | p->ap_mac[1] | p->ap_mac[2] |
                                      p->ap_mac[3] | p->ap_mac[4] | p->ap_mac[5])) {
        memcpy(s_r.resp_mac, p->ap_mac, 6);
        s_r.resp_ch    = p->ap_ch;
        s_r.resp_known = true;
    }
    s_r.ftm_cm_theirs    = p->ftm_cm;
    s_r.ftm_state_theirs = p->ftm_state;
}

static void decide_role(void)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    bool lower = memcmp(st.own_mac, s_r.partner_mac, 6) < 0;
    s_r.role = lower ? ROLE_RESPONDER : ROLE_INITIATOR;
    s_r.ftm_ok = aos_hal_ftm_supported();
    if (!s_r.ftm_ok) {
        s_r.ftm_state = 2;
    } else if (s_r.role == ROLE_RESPONDER) {
        if (!aos_hal_ftm_responder(true)) {
            aos_hal_log("radar", "ftm responder did not come up");
            s_r.ftm_state = 3;
        }
    }
    aos_hal_log("radar", "role: %s, ftm %s", lower ? "responder" : "initiator",
                s_r.ftm_ok ? "yes" : "no");
}

static void ftm_tick(uint32_t now)
{
    if (s_r.role != ROLE_INITIATOR || !s_r.ftm_ok || !s_r.resp_known) return;
    aos_ftm_result_t r;
    aos_hal_ftm_result(&r);
    if (r.valid && r.sessions != s_r.ftm_sessions) {
        s_r.ftm_sessions = r.sessions;
        s_r.ftm_raw_cm = r.dist_cm;
        /* the driver's number carries a fixed offset of about a metre on
         * these boards (side by side they read 90-135 cm): the calibration
         * at one metre takes it out */
        /* one session is quantised to 1 ns of RTT (15 cm) and jumps by
         * three or four of those side by side: the mean of the last five
         * sessions (7.5 s) is what the screen shows */
        s_r.ftm_hist[s_r.ftm_hist_i] = r.dist_cm > 0xFFFE ? 0xFFFE : (uint16_t)r.dist_cm;
        s_r.ftm_hist_i = (uint8_t)((s_r.ftm_hist_i + 1) % 5);
        if (s_r.ftm_hist_n < 5) s_r.ftm_hist_n++;
        uint32_t sum = 0;
        for (int i = 0; i < s_r.ftm_hist_n; i++) sum += s_r.ftm_hist[i];
        s_r.ftm_raw_cm = sum / s_r.ftm_hist_n;
        int cm = (int)s_r.ftm_raw_cm - s_r.ftm0;
        if (cm < 0) cm = 0;
        s_r.ftm_cm    = cm > 0xFFFE ? 0xFFFE : (uint16_t)cm;
        s_r.ftm_state = 1;
    }
    if (r.failures != s_r.ftm_failures) {
        s_r.ftm_failures = r.failures;
        if (s_r.ftm_state != 1) s_r.ftm_state = 3;
    }
    if (!r.busy && (int32_t)(now - s_r.ftm_next_ms) >= 0) {
        s_r.ftm_next_ms = now + RD_FTM_MS;
        aos_hal_ftm_measure(s_r.resp_mac, s_r.resp_ch, RD_FTM_FRAMES);
    }
}

static void draw(uint32_t now)
{
    char buf[80];
    bool seen = s_r.pings_in && now - s_r.rx_ms < RD_LOST_MS;

    if (!s_r.up) {
        set_text(0, s_r.hint, _("El enlace no arrancó: la radio tiene que estar encendida"));
        return;
    }
    if (!s_r.have_partner) {
        set_text(0, s_r.hint, _("Sin pareja: apareá los relojes en Enlace"));
        return;
    }
    set_text(3, s_r.title, s_r.pname);

    if (!seen) {
        snprintf(buf, sizeof buf, "%s %s", _("Buscando a"), s_r.pname);
        set_text(0, s_r.hint, buf);
        set_text(1, s_r.dist, "--");
        set_text(2, s_r.rssi_lbl, "");
        lv_obj_add_flag(s_r.dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_r.pulse, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_text(0, s_r.hint, "");
        int cm = rssi_to_cm(s_r.rssi_f);
        /* what the big number is: the time of flight when there is one
         * fresh, the signal's guess otherwise */
        uint16_t ftm = s_r.role == ROLE_INITIATOR ? s_r.ftm_cm : s_r.ftm_cm_theirs;
        uint8_t  fst = s_r.role == ROLE_INITIATOR ? s_r.ftm_state : s_r.ftm_state_theirs;
        if (fst == 1 && ftm != 0xFFFF) {
            snprintf(buf, sizeof buf, "%u.%u m", ftm / 100, (ftm % 100) / 10);
        } else {
            snprintf(buf, sizeof buf, "~%d.%d m", cm / 100, (cm % 100) / 10);
        }
        set_text(1, s_r.dist, buf);
        snprintf(buf, sizeof buf, "%d dBm  ·  %s %d dBm", (int)s_r.rssi_f, _("te ve a"), s_r.rssi_theirs);
        set_text(2, s_r.rssi_lbl, buf);

        /* the dot: centre at RSSI_NEAR, outer ring at RSSI_FAR, twelve o'clock */
        float t = ((float)RSSI_NEAR - s_r.rssi_f) / (float)(RSSI_NEAR - RSSI_FAR);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        int r = (int)(t * (float)RING_R);
        lv_obj_remove_flag(s_r.dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_r.dot, RING_CX - 9, RING_CY - r - 9);
        /* a pulse that grows from me to the dot every second */
        uint32_t ph = (now - s_r.pulse_ms) % 1000;
        int pr = 6 + (int)((uint32_t)r * ph / 1000);
        lv_obj_remove_flag(s_r.pulse, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(s_r.pulse, pr * 2, pr * 2);
        lv_obj_set_pos(s_r.pulse, RING_CX - pr, RING_CY - pr);
        lv_obj_set_style_border_opa(s_r.pulse, (lv_opa_t)(200 - ph * 180 / 1000), 0);
    }

    /* the FTM line */
    if (!s_r.ftm_ok) {
        snprintf(buf, sizeof buf, "FTM: %s", _("sin soporte en esta placa"));
    } else if (s_r.role == ROLE_RESPONDER) {
        if (s_r.ftm_state_theirs == 1 && s_r.ftm_cm_theirs != 0xFFFF) {
            snprintf(buf, sizeof buf, "FTM: %u cm  ·  %s", s_r.ftm_cm_theirs, _("respondo"));
        } else if (s_r.ftm_state_theirs == 2) {
            snprintf(buf, sizeof buf, "FTM: %s", _("el otro reloj no lo tiene"));
        } else {
            snprintf(buf, sizeof buf, "FTM: %s", _("respondo, esperando la medida"));
        }
    } else if (!s_r.resp_known) {
        snprintf(buf, sizeof buf, "FTM: %s", _("esperando al respondedor"));
    } else if (s_r.ftm_state == 1) {
        snprintf(buf, sizeof buf, "FTM: %u cm (%s %lu)  ·  %lu ok / %lu mal", s_r.ftm_cm, _("crudo"),
                 (unsigned long)s_r.ftm_raw_cm, (unsigned long)s_r.ftm_sessions, (unsigned long)s_r.ftm_failures);
    } else if (s_r.ftm_state == 3) {
        snprintf(buf, sizeof buf, "FTM: %s (%lu)", _("sin respuesta"), (unsigned long)s_r.ftm_failures);
    } else {
        snprintf(buf, sizeof buf, "FTM: %s", _("midiendo..."));
    }
    lv_label_set_text(s_r.ftm_lbl, buf);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (!s_r.up) return;
    uint32_t now = now_ms();
    if (!s_r.have_partner) {
        aos_link_partner_t p;
        if (aos_hal_link_partner(&p) && p.valid) {
            s_r.have_partner = true;
            memcpy(s_r.partner_mac, p.mac, 6);
            snprintf(s_r.pname, sizeof s_r.pname, "%s", p.name[0] ? p.name : "?");
            decide_role();
        }
    } else {
        aos_link_frame_t f;
        while (aos_hal_link_recv(&f) > 0) {
            if (f.len >= sizeof(rd_ping_t) && f.data[0] == 'R' && f.data[1] == RD_PROTO &&
                memcmp(f.mac, s_r.partner_mac, 6) == 0) {
                on_ping(&f);
            }
        }
        if (now - s_r.tx_ms >= RD_PING_MS) send_ping();
        ftm_tick(now);
        if (now - s_r.log_ms >= 5000) {
            s_r.log_ms = now;
            aos_hal_log("radar", "rssi %d (they see %d) ~%d cm, ftm %u cm state %u, pings %lu in / %lu out",
                        (int)s_r.rssi_f, s_r.rssi_theirs, rssi_to_cm(s_r.rssi_f),
                        s_r.role == ROLE_INITIATOR ? s_r.ftm_cm : s_r.ftm_cm_theirs,
                        s_r.role == ROLE_INITIATOR ? s_r.ftm_state : s_r.ftm_state_theirs,
                        (unsigned long)s_r.pings_in, (unsigned long)s_r.pings_out);
        }
    }
    draw(now);
}

static void cal_cb(lv_event_t *e)
{
    (void)e;
    if (!s_r.pings_in || now_ms() - s_r.rx_ms > RD_LOST_MS) return;
    s_r.p0 = (int)s_r.rssi_f;
    aos_hal_pref_set_i32("radar_p0", s_r.p0);
    char buf[64];
    if (s_r.role == ROLE_INITIATOR && s_r.ftm_state == 1) {
        s_r.ftm0 = (int)s_r.ftm_raw_cm - 100;
        aos_hal_pref_set_i32("radar_ftm0", s_r.ftm0);
        snprintf(buf, sizeof buf, "%s: %d dBm, FTM %+d cm", _("Un metro"), s_r.p0, -s_r.ftm0);
    } else {
        snprintf(buf, sizeof buf, "%s: %d dBm", _("Un metro"), s_r.p0);
    }
    aos_ui_toast(buf, 1500);
    aos_hal_beep(900, 40);
}

static lv_obj_t *circle(lv_obj_t *parent, int r, uint32_t color, lv_opa_t fill, int border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_pos(o, RING_CX - r, RING_CY - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, fill, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_border_opa(o, border ? 120 : 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_r, 0, sizeof s_r);
    s_r.root = root;
    s_r.ftm_cm = 0xFFFF;
    s_r.ftm_cm_theirs = 0xFFFF;
    int32_t v;
    s_r.p0 = aos_hal_pref_get_i32("radar_p0", &v) && v < 0 && v > -100 ? (int)v : RD_P0_DEFAULT;
    s_r.ftm0 = aos_hal_pref_get_i32("radar_ftm0", &v) && v > -1000 && v < 1000 ? (int)v : 0;

    lv_obj_t *page = aos_page(root);
    s_r.title = aos_label(page, _("Radar"), aos_font_title, AOS_C_TEXT);
    lv_obj_align(s_r.title, LV_ALIGN_TOP_MID, 0, 26);
    s_r.dist = aos_label(page, "--", aos_font_title, AOS_C_ACCENT);
    lv_obj_align(s_r.dist, LV_ALIGN_TOP_MID, 0, 58);
    s_r.rssi_lbl = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(s_r.rssi_lbl, LV_ALIGN_TOP_MID, 0, 94);

    for (int i = 0; i < 3; i++) {
        s_r.ring[i] = circle(page, RING_R - i * 33, 0x0A84FF, 0, 2);
    }
    s_r.pulse = circle(page, 6, 0x40C8E0, 0, 2);
    lv_obj_add_flag(s_r.pulse, LV_OBJ_FLAG_HIDDEN);
    s_r.me  = circle(page, 6, 0xFFFFFF, LV_OPA_COVER, 0);
    s_r.dot = circle(page, 9, 0x30D158, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_r.dot, LV_OBJ_FLAG_HIDDEN);

    s_r.ftm_lbl = aos_label(page, "", aos_font_small, AOS_C_TEAL);
    lv_obj_set_width(s_r.ftm_lbl, AOS_SCREEN_W - 32);
    lv_obj_set_style_text_align(s_r.ftm_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_r.ftm_lbl, LV_ALIGN_TOP_MID, 0, 334);
    s_r.hint = aos_label(page, "", aos_font_small, AOS_C_ACCENT);
    lv_obj_set_width(s_r.hint, AOS_SCREEN_W - 48);
    lv_label_set_long_mode(s_r.hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_r.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_r.hint, LV_ALIGN_TOP_MID, 0, 118);

    s_r.cal = aos_button(page, _("Calibrar a 1 m"), AOS_C_CARD2, cal_cb, NULL);
    lv_obj_align(s_r.cal, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_height(s_r.cal, 34);

    s_r.up = aos_hal_link_start();
    if (s_r.up) {
        aos_hal_link_offer("radar");
    }
    s_r.pulse_ms = now_ms();
    s_r.timer = lv_timer_create(tick, RD_TICK_MS, NULL);
    draw(now_ms());
    return &s_r;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_r.timer) {
        lv_timer_delete(s_r.timer);
        s_r.timer = NULL;
    }
    if (s_r.role == ROLE_RESPONDER) {
        aos_hal_ftm_responder(false);
    }
    if (s_r.up) {
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }
}

static bool radar_init(aos_app_t *app)
{
    app->desc.id       = "aos.radar";
    app->desc.name     = "Radar";
    app->desc.icon     = LV_SYMBOL_GPS;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0x40C8E0;
    app->desc.color_b  = 0x0A3C50;
    app->desc.order    = 128;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE;
    aos_icon_set_ops(app, RADAR_ICON, sizeof RADAR_ICON);
    app->create  = create;
    app->destroy = destroy;
    return true;
}

AOS_APP_ENTRY(radar_init);
