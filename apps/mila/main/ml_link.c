/*
 * MILA - the other watch (see ml_link.h)
 *
 *   HELLO   (fast, every 400 ms in the lobby and during a visit)
 *           nonce, mac, the pack's fingerprint, the outfit, the levels open
 *   CHOOSE  (reliable, host -> guest)  a visit, or a race on world + level
 *   READY   (reliable, guest -> host)  then both go
 *   LOADED  (reliable, both)           a race starts when both have the level
 *   PROG    (reliable, on change)      a racer's moves and things on targets
 *   WON     (reliable)                 solved, in so many moves
 *   BYE     (fast, three times)
 *
 * The lower MAC hosts and picks. A visit is simulated on each watch: the
 * friend's Mila, in the outfit her HELLO carries, comes in by the casita's
 * door and plays; nothing about her moves travels, so a weak signal only
 * ends the visit, never jerks it. A race is the same level on both, each
 * watch playing its own, with the other's progress on a pill.
 */
#include "ml_app.h"
#include "ml_casita.h"
#include "ml_link.h"
#include "ml_ui.h"

#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTO     1
#define LINK_APP  "mila"

enum { M_HELLO = 1, M_CHOOSE, M_READY, M_LOADED, M_PROG, M_WON, M_BYE };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
    uint8_t  mac[6];
    uint32_t pak;
    char     hat[16], neck[16];
    uint32_t hat_col, neck_col;
    uint16_t open[ML_MAX_WORLDS];   /* a bit per level open, by the table's order */
} msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce, guest_nonce;
    uint8_t  what, world, level;
} msg_choose_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
} msg_nonce_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint16_t moves;
    uint8_t  on, won;
} msg_prog_t;

static struct {
    bool     started, role_known, is_host, me_loaded, peer_loaded, mismatch;
    uint32_t nonce, peer_nonce, hello_ms, away_ms, check_ms;
    uint8_t  their_mac[6];
    uint16_t their_open[ML_MAX_WORLDS];
    int      last_moves, last_on;
    bool     sent_won;
    int      race_w, race_l;
    int      asked;             /* what the host picked, until READY */
    int      auto_pick;         /* the simulator's ML_LINK            */
    uint32_t hellos, sent;
} s;

bool ml_link_available(void)
{
    aos_link_partner_t p;
    return aos_hal_link_partner(&p) && p.valid;
}

static void send_hello(app_t *a)
{
    msg_hello_t h;
    memset(&h, 0, sizeof h);
    h.type = M_HELLO;
    h.proto = PROTO;
    h.nonce = s.nonce;
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    memcpy(h.mac, st.own_mac, 6);
    h.pak = ml_art_hash();
    snprintf(h.hat, sizeof h.hat, "%.15s", a->prog.hat);
    snprintf(h.neck, sizeof h.neck, "%.15s", a->prog.neck);
    h.hat_col = (uint32_t)a->prog.hat_col;
    h.neck_col = (uint32_t)a->prog.neck_col;
    for (int w = 0; w < a->worlds.nworlds && w < ML_MAX_WORLDS; w++)
        for (int l = 0; l < a->worlds.w[w].nlevels; l++)
            if (mla_level_open(a, w, l)) h.open[w] |= (uint16_t)(1u << l);
    aos_hal_link_send_partner(&h, sizeof h);
    s.sent++;
}

static void lobby_fill(app_t *a)
{
    char buf[200];
    const char *who = a->partner[0] ? a->partner : "?";
    if (s.mismatch)
        snprintf(buf, sizeof buf, "%s\n%s", _("El otro reloj tiene otra versión de Mila."),
                 _("Actualizá los dos para jugar juntos."));
    else if (!s.role_known) snprintf(buf, sizeof buf, "%s %s...", _("Buscando a"), who);
    else if (s.is_host) snprintf(buf, sizeof buf, "%s %s", who, _("está listo. ¿Qué hacemos?"));
    else snprintf(buf, sizeof buf, "%s %s", who, _("está eligiendo..."));
    ml_ui_lobby_fill(a, buf, s.role_known && s.is_host && !s.mismatch);
}

