/*
 * BURBUJAS - the rules (see burbujas.h)
 *
 * Neither LVGL nor the HAL: this file is the game, and tools/bb_harness.c
 * plays thousands of shots through it in a second without a screen.
 *
 * The one idea worth knowing before reading: the aiming guide, the flight and
 * the bot all advance a shot with bb_ray_step(), one pixel at a time. The
 * dotted line is not an estimate of the shot, it IS the shot, run ahead of
 * time - so the guide can never promise a bounce the bubble will not make.
 */
#include "bb_art.h"          /* only for how long a burst and a blast last */
#include "burbujas.h"

#include <string.h>

#define POP_PTS         10
#define DROP_PTS        20
#define CLEAN_BONUS     1000
#define LEVEL_BONUS     300
#define TIMED_MS        120000
#define PUSH_WAIT_MS    280     /* between the bursts and the row coming in  */
#define QUEUE_MS        700     /* a release while busy stays valid this long */

/* --------------------------------------------------------------------------
 * Random
 * -------------------------------------------------------------------------- */

uint32_t bb_rand(bb_game_t *g)
{
    uint32_t x = g->rng ? g->rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}

int bb_rand_range(bb_game_t *g, int lo, int hi)
{
    return hi <= lo ? lo : lo + (int)(bb_rand(g) % (uint32_t)(hi - lo + 1));
}

/* --------------------------------------------------------------------------
 * The grid
 * -------------------------------------------------------------------------- */

/* The six neighbours of a cell. An offset row sits half a bubble to the
 * right, so the ones above and below are c and c+1; a straight row's are
 * c-1 and c. Getting this backwards is the classic hexagonal bug and it does
 * not crash: groups simply come out wrong, which is why the harness checks
 * that nothing is ever left hanging from nothing. */
static int neighbours(const bb_game_t *g, int r, int c, int8_t out[6][2])
{
    const int up = bb_indent(g, r) ? c : c - 1;
    const int8_t cand[6][2] = {
        { (int8_t)r,       (int8_t)(c - 1) },
        { (int8_t)r,       (int8_t)(c + 1) },
        { (int8_t)(r - 1), (int8_t)up      },
        { (int8_t)(r - 1), (int8_t)(up + 1)},
        { (int8_t)(r + 1), (int8_t)up      },
        { (int8_t)(r + 1), (int8_t)(up + 1)},
    };
    int n = 0;
    for (int i = 0; i < 6; i++) {
        if (bb_valid(g, cand[i][0], cand[i][1])) {
            out[n][0] = cand[i][0];
            out[n][1] = cand[i][1];
            n++;
        }
    }
    return n;
}

typedef uint8_t bb_cells_t[BB_ROWS][BB_COLS];

/* The connected same-colour group of (r0, c0), in breadth-first order. depth
 * is how many bubbles away each one is: the bursts are delayed by it, so a
 * group goes off from the impact outwards. */
static int flood(const bb_game_t *g, const bb_cells_t cells, int r0, int c0,
                 int8_t list[][2], uint8_t *depth)
{
    uint8_t seen[BB_ROWS][BB_COLS];
    memset(seen, 0, sizeof(seen));

    const uint8_t want = cells[r0][c0];
    int n = 0;

    list[n][0] = (int8_t)r0;
    list[n][1] = (int8_t)c0;
    if (depth) {
        depth[n] = 0;
    }
    seen[r0][c0] = 1;
    n++;

    for (int i = 0; i < n; i++) {
        int8_t nb[6][2];
        int    k = neighbours(g, list[i][0], list[i][1], nb);
        for (int j = 0; j < k; j++) {
            int r = nb[j][0], c = nb[j][1];
            if (!seen[r][c] && cells[r][c] == want) {
                seen[r][c] = 1;
                list[n][0] = (int8_t)r;
                list[n][1] = (int8_t)c;
                if (depth) {
                    depth[n] = (uint8_t)(depth[i] + 1);
                }
                n++;
            }
        }
    }
    return n;
}

/* Everything that no longer hangs from the ceiling. */
static int floating(const bb_game_t *g, const bb_cells_t cells,
                    int8_t list[][2])
{
    uint8_t seen[BB_ROWS][BB_COLS];
    memset(seen, 0, sizeof(seen));

    int8_t stack[BB_ROWS * BB_COLS][2];
    int    top = 0;

    for (int c = 0; c < bb_ncols(g, 0); c++) {
        if (cells[0][c]) {
            seen[0][c] = 1;
            stack[top][0] = 0;
            stack[top][1] = (int8_t)c;
            top++;
        }
    }
    for (int i = 0; i < top; i++) {
        int8_t nb[6][2];
        int    k = neighbours(g, stack[i][0], stack[i][1], nb);
        for (int j = 0; j < k; j++) {
            int r = nb[j][0], c = nb[j][1];
            if (!seen[r][c] && cells[r][c]) {
                seen[r][c] = 1;
                stack[top][0] = (int8_t)r;
                stack[top][1] = (int8_t)c;
                top++;
            }
        }
    }

    int n = 0;
    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            if (cells[r][c] && !seen[r][c]) {
                list[n][0] = (int8_t)r;
                list[n][1] = (int8_t)c;
                n++;
            }
        }
    }
    return n;
}

static int count_cells(const bb_game_t *g)
{
    int n = 0;
    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            n += g->cell[r][c] != 0;
        }
    }
    return n;
}

/* Which colours are actually on the board. A shot of a colour that is not
 * there any more can never burst anything, so the launcher never loads one. */
static int colors_present(const bb_game_t *g, uint8_t *list)
{
    uint8_t seen[BB_PALS];
    memset(seen, 0, sizeof(seen));

    int n = 0;
    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            uint8_t v = g->cell[r][c];
            if (v >= BC_RED && v <= BC_PURPLE && !seen[v]) {
                seen[v] = 1;
                list[n++] = v;
            }
        }
    }
    return n;
}

