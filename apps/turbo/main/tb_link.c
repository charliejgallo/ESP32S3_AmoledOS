/*
 * TURBO - racing the paired watch (see tb_app.h)
 *
 * Both watches race the same stage at the same time; each drives its own car
 * and sees the other's as a ghost (no collisions), from its position sent 15
 * times a second. The winner is the lower time. The traffic comes from the
 * same seed on both, so it starts out the same; after that each watch's
 * traffic reacts to its own car, which is fine for a ghost.
 *
 *   HELLO   (fast, every 400 ms in the lobby)  nonce, car, paint, the stage
 *                                              this watch picked, its records
 *   START   (reliable, host -> guest)          stage, difficulty, both nonces
 *   READY   (reliable, guest -> host)          its car and paint
 *   LOADED  (reliable, both)                   the stage is built: when both
 *                                              have it, the countdown starts
 *   POS     (fast, 15 Hz while racing)         distance, offset, speed
 *   RESULT  (reliable)                         finished or not, time, progress
 *   BYE     (fast, three times)
 *
 * The lower MAC is the host and its stage is the one raced. The records in
 * HELLO are kept (tb_rb*), so the stage list shows the rival's times even
 * when the two never race at once. START can arrive before the first HELLO
 * (Neon Snakes' trap): the partner's MAC comes from aos_hal_link_partner()
 * and the host's nonce from START itself.
 */
#include "tb_app.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTO       1
#define LINK_APP    "turbo"

enum { MSG_HELLO = 1, MSG_START, MSG_READY, MSG_LOADED, MSG_POS, MSG_RESULT, MSG_BYE };
enum { LK_OFF = 0, LK_LOBBY, LK_LOADING, LK_PLAY, LK_GONE };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
    uint8_t  mac[6];
    uint8_t  car, paint, stage;
    uint16_t best[STAGE_N];
} msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce, guest_nonce;
    uint8_t  stage, diff, car, paint;
} msg_start_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce;
    uint8_t  car, paint;
} msg_ready_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
} msg_loaded_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    float    z, x, v;
    uint8_t  state;
} msg_pos_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  finished;
    float    elapsed, progress;
} msg_result_t;

static bool     s_role_known, s_started_link, s_me_loaded, s_peer_loaded;
static uint32_t s_hello_ms, s_away_ms;
static uint8_t  s_their_mac[6];

static void send_hello(app_t *a)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    msg_hello_t h = { .type = MSG_HELLO, .proto = PROTO, .nonce = a->link_nonce,
                      .car = (uint8_t)a->car, .paint = a->paint[a->car], .stage = (uint8_t)a->stage };
    memcpy(h.mac, st.own_mac, 6);
    for (int i = 0; i < STAGE_N; i++) h.best[i] = (uint16_t)(a->best[i] > 65535 ? 65535 : a->best[i]);
    aos_hal_link_send_partner(&h, sizeof h);
}

bool tbl_available(app_t *a, char *name, int n)
{
    (void)a;
    if (!aos_hal_link_running()) s_started_link = aos_hal_link_start();
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(name, (size_t)n, "%s", p.name[0] ? p.name : "?");
        return true;
    }
#ifdef AOS_SIM_BUILTIN
    const char *e = getenv("TB_LINK");
    if (e && e[0]) {
        snprintf(name, (size_t)n, "sim");
        return true;
    }
#endif
    return false;
}

static void lobby_text(app_t *a)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%s %s...\n\n%s", _("Esperando a"), a->partner[0] ? a->partner : "?",
             _("El tramo lo elige el reloj anfitrión."));
    lv_label_set_text(a->lobby_lbl, buf);
}

void tbl_begin(app_t *a)
{
    if (!aos_hal_link_running() && !aos_hal_link_start()) {
        tba_toast(a, _("El enlace no arrancó"));
        return;
    }
    aos_hal_link_offer(LINK_APP);
    aos_hal_link_reliable_reset();
    a->link_on = true;
    a->link_state = LK_LOBBY;
    a->link_nonce = ((uint32_t)aos_hal_uptime_ms() * 2654435761u) | 1u;
    a->link_peer_nonce = 0;
    a->link_ms = (uint32_t)aos_hal_uptime_ms();
    a->rival_done = false;
    s_role_known = false;
    s_me_loaded = s_peer_loaded = false;
    s_hello_ms = 0;
    a->partner[0] = 0;
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(a->partner, sizeof a->partner, "%s", p.name);
        memcpy(s_their_mac, p.mac, 6);
    }
    /* the role comes from the MAC in the first HELLO (or START says it) */
    a->is_host = false;
#ifdef AOS_SIM_BUILTIN
    if (!a->partner[0]) snprintf(a->partner, sizeof a->partner, "sim");
#endif
    lobby_text(a);
    tba_set_state(a, ST_LOBBY);
}

