/*
 * AmoledOS - Maze
 *
 * A ball rolling through a maze as you tilt the board, with pits that send it
 * back to the start and an exit that opens the next level. The maze is
 * generated from scratch on every level (backtracking, so it always has a
 * solution and it is unique).
 *
 * The three things you need to know to touch it:
 *
 * 1. THE DRAWING IS BY DIRTY RECTANGLES, arkanos's recipe. There are two
 *    logical buffers of 184x192: 'bg' with the maze standing still and 'fb'
 *    with the frame. Per frame, the rectangle the ball dirtied on the previous
 *    frame is restored from bg, the ball is drawn on the new one, and ONLY the
 *    union of the two is upscaled and invalidated. That is some 200 pixels out
 *    of 35 thousand: the rest of the budget is left for the physics. The scale
 *    2 upscaling is done by hand; LV_IMAGE_ALIGN_STRETCH costs a measured
 *    129 ms per frame.
 *
 * 2. THE COLLISION IS AGAINST A BITMAP, not against wall rectangles. 'solid'
 *    has one byte per logical pixel, is filled once when the level is
 *    generated and the rest of the game asks pixel by pixel. It is far less
 *    code than carrying each cell's wall list and it does not get the corners
 *    wrong, which is where the other way always gets them wrong.
 *
 * 3. THE ACCELEROMETER'S AXES. The board measures (2026-08-28,
 *    DECISIONES.md) with +ax pointing DOWN the screen and the right at -ay, so
 *    the ball's acceleration is (x = -ay, y = +ax). That mapping is measured
 *    but was not tested with this app in hand, and getting it wrong is easy:
 *    that is why the ZERO button, besides zeroing the current tilt with a tap,
 *    cycles the four possible mappings if it is held down. It is stored in
 *    preferences.
 */
#include "aos_app.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Geometry -------------------------------------------------------------
 * Exact scale 2: 184x192 logical is 368x384 on screen, and the 64 px left over
 * at the bottom are the bar. The maze is 15x15 cells of 12 px plus the outside
 * wall: 182x182, centred with 1 px on each side. */
#define LW          184
#define LH          192
#define SCALE       2
#define DW          (LW * SCALE)
#define DH          (LH * SCALE)
#define BAR_H       (AOS_SCREEN_H - DH)

/* The bar goes ON TOP, and not at the bottom as it was.
 *
 * Measured on the board on 2026-09-06: the digitiser reports nothing past
 * y = 395 however far down the panel draws. The buttons were at 410..444, that
 * is, entirely inside the strip that cannot be touched, and that is why they
 * never responded. The system's lowest calibration point is at y = 393
 * (AOS_SCREEN_H - 55), so nothing below that was ever measured: the ceiling
 * was always there and had gone unnoticed.
 *
 * Here raising it is free because the maze's canvas does NOT receive touches
 * -the ball is driven by tilting the board-, so it can be pushed down without
 * losing anything. In Minesweeper the same does not hold: there the board IS
 * touched and lowering it would put it into the dead strip. */
#define BAR_Y       0
#define CANVAS_Y    BAR_H

#define MW          15
#define MH          15
#define CELL        12
#define WT          2                   /* wall thickness */
#define OX          ((LW - (MW * CELL + WT)) / 2)
#define OY          ((LH - (MH * CELL + WT)) / 2)

#define BALL_R      3                   /* half-side of the collision box */
#define MAX_HOLES   8
#define HOLE_R      4

#define FRAME_MS    33
#define IMU_MS      100                 /* the IMU shares the I2C with the touch panel */

/* Physics in logical pixels per frame. GAIN converts g into px/frame^2. */
#define GAIN        0.55f
#define DAMP        0.985f
#define VMAX        3.0f
#define BOUNCE      0.30f
#define DEADZONE    0.02f

/* Walls per cell, for generating. */
#define WN  1u
#define WE  2u
#define WS  4u
#define WWW 8u

