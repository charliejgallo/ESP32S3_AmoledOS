/*
 * GEMAS - the board (see gm_board.h)
 */
#include "gm_board.h"

#include <string.h>

/* -------------------------------------------------------------------------- */

uint32_t gm_rnd(gm_board_t *b)
{
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b->rng = x;
    return x;
}

int gm_rnd_range(gm_board_t *b, int lo, int hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + (int)(gm_rnd(b) % (uint32_t)(hi - lo + 1));
}

/* --------------------------------------------------------------------------
 * Completed lines
 *
 * First the runs of three or more on each axis are marked (bit 1 horizontal,
 * bit 2 vertical) and then the marked cells that touch are joined up. With
 * that an L or a T comes out as a single group, which is what is needed to
 * decide which special jewel it leaves.
 *
 * The hypercube enters no run: it is a wildcard, not a colour.
 * -------------------------------------------------------------------------- */

static void runs_mark(const gm_board_t *b, uint8_t hit[GM_N][GM_N])
{
    memset(hit, 0, (size_t)GM_N * GM_N);

    for (int r = 0; r < GM_N; r++) {
        int c = 0;
        while (c < GM_N) {
            int8_t t = b->c[r][c].type;
            if (t < 0 || b->c[r][c].special == GM_SP_HYPER) {
                c++;
                continue;
            }
            int e = c + 1;
            while (e < GM_N && b->c[r][e].type == t &&
                   b->c[r][e].special != GM_SP_HYPER) {
                e++;
            }
            if (e - c >= 3) {
                for (int i = c; i < e; i++) {
                    hit[r][i] |= 1;
                }
            }
            c = e;
        }
    }

    for (int c = 0; c < GM_N; c++) {
        int r = 0;
        while (r < GM_N) {
            int8_t t = b->c[r][c].type;
            if (t < 0 || b->c[r][c].special == GM_SP_HYPER) {
                r++;
                continue;
            }
            int e = r + 1;
            while (e < GM_N && b->c[e][c].type == t &&
                   b->c[e][c].special != GM_SP_HYPER) {
                e++;
            }
            if (e - r >= 3) {
                for (int i = r; i < e; i++) {
                    hit[i][c] |= 2;
                }
            }
            r = e;
        }
    }
}

int gm_find_groups(const gm_board_t *b, gm_group_t *out, int max,
                   int swap_r, int swap_c)
{
    uint8_t hit[GM_N][GM_N];
    runs_mark(b, hit);

    uint8_t seen[GM_N][GM_N];
    memset(seen, 0, sizeof(seen));

    int ngroups = 0;

    for (int r0 = 0; r0 < GM_N && ngroups < max; r0++) {
        for (int c0 = 0; c0 < GM_N && ngroups < max; c0++) {
            if (!hit[r0][c0] || seen[r0][c0]) {
                continue;
            }

            gm_group_t *g = &out[ngroups];
            memset(g, 0, sizeof(*g));
            g->type = b->c[r0][c0].type;

            /* explicit stack: no recursion in a dynamic app if it can be
             * avoided, since the LVGL task's stack is not large */
            uint8_t stack[GM_N * GM_N];
            int top = 0;
            stack[top++] = (uint8_t)(r0 * GM_N + c0);
            seen[r0][c0] = 1;

            while (top > 0) {
                uint8_t id = stack[--top];
                int r = id / GM_N, c = id % GM_N;
                if (g->n < (uint8_t)sizeof(g->cell)) {
                    g->cell[g->n++] = id;
                }

                static const int dr[4] = { -1, 1, 0, 0 };
                static const int dc[4] = { 0, 0, -1, 1 };
                for (int k = 0; k < 4; k++) {
                    int nr = r + dr[k], nc = c + dc[k];
                    if (nr < 0 || nr >= GM_N || nc < 0 || nc >= GM_N) {
                        continue;
                    }
                    if (seen[nr][nc] || !hit[nr][nc]) {
                        continue;
                    }
                    if (b->c[nr][nc].type != g->type) {
                        continue;
                    }
                    seen[nr][nc] = 1;
                    stack[top++] = (uint8_t)(nr * GM_N + nc);
                }
            }

            /* longest run on each axis, within the group */
            uint8_t in[GM_N][GM_N];
            memset(in, 0, sizeof(in));
            for (int i = 0; i < g->n; i++) {
                in[g->cell[i] / GM_N][g->cell[i] % GM_N] = 1;
            }
            for (int r = 0; r < GM_N; r++) {
                int run = 0;
                for (int c = 0; c < GM_N; c++) {
                    run = in[r][c] ? run + 1 : 0;
                    if (run > g->run_h) g->run_h = (uint8_t)run;
                }
            }
            for (int c = 0; c < GM_N; c++) {
                int run = 0;
                for (int r = 0; r < GM_N; r++) {
                    run = in[r][c] ? run + 1 : 0;
                    if (run > g->run_v) g->run_v = (uint8_t)run;
                }
            }

            if (g->run_h >= 5 || g->run_v >= 5) {
                g->special = GM_SP_HYPER;
            } else if (g->run_h >= 3 && g->run_v >= 3) {
                g->special = GM_SP_STAR;
            } else if (g->run_h == 4 || g->run_v == 4) {
                g->special = GM_SP_FLAME;
            } else {
                g->special = GM_SP_NONE;
            }

            /* Where the new jewel appears: where the player left their finger
             * if that place is part of the group; failing that, at the L's
             * junction; and failing that, in the middle. */
            g->pr = g->cell[g->n / 2] / GM_N;
            g->pc = g->cell[g->n / 2] % GM_N;

            bool placed = false;
            if (swap_r >= 0 && swap_c >= 0 && in[swap_r][swap_c]) {
                g->pr = (uint8_t)swap_r;
                g->pc = (uint8_t)swap_c;
                placed = true;
            }
            if (!placed) {
                for (int i = 0; i < g->n; i++) {
                    int r = g->cell[i] / GM_N, c = g->cell[i] % GM_N;
                    if (hit[r][c] == 3) {       /* junction of horizontal and vertical */
                        g->pr = (uint8_t)r;
                        g->pc = (uint8_t)c;
                        break;
                    }
                }
            }

            ngroups++;
        }
    }

    return ngroups;
}