void tbl_end(app_t *a)
{
    if (a->link_on || s_started_link) {
        uint8_t bye[2] = { MSG_BYE, PROTO };
        for (int i = 0; i < 3; i++) aos_hal_link_send_partner(bye, sizeof bye);
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }
    a->link_on = false;
    s_started_link = false;
    a->link_state = LK_OFF;
    a->game.rival_on = false;
}

static void gone(app_t *a, const char *fmt)
{
    if (a->link_state == LK_OFF || a->link_state == LK_GONE) return;
    char buf[80];
    snprintf(buf, sizeof buf, fmt, a->partner[0] ? a->partner : "?");
    tba_toast(a, buf);
    /* in a race, keep racing alone; in the lobby, back to the menu */
    if (a->link_state == LK_LOBBY || a->link_state == LK_LOADING) {
        a->link_state = LK_GONE;
        tbl_end(a);
        if (a->state == ST_LOBBY) tba_set_state(a, ST_MENU);
        return;
    }
    a->link_state = LK_GONE;
    a->game.rival_on = false;
    a->hs.rival = false;
}

static void begin_loading(app_t *a, int stage)
{
    aos_hal_log("turbo", "link: %s, loading stage %d (%u)", a->is_host ? "host" : "guest", stage,
                (unsigned)aos_hal_uptime_ms());
    a->link_state = LK_LOADING;
    s_me_loaded = s_peer_loaded = false;
    s_away_ms = (uint32_t)aos_hal_uptime_ms();
    a->mode = MODE_LINK;
    tba_race_start(a, stage);
}

static void handle(app_t *a, const uint8_t *d, int len, bool reliable)
{
    if (len < 2 || d[1] != PROTO) return;
    switch (d[0]) {
    case MSG_HELLO: {
        if (len < (int)sizeof(msg_hello_t)) return;
        const msg_hello_t *h = (const msg_hello_t *)d;
        /* its records, always */
        bool changed = false;
        for (int i = 0; i < STAGE_N; i++) {
            if (h->best[i] && h->best[i] != a->rival_best[i]) {
                a->rival_best[i] = h->best[i];
                changed = true;
            }
        }
        if (a->partner[0] && strcmp(a->rival_name, a->partner)) {
            snprintf(a->rival_name, sizeof a->rival_name, "%s", a->partner);
            changed = true;
        }
        if (changed) tba_prefs_save(a);
        if (a->link_state == LK_PLAY && a->link_peer_nonce && h->nonce != a->link_peer_nonce) {
            gone(a, _("%s salió de la carrera"));
            return;
        }
        if (a->link_state != LK_LOBBY) return;
        bool fresh = h->nonce != a->link_peer_nonce;
        a->link_peer_nonce = h->nonce;
        a->rival_car = h->car;
        a->rival_paint = h->paint;
        if (!s_role_known) {
            /* the lower MAC hosts; the same MAC (never on the board) falls
             * back to the lower nonce */
            aos_link_stats_t st;
            aos_hal_link_stats(&st);
            int c = memcmp(st.own_mac, h->mac, 6);
            a->is_host = c < 0 || (c == 0 && a->link_nonce < h->nonce);
            s_role_known = true;
            memcpy(s_their_mac, h->mac, 6);
            send_hello(a);
        }
        if (a->is_host && fresh) {
            msg_start_t m = { .type = MSG_START, .proto = PROTO, .host_nonce = a->link_nonce,
                              .guest_nonce = h->nonce, .stage = (uint8_t)a->stage, .diff = (uint8_t)a->diff,
                              .car = (uint8_t)a->car, .paint = a->paint[a->car] };
            aos_hal_link_send_reliable(&m, sizeof m);
        }
        break;
    }
    case MSG_START: {
        if (!reliable || a->link_state != LK_LOBBY || len < (int)sizeof(msg_start_t)) return;
        const msg_start_t *m = (const msg_start_t *)d;
        if (m->guest_nonce != a->link_nonce) return;
        a->is_host = false;
        s_role_known = true;
        a->link_peer_nonce = m->host_nonce;
        a->rival_car = m->car;
        a->rival_paint = m->paint;
        a->diff = m->diff < DIFF_N ? m->diff : DIFF_NORMAL;
        msg_ready_t r = { .type = MSG_READY, .proto = PROTO, .host_nonce = m->host_nonce,
                          .car = (uint8_t)a->car, .paint = a->paint[a->car] };
        aos_hal_link_send_reliable(&r, sizeof r);
        begin_loading(a, m->stage < STAGE_N ? m->stage : 0);
        break;
    }
    case MSG_READY: {
        if (!reliable || !a->is_host || a->link_state != LK_LOBBY || len < (int)sizeof(msg_ready_t)) return;
        const msg_ready_t *r = (const msg_ready_t *)d;
        if (r->host_nonce != a->link_nonce) return;
        a->rival_car = r->car;
        a->rival_paint = r->paint;
        begin_loading(a, a->stage);
        break;
    }
    case MSG_LOADED:
        if (reliable) {
            s_peer_loaded = true;
            aos_hal_log("turbo", "link: %s has the stage (%u)", a->partner, (unsigned)aos_hal_uptime_ms());
        }
        break;
    case MSG_POS: {
        if (len < (int)sizeof(msg_pos_t) || a->link_state != LK_PLAY) return;
        const msg_pos_t *p = (const msg_pos_t *)d;
        a->game.rival_z = p->z;
        a->game.rival_x = p->x;
        a->game.rival_v = p->v;
        float fin = (float)a->trk.cp_seg[a->trk.ncp - 1] * TB_SEG_LEN;
        float pr = fin > 0 ? p->z / fin : 0;
        a->hs.rival_prog = pr < 0 ? 0 : (pr > 1 ? 1 : pr);
        s_away_ms = (uint32_t)aos_hal_uptime_ms();
        break;
    }
    case MSG_RESULT: {
        if (!reliable || len < (int)sizeof(msg_result_t)) return;
        const msg_result_t *r = (const msg_result_t *)d;
        a->rival_done = true;
        a->rival_finished = r->finished != 0;
        a->rival_time = r->elapsed;
        a->rival_dist = r->progress;
        break;
    }
    case MSG_BYE:
        gone(a, _("%s salió de la carrera"));
        break;
    }
}