typedef struct {
    lv_obj_t   *canvas;
    lv_obj_t   *lbl_info;
    lv_obj_t   *lbl_axes;
    lv_timer_t *timer;

    uint16_t *big;              /* 368x384 RGB565, PSRAM */
    uint16_t *fb;               /* 184x192, the frame          */
    uint16_t *bg;               /* 184x192, the maze alone     */
    uint8_t  *solid;            /* 184x192, 1 = wall           */

    uint8_t wall[MW * MH];

    float x, y;                 /* centre of the ball, in logical px */
    float vx, vy;
    float ax, ay;               /* acceleration applied, already mapped */
    float zero_a, zero_b;       /* tilt reference, in raw axes */

    int hole_x[MAX_HOLES];
    int hole_y[MAX_HOLES];
    int holes;

    int goal_cx, goal_cy;
    int level;
    int falls;
    int axes;                   /* 0..3, axis mapping */

    uint32_t start_ms;
    uint32_t elapsed_s;
    uint32_t last_imu_ms;
    uint32_t last_info_ms;
    int      best_level;

    /* rectangle the ball dirtied on the previous frame */
    int prev_x0, prev_y0, prev_x1, prev_y1;
    bool prev_valid;

    bool won;                   /* it reached the exit: a short pause */
    uint32_t won_until_ms;

    uint32_t rng;
    aos_app_t *self;

    /* Development switches (getenv returns NULL on the board) */
    bool    autoplay;
    uint8_t next_cell[MW * MH];     /* MAZE_AUTO: where to go from each cell */
} maze_t;

static maze_t s_z;

/* Forward-declared: used by new_level(), which comes before them because the
 * file's reading order is the game's, not the compiler's. */
static void solve(void);

/* Colours in RGB565, without going through LVGL: they are written pixel by
 * pixel. */
static uint16_t C_FLOOR, C_WALL, C_BALL, C_SHINE, C_HOLE, C_HOLE_RIM,
                C_GOAL, C_GOAL2;

static uint16_t rgb565(uint32_t rgb)
{
    return (uint16_t)(((rgb >> 19) & 0x1F) << 11 |
                      ((rgb >> 10) & 0x3F) << 5  |
                      ((rgb >> 3)  & 0x1F));
}

static void colors_init(void)
{
    C_FLOOR = rgb565(0x101014);
    C_WALL  = rgb565(0x4A4A52);
    C_BALL  = rgb565(0xFF9F0A);
    C_SHINE = rgb565(0xFFE0A0);
    C_HOLE     = rgb565(0x000000);
    C_HOLE_RIM = rgb565(0x8E8E93);
    C_GOAL  = rgb565(0x30D158);
    C_GOAL2 = rgb565(0x0E4622);
}

static uint32_t rnd(void)
{
    uint32_t x = s_z.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_z.rng = x;
    return x;
}

/* -------------------------------------------------------------------------- */
/* Generating the maze                                                         */

static void carve(void)
{
    /* Backtracking with an explicit stack. Recursive it would be 225 frames in
     * the LVGL task's 20 KB of stack, which is exactly the kind of thing that
     * on the board does not give a clean error. */
    static uint8_t seen[MW * MH];
    static int16_t stack[MW * MH];
    int top = 0;

    memset(seen, 0, sizeof(seen));
    memset(s_z.wall, (int)(WN | WE | WS | WWW), sizeof(s_z.wall));

    int cur = 0;
    seen[cur] = 1;
    stack[top++] = (int16_t)cur;

    while (top > 0) {
        cur = stack[top - 1];
        int cx = cur % MW, cy = cur / MW;

        int cand[4], nd[4], n = 0;
        if (cy > 0      && !seen[cur - MW]) { cand[n] = cur - MW; nd[n++] = 0; }
        if (cx < MW - 1 && !seen[cur + 1])  { cand[n] = cur + 1;  nd[n++] = 1; }
        if (cy < MH - 1 && !seen[cur + MW]) { cand[n] = cur + MW; nd[n++] = 2; }
        if (cx > 0      && !seen[cur - 1])  { cand[n] = cur - 1;  nd[n++] = 3; }

        if (n == 0) {
            top--;
            continue;
        }
        int pick = (int)(rnd() % (uint32_t)n);
        int nxt  = cand[pick];

        switch (nd[pick]) {
        case 0: s_z.wall[cur] &= ~WN;  s_z.wall[nxt] &= ~WS;  break;
        case 1: s_z.wall[cur] &= ~WE;  s_z.wall[nxt] &= ~WWW; break;
        case 2: s_z.wall[cur] &= ~WS;  s_z.wall[nxt] &= ~WN;  break;
        default: s_z.wall[cur] &= ~WWW; s_z.wall[nxt] &= ~WE;  break;
        }
        seen[nxt] = 1;
        stack[top++] = (int16_t)nxt;
    }
}

