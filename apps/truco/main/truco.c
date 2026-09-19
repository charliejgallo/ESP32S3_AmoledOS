/* This app is deliberately NOT translated.
 *
 * The calls of truco -envido, quiero retruco, falta envido- and the vocabulary
 * of the table are not interface copy: they are the names of the plays.
 * Translating them would give a game that is no longer truco. That is why
 * there is not a single _() here and the aos.truco.lang catalogue does not
 * exist: the system may be in English and this screen stays in Spanish, which
 * is the right thing.
 *
 * The only thing translated is the menu's "Truco" name, which comes from the
 * system catalogue like any other app's. See docs/I18N.md. */

/*
 * AmoledOS - TRUCO
 *
 * Argentine truco, two-handed against the machine, without flor and to 30
 * points. A green baize table, Spanish playing cards drawn in code and the
 * matchstick scoreboard, which is how it is really kept: a little square of
 * four sticks plus the diagonal for every five points, three squares of malas
 * and three of buenas.
 *
 * The file is split in three on purpose:
 *
 *   tr_game.c   the rules and the opponent. It knows nothing of LVGL, so a
 *               whole game can be played from tools/tr_harness.c: thousands of
 *               games in a row verifying that the score adds up and that
 *               nobody is left waiting for a turn that never comes.
 *   tr_cards.c  the drawing of the cards and of the scoreboard.
 *   truco.c     this one: the table, the animations and who touches what.
 *
 * The rules and the screen talk to each other through EVENTS. tr_apply()
 * leaves in a queue what happened ("said truco", "played the 7 of swords") and
 * here they are taken out one at a time and animated. Without that, every rule
 * would have to know how long an animation lasts.
 *
 * Development switches:
 *   TRUCO_AUTO=1        the machine plays both sides (for leaving it running)
 *   TRUCO_SHOWALL=1     the opponent's cards are visible
 *   TRUCO_FAST=1        no waits between plays
 *   TRUCO_SEED=n        reproducible deal
 *   TRUCO_SCORE=a,b     starts with that score (for testing the falta and the ending)
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include "tr_game.h"
#include "tr_cards.h"
#include "tl_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- geometry of the table -------------------------------------------------
 * 368x448 without the status bar. From top to bottom: the scoreboard, the
 * opponent's cards, the table's two rows, the button row (which comes and
 * goes) and your own hand. */
#define SC_Y        0
#define OPP_Y       70
#define MESA_OP_Y   170
#define MESA_MY_Y   216
#define HAND_Y      350
#define BAR_Y1      348                 /* bottom edge of the button row */
#define LIFT        8                   /* how far the chosen card is lifted */

static const int HAND_X[3] = {  69, 153, 237 };
static const int OPP_X[3]  = { 105, 153, 201 };
/* The three tricks are set well apart across the width and each one's two
 * cards deliberately overlap, offset 10 px each way: it is how they end up
 * lying on the table and you can tell at a glance whose is whose. */
static const int MESA_X[3] = {  54, 150, 246 };
#define MESA_DX     10
#define DECK_X      306                 /* the deck, top right */
#define DECK_Y      72

#define BTN_H       38
#define BTN_GAP     8
#define BTN_MAX     6

typedef struct {
    lv_obj_t *oval;
    lv_obj_t *score_cv;
    void     *score_buf;

    lv_obj_t *card[2][TR_HAND_N];
    void     *card_buf[2][TR_HAND_N];
    int32_t   cx[2][TR_HAND_N], cy[2][TR_HAND_N];   /* current position */
    int32_t   tx[2][TR_HAND_N], ty[2][TR_HAND_N];   /* destination */

    lv_obj_t *deck;
    void     *deck_buf;

    lv_obj_t *banner, *banner_lbl;
    lv_obj_t *btn[BTN_MAX], *btn_lbl[BTN_MAX + 2];   /* + the two of the chooser */
    tr_call_t btn_call[BTN_MAX];
    lv_obj_t *tantos;

    lv_timer_t *timer;
    tr_state_t  g;

    int      sel;                   /* card touched, -1 = none */
    uint32_t wait_until;
    uint32_t banner_until;
    int      deal_i;                /* -1 = not dealing */
    int      bar_n;                 /* buttons visible */
    bool     over;                  /* game over, waiting for a touch */
    bool     closing;

    bool     auto_play, showall, fast;
    uint32_t exit_ms;               /* for the "touch again to leave" */

    int32_t  won, lost;             /* games won and lost */

    /* Two watches (tl_link.h). 'me' is my seat in the engine: 0 alone or as
     * the host, 1 as the guest. Everything the UI draws goes through ME/THEM
     * and never through TR_YO/TR_EL, so the guest also sees itself at the
     * bottom of the table. */
    int       me;
    bool      link;                 /* playing against the other watch */
    bool      link_up;              /* aos_hal_link_start() went through */
    int       lk;                   /* LK_* */
    bool      is_host;
    uint32_t  nonce, their_nonce;   /* this run of the app, and the partner's */
    uint32_t  my_seed;
    uint32_t  hello_ms;             /* last hello sent */
    uint32_t  unseen_ms;            /* since when the partner has been silent */
    char      pname[AOS_LINK_NAME_MAX + 1];
    tl_act_t  inbox[TL_INBOX];      /* what came in, applied when the table is quiet */
    uint8_t   in_head, in_tail;
    bool      await_echo;           /* guest: my move went to the host, not yet back */
    uint32_t  echo_ms;
    lv_obj_t *choose[2];            /* "the machine" / "<name>" */
    lv_obj_t *wait_lbl;
} truco_t;

static truco_t s_t;

