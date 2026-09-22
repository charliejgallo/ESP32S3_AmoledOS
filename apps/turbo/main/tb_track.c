/*
 * TURBO - the stages (see tb_track.h)
 *
 * Tables are static and read through functions: a global table read from
 * another file goes through R_XTENSA_GLOB_DAT, whose addend the .so loader
 * dropped until v0.4.9 (Golf's trap).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
/* the tables leave out the fields that are zero (a section's flags, a
 * stage's ghosts): that is on purpose */
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "tb_track.h"
#include "tb_game.h"
#include "tb_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* a stretch of road: length, how it bends and climbs, and what stands beside it */
typedef struct {
    uint16_t len;           /* segments                                       */
    int8_t   curve;         /* curve units: + to the right                    */
    int8_t   hill;          /* metres gained over the section                 */
    uint8_t  deco;          /* the stage's decoration set                     */
    uint8_t  gl, gr;        /* ground left and right                          */
    uint8_t  lanes;
    uint8_t  flags;         /* SEC_*                                          */
} sec_t;

#define SEC_TUNNEL  0x01    /* the whole section is a tunnel: keep it flat     */

/* a decoration rule: every 'every' segments (+ jitter), on a side, at 'off'
 * metres beyond the road's edge (+ up to 'spread'), with a probability */
typedef struct {
    uint8_t kind;
    uint8_t every;
    uint8_t side;           /* 1 left, 2 right, 3 both, 4 random side, 0 span */
    uint8_t off;            /* metres beyond the edge                         */
    uint8_t spread;
    uint8_t prob;           /* percent                                        */
} rule_t;

#define RULES 5
typedef struct {
    rule_t r[RULES];
} deco_t;

/* a model of the traffic and its weight */
typedef struct {
    uint8_t model, weight;
} traffic_t;

typedef struct {
    const sec_t  *sec;
    int           nsec;
    const deco_t *deco;
    int           ndeco;
    int           ncp;          /* checkpoints including the finish           */
    float         start_time;
    float         cp_time;
    uint8_t       unlock;       /* UNL_*                                      */
    int8_t        after;        /* UNL_AFTER: finishing this stage opens it   */
    bool          in_tour;
    traffic_t     traffic[TB_TRAFFIC_KINDS];     /* weight 0 ends the list   */
    uint8_t       ghost_pct;    /* see-through traffic (Halloween)            */
} stage_def_t;

/* --------------------------------------------------------------------------
 * The five stages
 * -------------------------------------------------------------------------- */

#define C GR_CONCRETE
#define G GR_GRASS
#define S GR_SAND
#define W GR_SEA
#define N GR_SNOW
#define V GR_VOID
#define R GR_ROCK

static const sec_t CITY_SEC[] = {
    { 40,   0,   0, 0, G, G, 3 },
    { 80,   2,   0, 0, G, G, 3 },
    { 60,   0,   4, 1, G, G, 3 },
    { 90,  -4,  -4, 0, G, G, 3 },
    { 70,   0,   0, 2, C, C, 3 },
    { 80,   5,   6, 0, G, G, 3 },
    { 60,   0,  -6, 1, G, G, 3 },
    { 70,  -3,   0, 2, G, C, 3 },
    { 90,   0,   0, 1, C, C, 3 },
    { 80,   6,   3, 0, G, G, 3 },
    { 50,   0,  -3, 0, G, G, 3 },
    { 90,  -6,   0, 2, G, G, 3 },
    { 70,   0,   8, 1, G, G, 3 },
    { 60,   3,  -8, 0, G, G, 3 },
    { 80,  -2,   0, 2, C, C, 3 },
    { 60,   0,   0, 1, C, C, 3 },
    { 90,   5,   5, 0, G, G, 3 },
    { 70,  -5,  -5, 0, G, G, 3 },
    { 80,   0,   0, 1, G, G, 3 },
    { 60,   0,   0, 0, G, G, 3 },
};
static const deco_t CITY_DECO[] = {
    { { { PR_LAMP, 6, 3, 3, 0, 100 }, { PR_TREE_ROUND, 3, 4, 5, 10, 55 },
        { PR_TOWER_BRICK, 7, 4, 28, 30, 60 }, { PR_TOWER_GLASS, 9, 4, 30, 40, 60 },
        { PR_BILLBOARD, 40, 2, 6, 4, 70 } } },
    { { { PR_LAMP, 6, 3, 3, 0, 100 }, { PR_OVERPASS, 30, 0, 0, 0, 100 },
        { PR_TOWER_GLASS, 6, 4, 26, 30, 70 }, { PR_TREE_ROUND, 4, 4, 5, 8, 50 },
        { PR_BARRIER, 2, 3, 0, 0, 0 } } },
    { { { PR_LAMP, 6, 3, 3, 0, 100 }, { PR_GANTRY, 45, 0, 0, 0, 100 },
        { PR_TOWER_BRICK, 5, 4, 22, 30, 75 }, { PR_BILLBOARD, 25, 1, 6, 4, 80 },
        { PR_TREE_ROUND, 3, 4, 4, 12, 45 } } },
};

