/*
 * MAPAS - where tiles come from, in order:
 *
 *   1. RAM: decoded tiles, least recently used first out, within a budget.
 *   2. Offline packs: <card>/maps/ (files .amp), a zone downloaded whole (the
 *      portal's /mapas page makes them; tools/map_pack.py too). One file per
 *      zone, an index at the front, so a zone of hundreds of tiles is one
 *      file on the FAT and not hundreds of small ones.
 *   3. The card's cache: <card>/maps/cache/z/x/y.mvt, every tile that came
 *      from the network, stripped of the layers the watch does not draw.
 *   4. The network (the app asks for it; mp_store_put() takes the answer).
 *
 * Worker thread only, except mp_store_packs(), a snapshot for the list.
 *
 * Pack format (.amp, little-endian):
 *   0   "AMP1"
 *   4   u32 ntiles
 *   8   u8  minz, u8 maxz, u16 flags (0)
 *   12  i32 west, south, east, north (degrees x 1e6)
 *   28  char name[32] (UTF-8, NUL-padded)
 *   60  u32 index offset (64)
 *   64  ntiles x { u8 z, u8 pad[3], u32 x, u32 y, u32 offset, u32 length }
 *       sorted by (z, x, y)
 *   ... tile data: MVT, stripped like the cache's
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mp_tile.h"

#define MP_MAX_PACKS 16

typedef struct {
    char    name[32];
    char    file[48];
    int     ntiles;
    int     minz, maxz;
    int32_t w, s, e, n;         /* degrees x 1e6 */
} mp_pack_info_t;

void mp_store_init(uint32_t ram_budget);
void mp_store_deinit(void);

/* (Re)reads the .amp files in <card>/maps. */
void mp_store_scan_packs(void);
int  mp_store_packs(mp_pack_info_t *out, int max);

/* A new render starts: tiles used from here on are kept over older ones. */
void mp_store_frame(void);

/* The tile if it is somewhere on the watch. NULL otherwise, and then
 * *need_net says whether asking the network makes sense (false when it is
 * known to be empty or a download already failed for good). */
mp_tile_t *mp_store_get(int z, uint32_t x, uint32_t y, bool *need_net);

/* Only what is already in RAM (for the ancestors that stand in). */
mp_tile_t *mp_store_peek(int z, uint32_t x, uint32_t y);

/* A tile from the network: decoded into RAM and, if save, stripped onto the
 * card. len 0 is an empty tile (the server's 204/404 for open sea). The
 * buffer is used as scratch (the stripping is done in place). */
bool mp_store_put(int z, uint32_t x, uint32_t y, uint8_t *mvt, int len, bool save);

/* Forget the card cache (a button in the list). Returns files deleted. */
int mp_store_clear_cache(void);

/* <card>/maps, or NULL without a card. */
const char *mp_maps_dir(void);

/* For the log. */
void mp_store_stats(uint32_t *ram_bytes, int *ram_tiles, uint32_t *hits_pack, uint32_t *hits_cache);
