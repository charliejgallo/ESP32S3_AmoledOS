/*
 * BLACKJACK - the rules. See bj_game.h.
 *
 * The queue: every action turns into a short list of operations, run one per
 * bj_step(). An operation may queue more, always at the end and only when it
 * is the last one queued (the dealer drawing, one hand after another), so a
 * plain FIFO keeps the order a real table would.
 */
#include "bj_game.h"

#include <string.h>

enum {
    OP_SHUFFLE = 1,
    OP_DEAL_P,          /* + hand index in the high bits, see op_arg()      */
    OP_DEAL_D_UP,
    OP_DEAL_D_DOWN,
    OP_AFTER_DEAL,      /* insurance, peek, naturals                        */
    OP_PEEK,
    OP_CHECK,           /* the active hand after a card: 21 or bust ends it */
    OP_FINISH,          /* the active hand is done: next hand or dealer     */
    OP_NEXT,
    OP_SPLIT,
    OP_REVEAL,
    OP_DEALER,          /* draw one more, or stop                           */
    OP_SETTLE,
    OP_RESULT,          /* + hand index                                     */
    OP_INS_RESULT,
    OP_OVER,
};

#define OP(op, arg)  ((uint8_t)((op) | ((arg) << 5)))
#define OP_CODE(v)   ((v) & 0x1F)
#define OP_ARG(v)    ((v) >> 5)

/* --- randomness ----------------------------------------------------------- */

static uint32_t rnd(bj_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}

static void shuffle(bj_game_t *g)
{
    for (int i = 0; i < BJ_SHOE_N; i++) {
        g->shoe[i] = (uint8_t)(i % 52);
    }
    for (int i = BJ_SHOE_N - 1; i > 0; i--) {
        int     j = (int)(rnd(g) % (uint32_t)(i + 1));
        uint8_t t = g->shoe[i];
        g->shoe[i] = g->shoe[j];
        g->shoe[j] = t;
    }
    g->pos = 0;
    /* the cut card, somewhere around three quarters in */
    g->cut = (int16_t)(BJ_SHOE_N * 3 / 4 - 8 + (int)(rnd(g) % 17));
}

static uint8_t draw(bj_game_t *g)
{
    if (g->pos >= BJ_SHOE_N) {          /* never with the cut card, but */
        shuffle(g);
    }
    return g->shoe[g->pos++];
}

/* --- values --------------------------------------------------------------- */

int bj_card_value(int card)
{
    int r = card % 13;
    return r == 0 ? 1 : (r >= 9 ? 10 : r + 1);
}

int bj_total(const bj_hand_t *h, bool *soft)
{
    int  t   = 0;
    bool ace = false;
    for (int i = 0; i < h->n; i++) {
        int v = bj_card_value(h->card[i]);
        t += v;
        ace |= v == 1;
    }
    bool s = ace && t + 10 <= 21;
    if (soft) {
        *soft = s;
    }
    return s ? t + 10 : t;
}

bool bj_is_blackjack(const bj_hand_t *h)
{
    return h->n == 2 && !h->split && bj_total(h, NULL) == 21;
}

/* --- the queue ------------------------------------------------------------ */

static void push(bj_game_t *g, uint8_t op)
{
    if (g->qn < sizeof g->q) {
        g->q[(g->qh + g->qn) % sizeof g->q] = op;
        g->qn++;
    }
}

bool bj_busy(const bj_game_t *g)
{
    return g->qn > 0;
}

/* --- setting up ----------------------------------------------------------- */

void bj_game_init(bj_game_t *g, uint32_t seed, int32_t bank)
{
    memset(g, 0, sizeof *g);
    g->rng   = seed ? seed : 0x9E3779B9u;
    g->bank  = bank > 0 ? bank : BJ_BANK0;
    g->best  = g->bank;
    g->bet   = BJ_BET_MIN * 5;
    if (g->bet > g->bank) {
        g->bet = g->bank - g->bank % BJ_BET_MIN;
    }
    g->state = BJ_ST_BET;
    shuffle(g);
}