static const sec_t COAST_SEC[] = {
    { 40,   0,   0, 0, W, S, 3 },
    { 90,  -3,   2, 0, W, S, 3 },
    { 70,   5,   6, 1, W, R, 3 },
    { 60,   0,  -4, 0, W, S, 3 },
    { 90,  -6,   0, 2, W, S, 3 },
    { 70,   4,   8, 1, W, R, 3 },
    { 80,  -4, -10, 0, W, S, 3 },
    { 60,   0,   0, 2, S, S, 3 },
    { 90,   6,   4, 1, W, R, 3 },
    { 70,  -5,  -4, 0, W, S, 3 },
    { 80,   3,   0, 2, W, S, 3 },
    { 70,  -7,   6, 1, W, R, 3 },
    { 60,   0,  -6, 0, W, S, 3 },
    { 90,   4,   0, 2, W, S, 3 },
    { 70,  -3,   3, 0, W, S, 3 },
    { 80,   0,  -3, 0, W, S, 3 },
    { 60,   0,   0, 0, W, S, 3 },
};
static const deco_t COAST_DECO[] = {
    { { { PR_PALM, 4, 2, 3, 8, 80 }, { PR_PALM_TALL, 5, 1, 2, 3, 60 },
        { PR_GUARDRAIL, 2, 1, 1, 0, 100 }, { PR_HUT, 35, 2, 10, 6, 70 },
        { PR_ROCK_RED, 11, 2, 12, 20, 40 } } },
    { { { PR_CLIFF, 3, 2, 3, 6, 90 }, { PR_GUARDRAIL, 2, 1, 1, 0, 100 },
        { PR_PALM, 6, 2, 1, 2, 50 }, { PR_LIGHTHOUSE, 80, 1, 30, 10, 100 },
        { PR_CLIFF, 7, 2, 14, 10, 60 } } },
    { { { PR_PALM, 3, 3, 3, 6, 75 }, { PR_PALM_TALL, 4, 4, 6, 10, 60 },
        { PR_HUT, 20, 2, 8, 8, 70 }, { PR_GUARDRAIL, 2, 1, 1, 0, 100 },
        { PR_SIGN_CURVE, 0, 0, 0, 0, 0 } } },
};

static const sec_t DESERT_SEC[] = {
    { 40,   0,   0, 0, S, S, 3 },
    { 120,  0,  10, 0, S, S, 3 },
    { 80,   4, -10, 1, S, S, 3 },
    { 100,  0,  14, 0, S, S, 3 },
    { 70,  -5,  -6, 2, S, S, 3 },
    { 110,  0,  -8, 0, S, S, 3 },
    { 80,   6,  12, 1, S, S, 3 },
    { 90,   0, -12, 0, S, S, 3 },
    { 70,  -4,   6, 2, S, S, 3 },
    { 120,  0,  -6, 0, S, S, 3 },
    { 80,   5,  16, 1, S, S, 3 },
    { 70,  -6, -16, 1, S, S, 3 },
    { 100,  0,   0, 0, S, S, 3 },
    { 60,   0,   0, 0, S, S, 3 },
};
static const deco_t DESERT_DECO[] = {
    { { { PR_SAGUARO, 5, 4, 4, 20, 70 }, { PR_SAGUARO_S, 3, 4, 3, 15, 60 },
        { PR_BUTTE, 25, 4, 70, 90, 80 }, { PR_ROCK_RED, 7, 4, 5, 20, 50 },
        { PR_DINER, 120, 2, 8, 0, 100 } } },
    { { { PR_ROCK_RED, 3, 3, 2, 10, 70 }, { PR_BUTTE, 12, 4, 40, 60, 90 },
        { PR_DEAD_TREE, 6, 4, 4, 10, 60 }, { PR_SAGUARO_S, 4, 4, 3, 10, 50 },
        { PR_SIGN_CURVE, 0, 0, 0, 0, 0 } } },
    { { { PR_SAGUARO, 3, 3, 3, 12, 80 }, { PR_DEAD_TREE, 8, 4, 6, 10, 50 },
        { PR_BUTTE, 20, 4, 60, 80, 80 }, { PR_DINER, 60, 1, 8, 0, 100 },
        { PR_ROCK_RED, 9, 4, 4, 10, 60 } } },
};

