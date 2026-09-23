/*
 * MILA - what the player has, in the prefs (DESIGN.md section 4)
 *
 *   ml_s_<world id>  one digit of stars per level ("33210000")
 *   ml_coins, ml_own (items bought, space separated), ml_hat, ml_neck,
 *   ml_hatc, ml_neckc (their colours), ml_gift (day of the last gift),
 *   ml_last ("<world id> <level>"), ml_snd (bit 0 effects, bit 1 music),
 *   ml_st_* (stats)
 *
 * Stars are kept by world id, never by position: worlds can be added or
 * moved without breaking a save.
 */
#pragma once

#include "ml_level.h"

#include <stdbool.h>
#include <stdint.h>

#define ML_OWN_LEN 400

enum { SX_MOVES = 0, SX_PUSHES, SX_SOLVED, SX_UNDOS, SX_PETS, SX_TOYS, SX_PLAY_S, SX_N };

typedef struct {
    uint8_t  stars[ML_MAX_WORLDS][ML_MAX_LEVELS];   /* by the table's order */
    int32_t  coins;
    char     own[ML_OWN_LEN];
    char     hat[20], neck[20];
    int32_t  hat_col, neck_col;
    int32_t  gift_day;
    char     last[32];
    int32_t  snd;
    int32_t  stat[SX_N];
} ml_prog_t;

void ml_prog_load(ml_prog_t *p, const ml_worlds_t *w);
void ml_prog_save(const ml_prog_t *p, const ml_worlds_t *w);
int  ml_prog_world_stars(const ml_prog_t *p, const ml_worlds_t *w, int world);
int  ml_prog_total_stars(const ml_prog_t *p, const ml_worlds_t *w);
bool ml_prog_owns(const ml_prog_t *p, const char *item);
void ml_prog_add(ml_prog_t *p, const char *item);
