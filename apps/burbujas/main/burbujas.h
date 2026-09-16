/*
 * BURBUJAS - game state
 *
 * A bubble shooter. A hexagonal board hangs from the ceiling, the launcher at
 * the bottom throws bubbles at it, and three or more of a colour that end up
 * touching burst; whatever is left hanging from nothing falls with them.
 *
 * Three modes:
 *
 *   CLASSIC    endless. Every few shots that burst nothing, a new row comes
 *              in from the top and the board gets closer to the line.
 *   LEVELS     a board to clear. The ceiling comes DOWN instead, and the
 *              level you reach is kept.
 *   TIMED      two minutes. The rows come by the clock, not by your misses.
 *
 * The file layout is Topos's, and for the same reason -the rules and the
 * drawing are tested without a screen (tools/bb_harness.c)-:
 *
 *   bb_pixel.c   pixel engine and dirty rectangles (from topos, via cjump)
 *   bb_art.c     the bubbles: one lit sphere map, a palette per colour
 *   bb_game.c    rules, aiming, physics and levels. Neither LVGL nor the HAL.
 *   bb_draw.c    backdrop, board, and the compositor that repaints only what
 *                changed
 *   burbujas.c   the app: LVGL screens, preferences, touch, timer, sound
 *
 * THE ONLY TEXT ON THE CANVAS IS NUMBERS AND SIGNS. The 5x7 font has no
 * accents; every word is an LVGL label wrapped in _(). That is Claude Jump's
 * rule and it is what keeps the catalogues free of transliterations.
 */
#pragma once

#include "bb_pixel.h"

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Geometry, in pixels of the 184x224 buffer (the screen is twice that)
 *
 * Eight columns of 22 px with the odd rows offset by half a bubble, which is
 * the layout of the photo this was drawn from. 19 px between rows is the
 * height of a hexagonal row, 22 * sqrt(3) / 2, rounded.
 * -------------------------------------------------------------------------- */

#define BB_HUD_H        24              /* the score's strip                  */
#define BB_WALL_L       4               /* the walls the shot bounces off     */
#define BB_WALL_R       180
#define BB_D            22              /* a bubble                           */
#define BB_R            11
#define BB_ROW_H        19
#define BB_COLS         8               /* an offset row holds one less       */
#define BB_ROWS         10              /* rows the board can hold at all     */
#define BB_CEIL_Y       BB_HUD_H        /* where row 0 hangs from             */

/* The line. A bubble whose bottom edge passes it loses the game; at y=184 of
 * the buffer it is 368 of the screen, and it leaves room below for the
 * launcher without stealing a row from the board. */
#define BB_DEAD_Y       184

#define BB_LAUNCH_X     92
#define BB_LAUNCH_Y     204
#define BB_PIPE_X       36              /* the next bubble, at the pipe mouth */
#define BB_PIPE_Y       206
#define BB_PIPE_HIDDEN  12              /* where it comes out from            */

/* A press below this swaps the two bubbles instead of aiming. The touch panel
 * is comfortable down to y=390 of the screen and a finger against the rim
 * lands on 410, so this strip is reachable; it is also the only part of the
 * screen that is NOT the board. */
#define BB_SWAP_Y       (BB_DEAD_Y + 2)

/* Ray positions and directions, in 1/1024 px. A direction is a unit vector of
 * that same 1024, so one step of the trace advances exactly one pixel. */
#define BB_FP           1024
#define BB_COLL         18              /* centre distance at which it sticks */
#define BB_SPEED        330             /* px per second                      */
#define BB_MIN_TAN      275             /* 15 degrees, in 1/1024: shallower
                                           than this and a shot bounces for
                                           whole seconds before it arrives    */
#define BB_RELOAD_MS    180
#define BB_SLIDE_MS     260             /* a row coming in / the ceiling down */

#define BB_MAX_DOTS     40              /* the aiming guide                   */
#define BB_DOT_GAP      9
#define BB_MAX_FX       56
#define BB_MAX_POPUPS   6
#define BB_TITLE_RINGS  5

/* --------------------------------------------------------------------------
 * Colours
 *
 * 1..6 are what a cell can hold. 7 and 8 only ever exist in the launcher: the
 * rainbow one takes the colour of whatever it touches, the bomb clears a
 * patch. 9 is the board on the way out, when the game is lost.
 * -------------------------------------------------------------------------- */
