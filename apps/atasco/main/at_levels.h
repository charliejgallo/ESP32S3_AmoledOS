/*
 * ATASCO - level table.
 *
 * Each level is a text map of AT_GRID rows by AT_GRID columns, just like
 * arkanos with its bricks: adding a level means adding a map to at_levels[] in
 * at_levels.c, without touching any other file.
 *
 *   .      empty cell
 *   A      the target car, always horizontal, length 2, leaves to the right
 *   B..Z   another car: all its cells in the same row (horizontal) or the same
 *          column (vertical), with no gaps
 *
 * The factory levels were generated and verified with a separate BFS
 * (tools/at_harness.c does the same verification on every build): each one has
 * a measured minimum move count, not a guessed one, and they are ordered from
 * lowest to highest.
 */
#pragma once

#include "at_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *rows[AT_GRID];
} at_level_t;

int               at_level_count(void);
const at_level_t *at_level_get(int index);

#ifdef __cplusplus
}
#endif