static const sec_t MOUNTAIN_SEC[] = {
    { 40,   0,   0, 0, N, N, 2 },
    { 70,   5,  10, 0, N, N, 2 },
    { 60,  -7,  12, 1, N, N, 2 },
    { 50,   0,   4, 0, N, N, 2 },
    { 70,   8,  -8, 2, N, N, 2 },
    { 60,  -8, -10, 1, N, N, 2 },
    { 50,   6,   6, 0, N, N, 2 },
    { 80,   0,  14, 2, N, N, 2 },
    { 60,  -6,   8, 1, N, N, 2 },
    { 60,   9, -14, 0, N, N, 2 },
    { 50,  -5,  -6, 1, N, N, 2 },
    { 70,   0,  -4, 2, N, N, 2 },
    { 60,   7,  10, 0, N, N, 2 },
    { 70,  -9,  -8, 1, N, N, 2 },
    { 60,   4,   0, 0, N, N, 2 },
    { 50,   0,   0, 0, N, N, 2 },
};
static const deco_t MOUNTAIN_DECO[] = {
    { { { PR_PINE_SNOW, 3, 4, 3, 16, 70 }, { PR_PINE, 4, 4, 6, 20, 55 },
        { PR_LAMP_NIGHT, 8, 2, 3, 0, 100 }, { PR_SNOWBANK, 3, 3, 1, 1, 60 },
        { PR_CABIN, 40, 4, 12, 10, 80 } } },
    { { { PR_PINE_SNOW, 2, 4, 2, 10, 70 }, { PR_ROCK_SNOW, 5, 4, 2, 8, 50 },
        { PR_LAMP_NIGHT, 8, 1, 3, 0, 100 }, { PR_SNOWBANK, 2, 3, 1, 1, 70 },
        { PR_PINE, 4, 4, 10, 20, 60 } } },
    { { { PR_PINE, 3, 3, 3, 14, 70 }, { PR_CABIN, 25, 4, 10, 12, 80 },
        { PR_LAMP_NIGHT, 6, 3, 3, 0, 100 }, { PR_ROCK_SNOW, 5, 4, 3, 10, 50 },
        { PR_PINE_SNOW, 4, 4, 8, 20, 60 } } },
};

static const sec_t SPACE_SEC[] = {
    { 40,   0,   0, 0, V, V, 3 },
    { 80,   4,  12, 0, V, V, 3 },
    { 70,  -6, -18, 1, V, V, 3 },
    { 60,   0,  20, 2, V, V, 3 },
    { 80,   8, -14, 0, V, V, 3 },
    { 70,  -8,  10, 1, V, V, 3 },
    { 60,   0, -24, 2, V, V, 3 },
    { 90,   6,  16, 0, V, V, 3 },
    { 70,  -5,   0, 1, V, V, 3 },
    { 60,   0,  18, 2, V, V, 3 },
    { 80,   7, -20, 0, V, V, 3 },
    { 70,  -7,   6, 1, V, V, 3 },
    { 80,   0,  -6, 2, V, V, 3 },
    { 60,   0,   0, 0, V, V, 3 },
};
static const deco_t SPACE_DECO[] = {
    { { { PR_BEACON, 8, 3, 2, 0, 100 }, { PR_CRYSTAL, 5, 4, 6, 20, 60 },
        { PR_ASTEROID_A, 6, 4, 25, 60, 70 }, { PR_SATELLITE, 30, 4, 15, 20, 80 },
        { PR_RING_GATE, 40, 0, 0, 0, 100 } } },
    { { { PR_BEACON, 8, 3, 2, 0, 100 }, { PR_RING_GATE, 14, 0, 0, 0, 100 },
        { PR_ASTEROID_B, 4, 4, 15, 50, 70 }, { PR_CRYSTAL, 7, 4, 8, 20, 60 },
        { PR_ASTEROID_A, 9, 4, 40, 60, 70 } } },
    { { { PR_CRYSTAL, 3, 3, 3, 12, 80 }, { PR_BEACON, 8, 3, 2, 0, 100 },
        { PR_ASTEROID_B, 5, 4, 20, 40, 70 }, { PR_SATELLITE, 20, 4, 10, 20, 80 },
        { PR_ASTEROID_A, 7, 4, 30, 50, 60 } } },
};

/* v0.4.12: a night road through a haunted valley, two lanes */
static const sec_t HALLOWEEN_SEC[] = {
    { 40,   0,   0, 0, G, G, 2 },
    { 70,   4,   6, 1, G, G, 2 },
    { 60,  -6,  -4, 0, G, G, 2 },
    { 80,   0,   8, 2, G, G, 2 },
    { 60,   7,  -8, 1, G, G, 2 },
    { 70,  -5,   4, 0, G, G, 2 },
    { 60,   0,  -6, 2, G, G, 2 },
    { 70,   6,   6, 1, G, G, 2 },
    { 60,  -7,  -6, 0, G, G, 2 },
    { 80,   0,   0, 2, G, G, 2 },
    { 60,   5,   4, 1, G, G, 2 },
    { 70,  -4,  -4, 0, G, G, 2 },
    { 80,   3,   6, 2, G, G, 2 },
    { 60,   0,   0, 0, G, G, 2 },
};
static const deco_t HALLOWEEN_DECO[] = {
    /* haunted woods */
    { { { PR_DEAD_TWISTED, 1, 4, 2, 12, 80 }, { PR_GAS_LAMP, 6, 3, 3, 0, 100 },
        { PR_HAUNTED, 60, 4, 20, 12, 100 }, { PR_PUMPKINS, 3, 4, 1, 3, 70 },
        { PR_TOMBSTONES, 5, 4, 2, 6, 60 } } },
    /* the cemetery, on the left */
    { { { PR_CEM_FENCE, 2, 1, 1, 0, 100 }, { PR_TOMBSTONES, 1, 1, 3, 8, 90 },
        { PR_GAS_LAMP, 6, 2, 3, 0, 100 }, { PR_DEAD_TWISTED, 2, 4, 4, 10, 70 },
        { PR_HAUNTED, 80, 2, 22, 10, 100 } } },
    /* the pumpkin patch */
    { { { PR_PUMPKINS, 2, 4, 1, 6, 90 }, { PR_SCARECROW, 6, 4, 3, 5, 90 },
        { PR_DEAD_TWISTED, 3, 4, 5, 12, 70 }, { PR_GAS_LAMP, 6, 3, 3, 0, 100 },
        { PR_TOMBSTONES, 10, 4, 4, 6, 50 } } },
};

