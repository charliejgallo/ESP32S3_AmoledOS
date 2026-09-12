/*
 * TOPOS - game state
 *
 * A whack-a-mole. Nine holes in a lawn; moles pop up and you tap them. A mole
 * in a hard hat takes two taps (the first knocks the hat off), a golden one
 * is worth a lot, and a bomb must NOT be tapped.
 *
 * Three modes, all with a difficulty that climbs as you play:
 *
 *   CLASSIC    60 seconds, points and combos.
 *   SURVIVAL   three hearts. A mole that gets away or a bomb costs one;
 *              every eight moles is a level.
 *   FRENZY     30 seconds starting fast, several moles at once, and a combo
 *              multiplier that reaches x5.
 *
 * The file layout is Claude Jump's, and for the same reason -the rules and
 * the drawing are tested without a screen (tools/tp_harness.c)-:
 *
 *   tp_pixel.c   pixel engine and dirty rectangles (from cjump, plus a lip)
 *   tp_art.c     the sprites, rendered once when the app opens
 *   tp_game.c    rules, spawning and difficulty. Neither LVGL nor the HAL.
 *   tp_draw.c    lawn, holes, and the compositor that repaints only what
 *                changed
 *   topos.c      the app: LVGL screens, preferences, touch, timer, sound
 *
 * THE ONLY TEXT ON THE CANVAS IS NUMBERS AND SIGNS. The 5x7 font has no
 * accents; every word is an LVGL label. That is Claude Jump's rule and it is
 * what keeps the catalogues free of transliterations.
 */
#pragma once

#include "tp_pixel.h"

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Geometry, in pixels of the 184x224 buffer (the screen is twice that)
 * -------------------------------------------------------------------------- */

#define TP_HUD_H        30              /* the score's strip                  */
#define TP_COLS         3
#define TP_ROWS         3
#define TP_HOLES        (TP_COLS * TP_ROWS)
#define TP_COL_X(c)     (34 + (c) * 58)
#define TP_ROW_Y(r)     (82 + (r) * 50)

/* The bottom row of holes ends at y=191 of the buffer, 382 of the screen:
 * inside the touch panel's comfortable window (56..390, see aos_hal.h). */

#define TP_MOUND_RX     25              /* the dirt ring                      */
#define TP_MOUND_RY     10
#define TP_OPEN_RX      17              /* the opening the moles come out of  */
#define TP_OPEN_RY      6

/* The cell a tap is judged against: generous, and the nine tile the field. */
#define TP_CELL_HW      29
#define TP_CELL_UP      44
#define TP_CELL_DN      10

/* How far an occupant travels from hidden to fully up. Hidden means that not
 * even the tuft or the spark peeks over the front lip. */
#define TP_MOLE_RISE    40
#define TP_BOMB_RISE    41

/* How long a whacked mole stays dizzy before sinking. tp_draw.c needs it too:
 * the stars keep turning from where they were when it starts to sink. */
#define TP_HIT_MS       430

/* Where the head top of a mole is, for a given rise (0..TP_MOLE_RISE) */
#define TP_HEAD_TOP(cy, rise)   ((cy) + 10 - (rise))
/* and the centre of a bomb */
#define TP_BOMB_CY(cy, rise)    ((cy) + 33 - (rise))

/* --------------------------------------------------------------------------
 * Holes
 * -------------------------------------------------------------------------- */

enum {
    OCC_NONE = 0,
    OCC_MOLE,
    OCC_HELMET,         /* two taps: the first knocks the hat off           */
    OCC_GOLD,           /* worth a lot, and a bonus that depends on the mode */
    OCC_BOMB,           /* do not tap                                        */
};

enum {
    PH_EMPTY = 0,
    PH_RISE,            /* coming out                                        */
    PH_UP,              /* waiting; a bomb burns its fuse here               */
    PH_TAUNT,           /* nobody hit it: it sticks its tongue out. Still
                           hittable: it is the last chance                   */
    PH_SINK,            /* going back in                                     */
    PH_BONK,            /* the hat just flew off: startled for a moment      */
    PH_HIT,             /* whacked: dizzy, then it sinks                     */
    PH_FIZZLE,          /* a bomb whose fuse ran out: a puff and it sinks    */
    PH_COOL,            /* empty, resting before anything comes out again    */
};