enum {
    BC_NONE = 0,
    BC_RED, BC_ORANGE, BC_YELLOW, BC_GREEN, BC_BLUE, BC_PURPLE,
    BC_RAINBOW, BC_BOMB, BC_GREY,
    BB_PALS
};
#define BB_NCOLORS      6

/* --------------------------------------------------------------------------
 * Effects: everything that is neither the board nor the launcher
 * -------------------------------------------------------------------------- */
enum { FX_NONE = 0, FX_POP, FX_FALL, FX_BLAST };

typedef struct {
    uint8_t  kind;
    uint8_t  color;
    uint8_t  seed;
    uint8_t  pad;
    int16_t  delay;         /* ms before it starts; until then it is a bubble */
    uint16_t t;             /* ms since it started                           */
    int32_t  x, y;          /* 1/16 px                                        */
    int16_t  vx, vy;        /* falls: 1/16 px per second                      */
} bb_fx_t;

typedef struct {
    uint8_t  on;
    uint8_t  color;
    uint16_t value;
    int16_t  x, y;
    uint16_t t;
} bb_popup_t;

/* --------------------------------------------------------------------------
 * Drawing slots
 *
 * Topos's contract, unchanged: everything that moves is a slot, a slot is
 * drawn from its bb_dp_t and NOTHING ELSE, and if the parameters did not
 * change neither did the pixels. What changed is rebuilt from the background
 * plus every slot that touches it. The still board is not a slot: it lives in
 * the background, and a cell that changes marks its rectangle (bb_bgd).
 * -------------------------------------------------------------------------- */
enum {
    DK_NONE = 0,
    DK_LINE,            /* the death line, which blinks when it is close      */
    DK_GHOST,           /* where the shot would stick                         */
    DK_DOT,             /* one dot of the aiming guide                        */
    DK_BUBBLE,          /* the shot, the launcher's, a falling one            */
    DK_POP,             /* a bubble bursting                                  */
    DK_BLAST,           /* the bomb                                           */
    DK_POPUP,           /* "+120"                                             */
    DK_PTR,             /* the launcher's pointer                             */
    DK_NEXT,            /* the pipe and the bubble waiting in it              */
    DK_METER,           /* how many shots until the next row                  */
    DK_RING,            /* the title's rising bubbles                         */
};

typedef struct {
    uint8_t kind;
    uint8_t v[7];
    int16_t x, y;
    int16_t w0, w1;
} bb_dp_t;              /* 16 bytes, no padding: compared with memcmp         */

typedef struct {
    bb_dp_t   p;
    bb_rect_t box;
    uint8_t   vis;
} bb_slot_t;

#define BB_SLOT_LINE    0
#define BB_SLOT_GHOST   1
#define BB_SLOT_DOT0    2
#define BB_SLOT_FX0     (BB_SLOT_DOT0 + BB_MAX_DOTS)
#define BB_SLOT_SHOT    (BB_SLOT_FX0 + BB_MAX_FX)
#define BB_SLOT_PTR     (BB_SLOT_SHOT + 1)
#define BB_SLOT_CUR     (BB_SLOT_PTR + 1)
#define BB_SLOT_NEXT    (BB_SLOT_CUR + 1)
#define BB_SLOT_METER   (BB_SLOT_NEXT + 1)
#define BB_SLOT_POP0    (BB_SLOT_METER + 1)
#define BB_SLOT_RING0   (BB_SLOT_POP0 + BB_MAX_POPUPS)
#define BB_SLOTS        (BB_SLOT_RING0 + BB_TITLE_RINGS)

/* --------------------------------------------------------------------------
 * The whole state
 * -------------------------------------------------------------------------- */

enum { MODE_CLASSIC = 0, MODE_LEVELS, MODE_TIMED, BB_MODES };

enum {
    GS_TITLE = 0,
    GS_PLAY,
    GS_ENDING,          /* lost, cleared or time up: it settles, no input     */
    GS_OVER,
};

enum {
    PS_AIM = 0,         /* the board is still: you may shoot                  */
    PS_FLY,             /* a bubble is in the air                             */
    PS_PUSH,            /* a row is coming in, or the ceiling coming down     */
};

/* What the app has to react to (banners, sounds). The game ORs them in and
 * the app clears them after reading. */