static uint8_t pick_color(bb_game_t *g)
{
    uint8_t list[BB_NCOLORS];
    int     n = colors_present(g, list);

    if (n > 0) {
        return list[bb_rand(g) % (uint32_t)n];
    }
    return (uint8_t)bb_rand_range(g, BC_RED, BC_RED + g->ncolors - 1);
}

/* --------------------------------------------------------------------------
 * Changing the board
 *
 * Every cell that changes marks its rectangle: the still bubbles live in the
 * background buffer, and bb_draw.c rebuilds exactly those rectangles from the
 * backdrop plus whatever bubbles touch them.
 * -------------------------------------------------------------------------- */

static void cell_dirty(bb_game_t *g, int r, int c)
{
    bb_dirty_add(&g->bgd, bb_cell_x(g, r, c) - BB_R,
                 bb_cell_y(g, r) + g->slide - BB_R, BB_D, BB_D);
}

static void set_cell(bb_game_t *g, int r, int c, uint8_t color)
{
    g->cell[r][c] = color;
    cell_dirty(g, r, c);
    g->preview_dirty = 1;
}

/* --------------------------------------------------------------------------
 * Effects
 * -------------------------------------------------------------------------- */

static bb_fx_t *fx_new(bb_game_t *g)
{
    for (int i = 0; i < BB_MAX_FX; i++) {
        if (g->fx[i].kind == FX_NONE) {
            memset(&g->fx[i], 0, sizeof(g->fx[i]));
            return &g->fx[i];
        }
    }
    return NULL;        /* a clear so big it ran out: the cell just goes */
}

static void fx_pop(bb_game_t *g, int cx, int cy, int color, int delay)
{
    bb_fx_t *f = fx_new(g);
    if (!f) {
        return;
    }
    f->kind  = FX_POP;
    f->color = (uint8_t)color;
    f->seed  = (uint8_t)bb_rand_range(g, 0, 31);
    f->delay = (int16_t)delay;
    f->x     = cx * 16;
    f->y     = cy * 16;
}

static void fx_fall(bb_game_t *g, int cx, int cy, int color, int delay)
{
    bb_fx_t *f = fx_new(g);
    if (!f) {
        return;
    }
    f->kind  = FX_FALL;
    f->color = (uint8_t)color;
    f->delay = (int16_t)delay;
    f->x     = cx * 16;
    f->y     = cy * 16;
    f->vx    = (int16_t)bb_rand_range(g, -22 * 16, 22 * 16);
    f->vy    = (int16_t)bb_rand_range(g, -40 * 16, -10 * 16);
}

static void fx_blast(bb_game_t *g, int cx, int cy)
{
    bb_fx_t *f = fx_new(g);
    if (!f) {
        return;
    }
    f->kind = FX_BLAST;
    f->x    = cx * 16;
    f->y    = cy * 16;
}

static void popup(bb_game_t *g, int x, int y, int value, int color)
{
    if (value <= 0) {
        return;
    }
    for (int i = 0; i < BB_MAX_POPUPS; i++) {
        if (!g->popup[i].on) {
            g->popup[i].on    = 1;
            g->popup[i].value = (uint16_t)(value > 9999 ? 9999 : value);
            g->popup[i].color = (uint8_t)color;
            g->popup[i].x     = (int16_t)x;
            g->popup[i].y     = (int16_t)y;
            g->popup[i].t     = 0;
            return;
        }
    }
}

/* --------------------------------------------------------------------------
 * The shot: one pixel at a time
 * -------------------------------------------------------------------------- */

int bb_ray_step(const bb_game_t *g, bb_ray_t *s, int *hit_r, int *hit_c)
{
    int res = RAY_FLY;

    s->x += s->vx;
    s->y += s->vy;

    const int32_t xmin = (BB_WALL_L + BB_R) * BB_FP;
    const int32_t xmax = (BB_WALL_R - BB_R) * BB_FP;
    if (s->x < xmin) {
        s->x = 2 * xmin - s->x;
        s->vx = -s->vx;
        res = RAY_BOUNCE;
    } else if (s->x > xmax) {
        s->x = 2 * xmax - s->x;
        s->vx = -s->vx;
        res = RAY_BOUNCE;
    }

    /* Distances in 1/32 px: (184 * 32)^2 still fits an int32 with room, and
     * no division is needed to compare them. */
    const int32_t sx = s->x >> 5, sy = s->y >> 5;
    const int32_t coll2 = (int32_t)(BB_COLL * 32) * (BB_COLL * 32);

    /* Only the nearest row and its two neighbours can be within 18 px: the
     * rows are 19 apart. */
    int rr = (s->y / BB_FP - g->board_top - BB_R + BB_ROW_H / 2) / BB_ROW_H;
    for (int r = rr - 1; r <= rr + 1; r++) {
        if (r < 0 || r >= BB_ROWS) {
            continue;
        }
        for (int c = 0; c < bb_ncols(g, r); c++) {
            if (!g->cell[r][c]) {
                continue;
            }
            int32_t dx = sx - bb_cell_x(g, r, c) * 32;
            int32_t dy = sy - bb_cell_y(g, r) * 32;
            if (dx * dx + dy * dy < coll2) {
                if (hit_r) {
                    *hit_r = r;
                }
                if (hit_c) {
                    *hit_c = c;
                }
                return RAY_BUBBLE;
            }
        }
    }

    if (s->y <= (int32_t)(g->board_top + BB_R) * BB_FP) {
        return RAY_CEIL;
    }
    return res;
}

