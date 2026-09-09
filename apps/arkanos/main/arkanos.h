/*
 * ARKANOS - shared state
 *
 * The game is split into five:
 *
 *   ak_pixel.c   drawing pixels and keeping track of what was dirtied
 *   ak_level.c   the bricks: types, level tables and palettes
 *   ak_play.c    the simulation: ball, paddle, collisions, capsules
 *   ak_draw.c    the background, the HUD and everything that moves
 *   arkanos.c    the app: input, panels, loop and state machine
 *
 * Positions go in 1/16-of-an-art-pixel fixed point: smooth movement without
 * floating point, which in a dynamic app means not dragging in libm.
 */
#pragma once

#include "ak_pixel.h"

/* --------------------------------------------------------------------------
 * Pace
 *
 * The floor is 33 ms and no less on purpose: the firmware has
 * CONFIG_LV_DEF_REFR_PERIOD=33, so LVGL does not refresh more often than that
 * and asking for 20 ms frames would only spend CPU on upscales nobody sees.
 * This app's goal is not to beat 30 frames per second but to sustain them.
 *
 * The ceiling is still there: if the board cannot keep up, the game relaxes
 * rather than leaving the LVGL task without sleep and waking the watchdog.
 * -------------------------------------------------------------------------- */
#define AK_FRAME_MS         33
#define AK_FRAME_START      33
#define AK_FRAME_MAX       100

/* --------------------------------------------------------------------------
 * Field geometry
 *
 * 11 columns of 16 px starting at x=4 give 4 + 176 = 180, and 4 px of wall are
 * left on each side: the width closes exactly and no brick ends up split.
 * -------------------------------------------------------------------------- */
#define AK_HUD_H            14
#define AK_WALL             4
#define AK_FIELD_X0         AK_WALL                 /*   4 */
#define AK_FIELD_X1         (AK_W - AK_WALL)        /* 180 */
#define AK_FIELD_Y0         (AK_HUD_H + AK_WALL)    /*  18 */

#define AK_COLS             11
#define AK_ROWS             13
#define AK_BRICK_W          16
#define AK_BRICK_H          8
#define AK_BRICK_X0         AK_FIELD_X0
#define AK_BRICK_Y0         26

#define AK_PAD_Y            204
#define AK_PAD_H            5
#define AK_PAD_W_MIN        20
#define AK_PAD_W_STD        32
#define AK_PAD_W_MAX        50
#define AK_DEATH_Y          219

#define AK_BALL_R           2

#define FX_ONE              16
#define FX(v)               ((int16_t)((v) * FX_ONE))
#define UNFX(v)             ((v) / FX_ONE)

/* --------------------------------------------------------------------------
 * Bricks
 * -------------------------------------------------------------------------- */

typedef enum {
    BK_NONE = 0,
    BK_C1, BK_C2, BK_C3, BK_C4, BK_C5, BK_C6, BK_C7, BK_C8,
    BK_HARD,        /* two hits   */
    BK_TOUGH,       /* three hits */
    BK_STEEL,       /* unbreakable */
    BK_BOMB,        /* blows up its neighbours */
    BK_MYST,        /* always drops a capsule */
    BK_COUNT,
} ak_brick_kind_t;

#define AK_BF_SOLID     0x01    /* indestructible: does not count towards finishing */
#define AK_BF_BOMB      0x02
#define AK_BF_MYST      0x04

typedef struct {
    uint32_t color;
    uint8_t  hp;
    uint8_t  flags;
    uint16_t score;
} ak_brick_def_t;

const ak_brick_def_t *ak_brick(int kind);
/* Translates a character of the map into a type. '.' and space are empty. */
int ak_brick_from_char(char ch);

/* --------------------------------------------------------------------------
 * Levels
 * -------------------------------------------------------------------------- */

