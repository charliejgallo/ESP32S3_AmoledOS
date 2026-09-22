/*
 * GOLF - the course: holes as data, and the terrain built from them
 *
 * A hole is DATA (gf_hole_t): shapes drawn with a handful of control points
 * (fairways, greens, bunkers, water, forest, paths), single trees, mounds and
 * a few numbers for the ground (overall rise, roughness, the green's tilt).
 * Loading a hole (gf_world_load) turns that into everything the game needs:
 *
 *   - smooth polygons (closed Catmull-Rom through the control points), plus
 *     the ones derived from them: the fringe around each green and the first
 *     cut around each fairway;
 *   - a HEIGHT grid, 1 m per cell, where greens are tilted planes, bunkers
 *     are hollows, water lies low and flat and fairways are smoother than the
 *     rough around them;
 *   - a normal per cell, for the light in both views and for the roll;
 *   - the tree list, single trees plus the forests filled in.
 *
 * Nothing here knows about LVGL or the HAL: the harness renders holes to
 * image files on the Mac with plain cc.
 *
 * Units: metres, float. Hole coordinates: x across, y along the hole, the tee
 * near y = 0 and the green at the far end. Distances shown to the player are
 * converted to yards (or kept in metres) by the app.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * Hole data, as written in gf_holes.c
 * -------------------------------------------------------------------------- */

typedef struct {
    int16_t x, y;                   /* metres                                */
} gf_pt_t;

enum {
    SH_FAIRWAY = 0,
    SH_GREEN,
    SH_BUNKER,
    SH_WATER,
    SH_TEE,         /* a tee box: drawn from its control points like the rest */
    SH_DEEP,        /* deep rough, long yellowish grass                       */
    SH_FOREST,      /* an area filled with trees at load time                 */
    SH_PATH,        /* an OPEN polyline, 2.4 m wide                           */
    SH_WASTE,       /* sandy waste area with scrub: a lie like a bunker, flat */
    SH_KINDS,
};

typedef struct {
    uint8_t        kind;            /* SH_*                                   */
    uint8_t        n;               /* control points                         */
    uint8_t        param;           /* SH_FOREST: density 1..10; SH_BUNKER:
                                       depth in decimetres (0 = default)      */
    uint8_t        tree;            /* SH_FOREST: tree kind mix (GF_TREE_*)   */
    const gf_pt_t *p;
} gf_shape_t;

enum {
    GF_TREE_PINE = 0,
    GF_TREE_OAK,
    GF_TREE_POPLAR,
    GF_TREE_PALM,
    GF_TREE_BUSH,
    GF_TREE_KINDS,
    GF_TREE_MIXED = 0xFF,           /* a forest of pines, oaks and poplars    */
};

typedef struct {
    int16_t x, y;
    uint8_t kind;                   /* GF_TREE_*                              */
    uint8_t size;                   /* percent of the kind's normal size      */
} gf_tree_def_t;

typedef struct {
    int16_t x, y;
    int16_t r;                      /* metres                                 */
    int16_t h;                      /* centimetres, negative = a dip          */
} gf_mound_t;

typedef struct {
    const char          *name;      /* shown as it is: a proper name          */
    uint8_t              par;
    gf_pt_t              tee[3];    /* back, middle, front                    */
    gf_pt_t              pin[3];    /* three pin positions on the green       */
    int16_t              x0, y0, x1, y1;   /* the playing area; beyond is OB  */

    int16_t              rise;      /* cm the ground climbs from tee to green */
    uint8_t              rough;     /* ground roughness, 0..10                */
    uint8_t              seed;
    int8_t               green_tx;  /* the green's tilt, 1/10 % along x      */
    int8_t               green_ty;  /*                        and along y    */
    int8_t               green_bump;/* one soft ridge across it, cm           */
    uint8_t              flags;

    const gf_shape_t    *shapes;
    uint8_t              nshapes;
    const gf_tree_def_t *trees;
    uint8_t              ntrees;
    const gf_mound_t    *mounds;
    uint8_t              nmounds;
} gf_hole_t;

/* A course's look: its greens and sands, its water, its horizon. The theme
 * travels in every hole's 'flags' (low bits) so a loaded world knows it. */
enum {
    THEME_WOODS = 0,    /* parkland: pines and oaks, hills on the horizon     */
    THEME_COAST,        /* links by the sea: straw rough, dunes, the sea all
                           the way to the horizon, wind                       */
    THEME_LAKES,        /* a park of lakes: lush grass, green water, calm     */
    THEME_N,
};
#define GF_THEME(h) ((h)->flags & 3)

typedef struct {
    const char       *name;
    uint8_t           nholes;
    const gf_hole_t  *holes;
    uint8_t           theme;
    uint8_t           wind10;       /* the wind, x/10 of the usual            */
} gf_course_t;

/* The courses (functions, not extern data: see the note in gf_outfit.h).
 * gf_course() is the one being played; gf_course_select() picks it. */