/* -------------------------------------------------------------------------- */
/* Rasterising the background                                                  */

static void fill_rect(uint16_t *buf, int x, int y, int w, int h, uint16_t c,
                      bool mark_solid)
{
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= LH) {
            continue;
        }
        uint16_t *row = buf + (size_t)j * LW;
        uint8_t  *sol = s_z.solid + (size_t)j * LW;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= LW) {
                continue;
            }
            row[i] = c;
            if (mark_solid) {
                sol[i] = 1;
            }
        }
    }
}

static void disc(uint16_t *buf, int cx, int cy, int r, uint16_t c)
{
    int r2 = r * r + r;         /* a shade more than r^2: it rounds better */
    for (int dy = -r; dy <= r; dy++) {
        int j = cy + dy;
        if (j < 0 || j >= LH) {
            continue;
        }
        uint16_t *row = buf + (size_t)j * LW;
        for (int dx = -r; dx <= r; dx++) {
            int i = cx + dx;
            if (i < 0 || i >= LW || dx * dx + dy * dy > r2) {
                continue;
            }
            row[i] = c;
        }
    }
}

static float cell_center(int c)
{
    return (float)(c * CELL + WT) + (float)(CELL - WT) / 2.0f - 0.5f;
}

static void build_background(void)
{
    memset(s_z.solid, 0, (size_t)LW * LH);

    /* All wall outside the board: that way the ball cannot escape through a
     * rounding hole at the edges. */
    for (int i = 0; i < LW * LH; i++) {
        s_z.bg[i] = C_FLOOR;
    }
    for (int j = 0; j < LH; j++) {
        for (int i = 0; i < LW; i++) {
            bool fuera = i < OX || i >= OX + MW * CELL + WT ||
                         j < OY || j >= OY + MH * CELL + WT;
            if (fuera) {
                s_z.solid[(size_t)j * LW + i] = 1;
                s_z.bg[(size_t)j * LW + i]    = C_FLOOR;
            }
        }
    }

    /* Each cell contributes its north wall and its west wall; the outer ones
     * on the east and south sides are the two bands at the end. With the walls
     * carved in pairs, that is enough to draw the whole maze without
     * repeating. */
    for (int cy = 0; cy < MH; cy++) {
        for (int cx = 0; cx < MW; cx++) {
            int x = OX + cx * CELL;
            int y = OY + cy * CELL;
            uint8_t w = s_z.wall[cy * MW + cx];
            if (w & WN) {
                fill_rect(s_z.bg, x, y, CELL + WT, WT, C_WALL, true);
            }
            if (w & WWW) {
                fill_rect(s_z.bg, x, y, WT, CELL + WT, C_WALL, true);
            }
        }
    }
    fill_rect(s_z.bg, OX + MW * CELL, OY, WT, MH * CELL + WT, C_WALL, true);
    fill_rect(s_z.bg, OX, OY + MH * CELL, MW * CELL + WT, WT, C_WALL, true);

    /* The exit: a green square with a darker border. */
    {
        int x = OX + s_z.goal_cx * CELL + WT;
        int y = OY + s_z.goal_cy * CELL + WT;
        fill_rect(s_z.bg, x, y, CELL - WT, CELL - WT, C_GOAL2, false);
        fill_rect(s_z.bg, x + 2, y + 2, CELL - WT - 4, CELL - WT - 4, C_GOAL, false);
    }

    /* The pits. Black and not marked solid: the ball falls in, it does not
     * bounce. They come with a slightly lighter ring around them because a
     * black disc on a nearly black floor cannot be seen, and a pit you cannot
     * see is not a difficulty, it is a trap. */
    for (int i = 0; i < s_z.holes; i++) {
        disc(s_z.bg, s_z.hole_x[i], s_z.hole_y[i], HOLE_R + 1, C_HOLE_RIM);
        disc(s_z.bg, s_z.hole_x[i], s_z.hole_y[i], HOLE_R, C_HOLE);
    }

    memcpy(s_z.fb, s_z.bg, (size_t)LW * LH * 2);
}