void ml_link_begin(app_t *a)
{
    if (!aos_hal_link_running()) s.started = aos_hal_link_start();
    if (!aos_hal_link_running()) {
        aos_ui_toast(_("El enlace no arrancó"), 1800);
        return;
    }
    aos_link_partner_t p;
    if (!(aos_hal_link_partner(&p) && p.valid)) {
        aos_hal_log("mila", "link: no partner");
        aos_ui_toast(_("Primero emparejá los relojes en Enlace"), 2200);
        return;
    }
    aos_hal_log("mila", "link: lobby with %s", p.name[0] ? p.name : "?");
    snprintf(a->partner, sizeof a->partner, "%s", p.name[0] ? p.name : "?");
    memcpy(s.their_mac, p.mac, 6);
    aos_hal_link_offer(LINK_APP);
    aos_hal_link_reliable_reset();
    a->link_on = true;
    a->link_state = ML_LK_LOBBY;
    s.nonce = ((uint32_t)aos_hal_uptime_ms() * 2654435761u) | 1u;
    s.peer_nonce = 0;
    s.role_known = s.is_host = s.mismatch = false;
    s.hello_ms = 0;
    lobby_fill(a);
    mla_set_state(a, ST_LOBBY);
}

void ml_link_end(app_t *a)
{
    if (a->link_on) aos_hal_log("mila", "link: end, state %d, app state %d", a->link_state, a->state);
    if (a->link_on || s.started) {
        uint8_t bye[2] = { M_BYE, PROTO };
        for (int i = 0; i < 3; i++) aos_hal_link_send_partner(bye, sizeof bye);
        aos_hal_link_offer("");
        if (s.started) aos_hal_link_stop();
    }
    s.started = false;
    a->link_on = false;
    a->link_state = ML_LK_OFF;
    a->race = false;
    if (a->visit) {
        a->visit = false;
        mlc_guest(a, NULL, 0, NULL, 0, false);
    }
}

static bool both_open(const app_t *a, int w, int l)
{
    return mla_level_open(a, w, l) && (s.their_open[w] >> l & 1);
}

/* the race's level: one open on both, in the furthest world they share */
static bool pick_race(app_t *a, int *w, int *l)
{
    for (int wi = a->worlds.nworlds - 1; wi >= 0; wi--) {
        int n = 0, got[ML_MAX_LEVELS];
        for (int li = 0; li < a->worlds.w[wi].nlevels; li++)
            if (both_open(a, wi, li)) got[n++] = li;
        if (n) {
            *w = wi;
            *l = got[aos_hal_uptime_ms() % (uint32_t)n];
            return true;
        }
    }
    return false;
}

static void start_visit(app_t *a)
{
    aos_hal_log("mila", "link: visit");
    a->link_state = ML_LK_VISIT;
    a->visit = true;
    s.away_ms = (uint32_t)aos_hal_uptime_ms();
    mla_set_state(a, ST_CASITA);
    mlc_guest(a, a->guest_hat, a->guest_hat_col, a->guest_neck, a->guest_neck_col, true);
}

static void start_race(app_t *a, int w, int l)
{
    a->link_state = ML_LK_LOADING;
    a->race = true;
    a->race_over = a->rival_won = a->race_lost = false;
    a->rival_on = a->rival_moves = 0;
    a->rival_targets = 0;
    s.me_loaded = s.peer_loaded = s.sent_won = false;
    s.last_moves = s.last_on = -1;
    s.race_w = w;
    s.race_l = l;
    s.away_ms = (uint32_t)aos_hal_uptime_ms();
    aos_hal_log("mila", "link: %s, race on %s", s.is_host ? "host" : "guest", a->worlds.w[w].lv[l].id);
    mla_level_start(a, w, l);
}

void ml_link_choose(app_t *a, int what)
{
    if (!s.is_host || a->link_state != ML_LK_LOBBY || !s.peer_nonce || s.mismatch) return;
    int w = 0, l = 0;
    if (what == LINK_RACE && !pick_race(a, &w, &l)) {
        aos_ui_toast(_("No hay un nivel abierto en los dos"), 1800);
        return;
    }
    msg_choose_t m = { .type = M_CHOOSE, .proto = PROTO, .host_nonce = s.nonce, .guest_nonce = s.peer_nonce,
                       .what = (uint8_t)what, .world = (uint8_t)w, .level = (uint8_t)l };
    aos_hal_link_send_reliable(&m, sizeof m);
    s.race_w = w;
    s.race_l = l;
    ml_ui_lobby_fill(a, _("Preparando..."), false);
    s.asked = what;
}