/* v0.4.12: an alpine gorge by day, with road tunnels (flat inside) */
static const sec_t TUNNELS_SEC[] = {
    { 40,   0,   0, 0, G, G, 3 },
    { 80,   3,   6, 0, G, R, 3 },
    { 60,   0,   0, 0, R, R, 3, SEC_TUNNEL },
    { 70,  -5,  -4, 1, R, R, 3 },
    { 80,   3,   0, 0, R, R, 3, SEC_TUNNEL },
    { 60,  -3,   6, 2, W, G, 3 },
    { 90,   0,  -4, 0, W, G, 3 },
    { 70,   0,   0, 0, R, R, 3, SEC_TUNNEL },
    { 60,   5,  -6, 1, R, R, 3 },
    { 80,  -6,   4, 2, W, G, 3 },
    { 100,  2,   0, 0, R, R, 3, SEC_TUNNEL },
    { 60,   4,   4, 1, G, R, 3 },
    { 70,  -4,  -4, 0, G, G, 3 },
    { 60,   0,   0, 0, G, G, 3 },
};
static const deco_t TUNNELS_DECO[] = {
    /* the valley */
    { { { PR_PINE_DAY, 3, 4, 4, 16, 75 }, { PR_ROCK_GRANITE, 7, 4, 6, 10, 60 },
        { PR_PYLON, 30, 2, 20, 10, 100 }, { PR_GUARDRAIL, 2, 1, 1, 0, 100 },
        { PR_PINE_DAY, 5, 4, 12, 20, 60 } } },
    /* the gorge */
    { { { PR_ROCK_GRANITE, 2, 3, 2, 4, 80 }, { PR_WATERFALL, 40, 2, 8, 4, 100 },
        { PR_PINE_DAY, 5, 4, 8, 10, 60 }, { PR_GUARDRAIL, 2, 1, 1, 0, 100 },
        { PR_ROCK_GRANITE, 6, 4, 12, 16, 60 } } },
    /* the lake shore: the reservoir on the left */
    { { { PR_GUARDRAIL, 2, 1, 1, 0, 100 }, { PR_PINE_DAY, 2, 2, 3, 12, 80 },
        { PR_PYLON, 25, 2, 25, 10, 100 }, { PR_ROCK_GRANITE, 9, 2, 6, 10, 50 },
        { PR_WATERFALL, 60, 2, 10, 4, 100 } } },
};

#undef C
#undef G
#undef S
#undef W
#undef N
#undef V
#undef R

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* The stages, in the order of the stage list. Adding one: its sections and
 * decoration above, a row here, its colours in theme_of(), its name in
 * tb_stage_name() and its backdrop's key in tb_art.c. The traffic lists only
 * the models that stage needs: only they are loaded for a race. */
