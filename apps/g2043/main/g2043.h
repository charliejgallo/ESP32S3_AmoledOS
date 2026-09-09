/*
 * 2043 - shared game state
 *
 * The game is split into four:
 *
 *   gx_pixel.c   drawing pixels
 *   gx_art.c     the sprites
 *   gx_world.c   the planets: backgrounds, waves, level table
 *   gx_foe.c     how the enemies and bosses behave
 *   g2043.c      the app: input, HUD, loop and state machine
 *
 * Everything crossing those boundaries is in this header. Positions go in
 * 1/16-of-an-art-pixel fixed point: it gives smooth movement without floating
 * point, which in a dynamic app means not dragging in libm.
 */
#pragma once

#include "gx_pixel.h"
#include "gx_art.h"

/* --------------------------------------------------------------------------
 * Measurements and pace
 * -------------------------------------------------------------------------- */

/* Frame period.
 *
 * G_FRAME_MS is the floor (the best it aspires to) and G_FRAME_MAX the
 * ceiling. The game starts at the floor and relaxes on its own according to
 * what it measures: if the drawing comes out more expensive than the period,
 * the timer is always overdue, the LVGL task never gets to sleep, the idle
 * task does not run and the task_wdt fires. Rather than hang the board, the
 * game relaxes.
 *
 * The floor is 33 ms because it is the firmware's ceiling:
 * CONFIG_LV_DEF_REFR_PERIOD is 33, so LVGL does not refresh more often even
 * with CPU to spare. */
#define G_FRAME_MS      33          /* floor: ~30 frames per second */
#define G_FRAME_MAX    120          /* ceiling: ~8, ugly but alive  */
#define G_FPS           30

#define FX_ONE          16
#define FX(v)           ((int16_t)((v) * FX_ONE))
#define UNFX(v)         ((v) / FX_ONE)

#define G_HUD_H         12
#define G_FOOT_H        16
#define G_PLAY_Y0       G_HUD_H
#define G_PLAY_Y1       (GX_H - G_FOOT_H - 1)   /* last playable row */

#define G_MAX_ENEMIES   20
#define G_MAX_PSHOTS    36
#define G_MAX_ESHOTS    64
#define G_MAX_PICKUPS    6
#define G_MAX_FX        30
#define G_MAX_SCENERY   14
#define G_MAX_STARS     44
#define G_BOSS_PARTS     5

#define G_ENERGY_MAX    240
#define G_LIVES_START     3

/* --------------------------------------------------------------------------
 * Weapons and capsules
 * -------------------------------------------------------------------------- */

typedef enum {
    W_SHOT = 0,     /* the factory one: two straight bullets */
    W_TWIN,         /* four straight bullets                 */
    W_SPREAD,       /* a fan of three                        */
    W_LASER,        /* a beam that goes through              */
    W_WAVE,         /* wide slow wave, heavy damage          */
    W_COUNT,
} g_weapon_t;

typedef enum {
    PU_TWIN = 0,
    PU_SPREAD,
    PU_LASER,
    PU_WAVE,
    PU_ENERGY,
    PU_SHIELD,
    PU_LIFE,
    PU_COUNT,
} g_pickup_kind_t;

/* Player's shots */
typedef enum { PS_BULLET = 0, PS_LASER, PS_WAVE } g_pshot_kind_t;

/* Enemy shots */
typedef enum { ES_BALL = 0, ES_AIMED, ES_HEAVY, ES_ROCK, ES_HOMING } g_eshot_kind_t;

/* Effects */
typedef enum { FX_SPARK = 0, FX_BOOM, FX_RING, FX_TEXT, FX_DEBRIS } g_fx_kind_t;

/* --------------------------------------------------------------------------
 * Entities
 * -------------------------------------------------------------------------- */

typedef struct {
    int16_t x, y;       /* centre, 1/16 of a pixel */
    int16_t vx, vy;
    int16_t hp;
    int16_t t;          /* frames alive         */
    int16_t a, b;       /* state private to each type */
    uint8_t kind;
    uint8_t wave;       /* wave it belongs to, 0 = loose */
    uint8_t flash;      /* frames of flash from a hit */
    uint8_t alive;
} g_enemy_t;

typedef struct {
    int16_t x, y, vx, vy;
    int16_t t;
    uint8_t kind;
    uint8_t alive;
} g_shot_t;

