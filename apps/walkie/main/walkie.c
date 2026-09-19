/*
 * WALKIE - push to talk between two watches (docs/LINK.md, v0.4.3)
 *
 * Half duplex, like the real thing, and for a reason of the hardware: the
 * speaker and the microphone are the same ES8311 and it does one at a time.
 * Hold the button and the microphone is open and every 29 ms of it goes out
 * as one frame on the fast channel: 464 samples at 16 kHz, IMA ADPCM at 4
 * bits a sample, 232 bytes plus the coder's state so a lost frame costs its
 * 29 ms and nothing after it. Let go and the speaker takes the codec back;
 * what arrives is decoded straight into the HAL's streaming speaker
 * (aos_hal_spk_*), whose task plays silence when nothing is queued so the
 * amplifier never has to wake up mid-word.
 *
 * 8.3 KB/s on the air, a seventh of what the fast channel carries. The cost
 * that matters is the switch: closing one side of the codec and opening the
 * other is about 200 ms, which is why there is a beep when the button is
 * ready and not before.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"
#include "wk_adpcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WK_PROTO        1
#define WK_RATE         16000
#define WK_FRAME        464                 /* samples per frame: 29 ms */
#define WK_BYTES        (WK_FRAME / 2)      /* ADPCM bytes per frame */
#define WK_TICK_MS      20
#define WK_STATE_MS     500
#define WK_LOST_MS      2500

/* A speech bubble with sound waves. */
static const uint8_t WALKIE_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_CENTER, -6,  0, 14, 34, 5, AIC_C_TEXT, 255),
    AIC_RECT(AIC_CENTER, -6, 24,  6,  8, 2, AIC_C_TEXT, 255),
    AIC_RING(30, 3, AIC_C_ACCENT, 200),
    AIC_RING(46, 3, AIC_C_ACCENT, 120),
    AIC_END
};

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;               /* 'V', WK_PROTO */
    uint16_t seq;
    int16_t  pred;                      /* the coder's state at the start of the frame */
    uint8_t  index;
    uint8_t  level;                     /* 0..100, for the other side's meter */
    uint8_t  data[WK_BYTES];
} wk_voice_t;

typedef struct __attribute__((packed)) {
    uint8_t type, proto;                /* 'S', WK_PROTO */
    uint8_t talking;
} wk_state_t;

enum { ST_LISTEN = 0, ST_TALK };

typedef struct {
    lv_timer_t *timer;
    lv_obj_t   *title, *status, *stats, *ptt, *ptt_lbl, *meter, *meter_fill, *hint;
    bool        up, have_partner, spk_open;
    uint8_t     partner_mac[6];
    char        pname[AOS_LINK_NAME_MAX + 1];
    int         state;
    bool        pressed;
    uint32_t    switch_ms;              /* when the codec was asked to switch */
    /* talking */
    int16_t     pcm[WK_FRAME];
    int         pcm_n;
    wk_adpcm_t  enc;
    uint16_t    seq;
    /* listening */
    wk_adpcm_t  dec;
    uint16_t    rx_seq;
    bool        rx_any;
    uint32_t    rx_ms, their_state_ms, state_ms;
    bool        they_talk;
    int         rx_level;
    uint32_t    frames_out, frames_in, frames_lost;
    uint32_t    stats_ms;
    char        shown[3][80];
} walkie_t;

static walkie_t s_w;

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static void set_text(int slot, lv_obj_t *lbl, const char *text)
{
    if (strncmp(s_w.shown[slot], text, sizeof s_w.shown[slot] - 1) == 0) return;
    snprintf(s_w.shown[slot], sizeof s_w.shown[slot], "%s", text);
    lv_label_set_text(lbl, text);
}

static void speaker_on(void)
{
    if (s_w.spk_open) return;
    s_w.spk_open = aos_hal_spk_open(WK_RATE);
    if (!s_w.spk_open) aos_hal_log("walkie", "speaker did not open");
}

static void speaker_off(void)
{
    if (!s_w.spk_open) return;
    aos_hal_spk_close();
    s_w.spk_open = false;
}

static void talk_start(void)
{
    if (s_w.state == ST_TALK) return;
    s_w.state = ST_TALK;
    s_w.switch_ms = now_ms();
    speaker_off();
    aos_hal_mic_open(WK_RATE);
    s_w.pcm_n = 0;
    memset(&s_w.enc, 0, sizeof s_w.enc);
    aos_hal_log("walkie", "talk");
}

static void talk_stop(void)
{
    if (s_w.state != ST_TALK) return;
    s_w.state = ST_LISTEN;
    s_w.switch_ms = now_ms();
    aos_hal_mic_close();
    speaker_on();
    aos_hal_log("walkie", "listen: %lu frames out", (unsigned long)s_w.frames_out);
}