void bj_bet_add(bj_game_t *g, int32_t chips)
{
    if (g->state != BJ_ST_BET) {
        return;
    }
    int32_t b = g->bet + chips;
    if (b > BJ_BET_MAX) {
        b = BJ_BET_MAX;
    }
    if (b > g->bank) {
        b = g->bank - g->bank % BJ_BET_MIN;
    }
    g->bet = b < 0 ? 0 : b;
}

void bj_bet_clear(bj_game_t *g)
{
    if (g->state == BJ_ST_BET) {
        g->bet = 0;
    }
}

bool bj_can_deal(const bj_game_t *g)
{
    return g->state == BJ_ST_BET && g->bet >= BJ_BET_MIN && g->bet <= g->bank;
}

bool bj_deal(bj_game_t *g)
{
    if (!bj_can_deal(g)) {
        return false;
    }
    memset(&g->dealer, 0, sizeof g->dealer);
    memset(g->hand, 0, sizeof g->hand);
    g->nhands    = 1;
    g->active    = 0;
    g->hole_up   = false;
    g->insurance = 0;
    g->hand[0].bet = g->bet;
    g->bank -= g->bet;
    g->state = BJ_ST_DEAL;

    if (g->pos >= g->cut) {
        push(g, OP_SHUFFLE);
    }
    push(g, OP(OP_DEAL_P, 0));
    push(g, OP_DEAL_D_UP);
    push(g, OP(OP_DEAL_P, 0));
    push(g, OP_DEAL_D_DOWN);
    push(g, OP_AFTER_DEAL);
    return true;
}

/* --- the player ----------------------------------------------------------- */

static bj_hand_t *cur(bj_game_t *g)
{
    return &g->hand[g->active];
}

static bool can_act(const bj_game_t *g)
{
    return g->state == BJ_ST_PLAYER && !bj_busy(g) && !g->hand[g->active].done;
}

bool bj_can_double(const bj_game_t *g)
{
    const bj_hand_t *h = &g->hand[g->active];
    return can_act(g) && h->n == 2 && !h->aces && g->bank >= h->bet;
}

bool bj_can_split(const bj_game_t *g)
{
    const bj_hand_t *h = &g->hand[g->active];
    return can_act(g) && g->nhands == 1 && h->n == 2 &&
           bj_card_value(h->card[0]) == bj_card_value(h->card[1]) && g->bank >= h->bet;
}

void bj_hit(bj_game_t *g)
{
    if (!can_act(g)) {
        return;
    }
    push(g, OP(OP_DEAL_P, g->active));
    push(g, OP_CHECK);
}

void bj_stand(bj_game_t *g)
{
    if (!can_act(g)) {
        return;
    }
    push(g, OP_FINISH);
}

bool bj_double(bj_game_t *g)
{
    if (!bj_can_double(g)) {
        return false;
    }
    bj_hand_t *h = cur(g);
    g->bank -= h->bet;
    h->bet *= 2;
    h->doubled = true;
    push(g, OP(OP_DEAL_P, g->active));
    push(g, OP_FINISH);
    return true;
}

bool bj_split(bj_game_t *g)
{
    if (!bj_can_split(g)) {
        return false;
    }
    bj_hand_t *a = &g->hand[0], *b = &g->hand[1];
    g->bank -= a->bet;
    memset(b, 0, sizeof *b);
    b->card[0] = a->card[1];
    b->n       = 1;
    b->bet     = a->bet;
    a->n       = 1;
    a->split   = b->split = true;
    a->aces    = b->aces = bj_card_value(a->card[0]) == 1;
    g->nhands  = 2;
    push(g, OP_SPLIT);
    push(g, OP(OP_DEAL_P, 0));
    if (a->aces) {
        /* one card each and nothing more: both are finished by the deal */
        push(g, OP(OP_DEAL_P, 1));
        push(g, OP_FINISH);
    } else {
        push(g, OP_CHECK);
    }
    return true;
}