/* -------------------------------------------------------------------------- */
/* Upscaling and flushing                                                      */

static void expand(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LW) x1 = LW;
    if (y1 > LH) y1 = LH;
    int span = x1 - x0;
    if (span <= 0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        const uint16_t *s   = s_z.fb + (size_t)y * LW + x0;
        uint16_t       *row = s_z.big + (size_t)y * SCALE * DW + (size_t)x0 * SCALE;
        for (int i = 0; i < span; i++) {
            uint16_t c = s[i];
            row[i * 2]     = c;
            row[i * 2 + 1] = c;
        }
        memcpy(row + DW, row, (size_t)span * SCALE * 2);
    }
}

static void push(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LW) x1 = LW;
    if (y1 > LH) y1 = LH;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    expand(x0, y0, x1, y1);

    /* lv_obj_invalidate_area() wants absolute screen coordinates */
    lv_area_t co;
    lv_obj_get_coords(s_z.canvas, &co);
    lv_area_t area = {
        .x1 = co.x1 + x0 * SCALE,
        .y1 = co.y1 + y0 * SCALE,
    };
    area.x2 = co.x1 + x1 * SCALE - 1;
    area.y2 = co.y1 + y1 * SCALE - 1;
    lv_obj_invalidate_area(s_z.canvas, &area);
}

static void push_all(void)
{
    push(0, 0, LW, LH);
    s_z.prev_valid = false;
}

/* -------------------------------------------------------------------------- */
/* Level                                                                       */

static bool hole_free(int cx, int cy)
{
    if (cx == 0 && cy == 0) {
        return false;                       /* the start */
    }
    if (cx <= 1 && cy <= 1) {
        return false;                       /* nor right against it */
    }
    if (cx == s_z.goal_cx && cy == s_z.goal_cy) {
        return false;
    }
    for (int i = 0; i < s_z.holes; i++) {
        if (s_z.hole_x[i] == (int)(OX + cx * CELL + CELL / 2) &&
            s_z.hole_y[i] == (int)(OY + cy * CELL + CELL / 2)) {
            return false;
        }
    }
    return true;
}

static void reset_ball(void)
{
    s_z.x  = (float)OX + cell_center(0);
    s_z.y  = (float)OY + cell_center(0);
    s_z.vx = 0.0f;
    s_z.vy = 0.0f;
}

static void new_level(bool reset_run)
{
    if (reset_run) {
        s_z.level    = 1;
        s_z.falls    = 0;
        s_z.start_ms = lv_tick_get();
        s_z.elapsed_s = 0;
    }

    s_z.goal_cx = MW - 1;
    s_z.goal_cy = MH - 1;
    carve();

    /* One more pit per level, up to eight. The first is played clean. */
    s_z.holes = s_z.level - 1;
    if (s_z.holes > MAX_HOLES) {
        s_z.holes = MAX_HOLES;
    }
    int puestos = 0, intentos = 0;
    while (puestos < s_z.holes && intentos < 400) {
        intentos++;
        int cx = (int)(rnd() % MW), cy = (int)(rnd() % MH);
        if (!hole_free(cx, cy)) {
            continue;
        }
        s_z.hole_x[puestos] = OX + cx * CELL + CELL / 2;
        s_z.hole_y[puestos] = OY + cy * CELL + CELL / 2;
        puestos++;
    }
    s_z.holes = puestos;

    build_background();
    reset_ball();
    s_z.won = false;
    if (s_z.autoplay) {
        solve();
    }
}

/* --------------------------------------------------------------------------
 * MAZE_AUTO: it plays itself
 *
 * It is well worth it and costs little: with this the simulator is left
 * running for half an hour and you see whether the maze always gets finished,
 * whether the ball gets stuck in some corner and whether the physics holds up
 * over thousands of consecutive frames. It is the same idea as GEMAS_AUTO.
 *
 * A BFS from the exit leaves, for each cell, which neighbour brings it closer;
 * after that the "player" only pushes towards the centre of that neighbour.
 * -------------------------------------------------------------------------- */