#define ME    (s_t.me)
#define THEM  (s_t.me ^ 1)

enum { LK_OFF = 0, LK_CHOOSE, LK_HELLO, LK_PLAY, LK_LOST };

static void link_begin(void);
static void solo_begin(void);
static void game_begin(uint32_t seed);
static void choose_cb(lv_event_t *e);

/* -------------------------------------------------------------------------- */

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

static bool elapsed(uint32_t deadline)
{
    return (int32_t)(now_ms() - deadline) >= 0;
}

static int delay(int ms)
{
    return s_t.fast ? (ms > 120 ? 60 : 20) : ms;
}

static bool dev_flag(const char *name)
{
    const char *v = getenv(name);
    return v && v[0] && v[0] != '0';
}

/* -------------------------------------------------------------------------- */
/* Positions                                                                   */

static void card_target(int who, int idx, int32_t *x, int32_t *y)
{
    const tr_state_t *g = &s_t.g;

    if (!g->spent[who][idx]) {
        *x = who == ME ? HAND_X[idx] : OPP_X[idx];
        *y = who == ME ? HAND_Y : OPP_Y;
        if (who == ME && s_t.sel == idx) *y -= LIFT;
        return;
    }

    /* Already played: we have to find which trick it fell in. */
    for (int t = 0; t < TR_HAND_N; t++) {
        if (g->table[who][t] == g->hand[who][idx]) {
            *x = MESA_X[t] + (who == ME ? MESA_DX : -MESA_DX);
            *y = who == ME ? MESA_MY_Y : MESA_OP_Y;
            /* the one that won the trick sits a little higher */
            if (g->trick_win[t] == who) *y -= 6;
            return;
        }
    }
    *x = DECK_X;
    *y = DECK_Y;
}

static void place(int who, int idx, int32_t x, int32_t y)
{
    s_t.cx[who][idx] = x;
    s_t.cy[who][idx] = y;
    s_t.tx[who][idx] = x;
    s_t.ty[who][idx] = y;
    if (s_t.card[who][idx]) {
        lv_obj_set_pos(s_t.card[who][idx], x - TR_PAD, y - TR_PAD);
    }
}

static void retarget_all(void)
{
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            card_target(p, i, &s_t.tx[p][i], &s_t.ty[p][i]);
        }
    }
}

/* A third of what remains per frame, with a floor of one pixel so it finishes
 * arriving. The two axes are treated separately on purpose: with a step common
 * to both, a card that only descends (dx = 0) is left oscillating around its
 * destination and the game never advances, because everything else waits for
 * nothing to be moving.
 *
 * Without lv_anim, which would have to be cancelled in destroy(): the timer
 * already exists. */
static int32_t approach(int32_t cur, int32_t tgt)
{
    int32_t d = tgt - cur;
    if (d == 0) return cur;
    int32_t step = d / 3;
    if (step == 0) step = d > 0 ? 1 : -1;
    return cur + step;
}

static bool tween(void)
{
    bool moving = false;

    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            lv_obj_t *o = s_t.card[p][i];
            if (!o) continue;
            if (s_t.cx[p][i] == s_t.tx[p][i] && s_t.cy[p][i] == s_t.ty[p][i]) continue;
            s_t.cx[p][i] = approach(s_t.cx[p][i], s_t.tx[p][i]);
            s_t.cy[p][i] = approach(s_t.cy[p][i], s_t.ty[p][i]);
            lv_obj_set_pos(o, s_t.cx[p][i] - TR_PAD, s_t.cy[p][i] - TR_PAD);
            moving = true;
        }
    }
    return moving;
}

/* -------------------------------------------------------------------------- */
/* Panel                                                                       */

static void banner(const char *text, int ms)
{
    if (!s_t.banner) return;
    /* lv_label_set_text() copies the string, so it may come from the stack. */
    lv_label_set_text(s_t.banner_lbl, text);
    lv_obj_set_style_text_font(s_t.banner_lbl,
                               strlen(text) > 18 ? aos_font_small : aos_font_body, 0);
    lv_obj_remove_flag(s_t.banner, LV_OBJ_FLAG_HIDDEN);
    s_t.banner_until = ms ? now_ms() + (uint32_t)ms : 0;
}

static void banner_hide(void)
{
    if (s_t.banner) lv_obj_add_flag(s_t.banner, LV_OBJ_FLAG_HIDDEN);
    s_t.banner_until = 0;
}

/* -------------------------------------------------------------------------- */
/* Button row                                                                  */

static const char *btn_text(tr_call_t c)
{
    switch (c) {
    case TR_C_ENVIDO:   return "ENVIDO";
    case TR_C_REAL:     return "REAL";
    case TR_C_FALTA:    return "FALTA";
    case TR_C_TRUCO:    return "TRUCO";
    case TR_C_RETRUCO:  return "RE TRUCO";
    case TR_C_VALE4:    return "VALE 4";
    case TR_C_QUIERO:   return "QUIERO";
    case TR_C_NOQUIERO: return "NO QUIERO";
    case TR_C_MAZO:     return "AL MAZO";
    default:            return "";
    }
}

/* Who is who on the banners: "VOS" for me, the other watch's name when there
 * is one, "ELLOS" for the machine. The scoreboard keeps NOS/ELLOS. */
static const char *who_name(int who)
{
    if (who == ME) return "VOS";
    return s_t.link && s_t.pname[0] ? s_t.pname : "ELLOS";
}

static const char *side_name(int who)
{
    return who == ME ? "NOS" : "ELLOS";
}