void bj_insure(bj_game_t *g, bool yes)
{
    if (g->state != BJ_ST_INSURANCE || bj_busy(g)) {
        return;
    }
    if (yes) {
        int32_t ins = g->hand[0].bet / 2;
        if (ins <= g->bank) {
            g->insurance = ins;
            g->bank -= ins;
        }
    }
    push(g, OP_PEEK);
}

void bj_next_round(bj_game_t *g)
{
    if (g->state != BJ_ST_OVER) {
        return;
    }
    g->state = BJ_ST_BET;
    if (g->bet > g->bank) {
        g->bet = g->bank - g->bank % BJ_BET_MIN;
    }
    if (g->bet > BJ_BET_MAX) {
        g->bet = BJ_BET_MAX;
    }
}

bool bj_broke(const bj_game_t *g)
{
    return g->bank < BJ_BET_MIN;
}

void bj_refill(bj_game_t *g)
{
    g->bank = BJ_BANK0;
    g->best = g->bank;
    g->bet  = BJ_BET_MIN * 5;
    g->rounds = g->wins = g->pushes = g->losses = g->blackjacks = 0;
    g->refills++;
}

/* --- the work ------------------------------------------------------------- */

static bj_ev_t ev(uint8_t type)
{
    bj_ev_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    return e;
}

static bool all_bust(const bj_game_t *g)
{
    for (int i = 0; i < g->nhands; i++) {
        if (bj_total(&g->hand[i], NULL) <= 21) {
            return false;
        }
    }
    return true;
}

static int up_value(const bj_game_t *g)
{
    return bj_card_value(g->dealer.card[0]);
}

static void settle_hand(bj_game_t *g, int i)
{
    bj_hand_t *h  = &g->hand[i];
    int        pt = bj_total(h, NULL);
    int        dt = bj_total(&g->dealer, NULL);
    bool       pb = bj_is_blackjack(h);
    bool       db = bj_is_blackjack(&g->dealer);
    if (pt > 21) {
        h->result = BJ_RES_BUST;
        h->paid   = 0;
    } else if (pb && db) {
        h->result = BJ_RES_PUSH;
        h->paid   = h->bet;
    } else if (pb) {
        h->result = BJ_RES_BLACKJACK;
        h->paid   = h->bet + h->bet * 3 / 2;
    } else if (db) {
        h->result = BJ_RES_LOSE;
        h->paid   = 0;
    } else if (dt > 21 || pt > dt) {
        h->result = BJ_RES_WIN;
        h->paid   = h->bet * 2;
    } else if (pt == dt) {
        h->result = BJ_RES_PUSH;
        h->paid   = h->bet;
    } else {
        h->result = BJ_RES_LOSE;
        h->paid   = 0;
    }
    g->bank += h->paid;
}

