/*
 * GOLF - the rules: a round, its players, turns, penalties, scores, the
 * computer rivals of a tournament and the coins a round earns
 *
 * No LVGL, no HAL: the harness plays whole rounds with it.
 *
 * Stroke play. On each hole every player starts from the tee in order; after
 * that the one furthest from the hole plays. A ball in the water costs a
 * stroke and is dropped where it went in; out of bounds costs a stroke and
 * is played again from where it was hit. A hole is abandoned at triple par
 * (the "pick up" rule of friendly games, so nobody is stuck in a bunker for
 * ever).
 */
#pragma once

#include "gf_phys.h"
#include "gf_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    MODE_QUICK = 0,     /* three holes of the course, alone                  */
    MODE_TOUR,          /* the eight holes against three computer players    */
    MODE_PRACTICE,      /* one hole, as often as you like, no coins          */
    MODE_LOCAL,         /* 2-4 players passing the watch around              */
    MODE_LINK,          /* against the paired watch                          */
    MODE_N,
};

enum { DIFF_EASY = 0, DIFF_NORMAL, DIFF_PRO, DIFF_N };

#define GF_MAX_PLAYERS  4
#define GF_MAX_HOLES    8
#define GF_RIVALS       3

typedef struct {
    uint8_t  strokes[GF_MAX_HOLES];     /* per hole index of the round       */
    uint8_t  cur;                       /* strokes on the current hole       */
    uint8_t  done;                      /* holed out (or picked up)          */
    uint8_t  picked;
    uint8_t  lie;
    float    x, y;                      /* the ball                          */
    float    px, py;                    /* where the last shot was hit from  */
    uint8_t  remote;                    /* played on the other watch         */
} gf_player_t;

typedef struct {
    uint8_t  skill;                     /* 0..100                            */
    uint8_t  name;                      /* index into the rival names        */
    uint8_t  strokes[GF_MAX_HOLES];
} gf_rival_t;

typedef struct {
    uint8_t     mode, diff;
    uint8_t     nholes;
    uint8_t     holes[GF_MAX_HOLES];    /* course hole numbers, in order     */
    uint8_t     hi;                     /* index into holes[]                */
    uint8_t     nplayers;
    uint8_t     turn;                   /* whose shot                        */
    gf_player_t pl[GF_MAX_PLAYERS];
    gf_rival_t  rival[GF_RIVALS];
    uint8_t     nrivals;
    float       wind_x, wind_y;         /* this hole's wind, m/s             */
    uint32_t    seed;                   /* the round's: wind, rivals         */
    uint32_t    rng;
    uint8_t     tee_i;                  /* which tees, from the difficulty   */
    uint8_t     pin_i;                  /* which pin, from the round seed    */
} gf_game_t;

void gf_game_new(gf_game_t *g, int mode, int diff, int nplayers, uint32_t seed, int practice_hole);

/* The current hole's course number (0-based) */
int  gf_game_hole(const gf_game_t *g);
/* Loads the current hole into w and puts every ball on the tee. */
void gf_game_hole_start(gf_game_t *g, gf_world_t *w);
/* Who plays next (the furthest from the hole among those not done), or -1
 * when everybody is done. */
int  gf_game_next(gf_game_t *g, const gf_world_t *w);
/* Books the shot the current player just played. */
void gf_game_apply(gf_game_t *g, const gf_world_t *w, const gf_shot_t *s);
bool gf_game_hole_over(const gf_game_t *g);
/* Moves to the next hole; false when the round is over. Rivals' scores for
 * the hole just played are made up here. */
bool gf_game_next_hole(gf_game_t *g);

int  gf_game_total(const gf_game_t *g, int player);        /* strokes so far */
int  gf_game_to_par(const gf_game_t *g, int player);       /* vs par so far  */
int  gf_game_rival_total(const gf_game_t *g, int r);
int  gf_game_par_so_far(const gf_game_t *g, int upto_hole_index);
/* Finishing place of the human (1-based) in a tournament */
int  gf_game_place(const gf_game_t *g);
/* The coins the round earned so far */
int  gf_game_coins(const gf_game_t *g);

/* The club the caddie suggests for a distance from a lie */
int  gf_game_suggest_club(float dist, int lie, bool on_green);

/* distance to the pin */
float gf_game_dist_to_pin(const gf_world_t *w, float x, float y);

/* score names: -3 albatross .. +3 triple; 1 stroke = hole in one */
const char *gf_score_name(int strokes, int par);    /* N_() marked keys     */

const char *gf_rival_name(int i);
int         gf_rival_n(void);