typedef struct {
    uint8_t  occ;
    uint8_t  phase;
    uint8_t  helmet;        /* the hat is still on                           */
    uint8_t  seed;          /* per-appearance variety (idle glances)         */
    uint8_t  was_hit;       /* SINK after HIT keeps the dizzy face           */
    int8_t   rise;          /* px out of the hole                            */
    int8_t   sink_from;     /* rise when it started sinking                  */
    uint8_t  pad;
    uint16_t t;             /* ms in the current phase                       */
    uint16_t up_t;          /* ms spent up, across a BONK                    */
    uint16_t up_ms;         /* how long it stays up before taunting          */
    uint16_t rise_ms;
    uint16_t fuse_t;        /* bombs                                         */
    uint16_t fuse_ms;
    uint16_t cool_ms;
} tp_hole_t;

/* --------------------------------------------------------------------------
 * Effects: everything that is not an occupant and not the mallet
 * -------------------------------------------------------------------------- */

enum {
    FX_NONE = 0,
    FX_HELMET,          /* the hard hat flying off, spinning                 */
    FX_BOOM,            /* a bomb going off                                  */
    FX_SMOKE,           /* a fizzled fuse's puff                             */
    FX_POPUP,           /* "+20", "X3", "+3S"                                */
    FX_RAYS,            /* the impact of a whack                             */
    FX_CLANG,           /* the mallet on the hard hat                        */
    FX_DUST,            /* a tap on nothing                                  */
    FX_CRUMBS,          /* dirt thrown up by something coming out            */
};

enum { POP_NUM = 0, POP_MULT, POP_SECS };
enum { POPC_WHITE = 0, POPC_GOLD, POPC_RED, POPC_ORANGE, POPC_CYAN };

typedef struct {
    uint8_t  kind;
    uint8_t  seed;
    uint8_t  c;             /* popup: colour; others: free                   */
    uint8_t  type;          /* popup: POP_*                                  */
    uint16_t t;             /* ms alive                                      */
    int16_t  x, y;          /* px; FX_HELMET in 1/16 px                      */
    int16_t  vx, vy;        /* FX_HELMET: 1/16 px per 33 ms                  */
    int16_t  a;             /* popup: value; FX_HELMET: angle in brads       */
} tp_fx_t;

#define TP_MAX_FX       16

/* --------------------------------------------------------------------------
 * Drawing slots
 *
 * Everything on the field is a slot, and a slot is drawn from its tp_dp_t and
 * NOTHING ELSE. That is the contract the compositor rests on: if a slot's
 * parameters did not change, its pixels did not change, so it costs nothing;
 * if they did, the union of its old and new boxes is rebuilt from the lawn
 * plus every slot touching it. The box is measured by running the same
 * drawing code with no buffer, so it cannot disagree with what gets drawn.
 * -------------------------------------------------------------------------- */

enum {
    DK_NONE = 0,
    DK_MOLE,
    DK_BOMB,
    DK_FX_HELMET,
    DK_FX_BOOM,
    DK_FX_SMOKE,
    DK_FX_POPUP,
    DK_FX_RAYS,
    DK_FX_CLANG,
    DK_FX_DUST,
    DK_FX_CRUMBS,
    DK_MALLET,
    DK_TITLE_MOLE,
    DK_TITLE_BOMB,
};

typedef struct {
    uint8_t kind;
    uint8_t v[7];
    int16_t x, y;
    int16_t w0, w1;
} tp_dp_t;              /* 16 bytes, no padding: compared with memcmp        */

typedef struct {
    tp_dp_t   p;
    tp_rect_t box;
    uint8_t   vis;
} tp_slot_t;

#define TP_SLOT_HOLE0   0
#define TP_SLOT_FX0     (TP_SLOT_HOLE0 + TP_HOLES)
#define TP_SLOT_MALLET  (TP_SLOT_FX0 + TP_MAX_FX)
#define TP_SLOT_TITLE0  (TP_SLOT_MALLET + 1)
#define TP_SLOTS        (TP_SLOT_TITLE0 + 2)

/* --------------------------------------------------------------------------
 * The whole state
 * -------------------------------------------------------------------------- */

enum { MODE_CLASSIC = 0, MODE_SURVIVAL, MODE_FRENZY, TP_MODES };

enum {
    GS_TITLE = 0,
    GS_COUNT,           /* 3, 2, 1...                                        */
    GS_PLAY,
    GS_ENDING,          /* time up or no hearts: things settle, no input     */
    GS_OVER,
};

/* Things the app has to react to (banners, sounds). The game ORs them in and
 * the app clears them after reading. */