bj_ev_t bj_step(bj_game_t *g)
{
    if (!g->qn) {
        return ev(BJ_EV_NONE);
    }
    uint8_t v = g->q[g->qh];
    g->qh = (uint8_t)((g->qh + 1) % sizeof g->q);
    g->qn--;

    bj_ev_t e = ev(BJ_EV_NONE);
    switch (OP_CODE(v)) {
    case OP_SHUFFLE:
        shuffle(g);
        return ev(BJ_EV_SHUFFLE);

    case OP_DEAL_P: {
        bj_hand_t *h = &g->hand[OP_ARG(v)];
        e = ev(BJ_EV_CARD);
        e.hand = (int8_t)OP_ARG(v);
        e.idx  = h->n;
        e.card = draw(g);
        e.up   = true;
        if (h->n < BJ_HAND_MAX) {
            h->card[h->n++] = e.card;
        }
        return e;
    }
    case OP_DEAL_D_UP:
    case OP_DEAL_D_DOWN:
        e = ev(BJ_EV_CARD);
        e.hand = -1;
        e.idx  = g->dealer.n;
        e.card = draw(g);
        e.up   = OP_CODE(v) == OP_DEAL_D_UP;
        if (g->dealer.n < BJ_HAND_MAX) {
            g->dealer.card[g->dealer.n++] = e.card;
        }
        return e;

    case OP_AFTER_DEAL:
        if (up_value(g) == 1 && g->bank >= g->hand[0].bet / 2) {
            g->state = BJ_ST_INSURANCE;
            return ev(BJ_EV_NONE);
        }
        push(g, OP_PEEK);
        return bj_step(g);

    case OP_PEEK: {
        int  up = up_value(g);
        bool db = bj_is_blackjack(&g->dealer);
        if ((up == 1 || up == 10) && db) {
            push(g, OP_REVEAL);
            push(g, OP_SETTLE);
            g->state = BJ_ST_DEALER;
            return bj_step(g);
        }
        if (g->insurance) {
            push(g, OP_INS_RESULT);         /* lost, and said so first */
        }
        if (bj_is_blackjack(&g->hand[0])) {
            push(g, OP_REVEAL);
            push(g, OP_SETTLE);
            g->state = BJ_ST_DEALER;
            return bj_step(g);
        }
        g->state  = BJ_ST_PLAYER;
        g->active = 0;
        if (g->qn) {
            return bj_step(g);
        }
        e = ev(BJ_EV_TURN);
        e.hand = 0;
        return e;
    }

    case OP_SPLIT:
        return ev(BJ_EV_SPLIT);

    case OP_CHECK: {
        bj_hand_t *h = cur(g);
        if (bj_total(h, NULL) >= 21) {
            push(g, OP_FINISH);
            return bj_step(g);
        }
        e = ev(BJ_EV_TURN);
        e.hand = (int8_t)g->active;
        return e;
    }

    case OP_FINISH:
        if (g->hand[0].aces && g->nhands == 2) {
            g->hand[0].done = g->hand[1].done = true;
            g->active = 1;
        } else {
            cur(g)->done = true;
        }
        push(g, OP_NEXT);
        return bj_step(g);

    case OP_NEXT:
        while (g->active < g->nhands && g->hand[g->active].done) {
            g->active++;
        }
        if (g->active < g->nhands) {
            /* the second hand of a split still has one card */
            e = ev(BJ_EV_TURN);
            e.hand = (int8_t)g->active;
            if (cur(g)->n == 1) {
                push(g, OP(OP_DEAL_P, g->active));
                push(g, OP_CHECK);
            }
            return e;
        }
        g->active = (uint8_t)(g->nhands - 1);
        g->state  = BJ_ST_DEALER;
        push(g, OP_REVEAL);
        push(g, OP_DEALER);
        return bj_step(g);

    case OP_REVEAL:
        g->hole_up = true;
        e = ev(BJ_EV_REVEAL);
        e.hand = -1;
        e.idx  = 1;
        e.card = g->dealer.card[1];
        return e;

    case OP_DEALER:
        if (!all_bust(g) && bj_total(&g->dealer, NULL) < 17) {
            push(g, OP_DEAL_D_UP);
            push(g, OP_DEALER);
        } else {
            push(g, OP_SETTLE);
        }
        return bj_step(g);

    case OP_SETTLE:
        if (g->insurance && bj_is_blackjack(&g->dealer)) {
            push(g, OP_INS_RESULT);
        }
        for (int i = 0; i < g->nhands; i++) {
            push(g, OP(OP_RESULT, i));
        }
        push(g, OP_OVER);
        return bj_step(g);

    case OP_INS_RESULT:
        e = ev(BJ_EV_INSURANCE);
        if (bj_is_blackjack(&g->dealer)) {
            e.amount = g->insurance * 3;
            g->bank += e.amount;
        }
        g->insurance = 0;
        return e;

    case OP_RESULT: {
        int i = OP_ARG(v);
        settle_hand(g, i);
        e = ev(BJ_EV_RESULT);
        e.hand   = (int8_t)i;
        e.result = g->hand[i].result;
        e.amount = g->hand[i].paid;
        return e;
    }

    case OP_OVER:
        g->state = BJ_ST_OVER;
        g->rounds += g->nhands;         /* hands, so a split counts twice */
        for (int i = 0; i < g->nhands; i++) {
            switch (g->hand[i].result) {
            case BJ_RES_BLACKJACK: g->blackjacks++; g->wins++; break;
            case BJ_RES_WIN:       g->wins++;       break;
            case BJ_RES_PUSH:      g->pushes++;     break;
            default:               g->losses++;     break;
            }
        }
        if (g->bank > g->best) {
            g->best = g->bank;
        }
        return ev(BJ_EV_OVER);
    }
    return e;
}