static const stage_def_t *stage_table(int *n)
{
    static const stage_def_t t[STAGE_N] = {
        [STAGE_CITY] = { CITY_SEC, NELEM(CITY_SEC), CITY_DECO, NELEM(CITY_DECO), 5, 40, 26,
                         UNL_OPEN, -1, true,
                         { { VH_SEDAN, 30 }, { VH_COMPACT, 25 }, { VH_VAN, 18 }, { VH_TRUCK, 15 },
                           { CAR_WEDGE, 6 }, { CAR_MUSCLE, 6 } } },
        [STAGE_COAST] = { COAST_SEC, NELEM(COAST_SEC), COAST_DECO, NELEM(COAST_DECO), 5, 40, 26,
                          UNL_OPEN, -1, true,
                          { { VH_SEDAN, 30 }, { VH_COMPACT, 30 }, { VH_VAN, 20 },
                            { CAR_MUSCLE, 10 }, { CAR_WEDGE, 10 } } },
        [STAGE_DESERT] = { DESERT_SEC, NELEM(DESERT_SEC), DESERT_DECO, NELEM(DESERT_DECO), 5, 42, 28,
                           UNL_OPEN, -1, true,
                           { { VH_TRUCK, 25 }, { CAR_PICKUP, 20 }, { VH_SEDAN, 25 }, { VH_VAN, 15 },
                             { CAR_MUSCLE, 15 } } },
        [STAGE_MOUNTAIN] = { MOUNTAIN_SEC, NELEM(MOUNTAIN_SEC), MOUNTAIN_DECO, NELEM(MOUNTAIN_DECO), 5, 40, 25,
                             UNL_AFTER, STAGE_DESERT, true,
                             { { VH_SEDAN, 30 }, { VH_COMPACT, 25 }, { VH_VAN, 20 }, { CAR_PICKUP, 15 },
                               { CAR_RALLY, 10 } } },
        [STAGE_SPACE] = { SPACE_SEC, NELEM(SPACE_SEC), SPACE_DECO, NELEM(SPACE_DECO), 5, 40, 26,
                          UNL_TOUR, -1, true,
                          { { CAR_WEDGE, 35 }, { CAR_MUSCLE, 35 }, { CAR_RALLY, 30 } } },
        /* v0.4.12: open from the start, the season's stage; a third of the
         * traffic are ghosts the car drives through */
        [STAGE_HALLOWEEN] = { HALLOWEEN_SEC, NELEM(HALLOWEEN_SEC), HALLOWEEN_DECO, NELEM(HALLOWEEN_DECO), 5, 40, 25,
                              UNL_OPEN, -1, false,
                              { { VH_HEARSE, 30 }, { VH_SEDAN, 30 }, { VH_COMPACT, 20 }, { VH_VAN, 20 } }, 35 },
        [STAGE_TUNNELS] = { TUNNELS_SEC, NELEM(TUNNELS_SEC), TUNNELS_DECO, NELEM(TUNNELS_DECO), 5, 40, 26,
                            UNL_AFTER, STAGE_MOUNTAIN, false,
                            { { VH_SEDAN, 30 }, { VH_COMPACT, 25 }, { VH_VAN, 20 }, { CAR_RALLY, 15 },
                              { CAR_WEDGE, 10 } } },
    };
    *n = STAGE_N;
    return t;
}

static bool stage_def(int stage, stage_def_t *d)
{
    int n;
    const stage_def_t *t = stage_table(&n);
    if (stage < 0 || stage >= n) return false;
    *d = t[stage];
    return true;
}

int tb_stage_unlock(int stage, int *after)
{
    stage_def_t d;
    if (!stage_def(stage, &d)) return UNL_OPEN;
    if (after) *after = d.after;
    return d.unlock;
}

uint32_t tb_stage_open_mask(void)
{
    uint32_t m = 0;
    for (int s = 0; s < STAGE_N; s++) {
        if (tb_stage_unlock(s, NULL) == UNL_OPEN) m |= 1u << s;
    }
    return m;
}

int tb_stage_order(int i)
{
    /* the open ones first, then as they open */
    static const int8_t o[STAGE_N] = { STAGE_CITY, STAGE_COAST, STAGE_DESERT, STAGE_HALLOWEEN,
                                       STAGE_MOUNTAIN, STAGE_TUNNELS, STAGE_SPACE };
    return i >= 0 && i < STAGE_N ? o[i] : 0;
}

int tb_tour_len(void)
{
    int n = 0;
    stage_def_t d;
    for (int s = 0; s < STAGE_N; s++) {
        if (stage_def(s, &d) && d.in_tour) n++;
    }
    return n;
}

int tb_tour_stage(int i)
{
    stage_def_t d;
    for (int s = 0; s < STAGE_N; s++) {
        if (stage_def(s, &d) && d.in_tour && i-- == 0) return s;
    }
    return -1;
}

uint32_t tb_track_vehicles(const tb_track_t *t)
{
    uint32_t m = 0;
    for (int i = 0; i < t->ntraffic_kinds; i++) m |= 1u << t->traffic_model[i];
    return m;
}

const char *tb_stage_name(int stage)
{
    switch (stage) {
    case STAGE_CITY:     return "Metro Freeway";
    case STAGE_COAST:    return "Costa Azul";
    case STAGE_DESERT:   return "Red Canyon";
    case STAGE_MOUNTAIN: return "Snow Pass";
    case STAGE_SPACE:    return "Orbit 9";
    case STAGE_HALLOWEEN: return "Hollow Road";
    case STAGE_TUNNELS:  return "Tunnel Ridge";
    default:             return "?";
    }
}

/* --------------------------------------------------------------------------
 * Colours of each stage
 * -------------------------------------------------------------------------- */