bool bb_snap_cell(const bb_game_t *g, int32_t x, int32_t y, int *orow, int *ocol)
{
    const int32_t sx = x >> 5, sy = y >> 5;
    int32_t best = 0;
    int     br = -1, bc = -1;

    for (int r = 0; r < BB_ROWS; r++) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            if (g->cell[r][c]) {
                continue;
            }
            /* it has to hang from something: the ceiling, or a neighbour */
            bool attached = r == 0;
            if (!attached) {
                int8_t nb[6][2];
                int    k = neighbours(g, r, c, nb);
                for (int j = 0; j < k && !attached; j++) {
                    attached = g->cell[nb[j][0]][nb[j][1]] != 0;
                }
            }
            if (!attached) {
                continue;
            }
            int32_t dx = sx - bb_cell_x(g, r, c) * 32;
            int32_t dy = sy - bb_cell_y(g, r) * 32;
            int32_t d2 = dx * dx + dy * dy;
            if (br < 0 || d2 < best) {
                best = d2;
                br   = r;
                bc   = c;
            }
        }
    }
    if (br < 0) {
        return false;
    }
    *orow = br;
    *ocol = bc;
    return true;
}

/* Runs a shot from the launcher until it arrives. Fills the guide's dots if
 * asked for them. Returns RAY_BUBBLE / RAY_CEIL, or RAY_FLY if it somehow
 * never landed (the cap is a safety net, not a game rule). */
static int trace(const bb_game_t *g, int vx, int vy, bb_ray_t *end,
                 int *hr, int *hc, bb_game_t *dots)
{
    bb_ray_t s = { BB_LAUNCH_X * BB_FP, BB_LAUNCH_Y * BB_FP, vx, vy };
    int      hit_r = 0, hit_c = 0;

    if (dots) {
        dots->ndots = 0;
    }
    for (int i = 0; i < 1400; i++) {
        int res = bb_ray_step(g, &s, &hit_r, &hit_c);
        if (res == RAY_BUBBLE || res == RAY_CEIL) {
            if (end) {
                *end = s;
            }
            if (hr) {
                *hr = hit_r;
            }
            if (hc) {
                *hc = hit_c;
            }
            return res;
        }
        /* The first dots would sit under the launcher's own bubble. */
        if (dots && i > 14 && i % BB_DOT_GAP == 0 &&
            dots->ndots < BB_MAX_DOTS) {
            dots->dotx[dots->ndots] = (int16_t)(s.x / BB_FP);
            dots->doty[dots->ndots] = (int16_t)(s.y / BB_FP);
            dots->ndots++;
        }
    }
    if (end) {
        *end = s;
    }
    return RAY_FLY;
}

/* --------------------------------------------------------------------------
 * Aiming
 * -------------------------------------------------------------------------- */

static void aim_set(bb_game_t *g, int x, int y)
{
    int dx = x - BB_LAUNCH_X;
    int dy = y - BB_LAUNCH_Y;

    g->aim_ok = y < BB_DEAD_Y;
    if (!g->aim_ok) {
        return;
    }
    if (dy > -8) {
        dy = -8;
    }
    /* Below fifteen degrees a shot spends whole seconds bouncing from wall to
     * wall, so the aim is clamped there instead of being refused: the guide
     * stops following the finger and that reads as a limit. */
    int adx = dx < 0 ? -dx : dx;
    if (-(int32_t)dy * BB_FP < (int32_t)BB_MIN_TAN * adx) {
        dy = (int16_t)(-(adx * BB_MIN_TAN + BB_FP - 1) / BB_FP);
        if (dy > -8) {
            dy = -8;
        }
    }

    int len = bb_isqrt(dx * dx + dy * dy);
    if (len < 1) {
        len = 1;
    }
    int16_t vx = (int16_t)(dx * BB_FP / len);
    int16_t vy = (int16_t)(dy * BB_FP / len);

    if (vx != g->aim_vx || vy != g->aim_vy) {
        g->aim_vx = vx;
        g->aim_vy = vy;
        g->preview_dirty = 1;
    }
}

static void preview_build(bb_game_t *g)
{
    bb_ray_t end;
    int      hr = 0, hc = 0, r = 0, c = 0;

    g->preview_dirty = 0;
    g->ghost_r = -1;
    g->ghost_c = -1;
    g->ndots   = 0;

    if (!g->aiming || !g->aim_ok || g->state != GS_PLAY) {
        return;
    }
    int res = trace(g, g->aim_vx, g->aim_vy, &end, &hr, &hc, g);
    if (res == RAY_FLY) {
        return;
    }
    if (bb_snap_cell(g, end.x, end.y, &r, &c)) {
        g->ghost_r = (int8_t)r;
        g->ghost_c = (int8_t)c;
    }
}

/* --------------------------------------------------------------------------
 * Firing and landing
 * -------------------------------------------------------------------------- */

static bool ready_to_fire(const bb_game_t *g)
{
    return g->state == GS_PLAY && g->sub == PS_AIM && g->reload_ms == 0 &&
           !g->push_pending;
}

static void load_next(bb_game_t *g)
{
    g->cur = g->nxt;
    g->nxt = pick_color(g);
    g->reload_ms = BB_RELOAD_MS;
}

static void fire(bb_game_t *g)
{
    g->shot.x  = BB_LAUNCH_X * BB_FP;
    g->shot.y  = BB_LAUNCH_Y * BB_FP;
    g->shot.vx = g->aim_vx;
    g->shot.vy = g->aim_vy;
    g->shot_color = g->cur;
    g->shot_acc   = 0;
    g->sub        = PS_FLY;
    g->shots++;
    g->queued = 0;
    load_next(g);
    bb_sfx(520, 18);
}

/* A rainbow one becomes the colour of what it touched; against the ceiling,
 * the colour of whatever it ends up next to. */
static uint8_t rainbow_color(bb_game_t *g, int kind, int hr, int hc, int r, int c)
{
    if (kind == RAY_BUBBLE && g->cell[hr][hc]) {
        return g->cell[hr][hc];
    }
    int8_t nb[6][2];
    int    k = neighbours(g, r, c, nb);
    for (int j = 0; j < k; j++) {
        if (g->cell[nb[j][0]][nb[j][1]]) {
            return g->cell[nb[j][0]][nb[j][1]];
        }
    }
    return pick_color(g);
}

