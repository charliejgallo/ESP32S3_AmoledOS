/*
 * TRUCO - rules and opponent. Not one line of LVGL: see tr_game.h.
 *
 * It is played WITHOUT FLOR, which is the most common two-handed form, and to
 * 30 points (15 malas and 15 buenas). The whole envido is there (envido,
 * envido-envido, real envido and falta envido), the whole truco (truco,
 * retruco and vale cuatro), "el envido esta primero" and going to the deck.
 */
#include "tr_game.h"

#include <string.h>

/* The Spanish deck has no 8 and no 9. */
static const uint8_t RANKS[10] = { 1, 2, 3, 4, 5, 6, 7, 10, 11, 12 };

int tr_suit(tr_card_t c) { return c / 10; }
int tr_rank(tr_card_t c) { return RANKS[c % 10]; }

/* Truco's ranking. The top four are "las bravas" and each one is unique; from
 * there down the numbers rule and the suits count for nothing, so two cards of
 * the same power tie. */
int tr_power(tr_card_t c)
{
    int s = tr_suit(c);
    int r = tr_rank(c);

    if (r == 1 && s == TR_ESPADA) return 13;
    if (r == 1 && s == TR_BASTO)  return 12;
    if (r == 7 && s == TR_ESPADA) return 11;
    if (r == 7 && s == TR_ORO)    return 10;

    switch (r) {
    case 3:  return 9;
    case 2:  return 8;
    case 1:  return 7;      /* the false 1: coins and cups */
    case 12: return 6;
    case 11: return 5;
    case 10: return 4;
    case 7:  return 3;      /* the false 7: cups and clubs */
    case 6:  return 2;
    case 5:  return 1;
    default: return 0;      /* the 4, the lowest */
    }
}

/* For the envido the face cards are worth zero. */
int tr_env_val(tr_card_t c)
{
    int r = tr_rank(c);
    return r >= 10 ? 0 : r;
}

int tr_envido_points(const tr_card_t *cards, int n)
{
    int best = 0;

    for (int s = 0; s < 4; s++) {
        int hi = -1, hi2 = -1;
        for (int i = 0; i < n; i++) {
            if (tr_suit(cards[i]) != s) continue;
            int v = tr_env_val(cards[i]);
            if (v > hi) { hi2 = hi; hi = v; }
            else if (v > hi2) { hi2 = v; }
        }
        if (hi2 >= 0) {
            int p = 20 + hi + hi2;
            if (p > best) best = p;
        } else if (hi > best) {
            best = hi;      /* with no two of the same suit, the highest alone */
        }
    }
    return best;
}

const char *tr_call_name(tr_call_t c)
{
    switch (c) {
    case TR_C_ENVIDO:   return "ENVIDO";
    case TR_C_REAL:     return "REAL ENVIDO";
    case TR_C_FALTA:    return "FALTA ENVIDO";
    case TR_C_TRUCO:    return "TRUCO";
    case TR_C_RETRUCO:  return "RE TRUCO";
    case TR_C_VALE4:    return "VALE CUATRO";
    case TR_C_QUIERO:   return "QUIERO";
    case TR_C_NOQUIERO: return "NO QUIERO";
    case TR_C_MAZO:     return "ME VOY AL MAZO";
    default:            return "";
    }
}

/* -------------------------------------------------------------------------- */

static uint32_t rnd(tr_state_t *st)
{
    uint32_t x = st->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    st->rng = x;
    return x;
}

static void push(tr_state_t *st, int kind, int who, int a, int b, int n)
{
    uint8_t next = (uint8_t)((st->ev_tail + 1) % TR_EV_MAX);
    if (next == st->ev_head) return;        /* queue full: it is lost, not hung */
    st->ev[st->ev_tail].kind = (uint8_t)kind;
    st->ev[st->ev_tail].who  = (int8_t)who;
    st->ev[st->ev_tail].a    = (int8_t)a;
    st->ev[st->ev_tail].b    = (int8_t)b;
    st->ev[st->ev_tail].n    = (int16_t)n;
    st->ev_tail = next;
}

