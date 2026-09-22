/*
 * MONSTER HOP - the key race with the paired watch (see mh_app.h)
 *
 * Both watches play the same level from the same moment, so the lanes, the
 * traps and the platforms (functions of the level's clock) agree without
 * talking. Each Tommy is seen on the other watch as a pale ghost; the keys,
 * the levers, the crates and the chests are shared. A key is a point; the
 * first one out takes two more. A key both took goes to whoever took it
 * earlier by the level's clock, the host on a tie: both watches apply that
 * rule to the same two times, so they agree.
 *
 *   HELLO   (fast, every 400 ms in the lobby)  nonce, outfit, the levels open
 *                                              here, the host's pick
 *   START   (reliable, host -> guest)          level, seed, both nonces
 *   READY   (reliable, guest -> host)          then both load the level
 *   LOADED  (reliable, both)                   when both have it: go
 *   POS     (fast, 15 Hz)                      Tommy's position and state
 *   KEY / LEVER / CRATE / CHEST (reliable)     what changed in the level
 *   EXIT    (reliable)                         out, at this clock
 *   BYE     (fast, three times)
 *
 * The lower MAC hosts and picks the level among those open on both. The
 * game is stepped by the worker (core 0) and the link runs in the LVGL
 * timer, so what arrives goes through lk_in (drained by the worker before
 * a step) and what changed here comes back through lk_out.
 */
#include "mh_app.h"
#include "mh_ui.h"

#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTO     1
#define LINK_APP  "monsterhop"

enum { M_HELLO = 1, M_START, M_READY, M_LOADED, M_POS, M_KEY, M_LEVER, M_CRATE, M_CHEST, M_EXIT, M_BYE };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
    uint8_t  mac[6];
    int8_t   eq[CAT_N];
    uint16_t open;
    uint8_t  pick;
} msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce, guest_nonce, seed;
    uint8_t  level;
} msg_start_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
} msg_nonce_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    float    x, y, z, f;
    float    t;                     /* the level's clock over there          */
    uint8_t  dir, state;
} msg_pos_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  idx;
    int8_t   x, y, z;
    uint8_t  flag;
    float    at;
} msg_op_t;

static bool     s_role_known, s_started_link, s_me_loaded, s_peer_loaded, s_mismatch_told, s_exit_sent;
static uint32_t s_hello_ms, s_away_ms;
static uint8_t  s_their_mac[6];

/* ---- the queues between the two cores ---- */

static void push_in(app_t *a, const msg_op_t *m)
{
    uint32_t w = a->lk_in_w;
    if (w - a->lk_in_r >= MH_LK_Q) return;
    mh_lk_op_t *o = &a->lk_in[w % MH_LK_Q];
    o->kind = m->type;
    o->idx = m->idx;
    o->x = m->x;
    o->y = m->y;
    o->z = m->z;
    o->flag = m->flag;
    o->at = m->at;
    a->lk_in_w = w + 1;
}

void mhl_worker_before(app_t *a)
{
    mh_game_t *g = &a->game;
    while (a->lk_in_r != a->lk_in_w) {
        const mh_lk_op_t *o = &a->lk_in[a->lk_in_r % MH_LK_Q];
        switch (o->kind) {
        case M_KEY: mh_game_rival_key(g, o->idx, o->at); break;
        case M_LEVER: mh_game_rival_lever(g, o->idx, o->flag != 0); break;
        case M_CRATE: mh_game_rival_crate(g, o->idx, o->x, o->y, o->z, o->flag != 0); break;
        case M_CHEST: mh_game_rival_chest(g, o->idx); break;
        case M_EXIT: mh_game_rival_exit(g, o->at); break;
        default: break;
        }
        a->lk_in_r++;
    }
}

void mhl_worker_after(app_t *a)
{
    mh_game_t *g = &a->game;
    for (int i = 0; i < g->n_out; i++) {
        uint32_t w = a->lk_out_w;
        if (w - a->lk_out_r >= MH_LK_Q) break;
        a->lk_out[w % MH_LK_Q] = g->out[i];
        a->lk_out_w = w + 1;
    }
    g->n_out = 0;
}

/* ---- sending ---- */

static uint16_t open_mask(const app_t *a)
{
    uint16_t m = 0;
    for (int i = 0; i < MH_LEVELS && i < 16; i++) if (mha_level_open(a, i)) m |= (uint16_t)(1u << i);
    return m;
}

static void send_hello(app_t *a)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    msg_hello_t h = { .type = M_HELLO, .proto = PROTO, .nonce = a->link_nonce, .open = open_mask(a),
                      .pick = (uint8_t)a->link_level };
    memcpy(h.mac, st.own_mac, 6);
    memcpy(h.eq, a->prog.eq, CAT_N);
    aos_hal_link_send_partner(&h, sizeof h);
}