void tbl_tick(app_t *a)
{
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) handle(a, f.data, f.len, true);
    while (aos_hal_link_recv(&f) > 0) handle(a, f.data, f.len, false);
    if (aos_hal_link_reliable_lost()) {
        aos_hal_link_reliable_reset();
        if (a->link_state == LK_PLAY || a->link_state == LK_LOADING) gone(a, _("Sin señal de %s"));
    }
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        memcpy(s_their_mac, p.mac, 6);
        snprintf(a->partner, sizeof a->partner, "%s", p.name);
    }
    switch (a->link_state) {
    case LK_LOBBY:
        if (now - s_hello_ms > 400) {
            s_hello_ms = now;
            send_hello(a);
        }
        break;
    case LK_LOADING:
        /* the stage built here: say so once, and start when both have it */
        if (!s_me_loaded && a->state == ST_LOADING && a->job == JOB_NONE && a->job_done && a->loaded_stage == a->stage) {
            s_me_loaded = true;
            msg_loaded_t m = { .type = MSG_LOADED, .proto = PROTO, .nonce = a->link_nonce };
            aos_hal_link_send_reliable(&m, sizeof m);
            aos_hal_log("turbo", "link: stage %d built here (%u)", a->stage, (unsigned)now);
            char buf[64];
            snprintf(buf, sizeof buf, "%s %s...", _("Esperando a"), a->partner);
            lv_label_set_text(a->load_lbl, buf);
        }
        if (s_me_loaded && s_peer_loaded) {
            aos_hal_log("turbo", "link: both have it, go (%u)", (unsigned)now);
            a->link_state = LK_PLAY;
            s_away_ms = now;
            a->job_done = true;         /* turbo.c's loading state starts the race */
        } else if (now - s_away_ms > 15000) {
            gone(a, _("Sin señal de %s"));
        }
        break;
    case LK_PLAY:
        if (a->state == ST_RACE && now - a->pos_ms >= 66) {
            a->pos_ms = now;
            msg_pos_t m = { .type = MSG_POS, .proto = PROTO, .z = a->game.z, .x = a->game.x,
                            .v = a->game.v, .state = (uint8_t)a->game.state };
            aos_hal_link_send_partner(&m, sizeof m);
        }
        if (now - s_away_ms > 8000 && !a->rival_done) {
            /* no positions for 8 s and no result: it left, or went out of range */
            aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
            int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
            bool offering = false;
            for (int i = 0; i < n; i++) {
                if (memcmp(nb[i].mac, s_their_mac, 6) == 0 && strcmp(nb[i].app, LINK_APP) == 0) offering = true;
            }
            if (!offering) gone(a, _("%s salió de la carrera"));
            else s_away_ms = now;
        }
        /* the result screen waits for the other: refresh it when it arrives */
        break;
    default:
        break;
    }
}

/* the link's loading holds the race until both watches have the stage */
bool tbl_hold_loading(app_t *a)
{
    return a->link_on && a->link_state == LK_LOADING;
}

void tbl_send_result(app_t *a)
{
    if (!a->link_on || a->link_state != LK_PLAY) return;
    msg_result_t m = { .type = MSG_RESULT, .proto = PROTO, .finished = a->game.state == RS_FINISHED,
                       .elapsed = a->game.elapsed, .progress = tb_game_progress(&a->game) };
    aos_hal_link_send_reliable(&m, sizeof m);
}

void tbl_host_pick(app_t *a, int stage)
{
    a->stage = stage;
}
