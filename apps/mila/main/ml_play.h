/*
 * MILA - a level being played: the rules' state, the undo, the animation of
 * Mila and the things, the gates and flaps, the camera, the draw list
 *
 * The rules (ml_rules.c) move in whole steps; this file shows them: a step
 * starts tweens (Mila one cell, the pushed thing its whole slide or roll),
 * and the next step waits until they end. Runs in the worker.
 */
#pragma once

#include "ml_level.h"
#include "ml_mila.h"
#include "ml_render.h"
#include "ml_world.h"

#include <stdbool.h>
#include <stdint.h>

#define ML_HIST     1024
#define ML_QUEUE    64
#define ML_MAXGATES 16

enum {
    PE_STEP    = 1 << 0,
    PE_PUSH    = 1 << 1,
    PE_SLIDE   = 1 << 2,
    PE_ROLL    = 1 << 3,
    PE_FALL    = 1 << 4,
    PE_TARGET  = 1 << 5,        /* a thing came to rest on a target         */
    PE_GATE    = 1 << 6,        /* a gate started opening                   */
    PE_GATE_SHUT = 1 << 7,
    PE_FLAP    = 1 << 8,
    PE_BUMP    = 1 << 9,
    PE_WIN     = 1 << 10,
    PE_UNDO    = 1 << 11,
    PE_RESTART = 1 << 12,
};

typedef struct {
    ml_state_t st;
    uint16_t   pushes;
    uint8_t    face;
} ml_hist_t;

typedef struct {
    bool    on;
    uint8_t from, to;           /* cells                                    */
    float   u, dur;             /* progress 0..1 over dur seconds           */
    float   delay;              /* waits this long first                    */
    bool    fell;
    float   dist;               /* cells travelled so far (the roll frames) */
} ml_tween_t;

/* the art of the level's moving things */
typedef struct {
    ml_anim_t obj, obj_sh, obj_on;      /* objects: variants                */
    ml_anim_t ball, ball_sh;            /* balls: variants                  */
    ml_anim_t ball_roll[2];             /* rolling frames per ball variant  */
    ml_anim_t gate_h, gate_v, flap_h, flap_v;
    ml_anim_t gate_h_sh, gate_v_sh;
} ml_things_art_t;

typedef struct {
    const ml_level_t *lv;
    ml_world_t *w;
    const ml_mila_t *mila;
    ml_things_art_t art;

    ml_state_t st;
    ml_hist_t  hist[ML_HIST];
    int        nhist;
    int        moves, pushes;
    bool       won;
    float      won_t;

    /* Mila on screen */
    int   face;                 /* MD_*                                     */
    int   anim;                 /* MA_*                                     */
    float anim_t;
    ml_tween_t mt;              /* her step                                 */
    float bump_t;               /* a step into a wall: a little nudge       */
    float idle_t;
    ml_tween_t th[ML_MAXT];

    /* gates and flaps */
    int   ngates;
    uint8_t gate_cell[ML_MAXGATES];
    float gate_amt[ML_MAXGATES];
    bool  gate_h[ML_MAXGATES];
    int   nflaps;
    uint8_t flap_cell[ML_MAXGATES];
    float flap_t[ML_MAXGATES];

    /* input, written by the UI */
    volatile uint8_t q[ML_QUEUE];
    volatile uint32_t qw, qr;
    volatile bool want_undo, want_restart;

    uint32_t events;

    /* the camera: LP of the screen's top-left */
    float cam_x, cam_y;
    int   icam_x, icam_y;
} ml_play_t;

bool ml_play_init(ml_play_t *p, const ml_level_t *lv, ml_world_t *w, const ml_mila_t *mila,
                  const char *kit);
void ml_play_free(ml_play_t *p);
/* input (any thread): a step, a walk to a cell (tap), undo, restart */
void ml_play_push_dir(ml_play_t *p, int d);
bool ml_play_walk_to(ml_play_t *p, int cell);
void ml_play_undo(ml_play_t *p);
void ml_play_restart(ml_play_t *p);
/* advances dt seconds */
void ml_play_step(ml_play_t *p, float dt);
bool ml_play_busy(const ml_play_t *p);
/* where Mila is drawn (grid coordinates, cell centre at +0.5) */
void ml_play_mila_pos(const ml_play_t *p, float *gx, float *gy);
/* the camera snaps (at the start) or follows */
void ml_play_camera(ml_play_t *p, float dt, bool snap);
/* the draw list of the moving things */
void ml_play_draw(ml_play_t *p, ml_dlist_t *dl);
/* the same things for the overview painter */
int  ml_play_ov_items(ml_play_t *p, ml_ov_item_t *out, int max);
/* the screen cell under a screen point, -1 if none */
int  ml_play_cell_at(const ml_play_t *p, int sx, int sy);
int  ml_play_stars(const ml_play_t *p);
int  ml_play_on_target(const ml_play_t *p);