/* What a burst sounds like.
 *
 * A short chain is one note per bubble, climbing. A LONG one -six or more, or
 * four dropped at once, which is also what earns a special- gets a flourish
 * instead: an arpeggio that climbs two octaves and holds the last note, with
 * the falling bubbles answering underneath. It REPLACES the per-bubble notes
 * rather than adding to them, and that is not a style choice: the HAL's tone
 * queue is sixteen notes deep and a full queue DROPS what does not fit
 * (aos_hal_beep in aos_hal_esp32.c), so a chain of twenty bubbles playing a
 * note each would eat the queue and the next shot would be silent. The codec
 * stays open between notes, so these do play as a melody and not as clicks. */
#define BB_LONG_CHAIN   6
#define BB_LONG_DROP    4

static void chain_sound(bb_game_t *g, int n, int dropped)
{
    static const int STEP[5]  = { 523, 659, 784, 988, 1175 };
    static const int FLOUR[7] = { 523, 659, 784, 1047, 1319, 1568, 2093 };

    (void)g;

    if (n >= BB_LONG_CHAIN || dropped >= BB_LONG_DROP) {
        for (int i = 0; i < 6; i++) {
            bb_sfx(FLOUR[i], 55);
        }
        bb_sfx(FLOUR[6], 200);              /* the one it lands on */
        if (dropped) {
            bb_sfx(392, 70);                /* and the fall, underneath */
            bb_sfx(262, 120);
        }
        return;
    }

    int k = n < 5 ? n : 5;
    for (int i = 0; i < k; i++) {
        bb_sfx(STEP[i], 40);
    }
    if (dropped) {
        bb_sfx(392, 60);
        bb_sfx(294, 90);
    }
}

static void lose(bb_game_t *g)
{
    g->state     = GS_ENDING;
    g->won       = 0;
    g->phase_ms  = 0;
    g->grey_from = BB_ROWS;
    g->aiming    = 0;
    g->touching  = 0;
    g->events   |= EV_LOST;
    bb_sfx(392, 140);
    bb_sfx(311, 140);
    bb_sfx(233, 260);
}

static void win(bb_game_t *g)
{
    g->state    = GS_ENDING;
    g->won      = 1;
    g->phase_ms = 0;
    g->aiming   = 0;
    g->touching = 0;
    g->events  |= EV_WIN;
    bb_sfx(659, 90);
    bb_sfx(880, 90);
    bb_sfx(1047, 90);
    bb_sfx(1319, 200);
}

static bool over_the_line(const bb_game_t *g)
{
    for (int r = BB_ROWS - 1; r >= 0; r--) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            if (g->cell[r][c] && bb_cell_y(g, r) + BB_R > BB_DEAD_Y) {
                return true;
            }
        }
    }
    return false;
}

static void danger_update(bb_game_t *g)
{
    uint8_t d = 0;
    for (int r = BB_ROWS - 1; r >= 0 && !d; r--) {
        for (int c = 0; c < bb_ncols(g, r); c++) {
            if (g->cell[r][c] &&
                bb_cell_y(g, r) + BB_R > BB_DEAD_Y - BB_ROW_H) {
                d = 1;
                break;
            }
        }
    }
    g->danger = d;
}

/* A burst of six, or four dropped at once, earns the next special. It goes
 * into the pipe right away: the reward has to be visible when it is earned,
 * not one shot later. */
static void maybe_special(bb_game_t *g, int popped, int dropped)
{
    if (g->nxt == BC_BOMB || g->nxt == BC_RAINBOW || g->cur == BC_BOMB ||
        g->cur == BC_RAINBOW) {
        return;
    }
    if (popped < 6 && dropped < 4) {
        return;
    }
    g->nxt = (g->special_turn ^= 1) ? BC_BOMB : BC_RAINBOW;
    g->events |= EV_SPECIAL;
}

static void classic_difficulty(bb_game_t *g)
{
    uint8_t colors = g->rows_pushed >= 16 ? 6 : g->rows_pushed >= 6 ? 5 : 4;
    uint8_t limit  = g->rows_pushed >= 20 ? 3 : g->rows_pushed >= 10 ? 4
                   : g->rows_pushed >= 4  ? 5 : 6;
    if (colors > g->ncolors) {
        g->ncolors = colors;
        g->events |= EV_COLOR;
    }
    g->miss_limit = limit;
}