typedef struct {
    int16_t x, y, vy;
    int16_t t;
    uint8_t kind;
    uint8_t alive;
} g_pickup_t;

typedef struct {
    int16_t x, y, vx, vy;
    int16_t t, life;
    uint16_t color;
    uint8_t kind;
    uint8_t alive;
    const char *text;   /* FX_TEXT only */
} g_fx_t;

typedef struct {
    int16_t x, y;       /* position of the scenery item, in pixels */
    int16_t speed;      /* 1/16 of a pixel per frame */
    uint8_t kind;
    uint8_t size;
    uint8_t alive;
} g_scenery_t;

typedef struct {
    uint8_t x;
    uint8_t layer;      /* 0 far, 2 near */
    int16_t y;          /* 1/16 of a pixel */
} g_star_t;

typedef struct {
    int16_t x, y, vx, vy;
    int16_t hp, hp_max;
    int16_t part_hp[G_BOSS_PARTS];
    int16_t t, phase, timer, a, b;
    uint8_t def;
    uint8_t flash;
    uint8_t parts;      /* how many parts this boss has */
    uint8_t alive;
    uint8_t dying;      /* frames of death throes */
} g_boss_t;

/* --------------------------------------------------------------------------
 * Game state
 * -------------------------------------------------------------------------- */

typedef enum {
    ST_TITLE = 0,       /* control choice, with an LVGL overlay */
    ST_READY,           /* the planet's panel                       */
    ST_PLAY,
    ST_BOSS_IN,
    ST_BOSS,
    ST_CLEAR,           /* boss finished off                        */
    ST_DEAD,            /* the player explodes                      */
    ST_GAMEOVER,
    ST_WIN,
    ST_PAUSE,
} g_state_t;

typedef enum {
    CTRL_TOUCH = 0,
    CTRL_TILT,
} g_control_t;

typedef struct {
    gx_buf_t buf;

    /* --- player --- */
    int16_t  px, py;            /* 1/16 of a pixel */
    int16_t  pvx, pvy;
    int16_t  energy;
    int8_t   lives;
    uint8_t  weapon;
    uint8_t  wlevel;            /* 1..3 */
    int16_t  fire_cd;
    int16_t  invuln;            /* frames of invulnerability */
    int16_t  shield;            /* frames of shield from a capsule */
    int16_t  roll;              /* frames of barrel roll; invulnerable while it lasts */
    int16_t  roll_cd;
    uint32_t score;
    uint32_t hiscore;

    /* --- input --- */
    uint8_t  control;
    uint8_t  autofire;
    uint8_t  fire_down;
    int16_t  last_click_t;      /* to detect the barrel roll's double tap */
    uint8_t  touching;
    int16_t  touch_x, touch_y;  /* target in 1/16 of a pixel */
    int8_t   inv_x, inv_y, swap_axes;
    float    tilt_zero_x, tilt_zero_y;
    uint8_t  tilt_zeroed;
    int16_t  tilt_mx, tilt_my;  /* last IMU reading, in milli-g */
    uint8_t  imu_skip;          /* frames left before reading it again */

    /* --- world --- */
    uint8_t  state;
    uint8_t  level;             /* index into gx_levels */
    int16_t  state_t;           /* frames in the current state */
    int32_t  level_t;           /* frames within the level */
    int32_t  scroll;            /* background offset, 1/16 of a pixel */
    uint16_t wave_next;         /* next wave from the table */
    uint8_t  wave_seq;          /* identifier of the current wave */
    uint8_t  wave_gift[64];     /* gift pending for the wave */
    uint8_t  wave_left[64];     /* how many enemies of that wave are left */
    uint32_t rng;
    int16_t  shake;             /* frames of screen shake */
    uint8_t  show_fps;          /* frames-per-second meter in the HUD */
    uint8_t  detail;            /* 1 = expensive decorations on (see period_tune) */
    int8_t   hold_pct;          /* 0..100: sustained touch on the score */
    int16_t  fps10;             /* frames per second x10, averaged           */
    int16_t  flash_screen;      /* frames of white flash */

    g_enemy_t   enemies[G_MAX_ENEMIES];
    g_shot_t    pshots[G_MAX_PSHOTS];
    g_shot_t    eshots[G_MAX_ESHOTS];
    g_pickup_t  pickups[G_MAX_PICKUPS];
    g_fx_t      fx[G_MAX_FX];
    g_scenery_t scenery[G_MAX_SCENERY];
    g_star_t    stars[G_MAX_STARS];
    g_boss_t    boss;
} g_t;