static void theme_of(int stage, tb_theme_t *th)
{
    memset(th, 0, sizeof(*th));
    th->road[0] = 0x6B6E73; th->road[1] = 0x64676C;
    th->line = 0xE8E8E0;
    th->rumble[0] = 0xD8D8D8; th->rumble[1] = 0xC4312B;
    th->ground[GR_GRASS][0] = 0x4E9A3A;    th->ground[GR_GRASS][1] = 0x468C34;
    th->ground[GR_SAND][0] = 0xE2C68E;     th->ground[GR_SAND][1] = 0xD8BA80;
    th->ground[GR_SEA][0] = 0x2A7FB8;      th->ground[GR_SEA][1] = 0x2877B0;
    th->ground[GR_SNOW][0] = 0xE6EEF6;     th->ground[GR_SNOW][1] = 0xD8E2EE;
    th->ground[GR_VOID][0] = 0x000000;     th->ground[GR_VOID][1] = 0x000000;
    th->ground[GR_CONCRETE][0] = 0xA8A49A; th->ground[GR_CONCRETE][1] = 0x9E9A90;
    th->ground[GR_ROCK][0] = 0x8C7A66;     th->ground[GR_ROCK][1] = 0x83715E;
    th->fog_start = 40;
    /* the colours come from tools/blender's sky.json, matched to the renders */
    switch (stage) {
    case STAGE_CITY:
        th->sky_top = 0x3F78CC; th->sky_hor = 0xC6D8EC; th->fog = 0xB8CBE0;
        th->ground[GR_GRASS][0] = 0x5C9A3C; th->ground[GR_GRASS][1] = 0x548F36;
        th->ground[GR_CONCRETE][0] = 0x9A968C; th->ground[GR_CONCRETE][1] = 0x928E84;
        th->road[0] = 0x6C6C70; th->road[1] = 0x666669;
        th->rumble[0] = 0xD82020; th->rumble[1] = 0xF0F0F0;
        th->line = 0xF2F2F2;
        break;
    case STAGE_COAST:
        th->sky_top = 0x2F86D8; th->sky_hor = 0xC8E2F4; th->fog = 0xC4D8EA;
        th->ground[GR_SAND][0] = 0xE8D4A0; th->ground[GR_SAND][1] = 0xDCC690;
        th->ground[GR_SEA][0] = 0x2A78B8; th->ground[GR_SEA][1] = 0x2872B0;
        th->road[0] = 0x707074; th->road[1] = 0x6A6A6E;
        th->rumble[0] = 0xF0F0F0; th->rumble[1] = 0x2A70D0;
        th->line = 0xF2F2F2;
        break;
    case STAGE_DESERT:
        th->sky_top = 0x3A7AD0; th->sky_hor = 0xF0D8B8; th->fog = 0xF0C8A0;
        th->ground[GR_SAND][0] = 0xD89A58; th->ground[GR_SAND][1] = 0xCC8E50;
        th->road[0] = 0x6A6664; th->road[1] = 0x64605E;
        th->rumble[0] = 0xE02020; th->rumble[1] = 0xF0F0F0;
        th->line = 0xF4D040;
        break;
    case STAGE_MOUNTAIN:
        th->sky_top = 0x050A1C; th->sky_hor = 0x1C2C50; th->fog = 0x1C2C50;
        th->ground[GR_SNOW][0] = 0x8AA0C8; th->ground[GR_SNOW][1] = 0x7E94BC;
        th->road[0] = 0x2A2E3A; th->road[1] = 0x262A34;
        th->rumble[0] = 0xC02030; th->rumble[1] = 0xC8D0E0;
        th->line = 0xE0D8A0;
        th->night = true;
        th->stars = true;
        th->fog_start = 20;
        break;
    case STAGE_HALLOWEEN:
        th->sky_top = 0x0C0620; th->sky_hor = 0x4A2A6E; th->fog = 0x3A2460;
        th->ground[GR_GRASS][0] = 0x3A3822; th->ground[GR_GRASS][1] = 0x33311D;
        th->road[0] = 0x2C2A32; th->road[1] = 0x28262E;
        th->rumble[0] = 0xF07818; th->rumble[1] = 0x6A2A9A;
        th->line = 0xD8D0B0;
        th->night = true;
        th->stars = true;
        th->fog_start = 16;
        th->bg_start = 344;                     /* the moon and the castle ahead */
        break;
    case STAGE_TUNNELS:
        th->sky_top = 0x2E70CC; th->sky_hor = 0xC4DCF0; th->fog = 0xB8CCE0;
        th->ground[GR_GRASS][0] = 0x5A8E3C; th->ground[GR_GRASS][1] = 0x528436;
        th->ground[GR_ROCK][0] = 0x8A8984; th->ground[GR_ROCK][1] = 0x82817C;
        th->ground[GR_SEA][0] = 0x2E6E8C; th->ground[GR_SEA][1] = 0x2C6886;
        th->road[0] = 0x68686C; th->road[1] = 0x626266;
        th->rumble[0] = 0xF0F0F0; th->rumble[1] = 0xD82020;
        th->line = 0xF2F2F2;
        th->tunnel_wall = 0xBAB4A6; th->tunnel_ceiling = 0x6C6860; th->tunnel_lamp = 0xFFA844;
        th->bg_start = 328;                     /* the dam ahead */
        break;
    case STAGE_SPACE:
        th->sky_top = 0x05020E; th->sky_hor = 0x1A0A36; th->fog = 0x1A0A36;
        th->road[0] = 0x2A2440; th->road[1] = 0x242038;
        th->rumble[0] = 0xFF30D0; th->rumble[1] = 0x30E0FF;
        th->line = 0x30E0FF;
        th->stars = true;
        th->neon = true;
        th->fog_start = 60;
        break;
    }
}