enum {
    EV_COUNT  = 1u << 0,    /* count_num changed (3, 2, 1)                   */
    EV_GO     = 1u << 1,
    EV_LEVEL  = 1u << 2,    /* survival: a new level                        */
    EV_TIMEUP = 1u << 3,
    EV_DEAD   = 1u << 4,    /* survival: no hearts left                     */
    EV_OVER   = 1u << 5,    /* the game over panel can go up                */
    EV_HURRY  = 1u << 6,    /* timed modes: ten seconds left                */
    EV_HEART  = 1u << 7,    /* survival: a golden mole gave a heart back    */
};

#define TP_LIVES        3

typedef struct {
    tp_buf_t   fb, bg;
    uint32_t   rng;

    uint8_t    mode;
    uint8_t    state;
    tp_hole_t  holes[TP_HOLES];
    tp_fx_t    fx[TP_MAX_FX];

    /* the mallet: only the last tap's */
    int16_t    mx, my;
    uint16_t   mt;
    uint8_t    mallet;          /* 0 none, 1 on something, 2 on nothing      */

    /* progress */
    uint32_t   score;
    uint16_t   hits, streak, best_streak;
    uint16_t   bombs, escaped, helmets, golds;
    uint8_t    mult;
    uint8_t    lives;
    uint8_t    level;
    uint8_t    count_num;
    int32_t    time_ms;         /* timed modes: what is left                 */
    uint32_t   elapsed_ms;
    int32_t    spawn_ms;        /* until the next spawn                      */
    uint16_t   stun_ms;         /* after a bomb the mallet does not work     */
    uint16_t   phase_ms;        /* countdown and ending                      */
    uint16_t   flash_ms;        /* the HUD blinks red after a loss           */
    int8_t     last_hole;
    uint16_t   events;

    /* drawing (tp_draw.c) */
    tp_slot_t  slot[TP_SLOTS], prev[TP_SLOTS];
    tp_dirty_t push;            /* rebuilt this frame: to be upscaled        */
    uint8_t    full;            /* next present repaints the whole field     */
    uint8_t    hud_push;        /* present() redrew the HUD: push it too     */
    uint8_t    hud_valid;
    int16_t    hud_fps;         /* the fps the strip shows, when shown       */
    struct {
        uint32_t score;
        int16_t  secs;
        uint8_t  lives, mult, red, mode, flash;
    }          hud;
    uint32_t   title_t;         /* ms on the title screen, for its idle anim */

    uint8_t    autoplay;        /* the bot plays (TP_AUTO=1, and the harness) */
    uint16_t   bot_ms;
    uint8_t    show_fps;
    int16_t    fps10;
    uint16_t   last_area;       /* % of the screen pushed last frame         */
    uint8_t    last_rects;
} tp_game_t;

/* Defined in topos.c, which is the only file that sees the HAL. It honours
 * the sound switch. The harness defines a silent one. */
void tp_sfx(int freq_hz, int ms);

/* --------------------------------------------------------------------------
 * tp_game.c
 * -------------------------------------------------------------------------- */

void     tp_game_init(tp_game_t *g, uint32_t seed);
void     tp_game_start(tp_game_t *g, int mode);
void     tp_game_step(tp_game_t *g, int dt_ms);
void     tp_game_tap(tp_game_t *g, int x, int y);
/* the bot: taps what a decent player would, and a bomb now and then */
void     tp_game_bot(tp_game_t *g, int dt_ms);
uint32_t tp_rand(tp_game_t *g);
int      tp_rand_range(tp_game_t *g, int lo, int hi);
int      tp_difficulty(const tp_game_t *g);         /* 0..1000 */

/* --------------------------------------------------------------------------
 * tp_draw.c
 * -------------------------------------------------------------------------- */

/* The lawn and the holes (or the title's scene) into bg, and bg into fb. */
void tp_bg_build(tp_game_t *g, bool title);
/* One frame: rebuilds what changed into fb and leaves in g->push what has to
 * be upscaled (plus g->hud_push for the score's strip). */
void tp_present(tp_game_t *g);
/* The slow path: bg plus every visible slot, drawn from scratch into out.
 * The harness compares it with fb after every present(). */
void tp_render_full(tp_game_t *g, uint16_t *out);
/* The box a slot's parameters produce, and drawing it on any buffer. For the
 * harness's box check. */
void tp_slot_box(const tp_dp_t *p, tp_rect_t *out, bool *any);
void tp_slot_paint(tp_buf_t *b, const tp_dp_t *p);