static void solve(void)
{
    static int16_t queue[MW * MH];
    int head = 0, tail = 0;

    memset(s_z.next_cell, 0xFF, sizeof(s_z.next_cell));
    int goal = s_z.goal_cy * MW + s_z.goal_cx;
    s_z.next_cell[goal] = (uint8_t)goal;        /* the exit points at itself */
    queue[tail++] = (int16_t)goal;

    while (head < tail) {
        int c  = queue[head++];
        int cx = c % MW, cy = c / MW;
        uint8_t w = s_z.wall[c];

        /* We walk backwards: the still unmarked neighbour learns that to get
         * closer to the exit it has to come towards 'c'. */
        int nb[4], n = 0;
        if (!(w & WN)  && cy > 0)      nb[n++] = c - MW;
        if (!(w & WE)  && cx < MW - 1) nb[n++] = c + 1;
        if (!(w & WS)  && cy < MH - 1) nb[n++] = c + MW;
        if (!(w & WWW) && cx > 0)      nb[n++] = c - 1;

        for (int i = 0; i < n; i++) {
            if (s_z.next_cell[nb[i]] == 0xFF) {
                s_z.next_cell[nb[i]] = (uint8_t)c;
                queue[tail++] = (int16_t)nb[i];
            }
        }
    }
}

static void autoplay_step(void)
{
    int cx = ((int)s_z.x - OX) / CELL;
    int cy = ((int)s_z.y - OY) / CELL;
    if (cx < 0) cx = 0;
    if (cx >= MW) cx = MW - 1;
    if (cy < 0) cy = 0;
    if (cy >= MH) cy = MH - 1;

    int c = cy * MW + cx;
    int t = s_z.next_cell[c];
    if (t == 0xFF) {
        t = c;
    }
    float tx = (float)OX + cell_center(t % MW);
    float ty = (float)OY + cell_center(t / MW);

    float dx = tx - s_z.x, dy = ty - s_z.y;
    float d  = sqrtf(dx * dx + dy * dy);
    if (d < 0.5f) {
        s_z.ax = s_z.ay = 0.0f;
        return;
    }
    s_z.ax = dx / d * 0.45f;
    s_z.ay = dy / d * 0.45f;
}

/* -------------------------------------------------------------------------- */
/* Physics                                                                     */

static bool blocked(float fx, float fy)
{
    int x0 = (int)floorf(fx) - BALL_R;
    int y0 = (int)floorf(fy) - BALL_R;
    for (int j = y0; j <= y0 + 2 * BALL_R; j++) {
        if (j < 0 || j >= LH) {
            return true;
        }
        const uint8_t *row = s_z.solid + (size_t)j * LW;
        for (int i = x0; i <= x0 + 2 * BALL_R; i++) {
            if (i < 0 || i >= LW || row[i]) {
                return true;
            }
        }
    }
    return false;
}

/* Advances one axis in steps of at most one pixel. With larger steps the ball
 * goes through a 2 px wall when it moves fast. */
static void move_axis(float d, bool horizontal)
{
    int steps = (int)ceilf(fabsf(d));
    if (steps <= 0) {
        return;
    }
    float step = d / (float)steps;

    for (int k = 0; k < steps; k++) {
        float nx = s_z.x + (horizontal ? step : 0.0f);
        float ny = s_z.y + (horizontal ? 0.0f : step);
        if (blocked(nx, ny)) {
            if (horizontal) {
                s_z.vx = -s_z.vx * BOUNCE;
            } else {
                s_z.vy = -s_z.vy * BOUNCE;
            }
            return;
        }
        s_z.x = nx;
        s_z.y = ny;
    }
}

static void read_tilt(uint32_t now)
{
    if ((uint32_t)(now - s_z.last_imu_ms) < IMU_MS) {
        return;
    }
    s_z.last_imu_ms = now;

    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) {
        return;
    }
    float a = imu.ax - s_z.zero_a;
    float b = imu.ay - s_z.zero_b;

    /* The four mappings. Number 0 is the one that comes out of the measurement
     * of the board's axes: +ax down the screen, the right at -ay. */
    switch (s_z.axes) {
    case 1:  s_z.ax =  b; s_z.ay = -a; break;
    case 2:  s_z.ax =  a; s_z.ay =  b; break;
    case 3:  s_z.ax = -a; s_z.ay = -b; break;
    default: s_z.ax = -b; s_z.ay =  a; break;
    }
    if (fabsf(s_z.ax) < DEADZONE) s_z.ax = 0.0f;
    if (fabsf(s_z.ay) < DEADZONE) s_z.ay = 0.0f;
}