typedef struct {
    const char        *name;
    const char *const *rows;    /* AK_COLS characters each */
    uint8_t            nrows;
    uint8_t            speed;   /* the ball's speed, in 1/16 of a pixel    */
    uint8_t            drop;    /* capsule probability, out of 100         */
    uint32_t           sky_top;
    uint32_t           sky_bot;
    uint32_t           accent;  /* walls and details                       */
    uint8_t            stars;
} ak_level_t;

int                 ak_level_count(void);
const ak_level_t   *ak_level_get(int i);

/* --------------------------------------------------------------------------
 * Capsules
 * -------------------------------------------------------------------------- */

typedef enum {
    CAP_WIDE = 0,   /* wide paddle         */
    CAP_SLOW,       /* slow ball           */
    CAP_MULTI,      /* three balls         */
    CAP_LASER,      /* the paddle fires    */
    CAP_CATCH,      /* magnet: the ball sticks */
    CAP_LIFE,       /* one life            */
    CAP_POINTS,     /* points              */
    CAP_COUNT,
} ak_cap_kind_t;

typedef struct {
    char     letra;
    uint32_t color;
    const char *nombre;
} ak_cap_def_t;

const ak_cap_def_t *ak_cap_def(int kind);

/* --------------------------------------------------------------------------
 * Entities
 * -------------------------------------------------------------------------- */

#define AK_MAX_BALLS        4
#define AK_MAX_CAPS         4
#define AK_MAX_SHOTS        6
#define AK_MAX_BITS        40
#define AK_MAX_POPS         4
#define AK_MAX_RINGS        3
#define AK_TRAIL            3

typedef struct {
    int16_t x, y;                   /* fx, centre */
    int16_t vx, vy;                 /* fx per frame */
    int16_t tx[AK_TRAIL];           /* trail, in whole pixels */
    int16_t ty[AK_TRAIL];
    int16_t hold_off;               /* fx from the centre of the paddle */
    uint8_t alive;
    uint8_t held;                   /* resting on the paddle, not yet launched */
    uint8_t tn;
    uint8_t combo;                  /* consecutive bricks without touching the paddle */
} ak_ball_t;

typedef struct {
    int16_t x, y;                   /* fx, centre */
    uint8_t kind;
    uint8_t alive;
    uint8_t t;
} ak_cap_t;

typedef struct {
    int16_t x, y;                   /* fx */
    uint8_t alive;
} ak_shot_t;

typedef struct {
    int16_t  x, y, vx, vy;          /* fx */
    uint16_t col;
    uint8_t  life, life0;
} ak_bit_t;

typedef struct {
    int16_t  x, y;                  /* pixels */
    uint16_t col;
    uint8_t  life;
    char     txt[8];
} ak_pop_t;

typedef struct {
    int16_t  x, y;                  /* pixels */
    uint16_t col;
    uint8_t  life, life0;
} ak_ring_t;

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

typedef enum {
    ST_TITLE = 0,
    ST_READY,       /* level panel, ball resting */
    ST_PLAY,
    ST_LOST,        /* the ball was lost */
    ST_CLEAR,       /* level finished */
    ST_OVER,
    ST_WIN,
    ST_PAUSE,
} ak_state_t;

typedef enum { CTRL_TOUCH = 0, CTRL_TILT } ak_control_t;

