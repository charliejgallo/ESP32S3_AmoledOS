/*
 * NEON SNAKES - the rules (see ns_game.h for the shape of the state)
 *
 * A step moves every snake at once, which is where the classic single-snake
 * code gets it wrong the moment there are two: whether a head may enter a
 * cell depends on whether the tail that is there right now leaves on this
 * same step, and that depends on whether ITS snake survives the step. So the
 * step is three passes - decide, resolve collisions until nothing changes,
 * apply (every tail first, then every head) - and two heads that want the
 * same cell both die.
 */
#include "ns_game.h"

#include <string.h>

const int8_t NS_DX[4] = { 0, 1, 0, -1 };
const int8_t NS_DY[4] = { -1, 0, 1, 0 };

/* Growth per kind: the big ones are worth more. Sparks are +1. */
static const uint8_t GROWTH[NS_FRUIT_COUNT] = { 1, 1, 1, 2, 1, 1, 3 };

int ns_fruit_growth(int kind)
{
    return kind < NS_FRUIT_COUNT ? GROWTH[kind] : 1;
}

/* xorshift32: rand() is not in the firmware's table, and it would not be
 * deterministic across the two watches anyway. */
static uint32_t rnd(ns_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

static int rnd_n(ns_game_t *g, int n)
{
    return (int)(rnd(g) % (uint32_t)n);
}

static inline int idx(const ns_game_t *g, int x, int y) { return y * g->cols + x; }
static inline bool inside(const ns_game_t *g, int x, int y)
{
    return x >= 0 && y >= 0 && x < g->cols && y < g->rows;
}

static void event(ns_game_t *g, uint8_t type, int snake, int x, int y, int kind)
{
    if (g->nev >= NS_MAX_EVENTS) {
        return;
    }
    ns_event_t *e = &g->ev[g->nev++];
    e->type  = type;
    e->snake = (uint8_t)snake;
    e->x     = (uint8_t)x;
    e->y     = (uint8_t)y;
    e->kind  = (uint8_t)kind;
}

static void mark_snake(ns_game_t *g, const ns_snake_t *s)
{
    for (int k = 0; k < s->len; k++) {
        ns_mark(g, ns_seg_x(s, k), ns_seg_y(s, k));
    }
}

/* -------------------------------------------------------------------------- */
/* Fruits                                                                      */

static bool spawn_fruit(ns_game_t *g)
{
    int n = g->cols * g->rows;
    /* Random tries first; a nearly full board falls back to a scan from a
     * random start so it still finds the last hole. */
    for (int t = 0; t < 24; t++) {
        int i = rnd_n(g, n);
        if (g->grid[i] == 0) {
            int kind = rnd_n(g, NS_FRUIT_COUNT);
            g->grid[i] = (uint16_t)(NS_CELL_FRUIT | kind);
            g->nfruits++;
            ns_mark(g, i % g->cols, i / g->cols);
            return true;
        }
    }
    int start = rnd_n(g, n);
    for (int k = 0; k < n; k++) {
        int i = (start + k) % n;
        if (g->grid[i] == 0) {
            g->grid[i] = (uint16_t)(NS_CELL_FRUIT | rnd_n(g, NS_FRUIT_COUNT));
            g->nfruits++;
            ns_mark(g, i % g->cols, i / g->cols);
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------------- */
/* Snakes on and off the board                                                 */

static void place(ns_game_t *g, int i, int hx, int hy, int dir, int len)
{
    ns_snake_t *s = &g->s[i];
    s->len      = (uint16_t)len;
    s->grow     = 0;
    s->dir      = (uint8_t)dir;
    s->alive    = 1;
    s->dying    = 0;
    s->respawn  = 0;
    s->age      = 0;
    /* keep counting from where it was: a cell of the old body that is still
     * somewhere as a spark says nothing, but a fresh numbering could make a
     * stale value look alive to a check */
    s->head_seq = (uint16_t)(s->head_seq + NS_RING);
    for (int k = len - 1; k >= 0; k--) {
        int x = hx - NS_DX[dir] * k;
        int y = hy - NS_DY[dir] * k;
        uint16_t seq = (uint16_t)(s->head_seq - k);
        s->x[seq & (NS_RING - 1)] = (uint8_t)x;
        s->y[seq & (NS_RING - 1)] = (uint8_t)y;
        g->grid[idx(g, x, y)] = (uint16_t)(((i + 1) << 12) | (seq & NS_SEQ_MASK));
        ns_mark(g, x, y);
    }
    event(g, NS_EV_SPAWN, i, hx, hy, 0);
}

/* A place for a snake of 'len' heading 'dir': the body on empty cells and a
 * clear run ahead, away from the other heads. */
static bool spawn_snake(ns_game_t *g, int i, int len)
{
    for (int t = 0; t < 80; t++) {
        int dir = rnd_n(g, 4);
        int hx  = rnd_n(g, g->cols);
        int hy  = rnd_n(g, g->rows);
        bool ok = true;
        for (int k = -6; k < len && ok; k++) {       /* k < 0: the run ahead */
            int x = hx - NS_DX[dir] * k;
            int y = hy - NS_DY[dir] * k;
            if (!inside(g, x, y)) { ok = false; break; }
            uint16_t c = g->grid[idx(g, x, y)];
            if (k >= 0 ? c != 0 : NS_IS_SNAKE(c)) ok = false;
        }
        for (int j = 0; j < g->nsnakes && ok; j++) {
            const ns_snake_t *o = &g->s[j];
            if (j == i || !o->alive) continue;
            int dx = ns_seg_x(o, 0) - hx, dy = ns_seg_y(o, 0) - hy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy < 6) ok = false;
        }
        if (ok) {
            place(g, i, hx, hy, dir, len);
            return true;
        }
    }
    return false;
}

/* The flash is over: the body leaves the board, and in combat every other
 * segment stays behind as a spark of its colour. */
static void remove_snake(ns_game_t *g, int i)
{
    ns_snake_t *s = &g->s[i];
    for (int k = 0; k < s->len; k++) {
        int x = ns_seg_x(s, k), y = ns_seg_y(s, k);
        uint16_t *c = &g->grid[idx(g, x, y)];
        if (NS_IS_SNAKE(*c) && NS_SNAKE_OF(*c) == i) {
            bool spark = g->mode == NS_MODE_COMBAT && (k & 1) == 0;
            *c = spark ? (uint16_t)(NS_CELL_FRUIT | (NS_SPARK_0 + s->colour)) : 0;
            ns_mark(g, x, y);
        }
    }
    s->alive   = 0;
    s->dying   = 0;
    s->respawn = s->human ? NS_RESPAWN_HUMAN : NS_RESPAWN_BOT;
    if (g->mode == NS_MODE_NORMAL) {
        g->over = 1;
    }
}

/* -------------------------------------------------------------------------- */
/* Init                                                                        */

void ns_init(ns_game_t *g, uint8_t mode, uint32_t seed, int humans, int snakes)
{
    memset(g, 0, sizeof *g);
    g->mode = mode;
    g->rng  = seed ? seed : 0x6E656F6Eu;             /* "neon" */
    for (int k = 0; k < 8; k++) rnd(g);              /* stir a small seed */

    if (mode == NS_MODE_NORMAL) {
        g->cols = 21;
        g->rows = 24;
        g->nsnakes = 1;
        g->fruit_target = 1;
        g->s[0].human  = 1;
        g->s[0].colour = 0;
        place(g, 0, g->cols / 2, g->rows * 2 / 3, NS_UP, 4);
    } else {
        g->cols = 34;
        g->rows = 40;
        if (snakes > NS_MAX_SNAKES) snakes = NS_MAX_SNAKES;
        if (snakes < 1) snakes = 1;
        g->nsnakes = (uint8_t)snakes;
        g->fruit_target = 9;
        for (int i = 0; i < snakes; i++) {
            g->s[i].human  = i < humans;
            g->s[i].colour = (uint8_t)i;
        }
        /* The humans start on fixed, fair places - the two lower quarters
         * heading up - and the bots wherever the dice say. */
        for (int i = 0; i < snakes; i++) {
            if (i < humans && i < 2) {
                int hx = i == 0 ? g->cols / 4 : g->cols - 1 - g->cols / 4;
                place(g, i, hx, g->rows * 3 / 4, NS_UP, 5);
            } else if (!spawn_snake(g, i, 5)) {
                g->s[i].respawn = 1;
            }
        }
    }
    while (g->nfruits < g->fruit_target && spawn_fruit(g)) {
    }
    g->chg_any = true;
}

/* -------------------------------------------------------------------------- */
/* The bot                                                                     */

/* Would a head be allowed into (x, y) on the next step, as far as the bot can
 * tell? A tail that is about to leave counts as free. */
static bool free_for_bot(const ns_game_t *g, int x, int y)
{
    if (!inside(g, x, y)) {
        return false;
    }
    uint16_t c = g->grid[idx(g, x, y)];
    if (!NS_IS_SNAKE(c)) {
        return true;
    }
    const ns_snake_t *o = &g->s[NS_SNAKE_OF(c)];
    return !o->dying && o->grow == 0 && ns_seg_index(o, c) == o->len - 1;
}

/* How many cells can be reached from (x, y), up to 'limit'. */
static int flood(ns_game_t *g, int x0, int y0, int limit)
{
    if (++g->stamp == 0) {
        memset(g->seen, 0, sizeof g->seen);
        g->stamp = 1;
    }
    int head = 0, tail = 0, count = 0;
    g->queue[tail++] = (uint16_t)idx(g, x0, y0);
    g->seen[idx(g, x0, y0)] = g->stamp;
    while (head < tail && count < limit) {
        int i = g->queue[head++];
        count++;
        int x = i % g->cols, y = i / g->cols;
        for (int d = 0; d < 4; d++) {
            int nx = x + NS_DX[d], ny = y + NS_DY[d];
            if (!inside(g, nx, ny)) continue;
            int j = idx(g, nx, ny);
            if (g->seen[j] == g->stamp || NS_IS_SNAKE(g->grid[j])) continue;
            g->seen[j] = g->stamp;
            g->queue[tail++] = (uint16_t)j;
        }
    }
    return count;
}

uint8_t ns_bot_choice(ns_game_t *g, int i)
{
    ns_snake_t *s = &g->s[i];
    int hx = ns_seg_x(s, 0), hy = ns_seg_y(s, 0);

    /* the nearest thing to eat */
    int fx = -1, fy = -1, best_d = 1 << 20;
    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            if (!NS_IS_FRUIT(g->grid[idx(g, x, y)])) continue;
            int d = (x > hx ? x - hx : hx - x) + (y > hy ? y - hy : hy - y);
            if (d < best_d) { best_d = d; fx = x; fy = y; }
        }
    }

    int limit = s->len + 6;
    if (limit < 14) limit = 14;
    if (limit > 60) limit = 60;

    const uint8_t cand[3] = { s->dir, (uint8_t)((s->dir + 3) & 3), (uint8_t)((s->dir + 1) & 3) };
    int  score[3];
    bool ok[3];
    int  best = 0, nok = 0;
    for (int c = 0; c < 3; c++) {
        int nx = hx + NS_DX[cand[c]], ny = hy + NS_DY[cand[c]];
        int jitter = rnd_n(g, 6);               /* drawn always: same count of draws */
        ok[c] = free_for_bot(g, nx, ny);
        if (!ok[c]) {
            score[c] = -1000000;
        } else {
            nok++;
            int sc = jitter;
            int space = flood(g, nx, ny, limit);
            if (space < limit) sc -= (limit - space) * 60;
            if (fx >= 0) {
                int d = (fx > nx ? fx - nx : nx - fx) + (fy > ny ? fy - ny : ny - fy);
                sc -= d * 8;
            }
            if (c == 0) sc += 3;
            for (int j = 0; j < g->nsnakes; j++) {
                const ns_snake_t *o = &g->s[j];
                if (j == i || !o->alive || o->dying) continue;
                int dx = ns_seg_x(o, 0) - nx, dy = ns_seg_y(o, 0) - ny;
                if ((dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1))) {
                    sc -= 150;                  /* a head could land there too */
                }
            }
            score[c] = sc;
        }
        if (score[c] > score[best]) best = c;
    }
    /* Now and then a bot blunders into a free cell it did not weigh: without
     * that they never die on their own and the arena fills up. */
    if (rnd_n(g, 70) == 0 && nok > 1) {
        int pick = rnd_n(g, nok);
        for (int c = 0; c < 3; c++) {
            if (ok[c] && pick-- == 0) { best = c; break; }
        }
    }
    return cand[best];
}

