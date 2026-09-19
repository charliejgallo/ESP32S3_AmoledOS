/*
 * TRUCO between two watches - what goes over the link
 *
 * Everything travels on the reliable channel (aos_hal_link_send_reliable):
 * a card game is a handful of moves a minute and every one of them matters,
 * so there is nothing for the fast channel to do here.
 *
 * Host-ordered lockstep: the two watches run the same deterministic engine
 * (tr_game.c) from the same seed. The host (the lower MAC) picks the seed,
 * takes every move, applies it and echoes it; the guest applies only what
 * the host echoes, in that order, its own taps included.
 */
#pragma once

#include <stdint.h>

#define TL_PROTO   1
#define TL_INBOX   16               /* moves waiting for the table to be quiet */

enum { TL_HELLO = 1, TL_ACT = 2 };

/* Twice a second until the other side answers; again when a new game has
 * to start. 'nonce' identifies this run of the app: a hello with another
 * nonce from the same partner means they re-entered Truco and the game
 * starts over on both watches. */
typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  mac[6];
    uint32_t seed;                  /* the host's is the one used */
    uint32_t nonce;
} tl_hello_t;

/* A move. Guest -> host: a proposal for the guest's own seat. Host -> guest:
 * the move as applied, whoever made it. */
typedef struct __attribute__((packed)) {
    uint8_t type, proto;
    uint8_t who;                    /* seat in the engine: 0 host, 1 guest */
    uint8_t call;                   /* tr_call_t */
    int8_t  arg;                    /* card index for TR_C_PLAY */
} tl_act_t;