static void gone(app_t *a, const char *fmt)
{
    if (a->link_state == ML_LK_OFF || a->link_state == ML_LK_GONE) return;
    char buf[96];
    snprintf(buf, sizeof buf, fmt, a->partner[0] ? a->partner : "?");
    aos_hal_log("mila", "link: gone (%s), state %d, hellos in %u out %u, away %u ms", buf, a->link_state,
                (unsigned)s.hellos, (unsigned)s.sent, (unsigned)((uint32_t)aos_hal_uptime_ms() - s.away_ms));
    aos_ui_toast(buf, 2000);
    int st = a->link_state;
    ml_link_end(a);
    a->link_state = ML_LK_GONE;
    if (st == ML_LK_LOBBY && a->state == ST_LOBBY) mla_set_state(a, ST_CASITA);
    /* a race goes on alone; a visit simply ends (the guest walks out) */
}

static void handle(app_t *a, const uint8_t *d, int len, bool reliable)
{
    if (len < 2) return;
    if (d[1] != PROTO) return;
    switch (d[0]) {
    case M_HELLO: {
        s.hellos++;
        if (len < (int)sizeof(msg_hello_t)) return;
        const msg_hello_t *h = (const msg_hello_t *)d;
        if (h->pak != ml_art_hash()) {
            if (!s.mismatch) {
                s.mismatch = true;
                if (a->link_state == ML_LK_LOBBY) lobby_fill(a);
            }
            return;
        }
        s.away_ms = (uint32_t)aos_hal_uptime_ms();
        snprintf(a->guest_hat, sizeof a->guest_hat, "%.15s", h->hat);
        snprintf(a->guest_neck, sizeof a->guest_neck, "%.15s", h->neck);
        a->guest_hat_col = h->hat_col;
        a->guest_neck_col = h->neck_col;
        memcpy(s.their_open, h->open, sizeof s.their_open);
        if (a->link_state != ML_LK_LOBBY) return;
        s.peer_nonce = h->nonce;
        if (!s.role_known) {
            aos_link_stats_t st;
            aos_hal_link_stats(&st);
            int c = memcmp(st.own_mac, h->mac, 6);
            s.is_host = c < 0 || (c == 0 && s.nonce < h->nonce);
            s.role_known = true;
            aos_hal_log("mila", "link: %s", s.is_host ? "host" : "guest");
            send_hello(a);
            lobby_fill(a);
#ifdef AOS_SIM_BUILTIN
            /* ML_LINK=visit|race: the host picks by itself (two sims) */
            const char *e = getenv("ML_LINK");
            if (s.is_host && e && e[0]) s.auto_pick = !strcmp(e, "race") ? LINK_RACE : LINK_VISIT;
#endif
        }
        break;
    }
    case M_CHOOSE: {
        if (!reliable || a->link_state != ML_LK_LOBBY || len < (int)sizeof(msg_choose_t)) return;
        const msg_choose_t *m = (const msg_choose_t *)d;
        if (m->guest_nonce != s.nonce) return;
        s.is_host = false;
        s.role_known = true;
        s.peer_nonce = m->host_nonce;
        msg_nonce_t r = { .type = M_READY, .proto = PROTO, .nonce = m->host_nonce };
        aos_hal_link_send_reliable(&r, sizeof r);
        if (m->what == LINK_RACE && m->world < a->worlds.nworlds && m->level < a->worlds.w[m->world].nlevels)
            start_race(a, m->world, m->level);
        else
            start_visit(a);
        break;
    }
    case M_READY: {
        if (!reliable || !s.is_host || a->link_state != ML_LK_LOBBY || len < (int)sizeof(msg_nonce_t)) return;
        if (((const msg_nonce_t *)d)->nonce != s.nonce) return;
        if (s.asked == LINK_RACE) start_race(a, s.race_w, s.race_l);
        else start_visit(a);
        break;
    }
    case M_LOADED:
        if (reliable) s.peer_loaded = true;
        break;
    case M_PROG: {
        if (!reliable || len < (int)sizeof(msg_prog_t)) return;
        const msg_prog_t *p = (const msg_prog_t *)d;
        a->rival_moves = p->moves;
        a->rival_on = p->on;
        s.away_ms = (uint32_t)aos_hal_uptime_ms();
        break;
    }
    case M_WON: {
        if (!reliable || len < (int)sizeof(msg_prog_t)) return;
        const msg_prog_t *p = (const msg_prog_t *)d;
        a->rival_moves = p->moves;
        a->rival_won = true;
        /* first one out wins: if I had not won yet, it's theirs */
        if (!s.sent_won) a->race_lost = true;
        a->race_over = true;
        break;
    }
    case M_BYE:
        gone(a, a->link_state == ML_LK_VISIT ? _("%s se fue a su casa") : _("%s se desconectó"));
        break;
    default:
        break;
    }
}