static void fall_in_hole(void)
{
    s_z.falls++;
    aos_hal_beep(180, 120);
    aos_hal_beep(120, 160);
    reset_ball();
}

static void reach_goal(void)
{
    s_z.won          = true;
    s_z.won_until_ms = lv_tick_get() + 600;
    aos_hal_beep(880, 70);
    aos_hal_beep(1180, 110);

    if (s_z.level > s_z.best_level) {
        s_z.best_level = s_z.level;
        aos_hal_pref_set_i32("maze_best", s_z.best_level);
    }
}

/* -------------------------------------------------------------------------- */
/* Score                                                                       */

/* The label lives OUTSIDE the canvas, so every write is a separate invalid
 * area with its background and its glyphs: measured in the Life app,
 * refreshing it on every frame is 12 of the frame's 47 ms. Three times a
 * second is enough. */
static void refresh_info(bool force)
{
    uint32_t now = lv_tick_get();
    if (!force && (uint32_t)(now - s_z.last_info_ms) < 350u) {
        return;
    }
    s_z.last_info_ms = now;

    char buf[64];
    snprintf(buf, sizeof(buf), _("nivel %d   %u:%02u   caidas %d   mejor %d"),
             s_z.level, (unsigned)(s_z.elapsed_s / 60),
             (unsigned)(s_z.elapsed_s % 60), s_z.falls, s_z.best_level);
    lv_label_set_text(s_z.lbl_info, buf);
}

/* -------------------------------------------------------------------------- */
/* Frame                                                                       */

static void draw_ball(int *x0, int *y0, int *x1, int *y1)
{
    int bx = (int)floorf(s_z.x);
    int by = (int)floorf(s_z.y);
    disc(s_z.fb, bx, by, BALL_R, C_BALL);
    /* The highlight is the only thing that makes it look like a sphere and not
     * a dot. */
    s_z.fb[(size_t)(by - 1) * LW + (bx - 1)] = C_SHINE;

    *x0 = bx - BALL_R - 1;
    *y0 = by - BALL_R - 1;
    *x1 = bx + BALL_R + 2;
    *y1 = by + BALL_R + 2;
}

static void restore(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > LW) x1 = LW;
    if (y1 > LH) y1 = LH;
    int span = x1 - x0;
    if (span <= 0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        size_t off = (size_t)y * LW + x0;
        memcpy(s_z.fb + off, s_z.bg + off, (size_t)span * 2);
    }
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (s_z.won) {
        if ((int32_t)(now - s_z.won_until_ms) < 0) {
            return;
        }
        s_z.level++;
        new_level(false);
        push_all();
        refresh_info(true);
        return;
    }

    if (s_z.autoplay) {
        autoplay_step();
    } else {
        read_tilt(now);
    }

    s_z.vx = (s_z.vx + s_z.ax * GAIN) * DAMP;
    s_z.vy = (s_z.vy + s_z.ay * GAIN) * DAMP;
    if (s_z.vx >  VMAX) s_z.vx =  VMAX;
    if (s_z.vx < -VMAX) s_z.vx = -VMAX;
    if (s_z.vy >  VMAX) s_z.vy =  VMAX;
    if (s_z.vy < -VMAX) s_z.vy = -VMAX;

    move_axis(s_z.vx, true);
    move_axis(s_z.vy, false);

    /* Restore the previous frame's and draw the new one. */
    int nx0, ny0, nx1, ny1;
    if (s_z.prev_valid) {
        restore(s_z.prev_x0, s_z.prev_y0, s_z.prev_x1, s_z.prev_y1);
    }
    draw_ball(&nx0, &ny0, &nx1, &ny1);

    if (s_z.prev_valid) {
        push(nx0 < s_z.prev_x0 ? nx0 : s_z.prev_x0,
             ny0 < s_z.prev_y0 ? ny0 : s_z.prev_y0,
             nx1 > s_z.prev_x1 ? nx1 : s_z.prev_x1,
             ny1 > s_z.prev_y1 ? ny1 : s_z.prev_y1);
    } else {
        push(nx0, ny0, nx1, ny1);
    }
    s_z.prev_x0 = nx0; s_z.prev_y0 = ny0;
    s_z.prev_x1 = nx1; s_z.prev_y1 = ny1;
    s_z.prev_valid = true;

    /* Pits and exit, after moving. */
    for (int i = 0; i < s_z.holes; i++) {
        float dx = s_z.x - (float)s_z.hole_x[i];
        float dy = s_z.y - (float)s_z.hole_y[i];
        if (dx * dx + dy * dy < (float)(HOLE_R * HOLE_R)) {
            restore(s_z.prev_x0, s_z.prev_y0, s_z.prev_x1, s_z.prev_y1);
            push(s_z.prev_x0, s_z.prev_y0, s_z.prev_x1, s_z.prev_y1);
            s_z.prev_valid = false;
            fall_in_hole();
            break;
        }
    }
    if (!s_z.won) {
        int gx = OX + s_z.goal_cx * CELL + CELL / 2;
        int gy = OY + s_z.goal_cy * CELL + CELL / 2;
        float dx = s_z.x - (float)gx, dy = s_z.y - (float)gy;
        if (dx * dx + dy * dy < 20.0f) {
            reach_goal();
        }
    }

    uint32_t s = (now - s_z.start_ms) / 1000u;
    if (s != s_z.elapsed_s && s < 5999) {
        s_z.elapsed_s = s;
    }
    refresh_info(false);
}