/* -------------------------------------------------------------------------- */

int gm_mark_count(const uint8_t mark[GM_N][GM_N])
{
    int n = 0;
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            if (mark[r][c]) {
                n++;
            }
        }
    }
    return n;
}

int gm_mark_color(const gm_board_t *b, uint8_t mark[GM_N][GM_N], int type)
{
    int n = 0;
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            if (b->c[r][c].type == type && !mark[r][c]) {
                mark[r][c] = 1;
                n++;
            }
        }
    }
    return n;
}

int gm_expand_specials(const gm_board_t *b, uint8_t mark[GM_N][GM_N])
{
    uint8_t fired[GM_N][GM_N];
    memset(fired, 0, sizeof(fired));

    bool again = true;
    while (again) {
        again = false;
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                if (!mark[r][c] || fired[r][c]) {
                    continue;
                }
                uint8_t sp = b->c[r][c].special;
                if (sp == GM_SP_NONE) {
                    continue;
                }
                fired[r][c] = 1;
                again = true;

                if (sp == GM_SP_FLAME) {
                    for (int dr = -1; dr <= 1; dr++) {
                        for (int dc = -1; dc <= 1; dc++) {
                            int nr = r + dr, nc = c + dc;
                            if (nr >= 0 && nr < GM_N && nc >= 0 && nc < GM_N) {
                                mark[nr][nc] = 1;
                            }
                        }
                    }
                } else if (sp == GM_SP_STAR) {
                    for (int i = 0; i < GM_N; i++) {
                        mark[r][i] = 1;
                        mark[i][c] = 1;
                    }
                } else if (sp == GM_SP_HYPER) {
                    /* if an explosion catches it, it takes its own colour */
                    gm_mark_color(b, mark, b->c[r][c].type);
                }
            }
        }
    }

    return gm_mark_count(mark);
}

/* -------------------------------------------------------------------------- */

void gm_collapse(gm_board_t *b, const uint8_t mark[GM_N][GM_N], gm_fall_t *fall)
{
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            fall->from[r][c] = -1;
            fall->born[r][c] = 0;
        }
    }

    for (int c = 0; c < GM_N; c++) {
        int write = GM_N - 1;

        for (int r = GM_N - 1; r >= 0; r--) {
            if (mark[r][c]) {
                continue;
            }
            b->c[write][c] = b->c[r][c];
            fall->from[write][c] = (int8_t)r;
            write--;
        }

        int born = 1;
        while (write >= 0) {
            b->c[write][c].type    = (int8_t)gm_rnd_range(b, 0, b->ncolors - 1);
            b->c[write][c].special = GM_SP_NONE;
            fall->from[write][c]   = -1;
            fall->born[write][c]   = (int8_t)born++;
            write--;
        }
    }
}

/* -------------------------------------------------------------------------- */

/* Is there a line of three through (r,c)? It is the cheap test the swap and
 * the move search use. */