static void wait_show(bool on)
{
    if (!s_t.wait_lbl) return;
    if (on) {
        static char t[48];
        lv_snprintf(t, sizeof(t), "ESPERANDO A %s", who_name(THEM));
        lv_label_set_text(s_t.wait_lbl, t);
        lv_obj_remove_flag(s_t.wait_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_t.wait_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

static void bar_hide(void)
{
    for (int i = 0; i < BTN_MAX; i++) {
        if (s_t.btn[i]) lv_obj_add_flag(s_t.btn[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_t.bar_n = 0;
    if (s_t.tantos) lv_obj_add_flag(s_t.tantos, LV_OBJ_FLAG_HIDDEN);
}

static void bar_show(const tr_call_t *opts, int n)
{
    if (n > BTN_MAX) n = BTN_MAX;

    /* Up to three go on a single row, against the bottom edge; from four
     * upwards a second row opens above. It never covers your own hand, which
     * is the one thing that cannot be covered. */
    int row1 = n <= 3 ? n : 3;
    int row2 = n - row1;
    int y1   = row2 ? BAR_Y1 - BTN_H * 2 - BTN_GAP : BAR_Y1 - BTN_H;

    for (int i = 0; i < BTN_MAX; i++) {
        if (i >= n) {
            lv_obj_add_flag(s_t.btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        int row  = i < row1 ? 0 : 1;
        int cnt  = row ? row2 : row1;
        int k    = row ? i - row1 : i;
        int w    = (AOS_SCREEN_W - 16 - BTN_GAP * (cnt - 1)) / cnt;
        if (w > 130) w = 130;
        int x0   = (AOS_SCREEN_W - (w * cnt + BTN_GAP * (cnt - 1))) / 2;

        lv_obj_set_size(s_t.btn[i], w, BTN_H);
        lv_obj_set_pos(s_t.btn[i], x0 + k * (w + BTN_GAP),
                       y1 + row * (BTN_H + BTN_GAP));
        lv_label_set_text(s_t.btn_lbl[i], btn_text(opts[i]));
        s_t.btn_call[i] = opts[i];

        bool danger = opts[i] == TR_C_NOQUIERO || opts[i] == TR_C_MAZO;
        lv_obj_set_style_bg_color(s_t.btn[i],
                                  lv_color_hex(danger ? 0x53311E : 0x0E4A2A), 0);
        lv_obj_set_style_border_color(s_t.btn[i],
                                      lv_color_hex(danger ? 0xB07A45 : 0xC9A227), 0);
        lv_obj_remove_flag(s_t.btn[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_t.bar_n = n;
}

static void refresh_bar(void)
{
    const tr_state_t *g = &s_t.g;
    tr_call_t opts[BTN_MAX];
    int n = 0;

    if (g->turn != ME || s_t.over || s_t.auto_play || s_t.await_echo) {
        bar_hide();
        return;
    }

    if (g->phase == TR_P_ANSWER) {
        /* The call's panel is short-lived and the button row may appear
         * afterwards, so it is put back here WITHOUT a timeout: if something
         * has to be answered, it has to be written down what it is. */
        char q[48];
        lv_snprintf(q, sizeof(q), "%s: %s", who_name(THEM), tr_call_name(g->pending));
        banner(q, 0);

        opts[n++] = TR_C_QUIERO;
        opts[n++] = TR_C_NOQUIERO;
        static const tr_call_t UP[] = {
            TR_C_ENVIDO, TR_C_REAL, TR_C_FALTA,
            TR_C_TRUCO, TR_C_RETRUCO, TR_C_VALE4,
        };
        for (unsigned k = 0; k < sizeof(UP) / sizeof(UP[0]) && n < BTN_MAX; k++) {
            if (tr_can(g, ME, UP[k], 0)) opts[n++] = UP[k];
        }
    } else if (g->phase == TR_P_TURN) {
        static const tr_call_t CALLS[] = {
            TR_C_ENVIDO, TR_C_REAL, TR_C_FALTA,
            TR_C_TRUCO, TR_C_RETRUCO, TR_C_VALE4,
        };
        for (unsigned k = 0; k < sizeof(CALLS) / sizeof(CALLS[0]) && n < BTN_MAX - 1; k++) {
            if (tr_can(g, ME, CALLS[k], 0)) opts[n++] = CALLS[k];
        }
        opts[n++] = TR_C_MAZO;
    } else {
        bar_hide();
        return;
    }

    bar_show(opts, n);

    /* Your own points, while they can still be called: it is what you look
     * at. */
    if (s_t.tantos) {
        if (g->trick == 0 && !g->env_closed) {
            static char t[24];
            lv_snprintf(t, sizeof(t), "tengo %d", g->env_points[ME]);
            lv_label_set_text(s_t.tantos, t);
            lv_obj_remove_flag(s_t.tantos, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_t.tantos, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Scoreboard                                                                  */

static void score_refresh(void)
{
    tr_score_render(s_t.score_cv, s_t.g.score[ME], s_t.g.score[THEM],
                    s_t.g.mano == ME, tr_hand_value(&s_t.g));
}

static void save_prefs(void)
{
    /* The score that survives leaving is the game against the machine: a
     * game between two watches starts from zero every time. */
    if (!s_t.link) {
        aos_hal_pref_set_i32("truco_nos", s_t.g.score[ME]);
        aos_hal_pref_set_i32("truco_ellos", s_t.g.score[THEM]);
    }
    aos_hal_pref_set_i32("truco_won", s_t.won);
    aos_hal_pref_set_i32("truco_lost", s_t.lost);
}

/* -------------------------------------------------------------------------- */
/* Dealing                                                                     */

static void deal_start(void)
{
    s_t.sel = -1;
    bar_hide();
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            if (s_t.card[p][i]) lv_obj_add_flag(s_t.card[p][i], LV_OBJ_FLAG_HIDDEN);
            place(p, i, DECK_X, DECK_Y);
        }
    }
    s_t.deal_i = 0;
    score_refresh();
}

static void deal_one(void)
{
    /* Dealt one at a time and alternating, starting with the one who is not
     * the mano. */
    int k   = s_t.deal_i;
    int who = (k % 2) ? s_t.g.mano : (s_t.g.mano ^ 1);
    int idx = k / 2;

    bool face = (who == ME) || s_t.showall;
    tr_card_render(s_t.card[who][idx], face ? (int)s_t.g.hand[who][idx] : -1);
    lv_obj_remove_flag(s_t.card[who][idx], LV_OBJ_FLAG_HIDDEN);
    card_target(who, idx, &s_t.tx[who][idx], &s_t.ty[who][idx]);

    aos_hal_beep(520 + idx * 40, 12);
    s_t.deal_i++;
    s_t.wait_until = now_ms() + (uint32_t)delay(95);
}

/* -------------------------------------------------------------------------- */
/* Presenting the game's events                                                */

static void present(const tr_ev_t *ev)
{
    char buf[64];

    switch (ev->kind) {
    case TR_EV_DEAL:
        deal_start();
        break;

    case TR_EV_SAY:
        lv_snprintf(buf, sizeof(buf), "%s: %s", who_name(ev->who),
                    tr_call_name((tr_call_t)ev->a));
        banner(buf, delay(1100));
        aos_hal_beep(ev->who == ME ? 700 : 480, 45);
        s_t.wait_until = now_ms() + (uint32_t)delay(750);
        score_refresh();
        break;

    case TR_EV_PLAY:
        if (ev->who == THEM && !s_t.showall) {
            tr_card_render(s_t.card[THEM][ev->a], (int)ev->b);
        }
        retarget_all();
        aos_hal_beep(300, 18);
        s_t.wait_until = now_ms() + (uint32_t)delay(260);
        break;

    case TR_EV_TRICK:
        retarget_all();
        s_t.wait_until = now_ms() + (uint32_t)delay(ev->n == 2 ? 250 : 420);
        break;

    case TR_EV_ENVIDO:
        if (ev->a < 0) {
            lv_snprintf(buf, sizeof(buf), "ENVIDO NO QUERIDO: %d PARA %s",
                        ev->n, side_name(ev->who));
        } else {
            lv_snprintf(buf, sizeof(buf), "%d A %d: %d PARA %s",
                        ev->a, ev->b, ev->n, side_name(ev->who));
        }
        banner(buf, delay(1900));
        score_refresh();
        aos_hal_beep(ev->who == ME ? 880 : 330, 90);
        s_t.wait_until = now_ms() + (uint32_t)delay(1500);
        break;

    case TR_EV_HAND:
        lv_snprintf(buf, sizeof(buf), "LA MANO ES DE %s  (+%d)",
                    side_name(ev->who), ev->n);
        banner(buf, delay(1900));
        score_refresh();
        aos_hal_beep(ev->who == ME ? 780 : 260, 120);
        /* the cards are gathered up */
        for (int p = 0; p < 2; p++) {
            for (int i = 0; i < TR_HAND_N; i++) {
                s_t.tx[p][i] = DECK_X;
                s_t.ty[p][i] = DECK_Y;
            }
        }
        s_t.wait_until = now_ms() + (uint32_t)delay(1600);
        break;

    case TR_EV_GAME:
        lv_snprintf(buf, sizeof(buf), "%s LA PARTIDA  %d-%d",
                    ev->who == ME ? "GANASTE" : "PERDISTE",
                    s_t.g.score[ME], s_t.g.score[THEM]);
        banner(buf, 0);
        if (ev->who == ME) s_t.won++; else s_t.lost++;
        s_t.over = true;
        bar_hide();
        wait_show(false);
        aos_hal_beep(ev->who == ME ? 990 : 200, 250);
        s_t.g.score[0] = 0;
        s_t.g.score[1] = 0;
        save_prefs();
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------------- */

/* ---- two watches: the wire -------------------------------------------------
 *
 * Host-ordered lockstep. The engine is deterministic given its seed, so both
 * watches run the same tr_game.c: the host picks the seed, every move goes
 * through the host, and the host echoes the moves in the order it applied
 * them. The guest applies nothing on its own, not even its own taps: it sends
 * them and waits for the echo. Moves that arrive wait in the inbox until the
 * table is quiet (nothing moving, no event pending), exactly where the
 * machine would have thought its move, so the animations keep their pace on
 * both screens even when one of them is behind. */
static bool inbox_push(const tl_act_t *a)
{
    uint8_t next = (uint8_t)((s_t.in_head + 1) % TL_INBOX);
    if (next == s_t.in_tail) return false;
    s_t.inbox[s_t.in_head] = *a;
    s_t.in_head = next;
    return true;
}

static bool inbox_pop(tl_act_t *a)
{
    if (s_t.in_head == s_t.in_tail) return false;
    *a = s_t.inbox[s_t.in_tail];
    s_t.in_tail = (uint8_t)((s_t.in_tail + 1) % TL_INBOX);
    return true;
}

static void send_act(int who, tr_call_t c, int arg)
{
    tl_act_t a = { .type = TL_ACT, .proto = TL_PROTO,
                   .who = (uint8_t)who, .call = (uint8_t)c, .arg = (int8_t)arg };
    if (!aos_hal_link_send_reliable(&a, sizeof a)) {
        aos_hal_log("truco", "act %d/%d not sent", who, (int)c);
    }
}

static void send_hello(void)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    tl_hello_t h = { .type = TL_HELLO, .proto = TL_PROTO,
                     .seed = s_t.my_seed, .nonce = s_t.nonce };
    memcpy(h.mac, st.own_mac, 6);
    aos_hal_link_send_reliable(&h, sizeof h);
    s_t.hello_ms = now_ms();
}

static void link_lost(const char *why)
{
    if (s_t.lk == LK_LOST) return;
    aos_hal_log("truco", "link lost: %s", why);
    s_t.lk = LK_LOST;
    s_t.over = false;
    bar_hide();
    wait_show(false);
    banner(why, 0);
    aos_hal_beep(200, 200);
}

static void on_hello(const tl_hello_t *h)
{
    if (s_t.lk == LK_PLAY && h->nonce == s_t.their_nonce) return;   /* a repeat */
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    s_t.their_nonce = h->nonce;
    s_t.is_host = memcmp(st.own_mac, h->mac, 6) < 0;
    s_t.me = s_t.is_host ? 0 : 1;
    if (s_t.lk == LK_PLAY) {
        aos_hal_log("truco", "partner restarted");   /* a new game, like them */
    }
    s_t.lk = LK_PLAY;
    send_hello();                       /* so the other side decides too */
    aos_hal_log("truco", "role: %s, seat %d", s_t.is_host ? "host" : "guest", s_t.me);
    game_begin(s_t.is_host ? s_t.my_seed : h->seed);
}

static void link_tick(void)
{
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) {
        if (f.len < 2 || f.data[1] != TL_PROTO) continue;
        if (f.data[0] == TL_HELLO && f.len >= sizeof(tl_hello_t)) {
            on_hello((const tl_hello_t *)f.data);
        } else if (f.data[0] == TL_ACT && f.len >= sizeof(tl_act_t)) {
            if (s_t.lk == LK_PLAY && !inbox_push((const tl_act_t *)f.data)) {
                link_lost("SE DESINCRONIZO");
            }
        }
    }
    if (s_t.lk == LK_HELLO) {
        aos_link_partner_t p;
        if (!aos_hal_link_partner(&p) || !p.valid) return;
        if (!s_t.pname[0]) {
            snprintf(s_t.pname, sizeof s_t.pname, "%s", p.name);
            for (char *c = s_t.pname; *c; c++) {
                if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
            }
        }
        if (aos_hal_link_reliable_lost()) aos_hal_link_reliable_reset();
        if (now_ms() - s_t.hello_ms >= 500) send_hello();
        return;
    }
    if (s_t.lk != LK_PLAY) return;
    if (aos_hal_link_reliable_lost()) {
        link_lost("SE PERDIO EL ENLACE");
        return;
    }
    /* The partner stopped beaconing (left the link) or offers another app
     * (left Truco): otherwise we would wait for a move for ever. */
    aos_link_partner_t p;
    uint32_t now = now_ms();
    bool gone = aos_hal_link_partner(&p) && p.valid && !p.seen;
    if (!gone) {
        aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
        int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
        for (int i = 0; i < n; i++) {
            if (memcmp(nb[i].mac, p.mac, 6) == 0 && nb[i].app[0] && strcmp(nb[i].app, "truco") != 0) {
                gone = true;
            }
        }
    }
    if (!gone) {
        s_t.unseen_ms = now;
    } else if (now - s_t.unseen_ms > 6000) {
        char buf[48];
        lv_snprintf(buf, sizeof buf, "SE FUE %s", who_name(THEM));
        link_lost(buf);
    }
    if (s_t.await_echo && now - s_t.echo_ms > 4000) {
        s_t.await_echo = false;         /* the host did not take it: the bar comes back */
    }
}

/* One move that came over the link, once the table is quiet. Returns true if
 * something was applied (the caller then waits before the next). */
static bool link_apply_one(void)
{
    tl_act_t a;
    if (!inbox_pop(&a)) return false;
    if (s_t.is_host && a.who != THEM) {
        return false;                   /* the guest may only move for itself */
    }
    if (!tr_apply(&s_t.g, a.who, (tr_call_t)a.call, a.arg)) {
        if (s_t.is_host) {
            aos_hal_log("truco", "guest move refused: %d/%d", (int)a.call, (int)a.arg);
            return false;               /* not legal here: it is simply not echoed */
        }
        link_lost("SE DESINCRONIZO");   /* the host's stream must always apply */
        return false;
    }
    if (s_t.is_host) send_act(a.who, (tr_call_t)a.call, a.arg);
    if (a.who == ME) s_t.await_echo = false;
    s_t.sel = -1;
    bar_hide();
    wait_show(false);
    banner_hide();
    retarget_all();
    s_t.wait_until = now_ms() + (uint32_t)delay(a.who == ME ? 0 : 320);
    return true;
}

static void do_call(tr_call_t c, int arg)
{
    if (s_t.link) {
        if (s_t.lk != LK_PLAY || s_t.await_echo) return;
        if (!s_t.is_host) {
            /* the guest proposes; the move happens when the host echoes it */
            if (!tr_can(&s_t.g, ME, c, arg)) return;
            send_act(ME, c, arg);
            s_t.await_echo = true;
            s_t.echo_ms = now_ms();
            s_t.sel = -1;
            bar_hide();
            return;
        }
    }
    if (!tr_apply(&s_t.g, ME, c, arg)) return;
    if (s_t.link) send_act(ME, c, arg);
    s_t.sel = -1;
    bar_hide();
    banner_hide();
    retarget_all();
}

static void btn_cb(lv_event_t *e)
{
    if (s_t.closing) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_t.bar_n) return;
    do_call(s_t.btn_call[i], 0);
}

/* The game ended: the next touch starts another. */
static void continue_after_end(void)
{
    s_t.over = false;
    banner_hide();
    tr_new_hand(&s_t.g);
}

static void card_cb(lv_event_t *e)
{
    if (s_t.closing || s_t.auto_play) return;

    int i = (int)(intptr_t)lv_event_get_user_data(e);
    tr_state_t *g = &s_t.g;

    /* A click on a card does not reach the root, so the "touch to carry on" at
     * the end of a game has to be handled here as well. */
    if (s_t.lk == LK_LOST) { aos_ui_back(); return; }
    if (s_t.over) { continue_after_end(); return; }
    if (s_t.link && (s_t.lk != LK_PLAY || s_t.await_echo)) return;
    if (g->phase != TR_P_TURN || g->turn != ME) return;
    if (!tr_can(g, ME, TR_C_PLAY, i)) return;

    /* Two taps: the first lifts the card, the second plays it. In 368 px with
     * the card 60 wide, playing on one tap goes wrong often. */
    if (s_t.sel != i) {
        s_t.sel = i;
        retarget_all();
        aos_hal_beep(620, 10);
        return;
    }
    do_call(TR_C_PLAY, i);
}

static void root_cb(lv_event_t *e)
{
    (void)e;
    if (s_t.closing) return;
    if (s_t.lk == LK_LOST) { aos_ui_back(); return; }

    if (s_t.over) {
        continue_after_end();
        return;
    }
    /* touching the baize deselects */
    if (s_t.sel >= 0) {
        s_t.sel = -1;
        retarget_all();
    }
}

/* -------------------------------------------------------------------------- */

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_t.closing) return;
    if (s_t.lk == LK_CHOOSE) return;
    if (s_t.link) link_tick();
    if (s_t.lk == LK_HELLO || s_t.lk == LK_LOST) return;

    bool moving = tween();

    if (s_t.banner_until && elapsed(s_t.banner_until)) banner_hide();

    if (s_t.deal_i >= 0) {
        if (s_t.deal_i < TR_HAND_N * 2) {
            if (elapsed(s_t.wait_until)) deal_one();
        } else if (!moving) {
            s_t.deal_i = -1;
            s_t.wait_until = now_ms() + (uint32_t)delay(200);
        }
        return;
    }

    if (moving || !elapsed(s_t.wait_until)) return;

    tr_ev_t ev;
    if (tr_pop_event(&s_t.g, &ev)) {
        present(&ev);
        return;
    }

    if (s_t.over) {
        /* In automatic mode there is nobody to touch the screen: another game
         * starts by itself, which is what makes leaving it running useful. */
        if (s_t.auto_play) continue_after_end();
        return;
    }

    if (s_t.g.phase == TR_P_HAND_END) {
        tr_new_hand(&s_t.g);
        return;
    }
    if (s_t.g.phase == TR_P_GAME_END) {
        return;                     /* the TR_EV_GAME event already went through present() */
    }

    int turn = s_t.g.turn;
    if (s_t.link) {
        if (link_apply_one()) return;
        if (turn == ME && !s_t.await_echo) {
            wait_show(false);
            if (s_t.bar_n == 0) refresh_bar();
        } else {
            wait_show(true);
        }
        return;
    }
    if (turn == THEM || s_t.auto_play) {
        int arg = 0;
        tr_call_t c = tr_ai_decide(&s_t.g, turn, &arg);
        if (c == TR_C_NONE) return;
        bar_hide();
        tr_apply(&s_t.g, turn, c, arg);
        retarget_all();
        s_t.wait_until = now_ms() + (uint32_t)delay(320);
        return;
    }

    if (s_t.bar_n == 0) refresh_bar();
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *make_button(lv_obj_t *parent, int i)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x0E4A2A), 0);
    lv_obj_set_style_bg_opa(b, 235, 0);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xC9A227), 0);
    lv_obj_set_style_border_opa(b, 200, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    s_t.btn_lbl[i] = lv_label_create(b);
    lv_obj_set_style_text_font(s_t.btn_lbl[i], aos_font_small, 0);
    lv_obj_set_style_text_color(s_t.btn_lbl[i], lv_color_hex(0xF3EBD2), 0);
    lv_obj_center(s_t.btn_lbl[i]);
    lv_obj_remove_flag(s_t.btn_lbl[i], LV_OBJ_FLAG_CLICKABLE);
    return b;
}

static void *truco_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    memset(&s_t, 0, sizeof(s_t));
    s_t.sel    = -1;
    s_t.deal_i = -1;

    s_t.auto_play = dev_flag("TRUCO_AUTO");
    s_t.showall   = dev_flag("TRUCO_SHOWALL");
    s_t.fast      = dev_flag("TRUCO_FAST");

    /* The baize: a vertical gradient and a lighter oval where the cards fall.
     * All with styles, no canvas: it is a still background and it is not worth
     * spending a full-screen buffer on it. */
    /* Beware lv_obj_remove_style_all(root): in LVGL 9 the width and the height
     * ARE local style properties, so deleting them leaves the root at content
     * size and the table appears clipped to a small rectangle in the top left,
     * with no error at all. The runtime has already cleaned it before calling
     * us: here it only has to be painted. */
    lv_obj_set_style_bg_color(root, lv_color_hex(0x14522F), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(0x072A16), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, root_cb, LV_EVENT_CLICKED, NULL);

    s_t.oval = lv_obj_create(root);
    lv_obj_remove_style_all(s_t.oval);
    lv_obj_set_size(s_t.oval, 344, 216);
    lv_obj_set_pos(s_t.oval, 12, 150);
    lv_obj_set_style_radius(s_t.oval, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_t.oval, lv_color_hex(0x1C7A44), 0);
    lv_obj_set_style_bg_opa(s_t.oval, 50, 0);
    lv_obj_set_style_border_width(s_t.oval, 2, 0);
    lv_obj_set_style_border_color(s_t.oval, lv_color_hex(0xC9A227), 0);
    lv_obj_set_style_border_opa(s_t.oval, 55, 0);
    aos_make_decorative(s_t.oval);

    /* Scoreboard */
    s_t.score_buf = malloc((size_t)TR_SCORE_W * TR_SCORE_H * 4);
    if (!s_t.score_buf) {
        aos_ui_toast("Sin memoria", 2000);
        return NULL;
    }
    s_t.score_cv = lv_canvas_create(root);
    lv_canvas_set_buffer(s_t.score_cv, s_t.score_buf, TR_SCORE_W, TR_SCORE_H,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_set_size(s_t.score_cv, TR_SCORE_W, TR_SCORE_H);
    lv_obj_set_pos(s_t.score_cv, 0, SC_Y);
    lv_obj_set_style_radius(s_t.score_cv, 0, 0);
    lv_image_set_antialias(s_t.score_cv, false);
    lv_obj_remove_flag(s_t.score_cv, LV_OBJ_FLAG_CLICKABLE);

    /* The six cards. The opponent's first so that yours end up on top when
     * they overlap on the table. */
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            s_t.card[p][i] = tr_card_canvas(root, &s_t.card_buf[p][i]);
            if (!s_t.card[p][i]) {
                aos_ui_toast("Sin memoria", 2000);
                return NULL;
            }
            lv_obj_add_flag(s_t.card[p][i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* The deck, for show: it is where the cards come from and go back to. */
    s_t.deck = tr_card_canvas(root, &s_t.deck_buf);
    if (!s_t.deck) {
        aos_ui_toast("Sin memoria", 2000);
        return NULL;
    }
    tr_card_render(s_t.deck, -1);
    lv_obj_set_pos(s_t.deck, DECK_X - TR_PAD, DECK_Y - TR_PAD);

    /* Panel */
    s_t.banner = lv_obj_create(root);
    lv_obj_remove_style_all(s_t.banner);
    lv_obj_set_size(s_t.banner, 344, 40);
    lv_obj_set_pos(s_t.banner, 12, 116);
    lv_obj_set_style_bg_color(s_t.banner, lv_color_hex(0x08240F), 0);
    lv_obj_set_style_bg_opa(s_t.banner, 225, 0);
    lv_obj_set_style_radius(s_t.banner, 12, 0);
    lv_obj_set_style_border_width(s_t.banner, 2, 0);
    lv_obj_set_style_border_color(s_t.banner, lv_color_hex(0xC9A227), 0);
    lv_obj_add_flag(s_t.banner, LV_OBJ_FLAG_HIDDEN);
    s_t.banner_lbl = lv_label_create(s_t.banner);
    lv_obj_set_style_text_font(s_t.banner_lbl, aos_font_body, 0);
    lv_obj_set_style_text_color(s_t.banner_lbl, lv_color_hex(0xF6EFD8), 0);
    lv_obj_center(s_t.banner_lbl);
    aos_make_decorative(s_t.banner);

    /* Button row */
    for (int i = 0; i < BTN_MAX; i++) s_t.btn[i] = make_button(root, i);

    s_t.tantos = lv_label_create(root);
    lv_obj_set_style_text_font(s_t.tantos, aos_font_small, 0);
    lv_obj_set_style_text_color(s_t.tantos, lv_color_hex(0x9FD3AE), 0);
    lv_obj_set_pos(s_t.tantos, 6, 150);
    lv_obj_add_flag(s_t.tantos, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_t.tantos, LV_OBJ_FLAG_CLICKABLE);

    s_t.wait_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(s_t.wait_lbl, aos_font_small, 0);
    lv_obj_set_style_text_color(s_t.wait_lbl, lv_color_hex(0x9FD3AE), 0);
    lv_obj_set_width(s_t.wait_lbl, AOS_SCREEN_W);
    lv_obj_set_style_text_align(s_t.wait_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_t.wait_lbl, 0, BAR_Y1 - 26);
    lv_obj_add_flag(s_t.wait_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_t.wait_lbl, LV_OBJ_FLAG_CLICKABLE);
    score_refresh();                /* the canvas comes from malloc: paint it before asking */

    int32_t v = 0;
    if (aos_hal_pref_get_i32("truco_won", &v))  s_t.won  = v;
    if (aos_hal_pref_get_i32("truco_lost", &v)) s_t.lost = v;
    s_t.my_seed = (uint32_t)aos_hal_uptime_ms() * 2654435761u + 1u;
    const char *sd = getenv("TRUCO_SEED");
    if (sd && sd[0]) s_t.my_seed = (uint32_t)atoi(sd);
    s_t.nonce = s_t.my_seed ^ 0xA5A5F00Du;
    s_t.timer = lv_timer_create(tick_cb, 33, NULL);

    /* Against whom: with a partner paired in Enlace the table asks first.
     * TRUCO_LINK=1 skips the question (the simulator has no partner in its
     * preferences until the link is up). */
    aos_link_partner_t p;
    bool have_partner = aos_hal_link_partner(&p) && p.valid;
    if (dev_flag("TRUCO_LINK")) {
        link_begin();
    } else if (have_partner && !s_t.auto_play) {
        snprintf(s_t.pname, sizeof s_t.pname, "%s", p.name);
        for (char *c = s_t.pname; *c; c++) {
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        }
        s_t.lk = LK_CHOOSE;
        banner("CONTRA QUIEN?", 0);
        for (int i = 0; i < 2; i++) {
            lv_obj_t *b = make_button(root, BTN_MAX + i);   /* out of the bar: its own callback */
            lv_obj_remove_event_cb(b, btn_cb);
            lv_obj_add_event_cb(b, choose_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_set_size(b, 280, 46);
            lv_obj_set_pos(b, (AOS_SCREEN_W - 280) / 2, 190 + i * 62);
            lv_label_set_text(s_t.btn_lbl[BTN_MAX + i], i == 0 ? "LA MAQUINA" : s_t.pname);
            lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
            s_t.choose[i] = b;
        }
    } else {
        solo_begin();
    }
    return &s_t;
}

/* The six cards' owners are only known once the seat is: mine get the tap
 * and sit on top of the other's when they overlap on the table. */
static void game_begin(uint32_t seed)
{
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            lv_obj_t *c = s_t.card[p][i];
            lv_obj_remove_event_cb(c, card_cb);
            if (p == ME) {
                lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            } else {
                lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
    /* the cards were created 0 then 1; if I am 1 mine are already on top */
    if (ME == 0) {
        for (int i = 0; i < TR_HAND_N; i++) {
            if (lv_obj_get_index(s_t.card[0][i]) < lv_obj_get_index(s_t.card[1][i])) {
                lv_obj_swap(s_t.card[0][i], s_t.card[1][i]);
            }
        }
    }
    s_t.in_head = s_t.in_tail = 0;
    s_t.await_echo = false;
    s_t.over = false;
    s_t.sel  = -1;
    s_t.deal_i = -1;
    s_t.unseen_ms = now_ms();
    bar_hide();
    wait_show(false);
    banner_hide();
    tr_new_game(&s_t.g, seed);
    score_refresh();
}

static void solo_begin(void)
{
    s_t.me = TR_YO;
    game_begin(s_t.my_seed);
    int32_t v = 0;
    if (aos_hal_pref_get_i32("truco_nos", &v) && v > 0 && v < TR_TARGET) {
        s_t.g.score[TR_YO] = v;
    }
    if (aos_hal_pref_get_i32("truco_ellos", &v) && v > 0 && v < TR_TARGET) {
        s_t.g.score[TR_EL] = v;
    }
    const char *sc = getenv("TRUCO_SCORE");
    if (sc && sc[0]) {
        int a = 0, b = 0;
        if (sscanf(sc, "%d,%d", &a, &b) == 2) {
            s_t.g.score[TR_YO] = a < 0 ? 0 : (a > 29 ? 29 : a);
            s_t.g.score[TR_EL] = b < 0 ? 0 : (b > 29 ? 29 : b);
        }
    }
    score_refresh();
}

static void link_begin(void)
{
    s_t.link_up = aos_hal_link_start();
    if (!s_t.link_up) {
        aos_ui_toast("Sin enlace: jugas contra la maquina", 2500);
        s_t.link = false;
        solo_begin();
        return;
    }
    s_t.link = true;
    aos_hal_link_offer("truco");
    aos_hal_link_reliable_reset();
    s_t.lk = LK_HELLO;
    s_t.hello_ms = 0;
    char buf[48];
    lv_snprintf(buf, sizeof buf, "BUSCANDO A %s", s_t.pname[0] ? s_t.pname : "LA PAREJA");
    banner(buf, 0);
}

static void choose_cb(lv_event_t *e)
{
    if (s_t.closing || s_t.lk != LK_CHOOSE) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    for (int k = 0; k < 2; k++) {
        if (s_t.choose[k]) {
            lv_obj_delete(s_t.choose[k]);
            s_t.choose[k] = NULL;
            s_t.btn_lbl[BTN_MAX + k] = NULL;
        }
    }
    banner_hide();
    s_t.lk = LK_OFF;
    aos_hal_beep(620, 15);
    if (i == 0) solo_begin(); else link_begin();
}

static void truco_destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    s_t.closing = true;

    if (s_t.timer) {
        lv_timer_delete(s_t.timer);
        s_t.timer = NULL;
    }
    save_prefs();
    if (s_t.link_up) {
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }

    /* The objects first, with the context still standing: each canvas points
     * at a buffer we are about to free. */
    if (self && self->root) lv_obj_clean(self->root);

    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) free(s_t.card_buf[p][i]);
    }
    free(s_t.deck_buf);
    free(s_t.score_buf);
    memset(&s_t, 0, sizeof(s_t));
}

/* Leaving in the middle of a hand loses the hand, so the first gesture warns
 * and the second leaves. */
static bool truco_back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;

    if (s_t.over || s_t.g.phase == TR_P_GAME_END) return false;
    if (s_t.lk == LK_CHOOSE || s_t.lk == LK_HELLO || s_t.lk == LK_LOST) return false;

    uint32_t now = now_ms();
    if (s_t.exit_ms && (int32_t)(now - s_t.exit_ms) < 3000) return false;

    s_t.exit_ms = now;
    aos_ui_toast("Toca de nuevo para dejar la mano", 2500);
    return true;
}

static bool truco_init(aos_app_t *app)
{
    app->desc.id      = "aos.truco";
    app->desc.name    = "Truco";
    app->desc.icon    = "Tr";
    app->desc.icon_vec = AOS_ICON_CARDS;
    app->desc.color_a = 0x1E7A45;
    app->desc.color_b = 0x0A3C21;
    app->desc.order   = 152;
    app->desc.flags   = AOS_APP_FLAG_FULLSCREEN | AOS_APP_FLAG_KEEP_AWAKE;

    app->create  = truco_create;
    app->destroy = truco_destroy;
    app->back    = truco_back;
    return true;
}

AOS_APP_ENTRY(truco_init);