int                gf_course_n(void);
const gf_course_t *gf_course_get(int i);
const gf_course_t *gf_course(void);
void               gf_course_select(int i);
int                gf_course_index(void);

/* --------------------------------------------------------------------------
 * Lies: what the ball sits on
 * -------------------------------------------------------------------------- */

enum {
    LIE_ROUGH = 0,
    LIE_FAIRWAY,
    LIE_FIRST,      /* the first cut, around the fairway                      */
    LIE_TEE,
    LIE_FRINGE,
    LIE_GREEN,
    LIE_BUNKER,
    LIE_WATER,
    LIE_PATH,
    LIE_DEEP,
    LIE_FOREST,     /* rough under trees                                      */
    LIE_WASTE,
    LIE_OB,         /* out of bounds                                          */
    LIE_KINDS,
};

/* --------------------------------------------------------------------------
 * The loaded hole
 * -------------------------------------------------------------------------- */

#define GF_MAX_POLYS     48
#define GF_MAX_PTS       2600      /* all polygon points of a hole together  */
#define GF_MAX_TREES     2000
#define GF_CELL          1.0f      /* metres per height cell                 */

typedef struct {
    uint8_t  kind;                  /* SH_*, or GF_POLY_FRINGE / _FIRST       */
    uint8_t  src;                   /* index of the shape it came from        */
    uint16_t first, n;              /* into the point pool                    */
    float    x0, y0, x1, y1;        /* bounding box                           */
    float    param;
} gf_poly_t;

#define GF_POLY_FRINGE  (SH_KINDS + 0)
#define GF_POLY_FIRST   (SH_KINDS + 1)
/* water with param 1: a lake that land sits on (an island green). Painted
 * under the land layers, and a lie below them. */
#define GF_POLY_LAKE    (SH_KINDS + 2)
#define GF_IS_WATER(k)  ((k) == SH_WATER || (k) == GF_POLY_LAKE)

typedef struct {
    float   x, y;
    float   h;                      /* ground height at the trunk             */
    float   height;                 /* metres, top of the canopy              */
    float   radius;                 /* canopy radius                          */
    uint8_t kind;
    uint8_t shade;                  /* per-tree tint variation, 0..15         */
} gf_tree_t;

typedef struct {
    const gf_hole_t *def;
    int              tee_i, pin_i;
    float            tee_x, tee_y, pin_x, pin_y;
    float            length;        /* tee to pin in a straight line, metres  */

    /* the ground grid covers [gx0, gx0 + gw) x [gy0, gy0 + gh) metres */
    float            gx0, gy0;
    int              gw, gh;
    int16_t         *height;        /* centimetres                            */
    int8_t          *nrm;           /* 2 per cell: dh/dx, dh/dy in 1/64 units */

    gf_poly_t        poly[GF_MAX_POLYS];
    int              npoly;
    float           *pts;           /* x, y pairs: GF_MAX_PTS of them         */
    float           *inv_l2;        /* per point: 1 / squared length of its edge */
    int              npts;

    gf_tree_t       *trees;
    int              ntrees;
    /* a coarse grid of tree indices, for collisions and culling */
    uint16_t        *tcell_first;   /* per 16 m cell: first index into tidx   */
    uint16_t        *tidx;
    int              tcw, tch;

    float            water_level;   /* height of the hole's water, if any     */
    uint32_t         gen;           /* bumped by every load: caches key on it  */
    int              cells_max;     /* what the grid buffers hold              */
    float            green_cx, green_cy;
} gf_world_t;

/* The ground grid a hole needs, in cells */
void  gf_world_grid_size(const gf_hole_t *h, int *gw, int *gh);
/* Allocates the buffers once, for 'cells' cells: the biggest hole of every
 * course (PSRAM on the board); false when out of memory. */
bool  gf_world_init(gf_world_t *w, int cells);
void  gf_world_free(gf_world_t *w);
/* Builds everything for a hole. tee_i / pin_i pick among the three. */
void  gf_world_load(gf_world_t *w, const gf_hole_t *h, int tee_i, int pin_i);

/* Queries, at any point in metres */
float gf_height(const gf_world_t *w, float x, float y);
void  gf_normal(const gf_world_t *w, float x, float y, float *nx, float *ny);
int   gf_lie(const gf_world_t *w, float x, float y);
bool  gf_in_bounds(const gf_world_t *w, float x, float y);

/* Point in polygon, for one loaded polygon */
bool  gf_poly_inside(const gf_world_t *w, const gf_poly_t *p, float x, float y);
/* Distance from a point to a polygon's outline */
float gf_poly_dist(const gf_world_t *w, const gf_poly_t *p, float x, float y);

/* Deterministic noise in [-1, 1], smooth, with a period in metres */
float gf_noise(float x, float y, float period, uint32_t seed);
uint32_t gf_hash(uint32_t x);