static void send_op(int type, int idx, int x, int y, int z, int flag, float at)
{
    msg_op_t m = { .type = (uint8_t)type, .proto = PROTO, .idx = (uint8_t)idx, .x = (int8_t)x, .y = (int8_t)y,
                   .z = (int8_t)z, .flag = (uint8_t)flag, .at = at };
    aos_hal_link_send_reliable(&m, sizeof m);
}

/* what the worker says changed here, read back from the game's state */
static void send_changes(app_t *a)
{
    const mh_game_t *g = &a->game;
    while (a->lk_out_r != a->lk_out_w) {
        uint16_t o = a->lk_out[a->lk_out_r % MH_LK_Q];
        a->lk_out_r++;
        int kind = o >> 8, i = o & 0xFF;
        switch (kind) {
        case OUT_KEY:
            if (i < g->n_pick) send_op(M_KEY, i, 0, 0, 0, 0, g->pick[i].at);
            break;
        case OUT_LEVER:
            if (i < g->n_lever) send_op(M_LEVER, i, 0, 0, 0, g->lever[i].on, 0);
            break;
        case OUT_CRATE:
            if (i < g->n_crate) {
                const mh_crate_t *c = &g->crate[i];
                send_op(M_CRATE, i, c->x, c->y, c->z, c->sunk, 0);
            }
            break;
        case OUT_CHEST:
            send_op(M_CHEST, i, 0, 0, 0, 0, 0);
            break;
        default:
            break;
        }
    }
}

/* ---- the lobby ---- */

bool mha_link_available(app_t *a, char *name, int n)
{
    (void)a;
    if (!aos_hal_link_running()) s_started_link = aos_hal_link_start();
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(name, (size_t)n, "%s", p.name[0] ? p.name : "?");
        return true;
    }
#ifdef AOS_SIM_BUILTIN
    const char *e = getenv("MH_LINK");
    if (e && e[0]) {
        snprintf(name, (size_t)n, "sim");
        return true;
    }
#endif
    return false;
}

static bool both_open(const app_t *a, int i)
{
    return i >= 0 && i < MH_LEVELS && mha_level_open(a, i) && (a->rival_open & (1u << i));
}

static void lobby_fill(app_t *a)
{
    char buf[160];
    const char *who = a->partner[0] ? a->partner : "?";
    if (!s_role_known) snprintf(buf, sizeof buf, "%s %s...", _("Buscando a"), who);
    else if (a->is_host) snprintf(buf, sizeof buf, "%s %s", who, _("está listo. Elige el nivel:"));
    else snprintf(buf, sizeof buf, "%s %s", who, _("elige el nivel..."));
    mh_ui_lobby_fill(a, buf, s_role_known ? mha_level_title(a->link_level) : "", s_role_known && a->is_host);
}

void mha_link_begin(app_t *a)
{
    if (!aos_hal_link_running() && !aos_hal_link_start()) {
        aos_ui_toast(_("El enlace no arrancó"), 1800);
        return;
    }
    aos_hal_link_offer(LINK_APP);
    aos_hal_link_reliable_reset();
    a->link_on = true;
    a->link_state = LK_LOBBY;
    a->link_nonce = ((uint32_t)aos_hal_uptime_ms() * 2654435761u) | 1u;
    a->link_peer_nonce = 0;
    a->rival_open = 1;
    if (a->link_level < 0 || !mha_level_open(a, a->link_level)) a->link_level = 0;
    s_role_known = false;
    s_me_loaded = s_peer_loaded = false;
    s_mismatch_told = false;
    s_hello_ms = 0;
    a->partner[0] = 0;
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        snprintf(a->partner, sizeof a->partner, "%s", p.name);
        memcpy(s_their_mac, p.mac, 6);
    }
    a->is_host = false;
#ifdef AOS_SIM_BUILTIN
    if (!a->partner[0]) snprintf(a->partner, sizeof a->partner, "sim");
#endif
    lobby_fill(a);
    mha_set_state(a, ST_LOBBY);
}

void mhl_end(app_t *a)
{
    if (a->link_on || s_started_link) {
        uint8_t bye[2] = { M_BYE, PROTO };
        for (int i = 0; i < 3; i++) aos_hal_link_send_partner(bye, sizeof bye);
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }
    a->link_on = false;
    s_started_link = false;
    a->link_state = LK_OFF;
    a->game.rival = false;
}