typedef struct {
    ak_buf_t fb;                    /* the frame on screen */
    ak_buf_t bg;                    /* background + walls + bricks */

    uint8_t  state;
    int16_t  state_t;
    uint8_t  level;
    uint8_t  lap;                   /* lap: on finishing the levels they repeat faster */
    int8_t   lives;
    uint32_t score, hiscore;
    uint16_t left;                  /* breakable bricks remaining */

    uint8_t  grid[AK_ROWS][AK_COLS];
    uint8_t  hp[AK_ROWS][AK_COLS];
    uint8_t  hit[AK_ROWS][AK_COLS]; /* frames of flash on being hit */

    ak_ball_t ball[AK_MAX_BALLS];
    ak_cap_t  cap[AK_MAX_CAPS];
    ak_shot_t shot[AK_MAX_SHOTS];
    ak_bit_t  bit[AK_MAX_BITS];
    ak_pop_t  pop[AK_MAX_POPS];
    ak_ring_t ring[AK_MAX_RINGS];

    int16_t  pad_x;                 /* fx, centre */
    int16_t  pad_vx;                /* fx per frame */
    int16_t  pad_w;                 /* px, current width */
    int16_t  pad_w_want;
    uint16_t t_wide, t_laser, t_catch, t_slow;
    int8_t   laser_cd;
    uint8_t  speed;                 /* magnitude of the ball's velocity, fx */
    uint8_t  speed0;                /* the level's, without the SLOW effect   */

    /* input */
    uint8_t  control;
    uint8_t  touching;
    int16_t  touch_x, touch_y;
    uint8_t  fire_down, fire_edge;
    uint8_t  autoplay;              /* simulator only: the paddle plays itself */
    int8_t   imu_skip;
    uint8_t  tilt_zeroed;
    float    tilt_zero;
    int16_t  tilt_mx;
    /* Which sensor axis runs across the screen, and with what sign. Bit 1:
     * 0 = imu.ax, 1 = imu.ay.  Bit 0: 1 = inverted. That is, 0 = X+, 1 = X-,
     * 2 = Y+, 3 = Y-.
     *
     * It is a stored setting and not a constant because it depends on how the
     * chip ended up mounted, and on this board it is NOT what the code
     * assumed: measured on 2026-08-28, imu.ax is the PITCH axis (tilting the
     * screen forwards and backwards), so the paddle's is imu.ay. The sign is
     * not measured: that is what the switch is for. */
    uint8_t  tilt_axis;

    /* drawing */
    ak_dirty_t d_prev;              /* what we dirtied last frame */
    ak_dirty_t d_cur;               /* what we dirtied on this one          */
    ak_dirty_t d_bg;                /* background changes (bricks)          */
    ak_dirty_t d_push;              /* union: what is upscaled and invalidated */
    /* Background stars. They are stored rather than rolled on the fly because
     * ANY rectangle of the background has to be repaintable: when a brick
     * breaks, whatever was behind it has to be reconstructed. */
    int16_t  star_x[40], star_y[40];
    uint8_t  star_b[40];
    uint8_t  nstars;

    uint8_t  hud_dirty;
    uint8_t  hud_hold;              /* the finger is pressing the score */
    uint8_t  banner;                /* frames of the level panel */

    uint8_t  show_fps;
    int16_t  fps10;
    uint16_t last_area;             /* pixels pushed on the last frame */

    uint32_t rng;
} ak_t;

/* --------------------------------------------------------------------------
 * ak_play.c
 * -------------------------------------------------------------------------- */

uint32_t ak_rnd(ak_t *g);
int      ak_rnd_range(ak_t *g, int lo, int hi);

void ak_game_start(ak_t *g);
void ak_load_level(ak_t *g, int level);
void ak_step(ak_t *g);
void ak_boom(ak_t *g, int x, int y, int n, uint16_t col);
void ak_popup(ak_t *g, int x, int y, const char *txt, uint16_t col);

/* --------------------------------------------------------------------------
 * ak_draw.c
 * -------------------------------------------------------------------------- */

void ak_bg_build(ak_t *g);              /* the whole background, bricks included */
void ak_bg_brick(ak_t *g, int row, int col);  /* repaints one cell of the background */
void ak_draw_hud(ak_t *g);
void ak_draw_movers(ak_t *g);           /* draws into fb and fills d_cur */
void ak_brick_box(int row, int col, int *x, int *y);

/* --------------------------------------------------------------------------
 * arkanos.c
 * -------------------------------------------------------------------------- */

void ak_sfx(int freq_hz, int ms);       /* honours the sound switch */
