/*
 * MONSTER HOP - a level: the grid, the art it names, the things in it
 *
 * Levels are written by tools/levels.py (a small builder over ASCII maps)
 * into assets/levels/<name>.bin and packed as lvl_<name>. The grid is
 * x across (to the screen's right), y forward (up the screen), y = 0 the
 * row nearest the camera; each cell has a ground floor 0..3.
 *
 * Binary layout (little endian):
 *   "MHLV" u8 version u8 zone u8 w u8 h
 *   u8 start_x start_y start_dir flags  u16 time_s par_s
 *   u16 n_assets n_ents n_paths n_points
 *   n_assets x char[32]           index 0 is "" (none)
 *   w*h x cell (8 bytes, row-major from y = 0)
 *   n_ents x entity (12 bytes)
 *   n_paths x (u16 first point, u8 count, u8 flags)
 *   n_points x (u8 x, u8 y)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MH_LV_VERSION   1
#define MH_LV_MAXW      24
#define MH_LV_MAXH      40
#define MH_LV_MAXASSET  96
#define MH_LV_MAXENT    160
#define MH_LV_MAXPATH   32
#define MH_LV_MAXPT     256

enum { ZONE_CITY = 0, ZONE_CASTLE, ZONE_DESERT, ZONE_FOREST, ZONE_TEST, ZONE_N };

enum { DIR_N = 0, DIR_E, DIR_S, DIR_W };   /* +Y, +X, -Y, -X */

/* cell kinds */
enum {
    CK_GROUND = 0,      /* blocks up to floor h, walkable on top            */
    CK_PIT,             /* nothing: falling                                  */
    CK_WATER,           /* a surface 0.18 m below floor h: sinking           */
    CK_QUICK,           /* quicksand: walkable, but it swallows who stays    */
    CK_BRIDGE,          /* a surface below and a deck at floor h: walkable   */
};

/* cell flags */
enum {
    CF_SOLID  = 1 << 0, /* a prop stands here: nobody enters                */
    CF_ORIGIN = 1 << 1, /* the prop is drawn from this cell                 */
    CF_HIGH   = 1 << 2, /* a wall too tall to climb whatever the floors     */
    CF_GROUP  = 1 << 3, /* toggled by a lever (entity ENT_GROUPCELL)         */
    CF_SUNK   = 1 << 4, /* the prop (a crate) is one floor down: it filled a hole */
};

typedef struct {
    uint8_t h;          /* floor of the top, 0..3                          */
    uint8_t kind;       /* CK_*                                             */
    uint8_t top;        /* asset: the top block (CK_GROUND/QUICK), 0 none   */
    uint8_t fill;       /* asset: the blocks under it                       */
    uint8_t prop;       /* asset: the prop drawn from here (CF_ORIGIN)      */
    uint8_t flags;      /* CF_*                                             */
    uint8_t surf;       /* asset: the water / quicksand surface             */
    uint8_t deck;       /* asset: a bridge deck                             */
} mh_cell_t;

/* entities */
enum {
    ENT_NONE = 0,
    ENT_KEY,            /* x y z                                            */
    ENT_COIN,           /* x y z                                            */
    ENT_HEART,          /* x y z: one more life                             */
    ENT_HOURGLASS,      /* x y z: +30 s                                     */
    ENT_CHEST,          /* x y z dir, a = coins inside, b = ENT_HEART/...   */
    ENT_CHECKPOINT,     /* x y z                                            */
    ENT_EXIT,           /* x y z dir: the gate, facing dir                  */
    ENT_LEVER,          /* x y z dir, a = group                             */
    ENT_CRATE,          /* x y z, a = its art (asset index)                 */
    ENT_GROUPCELL,      /* x y, a = group, b = kind when ON (CK_*), c = floor when ON, d0 = deck asset */
    ENT_PLATFORM,       /* path a, p0 = ms per cell, b = group (0 = always), c = floor */
    ENT_MONSTER,        /* a = MON_*, x y z dir, b = path, c = flags, p0 = ms per cell, p1 = parameter */
    ENT_LANE,           /* a = LANE_*, x y z dir, c = length (cells), b = size, p0 = ms per cell, p1 = gap/phase */
    ENT_TRAP,           /* a = TRAP_*, x y z dir, p0 = period ms, p1 = phase ms */
    ENT_STICKER,        /* x y z: the level's hidden sticker for the album  */
    ENT_N,
};

/* monsters */
enum {
    MON_ZOMBIE = 0, MON_VAMPIRE, MON_MUMMY, MON_WEREWOLF,
    MON_ZOMBIEDOG, MON_ARMOR, MON_CROW,
    MON_BRUTE, MON_COUNT, MON_PHARAOH, MON_ALPHA,
    MON_N,
};
/* monster flags (c) */
enum {
    MF_PINGPONG = 1 << 0,   /* back and forth along the path, not a loop    */
    MF_NOTICE   = 1 << 1,   /* a zombie that turns and lunges               */
};

/* lanes: movers that cross the level in a line, like Frogger's traffic */
enum {
    LANE_CAR = 0,       /* runaway cars: deadly                             */
    LANE_LOG,           /* floating logs: ride them                         */
    LANE_BOULDER,       /* rolling boulders: deadly                         */
    LANE_SCARAB,        /* a line of beetles: deadly                        */
    LANE_BAT,           /* bats flying across: deadly                       */
    LANE_LILY,          /* lily pads that sink in turns: ride them          */
    LANE_N,
};

/* traps: cells that are deadly part of the time */
enum {
    TRAP_SPIKES = 0,    /* out for half the period                          */
    TRAP_VENT,          /* a burst of steam                                 */
    TRAP_DARTS,         /* a dart wall shooting along dir                   */
    TRAP_BEAR,          /* snaps once, then stays shut                      */
    TRAP_N,
};

typedef struct {
    uint8_t  type, x, y, z;
    uint8_t  dir, a, b, c;
    uint16_t p0, p1;
} mh_ent_def_t;

typedef struct {
    uint16_t first;
    uint8_t  n, flags;
} mh_path_t;

typedef struct {
    uint8_t  zone, w, h, flags;
    uint8_t  start_x, start_y, start_dir;
    uint16_t time_s, par_s;
    int      n_assets, n_ents, n_paths, n_points;
    char     asset[MH_LV_MAXASSET][32];
    mh_cell_t    *cell;             /* w * h                              */
    mh_ent_def_t  ent[MH_LV_MAXENT];
    mh_path_t     path[MH_LV_MAXPATH];
    uint8_t       pt[MH_LV_MAXPT][2];
} mh_level_t;

/* from a blob of the pack (lvl_<name>); false if malformed */
bool mh_level_parse(mh_level_t *lv, const uint8_t *b, uint32_t len);
void mh_level_free(mh_level_t *lv);

static inline mh_cell_t *mh_cell(const mh_level_t *lv, int x, int y)
{
    return &lv->cell[y * lv->w + x];
}
static inline bool mh_in(const mh_level_t *lv, int x, int y)
{
    return x >= 0 && y >= 0 && x < lv->w && y < lv->h;
}