static bool line_at(const gm_board_t *b, int r, int c)
{
    int8_t t = b->c[r][c].type;
    if (t < 0 || b->c[r][c].special == GM_SP_HYPER) {
        return false;
    }

    int n = 1;
    for (int i = c - 1; i >= 0 && b->c[r][i].type == t &&
         b->c[r][i].special != GM_SP_HYPER; i--) n++;
    for (int i = c + 1; i < GM_N && b->c[r][i].type == t &&
         b->c[r][i].special != GM_SP_HYPER; i++) n++;
    if (n >= 3) {
        return true;
    }

    n = 1;
    for (int i = r - 1; i >= 0 && b->c[i][c].type == t &&
         b->c[i][c].special != GM_SP_HYPER; i--) n++;
    for (int i = r + 1; i < GM_N && b->c[i][c].type == t &&
         b->c[i][c].special != GM_SP_HYPER; i++) n++;
    return n >= 3;
}

bool gm_swap_makes_match(gm_board_t *b, int r1, int c1, int r2, int c2)
{
    gm_cell_t tmp = b->c[r1][c1];
    b->c[r1][c1] = b->c[r2][c2];
    b->c[r2][c2] = tmp;

    bool ok = line_at(b, r1, c1) || line_at(b, r2, c2);

    tmp = b->c[r1][c1];
    b->c[r1][c1] = b->c[r2][c2];
    b->c[r2][c2] = tmp;
    return ok;
}

bool gm_find_move(const gm_board_t *b, uint8_t out[4])
{
    gm_board_t copy = *b;       /* the test touches the board and leaves it as it was */

    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            /* the hypercube can always be played against any neighbour */
            if (copy.c[r][c].special == GM_SP_HYPER) {
                out[0] = (uint8_t)r; out[1] = (uint8_t)c;
                out[2] = (uint8_t)r;
                out[3] = (uint8_t)(c + 1 < GM_N ? c + 1 : c - 1);
                return true;
            }
            if (c + 1 < GM_N && gm_swap_makes_match(&copy, r, c, r, c + 1)) {
                out[0] = (uint8_t)r; out[1] = (uint8_t)c;
                out[2] = (uint8_t)r; out[3] = (uint8_t)(c + 1);
                return true;
            }
            if (r + 1 < GM_N && gm_swap_makes_match(&copy, r, c, r + 1, c)) {
                out[0] = (uint8_t)r;       out[1] = (uint8_t)c;
                out[2] = (uint8_t)(r + 1); out[3] = (uint8_t)c;
                return true;
            }
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */

void gm_board_init(gm_board_t *b, uint32_t seed, int ncolors)
{
    memset(b, 0, sizeof(*b));
    b->rng = seed | 1u;
    b->ncolors = (ncolors < 3) ? 3 : (ncolors > GM_TYPES ? GM_TYPES : ncolors);
}

void gm_board_fill(gm_board_t *b)
{
    for (int intento = 0; intento < 40; intento++) {
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                int8_t t;
                int guard = 0;
                do {
                    t = (int8_t)gm_rnd_range(b, 0, b->ncolors - 1);
                    guard++;
                } while (guard < 20 &&
                         ((c >= 2 && b->c[r][c - 1].type == t && b->c[r][c - 2].type == t) ||
                          (r >= 2 && b->c[r - 1][c].type == t && b->c[r - 2][c].type == t)));
                b->c[r][c].type    = t;
                b->c[r][c].special = GM_SP_NONE;
            }
        }
        if (gm_has_move(b)) {
            return;
        }
    }
}

bool gm_board_shuffle(gm_board_t *b)
{
    gm_cell_t bag[GM_N * GM_N];
    int n = 0;
    for (int r = 0; r < GM_N; r++) {
        for (int c = 0; c < GM_N; c++) {
            bag[n++] = b->c[r][c];
        }
    }

    for (int intento = 0; intento < 60; intento++) {
        for (int i = n - 1; i > 0; i--) {
            int j = gm_rnd_range(b, 0, i);
            gm_cell_t t = bag[i];
            bag[i] = bag[j];
            bag[j] = t;
        }
        int k = 0;
        for (int r = 0; r < GM_N; r++) {
            for (int c = 0; c < GM_N; c++) {
                b->c[r][c] = bag[k++];
            }
        }

        gm_group_t groups[GM_MAX_GROUPS];
        if (gm_find_groups(b, groups, GM_MAX_GROUPS, -1, -1) == 0 && gm_has_move(b)) {
            return true;
        }
    }

    gm_board_fill(b);       /* bad luck: a new board */
    return false;
}
