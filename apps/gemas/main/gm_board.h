/*
 * GEMAS - the board
 *
 * Pure logic: neither LVGL nor the HAL. The rules live here (what forms a
 * line, what special jewel it leaves, how everything falls afterwards) and
 * gemas.c is left with only how it looks. Deliberately separate: the rules are
 * the one thing that can be reasoned about without looking at the screen.
 */
#pragma once

#include "gm_art.h"     /* gm_special_t */
#include <stdint.h>
#include <stdbool.h>

#define GM_N            8       /* cells per side */
#define GM_MAX_GROUPS   12

typedef struct {
    int8_t  type;               /* 0..ncolors-1, or -1 if it is empty */
    uint8_t special;            /* gm_special_t */
} gm_cell_t;

typedef struct {
    gm_cell_t c[GM_N][GM_N];
    uint32_t  rng;
    int       ncolors;
} gm_board_t;

/* A group is a set of adjacent cells of the same colour forming at least one
 * line of three. An L or a T is a single group. */
typedef struct {
    uint8_t n;
    uint8_t cell[GM_N * 4];     /* r * GM_N + c */
    uint8_t run_h, run_v;       /* the longest run on each axis */
    uint8_t special;            /* which jewel it leaves on breaking */
    uint8_t pr, pc;             /* where that jewel appears */
    int8_t  type;
} gm_group_t;

/* How each column ended up after the fall. */
typedef struct {
    int8_t from[GM_N][GM_N];    /* row of origin, -1 = new jewel        */
    int8_t born[GM_N][GM_N];    /* if it is new: how many cells above   */
} gm_fall_t;

uint32_t gm_rnd(gm_board_t *b);
int      gm_rnd_range(gm_board_t *b, int lo, int hi);

void gm_board_init(gm_board_t *b, uint32_t seed, int ncolors);
void gm_board_fill(gm_board_t *b);          /* a new board, with no lines already made */
bool gm_board_shuffle(gm_board_t *b);       /* shuffles what is there; false if it had to rebuild it */

/* Looks for every completed line. Returns how many groups it found. If
 * 'swap_r/c' is not negative, the special jewel appears there (it is where the
 * finger left the jewel, which is what the player expects). */
int  gm_find_groups(const gm_board_t *b, gm_group_t *out, int max,
                    int swap_r, int swap_c);

/* Marks in 'mark' whatever is taken by the explosion of the specials already
 * marked, chaining. Returns the total number of cells marked. */
int  gm_expand_specials(const gm_board_t *b, uint8_t mark[GM_N][GM_N]);
int  gm_mark_color(const gm_board_t *b, uint8_t mark[GM_N][GM_N], int type);
int  gm_mark_count(const uint8_t mark[GM_N][GM_N]);

/* Removes what is marked and lets the rest fall, refilling from above. */
void gm_collapse(gm_board_t *b, const uint8_t mark[GM_N][GM_N], gm_fall_t *fall);

bool gm_swap_makes_match(gm_board_t *b, int r1, int c1, int r2, int c2);
bool gm_find_move(const gm_board_t *b, uint8_t out[4]);     /* one possible move */

static inline bool gm_has_move(const gm_board_t *b)
{
    uint8_t tmp[4];
    return gm_find_move(b, tmp);
}