/* -------------------------------------------------------------------------- */
/* The step                                                                    */

void ns_step(ns_game_t *g, const uint8_t dirs[NS_MAX_SNAKES])
{
    int n = g->nsnakes;
    g->step_no++;

    /* 1. The dead: flashing, leaving, coming back. */
    for (int i = 0; i < n; i++) {
        ns_snake_t *s = &g->s[i];
        if (s->alive && s->dying) {
            s->dying--;
            mark_snake(g, s);
            if (s->dying == 0) {
                remove_snake(g, i);
            }
        } else if (!s->alive && g->mode == NS_MODE_COMBAT && s->respawn) {
            if (--s->respawn == 0 && !spawn_snake(g, i, 5)) {
                s->respawn = 1;                 /* no room yet: try next step */
            }
        }
    }

    /* 2. Decide. */
    bool moving[NS_MAX_SNAKES] = { 0 }, dead[NS_MAX_SNAKES] = { 0 }, eat[NS_MAX_SNAKES] = { 0 };
    bool vacates[NS_MAX_SNAKES] = { 0 };
    int  tx[NS_MAX_SNAKES], ty[NS_MAX_SNAKES];
    for (int i = 0; i < n; i++) {
        ns_snake_t *s = &g->s[i];
        if (!s->alive || s->dying) continue;
        moving[i] = true;
        uint8_t d = s->human ? (dirs ? dirs[i] : NS_NODIR) : ns_bot_choice(g, i);
        if (d < 4 && d != ((s->dir + 2) & 3)) {
            s->dir = d;
        }
        tx[i] = ns_seg_x(s, 0) + NS_DX[s->dir];
        ty[i] = ns_seg_y(s, 0) + NS_DY[s->dir];
        if (!inside(g, tx[i], ty[i])) {
            dead[i] = true;
            continue;
        }
        eat[i]     = NS_IS_FRUIT(g->grid[idx(g, tx[i], ty[i])]);
        vacates[i] = s->grow == 0 && !eat[i];
    }

    /* 3. Collisions, until nothing changes: a snake that dies keeps its tail,
     * which can kill the one that was counting on it leaving. */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (moving[i] && moving[j] && !dead[i] && !dead[j] &&
                tx[i] == tx[j] && ty[i] == ty[j]) {
                dead[i] = dead[j] = true;       /* head on */
            }
        }
    }
    for (bool again = true; again; ) {
        again = false;
        for (int i = 0; i < n; i++) {
            if (!moving[i] || dead[i]) continue;
            uint16_t c = g->grid[idx(g, tx[i], ty[i])];
            if (!NS_IS_SNAKE(c)) continue;
            int o = NS_SNAKE_OF(c);
            const ns_snake_t *os = &g->s[o];
            bool tail_leaves = moving[o] && !dead[o] && vacates[o] &&
                               ns_seg_index(os, c) == os->len - 1;
            if (!tail_leaves) {
                dead[i] = true;
                again = true;
            }
        }
    }

    /* 4. Apply: the deaths, every tail, then every head. */
    for (int i = 0; i < n; i++) {
        ns_snake_t *s = &g->s[i];
        if (!moving[i] || !dead[i]) continue;
        s->dying = NS_DYING_STEPS;
        mark_snake(g, s);
        event(g, NS_EV_DIE, i, ns_seg_x(s, 0), ns_seg_y(s, 0), 0);
        moving[i] = false;
    }
    for (int i = 0; i < n; i++) {
        ns_snake_t *s = &g->s[i];
        if (!moving[i]) continue;
        if (eat[i]) {
            uint16_t c = g->grid[idx(g, tx[i], ty[i])];
            int kind = c & 0x0FFF;
            s->grow = (uint16_t)(s->grow + ns_fruit_growth(kind));
            if (kind < NS_FRUIT_COUNT) g->nfruits--;
            g->grid[idx(g, tx[i], ty[i])] = 0;
            event(g, NS_EV_EAT, i, tx[i], ty[i], kind);
        }
        if (s->grow > 0 && s->len < NS_RING - 8) {
            s->grow--;
            s->len++;
        } else {
            s->grow = 0;
            int x = ns_seg_x(s, s->len - 1), y = ns_seg_y(s, s->len - 1);
            uint16_t *c = &g->grid[idx(g, x, y)];
            if (NS_IS_SNAKE(*c) && NS_SNAKE_OF(*c) == i) {
                *c = 0;
            }
            ns_mark(g, x, y);
            ns_mark(g, ns_seg_x(s, s->len - 2), ns_seg_y(s, s->len - 2));
        }
    }
    for (int i = 0; i < n; i++) {
        ns_snake_t *s = &g->s[i];
        if (!moving[i]) continue;
        ns_mark(g, ns_seg_x(s, 0), ns_seg_y(s, 0));   /* the old head becomes body */
        s->head_seq++;
        uint16_t seq = s->head_seq;
        s->x[seq & (NS_RING - 1)] = (uint8_t)tx[i];
        s->y[seq & (NS_RING - 1)] = (uint8_t)ty[i];
        g->grid[idx(g, tx[i], ty[i])] = (uint16_t)(((i + 1) << 12) | (seq & NS_SEQ_MASK));
        ns_mark(g, tx[i], ty[i]);
        if (s->age < 0xFFFF) s->age++;
    }

    /* 5. Keep the table laid. */
    while (g->nfruits < g->fruit_target && spawn_fruit(g)) {
    }
}

