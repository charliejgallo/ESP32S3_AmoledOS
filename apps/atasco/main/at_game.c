#include "at_game.h"

#include <string.h>

/* Marks in 'occ' (of size AT_GRID) the cells occupied by other cars along
 * 'self's axis: the column of every vertical car crossing its row, or the row
 * of every horizontal car crossing its column. */
static void axis_occupancy(const at_board_t *b, int idx, bool occ[AT_GRID])
{
    memset(occ, 0, AT_GRID * sizeof(bool));
    const at_car_t *self = &b->cars[idx];

    for (int i = 0; i < b->count; i++) {
        if (i == idx) {
            continue;
        }
        const at_car_t *o = &b->cars[i];

        if (self->horizontal) {
            if (o->horizontal) {
                if (o->row == self->row) {
                    for (int k = 0; k < o->len; k++) {
                        occ[o->col + k] = true;
                    }
                }
            } else if (self->row >= o->row && self->row < o->row + o->len) {
                occ[o->col] = true;
            }
        } else {
            if (!o->horizontal) {
                if (o->col == self->col) {
                    for (int k = 0; k < o->len; k++) {
                        occ[o->row + k] = true;
                    }
                }
            } else if (self->col >= o->col && self->col < o->col + o->len) {
                occ[o->row] = true;
            }
        }
    }
}

void at_slide_range(const at_board_t *b, int idx, int *out_min, int *out_max)
{
    const at_car_t *self = &b->cars[idx];
    bool occ[AT_GRID];
    axis_occupancy(b, idx, occ);

    /* Only the target's row has a gap in the right-hand wall: it can go on
     * sliding until it is fully out. */
    int limit = self->target ? (AT_GRID + self->len) : AT_GRID;
    int pos0  = self->horizontal ? self->col : self->row;

    int lo = pos0;
    while (lo > 0 && !occ[lo - 1]) {
        lo--;
    }
    int hi = pos0;
    while (hi + self->len < limit && (hi + self->len >= AT_GRID || !occ[hi + self->len])) {
        hi++;
    }

    *out_min = lo;
    *out_max = hi;
}

bool at_apply_move(at_board_t *b, int idx, int pos)
{
    if (idx < 0 || idx >= b->count) {
        return false;
    }
    int lo, hi;
    at_slide_range(b, idx, &lo, &hi);
    if (pos < lo) pos = lo;
    if (pos > hi) pos = hi;

    at_car_t *self = &b->cars[idx];
    int old = self->horizontal ? self->col : self->row;
    if (pos == old) {
        return false;
    }
    if (self->horizontal) {
        self->col = (int8_t)pos;
    } else {
        self->row = (int8_t)pos;
    }
    return true;
}

bool at_is_solved(const at_board_t *b)
{
    if (b->target_idx < 0) {
        return false;
    }
    return b->cars[b->target_idx].col >= AT_GRID;
}

bool at_target_at_exit(const at_board_t *b)
{
    if (b->target_idx < 0) {
        return false;
    }
    const at_car_t *t = &b->cars[b->target_idx];
    return t->col + t->len >= AT_GRID;
}

int at_car_at(const at_board_t *b, int row, int col)
{
    for (int i = 0; i < b->count; i++) {
        const at_car_t *c = &b->cars[i];
        if (c->horizontal) {
            if (row == c->row && col >= c->col && col < c->col + c->len) {
                return i;
            }
        } else {
            if (col == c->col && row >= c->row && row < c->row + c->len) {
                return i;
            }
        }
    }
    return -1;
}

bool at_parse_level(const char *const rows[AT_GRID], at_board_t *out)
{
    memset(out, 0, sizeof(*out));
    out->target_idx = -1;

    for (char letter = 'A'; letter <= 'Z'; letter++) {
        int min_r = AT_GRID, max_r = -1, min_c = AT_GRID, max_c = -1, hits = 0;

        for (int r = 0; r < AT_GRID; r++) {
            for (int c = 0; c < AT_GRID; c++) {
                if (rows[r][c] != letter) {
                    continue;
                }
                hits++;
                if (r < min_r) min_r = r;
                if (r > max_r) max_r = r;
                if (c < min_c) min_c = c;
                if (c > max_c) max_c = c;
            }
        }
        if (hits == 0) {
            continue;
        }
        if (out->count >= AT_MAX_CARS) {
            return false;
        }

        bool horizontal = (min_r == max_r);
        int  len        = horizontal ? (max_c - min_c + 1) : (max_r - min_r + 1);
        /* A continuous straight line occupies exactly 'hits' cells of length;
         * if it does not match, the letter formed an L or had gaps. */
        if (len != hits) {
            return false;
        }

        at_car_t *car  = &out->cars[out->count];
        car->row       = (int8_t)min_r;
        car->col       = (int8_t)min_c;
        car->len       = (int8_t)len;
        car->horizontal = horizontal;
        car->target    = (letter == 'A');
        car->letter    = letter;
        car->skin      = 0;

        if (car->target) {
            out->target_idx = out->count;
        }
        out->count++;
    }

    if (out->target_idx < 0) {
        return false;
    }

    /* Visual models: one counter per length, so neighbouring cars of the same
     * size do not all look alike. The target has its own fixed model, chosen
     * by the drawer (at_cars.c), so it does not count. */
    uint8_t next2 = 0, next3 = 0;
    for (int i = 0; i < out->count; i++) {
        at_car_t *car = &out->cars[i];
        if (car->target) {
            continue;
        }
        car->skin = (car->len >= 3) ? next3++ : next2++;
    }

    return true;
}