enum {
    EV_GO      = 1u << 0,
    EV_LEVEL   = 1u << 1,   /* levels: the level starts                       */
    EV_PUSH    = 1u << 2,   /* a row came in / the ceiling came down          */
    EV_CLEAN   = 1u << 3,   /* the board was cleared (endless modes)          */
    EV_SPECIAL = 1u << 4,   /* a bomb or a rainbow was earned                 */
    EV_COLOR   = 1u << 5,   /* classic: one more colour from now on           */
    EV_HURRY   = 1u << 6,   /* timed: ten seconds left                        */
    EV_TIMEUP  = 1u << 7,
    EV_WIN     = 1u << 8,   /* levels: board cleared                          */
    EV_LOST    = 1u << 9,
    EV_OVER    = 1u << 10,  /* the result panel can go up                     */
};

typedef struct {
    int32_t x, y;           /* 1/1024 px                                      */
    int32_t vx, vy;         /* 1/1024 px, |v| = 1024: one step, one pixel     */
} bb_ray_t;

enum { RAY_FLY = 0, RAY_BOUNCE, RAY_BUBBLE, RAY_CEIL };

typedef struct {
    bb_buf_t   fb, bg;
    uint32_t   rng;

    uint8_t    mode;
    uint8_t    state;
    uint8_t    sub;
    uint8_t    won;             /* ending: cleared or ran out of time         */

    /* the board */
    uint8_t    cell[BB_ROWS][BB_COLS];
    uint8_t    parity;          /* is row 0 the offset one?                   */
    int16_t    board_top;       /* y of the top of row 0                      */
    int16_t    slide;           /* drawing offset while it slides (<= 0)      */
    uint16_t   slide_ms;
    uint8_t    drops;           /* levels: rows the ceiling has come down     */
    uint8_t    grey_from;       /* lost: rows from here down are grey         */
    uint8_t    danger;          /* something is one row from the line         */

    /* the launcher */
    uint8_t    cur, nxt;
    uint16_t   reload_ms;       /* > 0: the next one is sliding in            */
    uint8_t    special_turn;    /* which special comes next                   */

    /* aiming */
    uint8_t    touching;        /* 0 none, 1 aiming, 2 in the swap strip      */
    uint8_t    aiming;          /* the guide is up                            */
    uint8_t    aim_ok;          /* the finger is somewhere one can shoot at   */
    uint8_t    queued;          /* released while busy: shoot when ready      */
    uint16_t   queued_ms;
    int16_t    aim_vx, aim_vy;  /* unit vector, 1/1024                        */
    uint8_t    preview_dirty;

    /* the guide, recomputed only when the aim or the board changes */
    uint8_t    ndots;
    int16_t    dotx[BB_MAX_DOTS], doty[BB_MAX_DOTS];
    int8_t     ghost_r, ghost_c;

    /* the shot */
    bb_ray_t   shot;
    uint8_t    shot_color;
    uint8_t    shot_kind;       /* what it arrived at: RAY_BUBBLE / RAY_CEIL  */
    int8_t     shot_hit_r;      /* and which bubble, for the rainbow one      */
    int8_t     shot_hit_c;
    int32_t    shot_acc;        /* 1/1000 px left over from the last frame    */

    bb_fx_t    fx[BB_MAX_FX];
    bb_popup_t popup[BB_MAX_POPUPS];

    /* progress */
    uint32_t   score;
    uint32_t   best;            /* the record, for the strip. Set by the app  */
    uint16_t   level;           /* levels: the one being played               */
    uint16_t   shots, popped, dropped, best_drop;
    uint8_t    ncolors;
    uint8_t    misses, miss_limit;
    uint8_t    push_pending;
    uint16_t   push_wait;       /* ms before the pending row comes in         */
    uint16_t   rows_pushed;
    int32_t    time_ms;         /* timed: what is left                        */
    int32_t    push_ms;         /* timed: until the next row                  */
    int32_t    push_every;
    uint32_t   elapsed_ms;
    uint16_t   phase_ms;        /* ending                                     */
    uint8_t    hurried;
    uint16_t   events;

    /* drawing (bb_draw.c) */
    bb_slot_t  slot[BB_SLOTS], prev[BB_SLOTS];
    bb_dirty_t push;            /* rebuilt this frame: to be upscaled         */
    bb_dirty_t bgd;             /* the board changed here: rebuild the bg     */
    uint8_t    bg_all;          /* the whole background, next present         */
    uint8_t    full;            /* the whole field, next present              */
    uint8_t    hud_push;
    uint8_t    hud_valid;
    int16_t    hud_fps;
    struct {
        uint32_t score;
        uint32_t right;         /* seconds, level or record, by mode          */
        uint8_t  mode, red, danger;
    }          hud;
    uint32_t   title_t;

    uint8_t    autoplay;        /* the bot plays (BB_AUTO=1, and the harness) */
    uint8_t    bot_phase;
    uint16_t   bot_ms;
    uint8_t    show_fps;
    int16_t    fps10;
    uint16_t   last_area;
    uint8_t    last_rects;
} bb_game_t;