/* --------------------------------------------------------------------------
 * Services g2043.c offers the rest
 * -------------------------------------------------------------------------- */

uint32_t g_rnd(g_t *g);
int      g_rnd_range(g_t *g, int lo, int hi);   /* inclusive */

g_shot_t *g_spawn_eshot(g_t *g, int x, int y, int vx, int vy, int kind);
g_fx_t   *g_spawn_fx(g_t *g, int x, int y, int vx, int vy, int kind,
                     uint16_t color, int life);
void      g_boom(g_t *g, int x, int y, int radius, uint16_t color);
void      g_add_score(g_t *g, int points);
void      g_drop_pickup(g_t *g, int x, int y, int kind);
void      g_beep(int freq, int ms);

/* Angle (in brads) from a point towards the player. */
int  g_angle_to_player(const g_t *g, int x, int y);
/* Fires at the player with the given speed (1/16 of a pixel per frame). */
void g_shoot_at_player(g_t *g, int x, int y, int speed, int kind);

/* --------------------------------------------------------------------------
 * gx_world.c - the planets
 * -------------------------------------------------------------------------- */

typedef enum {
    FORM_COLUMN = 0,    /* one behind the other, same column     */
    FORM_ROW,           /* side by side, they come in together   */
    FORM_V,             /* in an arrowhead                       */
    FORM_SIDE_L,        /* they come in from the left            */
    FORM_SIDE_R,
    FORM_SPREAD,        /* spread across the width               */
} g_form_t;

typedef struct {
    uint16_t at;        /* frame of the level at which it appears */
    uint8_t  kind;      /* gx_enemy_kind_t */
    uint8_t  count;
    uint8_t  form;
    uint8_t  x;         /* base column, 0..100 (% of the width) */
    uint8_t  gift;      /* if != 0, a capsule on wiping out the wave: 1 + PU_* */
} g_wave_t;

typedef enum {
    BG_DUST = 0,        /* rusted plains                         */
    BG_OCEAN,           /* sea with crests and ice               */
    BG_BELT,            /* deep space with asteroids             */
} g_bg_t;

typedef struct {
    const char *name;
    const char *tag;            /* second line of the panel */
    uint32_t sky_top, sky_bot;  /* the background's gradient */
    uint32_t feat_a, feat_b;    /* the scenery's colours     */
    uint32_t haze;
    uint8_t  bg;
    uint16_t length;            /* frames until the boss      */
    uint16_t scroll;            /* 1/16 of a pixel per frame */
    const g_wave_t *waves;
    uint8_t  wave_count;
    uint8_t  boss;
} g_level_t;

extern const g_level_t gx_levels[];
extern const int       gx_level_count;

void gx_bg_reset(g_t *g);
void gx_bg_update(g_t *g);
void gx_bg_draw(g_t *g);

/* --------------------------------------------------------------------------
 * gx_foe.c - enemies and bosses
 * -------------------------------------------------------------------------- */

int  gx_enemy_hp(int kind);
int  gx_enemy_score(int kind);
int  gx_enemy_radius(int kind);

void gx_enemy_update(g_t *g, g_enemy_t *e);
void gx_enemy_draw(g_t *g, const g_enemy_t *e);
void gx_enemy_died(g_t *g, g_enemy_t *e);

typedef struct {
    const char *name;
    int16_t hp;         /* total: parts + core, it is what the bar shows */
    uint8_t parts;      /* destructible parts; the core is part 'parts'  */
    void (*init)(g_t *g, g_boss_t *b);
    void (*think)(g_t *g, g_boss_t *b);
    void (*draw)(g_t *g, const g_boss_t *b);
    /* Hit box of each part, relative to the boss's centre. Part number 'parts'
     * is always the core. */
    void (*hitbox)(const g_boss_t *b, int part, int *x, int *y, int *r);
} gx_boss_def_t;

extern const gx_boss_def_t gx_bosses[];
extern const int           gx_boss_count;

void gx_boss_start(g_t *g, int def);
void gx_boss_update(g_t *g);
void gx_boss_draw(g_t *g);
/* Returns true if the shot connected. 'dmg' is the damage. */
bool gx_boss_hit(g_t *g, int x, int y, int dmg);