/* -------------------------------------------------------------------------- */
/* Controls                                                                    */

static void zero_cb(lv_event_t *event)
{
    (void)event;
    aos_imu_t imu;
    if (aos_hal_imu_read(&imu)) {
        s_z.zero_a = imu.ax;
        s_z.zero_b = imu.ay;
        aos_ui_toast(_("Inclinacion a cero"), 1000);
        aos_hal_beep(1500, 25);
    }
}

/* Holding ZERO down cycles the axis mapping. It is the escape valve for the
 * day the board is in hand and the ball goes the wrong way: the mapping is
 * measured, but measured is not the same as tested. */
static void axes_cb(lv_event_t *event)
{
    (void)event;
    s_z.axes = (s_z.axes + 1) & 3;
    aos_hal_pref_set_i32("maze_axes", s_z.axes);

    char buf[24];
    snprintf(buf, sizeof(buf), "ejes %d", s_z.axes);
    lv_label_set_text(s_z.lbl_axes, buf);
    aos_ui_toast(buf, 900);
    aos_hal_beep(700, 40);
}

static void new_cb(lv_event_t *event)
{
    (void)event;
    new_level(true);
    push_all();
    refresh_info(true);
    aos_hal_beep(600, 30);
}

/* -------------------------------------------------------------------------- */
/* Construction                                                                */

static lv_obj_t *bar_button(lv_obj_t *parent, const char *text, int32_t x,
                            int32_t w, lv_color_t color, lv_event_cb_t cb,
                            lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, 34);
    lv_obj_set_pos(btn, x, 26);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = aos_label(btn, text, aos_font_small, AOS_C_TEXT);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(label);
    if (out_label) {
        *out_label = label;
    }
    return btn;
}