bool tr_pop_event(tr_state_t *st, tr_ev_t *out)
{
    if (st->ev_head == st->ev_tail) return false;
    *out = st->ev[st->ev_head];
    st->ev_head = (uint8_t)((st->ev_head + 1) % TR_EV_MAX);
    return true;
}

static int other(int who) { return who ^ 1; }

/* What the hand is worth if it is played to the end. */
int tr_hand_value(const tr_state_t *st)
{
    static const int V[4] = { 1, 2, 3, 4 };
    return st->truco_acc ? V[st->truco_step] : 1;
}

/* What the caller collects if they are told no. */
static int truco_prev(const tr_state_t *st)
{
    static const int V[4] = { 1, 1, 2, 3 };
    return V[st->truco_step];
}

/* The falta: what the one in the lead needs to reach 30. */
static int falta_points(const tr_state_t *st)
{
    int hi = st->score[0] > st->score[1] ? st->score[0] : st->score[1];
    int f  = TR_TARGET - hi;
    return f < 1 ? 1 : f;
}

static void add_score(tr_state_t *st, int who, int pts)
{
    st->score[who] += pts;
    if (st->score[who] > TR_TARGET) st->score[who] = TR_TARGET;
}

/* -------------------------------------------------------------------------- */

void tr_new_game(tr_state_t *st, uint32_t seed)
{
    memset(st, 0, sizeof(*st));
    st->rng  = seed ? seed : 0x1D3A57F1u;
    st->mano = TR_EL;           /* tr_new_hand() alternates it: the human starts */
    st->phase = TR_P_HAND_END;
    tr_new_hand(st);
}

void tr_new_hand(tr_state_t *st)
{
    tr_card_t deck[TR_DECK_N];
    for (int i = 0; i < TR_DECK_N; i++) deck[i] = (tr_card_t)i;
    for (int i = TR_DECK_N - 1; i > 0; i--) {
        int j = (int)(rnd(st) % (uint32_t)(i + 1));
        tr_card_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }

    st->mano = other(st->mano);

    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < TR_HAND_N; i++) {
            /* Dealt one at a time and alternating, starting with the pie, as
             * at the table: the mano gets the last one. */
            st->hand[p][i]  = deck[i * 2 + p];
            st->spent[p][i] = false;
            st->table[p][i] = TR_NOCARD;
        }
        st->env_points[p] = tr_envido_points(st->hand[p], TR_HAND_N);
    }

    for (int i = 0; i < TR_HAND_N; i++) st->trick_win[i] = -2;

    st->trick = 0;
    st->lead  = st->mano;
    st->turn  = st->mano;

    st->env_step   = 0;
    st->env_closed = false;
    st->env_stake  = 0;
    st->env_prev   = 0;
    st->env_caller = -1;

    st->truco_step   = 0;
    st->truco_acc    = false;
    st->truco_caller = -1;

    st->pending     = TR_C_NONE;
    st->held        = TR_C_NONE;
    st->pend_from   = -1;
    st->held_from   = -1;
    st->resume_turn = st->mano;

    st->hand_winner = -1;
    st->hand_points = 0;
    st->phase = TR_P_TURN;

    push(st, TR_EV_DEAL, st->mano, 0, 0, 0);
}

/* -------------------------------------------------------------------------- */

static bool is_env_call(tr_call_t c)
{
    return c == TR_C_ENVIDO || c == TR_C_REAL || c == TR_C_FALTA;
}

static bool is_truco_call(tr_call_t c)
{
    return c == TR_C_TRUCO || c == TR_C_RETRUCO || c == TR_C_VALE4;
}

/* The envido is called in the first trick and dies when that trick is
 * completed or when the truco has been accepted. */
static bool env_open(const tr_state_t *st)
{
    return !st->env_closed && st->trick == 0 && !st->truco_acc;
}