static void land(bb_game_t *g)
{
    int r = 0, c = 0;
    int hr = 0, hc = 0;
    (void)hr;
    (void)hc;

    g->sub = PS_AIM;
    if (!bb_snap_cell(g, g->shot.x, g->shot.y, &r, &c)) {
        lose(g);                        /* nowhere left to put it */
        return;
    }

    uint8_t color = g->shot_color;
    int     cx = bb_cell_x(g, r, c), cy = bb_cell_y(g, r);
    int     popped = 0, dropped = 0, delay = 0;
    int8_t  list[BB_ROWS * BB_COLS][2];
    uint8_t depth[BB_ROWS * BB_COLS];

    if (color == BC_RAINBOW) {
        color = rainbow_color(g, g->shot_kind, g->shot_hit_r, g->shot_hit_c, r, c);
    }

    if (color == BC_BOMB) {
        /* everything within a bubble and a half: the six neighbours and the
         * ring behind them */
        fx_blast(g, cx, cy);
        for (int rr = 0; rr < BB_ROWS; rr++) {
            for (int cc = 0; cc < bb_ncols(g, rr); cc++) {
                if (!g->cell[rr][cc]) {
                    continue;
                }
                int dx = bb_cell_x(g, rr, cc) - cx;
                int dy = bb_cell_y(g, rr) - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 <= 40 * 40) {
                    fx_pop(g, bb_cell_x(g, rr, cc), bb_cell_y(g, rr),
                           g->cell[rr][cc], bb_isqrt(d2) * 3);
                    set_cell(g, rr, cc, BC_NONE);
                    popped++;
                }
            }
        }
        delay = 120;
        bb_sfx(140, 90);
        bb_sfx(90, 160);
    } else {
        set_cell(g, r, c, color);
        int n = flood(g, (const uint8_t (*)[BB_COLS])g->cell, r, c, list, depth);
        if (n >= 3) {
            for (int i = 0; i < n; i++) {
                int rr = list[i][0], cc = list[i][1];
                int d  = depth[i] * 35;
                fx_pop(g, bb_cell_x(g, rr, cc), bb_cell_y(g, rr),
                       g->cell[rr][cc], d);
                set_cell(g, rr, cc, BC_NONE);
                if (d > delay) {
                    delay = d;
                }
            }
            popped = n;
        } else {
            bb_sfx(300, 20);
        }
    }

    if (popped) {
        int n = floating(g, (const uint8_t (*)[BB_COLS])g->cell, list);
        for (int i = 0; i < n; i++) {
            int rr = list[i][0], cc = list[i][1];
            fx_fall(g, bb_cell_x(g, rr, cc), bb_cell_y(g, rr),
                    g->cell[rr][cc], delay + 80);
            set_cell(g, rr, cc, BC_NONE);
        }
        dropped = n;
        chain_sound(g, popped, dropped > 0);
    }

    int pts = popped * POP_PTS + dropped * DROP_PTS * (1 + dropped / 5);
    g->score   += (uint32_t)pts;
    g->popped  += (uint16_t)popped;
    g->dropped += (uint16_t)dropped;
    if (dropped > g->best_drop) {
        g->best_drop = (uint16_t)dropped;
    }
    popup(g, cx, cy - 6, pts, popped ? color : BC_GREY);

    maybe_special(g, popped, dropped);

    /* A shot that burst nothing is a miss, and misses are what bring the next
     * row in. In the timed mode the clock does that instead. */
    if (!popped && g->mode != MODE_TIMED) {
        if (++g->misses >= g->miss_limit) {
            g->misses = 0;
            g->push_pending++;
            g->push_wait = PUSH_WAIT_MS;
        }
    }

    /* The launcher never holds a colour that is not on the board any more. */
    uint8_t list_c[BB_NCOLORS];
    if (colors_present(g, list_c) > 0) {
        if (g->cur >= BC_RED && g->cur <= BC_PURPLE) {
            bool ok = false;
            for (int i = 0; i < BB_NCOLORS && !ok; i++) {
                ok = i < colors_present(g, list_c) && list_c[i] == g->cur;
            }
            if (!ok) {
                g->cur = pick_color(g);
            }
        }
        if (g->nxt >= BC_RED && g->nxt <= BC_PURPLE) {
            bool ok = false;
            int  n  = colors_present(g, list_c);
            for (int i = 0; i < n && !ok; i++) {
                ok = list_c[i] == g->nxt;
            }
            if (!ok) {
                g->nxt = pick_color(g);
            }
        }
    }

    if (count_cells(g) == 0) {
        if (g->mode == MODE_LEVELS) {
            g->score += LEVEL_BONUS +
                        (uint32_t)(g->miss_limit - g->misses) * 50u;
            win(g);
            return;
        }
        g->score += CLEAN_BONUS;
        g->events |= EV_CLEAN;
        g->push_pending = 3;
        g->push_wait    = PUSH_WAIT_MS;
        popup(g, BB_W / 2, BB_CEIL_Y + 40, CLEAN_BONUS, BC_YELLOW);
    }

    danger_update(g);
    if (over_the_line(g)) {
        lose(g);
    }
}

/* --------------------------------------------------------------------------
 * Rows coming in, the ceiling coming down
 * -------------------------------------------------------------------------- */

static uint8_t row_color(bb_game_t *g, int c)
{
    /* Colours in clusters and not at random: a row of eight unrelated colours
     * is unplayable, and one of eight equal ones is free. */
    if (c > 0 && g->cell[0][c - 1] && bb_rand_range(g, 0, 99) < 40) {
        return g->cell[0][c - 1];
    }
    if (g->cell[1][c] && bb_rand_range(g, 0, 99) < 30) {
        return g->cell[1][c];
    }
    return (uint8_t)bb_rand_range(g, BC_RED, BC_RED + g->ncolors - 1);
}

static void push_row(bb_game_t *g)
{
    for (int r = BB_ROWS - 1; r > 0; r--) {
        memcpy(g->cell[r], g->cell[r - 1], BB_COLS);
    }
    memset(g->cell[0], 0, BB_COLS);
    g->parity ^= 1;

    for (int c = 0; c < bb_ncols(g, 0); c++) {
        g->cell[0][c] = row_color(g, c);
    }
    g->rows_pushed++;
    if (g->mode == MODE_CLASSIC) {
        classic_difficulty(g);
    }
}

static void start_push(bb_game_t *g)
{
    if (g->mode == MODE_LEVELS) {
        g->drops++;
        g->board_top = (int16_t)(BB_CEIL_Y + g->drops * BB_ROW_H);
    } else {
        push_row(g);
    }
    g->slide    = -BB_ROW_H;
    g->slide_ms = 0;
    g->sub      = PS_PUSH;
    g->bg_all   = 1;
    g->events  |= EV_PUSH;
    g->preview_dirty = 1;
    bb_sfx(160, 70);
}

static void step_push(bb_game_t *g, int dt)
{
    g->slide_ms = (uint16_t)(g->slide_ms + dt);
    if (g->slide_ms >= BB_SLIDE_MS) {
        g->slide = 0;
        g->sub   = PS_AIM;
        g->push_pending--;
        if (g->push_pending) {
            g->push_wait = 120;
        }
        g->bg_all = 1;
        danger_update(g);
        if (over_the_line(g)) {
            lose(g);
        }
        return;
    }
    /* eased, so the row arrives without a jolt */
    int p = g->slide_ms * 1000 / BB_SLIDE_MS;
    p = 1000 - (1000 - p) * (1000 - p) / 1000;
    g->slide  = (int16_t)(-BB_ROW_H + BB_ROW_H * p / 1000);
    g->bg_all = 1;
}

