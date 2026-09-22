/*
 * TURBO - the stages: a road made of 5 m segments
 *
 * Each stage is data: a list of sections (length, curve, hill, what stands
 * beside the road). tb_track_build() turns it into segments, the unit the
 * renderer projects and the physics walks: every segment knows its curve
 * (how much the road bends there), its height, its ground on each side and
 * the props that stand on it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TB_SEG_LEN      5.0f        /* metres per segment                     */
#define TB_ROAD_HW      6.0f        /* half the asphalt: three 3.6 m lanes    */
#define TB_LANE_W       3.6f
#define TB_RUMBLE_W     0.8f
#define TB_TAIL_SEGS    180         /* straight road after the finish          */
#define TB_CURVE_UNIT   0.01f       /* curve 1 = 0.01 m per segment^2 (R 2500 m) */

enum {
    STAGE_CITY = 0,
    STAGE_COAST,
    STAGE_DESERT,
    STAGE_MOUNTAIN,
    STAGE_SPACE,
    STAGE_HALLOWEEN,            /* v0.4.12 */
    STAGE_TUNNELS,              /* v0.4.12 */
    STAGE_N
};

/* how a stage opens in the time trial */
enum { UNL_OPEN = 0, UNL_AFTER, UNL_TOUR };

#define TB_TRAFFIC_KINDS 8      /* vehicle models one stage's traffic uses   */

/* what lies beside the road */
enum { GR_GRASS = 0, GR_SAND, GR_SEA, GR_SNOW, GR_VOID, GR_CONCRETE, GR_ROCK, GR_N };

/* the props: the pack's names follow this order (tb_art.c) */
enum {
    PR_CHECKPOINT = 0, PR_FINISH, PR_CONE, PR_SIGN_CURVE, PR_BARRIER,
    /* city */
    PR_OVERPASS, PR_LAMP, PR_TOWER_BRICK, PR_TOWER_GLASS, PR_TREE_ROUND, PR_BILLBOARD, PR_GANTRY,
    /* coast */
    PR_PALM, PR_PALM_TALL, PR_CLIFF, PR_LIGHTHOUSE, PR_HUT, PR_GUARDRAIL,
    /* desert */
    PR_SAGUARO, PR_SAGUARO_S, PR_BUTTE, PR_ROCK_RED, PR_DEAD_TREE, PR_DINER,
    /* mountain */
    PR_PINE_SNOW, PR_PINE, PR_ROCK_SNOW, PR_SNOWBANK, PR_CABIN, PR_LAMP_NIGHT,
    /* space */
    PR_ASTEROID_A, PR_ASTEROID_B, PR_CRYSTAL, PR_RING_GATE, PR_SATELLITE, PR_BEACON,
    /* halloween */
    PR_DEAD_TWISTED, PR_PUMPKINS, PR_TOMBSTONES, PR_CEM_FENCE, PR_SCARECROW, PR_HAUNTED, PR_GAS_LAMP,
    /* tunnels */
    PR_PORTAL, PR_ROCK_GRANITE, PR_WATERFALL, PR_PYLON, PR_PINE_DAY,
    PR_N
};

/* segment flags */
#define SF_CHECKPOINT   0x01
#define SF_FINISH       0x02
#define SF_START        0x04
#define SF_DARK         0x08        /* alternate shade band               */
#define SF_TUNNEL       0x10        /* inside a tunnel: walls, a ceiling  */

#define TB_TUNNEL_HW    8.0f        /* tunnel walls, metres from the centre */
#define TB_TUNNEL_H     7.0f        /* its ceiling over the road          */
#define TB_TUNNELS      8           /* at most per stage                  */

typedef struct {
    uint8_t  kind;              /* PR_*                                      */
    uint8_t  flags;             /* PF_*                                      */
    int16_t  x10;               /* centre, decimetres from the road's centre */
} tb_prop_t;

#define PF_MIRROR   0x01        /* drawn mirrored (left side of the road)    */
#define PF_SPAN     0x02        /* spans the road: drive under it            */
#define PF_SOLID    0x04        /* hitting it stops the car                  */

typedef struct {
    float    curve;             /* lateral acceleration of the road, m/seg^2 */
    float    y;                 /* height at the segment's start, m          */
    uint16_t prop0;             /* first prop in the track's list            */
    uint8_t  nprop;
    uint8_t  flags;             /* SF_*                                      */
    uint8_t  gl, gr;            /* ground on the left and right (GR_*)       */
    uint8_t  lanes;             /* 2 or 3 (a narrower road)                  */
    uint8_t  cp;                /* index of the checkpoint this leg ends at  */
} tb_seg_t;

/* a stage's colours (8-bit RGB) */
typedef struct {
    uint32_t sky_top, sky_hor;  /* gradient                                  */
    uint32_t fog;
    uint32_t road[2];           /* asphalt, the two alternating bands        */
    uint32_t line;              /* lane dashes and edge line                 */
    uint32_t rumble[2];
    uint32_t ground[GR_N][2];   /* each kind of ground, two bands            */
    uint8_t  fog_start;         /* segments before the fog starts             */
    bool     night;             /* headlights, dark ground                   */
    bool     stars;             /* draw stars in the sky                     */
    bool     neon;              /* glowing road edges (space)                */
    uint32_t tunnel_wall, tunnel_ceiling, tunnel_lamp;
    uint16_t bg_start;          /* backdrop column at the left edge at start  */
} tb_theme_t;

typedef struct {
    tb_seg_t  *seg;
    int        nseg;
    int        nreal;           /* without the tail after the finish          */
    tb_prop_t *prop;
    int        nprop;
    int        cp_seg[8];       /* checkpoint segments, the last is the finish */
    int        ncp;
    float      cp_time[8];      /* seconds added at each one (normal)         */
    float      start_time;
    int        stage;
    tb_theme_t theme;
    /* the traffic: which vehicle models (VH_* / CAR_*) and how often; only
     * these are loaded for a race (tb_track_vehicles) */
    uint8_t    traffic_model[TB_TRAFFIC_KINDS];
    uint8_t    traffic_weight[TB_TRAFFIC_KINDS];
    int        ntraffic_kinds;
    uint8_t    ghost_pct;       /* traffic that is a ghost (see-through, no hit) */
    /* the tunnels, as segment ranges [start, end) */
    int16_t    tunnel_s[TB_TUNNELS], tunnel_e[TB_TUNNELS];
    int        ntunnels;
} tb_track_t;

const char *tb_stage_name(int stage);           /* proper name, not translated */
/* how the stage opens: UNL_OPEN, UNL_AFTER (finishing *after* opens it) or
 * UNL_TOUR (finishing the tour) */
int  tb_stage_unlock(int stage, int *after);
uint32_t tb_stage_open_mask(void);              /* the stages open from the start */
/* the stage list's order (not the stages' numbers, which key the records) */
int  tb_stage_order(int i);
/* the tour: its stages in order */
int  tb_tour_len(void);
int  tb_tour_stage(int i);
/* the vehicle models a track's traffic uses, as a bit mask (1 << model) */
uint32_t tb_track_vehicles(const tb_track_t *t);
/* builds a stage; false if out of memory */
bool tb_track_build(tb_track_t *t, int stage);
void tb_track_free(tb_track_t *t);

static inline const tb_seg_t *tb_seg(const tb_track_t *t, int i)
{
    if (i < 0) i = 0;
    if (i >= t->nseg) i = t->nseg - 1;
    return &t->seg[i];
}

/* the prop's size in the world, for collisions (metres, half width) */
float tb_prop_halfw(int kind);