static void *create(aos_app_t *self, lv_obj_t *root)
{
    memset(&s_z, 0, sizeof(s_z));
    s_z.self = self;
    s_z.rng  = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    colors_init();

    /* Development switches. The first character is inspected and not just the
     * pointer: an exported empty variable returns "" and not NULL. */
    {
        const char *a = getenv("MAZE_AUTO");
        s_z.autoplay = (a && a[0] && a[0] != '0');
    }

    int32_t v = 0;
    if (aos_hal_pref_get_i32("maze_best", &v) && v > 0) {
        s_z.best_level = (int)v;
    }
    if (aos_hal_pref_get_i32("maze_axes", &v) && v >= 0 && v < 4) {
        s_z.axes = (int)v;
    }

    /* The large ones through malloc(), which with CONFIG_SPIRAM_USE_MALLOC go
     * to PSRAM. What is scarce is internal RAM and none of it is used here. */
    s_z.big   = malloc((size_t)DW * DH * 2);
    s_z.fb    = malloc((size_t)LW * LH * 2);
    s_z.bg    = malloc((size_t)LW * LH * 2);
    s_z.solid = malloc((size_t)LW * LH);
    if (!s_z.big || !s_z.fb || !s_z.bg || !s_z.solid) {
        free(s_z.big);
        free(s_z.fb);
        free(s_z.bg);
        free(s_z.solid);
        memset(&s_z, 0, sizeof(s_z));
        aos_ui_toast(_("Sin memoria"), 2000);
        return NULL;
    }

    lv_obj_t *page = aos_page(root);

    s_z.canvas = lv_canvas_create(page);
    lv_canvas_set_buffer(s_z.canvas, s_z.big, DW, DH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_z.canvas, DW, DH);
    lv_obj_set_pos(s_z.canvas, 0, CANVAS_Y);
    /* The theme gives everything a radius, and on a canvas that forces
     * clipping with a mask and drawing in layers. */
    lv_obj_set_style_radius(s_z.canvas, 0, 0);
    lv_image_set_antialias(s_z.canvas, false);
    lv_obj_remove_flag(s_z.canvas, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bar = lv_obj_create(page);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, AOS_SCREEN_W, BAR_H);
    lv_obj_set_pos(bar, 0, BAR_Y);
    lv_obj_set_style_bg_color(bar, AOS_C_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    s_z.lbl_info = aos_label_boxed(bar, "", aos_font_small, AOS_C_DIM,
                                   AOS_SCREEN_W, 20);
    lv_obj_align(s_z.lbl_info, LV_ALIGN_TOP_MID, 0, 3);

    lv_obj_t *zero = bar_button(bar, _("CERO"), 40, 110, AOS_C_CARD2, zero_cb, NULL);
    lv_obj_add_event_cb(zero, axes_cb, LV_EVENT_LONG_PRESSED, NULL);
    bar_button(bar, _("NUEVO"), AOS_SCREEN_W - 40 - 110, 110, AOS_C_ACCENT,
               new_cb, NULL);

    /* A little indicator of the axis mapping, so you know which one you ended
     * up in without having to open the menu. */
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "ejes %d", s_z.axes);
        s_z.lbl_axes = aos_label(bar, buf, aos_font_small, AOS_C_DIM);
        /* In the gap left between the two buttons, which are 110 px each with
         * 40 of margin: 68 are left in the middle. */
        lv_obj_align(s_z.lbl_axes, LV_ALIGN_TOP_MID, 0, 33);
        lv_obj_remove_flag(s_z.lbl_axes, LV_OBJ_FLAG_CLICKABLE);
    }

    /* The tilt at startup is the zero: it is played holding the board however
     * you happen to hold it, not necessarily horizontal. */
    {
        aos_imu_t imu;
        if (aos_hal_imu_read(&imu)) {
            s_z.zero_a = imu.ax;
            s_z.zero_b = imu.ay;
        }
    }

    s_z.last_imu_ms  = lv_tick_get();
    s_z.last_info_ms = lv_tick_get();
    new_level(true);
    {
        const char *lvl = getenv("MAZE_LEVEL");
        if (lvl && lvl[0]) {
            s_z.level = atoi(lvl);
            if (s_z.level < 1) {
                s_z.level = 1;
            }
            new_level(false);
        }
    }
    push_all();
    refresh_info(true);

    s_z.timer = lv_timer_create(frame_cb, FRAME_MS, NULL);
    return &s_z;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)inst;
    if (s_z.timer) {
        lv_timer_delete(s_z.timer);
        s_z.timer = NULL;
    }
    /* The objects first, with the context still standing: the canvas points at
     * a buffer we are about to free. */
    if (self && self->root) {
        lv_obj_clean(self->root);
    }
    free(s_z.big);
    free(s_z.fb);
    free(s_z.bg);
    free(s_z.solid);
    memset(&s_z, 0, sizeof(s_z));
}

static bool laberinto_init(aos_app_t *app)
{
    app->desc.id       = "aos.maze";
    app->desc.name     = "Laberinto";
    app->desc.icon     = "Lb";
    app->desc.icon_vec = AOS_ICON_MAZE;
    app->desc.color_a  = 0x8E8E93;
    app->desc.color_b  = 0x2C2C2E;
    app->desc.flags    = AOS_APP_FLAG_FULLSCREEN | AOS_APP_FLAG_KEEP_AWAKE;
    app->desc.order    = 156;

    app->create        = create;
    app->destroy       = destroy;
    return true;
}

AOS_APP_ENTRY(laberinto_init);