/* --------------------------------------------------------------------------
 * Levels
 *
 * Generated and not written down: the level number is the seed, so level 12
 * is the same board today and in a month, and there is no table to maintain.
 * -------------------------------------------------------------------------- */

static bool mask_cell(int pattern, int r, int c, int rows, int ncols)
{
    switch (pattern) {
    case 0: return true;                                    /* solid        */
    case 1: return c >= r / 2 && c < ncols - r / 2;          /* a wedge      */
    case 2: return (r + c) % 3 != 2;                         /* holes        */
    case 3: return (c % 4) < 2 || r == 0;                    /* pillars      */
    case 4: {                                               /* a diamond    */
        int mid = ncols / 2;
        int d   = (c < mid ? mid - c : c - mid) + (rows / 2 > r ? rows / 2 - r
                                                                : r - rows / 2);
        return d <= rows / 2 + 1;
    }
    default: return r % 2 == 0 || c % 2 == 0;                /* a lattice    */
    }
}

static void level_build(bb_game_t *g, int level)
{
    const int L = level < 1 ? 1 : level;

    g->rng     = 0x9E3779B9u ^ ((uint32_t)L * 2654435761u);
    g->ncolors = (uint8_t)(3 + (L - 1) / 3);
    if (g->ncolors > BB_NCOLORS) {
        g->ncolors = BB_NCOLORS;
    }
    g->miss_limit = (uint8_t)(7 - (L - 1) / 5);
    if (g->miss_limit < 4) {
        g->miss_limit = 4;
    }
    g->parity = (uint8_t)(L & 1);

    /* Level 1 gets one row less than the ramp asks for. It is the level you
     * play before knowing how the thing aims, and with four rows a bad streak
     * stacks a tower to the line in seven shots without the ceiling coming
     * down once - measured with `bb_harness poke`, which shoots at four fixed
     * points. One row is one row of headroom. */
    int rows = L == 1 ? 3 : 4 + (L - 1) / 3;
    if (rows > 6) {
        rows = 6;
    }
    const int pattern = (L - 1) % 6;

    memset(g->cell, 0, sizeof(g->cell));
    for (int r = 0; r < rows; r++) {
        int ncols = bb_ncols(g, r);
        for (int c = 0; c < ncols; c++) {
            if (!mask_cell(pattern, r, c, rows, ncols)) {
                continue;
            }
            if (pattern == 5) {
                g->cell[r][c] = (uint8_t)(BC_RED + ((r + c / 2) % g->ncolors));
            } else {
                g->cell[r][c] = row_color(g, c);
            }
        }
    }

    /* Nothing may start hanging from nothing: the board would drop as soon as
     * the first bubble arrived, which looks like a bug and plays like a gift. */
    int8_t list[BB_ROWS * BB_COLS][2];
    int    n = floating(g, (const uint8_t (*)[BB_COLS])g->cell, list);
    for (int i = 0; i < n; i++) {
        g->cell[list[i][0]][list[i][1]] = BC_NONE;
    }
}

/* --------------------------------------------------------------------------
 * A game
 * -------------------------------------------------------------------------- */

void bb_game_init(bb_game_t *g, uint32_t seed)
{
    g->rng     = seed ? seed : 0xB0BB1E5u;
    g->state   = GS_TITLE;
    g->sub     = PS_AIM;
    g->ghost_r = -1;
    g->ghost_c = -1;
    g->board_top = BB_CEIL_Y;
}

void bb_game_start(bb_game_t *g, int mode, int level)
{
    uint32_t rng = g->rng;
    uint8_t  fps = g->show_fps, bot = g->autoplay;
    uint32_t best = g->best;

    memset(g->cell, 0, sizeof(g->cell));
    memset(g->fx, 0, sizeof(g->fx));
    memset(g->popup, 0, sizeof(g->popup));

    g->rng       = rng;
    g->show_fps  = fps;
    g->autoplay  = bot;
    g->best      = best;
    g->mode      = (uint8_t)mode;
    g->state     = GS_PLAY;
    g->sub       = PS_AIM;
    g->won       = 0;
    g->parity    = 0;
    g->board_top = BB_CEIL_Y;
    g->slide     = 0;
    g->slide_ms  = 0;
    g->drops     = 0;
    g->grey_from = BB_ROWS;
    g->danger    = 0;
    g->score     = 0;
    g->level     = (uint16_t)(mode == MODE_LEVELS ? (level < 1 ? 1 : level) : 0);
    g->shots     = 0;
    g->popped    = 0;
    g->dropped   = 0;
    g->best_drop = 0;
    g->misses    = 0;
    g->push_pending = 0;
    g->push_wait = 0;
    g->rows_pushed = 0;
    g->elapsed_ms  = 0;
    g->phase_ms  = 0;
    g->hurried   = 0;
    g->reload_ms = 0;
    g->queued    = 0;
    g->aiming    = 0;
    g->touching  = 0;
    g->aim_ok    = 0;
    g->aim_vx    = 0;
    g->aim_vy    = -BB_FP;
    g->ndots     = 0;
    g->ghost_r   = -1;
    g->ghost_c   = -1;
    g->events    = 0;
    g->time_ms   = 0;
    g->push_ms   = 0;

    switch (mode) {
    case MODE_LEVELS:
        level_build(g, g->level);
        g->events |= EV_LEVEL;
        break;

    case MODE_TIMED:
        g->ncolors    = 5;
        g->miss_limit = 0;
        g->time_ms    = TIMED_MS;
        g->push_every = 9000;
        g->push_ms    = g->push_every;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < bb_ncols(g, r); c++) {
                g->cell[r][c] = row_color(g, c);
            }
        }
        g->events |= EV_GO;
        break;

    default:
        g->ncolors    = 4;
        g->miss_limit = 6;
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < bb_ncols(g, r); c++) {
                g->cell[r][c] = row_color(g, c);
            }
        }
        g->events |= EV_GO;
        break;
    }

    g->cur = pick_color(g);
    g->nxt = pick_color(g);
    danger_update(g);
    g->preview_dirty = 1;
    g->bg_all = 1;
}

