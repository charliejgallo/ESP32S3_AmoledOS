/*
 * MONSTER HOP - what the player has: progress, the wardrobe, trophies, stats
 *
 * One preference per number (Turbo's lesson: packed bits run out): stars
 * and best times per level, owned items per category as a bit mask, the
 * equipped item per category. Level numbers are forever: new levels go at
 * the end of the table even if the map shows them elsewhere.
 */
#pragma once

#include "mh_shop.h"

#include <stdbool.h>
#include <stdint.h>

#define MH_NLEVELS 16

enum {
    SX_KEYS = 0, SX_COINS, SX_HOPS, SX_LOST, SX_LEVELS, SX_RACES, SX_RACES_WON, SX_PLAY_S, SX_BOUGHT,
    SX_DROWNED, SX_CAUGHT, SX_FELL, SX_N,
};

enum {
    TR_FIRST = 0, TR_CLEAN, TR_HARD, TR_ZONE_CITY, TR_ZONE_CASTLE, TR_ZONE_DESERT, TR_ZONE_FOREST,
    TR_BOSSES, TR_ALBUM, TR_KEYS100, TR_HOPS5000, TR_SHOP10, TR_FRIEND, TR_ALL_STARS, TR_N,
};

typedef struct {
    int32_t  coins;
    uint8_t  stars[MH_NLEVELS];
    int32_t  best_ms[MH_NLEVELS];
    uint32_t stickers;              /* one bit per level                      */
    uint32_t trophies;
    int32_t  stat[SX_N];
    uint32_t own[CAT_N];
    int8_t   eq[CAT_N];
    int      diff;
    bool     sfx, music;
} mh_prog_t;

void mh_prog_load(mh_prog_t *p);
void mh_prog_save(const mh_prog_t *p);
void mh_prog_reset(mh_prog_t *p);
bool mh_prog_owns(const mh_prog_t *p, int cat, int i);
int  mh_prog_stars(const mh_prog_t *p);
/* checks every trophy; returns the bits won right now */
uint32_t mh_prog_trophies(mh_prog_t *p);

const char *mh_trophy_name(int t);      /* translated */
const char *mh_trophy_desc(int t);
int         mh_trophy_tier(int t);      /* 0 bronze, 1 silver, 2 gold */
