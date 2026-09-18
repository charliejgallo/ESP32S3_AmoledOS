/*
 * BLACKJACK - the rules
 *
 * No LVGL, no HAL: tools/bj_harness.c plays hundreds of thousands of hands
 * with it and checks the money after each one.
 *
 * The table's rules, the ones printed on the felt:
 *   - a shoe of six decks, reshuffled when the cut card comes out (75 %);
 *   - the dealer stands on every 17, soft ones included;
 *   - blackjack pays 3 to 2, insurance 2 to 1, and the dealer peeks with an
 *     ace or a ten showing, so a dealer blackjack takes only the first bet;
 *   - double on any first two cards, also after splitting;
 *   - one split per round; split aces get one card each, and 21 there is
 *     not a blackjack.
 *
 * How the app drives it: an action (deal, hit, stand...) only queues work;
 * bj_step() does ONE thing - one card, the reveal, one result - and says
 * what it did, so every card can be animated before the next one comes.
 * When there is nothing queued it returns BJ_EV_NONE and the game is waiting
 * for the player (to bet, to decide insurance, or to play the hand).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BJ_DECKS        6
#define BJ_SHOE_N       (52 * BJ_DECKS)
#define BJ_HAND_MAX     12
#define BJ_HANDS_MAX    2
#define BJ_BANK0        1000
#define BJ_BET_MIN      10
#define BJ_BET_MAX      1000

typedef enum {
    BJ_ST_BET,          /* choosing the bet                                */
    BJ_ST_DEAL,         /* the four first cards are on their way           */
    BJ_ST_INSURANCE,    /* the dealer shows an ace: insure or not          */
    BJ_ST_PLAYER,       /* the player plays hand 'active'                  */
    BJ_ST_DEALER,       /* the dealer plays                                */
    BJ_ST_OVER,         /* results on the table                            */
} bj_state_t;

typedef enum {
    BJ_RES_NONE,
    BJ_RES_BLACKJACK,   /* 3 to 2                                          */
    BJ_RES_WIN,
    BJ_RES_PUSH,
    BJ_RES_LOSE,
    BJ_RES_BUST,
} bj_result_t;

typedef enum {
    BJ_EV_NONE,         /* waiting for the player                          */
    BJ_EV_SHUFFLE,      /* a fresh shoe                                    */
    BJ_EV_CARD,         /* 'card' to 'hand' (-1 dealer) at 'idx', 'up'     */
    BJ_EV_REVEAL,       /* the hole card, 'card', turns over               */
    BJ_EV_SPLIT,        /* hand 0's second card becomes hand 1             */
    BJ_EV_TURN,         /* 'hand' is now the one being played              */
    BJ_EV_INSURANCE,    /* 'amount' back from insurance (0: lost)          */
    BJ_EV_RESULT,       /* 'hand' settled as 'result', 'amount' paid back  */
    BJ_EV_OVER,         /* the round is done                               */
} bj_ev_type_t;

typedef struct {
    uint8_t type;
    int8_t  hand;
    uint8_t idx;
    uint8_t card;
    uint8_t result;
    bool    up;
    int32_t amount;
} bj_ev_t;

typedef struct {
    uint8_t card[BJ_HAND_MAX];
    uint8_t n;
    int32_t bet;
    bool    doubled;
    bool    done;
    bool    split;          /* came from a split: 21 is not blackjack      */
    bool    aces;           /* split aces: one card and no more            */
    uint8_t result;
    int32_t paid;
} bj_hand_t;

typedef struct {
    uint8_t    shoe[BJ_SHOE_N];
    int16_t    pos, cut;
    uint32_t   rng;

    bj_state_t state;
    bj_hand_t  dealer;
    bj_hand_t  hand[BJ_HANDS_MAX];
    uint8_t    nhands;
    uint8_t    active;
    bool       hole_up;

    int32_t    bank;
    int32_t    bet;             /* what the next round stakes              */
    int32_t    insurance;

    uint8_t    q[24];           /* the queued work, see bj_game.c           */
    uint8_t    qh, qn;

    /* since the bank was last refilled; all counted in hands, a split
     * round being two */
    uint32_t   rounds, wins, pushes, losses, blackjacks;
    int32_t    best;            /* the highest the bank has been           */
    uint32_t   refills;
} bj_game_t;

void bj_game_init(bj_game_t *g, uint32_t seed, int32_t bank);

/* Values and totals. total() is the best one not over 21 if there is; *soft
 * says an ace is counting 11. */
int  bj_card_value(int card);           /* 1..10, the ace is 1          */
int  bj_total(const bj_hand_t *h, bool *soft);
bool bj_is_blackjack(const bj_hand_t *h);

/* Betting, in BJ_ST_BET. */
void bj_bet_add(bj_game_t *g, int32_t chips);   /* clamped to bank and max */
void bj_bet_clear(bj_game_t *g);
bool bj_can_deal(const bj_game_t *g);
bool bj_deal(bj_game_t *g);

/* In BJ_ST_INSURANCE. */
void bj_insure(bj_game_t *g, bool yes);

/* In BJ_ST_PLAYER, on hand 'active'. */
bool bj_can_double(const bj_game_t *g);
bool bj_can_split(const bj_game_t *g);
void bj_hit(bj_game_t *g);
void bj_stand(bj_game_t *g);
bool bj_double(bj_game_t *g);
bool bj_split(bj_game_t *g);

/* In BJ_ST_OVER: back to betting, keeping the bet if it still fits. */
void bj_next_round(bj_game_t *g);

/* When the bank cannot cover the smallest bet. */
bool bj_broke(const bj_game_t *g);
void bj_refill(bj_game_t *g);

/* One step of queued work. */
bj_ev_t bj_step(bj_game_t *g);
bool    bj_busy(const bj_game_t *g);

/* What basic strategy would do now: 'h'it, 's'tand, 'd'ouble, 'p'split;
 * for insurance, 'n'o. The harness plays with it and the app offers it as
 * a hint. */
char bj_advice(const bj_game_t *g);