void mhl_pick(app_t *a, int delta)
{
    if (!a->is_host || a->link_state != LK_LOBBY) return;
    int i = a->link_level;
    for (int k = 0; k < MH_LEVELS; k++) {
        i = (i + delta + MH_LEVELS) % MH_LEVELS;
        if (both_open(a, i)) break;
    }
    if (both_open(a, i)) a->link_level = i;
    lobby_fill(a);
    send_hello(a);
}

void mhl_go(app_t *a)
{
    if (!a->is_host || a->link_state != LK_LOBBY || !a->link_peer_nonce) return;
    a->link_seed = ((uint32_t)aos_hal_uptime_ms() * 747796405u) | 1u;
    msg_start_t m = { .type = M_START, .proto = PROTO, .host_nonce = a->link_nonce,
                      .guest_nonce = a->link_peer_nonce, .seed = a->link_seed, .level = (uint8_t)a->link_level };
    aos_hal_link_send_reliable(&m, sizeof m);
    mh_ui_lobby_fill(a, _("Preparando la carrera..."), mha_level_title(a->link_level), false);
}

static void begin_loading(app_t *a, int level)
{
    aos_hal_log("mhop", "link: %s, loading level %d", a->is_host ? "host" : "guest", level);
    a->link_state = LK_LOADING;
    s_me_loaded = s_peer_loaded = false;
    s_exit_sent = false;
    s_away_ms = (uint32_t)aos_hal_uptime_ms();
    a->lk_in_r = a->lk_in_w = 0;
    a->lk_out_r = a->lk_out_w = 0;
    a->lk_loaded = false;
    a->lk_behind = 0;
    a->link_level = level;
    mha_level_start(a, level);
}

static void gone(app_t *a, const char *fmt)
{
    if (a->link_state == LK_OFF || a->link_state == LK_GONE) return;
    char buf[80];
    snprintf(buf, sizeof buf, fmt, a->partner[0] ? a->partner : "?");
    aos_ui_toast(buf, 2000);
    if (a->link_state == LK_LOBBY || a->link_state == LK_LOADING) {
        a->link_state = LK_GONE;
        bool loading = a->state == ST_LOADING;
        mhl_end(a);
        if (a->state == ST_LOBBY) mha_set_state(a, ST_HOUSE);
        else if (loading) a->lk_abort = true;
        return;
    }
    /* mid-race: keep playing alone (the keys it took stay taken) */
    a->link_state = LK_GONE;
    a->game.rival = false;
}

/* ---- receiving ---- */

static void handle(app_t *a, const uint8_t *d, int len, bool reliable)
{
    if (len < 2) return;
    if (d[1] != PROTO) {
        if (d[0] == M_HELLO && a->link_state == LK_LOBBY && !s_mismatch_told) {
            s_mismatch_told = true;
            char buf[128];
            snprintf(buf, sizeof buf, "%s\n%s", _("El otro reloj tiene otra versión de Monster Hop."),
                     _("Actualiza los dos para jugar juntos."));
            mh_ui_lobby_fill(a, buf, "", false);
        }
        return;
    }
    switch (d[0]) {
    case M_HELLO: {
        if (len < (int)sizeof(msg_hello_t)) return;
        const msg_hello_t *h = (const msg_hello_t *)d;
        if (a->link_state == LK_PLAY && a->link_peer_nonce && h->nonce != a->link_peer_nonce) {
            gone(a, _("%s salió de la carrera"));
            return;
        }
        if (a->link_state != LK_LOBBY) return;
        a->link_peer_nonce = h->nonce;
        memcpy(a->rival_eq, h->eq, CAT_N);
        a->rival_open = h->open | 1u;
        if (!s_role_known) {
            aos_link_stats_t st;
            aos_hal_link_stats(&st);
            int c = memcmp(st.own_mac, h->mac, 6);
            a->is_host = c < 0 || (c == 0 && a->link_nonce < h->nonce);
            s_role_known = true;
            memcpy(s_their_mac, h->mac, 6);
            if (a->is_host && !both_open(a, a->link_level)) mhl_pick(a, -1);
            send_hello(a);
        }
        if (!a->is_host && h->pick < MH_LEVELS) a->link_level = h->pick;
        lobby_fill(a);
        break;
    }
    case M_START: {
        if (!reliable || a->link_state != LK_LOBBY || len < (int)sizeof(msg_start_t)) return;
        const msg_start_t *m = (const msg_start_t *)d;
        if (m->guest_nonce != a->link_nonce) return;
        a->is_host = false;
        s_role_known = true;
        a->link_peer_nonce = m->host_nonce;
        a->link_seed = m->seed;
        msg_nonce_t r = { .type = M_READY, .proto = PROTO, .nonce = m->host_nonce };
        aos_hal_link_send_reliable(&r, sizeof r);
        begin_loading(a, m->level < MH_LEVELS ? m->level : 0);
        break;
    }
    case M_READY: {
        if (!reliable || !a->is_host || a->link_state != LK_LOBBY || len < (int)sizeof(msg_nonce_t)) return;
        if (((const msg_nonce_t *)d)->nonce != a->link_nonce) return;
        begin_loading(a, a->link_level);
        break;
    }
    case M_LOADED:
        if (reliable) s_peer_loaded = true;
        break;
    case M_POS: {
        if (len < (int)sizeof(msg_pos_t) || a->link_state != LK_PLAY) return;
        const msg_pos_t *p = (const msg_pos_t *)d;
        mh_game_t *g = &a->game;
        g->rx = p->x;
        g->ry = p->y;
        g->rz = p->z;
        g->rf = p->f;
        g->rdir = p->dir & 3;
        g->rstate = p->state;
        g->rival = true;
        /* the clocks: the host's is the one; the guest eases towards it
         * (either way) in the worker. Only one side moves, or the two
         * would chase each other's old positions */
        if (!a->is_host && (g->state == GS_PLAY || g->state == GS_DYING)) {
            float d = p->t - g->t;
            a->lk_behind = (d > 0.02f || d < -0.02f) ? d : 0;
        }
        s_away_ms = (uint32_t)aos_hal_uptime_ms();
        break;
    }
    case M_KEY: case M_LEVER: case M_CRATE: case M_CHEST: case M_EXIT:
        if (!reliable || len < (int)sizeof(msg_op_t)) return;
        if (a->link_state != LK_PLAY && a->link_state != LK_LOADING) return;
        push_in(a, (const msg_op_t *)d);
        break;
    case M_BYE:
        gone(a, _("%s salió de la carrera"));
        break;
    default:
        break;
    }
}