static void send_state(void)
{
    wk_state_t st = { .type = 'S', .proto = WK_PROTO, .talking = s_w.state == ST_TALK };
    aos_hal_link_send_partner(&st, sizeof st);
    s_w.state_ms = now_ms();
}

static void send_frame(void)
{
    wk_voice_t v = { .type = 'V', .proto = WK_PROTO, .seq = s_w.seq++,
                     .pred = s_w.enc.pred, .index = s_w.enc.index,
                     .level = (uint8_t)aos_hal_mic_level() };
    wk_adpcm_encode(&s_w.enc, s_w.pcm, WK_FRAME, v.data);
    if (aos_hal_link_send_partner(&v, sizeof v)) s_w.frames_out++;
}

static void on_voice(const wk_voice_t *v)
{
    if (s_w.rx_any && v->seq != (uint16_t)(s_w.rx_seq + 1)) {
        uint16_t gap = (uint16_t)(v->seq - s_w.rx_seq - 1);
        if (gap < 100) s_w.frames_lost += gap;
    }
    s_w.rx_seq   = v->seq;
    s_w.rx_any   = true;
    s_w.rx_ms    = now_ms();
    s_w.rx_level = v->level;
    s_w.frames_in++;
    if (s_w.state != ST_LISTEN || !s_w.spk_open) return;   /* half duplex: talking wins */
    s_w.dec.pred  = v->pred;
    s_w.dec.index = v->index;
    int16_t pcm[WK_FRAME];
    wk_adpcm_decode(&s_w.dec, v->data, WK_FRAME, pcm);
    aos_hal_spk_write(pcm, WK_FRAME);
}