/* --- basic strategy ------------------------------------------------------- *
 * Six decks, the dealer standing on soft 17, doubling after a split. Rows by
 * the player's total, columns by the dealer's card 2..10, A.
 *   H hit  S stand  D double (else hit)  T double (else stand)  P split
 * ------------------------------------------------------------------------- */

static const char *const HARD[] = {     /* 8 or less .. 17 */
    /*  8 */ "HHHHHHHHHH",
    /*  9 */ "HDDDDHHHHH",
    /* 10 */ "DDDDDDDDHH",
    /* 11 */ "DDDDDDDDDH",
    /* 12 */ "HHSSSHHHHH",
    /* 13 */ "SSSSSHHHHH",
    /* 14 */ "SSSSSHHHHH",
    /* 15 */ "SSSSSHHHHH",
    /* 16 */ "SSSSSHHHHH",
    /* 17 */ "SSSSSSSSSS",
};
static const char *const SOFT[] = {     /* A+2 (13) .. A+9 (20) */
    /* 13 */ "HHHDDHHHHH",
    /* 14 */ "HHHDDHHHHH",
    /* 15 */ "HHDDDHHHHH",
    /* 16 */ "HHDDDHHHHH",
    /* 17 */ "HDDDDHHHHH",
    /* 18 */ "TTTTTSSHHH",
    /* 19 */ "SSSSTSSSSS",
    /* 20 */ "SSSSSSSSSS",
};
static const char *const PAIR[] = {     /* 2,2 .. 10,10 then A,A */
    /*  2 */ "PPPPPPHHHH",
    /*  3 */ "PPPPPPHHHH",
    /*  4 */ "HHHPPHHHHH",
    /*  5 */ "DDDDDDDDHH",
    /*  6 */ "PPPPPHHHHH",
    /*  7 */ "PPPPPPHHHH",
    /*  8 */ "PPPPPPPPPP",
    /*  9 */ "PPPPPSPPSS",
    /* 10 */ "SSSSSSSSSS",
    /*  A */ "PPPPPPPPPP",
};

char bj_advice(const bj_game_t *g)
{
    if (g->state == BJ_ST_INSURANCE) {
        return 'n';                     /* never, without counting */
    }
    if (g->state != BJ_ST_PLAYER) {
        return 0;
    }
    const bj_hand_t *h   = &g->hand[g->active];
    int              up  = up_value(g);
    int              col = up == 1 ? 9 : up - 2;
    bool             soft;
    int              t = bj_total(h, &soft);
    char             a;

    if (h->n == 2 && bj_card_value(h->card[0]) == bj_card_value(h->card[1])) {
        int  v = bj_card_value(h->card[0]);
        char p = PAIR[v == 1 ? 9 : v - 2][col];
        if (p == 'P' && bj_can_split(g)) {
            return 'p';
        }
        if (p == 'D') {
            return bj_can_double(g) ? 'd' : 'h';
        }
        /* otherwise the pair plays as its total */
    }
    if (soft) {
        a = t <= 12 ? 'H' : (t >= 21 ? 'S' : SOFT[t - 13][col]);
    } else {
        a = HARD[t <= 8 ? 0 : (t >= 17 ? 9 : t - 8)][col];
    }
    switch (a) {
    case 'D': return bj_can_double(g) ? 'd' : 'h';
    case 'T': return bj_can_double(g) ? 'd' : 's';
    case 'S': return 's';
    default:  return 'h';
    }
}
