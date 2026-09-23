/*
 * MILA - the rules (DESIGN.md section 1)
 *
 * Plain C with no platform in it: the watch plays with it and the Mac's
 * solver (tools/solve.c) searches with it, so a level the solver calls
 * solvable behaves the same in the game.
 *
 * A level is at most 16 x 16 cells; a cell's index is y * 16 + x (y grows
 * DOWN the screen, row 0 is the top of the level text). Things (objects and
 * balls) are at most 8; a thing that dropped into a hole is gone (ML_GONE)
 * and its hole is filled for good.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ML_MAXW     16
#define ML_MAXH     16
#define ML_CELLS    (ML_MAXW * ML_MAXH)
#define ML_MAXT     8
#define ML_MAXHOLES 16
#define ML_GONE     255

enum {                      /* terrain, the low nibble of a cell            */
    T_VOID = 0,             /* outside: nothing, not even floor             */
    T_FLOOR,
    T_WALL,
    T_WET,                  /* objects slide on it                           */
    T_PLATE,
    T_GATE,                 /* open while every plate is pressed             */
    T_FLAP_V,               /* a cat flap crossed up/down (Mila only)       */
    T_FLAP_H,               /* crossed left/right                            */
    T_HOLE,                 /* a thing drops in and fills it                 */
};
#define C_TARGET 0x10       /* flag: a target on this cell                   */
#define C_TERR(c) ((c) & 0x0F)

enum { K_OBJECT = 0, K_BALL = 1 };
enum { D_UP = 0, D_RIGHT, D_DOWN, D_LEFT };

typedef struct {
    uint8_t w, h;
    uint8_t cell[ML_CELLS];
    uint8_t hole[ML_CELLS];     /* hole number of a T_HOLE cell, else 255     */
    uint8_t nholes, nplates, ntargets;
    uint8_t nthings;
    uint8_t kind[ML_MAXT];      /* K_*, objects first, then balls              */
} ml_map_t;

typedef struct {
    uint8_t  mila;              /* cell                                        */
    uint8_t  pos[ML_MAXT];      /* cell, or ML_GONE                            */
    uint16_t filled;            /* holes filled, bit per hole number           */
} ml_state_t;

/* what one step did, for the animation and the sound */
typedef struct {
    bool    moved;              /* Mila changed cell                          */
    int8_t  thing;              /* the thing pushed, -1 none                   */
    uint8_t from, to;           /* the thing's cells (to = where it stopped,   *
                                 * or the hole it dropped into)                */
    uint8_t cells;              /* how many cells it travelled (slides, rolls) */
    bool    fell;               /* it dropped into a hole                      */
} ml_step_t;

static inline int ml_dx(int d) { return d == D_RIGHT ? 1 : d == D_LEFT ? -1 : 0; }
static inline int ml_dy(int d) { return d == D_DOWN ? 1 : d == D_UP ? -1 : 0; }
static inline int ml_cx(int c) { return c & 15; }
static inline int ml_cy(int c) { return c >> 4; }

/* the neighbour of cell c in direction d, or -1 off the grid */
int  ml_next(const ml_map_t *m, int c, int d);
/* the thing on cell c, or -1 */
int  ml_thing_at(const ml_map_t *m, const ml_state_t *s, int c);
/* the terrain as it is now: a filled hole is floor */
int  ml_terrain(const ml_map_t *m, const ml_state_t *s, int c);
bool ml_plates_down(const ml_map_t *m, const ml_state_t *s);
bool ml_gate_open(const ml_map_t *m, const ml_state_t *s, int c);
/* can Mila stand on c coming in direction d (things aside) */
bool ml_mila_can(const ml_map_t *m, const ml_state_t *s, int from, int c, int d);
/* one step; false (and *st unchanged) when Mila cannot move that way */
bool ml_step(const ml_map_t *m, ml_state_t *st, int d, ml_step_t *out);
bool ml_won(const ml_map_t *m, const ml_state_t *s);
/* sort the things of each kind (objects are interchangeable, balls too):
 * the solver's canonical form */
void ml_canon(const ml_map_t *m, ml_state_t *s);

/* parse a level text (DESIGN.md section 5): 0 on success, else an error
 * message in err */
int  ml_parse(const char *text, ml_map_t *m, ml_state_t *start, char *err, int errn);