/* --------------------------------------------------------------------------
 * Building
 * -------------------------------------------------------------------------- */

float tb_prop_halfw(int kind)
{
    switch (kind) {
    case PR_LAMP: case PR_LAMP_NIGHT: case PR_BEACON: case PR_SIGN_CURVE: return 0.3f;
    case PR_CONE: return 0.3f;
    case PR_BARRIER: case PR_GUARDRAIL: return 0.3f;
    case PR_SNOWBANK: return 0.8f;
    case PR_TREE_ROUND: case PR_PALM: case PR_PALM_TALL: case PR_PINE: case PR_PINE_SNOW: return 0.6f;
    case PR_SAGUARO: case PR_SAGUARO_S: case PR_DEAD_TREE: return 0.5f;
    case PR_ROCK_RED: case PR_ROCK_SNOW: case PR_CRYSTAL: case PR_ROCK_GRANITE: return 1.2f;
    case PR_GAS_LAMP: case PR_SCARECROW: return 0.3f;
    case PR_CEM_FENCE: return 0.3f;
    case PR_PUMPKINS: case PR_TOMBSTONES: return 0.8f;
    case PR_DEAD_TWISTED: case PR_PINE_DAY: return 0.6f;
    case PR_BILLBOARD: case PR_DINER: return 0.4f;
    default: return 2.0f;
    }
}

static bool is_solid(int kind)
{
    switch (kind) {
    case PR_OVERPASS: case PR_GANTRY: case PR_CHECKPOINT: case PR_FINISH: case PR_RING_GATE:
    case PR_BUTTE: case PR_TOWER_BRICK: case PR_TOWER_GLASS: case PR_LIGHTHOUSE:
    case PR_ASTEROID_A: case PR_ASTEROID_B: case PR_SATELLITE:
    case PR_HAUNTED: case PR_PORTAL: case PR_WATERFALL: case PR_PYLON:
        return false;           /* spans, or so far out nobody reaches them */
    default:
        return true;
    }
}

static int add_prop(tb_track_t *t, int cap, int seg, int kind, float x, int flags)
{
    if (t->nprop >= cap) return -1;
    tb_seg_t *s = &t->seg[seg];
    if (s->nprop == 0) s->prop0 = (uint16_t)t->nprop;
    else if (s->prop0 + s->nprop != t->nprop) return -1;   /* only while building that segment */
    if (s->nprop >= 255) return -1;
    tb_prop_t *p = &t->prop[t->nprop++];
    p->kind = (uint8_t)kind;
    p->x10 = (int16_t)(x * 10.0f);
    p->flags = (uint8_t)flags;
    if (is_solid(kind) && !(flags & PF_SPAN)) p->flags |= PF_SOLID;
    s->nprop++;
    return t->nprop - 1;
}

static float ease_in_out(float a, float b, float t)
{
    return a + (b - a) * ((-cosf(t * 3.14159265f) / 2.0f) + 0.5f);
}