static bool env_can_raise(const tr_state_t *st, tr_call_t c)
{
    switch (c) {
    case TR_C_ENVIDO: return st->env_step <= 1;     /* envido and envido-envido */
    case TR_C_REAL:   return st->env_step <= 2;
    case TR_C_FALTA:  return st->env_step <= 3;
    default:          return false;
    }
}

/* The truco is called by either player if nothing has been called; after that,
 * only the one who said quiero can raise. */
static bool truco_can_call(const tr_state_t *st, int who, tr_call_t c)
{
    int step = (c == TR_C_TRUCO) ? 1 : (c == TR_C_RETRUCO) ? 2 : 3;
    if (step != st->truco_step + 1) return false;
    if (st->truco_step == 0) return true;
    return st->truco_acc && who != st->truco_caller;
}

bool tr_can(const tr_state_t *st, int who, tr_call_t c, int arg)
{
    if (st->phase == TR_P_ANSWER) {
        if (who != st->turn) return false;
        if (c == TR_C_QUIERO || c == TR_C_NOQUIERO) return true;

        if (is_env_call(st->pending)) {
            return is_env_call(c) && env_can_raise(st, c);
        }
        /* Against a truco: raise it, or -if it is still possible- call the
         * envido, which comes first. It can only be interposed once. */
        if (is_truco_call(c)) {
            int step = (c == TR_C_TRUCO) ? 1 : (c == TR_C_RETRUCO) ? 2 : 3;
            return step == st->truco_step + 1 && who != st->truco_caller;
        }
        if (is_env_call(c)) {
            return st->held == TR_C_NONE && env_open(st) && st->env_step == 0;
        }
        return false;
    }

    if (st->phase != TR_P_TURN || who != st->turn) return false;

    switch (c) {
    case TR_C_PLAY:
        return arg >= 0 && arg < TR_HAND_N && !st->spent[who][arg];
    case TR_C_MAZO:
        return true;
    case TR_C_ENVIDO:
    case TR_C_REAL:
    case TR_C_FALTA:
        return env_open(st) && st->env_step == 0;
    case TR_C_TRUCO:
    case TR_C_RETRUCO:
    case TR_C_VALE4:
        return truco_can_call(st, who, c);
    default:
        return false;
    }
}

/* -------------------------------------------------------------------------- */

static void end_hand(tr_state_t *st, int winner, int pts)
{
    st->hand_winner = winner;
    st->hand_points = pts;
    add_score(st, winner, pts);
    push(st, TR_EV_HAND, winner, 0, 0, pts);

    if (st->score[winner] >= TR_TARGET) {
        st->phase = TR_P_GAME_END;
        push(st, TR_EV_GAME, winner, 0, 0, st->score[winner]);
    } else {
        st->phase = TR_P_HAND_END;
    }
}

/* Who won the hand according to the tricks, or -2 if it is not known yet. */
static int hand_result(const tr_state_t *st)
{
    int a = st->trick_win[0], b = st->trick_win[1], c = st->trick_win[2];

    if (a == -2 || b == -2) return -2;

    if (a >= 0 && (b == a || b == -1)) return a;    /* won the first and did not lose it */
    if (a == -1 && b >= 0) return b;                /* first tied: the second rules */

    if (c == -2) return -2;
    if (c >= 0) return c;                           /* 1-1, or all tied so far */
    if (a >= 0) return a;                           /* third tied: whoever won the first */
    if (b >= 0) return b;
    return st->mano;                                /* three tied: the mano wins */
}