/* --------------------------------------------------------------------------
 * Touch
 * -------------------------------------------------------------------------- */

void bb_game_swap(bb_game_t *g)
{
    if (g->state != GS_PLAY || g->reload_ms) {
        return;
    }
    uint8_t t = g->cur;
    g->cur = g->nxt;
    g->nxt = t;
    g->preview_dirty = 1;
    bb_sfx(880, 18);
}

void bb_game_press(bb_game_t *g, int x, int y)
{
    if (g->state != GS_PLAY) {
        return;
    }
    if (y >= BB_SWAP_Y) {
        g->touching = 2;
        bb_game_swap(g);
        return;
    }
    g->touching = 1;
    g->aiming   = 1;
    aim_set(g, x, y);
    g->preview_dirty = 1;
}

void bb_game_drag(bb_game_t *g, int x, int y)
{
    if (g->state != GS_PLAY || g->touching != 1) {
        return;
    }
    uint8_t was = g->aim_ok;
    aim_set(g, x, y);
    if (was != g->aim_ok) {
        g->preview_dirty = 1;
    }
}

void bb_game_release(bb_game_t *g, int x, int y)
{
    if (g->state != GS_PLAY || g->touching != 1) {
        g->touching = 0;
        return;
    }
    aim_set(g, x, y);
    g->touching = 0;
    g->aiming   = 0;
    g->preview_dirty = 1;

    if (!g->aim_ok) {
        return;                         /* let go below the line: cancelled */
    }
    if (ready_to_fire(g)) {
        fire(g);
    } else {
        /* Released while the last one was still arriving: remembered, so a
         * quick player is not punished for being quick. */
        g->queued    = 1;
        g->queued_ms = QUEUE_MS;
    }
}

void bb_game_cancel(bb_game_t *g)
{
    g->touching = 0;
    g->aiming   = 0;
    g->preview_dirty = 1;
}

/* --------------------------------------------------------------------------
 * Effects and the clock
 * -------------------------------------------------------------------------- */

static void step_fx(bb_game_t *g, int dt)
{
    for (int i = 0; i < BB_MAX_FX; i++) {
        bb_fx_t *f = &g->fx[i];
        if (!f->kind) {
            continue;
        }
        if (f->delay > 0) {
            f->delay = (int16_t)(f->delay - dt);
            continue;
        }
        f->t = (uint16_t)(f->t + dt);

        switch (f->kind) {
        case FX_POP:
            if (f->t >= BB_POP_FRAMES * BB_POP_MS) {
                f->kind = FX_NONE;
            }
            break;

        case FX_BLAST:
            if (f->t >= BB_BLAST_FRAMES * BB_BLAST_MS) {
                f->kind = FX_NONE;
            }
            break;

        case FX_FALL:
            f->vy = (int16_t)(f->vy + 900 * 16 * dt / 1000);
            f->x += f->vx * dt / 1000;
            f->y += f->vy * dt / 1000;
            if (f->y > (BB_H + BB_D) * 16 || f->x < -BB_D * 16 ||
                f->x > (BB_W + BB_D) * 16) {
                f->kind = FX_NONE;
            }
            break;

        default:
            f->kind = FX_NONE;
            break;
        }
    }

    for (int i = 0; i < BB_MAX_POPUPS; i++) {
        if (g->popup[i].on) {
            g->popup[i].t = (uint16_t)(g->popup[i].t + dt);
            if (g->popup[i].t >= 700) {
                g->popup[i].on = 0;
            }
        }
    }
}

static void step_ending(bb_game_t *g, int dt)
{
    g->phase_ms = (uint16_t)(g->phase_ms + dt);

    if (!g->won) {
        /* The board greys out from the bottom up: the game says "this is what
         * did you in" instead of jumping straight to a panel. */
        int want = BB_ROWS - g->phase_ms / 70;
        if (want < 0) {
            want = 0;
        }
        while (g->grey_from > want) {
            g->grey_from--;
            bb_dirty_add(&g->bgd, 0,
                         bb_cell_y(g, g->grey_from) - BB_R, BB_W, BB_D);
        }
        if (g->grey_from == 0 && g->phase_ms > BB_ROWS * 70 + 500) {
            g->state   = GS_OVER;
            g->events |= EV_OVER;
        }
        return;
    }
    if (g->phase_ms > 1400) {
        g->state   = GS_OVER;
        g->events |= EV_OVER;
    }
}

void bb_game_step(bb_game_t *g, int dt_ms)
{
    const int dt = dt_ms < 1 ? 1 : dt_ms > 100 ? 100 : dt_ms;

    step_fx(g, dt);

    if (g->state == GS_ENDING) {
        step_ending(g, dt);
        return;
    }
    if (g->state != GS_PLAY) {
        return;
    }
    g->elapsed_ms += (uint32_t)dt;

    if (g->reload_ms) {
        g->reload_ms = (uint16_t)(g->reload_ms > dt ? g->reload_ms - dt : 0);
    }
    if (g->queued_ms) {
        g->queued_ms = (uint16_t)(g->queued_ms > dt ? g->queued_ms - dt : 0);
        if (!g->queued_ms) {
            g->queued = 0;
        }
    }

    if (g->mode == MODE_TIMED) {
        g->time_ms -= dt;
        g->push_ms -= dt;
        g->push_every = 9000 - 4000 * (int32_t)g->elapsed_ms / TIMED_MS;
        if (g->time_ms <= 10000 && !g->hurried) {
            g->hurried = 1;
            g->events |= EV_HURRY;
        }
        if (g->time_ms <= 0) {
            g->time_ms = 0;
            g->events |= EV_TIMEUP;
            win(g);                     /* time up is not a defeat here */
            return;
        }
        if (g->push_ms <= 0) {
            g->push_ms = g->push_every;
            g->push_pending++;
            g->push_wait = PUSH_WAIT_MS;
        }
    }

    switch (g->sub) {
    case PS_FLY: {
        g->shot_acc += (int32_t)dt * BB_SPEED;
        int steps = (int)(g->shot_acc / 1000);
        g->shot_acc -= (int32_t)steps * 1000;

        for (int i = 0; i < steps; i++) {
            int hr = 0, hc = 0;
            int res = bb_ray_step(g, &g->shot, &hr, &hc);
            if (res == RAY_BOUNCE) {
                bb_sfx(1200, 8);
            } else if (res == RAY_BUBBLE || res == RAY_CEIL) {
                g->shot_kind  = (uint8_t)res;
                g->shot_hit_r = (int8_t)hr;
                g->shot_hit_c = (int8_t)hc;
                land(g);
                break;
            }
        }
        break;
    }

    case PS_PUSH:
        step_push(g, dt);
        break;

    default:
        if (g->push_pending) {
            if (g->push_wait > dt) {
                g->push_wait = (uint16_t)(g->push_wait - dt);
            } else {
                g->push_wait = 0;
                start_push(g);
            }
        } else if (g->queued && ready_to_fire(g)) {
            fire(g);
        }
        break;
    }

    if (g->preview_dirty) {
        preview_build(g);
    }
}

