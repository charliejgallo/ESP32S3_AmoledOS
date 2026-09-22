/*
 * MONSTER HOP - the rules: Tommy, the monsters, the level's things
 *
 * Positions are world metres (a cell's centre is +0.5); Tommy and the
 * monsters live on the grid and move between cell centres, except when
 * something carries them (a log, a platform). Lanes, traps and platforms
 * are functions of the level's clock only, so two watches that started the
 * level together see them in the same place without talking.
 */
#pragma once

#include "mh_level.h"

#include <stdbool.h>
#include <stdint.h>

#define MH_KEYS         5
#define MH_MAX_MON      24
#define MH_MAX_PICK     128
#define MH_MAX_LANE     16
#define MH_MAX_TRAP     24
#define MH_MAX_CRATE    12
#define MH_MAX_PLAT     8
#define MH_MAX_LEVER    8
#define MH_MAX_CHEST    8
#define MH_MAX_CP       6
#define MH_MAX_FX       24
#define MH_MAX_DART     8
#define MH_MAX_BOULDER  6

enum { DIFF_EASY = 0, DIFF_NORMAL, DIFF_HARD, DIFF_N };

/* Tommy's states */
enum {
    H_IDLE = 0, H_HOP, H_SUPER, H_PUSH, H_USE, H_BUMP,
    H_HURT, H_SINK, H_FALL, H_WIN, H_GONE,
};

/* why he was lost */
enum { DIE_MONSTER = 0, DIE_WATER, DIE_QUICK, DIE_PIT, DIE_TRAP, DIE_TIME };

/* events, for the sounds, the effects and the HUD */
enum {
    EV_HOP = 1u << 0, EV_SUPER = 1u << 1, EV_LAND = 1u << 2, EV_KEY = 1u << 3,
    EV_COIN = 1u << 4, EV_HURT = 1u << 5, EV_SPLASH = 1u << 6, EV_FALL = 1u << 7,
    EV_CHECK = 1u << 8, EV_OPEN = 1u << 9, EV_WIN = 1u << 10, EV_LEVER = 1u << 11,
    EV_CHEST = 1u << 12, EV_PUSH = 1u << 13, EV_BUMP = 1u << 14, EV_HOWL = 1u << 15,
    EV_TIMEUP = 1u << 16, EV_OVER = 1u << 17, EV_LIFE = 1u << 18, EV_TIME = 1u << 19,
    EV_RESPAWN = 1u << 20, EV_STOMP = 1u << 21, EV_CAST = 1u << 22, EV_SPLASHC = 1u << 23,
    EV_LOW_TIME = 1u << 24, EV_STICKER = 1u << 25, EV_KEYLOST = 1u << 26, EV_RIVAL_KEY = 1u << 27,
};

/* what changed here that the other watch must know (the link): kind << 8 | index */
enum { OUT_KEY = 1, OUT_LEVER, OUT_CRATE, OUT_CHEST };
#define MH_MAX_OUT 16

typedef struct {
    float x, y, z;          /* the anchor (feet), metres                    */
    int   cx, cy;           /* the cell he is in (or landing in)            */
    int   floor;            /* standing floor (a crate adds one)            */
    int   dir;              /* facing                                       */
    int   state;
    float t, dur;           /* time in the state, its length                */
    float fx, fy, fz, tx, ty, tz, arc;
    int   tcx, tcy;
    int   ride;             /* -1, or what carries him: lane*64+k, 1000+platform */
    float ride_off;         /* his offset along the carrier                 */
    float quick_t;          /* time standing on quicksand                   */
    int   die_why;
    float cool;             /* super hop cool-down                          */
    int   queued;           /* a command given mid-hop: 0 none, 1+dir, 5 action */
    bool  high;             /* airborne above monsters (super hop peak)     */
} mh_hero_t;