/* -------------------------------------------------------------------------- */
/* Checks                                                                      */

uint32_t ns_hash(const ns_game_t *g)
{
    uint32_t h = 2166136261u;
#define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619u; } while (0)
    for (int i = 0; i < g->cols * g->rows; i++) MIX(g->grid[i]);
    for (int i = 0; i < g->nsnakes; i++) {
        const ns_snake_t *s = &g->s[i];
        MIX(s->head_seq); MIX(s->len); MIX(s->grow); MIX(s->dir);
        MIX(s->alive); MIX(s->dying); MIX(s->respawn);
    }
    MIX(g->rng);
    MIX(g->step_no);
    MIX(g->nfruits);
#undef MIX
    return h;
}

int ns_check(const ns_game_t *g)
{
    int fruits = 0;
    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            uint16_t c = g->grid[idx(g, x, y)];
            if (NS_IS_FRUIT(c)) {
                if ((c & 0x0FFF) >= NS_KIND_COUNT) return 1;
                if ((c & 0x0FFF) < NS_FRUIT_COUNT) fruits++;
                continue;
            }
            if (!c) continue;
            int o = NS_SNAKE_OF(c);
            if (o < 0 || o >= g->nsnakes) return 2;
            const ns_snake_t *s = &g->s[o];
            if (!s->alive) return 3;
            int k = ns_seg_index(s, c);
            if (k >= s->len) return 4;
            if (ns_seg_x(s, k) != x || ns_seg_y(s, k) != y) return 5;
        }
    }
    if (fruits != g->nfruits) return 6;
    for (int i = 0; i < g->nsnakes; i++) {
        const ns_snake_t *s = &g->s[i];
        if (!s->alive) continue;
        for (int k = 0; k < s->len; k++) {
            int x = ns_seg_x(s, k), y = ns_seg_y(s, k);
            if (!inside(g, x, y)) return 7;
            uint16_t c = g->grid[idx(g, x, y)];
            if (!NS_IS_SNAKE(c) || NS_SNAKE_OF(c) != i || ns_seg_index(s, c) != k) return 8;
            if (k > 0) {
                int px = ns_seg_x(s, k - 1), py = ns_seg_y(s, k - 1);
                int d = (px > x ? px - x : x - px) + (py > y ? py - y : y - py);
                if (d != 1) return 9;
            }
        }
    }
    return 0;
}
