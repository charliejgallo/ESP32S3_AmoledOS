/*
 * MAPAS - one decoded vector tile.
 *
 * OpenFreeMap serves OpenMapTiles vector tiles (Mapbox Vector Tile, protobuf).
 * mp_mvt_decode() keeps only what the watch draws, already sorted by drawing
 * class, so the renderer walks the classes in order across every tile on the
 * screen (roads of one tile over the water of the next) without sorting.
 *
 * Coordinates stay as the tile has them: int16, 0..extent (4096), plus the
 * buffer the tile carries past its edges. The renderer turns them into
 * screen pixels with one multiply and one add.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Drawing classes, in the order they are painted. The areas first, the lines
 * from the least to the most important road, the labels last. */
enum {
    MC_LAND_GRASS = 0,
    MC_LAND_WOOD,
    MC_LAND_SAND,
    MC_RESID,
    MC_INDUS,
    MC_CIVIC,
    MC_PARK,
    MC_AERO_AREA,
    MC_WATER,
    MC_BUILDING,
    MC_STREAM,
    MC_RIVER,
    MC_BOUNDARY,
    MC_RUNWAY,
    MC_PATH,
    MC_SERVICE,
    MC_MINOR,
    MC_TERTIARY,
    MC_SECONDARY,
    MC_PRIMARY,
    MC_TRUNK,
    MC_MOTORWAY,
    MC_RAIL,
    MC_FERRY,
    MC_GEOM_END,
    /* labels */
    MC_L_ROAD = MC_GEOM_END,
    MC_L_WATER,
    MC_L_PLACE,
    MC_COUNT
};

#define MF_POLY    0x01     /* a polygon: rings, even-odd */
#define MF_TUNNEL  0x02
#define MF_POINT   0x04

typedef struct {
    int16_t x, y;
} mp_pt_t;

typedef struct {
    uint32_t pt;            /* first point */
    uint32_t ring;          /* first entry of ring_n */
    uint16_t nring;
    uint8_t  flags;
    uint8_t  sub;           /* places: city, town...; roads: the road class */
    int32_t  name;          /* offset in names, -1 none */
    int16_t  rank;          /* labels: lower goes first */
    int16_t  bx0, by0, bx1, by1;    /* bounding box, tile units */
} mp_feat_t;

typedef struct mp_tile {
    uint8_t    z;
    uint32_t   x, y;
    uint16_t   extent;
    mp_feat_t *f;
    int        nf;
    int        cls[MC_COUNT + 1];   /* features of class c: f[cls[c]] .. f[cls[c+1]-1] */
    uint32_t  *ring_n;              /* points per ring */
    int        nring;
    mp_pt_t   *p;
    int        np;
    char      *names;
    int        names_len;
    uint32_t   bytes;               /* what it holds, for the cache's budget */
} mp_tile_t;

/* Places, for label size and priority (feature.sub of MC_L_PLACE). */
enum {
    MP_PLACE_COUNTRY = 0, MP_PLACE_STATE, MP_PLACE_CITY, MP_PLACE_TOWN,
    MP_PLACE_VILLAGE, MP_PLACE_SUBURB, MP_PLACE_QUARTER, MP_PLACE_NEIGHBOURHOOD,
    MP_PLACE_HAMLET, MP_PLACE_OTHER,
};

/* Decodes a tile. NULL without memory; an empty tile (no features) is a valid
 * result, and so is a zero-length buffer (the server's answer for a tile of
 * open sea at some zooms). */
mp_tile_t *mp_mvt_decode(const uint8_t *buf, int len, int z, uint32_t x, uint32_t y);
void       mp_tile_free(mp_tile_t *t);

/* Whether buf parses to its very end as a list of layers. A download cut
 * by a dropped connection still arrives as "200 and some bytes" (HTTP/1.0
 * ends at the socket's close), and the cut lands inside a layer: this is how
 * the app tells, so a half tile never reaches the card's cache. */
bool mp_mvt_complete(const uint8_t *buf, int len);

/* Copies into out only the layers the watch draws (drops poi, housenumber,
 * aerodrome_label, mountain_peak: in the centre of a city the POIs alone are
 * three quarters of a 500 KB tile). A tile is a list of layers, so this is
 * byte copying, no re-encoding. Returns the new length (<= len). */
int mp_mvt_strip(const uint8_t *buf, int len, uint8_t *out);