enum { M_WALK = 0, M_IDLE, M_NOTICE, M_LUNGE, M_TRANSFORM, M_BAT, M_UNTRANSFORM, M_HOWL, M_RUN,
       M_STUN, M_HOME, M_PERCH, M_RISE, M_DIVE, M_RETURN, M_STOMP, M_CAST, M_WHIP, M_FADE, M_PUSHB };

typedef struct {
    uint8_t kind;           /* MON_*                                        */
    uint8_t state;
    uint8_t dir;
    uint8_t flags;
    int     path;           /* index in the level, -1 none                  */
    int     seg, sdir;      /* the path segment walked, +1/-1                */
    float   x, y, z;        /* position                                     */
    float   ox, oy;         /* where it stepped from                        */
    int     gx, gy;         /* the cell it is heading to                    */
    float   t, dur;         /* state timer                                  */
    float   s;              /* how far along its path, in cells              */
    float   step;           /* seconds per cell                             */
    float   param;          /* seconds (p1)                                 */
    float   anim;           /* animation clock                              */
    int     home_x, home_y, home_dir;
    int     cx0, cy0;       /* a charge's or a dive's start                 */
    float   timer;          /* a periodic action (transform, stomp...)      */
    uint8_t size;           /* 1, or 2 for the big bosses                   */
    uint32_t pal;           /* which palette variant                        */
} mh_mon_t;

typedef struct {
    uint8_t type;           /* ENT_KEY / COIN / HEART / HOURGLASS           */
    uint8_t x, y, z;
    bool    taken;
    uint8_t who;            /* 0 here, 1 the other watch (the link)         */
    float   t;              /* since taken (the sparkle)                    */
    float   at;             /* the level's clock when it was taken          */
} mh_pick_t;

typedef struct {
    uint8_t kind, dir, size, len;
    int     x, y, z;        /* the lane's first cell                        */
    float   step;           /* seconds per cell                             */
    float   gap;            /* cells between two movers                     */
    int     n;              /* movers on it                                 */
} mh_lane_t;

typedef struct {
    uint8_t kind, dir;
    int     x, y, z;
    float   period, phase;
    bool    sprung;         /* bear traps: snapped                          */
    float   fired;          /* darts: when the last one left                */
} mh_trap_t;

typedef struct {
    float x, y;             /* the dart's position                          */
    int   dir, z;
    bool  live;
} mh_dart_t;

typedef struct {
    float x, y;
    int   dir, z;
    bool  live;
    float roll;
} mh_boulder_t;

typedef struct {
    int   x, y, z;          /* the cell (z = its base floor)                */
    float mx, my;           /* moving: from where, for the slide             */
    float t;
    bool  sunk;             /* it fell into a hole and is ground now         */
    int   art;              /* its asset in the level                        */
} mh_crate_t;

typedef struct {
    int   path, group;
    float step;
    int   z;
    float x, y;             /* now                                           */
    float dx, dy;           /* its motion this step (to carry Tommy)        */
} mh_plat_t;

typedef struct {
    int  x, y, z, dir, group;
    bool on;
    float t;
} mh_lever_t;

typedef struct {
    int  x, y, z, dir, coins, bonus;
    bool open;
    float t;
} mh_chest_t;

typedef struct {
    int  x, y, z;
    bool lit;
} mh_cp_t;

typedef struct {
    uint8_t kind;           /* FX_*                                         */
    float x, y, z, t;
} mh_fx_t;

enum { FX_DUST = 1, FX_SPLASH, FX_POOF, FX_SPARKLE, FX_BUBBLES, FX_SHOCK };