/* --------------------------------------------------------------------------
 * The bot
 *
 * It is here for three reasons: BB_AUTO=1 leaves the game playing itself on
 * the Mac for an hour, the harness needs a player, and a game that a bot
 * cannot finish is a game whose rules have a hole.
 * -------------------------------------------------------------------------- */

/* What a shot of 'color' landing on (r, c) would be worth. */
static int value_of(const bb_game_t *g, int r, int c, int color, int hit_r,
                    int hit_c)
{
    bb_cells_t cells;
    int8_t     list[BB_ROWS * BB_COLS][2];

    memcpy(cells, g->cell, sizeof(cells));

    if (bb_cell_y(g, r) + BB_R > BB_DEAD_Y) {
        return -10000;
    }
    if (color == BC_BOMB) {
        int cx = bb_cell_x(g, r, c), cy = bb_cell_y(g, r), n = 0;
        for (int rr = 0; rr < BB_ROWS; rr++) {
            for (int cc = 0; cc < bb_ncols(g, rr); cc++) {
                int dx = bb_cell_x(g, rr, cc) - cx, dy = bb_cell_y(g, rr) - cy;
                if (cells[rr][cc] && dx * dx + dy * dy <= 40 * 40) {
                    cells[rr][cc] = BC_NONE;
                    n++;
                }
            }
        }
        return 90 * n + 150 * floating(g, (const uint8_t (*)[BB_COLS])cells, list);
    }
    if (color == BC_RAINBOW) {
        color = hit_r >= 0 && cells[hit_r][hit_c] ? cells[hit_r][hit_c] : BC_RED;
    }

    cells[r][c] = (uint8_t)color;
    uint8_t depth[BB_ROWS * BB_COLS];
    int n = flood(g, (const uint8_t (*)[BB_COLS])cells, r, c, list, depth);
    if (n >= 3) {
        for (int i = 0; i < n; i++) {
            cells[list[i][0]][list[i][1]] = BC_NONE;
        }
        int f = floating(g, (const uint8_t (*)[BB_COLS])cells, list);
        return 100 * n + 160 * f;
    }

    /* nothing bursts: at least leave it next to its own colour, and high up */
    int8_t nb[6][2];
    int    k = neighbours(g, r, c, nb), same = 0;
    for (int j = 0; j < k; j++) {
        same += cells[nb[j][0]][nb[j][1]] == (uint8_t)color;
    }
    return 25 * same - 12 * r;
}

static void bot_choose(bb_game_t *g)
{
    int best = -1000000, bvx = 0, bvy = -BB_FP;
    bool swap = false;

    for (int pass = 0; pass < 2; pass++) {
        int color = pass == 0 ? g->cur : g->nxt;
        for (int a = 11; a <= 117; a++) {           /* 15..165 degrees */
            bb_ray_t end;
            int      hr = -1, hc = -1, r = 0, c = 0;
            int      vx = bb_cos(a) * 4, vy = -bb_sin(a) * 4;

            int res = trace(g, vx, vy, &end, &hr, &hc, NULL);
            if (res == RAY_FLY || !bb_snap_cell(g, end.x, end.y, &r, &c)) {
                continue;
            }
            int v = value_of(g, r, c, color, res == RAY_BUBBLE ? hr : -1, hc);
            v += bb_rand_range(g, 0, 9);
            if (pass == 1) {
                v -= 40;                /* swapping costs a turn's worth */
            }
            if (v > best) {
                best = v;
                bvx  = vx;
                bvy  = vy;
                swap = pass == 1;
            }
        }
    }

    /* it misses on purpose now and then: a bot that never misses never sees
     * a row come in, and that is half the game */
    if (bb_rand_range(g, 0, 99) < 8) {
        int a = bb_rand_range(g, 11, 117);
        bvx  = bb_cos(a) * 4;
        bvy  = -bb_sin(a) * 4;
        swap = false;
    }
    if (swap) {
        bb_game_swap(g);
    }
    g->aim_vx = (int16_t)bvx;
    g->aim_vy = (int16_t)bvy;
    g->aiming = 1;
    g->aim_ok = 1;
    g->preview_dirty = 1;
}

void bb_game_bot(bb_game_t *g, int dt_ms)
{
    if (g->state != GS_PLAY) {
        return;
    }
    g->bot_ms = (uint16_t)(g->bot_ms + dt_ms);

    if (g->bot_phase == 0) {
        if (!ready_to_fire(g) || g->bot_ms < 260) {
            return;
        }
        g->bot_ms = 0;
        g->bot_phase = 1;
        bot_choose(g);
        return;
    }
    if (g->bot_ms >= 220) {
        g->bot_ms    = 0;
        g->bot_phase = 0;
        g->aiming    = 0;
        g->preview_dirty = 1;
        if (ready_to_fire(g)) {
            fire(g);
        }
    }
}