static void resolve_envido(tr_state_t *st, bool accepted)
{
    st->env_closed = true;

    if (!accepted) {
        int w = st->env_caller;
        add_score(st, w, st->env_prev);
        push(st, TR_EV_ENVIDO, w, -1, -1, st->env_prev);
    } else {
        int p0 = st->env_points[0], p1 = st->env_points[1];
        int w;
        if (p0 > p1)      w = 0;
        else if (p1 > p0) w = 1;
        else              w = st->mano;     /* draw: the mano wins */
        add_score(st, w, st->env_stake);
        push(st, TR_EV_ENVIDO, w, (int8_t)p0, (int8_t)p1, st->env_stake);
    }

    if (st->score[0] >= TR_TARGET || st->score[1] >= TR_TARGET) {
        int w = st->score[0] >= TR_TARGET ? 0 : 1;
        st->phase = TR_P_GAME_END;
        push(st, TR_EV_GAME, w, 0, 0, st->score[w]);
        return;
    }

    /* If the envido interposed on a truco ("el envido esta primero"), the
     * truco comes back. */
    if (st->held != TR_C_NONE) {
        st->pending   = st->held;
        st->pend_from = st->held_from;
        st->turn      = other(st->held_from);
        st->held      = TR_C_NONE;
        st->held_from = -1;
        st->phase     = TR_P_ANSWER;
    } else {
        st->pending = TR_C_NONE;
        /* The turn returns to whoever was about to play when the call started,
         * which is not necessarily the last one to call: if A calls envido and
         * B raises it, on resolving it is still A's turn. */
        st->turn  = st->resume_turn;
        st->phase = TR_P_TURN;
    }
}

static void play_card(tr_state_t *st, int who, int idx)
{
    tr_card_t c = st->hand[who][idx];
    st->spent[who][idx]   = true;
    st->table[who][st->trick] = c;
    push(st, TR_EV_PLAY, who, idx, (int8_t)c, st->trick);

    int op = other(who);
    if (st->table[op][st->trick] == TR_NOCARD) {
        st->turn = op;                      /* the answer is missing */
        return;
    }

    /* Trick complete. */
    int pa = tr_power(st->table[0][st->trick]);
    int pb = tr_power(st->table[1][st->trick]);
    int w  = pa > pb ? 0 : (pb > pa ? 1 : -1);
    st->trick_win[st->trick] = (int8_t)w;
    push(st, TR_EV_TRICK, w, 0, 0, st->trick);

    st->env_closed = true;                  /* past the first, there is no envido */

    int res = hand_result(st);
    if (res >= 0) {
        end_hand(st, res, tr_hand_value(st));
        return;
    }

    st->trick++;
    /* On a tie the same player opens again; otherwise, whoever won. */
    st->lead = (w >= 0) ? w : st->lead;
    st->turn = st->lead;
}

bool tr_apply(tr_state_t *st, int who, tr_call_t c, int arg)
{
    if (!tr_can(st, who, c, arg)) return false;

    if (c == TR_C_PLAY) {
        play_card(st, who, arg);
        return true;
    }

    push(st, TR_EV_SAY, who, (int8_t)c, 0, 0);

    switch (c) {
    case TR_C_MAZO: {
        int pts = st->truco_acc ? tr_hand_value(st) : 1;
        end_hand(st, other(who), pts);
        return true;
    }

    case TR_C_ENVIDO:
    case TR_C_REAL:
    case TR_C_FALTA: {
        bool tapping = (st->phase == TR_P_ANSWER) && is_truco_call(st->pending);
        if (tapping) {
            st->held      = st->pending;
            st->held_from = st->pend_from;
        }
        if (st->phase == TR_P_TURN) st->resume_turn = who;
        st->env_caller = who;               /* the last one to call is the one who collects */

        st->env_prev = st->env_stake > 0 ? st->env_stake : 1;
        if (c == TR_C_ENVIDO) {
            st->env_stake = st->env_step == 0 ? 2 : 4;
            st->env_step  = (uint8_t)(st->env_step + 1);
        } else if (c == TR_C_REAL) {
            st->env_stake = (st->env_stake > 0 ? st->env_stake : 0) + 3;
            st->env_step  = 3;
        } else {
            st->env_stake = falta_points(st);
            st->env_step  = 4;
        }
        st->pending   = c;
        st->pend_from = who;
        st->turn      = other(who);
        st->phase     = TR_P_ANSWER;
        return true;
    }

    case TR_C_TRUCO:
    case TR_C_RETRUCO:
    case TR_C_VALE4:
        if (st->phase == TR_P_TURN) st->resume_turn = who;
        st->truco_step   = (uint8_t)(c == TR_C_TRUCO ? 1 : c == TR_C_RETRUCO ? 2 : 3);
        st->truco_caller = who;
        st->truco_acc    = false;
        st->pending      = c;
        st->pend_from    = who;
        st->turn         = other(who);
        st->phase        = TR_P_ANSWER;
        return true;

    case TR_C_QUIERO:
        if (is_env_call(st->pending)) {
            resolve_envido(st, true);
        } else {
            st->truco_acc = true;
            st->pending   = TR_C_NONE;
            st->turn      = st->resume_turn; /* whoever called goes on playing */
            st->phase     = TR_P_TURN;
        }
        return true;

    case TR_C_NOQUIERO:
        if (is_env_call(st->pending)) {
            resolve_envido(st, false);
        } else {
            end_hand(st, st->truco_caller, truco_prev(st));
        }
        return true;

    default:
        return false;
    }
}

