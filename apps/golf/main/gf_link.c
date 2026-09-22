/*
 * GOLF - against the paired watch (see gf_app.h)
 *
 * Turns, not a clock, so it is the Truco scheme: both watches run the same
 * round from the same seed, and only the shots travel. The lower MAC is the
 * host and player 1; the host picks the seed and the difficulty.
 *
 *   HELLO  (fast, every 400 ms in the lobby)  nonce, MAC, outfit
 *   START  (reliable, host -> guest)          seed, difficulty, both nonces
 *   READY  (reliable, guest -> host)
 *   SHOT   (reliable)                         hole, player, stroke, the swing,
 *                                             and where the sender's ball
 *                                             stopped, to check the physics
 *   BYE    (fast, three times)
 *
 * The physics is deterministic on the same binary, so the ball the receiver
 * simulates stops where the sender's did; the position in SHOT is only a
 * check (logged, and snapped to, if they ever disagree). Shots wait in a
 * small inbox until the receiver is on that hole and that turn: the other
 * watch may tee off while this one is still reading the scorecard.
 */
#include "gf_app.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTO       2              /* 2: the course travels in START */
#define LINK_APP    "golf"

enum { MSG_HELLO = 1, MSG_START, MSG_READY, MSG_SHOT, MSG_BYE };
enum { LK_OFF = 0, LK_LOBBY, LK_PLAY, LK_GONE };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
    uint8_t  mac[6];
    uint8_t  eq[CAT_N];
} msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t seed, host_nonce, guest_nonce;
    uint8_t  diff;
    uint8_t  eq[CAT_N];
    uint8_t  course;
} msg_start_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce;
    uint8_t  eq[CAT_N];
} msg_ready_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  hole, player, stroke, club;
    float    aim, power, acc;
    float    x, y;
} msg_shot_t;

#define INBOX 4
static msg_shot_t s_inbox[INBOX];
static int        s_ninbox;
static bool       s_is_host, s_role_known, s_started_link;
static uint8_t    s_their_mac[6];
static uint32_t   s_hello_ms, s_away_ms, s_last_rx_ms;
static uint32_t   s_seed;             /* the host's, from its START */

static void send_hello(app_t *a)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    msg_hello_t h = { .type = MSG_HELLO, .proto = PROTO, .nonce = a->link_nonce };
    memcpy(h.mac, st.own_mac, 6);
    memcpy(h.eq, a->wr.eq, CAT_N);
    aos_hal_link_send_partner(&h, sizeof h);
}

bool gfl_available(app_t *a, char *name, int n)
{
    (void)a;
    if (!aos_hal_link_running()) {
        s_started_link = aos_hal_link_start();
    }
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(name, (size_t)n, "%s", p.name[0] ? p.name : "?");
        return true;
    }
#ifdef AOS_SIM_BUILTIN
    const char *e = getenv("GF_LINK");
    if (e && e[0]) {
        snprintf(name, (size_t)n, "sim");
        return true;
    }
#endif
    return false;
}

static void lobby_text(app_t *a, const char *t)
{
    lv_label_set_text(a->lbl_load_t, _("Contra el otro reloj"));
    lv_label_set_text(a->lbl_load_s, t);
}

void gfl_begin(app_t *a)
{
    if (!aos_hal_link_running() && !aos_hal_link_start()) {
        gfa_banner(a, _("El enlace no arrancó"), 0xFF6A5A, 2000);
        return;
    }
    aos_hal_link_offer(LINK_APP);
    aos_hal_link_reliable_reset();
    a->link_on = true;
    a->link_state = LK_LOBBY;
    a->link_nonce = ((uint32_t)aos_hal_uptime_ms() * 2654435761u) | 1u;
    a->link_peer_nonce = 0;
    a->link_ms = (uint32_t)aos_hal_uptime_ms();
    s_role_known = false;
    s_ninbox = 0;
    s_hello_ms = 0;
    a->partner[0] = 0;
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(a->partner, sizeof a->partner, "%s", p.name);
        memcpy(s_their_mac, p.mac, 6);
    }
    char buf[64];
    snprintf(buf, sizeof buf, "%s %s...", _("Esperando a"), a->partner[0] ? a->partner : "?");
    gfa_set_state(a, ST_LOADING);
    a->render_pending = false;
    lobby_text(a, buf);
}

