/*
 * ATASCO - the game's rules, without LVGL and without the HAL.
 *
 * A board of AT_GRID x AT_GRID with rectangular cars that only slide along
 * their own axis. The target car ('A' in the text map) is always horizontal,
 * of length 2, and leaves to the right: its row is the only one with a gap in
 * the wall.
 *
 * It compiles on its own (without ESP-IDF) from tools/at_harness.c to verify
 * with a BFS that every level is solvable, just as arkanos does with its dirty
 * rectangle test bench.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AT_GRID       6
#define AT_MAX_CARS   16

typedef struct {
    int8_t  row, col;     /* top-left cell it occupies */
    int8_t  len;          /* 2 or 3 */
    bool    horizontal;
    bool    target;       /* the red car that has to get out */
    uint8_t skin;         /* index of the visual model, see at_cars.h */
    char    letter;       /* the text map's, for debugging */
} at_car_t;

typedef struct {
    at_car_t cars[AT_MAX_CARS];
    int      count;
    int      target_idx;
} at_board_t;

/* 'rows' is AT_GRID strings of AT_GRID characters: '.' empty, 'A'..'Z' a car.
 * Every cell of a given letter has to form a continuous straight line (all in
 * the same row, or all in the same column): that decides the length and the
 * orientation. 'A' is always the target. Returns false if the map is malformed
 * or there is no target car. */
bool at_parse_level(const char *const rows[AT_GRID], at_board_t *out);

/* Range of positions (the coordinate that moves: column if horizontal, row if
 * vertical) that car 'idx' can reach in a straight line right now, given where
 * the others are. The target can reach exactly AT_GRID (only then does it
 * count as fully out); the rest stop at the wall. */
void at_slide_range(const at_board_t *b, int idx, int *out_min, int *out_max);

/* Moves the car to 'pos' if it is within its valid range (recomputed inside).
 * Returns true if it changed place. */
bool at_apply_move(at_board_t *b, int idx, int pos);

/* true once the target has left completely (col >= AT_GRID). It is the strict
 * condition, the one the test bench's BFS counts. */
bool at_is_solved(const at_board_t *b);

/* true once the target has reached the exit's wall, that is, its cells end at
 * the right-hand edge (col + len >= AT_GRID).
 *
 * It exists separately from at_is_solved() for a physical reason, not a rules
 * one: to get that far the car must already have had a clear way -if something
 * were blocking it, at_slide_range() would have stopped it earlier-, so as far
 * as the game is concerned that IS having got out. But for col to reach
 * AT_GRID the finger would have to drag all six cell widths, and on a 368 px
 * screen with a 300 px board there is not that much travel: the player got to
 * the wall, the car was left half out and the level was never completed. The
 * interface uses this condition and then sends the car out by itself. */
bool at_target_at_exit(const at_board_t *b);

/* The car occupying cell (row, col), or -1 if it is empty. */
int at_car_at(const at_board_t *b, int row, int col);

#ifdef __cplusplus
}
#endif