bool tb_track_build(tb_track_t *t, int stage)
{
    stage_def_t d;
    memset(t, 0, sizeof(*t));
    if (!stage_def(stage, &d)) return false;
    t->stage = stage;
    t->ghost_pct = d.ghost_pct;
    theme_of(stage, &t->theme);
    for (int i = 0; i < TB_TRAFFIC_KINDS && d.traffic[i].weight; i++) {
        t->traffic_model[i] = d.traffic[i].model;
        t->traffic_weight[i] = d.traffic[i].weight;
        t->ntraffic_kinds = i + 1;
    }
    /* past the finish the road runs on straight, so the view never ends */
    int nreal = 0;
    for (int i = 0; i < d.nsec; i++) nreal += d.sec[i].len;
    int n = nreal + TB_TAIL_SEGS;
    int cap = n * 3;
    t->seg = (tb_seg_t *)tb_calloc((size_t)n, sizeof(tb_seg_t));
    t->prop = (tb_prop_t *)tb_calloc((size_t)cap, sizeof(tb_prop_t));
    if (!t->seg || !t->prop) {
        tb_track_free(t);
        return false;
    }
    t->nseg = n;

    /* the shape */
    uint32_t rng = 0x1234567u + (uint32_t)stage * 7919u;
    float y = 0;
    int k = 0;
    for (int i = 0; i < d.nsec; i++) {
        const sec_t *s = &d.sec[i];
        int len = s->len;
        int enter = len / 4, leave = len / 4;
        float c = (float)s->curve * TB_CURVE_UNIT;
        for (int j = 0; j < len; j++, k++) {
            tb_seg_t *g = &t->seg[k];
            float tc = 1.0f;
            if (j < enter) tc = ease_in_out(0, 1, (float)j / (float)enter);
            else if (j >= len - leave) tc = ease_in_out(1, 0, (float)(j - (len - leave)) / (float)leave);
            g->curve = c * tc;
            g->y = ease_in_out(y, y + (float)s->hill, (float)j / (float)len);
            g->gl = s->gl;
            g->gr = s->gr;
            g->lanes = s->lanes;
            if (s->flags & SEC_TUNNEL) g->flags |= SF_TUNNEL;
            if ((k / 3) & 1) g->flags |= SF_DARK;
        }
        if ((s->flags & SEC_TUNNEL) && t->ntunnels < TB_TUNNELS) {
            t->tunnel_s[t->ntunnels] = (int16_t)(k - len);
            t->tunnel_e[t->ntunnels] = (int16_t)k;
            t->ntunnels++;
        }
        y += (float)s->hill;
    }
    for (; k < n; k++) {
        tb_seg_t *g = &t->seg[k];
        *g = t->seg[k - 1];
        g->curve = 0;
        g->y = y;
        g->flags = ((k / 3) & 1) ? SF_DARK : 0;
    }
    t->nreal = nreal;

    /* checkpoints: evenly along the stage, the last one is the finish */
    t->ncp = d.ncp;
    t->start_time = d.start_time;
    int first = 30;
    for (int i = 0; i < d.ncp; i++) {
        int seg = first + (nreal - first - 20) * (i + 1) / d.ncp;
        t->cp_seg[i] = seg;
        t->cp_time[i] = d.cp_time;
        t->seg[seg].flags |= (i == d.ncp - 1) ? SF_FINISH : SF_CHECKPOINT;
    }
    t->seg[8].flags |= SF_START;
    int cpi = 0;
    for (int i = 0; i < n; i++) {
        while (cpi < d.ncp - 1 && i > t->cp_seg[cpi]) cpi++;
        t->seg[i].cp = (uint8_t)cpi;
    }

    /* the props, segment by segment (a segment's props are contiguous) */
    k = 0;
    int next[8][RULES];
    for (int i = 0; i < 8; i++) for (int r = 0; r < RULES; r++) next[i][r] = 0;
    for (int i = 0; i < d.nsec; i++) {
        const sec_t *s = &d.sec[i];
        const deco_t *dc = &d.deco[s->deco % d.ndeco];
        for (int j = 0; j < s->len; j++, k++) {
            tb_seg_t *g = &t->seg[k];
            float edge = (g->lanes == 2 ? TB_LANE_W : TB_ROAD_HW) + TB_RUMBLE_W;
            if (g->flags & SF_TUNNEL) {
                /* inside a tunnel nothing stands beside the road: the portal
                 * at its mouth, and the watch draws the walls and the ceiling */
                if (j == 0) add_prop(t, cap, k, PR_PORTAL, 0, PF_SPAN);
                continue;
            }
            if (g->flags & SF_START) add_prop(t, cap, k, PR_FINISH, 0, PF_SPAN);
            if (g->flags & SF_CHECKPOINT) add_prop(t, cap, k, PR_CHECKPOINT, 0, PF_SPAN);
            if (g->flags & SF_FINISH) add_prop(t, cap, k, PR_FINISH, 0, PF_SPAN);
            /* chevrons on the outside of a sharp bend, as it begins */
            if (j >= 2 && j < s->len / 3 && (j % 4) == 2 && abs(s->curve) >= 5) {
                bool right = s->curve > 0;
                /* the curve goes right: the sign stands on the left, pointing right */
                add_prop(t, cap, k, PR_SIGN_CURVE, right ? -(edge + 1.5f) : (edge + 1.5f), right ? PF_MIRROR : 0);
            }
            if (k < 12 || (g->flags & (SF_CHECKPOINT | SF_FINISH | SF_START))) continue;
            for (int r = 0; r < RULES; r++) {
                const rule_t *ru = &dc->r[r];
                if (!ru->every || !ru->prob) continue;
                int *nx = &next[s->deco % 8][r];
                if (k < *nx) continue;
                *nx = k + ru->every + (ru->every > 3 ? (int)(tb_rand(&rng) % (uint32_t)(ru->every / 3 + 1)) : 0);
                if ((int)(tb_rand(&rng) % 100u) >= ru->prob) continue;
                if (ru->side == 0) {
                    add_prop(t, cap, k, ru->kind, 0, PF_SPAN);
                    continue;
                }
                for (int side = 0; side < 2; side++) {
                    bool left = side == 0;
                    if (ru->side == 1 && !left) continue;
                    if (ru->side == 2 && left) continue;
                    if (ru->side == 4 && (tb_rand(&rng) & 1) != (unsigned)side) continue;
                    float off = edge + (float)ru->off + tb_randf(&rng) * (float)ru->spread;
                    /* the sea side keeps only what belongs at the water's edge */
                    uint8_t gnd = left ? g->gl : g->gr;
                    if (gnd == GR_SEA && ru->kind != PR_GUARDRAIL && ru->kind != PR_LIGHTHOUSE && ru->kind != PR_PALM_TALL) continue;
                    add_prop(t, cap, k, ru->kind, left ? -off : off, left ? PF_MIRROR : 0);
                }
            }
        }
    }
    return true;
}

void tb_track_free(tb_track_t *t)
{
    free(t->seg);
    free(t->prop);
    t->seg = NULL;
    t->prop = NULL;
    t->nseg = t->nprop = 0;
}
