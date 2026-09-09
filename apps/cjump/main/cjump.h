/*
 * CLAUDE JUMP - game state
 *
 * A vertical platform jumper starring the Claude Code critter. You climb by
 * bouncing from platform to platform, collect coins and with the coins buy
 * costumes in the menu's shop.
 *
 * The file layout is the same as arkanos's and for the same reason -the rules
 * can be tested without a screen-:
 *
 *   cj_pixel.c   pixel art engine and dirty rectangles (copied from arkanos)
 *   cj_game.c    physics and rules. It includes neither LVGL nor the HAL.
 *   cj_draw.c    background, moving things and score, all onto the buffer
 *   cj_skins.c   the critter and its 16 costumes
 *   cjump.c      the app: LVGL screens, shop, preferences and the timer
 *
 * ALL WORLD COORDINATES GO IN 1/16 OF A PIXEL and grow downwards, so climbing
 * is subtracting. The camera never goes down: cam_y only decreases.
 *
 * And a rule that holds for everything drawn onto the canvas: cj_pixel's 5x7
 * font is ASCII and without accents, so NO TRANSLATABLE TEXT GOES THROUGH THE
 * CANVAS. The score is numbers and the rest of the words are LVGL labels,
 * which do have accents and umlauts. That heads off from the start the whole
 * class of problems 2043, arkanos and claudito had, whose catalogues have to
 * be transliterated.
 */
#pragma once

#include "cj_pixel.h"

#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Geometry
 * -------------------------------------------------------------------------- */

#define FX              16                  /* 1 pixel = 16 units             */
#define CJ_HUD_H        18                  /* the score's strip, at the top  */
#define CJ_TOP          CJ_HUD_H            /* first playable row             */

/* When the critter climbs above this row, the camera follows it. It is at 40%
 * of the usable height and not in the middle: leaving more sky above than
 * below lets you see where you are going to land, which is what you need to
 * look at. */
#define CAM_LINE        (CJ_TOP + 78)

#define PLAT_W          30
#define PLAT_H          6
#define HERO_W          18
#define HERO_H          16

/* The box the critter dirties. It is larger than the critter because the
 * costumes add things above it and at its sides (the top hat, the lamp, the
 * rocket) and all of that has to fit in the dirty rectangle or it leaves a
 * trail. */
#define HERO_BOX_W      28
#define HERO_BOX_H      30

#define COIN_R          4
#define BUG_W           16
#define BUG_H           12

/* --------------------------------------------------------------------------
 * Physics
 *
 * All measured in 1/16 of a pixel per frame, with the frame at 33 ms.
 *
 *   normal jump   9.5 px/frame -> 72 px of height
 *   spring       15.0 px/frame -> 180 px
 *   gravity       0.625 px/frame^2
 *
 * The gap between platforms runs from 28 to 56 px, so the normal jump always
 * reaches with room to spare and the spring is a shortcut, not a necessity.
 * -------------------------------------------------------------------------- */

#define GRAV            10
#define V_JUMP          (-152)
#define V_SPRING        (-240)
#define V_BUG_STOMP     (-176)      /* bounce from stomping a bug             */
#define V_FALL_MAX      200
#define V_ROCKET        (-104)
#define ROCKET_FRAMES   48

#define VX_MAX          88          /* 5.5 px per frame                       */

/* --------------------------------------------------------------------------
 * Platforms and objects
 * -------------------------------------------------------------------------- */

enum {
    PLAT_NORMAL = 0,
    PLAT_MOVING,        /* moves back and forth horizontally                  */
    PLAT_FRAGILE,       /* breaks when stepped on: it gives no boost          */
    PLAT_FADING,        /* boosts once and fades away                         */
    PLAT_KINDS
};

enum {
    ITEM_NONE = 0,
    ITEM_SPRING,        /* spring on top of the platform                      */
    ITEM_ROCKET,        /* rocket: it climbs by itself for a while            */
};

#define MAX_PLATS       22
#define MAX_COINS       14
#define MAX_BUGS        4
#define MAX_PARTS       28

typedef struct {
    int32_t  x, y;              /* world, 1/16 px, top-left corner            */
    int16_t  vx;                /* PLAT_MOVING only                           */
    uint8_t  type;
    uint8_t  item;
    uint8_t  fade;              /* 0 whole; 1..8 fading                       */
    uint8_t  active;
    /* The rectangle it occupied on screen last frame. The union of this with
     * the current one is the only dirty rectangle that has to be recorded. */
    cj_rect_t prev;
    uint8_t  drawn;
} cj_plat_t;

typedef struct {
    int32_t  x, y;
    uint8_t  active;
    uint8_t  phase;             /* sparkle, so they twinkle                   */
    cj_rect_t prev;
    uint8_t  drawn;
} cj_coin_t;

typedef struct {
    int32_t  x, y;
    int16_t  vx;
    uint8_t  active;
    uint8_t  kind;              /* 0 walks, 1 floats                          */
    uint8_t  anim;
    cj_rect_t prev;
    uint8_t  drawn;
} cj_bug_t;

typedef struct {
    int32_t  x, y;              /* world, 1/16 px: that way they scroll by themselves */
    int16_t  vx, vy;
    uint8_t  life;
    uint16_t color;
    cj_rect_t prev;
    uint8_t  drawn;
} cj_part_t;

/* --------------------------------------------------------------------------
 * States
 * -------------------------------------------------------------------------- */

typedef enum {
    ST_TITLE = 0,
    ST_SHOP,
    ST_PLAY,
    ST_DYING,       /* falling, already out of control, until it leaves the screen */
    ST_OVER,
    ST_PAUSE,
} cj_state_t;