/* --------------------------------------------------------------------------
 * The opponent
 *
 * There is no search and no tables: it is heuristics over two numbers -the
 * strength of what is left in its hand and its envido points- plus a pinch of
 * randomness so it bluffs now and then. That is enough for it to play like
 * somebody who knows the rules and not for it to be unbeatable, which is what
 * you want on the phone. The thresholds were tuned with tools/tr_harness.c.
 * -------------------------------------------------------------------------- */

/* 0..100. The highest card weighs twice as much as the other two together,
 * because in truco what decides is the best card, not the average. */
static int ai_strength(const tr_state_t *st, int who)
{
    int best = -1, sum = 0, n = 0;

    for (int i = 0; i < TR_HAND_N; i++) {
        if (st->spent[who][i]) continue;
        int p = tr_power(st->hand[who][i]);
        sum += p;
        if (p > best) best = p;
        n++;
    }
    if (n == 0) return 0;

    int rest = sum - best;
    int s = best * 5 + rest * 2;            /* ceiling ~ 5*13 + 2*24 = 113 */

    /* The first trick weighs far more than any card. */
    if (st->trick_win[0] == who)            s += 18;
    else if (st->trick_win[0] == other(who)) s -= 18;
    if (st->mano == who) s += 4;

    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return s;
}

/* Which card to play. Returns the index in the hand. */
static int ai_pick_card(tr_state_t *st, int who)
{
    int idx[TR_HAND_N], n = 0;
    for (int i = 0; i < TR_HAND_N; i++) {
        if (!st->spent[who][i]) idx[n++] = i;
    }
    if (n == 1) return idx[0];

    /* lowest to highest */
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (tr_power(st->hand[who][idx[j]]) < tr_power(st->hand[who][idx[i]])) {
                int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }
        }
    }

    tr_card_t rival = st->table[other(who)][st->trick];

    if (rival == TR_NOCARD) {
        /* It opens. With two bravas it is better to open with the lower of the
         * two and keep the best; otherwise the highest goes out to see whether
         * it takes the first trick. */
        if (n == 3 && tr_power(st->hand[who][idx[1]]) >= 9) return idx[1];
        return idx[n - 1];
    }

    /* Answering: the lowest that beats it. Tying also works, especially on the
     * first trick, so it is accepted if there is nothing that wins. */
    int rp = tr_power(rival);
    for (int i = 0; i < n; i++) {
        if (tr_power(st->hand[who][idx[i]]) > rp) return idx[i];
    }
    for (int i = 0; i < n; i++) {
        if (tr_power(st->hand[who][idx[i]]) == rp) return idx[i];
    }
    return idx[0];      /* it beats nothing: it gives up the lowest */
}

/* Points threshold for accepting an envido according to what is at stake. */
static int env_need(int stake)
{
    if (stake <= 2) return 22;
    if (stake <= 4) return 25;
    if (stake <= 5) return 26;
    if (stake <= 7) return 28;
    return 29;                  /* the falta */
}