void mhl_tick(app_t *a)
{
    if (!a->link_on) return;
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) handle(a, f.data, f.len, true);
    while (aos_hal_link_recv(&f) > 0) handle(a, f.data, f.len, false);
    if (!a->link_on) return;
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
        if (a->lk_loaded && !s_me_loaded) {
            s_me_loaded = true;
            msg_nonce_t m = { .type = M_LOADED, .proto = PROTO, .nonce = a->link_nonce };
            aos_hal_link_send_reliable(&m, sizeof m);
            char buf[64];
            snprintf(buf, sizeof buf, "%s %s...", _("Esperando a"), a->partner);
            mh_ui_loading_text(a, buf);
        }
        if (s_me_loaded && s_peer_loaded) {
            aos_hal_log("mhop", "link: both have the level, go (%u)", (unsigned)now);
            a->link_state = LK_PLAY;
            s_away_ms = now;
            a->pos_ms = 0;
            mhl_start_play(a);
        } else if (now - s_away_ms > 15000) {
            gone(a, _("Sin señal de %s"));
        }
        break;
    case LK_PLAY: {
        mh_game_t *g = &a->game;
        send_changes(a);
        static uint32_t s_log_ms;
        if (now - s_log_ms > 5000) {
            /* for the log: the clocks must agree to a frame or two */
            s_log_ms = now;
            aos_hal_log("mhop", "race: clock %d ms at %u, behind %d ms, keys %d/%d", (int)(g->t * 1000), (unsigned)now,
                        (int)(a->lk_behind * 1000), g->my_keys, g->keys - g->my_keys);
        }
        if (g->state == GS_WON && !s_exit_sent) {
            s_exit_sent = true;
            send_op(M_EXIT, 0, 0, 0, 0, 0, g->t);
        }
        if (a->state == ST_PLAY && now - a->pos_ms >= 66) {
            a->pos_ms = now;
            const mh_hero_t *h = &g->h;
            msg_pos_t m = { .type = M_POS, .proto = PROTO, .x = h->x, .y = h->y, .z = h->z,
                            .f = h->dur > 0 ? h->t / h->dur : 0, .t = g->t, .dir = (uint8_t)h->dir,
                            .state = (uint8_t)h->state };
            aos_hal_link_send_partner(&m, sizeof m);
        }
        if (now - s_away_ms > 8000) {
            aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
            int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
            bool offering = false;
            for (int i = 0; i < n; i++)
                if (memcmp(nb[i].mac, s_their_mac, 6) == 0 && strcmp(nb[i].app, LINK_APP) == 0) offering = true;
            if (!offering) gone(a, _("%s salió de la carrera"));
            else s_away_ms = now;
        }
        break;
    }
    default:
        break;
    }
}

bool mhl_racing(const app_t *a)
{
    return a->link_on && (a->link_state == LK_LOADING || a->link_state == LK_PLAY || a->link_state == LK_GONE);
}
