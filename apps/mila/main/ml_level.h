/*
 * MILA - the worlds table and the levels, read from mila.pak
 *
 * Both are TEXT entries of the pack (tools/levels.py writes them), so a new
 * world is data: DESIGN.md section 4.
 *
 * "worlds":
 *     world living                      one block per world, in map order
 *     name es=El living|en=Living room|de=Wohnzimmer
 *     kit living                        its tiles' prefix
 *     panel map_living                  its map panel
 *     need 0                            stars to open it
 *     music 0
 *     mech 0                            ML_MECH_* bits
 *     gift hat_bow                      given when finished ("-" none)
 *     level living_1 es=...|en=...|de=...     one line per level, in order
 *     end
 *
 * "lvl_<name>": u16 par moves, u16 pushes at par, then the level text
 * (DESIGN.md section 5) and a NUL, then the furniture string and a NUL.
 */
#pragma once

#include "ml_rules.h"

#include <stdbool.h>
#include <stdint.h>

#define ML_MAX_WORLDS   16
#define ML_MAX_LEVELS   16          /* per world                            */
#define ML_ID_LEN       16

enum {
    ML_MECH_WET   = 1 << 0,
    ML_MECH_PLATE = 1 << 1,
    ML_MECH_FLAP  = 1 << 2,
    ML_MECH_HOLE  = 1 << 3,
    ML_MECH_BALL  = 1 << 4,
};

typedef struct {
    char    id[ML_ID_LEN];          /* the level's pack name (living_1)     */
    char   *title;                  /* "es=..|en=..|de=.." (owned)          */
} ml_level_ref_t;

typedef struct {
    char    id[ML_ID_LEN];
    char    kit[ML_ID_LEN];
    char    panel[24];
    char    gift[24];
    char   *name;                   /* "es=..|en=..|de=.." (owned)          */
    int     need, music;
    uint32_t mech;
    int     nlevels;
    ml_level_ref_t lv[ML_MAX_LEVELS];
} ml_world_info_t;

typedef struct {
    int nworlds;
    ml_world_info_t w[ML_MAX_WORLDS];
} ml_worlds_t;

bool ml_worlds_load(ml_worlds_t *t);
void ml_worlds_free(ml_worlds_t *t);
int  ml_worlds_find(const ml_worlds_t *t, const char *id);
/* the text for the current language out of "es=..|en=..|de=..", into buf */
const char *ml_pick_lang(const char *multi, char *buf, int n);

/* furniture of a wall cell (ml_level_t.deco) */
enum { DECO_WALL = 0, DECO_HIDDEN = 254, DECO_NONE = 255 };
/* 1..63: a 1-cell piece number; 64 + k: the left cell of 2-cell piece k,
 * its right cell is DECO_NONE (drawn by the left one) */

typedef struct {
    ml_map_t   map;
    ml_state_t start;
    int        par, par_pushes;
    uint8_t    deco[ML_CELLS];
} ml_level_t;

/* loads lvl_<id>; the furniture comes from the level or, where it says
 * nothing, from a hash of the cell over n1 single and n2 double pieces */
bool ml_level_load(ml_level_t *l, const char *id, int n1, int n2);