static tr_call_t ai_answer_envido(tr_state_t *st, int who)
{
    int e    = st->env_points[who];
    int need = env_need(st->env_stake);
    int r    = (int)(rnd(st) % 100);

    /* With very good points it is better to raise than to stand pat. */
    if (e >= 31 && tr_can(st, who, TR_C_FALTA, 0) && r < 55) return TR_C_FALTA;
    if (e >= 29 && tr_can(st, who, TR_C_REAL, 0)  && r < 70) return TR_C_REAL;
    if (e >= 27 && tr_can(st, who, TR_C_ENVIDO, 0) && r < 45) return TR_C_ENVIDO;

    /* If it is losing badly, it plays the falta with less. */
    if (st->score[other(who)] >= 25 && e >= 26 && tr_can(st, who, TR_C_FALTA, 0)) {
        return TR_C_FALTA;
    }

    if (e >= need) return TR_C_QUIERO;
    if (e >= need - 3 && r < 30) return TR_C_QUIERO;    /* it stretches a little */
    return TR_C_NOQUIERO;
}

static tr_call_t ai_answer_truco(tr_state_t *st, int who)
{
    int s = ai_strength(st, who);
    int r = (int)(rnd(st) % 100);

    /* Raising with the hand won. */
    if (s >= 78 && r < 55) {
        if (tr_can(st, who, TR_C_RETRUCO, 0)) return TR_C_RETRUCO;
        if (tr_can(st, who, TR_C_VALE4, 0))   return TR_C_VALE4;
    }

    int need = 42;
    if (st->truco_step >= 2) need = 52;     /* the retruco and the vale cuatro are thought about */
    if (st->score[who] >= 27) need -= 6;  /* near the end it is worth taking risks */

    if (s >= need) return TR_C_QUIERO;
    if (s >= need - 8 && r < 25) return TR_C_QUIERO;
    return TR_C_NOQUIERO;
}

tr_call_t tr_ai_decide(tr_state_t *st, int who, int *arg)
{
    *arg = 0;
    if (st->turn != who) return TR_C_NONE;

    if (st->phase == TR_P_ANSWER) {
        if (is_env_call(st->pending)) {
            /* El envido esta primero: if it is called truco and it is still
             * possible, it sometimes interposes the envido. */
            return ai_answer_envido(st, who);
        }
        if (st->held == TR_C_NONE && env_open(st) && st->env_step == 0 &&
            st->env_points[who] >= 26 && (int)(rnd(st) % 100) < 75) {
            return TR_C_ENVIDO;
        }
        return ai_answer_truco(st, who);
    }

    if (st->phase != TR_P_TURN) return TR_C_NONE;

    /* Envido, on the first trick. */
    if (tr_can(st, who, TR_C_ENVIDO, 0)) {
        int e = st->env_points[who];
        int r = (int)(rnd(st) % 100);
        if (e >= 28 || (e >= 25 && r < 65) || (e >= 21 && r < 25) || r < 7) {
            if (e >= 31 && r < 30) return TR_C_REAL;
            return TR_C_ENVIDO;
        }
    }

    /* Truco. */
    if (tr_can(st, who, TR_C_TRUCO, 0) || tr_can(st, who, TR_C_RETRUCO, 0) ||
        tr_can(st, who, TR_C_VALE4, 0)) {
        int s = ai_strength(st, who);
        int r = (int)(rnd(st) % 100);
        bool ok = (s >= 62) || (s >= 50 && r < 40) || (r < 8);   /* the 8% is a bluff */
        if (ok) {
            if (tr_can(st, who, TR_C_TRUCO, 0))   return TR_C_TRUCO;
            if (tr_can(st, who, TR_C_RETRUCO, 0)) return TR_C_RETRUCO;
            return TR_C_VALE4;
        }
    }

    /* Going to the deck only with an impossible hand and something expensive
     * on the table. */
    if (st->truco_acc && tr_hand_value(st) >= 3 && ai_strength(st, who) < 18) {
        return TR_C_MAZO;
    }

    *arg = ai_pick_card(st, who);
    return TR_C_PLAY;
}
