/*
 * TURBO - the race: the player's car, the traffic, checkpoints and the clock
 *
 * Arcade physics along the track: the car has a distance travelled (z), an
 * offset from the road's centre (x) and a speed. Steering moves it sideways;
 * a bend pushes it towards the outside in proportion to speed squared; off
 * the asphalt the top speed drops; props and traffic are hit in (z, x).
 * Everything is in metres and seconds. tb_game_step() is deterministic for
 * a given sequence of inputs and steps.
 */
#pragma once

#include "tb_track.h"

#include <stdbool.h>
#include <stdint.h>

#define TB_CAM_BACK     3.2f        /* camera behind the car's rear bumper    */
#define TB_CAM_H        2.0f
#define TB_TRAFFIC      18

enum { CAR_WEDGE = 0, CAR_MUSCLE, CAR_RALLY, CAR_PICKUP, CAR_N };
/* the traffic's paints are 0..15, picked at random; the hearse has its own */
#define TB_PAINT_HEARSE 16

/* traffic models follow the player's in the pack */
enum { VH_SEDAN = CAR_N, VH_COMPACT, VH_VAN, VH_TRUCK, VH_HEARSE, VH_N };

enum { DIFF_EASY = 0, DIFF_NORMAL, DIFF_HARD, DIFF_N };

enum {
    RS_COUNTDOWN = 0,
    RS_RACING,
    RS_FINISHED,
    RS_TIMEUP,
};

/* things that happened in a step, for sounds and banners */
#define EV_CHECKPOINT   0x0001
#define EV_FINISH       0x0002
#define EV_TIMEUP       0x0004
#define EV_CRASH        0x0008
#define EV_BUMP         0x0010      /* a light hit on traffic                  */
#define EV_OFFROAD      0x0020      /* went off the asphalt                    */
#define EV_GEAR         0x0040
#define EV_COUNT        0x0080      /* a countdown beep                        */
#define EV_GO           0x0100
#define EV_PASS         0x0200      /* overtook a car close by                 */
#define EV_LOW_TIME     0x0400      /* a tick in the last ten seconds          */

typedef struct {
    const char *name;
    float vmax;                 /* m/s                                        */
    float accel;                /* m/s^2 from standstill                      */
    float grip;                 /* steering authority, and less slide         */
    float offroad;              /* top speed factor off the asphalt           */
    float tough;                /* speed kept in a crash                      */
    int   price;
} tb_car_spec_t;

const tb_car_spec_t *tb_car_spec(int car);

typedef struct {
    float   z;                  /* rear bumper, metres along the track        */
    float   x;                  /* centre, metres from the road's centre      */
    float   v;                  /* m/s                                        */
    float   tx;                 /* the lane it is moving to                   */
    uint8_t model;              /* VH_* or CAR_*                              */
    uint8_t paint;              /* index into the traffic paints              */
    uint8_t lane;
    bool    braking;
    bool    ghost;              /* see-through, and the car drives through it */
} tb_traffic_t;

typedef struct {
    const tb_track_t *trk;
    int      car;               /* CAR_*                                      */
    int      diff;
    /* input, set by the UI */
    float    in_steer;          /* -1 left .. 1 right                         */
    bool     in_gas, in_brake;

    /* the car */
    float    z, x, v;
    float    slide;             /* how much the curve is pushing, -1..1       */
    float    yaw;               /* visual yaw -1..1: picks the sprite frame   */
    float    bump;              /* shake 0..1, decays                          */
    float    crash;             /* seconds left of a crash                    */
    float    spin;              /* visual spin of a crash, radians            */
    int      gear;              /* 1..5                                       */
    float    rpm;               /* 0..1                                       */
    bool     offroad;
    float    cam_x;             /* the camera lags the car a little           */

    /* the race */
    int      state;             /* RS_*                                       */
    float    t_state;           /* seconds in this state                      */
    float    time_left;
    float    elapsed;           /* the score                                  */
    int      next_cp;
    float    cp_added;          /* seconds the last checkpoint gave           */
    float    split[8];          /* elapsed at each checkpoint                 */
    float    top_speed;
    int      crashes;
    int      passes;

    tb_traffic_t traffic[TB_TRAFFIC];
    int      ntraffic;
    uint32_t rng;
    uint32_t events;            /* EV_*, OR-ed until the UI takes them        */
    int      last_tick;

    /* the other watch's car (link), drawn as a ghost */
    bool     rival_on;
    float    rival_z, rival_x, rival_v;
    int      rival_car, rival_paint;
} tb_game_t;

void  tb_game_start(tb_game_t *g, const tb_track_t *trk, int car, int diff, uint32_t seed);
void  tb_game_step(tb_game_t *g, float dt);
/* where the car is on the stage, 0..1 (the finish is 1) */
float tb_game_progress(const tb_game_t *g);
/* the road's height and the curve under a distance, interpolated */
float tb_track_y(const tb_track_t *t, float z);
int   tb_game_kmh(const tb_game_t *g);
/* the road's half width where the car is */
float tb_road_hw(const tb_seg_t *s);
/* a bot sets the inputs: the lane with room, what the bend asks, brakes for it */
void  tb_game_bot(tb_game_t *g);