enum { CTRL_TILT = 0, CTRL_TOUCH = 1 };

/* Zones: each one has its own sky, its own platform colours and its own
 * difficulty. The background is rebuilt whole only on changing zone (see
 * cj_pixel.h). */
#define CJ_ZONES        5

typedef struct {
    uint32_t sky_top, sky_bot;      /* sky gradient                           */
    uint32_t deco_a, deco_b;        /* clouds / stars / planets               */
    uint32_t plat_a, plat_b;        /* face and edge of the platform          */
    uint16_t from_m;                /* metres at which it starts              */
    uint8_t  deco_kind;             /* 0 clouds, 1 stars, 2 bubbles           */
    uint8_t  moving_pc, fragile_pc, fading_pc, bug_pc;   /* percentages      */
    uint8_t  gap_min, gap_max;      /* vertical gap, in pixels                */
} cj_zone_t;

extern const cj_zone_t cj_zones[CJ_ZONES];

/* --------------------------------------------------------------------------
 * The whole state
 * -------------------------------------------------------------------------- */

typedef struct {
    cj_buf_t   fb, bg;

    cj_state_t state;
    uint32_t   rng;

    /* critter */
    int32_t    hx, hy;              /* world, top-left corner                 */
    int32_t    hvx, hvy;
    int32_t    target_x;            /* where the control is taking it         */
    uint8_t    facing;              /* 0 faces front, 1 left, 2 right         */
    uint8_t    rocket;              /* rocket frames remaining                */
    uint8_t    squash;              /* frames of squash on bouncing           */
    cj_rect_t  hero_prev;
    uint8_t    hero_drawn;

    /* camera and score */
    int32_t    cam_y;               /* world of the screen's row 0            */
    int32_t    top_y;               /* the highest it reached, for the score  */
    int32_t    start_y;
    uint32_t   score;               /* metres                                 */
    uint32_t   hiscore;
    uint16_t   coins_run;           /* coins of this game                     */
    uint8_t    zone;
    uint8_t    zone_changed;        /* asks for the background to be rebuilt  */
    uint32_t   bg_seed;             /* this game's sky                        */
    uint8_t    new_record;

    /* world */
    cj_plat_t  plats[MAX_PLATS];
    cj_coin_t  coins[MAX_COINS];
    cj_bug_t   bugs[MAX_BUGS];
    cj_part_t  parts[MAX_PARTS];
    int32_t    gen_y;               /* the highest platform generated         */
    int16_t    last_plat_x;         /* so impossible jumps are not chained    */
    uint8_t    last_type;           /* and so two fragile ones are not chained */

    /* control */
    uint8_t    control;
    uint8_t    tilt_axis;           /* 0..3, the sensor's four mappings       */
    uint8_t    tilt_zeroed;
    float      zero_a, zero_b;
    uint8_t    touching;
    int16_t    touch_x;             /* in pixels of the small buffer          */

    /* appearance */
    uint8_t    skin;                /* costume worn                           */

    /* drawing */
    cj_dirty_t d_prev, d_cur, d_push, d_bg;
    uint8_t    hud_dirty;
    uint32_t   hud_score, hud_coins;
    uint8_t    show_fps;
    int16_t    fps10;
    uint16_t   last_area;           /* % of screen pushed, for the HUD        */
    uint8_t    last_rects;          /* and how many: it is what really costs  */

    /* effects */
    uint8_t    autoplay;            /* CJ_AUTO=1 in the simulator             */
} cj_t;

/* Defined in cjump.c, which is the only file that sees the HAL. It honours the
 * menu's sound switch. */
void cj_sfx(int freq_hz, int ms);

/* --------------------------------------------------------------------------
 * cj_game.c
 * -------------------------------------------------------------------------- */

void     cj_game_reset(cj_t *g);        /* starts a new game                  */
void     cj_step(cj_t *g);              /* one frame of physics               */
uint32_t cj_rand(cj_t *g);
int      cj_rand_range(cj_t *g, int lo, int hi);

/* --------------------------------------------------------------------------
 * cj_draw.c
 * -------------------------------------------------------------------------- */

void cj_bg_build(cj_t *g);              /* the whole sky, into the bg buffer  */
void cj_draw_movers(cj_t *g);           /* everything that moves, onto fb     */
void cj_draw_hud(cj_t *g);

/* --------------------------------------------------------------------------
 * cj_skins.c
 * -------------------------------------------------------------------------- */

#define CJ_SKINS        16
#define CJ_SKINS_FREE   3               /* the first three come unlocked      */

typedef struct {
    const char *name;                   /* marked with N_(), translated when
                                           drawn with _()                     */
    uint16_t    price;                  /* 0 = comes unlocked already         */
    uint32_t    body;                   /* body colour                        */
    uint32_t    shade;                  /* its shadow                         */
} cj_skin_t;

extern const cj_skin_t cj_skins[CJ_SKINS];

/* Draws the critter in its costume. (x,y) is the top-left corner of the BODY,
 * that is, of HERO_W x HERO_H; whatever the costume adds goes out from there
 * upwards and sideways, and that is why the dirty rectangle is HERO_BOX_*.
 *
 *   pose  0 still, 1 rising, 2 falling
 *   sq    squash on bouncing, 0..4
 */
void cj_hero_draw(cj_buf_t *b, int x, int y, int skin, int pose, int sq,
                  int facing, bool rocket);

/* The rectangle the critter occupies on screen, for the dirty list. */
void cj_hero_box(int x, int y, cj_rect_t *out);
