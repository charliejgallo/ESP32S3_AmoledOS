/*
 * GOLF - the ball: clubs, flight, bounce, roll and the cup
 *
 * A point ball in metres and seconds. In the air: gravity, drag, and lift
 * and curve from spin (backspin holds it up, sidespin hooks or slices it),
 * all against the wind. On the ground: bounces off the slope with the
 * surface's restitution and friction, then rolls with the surface's rolling
 * resistance and the slope pulling it. Trees catch it in the canopy or turn
 * it off the trunk. The cup takes it when it passes slowly enough.
 *
 * Deterministic: the same shot on the same hole gives the same path on every
 * watch, which is what lets two watches play against each other by sending
 * only the shot (gf_game.c). The only "random" thing, the bounce off a tree,
 * comes from a hash of the shot itself.
 *
 * The path is recorded at 30 samples a second for the app to play back.
 */
#pragma once

#include "gf_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    CLUB_DR = 0, CLUB_3W, CLUB_5W, CLUB_4H, CLUB_5I, CLUB_6I, CLUB_7I,
    CLUB_8I, CLUB_9I, CLUB_PW, CLUB_GW, CLUB_SW, CLUB_LW, CLUB_PT,
    CLUB_N,
};

typedef struct {
    const char *name;       /* "DR", "7I": the universal short names         */
    int16_t     carry_yd;   /* at full power, no wind, from a good lie        */
    int8_t      launch;     /* degrees                                        */
    uint8_t     spin;       /* lift / stopping power, in tenths               */
} gf_club_t;

/* a function, not extern data: see the note in gf_outfit.h */
const gf_club_t *gf_club(int i);

#define GF_YD           0.9144f
#define GF_TRK_MAX      600         /* 20 s at 30 samples a second            */
#define GF_TRK_HZ       30

enum {
    TK_AIR = 0,
    TK_BOUNCE,      /* this sample is a touch-down                            */
    TK_ROLL,
    TK_TREE,        /* it went through a tree                                 */
    TK_SPLASH,
    TK_CUP,
    TK_LIP,         /* it touched the cup and did not drop                    */
};

enum {
    RES_OK = 0,
    RES_WATER,      /* +1, a drop                                             */
    RES_OB,         /* +1, from where it was hit                              */
    RES_HOLED,
};

typedef struct {
    float   x, y, z;
    uint8_t ev;
} gf_trk_t;

typedef struct {
    /* input */
    int     club;
    float   aim;            /* heading, radians                               */
    float   power;          /* 0..1.1 (above 1 is an overswing)               */
    float   acc;            /* -1..1: <0 hooks left, >0 slices right          */
    float   wind_x, wind_y; /* m/s                                            */
    float   x0, y0;         /* from                                           */
    int     lie0;

    /* output */
    gf_trk_t trk[GF_TRK_MAX];
    int     n;
    int     result;
    float   x, y;           /* where it stopped (or went in)                  */
    int     lie;
    float   carry;          /* metres, from the start to the first touch-down */
    float   total;          /* metres, start to rest                          */
    float   apex;           /* metres above the start                         */
    float   land_x, land_y;
    float   drop_x, drop_y; /* water: where the next shot is played from      */
} gf_shot_t;

/* Once: works out each club's launch speed so that it carries its distance.
 * About 150 short simulations. */
void  gf_phys_init(void);

/* Plays a shot. Everything it needs is in s's input half. */
void  gf_phys_shot(const gf_world_t *w, gf_shot_t *s);

/* The carry in metres a club gives at a power from a lie, no wind: for the
 * aim marker and the computer players. */
float gf_phys_carry(int club, float power, int lie);
/* The lie's effect on distance, 0..1 */
float gf_lie_factor(int club, int lie);
/* A putt's speed for a power; and the power the stroke needs to roll about
 * 'dist' metres on a flat green */
float gf_putt_speed(float power);
float gf_putt_power(float dist);