void gfl_end(app_t *a)
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
}

static void start_round(app_t *a, uint32_t seed, int diff)
{
    a->link_state = LK_PLAY;
    a->diff = diff;
    a->mode = MODE_LINK;
    a->nplayers = 2;
    a->local_player = s_is_host ? 0 : 1;
    /* play_round_start takes the seed from the nonces: set them so both
     * watches compute the same */
    a->link_nonce = seed;
    a->link_peer_nonce = 0;
    s_away_ms = (uint32_t)aos_hal_uptime_ms();
    s_last_rx_ms = s_away_ms;
    a->shown_player = -1;
    gfp_round_start(a);
}

static void gone(app_t *a, const char *fmt)
{
    if (a->link_state != LK_PLAY && a->link_state != LK_LOBBY) return;
    char buf[80];
    snprintf(buf, sizeof buf, fmt, a->partner[0] ? a->partner : "?");
    gfa_banner(a, buf, 0xFF6A5A, 3500);
    a->link_state = LK_GONE;
    a->link_ms = (uint32_t)aos_hal_uptime_ms();
}

static void handle(app_t *a, const uint8_t *d, int len, bool reliable)
{
    if (len < 2 || d[1] != PROTO) return;
    s_last_rx_ms = (uint32_t)aos_hal_uptime_ms();
    switch (d[0]) {
    case MSG_HELLO: {
        if (len < (int)sizeof(msg_hello_t)) return;
        const msg_hello_t *h = (const msg_hello_t *)d;
        if (a->link_state == LK_PLAY && a->link_peer_nonce && h->nonce != a->link_peer_nonce) {
            gone(a, _("%s salió del juego"));
            return;
        }
        if (a->link_state != LK_LOBBY) return;
        bool fresh = h->nonce != a->link_peer_nonce;
        a->link_peer_nonce = h->nonce;
        memcpy(s_their_mac, h->mac, 6);
        memcpy(a->partner_eq, h->eq, CAT_N);
        if (!s_role_known) {
            aos_link_stats_t st;
            aos_hal_link_stats(&st);
            s_is_host = memcmp(st.own_mac, h->mac, 6) < 0;
            s_role_known = true;
            send_hello(a);
        }
        if (s_is_host && fresh) {
            msg_start_t m = { .type = MSG_START, .proto = PROTO,
                              .seed = ((uint32_t)aos_hal_uptime_ms() * 2246822519u) ^ a->link_nonce,
                              .host_nonce = a->link_nonce, .guest_nonce = h->nonce, .diff = (uint8_t)a->diff,
                              .course = (uint8_t)a->course };
            memcpy(m.eq, a->wr.eq, CAT_N);
            s_seed = m.seed;
            aos_hal_link_send_reliable(&m, sizeof m);
        }
        break;
    }
    case MSG_START: {
        if (!reliable || a->link_state != LK_LOBBY || len < (int)sizeof(msg_start_t)) return;
        const msg_start_t *m = (const msg_start_t *)d;
        if (m->guest_nonce != a->link_nonce) return;
        s_is_host = false;
        s_role_known = true;
        memcpy(a->partner_eq, m->eq, CAT_N);
        gf_course_select(m->course);        /* the host's course */
        msg_ready_t r = { .type = MSG_READY, .proto = PROTO, .host_nonce = m->host_nonce };
        memcpy(r.eq, a->wr.eq, CAT_N);
        aos_hal_link_send_reliable(&r, sizeof r);
        start_round(a, m->seed, m->diff);
        a->link_peer_nonce = m->host_nonce;
        break;
    }
    case MSG_READY: {
        if (!reliable || !s_is_host || a->link_state != LK_LOBBY || len < (int)sizeof(msg_ready_t)) return;
        const msg_ready_t *r = (const msg_ready_t *)d;
        if (r->host_nonce != a->link_nonce) return;
        memcpy(a->partner_eq, r->eq, CAT_N);
        uint32_t peer = a->link_peer_nonce;
        gf_course_select(a->course);
        start_round(a, s_seed, a->diff);
        a->link_peer_nonce = peer;
        break;
    }
    case MSG_SHOT:
        if (!reliable || len < (int)sizeof(msg_shot_t)) return;
        if (s_ninbox < INBOX) memcpy(&s_inbox[s_ninbox++], d, sizeof(msg_shot_t));
        break;
    case MSG_BYE:
        gone(a, _("%s salió del juego"));
        break;
    }
}