/* --------------------------------------------------------------------------
 * The hexagonal grid
 *
 * A row is offset -half a bubble to the right, one cell less- when (r +
 * parity) is odd. A row coming in from the top flips the parity, which is
 * what makes the new row interlock with the one that was there.
 * -------------------------------------------------------------------------- */

static inline int bb_indent(const bb_game_t *g, int r)
{
    return (r + g->parity) & 1;
}

static inline int bb_ncols(const bb_game_t *g, int r)
{
    return bb_indent(g, r) ? BB_COLS - 1 : BB_COLS;
}

static inline int bb_cell_x(const bb_game_t *g, int r, int c)
{
    return BB_WALL_L + BB_R + c * BB_D + (bb_indent(g, r) ? BB_R : 0);
}

static inline int bb_cell_y(const bb_game_t *g, int r)
{
    return g->board_top + BB_R + r * BB_ROW_H;
}

static inline bool bb_valid(const bb_game_t *g, int r, int c)
{
    return r >= 0 && r < BB_ROWS && c >= 0 && c < bb_ncols(g, r);
}

/* Defined in burbujas.c, which is the only file that sees the HAL. It honours
 * the sound switch. The harness defines a silent one. */
void bb_sfx(int freq_hz, int ms);

/* --------------------------------------------------------------------------
 * bb_game.c
 * -------------------------------------------------------------------------- */

void     bb_game_init(bb_game_t *g, uint32_t seed);
void     bb_game_start(bb_game_t *g, int mode, int level);
void     bb_game_step(bb_game_t *g, int dt_ms);

/* Touch, in buffer pixels. A press in the bottom strip swaps; anywhere else
 * it aims, and the release shoots. */
void     bb_game_press(bb_game_t *g, int x, int y);
void     bb_game_drag(bb_game_t *g, int x, int y);
void     bb_game_release(bb_game_t *g, int x, int y);
void     bb_game_cancel(bb_game_t *g);
void     bb_game_swap(bb_game_t *g);

/* the bot: aims where a decent player would, and misses now and then */
void     bb_game_bot(bb_game_t *g, int dt_ms);

uint32_t bb_rand(bb_game_t *g);
int      bb_rand_range(bb_game_t *g, int lo, int hi);

/* One step of one pixel along the ray, walls included. Returns RAY_*. The
 * guide, the flight and the bot all go through here, which is what makes the
 * dotted line agree with where the bubble ends up. */
int      bb_ray_step(const bb_game_t *g, bb_ray_t *ray, int *hit_r, int *hit_c);

/* The empty cell a shot that stopped at (x, y) sticks to: the nearest one
 * that hangs from the ceiling or from another bubble. */
bool     bb_snap_cell(const bb_game_t *g, int32_t x, int32_t y, int *r, int *c);

/* --------------------------------------------------------------------------
 * bb_draw.c
 * -------------------------------------------------------------------------- */

/* The backdrop and the board (or the title's scene) into bg, and bg into fb. */
void bb_bg_build(bb_game_t *g, bool title);
/* One frame: rebuilds what changed into fb and leaves in g->push what has to
 * be upscaled (plus g->hud_push for the score's strip). */
void bb_present(bb_game_t *g);
/* The slow path: bg plus every visible slot, drawn from scratch into out.
 * The harness compares it with fb after every present(). */
void bb_render_full(bb_game_t *g, uint16_t *out);
/* The box a slot's parameters produce, and drawing it on any buffer. For the
 * harness's box check. */
void bb_slot_box(const bb_dp_t *p, bb_rect_t *out, bool *any);
void bb_slot_paint(bb_buf_t *b, const bb_dp_t *p);