static void draw(uint32_t now)
{
    char buf[96];
    if (!s_w.up) {
        set_text(0, s_w.status, _("El enlace no arrancó: la radio tiene que estar encendida"));
        return;
    }
    if (!s_w.have_partner) {
        set_text(0, s_w.status, _("Sin pareja: apareá los relojes en Enlace"));
        return;
    }
    set_text(2, s_w.title, s_w.pname);
    bool they_here = s_w.their_state_ms && now - s_w.their_state_ms < WK_LOST_MS;
    bool they_talk = s_w.rx_any && now - s_w.rx_ms < 300;
    int level = 0;
    if (s_w.state == ST_TALK) {
        if (now - s_w.switch_ms < 250) {
            set_text(0, s_w.status, _("un momento..."));
        } else {
            set_text(0, s_w.status, _("HABLÁS"));
        }
        level = aos_hal_mic_level();
        lv_obj_set_style_bg_color(s_w.ptt, lv_color_hex(0xFF453A), 0);
        lv_label_set_text(s_w.ptt_lbl, _("soltá para escuchar"));
    } else {
        if (they_talk) {
            snprintf(buf, sizeof buf, "%s %s", s_w.pname, _("habla"));
            set_text(0, s_w.status, buf);
            level = s_w.rx_level;
        } else if (!they_here) {
            snprintf(buf, sizeof buf, "%s %s", _("Buscando a"), s_w.pname);
            set_text(0, s_w.status, buf);
        } else {
            snprintf(buf, sizeof buf, "%s %s", s_w.pname, _("escucha"));
            set_text(0, s_w.status, buf);
        }
        lv_obj_set_style_bg_color(s_w.ptt, lv_color_hex(they_talk ? 0x1C1C1E : 0x0A84FF), 0);
        lv_label_set_text(s_w.ptt_lbl, _("mantené para hablar"));
    }
    lv_obj_set_width(s_w.meter_fill, 4 + (AOS_SCREEN_W - 64 - 4) * level / 100);
    if (now - s_w.stats_ms >= 500) {
        s_w.stats_ms = now;
        snprintf(buf, sizeof buf, "%lu %s  ·  %lu %s  ·  %lu %s", (unsigned long)s_w.frames_out, _("enviados"),
                 (unsigned long)s_w.frames_in, _("recibidos"), (unsigned long)s_w.frames_lost, _("perdidos"));
        set_text(1, s_w.stats, buf);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    if (!s_w.up) return;
    uint32_t now = now_ms();
    if (!s_w.have_partner) {
        aos_link_partner_t p;
        if (aos_hal_link_partner(&p) && p.valid) {
            s_w.have_partner = true;
            memcpy(s_w.partner_mac, p.mac, 6);
            snprintf(s_w.pname, sizeof s_w.pname, "%s", p.name[0] ? p.name : "?");
            speaker_on();
        }
    } else {
        aos_link_frame_t f;
        while (aos_hal_link_recv(&f) > 0) {
            if (f.len < 2 || f.data[1] != WK_PROTO || memcmp(f.mac, s_w.partner_mac, 6) != 0) continue;
            if (f.data[0] == 'V' && f.len >= sizeof(wk_voice_t)) {
                on_voice((const wk_voice_t *)f.data);
            } else if (f.data[0] == 'S' && f.len >= sizeof(wk_state_t)) {
                s_w.their_state_ms = now;
                s_w.they_talk = ((const wk_state_t *)f.data)->talking;
            }
        }
        if (s_w.state == ST_LISTEN && !s_w.spk_open && now - s_w.switch_ms > 150) {
            speaker_on();                   /* the codec was still the microphone's: again */
            if (!s_w.spk_open) s_w.switch_ms = now;
        }
        if (s_w.state == ST_TALK) {
            /* whatever the microphone has, in whole frames */
            for (;;) {
                int got = aos_hal_mic_read(s_w.pcm + s_w.pcm_n, WK_FRAME - s_w.pcm_n);
                if (got <= 0) break;
                s_w.pcm_n += got;
                if (s_w.pcm_n < WK_FRAME) break;
                send_frame();
                s_w.pcm_n = 0;
            }
        }
        if (now - s_w.state_ms >= WK_STATE_MS) send_state();
    }
    draw(now);
}

static void ptt_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        talk_start();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        talk_stop();
    }
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_w, 0, sizeof s_w);
    lv_obj_t *page = aos_page(root);
    s_w.title = aos_label(page, _("Walkie"), aos_font_title, AOS_C_TEXT);
    lv_obj_align(s_w.title, LV_ALIGN_TOP_MID, 0, 28);
    s_w.status = aos_label(page, "", aos_font_body, AOS_C_ACCENT);
    lv_obj_set_width(s_w.status, AOS_SCREEN_W - 40);
    lv_label_set_long_mode(s_w.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_w.status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_w.status, LV_ALIGN_TOP_MID, 0, 66);

    /* the meter: a bar that fills with whoever is speaking */
    s_w.meter = lv_obj_create(page);
    lv_obj_remove_style_all(s_w.meter);
    lv_obj_set_size(s_w.meter, AOS_SCREEN_W - 64, 10);
    lv_obj_align(s_w.meter, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_radius(s_w.meter, 5, 0);
    lv_obj_set_style_bg_color(s_w.meter, lv_color_hex(0x2C2C2E), 0);
    lv_obj_set_style_bg_opa(s_w.meter, LV_OPA_COVER, 0);
    s_w.meter_fill = lv_obj_create(s_w.meter);
    lv_obj_remove_style_all(s_w.meter_fill);
    lv_obj_set_size(s_w.meter_fill, 4, 10);
    lv_obj_set_style_radius(s_w.meter_fill, 5, 0);
    lv_obj_set_style_bg_color(s_w.meter_fill, lv_color_hex(0x30D158), 0);
    lv_obj_set_style_bg_opa(s_w.meter_fill, LV_OPA_COVER, 0);

    /* the button: big, round, held */
    s_w.ptt = lv_obj_create(page);
    lv_obj_remove_style_all(s_w.ptt);
    lv_obj_set_size(s_w.ptt, 220, 220);
    lv_obj_align(s_w.ptt, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_set_style_radius(s_w.ptt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_w.ptt, lv_color_hex(0x0A84FF), 0);
    lv_obj_set_style_bg_opa(s_w.ptt, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(s_w.ptt, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_add_flag(s_w.ptt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_w.ptt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_w.ptt, ptt_cb, LV_EVENT_ALL, NULL);
    s_w.ptt_lbl = aos_label(s_w.ptt, _("mantené para hablar"), aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(s_w.ptt_lbl, 180);
    lv_label_set_long_mode(s_w.ptt_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_w.ptt_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_w.ptt_lbl);
    lv_obj_remove_flag(s_w.ptt_lbl, LV_OBJ_FLAG_CLICKABLE);

    s_w.stats = aos_label(page, "", aos_font_small, AOS_C_DIM);
    lv_obj_set_width(s_w.stats, AOS_SCREEN_W - 24);
    lv_obj_set_style_text_align(s_w.stats, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_w.stats, LV_ALIGN_BOTTOM_MID, 0, -14);

    s_w.up = aos_hal_link_start();
    if (s_w.up) {
        aos_hal_link_offer("walkie");
    }
    s_w.timer = lv_timer_create(tick, WK_TICK_MS, NULL);
    draw(now_ms());
    return &s_w;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    if (s_w.timer) {
        lv_timer_delete(s_w.timer);
        s_w.timer = NULL;
    }
    if (s_w.state == ST_TALK) {
        aos_hal_mic_close();
    }
    speaker_off();
    if (s_w.up) {
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }
}

static bool walkie_init(aos_app_t *app)
{
    app->desc.id       = "aos.walkie";
    app->desc.name     = "Walkie";
    app->desc.icon     = LV_SYMBOL_AUDIO;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0xFF9F0A;
    app->desc.color_b  = 0x7A4A00;
    app->desc.order    = 129;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_NO_SWIPE;
    aos_icon_set_ops(app, WALKIE_ICON, sizeof WALKIE_ICON);
    app->create  = create;
    app->destroy = destroy;
    return true;
}

AOS_APP_ENTRY(walkie_init);