typedef struct {
    mh_level_t *lv;
    int     diff;
    uint32_t seed;
    float   t;              /* the level's clock, seconds                   */
    int     state;          /* GS_*                                         */
    float   st;             /* time in it                                   */
    mh_hero_t h;
    int     keys, coins, lives, lost;
    bool    sticker;        /* the level's sticker was found            */
    int     hops;           /* for the stats                            */
    float   time_left;      /* only with a timer                            */
    bool    timer;
    bool    exit_open;
    float   exit_t;         /* since it opened                              */
    int     exit_x, exit_y, exit_z, exit_dir;
    int     cp;             /* the lit checkpoint, -1 = the start           */
    int     start_x, start_y;
    uint32_t events;

    mh_mon_t   mon[MH_MAX_MON];     int n_mon;
    mh_pick_t  pick[MH_MAX_PICK];   int n_pick;
    mh_lane_t  lane[MH_MAX_LANE];   int n_lane;
    mh_trap_t  trap[MH_MAX_TRAP];   int n_trap;
    mh_crate_t crate[MH_MAX_CRATE]; int n_crate;
    mh_plat_t  plat[MH_MAX_PLAT];   int n_plat;
    mh_lever_t lever[MH_MAX_LEVER]; int n_lever;
    mh_chest_t chest[MH_MAX_CHEST]; int n_chest;
    mh_cp_t    cpt[MH_MAX_CP];      int n_cp;
    mh_fx_t    fx[MH_MAX_FX];
    mh_dart_t  dart[MH_MAX_DART];
    mh_boulder_t boulder[MH_MAX_BOULDER];
    uint32_t   groups;              /* lever groups that are on (bits)    */
    int        dirty[16][2];        /* cells whose art changed (the cache) */
    int        n_dirty;
    /* the key race with the other watch (link): lives never run out, the
     * keys are shared and whoever took one first keeps it */
    bool   link, host;
    int    my_keys;
    uint16_t out[MH_MAX_OUT];       /* OUT_*: what to tell the other watch  */
    int    n_out;
    bool   rival_out;               /* it reached the exit                  */
    float  rival_out_t;
    /* the other watch's Tommy */
    bool   rival;
    float  rx, ry, rz, rf;          /* rf: how far into its hop (0..1)      */
    int    rdir, rstate;
} mh_game_t;

enum { GS_PLAY = 0, GS_DYING, GS_WON, GS_OVER };

void mh_game_init(mh_game_t *g, mh_level_t *lv, int diff, uint32_t seed);
/* a hop towards dir, or the action (BOOT) */
void mh_game_hop(mh_game_t *g, int dir);
void mh_game_action(mh_game_t *g);
void mh_game_step(mh_game_t *g, float dt);

/* the stars a finished level earned */
int  mh_game_stars(const mh_game_t *g);
/* the nearest key not taken (or the exit once open): false if none */
bool mh_game_target(const mh_game_t *g, float *x, float *y);

/* where a lane's movers are: count, and each one's front along the lane */
int  mh_lane_movers(const mh_game_t *g, int li, float *pos, int max);
/* a platform's position at time t */
void mh_plat_pos(const mh_game_t *g, int pi, float t, float *x, float *y);
/* is the trap deadly now? and its animation phase 0..1 */
bool mh_trap_active(const mh_game_t *g, int ti, float *phase);
/* the floor Tommy would stand on at (x, y): -9 = none (water, pit, solid) */
int  mh_stand_floor(const mh_game_t *g, int x, int y, bool *solid);
/* what carries at a world point now (logs, lilies, platforms): -1 none */
int  mh_game_carrier(mh_game_t *g, float x, float y);

/* the link: what the other watch did, applied here */
void mh_game_rival_key(mh_game_t *g, int pick, float at);
void mh_game_rival_lever(mh_game_t *g, int lever, bool on);
void mh_game_rival_crate(mh_game_t *g, int crate, int x, int y, int z, bool sunk);
void mh_game_rival_chest(mh_game_t *g, int chest);
void mh_game_rival_exit(mh_game_t *g, float at);
/* the race's points: a point a key, two more for the first one out */
int  mh_game_points(const mh_game_t *g, bool rival);
/* the race is decided: someone is out, or the time ran out */
bool mh_game_race_over(const mh_game_t *g);