void ml_link_tick(app_t *a)
{
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (now - s.check_ms > 1500 && !a->link_on) {
        /* the casita's friend button: a paired watch, checked now and then */
        s.check_ms = now;
        a->partner_ok = ml_link_available();
    }
    if (!a->link_on) return;
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) handle(a, f.data, f.len, true);
    while (aos_hal_link_recv(&f) > 0) handle(a, f.data, f.len, false);
    if (!a->link_on) return;
    if (aos_hal_link_reliable_lost()) {
        aos_hal_link_reliable_reset();
        if (a->link_state != ML_LK_LOBBY) gone(a, _("Sin señal de %s"));
        return;
    }
    /* now again: what was just received moved away_ms past the first reading,
     * and "now - away_ms" would wrap round into a huge silence */
    now = (uint32_t)aos_hal_uptime_ms();
    if (s.auto_pick && a->link_state == ML_LK_LOBBY && s.peer_nonce) {
        int w = s.auto_pick;
        s.auto_pick = 0;
        ml_link_choose(a, w);
    }
    switch (a->link_state) {
    case ML_LK_LOBBY:
    case ML_LK_VISIT:
        if (now - s.hello_ms > 400) {
            s.hello_ms = now;
            send_hello(a);
        }
        if (a->link_state == ML_LK_VISIT && now - s.away_ms > 6000) gone(a, _("%s se fue a su casa"));
        if (a->link_state == ML_LK_VISIT && a->state != ST_CASITA && a->state != ST_SHOP &&
            a->state != ST_SETTINGS)
            ml_link_end(a);
        break;
    case ML_LK_LOADING:
        if (a->level_ok && a->state == ST_LEVEL && !s.me_loaded) {
            s.me_loaded = true;
            msg_nonce_t m = { .type = M_LOADED, .proto = PROTO, .nonce = s.nonce };
            aos_hal_link_send_reliable(&m, sizeof m);
        }
        if (s.me_loaded && s.peer_loaded) {
            a->link_state = ML_LK_PLAY;
            s.away_ms = now;
            /* both go: straight onto Mila */
            a->mode_t = 0;
            a->lmode = LM_ZOOM;
        } else if (now - s.away_ms > 15000) {
            gone(a, _("Sin señal de %s"));
        }
        break;
    case ML_LK_PLAY: {
        ml_play_t *p = &a->play;
        int on = ml_play_on_target(p);
        if ((p->moves != s.last_moves || on != s.last_on) && !s.sent_won) {
            s.last_moves = p->moves;
            s.last_on = on;
            msg_prog_t m = { .type = M_PROG, .proto = PROTO, .moves = (uint16_t)p->moves, .on = (uint8_t)on };
            aos_hal_link_send_reliable(&m, sizeof m);
        }
        if (p->won && !s.sent_won) {
            s.sent_won = true;
            msg_prog_t m = { .type = M_WON, .proto = PROTO, .moves = (uint16_t)p->moves, .on = (uint8_t)on, .won = 1 };
            aos_hal_link_send_reliable(&m, sizeof m);
            if (!a->rival_won) a->race_over = true;
        }
        break;
    }
    default:
        break;
    }
}
