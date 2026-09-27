/*
 * MAPAS - search: the names of the downloaded zones, and Photon online.
 *
 * Every offline zone brings an index, <card>/maps/<zone>.idx, written by the
 * portal's /mapas page or tools/map_pack.py (whose write_idx() documents the
 * AIX2 format): one 9-byte key per word of every name, sorted, and a table
 * with the first key of every 256. A search loads that table, reads only the
 * keys that start with the query's longest word, and then the few names it
 * needs: a few reads, however big the city. The first version's text index
 * (one line per name, read whole: ~1 s per 450 KB on the board) is still
 * read if found.
 *
 * Online, Photon (komoot's geocoder over OpenStreetMap) answers streets with
 * their numbers, which the offline index does not have. The request is the
 * app's (LVGL's side, like every HAL request); mp_search_photon() only reads
 * the answer.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MP_HIT_NAME 56

typedef struct {
    char    name[MP_HIT_NAME];
    char    sub[40];            /* kind, or street and town for Photon */
    int32_t lat, lon;           /* degrees x 1e6 */
    int     score;              /* lower is better */
    uint8_t kind;               /* MP_KIND_* */
} mp_hit_t;

enum { MP_KIND_PLACE = 0, MP_KIND_STREET, MP_KIND_POI, MP_KIND_WATER, MP_KIND_ADDRESS };

/* The query as typed (letters, digits, spaces) to the index's key form. */
void mp_search_key(const char *in, char *out, int n);

/* Every .idx in <card>/maps. Worker thread. Returns the hits, best first.
 * cancel is polled between chunks. */
int mp_search_files(const char *key, mp_hit_t *out, int max, volatile bool *cancel);

/* Photon's GeoJSON answer. Returns how many it added after the n already in
 * out (duplicates of an offline hit at the same spot are skipped). */
int mp_search_photon(const char *json, mp_hit_t *out, int n, int max);

/* The zoom that suits a hit: a town from further out than a shop. */
float mp_search_zoom(const mp_hit_t *h);