void gfl_tick(app_t *a)
{
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) handle(a, f.data, f.len, true);
    while (aos_hal_link_recv(&f) > 0) handle(a, f.data, f.len, false);
    if (aos_hal_link_reliable_lost()) {
        aos_hal_link_reliable_reset();
        if (a->link_state == LK_PLAY) gone(a, _("Sin señal de %s"));
    }
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        memcpy(s_their_mac, p.mac, 6);
        snprintf(a->partner, sizeof a->partner, "%s", p.name);
    }

    if (a->link_state == LK_LOBBY) {
        if (now - s_hello_ms > 400) {
            s_hello_ms = now;
            send_hello(a);
        }
        return;
    }
    if (a->link_state == LK_GONE) {
        if (now - a->link_ms > 3500) {
            gfl_end(a);
            gfa_set_state(a, ST_MENU);
        }
        return;
    }
    if (a->link_state != LK_PLAY) return;

    /* the other watch left the app: its beacon stops offering golf */
    aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
    int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
    bool offering = false;
    for (int i = 0; i < n; i++) {
        if (memcmp(nb[i].mac, s_their_mac, 6) == 0 && strcmp(nb[i].app, LINK_APP) == 0) offering = true;
    }
    if (offering) s_away_ms = now;
    else if (now - s_away_ms > 8000) gone(a, _("%s salió del juego"));

    /* a shot from the other watch, when this one is ready for it */
    if (s_ninbox && a->state == ST_REMOTE) {
        msg_shot_t *m = &s_inbox[0];
        gf_player_t *pl = &a->game.pl[a->game.turn];
        if (m->hole == a->game.hi && m->player == a->game.turn && m->stroke == pl->cur) {
            a->remote_x = m->x;
            a->remote_y = m->y;
            a->remote_check = true;
            gfp_apply_remote_shot(a, m->club, m->aim, m->power, m->acc);
            memmove(s_inbox, s_inbox + 1, sizeof(s_inbox[0]) * (size_t)(--s_ninbox));
        } else if (m->hole < a->game.hi || (m->hole == a->game.hi && m->stroke < pl->cur)) {
            memmove(s_inbox, s_inbox + 1, sizeof(s_inbox[0]) * (size_t)(--s_ninbox));
        }
    }
}

void gfl_send_shot(app_t *a, int club, float aim, float power, float acc)
{
    if (!a->link_on) return;
    gf_player_t *pl = &a->game.pl[a->game.turn];
    msg_shot_t m = { .type = MSG_SHOT, .proto = PROTO, .hole = a->game.hi, .player = a->game.turn,
                     .stroke = pl->cur, .club = (uint8_t)club, .aim = aim, .power = power, .acc = acc,
                     .x = a->shot.x, .y = a->shot.y };
    aos_hal_link_send_reliable(&m, sizeof m);
}

bool gfl_is_local_turn(app_t *a)
{
    return a->mode != MODE_LINK || a->game.turn == a->local_player;
}
