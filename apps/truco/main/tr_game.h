/*
 * TRUCO - rules and opponent, without LVGL
 *
 * This file does not know a screen exists. It compiles equally inside the app
 * and inside the test bench (tools/tr_harness.c), which plays thousands of
 * games in a row to verify that the score adds up and that nobody is left
 * waiting for a turn that never comes.
 *
 * The interface with the UI is EVENTS: tr_apply() mutates the state and leaves
 * in a queue what happened ("so-and-so said truco", "so-and-so played the 7 of
 * swords"). The app takes them out one at a time and animates them. That way
 * the rules do not have to know how long an animation lasts and the animation
 * does not have to know about rules.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TR_HAND_N   3       /* cards per hand */
#define TR_DECK_N   40      /* Spanish deck without 8 and 9 */
#define TR_TARGET   30      /* the game is to 30: 15 malas and 15 buenas */

#define TR_NOCARD   0xFF

/* Suits. The order matters: it is the one the deck uses and the one the cards
 * are drawn with. */
enum { TR_ESPADA = 0, TR_BASTO, TR_ORO, TR_COPA };

/* Players. 0 is always the human ("NOS"), 1 the opponent ("ELLOS"). */
enum { TR_YO = 0, TR_EL = 1 };

/* A card is 0..39: suit = c / 10, and the index 0..9 within the suit maps to
 * 1,2,3,4,5,6,7,10,11,12 (the Spanish deck has no 8 and no 9). */
typedef uint8_t tr_card_t;

int tr_suit(tr_card_t c);
int tr_rank(tr_card_t c);           /* 1..7, 10, 11, 12 */
int tr_power(tr_card_t c);          /* 0 (4) .. 13 (1 of swords) */
int tr_env_val(tr_card_t c);        /* envido value: face cards 0, the rest the number */
int tr_envido_points(const tr_card_t *cards, int n);

/* Calls and plays. tr_apply() receives one of these. */
typedef enum {
    TR_C_NONE = 0,
    TR_C_ENVIDO,
    TR_C_REAL,
    TR_C_FALTA,
    TR_C_TRUCO,
    TR_C_RETRUCO,
    TR_C_VALE4,
    TR_C_QUIERO,
    TR_C_NOQUIERO,
    TR_C_MAZO,
    TR_C_PLAY,          /* play a card; the argument is the index 0..2 */
} tr_call_t;

const char *tr_call_name(tr_call_t c);   /* "TRUCO", "NO QUIERO", ... */

typedef enum {
    TR_P_DEAL = 0,      /* freshly dealt, nobody has acted yet */
    TR_P_TURN,          /* it is 'turn's move: play a card or call */
    TR_P_ANSWER,        /* there is a call in the air; 'turn' has to answer */
    TR_P_HAND_END,      /* the hand is over; a new deal is needed */
    TR_P_GAME_END,      /* somebody reached 30 */
} tr_phase_t;

/* --- events --------------------------------------------------------------- */

enum {
    TR_EV_DEAL = 0,     /* new hand; who = who is the mano */
    TR_EV_SAY,          /* who made the call 'a' */
    TR_EV_PLAY,         /* who played card 'b' (index 'a') in trick 'n' */
    TR_EV_TRICK,        /* trick 'n' was won by 'who' (-1 = tied) */
    TR_EV_ENVIDO,       /* envido resolved: a/b are the points, who won 'n' */
    TR_EV_HAND,         /* the hand was won by 'who' and is worth 'n' */
    TR_EV_GAME,         /* the game was won by 'who' */
};

typedef struct {
    uint8_t kind;
    int8_t  who;
    int8_t  a, b;
    int16_t n;
} tr_ev_t;

#define TR_EV_MAX   16

/* --- state ---------------------------------------------------------------- */

typedef struct {
    tr_card_t hand[2][TR_HAND_N];
    bool      spent[2][TR_HAND_N];       /* card already played */
    tr_card_t table[2][TR_HAND_N];       /* what each one played in each trick */

    int8_t trick_win[TR_HAND_N];         /* -2 unplayed, -1 tied, 0/1 winner */
    int    trick;                        /* current trick, 0..2 */
    int    turn;                         /* whose turn it is */
    int    lead;                         /* who opened the current trick */
    int    mano;                         /* who is the mano in this hand */

    /* Envido. env_step: 0 nothing, 1 envido, 2 envido-envido, 3 real, 4
     * falta. */
    uint8_t env_step;
    bool    env_closed;                  /* already resolved or no longer callable */
    int     env_stake, env_prev;
    int     env_caller;

    /* Truco. truco_step: 0 nothing, 1 truco, 2 retruco, 3 vale cuatro. */
    uint8_t truco_step;
    bool    truco_acc;                   /* it was accepted */
    int     truco_caller;

    tr_call_t pending;                   /* call waiting for an answer */
    int       pend_from;
    tr_call_t held;                      /* truco interposed by "el envido esta primero" */
    int       held_from;
    int       resume_turn;               /* whose turn it was before the call */

    int  score[2];
    int  hand_winner;
    int  hand_points;
    int  env_points[2];                  /* each one's points, for showing them */

    tr_phase_t phase;

    /* event queue */
    tr_ev_t  ev[TR_EV_MAX];
    uint8_t  ev_head, ev_tail;

    uint32_t rng;
} tr_state_t;

void tr_new_game(tr_state_t *st, uint32_t seed);
void tr_new_hand(tr_state_t *st);

/* true if 'who' can do 'c' right now. 'arg' only matters for TR_C_PLAY. */
bool tr_can(const tr_state_t *st, int who, tr_call_t c, int arg);

/* Applies the action. Returns false if it was not legal (nothing changes). */
bool tr_apply(tr_state_t *st, int who, tr_call_t c, int arg);

/* What 'who' would do if the machine played them. TR_C_NONE if it is not their
 * turn. It takes the player so it can move both sides: that way the test bench
 * plays the machine against itself and the app has the TRUCO_AUTO mode. */
tr_call_t tr_ai_decide(tr_state_t *st, int who, int *arg);

/* Takes the next event; false if there is none. */
bool tr_pop_event(tr_state_t *st, tr_ev_t *out);

/* What the hand is worth right now (for the scoreboard's label). */
int tr_hand_value(const tr_state_t *st);
