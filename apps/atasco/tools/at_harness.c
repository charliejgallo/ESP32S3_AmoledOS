/*
 * ATASCO - test bench without a screen
 *
 * at_game.c depends on neither LVGL nor the HAL, so it compiles on its own
 * with 'cc' and a complete BFS can be run over each level: it confirms that it
 * is solvable and measures the real minimum move count, instead of trusting
 * what the generator thought. It is the same idea as
 * apps/arkanos/tools/ak_harness.c, applied to a game of rules rather than to
 * pixels.
 *
 *   cc -O2 -I ../main at_harness.c ../main/at_game.c ../main/at_levels.c -o /tmp/ath
 *   /tmp/ath
 *
 * It exits with a non-zero status if any level is unsolvable or malformed.
 */
#include "at_game.h"
#include "at_levels.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * A generic BFS over the board's state. A "move" is sliding a car to any free
 * position in a straight line: it is the same thing a drag on the screen does,
 * and it is the unit difficulty is measured in.
 *
 * Visited states are stored in an open hash table keyed by a 64-bit hash
 * (FNV-1a over the positions): with up to ~4*10^5 states the risk of a
 * collision is negligible, and it avoids comparing arrays on every insert.
 * -------------------------------------------------------------------------- */

#define MAX_STATES  400000
#define HASH_SIZE   (1u << 20)   /* > 2x MAX_STATES, keeps the table loose */

static uint64_t   s_hash[HASH_SIZE];      /* 0 = empty */
static at_board_t s_queue[MAX_STATES];
static int32_t    s_depth[MAX_STATES];
static int        s_queue_n;

static uint64_t hash_positions(const at_board_t *b)
{
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < b->count; i++) {
        const at_car_t *c = &b->cars[i];
        int8_t pos = c->horizontal ? c->col : c->row;
        h ^= (uint8_t)pos;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;   /* 0 is reserved for "empty" */
}

/* Inserts if it is new. Returns true if it was ALREADY there (no need to
 * enqueue it). */
static bool seen_mark(uint64_t h)
{
    size_t idx = (size_t)(h & (HASH_SIZE - 1));
    for (;;) {
        if (s_hash[idx] == 0) {
            s_hash[idx] = h;
            return false;
        }
        if (s_hash[idx] == h) {
            return true;
        }
        idx = (idx + 1) & (HASH_SIZE - 1);
    }
}

/* Returns the minimum move count, or -1 if it found no solution within the
 * state limit (for a 6x6 board with up to 16 cars that should not happen
 * unless the level is broken). */
static int solve(const at_board_t *start)
{
    memset(s_hash, 0, sizeof(s_hash));
    s_queue_n = 0;

    seen_mark(hash_positions(start));
    s_queue[s_queue_n] = *start;
    s_depth[s_queue_n] = 0;
    s_queue_n++;

    for (int head = 0; head < s_queue_n; head++) {
        at_board_t cur = s_queue[head];
        int        d   = s_depth[head];

        if (at_is_solved(&cur)) {
            return d;
        }

        for (int i = 0; i < cur.count; i++) {
            int lo, hi;
            at_slide_range(&cur, i, &lo, &hi);
            int cur_pos = cur.cars[i].horizontal ? cur.cars[i].col : cur.cars[i].row;

            for (int p = lo; p <= hi; p++) {
                if (p == cur_pos) {
                    continue;
                }
                at_board_t next = cur;
                at_apply_move(&next, i, p);
                if (seen_mark(hash_positions(&next))) {
                    continue;
                }
                if (s_queue_n >= MAX_STATES) {
                    return -1;
                }
                s_queue[s_queue_n] = next;
                s_depth[s_queue_n] = d + 1;
                s_queue_n++;
            }
        }
    }
    return -1;
}

int main(void)
{
    int n = at_level_count();
    printf("ATASCO: %d niveles\n", n);

    int  last_moves = 0;
    bool fail        = false;

    for (int i = 0; i < n; i++) {
        const at_level_t *lvl = at_level_get(i);
        at_board_t board;
        if (!at_parse_level(lvl->rows, &board)) {
            printf("nivel %2d: MAPA INVALIDO\n", i + 1);
            fail = true;
            continue;
        }

        int moves = solve(&board);
        if (moves < 0) {
            printf("nivel %2d: NO RESOLUBLE (o se paso del limite de estados)\n", i + 1);
            fail = true;
            continue;
        }

        const char *trend = (moves < last_moves) ? "  <-- baja respecto del anterior" : "";
        printf("nivel %2d: %2d autos, minimo %3d movimientos%s\n",
               i + 1, board.count, moves, trend);
        last_moves = moves;
    }

    if (fail) {
        printf("\nHay niveles rotos.\n");
        return 1;
    }
    printf("\nTodos los niveles son resolubles.\n");
    return 0;
}
