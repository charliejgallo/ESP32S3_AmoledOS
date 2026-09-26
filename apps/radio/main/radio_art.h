/*
 * AmoledOS - Radio: the picture in the dial.
 *
 * Three sources, best first: the cover of the song on air, looked up by its
 * StreamTitle in the iTunes Search API (the only thing that leaves the watch
 * is "artist title"; off with the portal's switch); the station's logo, a
 * 160 px JPEG the portal made in the browser when the station was put on a
 * key (card: radio/logoN.jpg); and, with neither, the drawn one.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ART_PX 160

typedef struct {
    int       req;              /* aos_hal_http id in flight, 0 none       */
    int       step;             /* 0 idle, 1 searching, 2 fetching          */
    char      want[128];        /* the title looked up                      */
    char      album[96];        /* the hit's album, for the info panel      */
    char      hit[96];          /* "artist - song" as iTunes has it         */
    uint16_t *px;               /* ART_PX x ART_PX RGB565, when 'have'      */
    bool      have;             /* px is the cover of 'want'                */
    bool      fresh;            /* a new one arrived and was not shown yet  */
} radio_art_t;

void art_init(radio_art_t *a);
void art_free(radio_art_t *a);

/* Looks up the cover of "Artist - Title". Nothing happens (and 'have' goes
 * false) for a title with no " - ", or whose artist is the station itself:
 * that is a jingle or the station's own slogan, and a search would find
 * somebody else's record. */
void art_request(radio_art_t *a, const char *title, const char *station);
void art_tick(radio_art_t *a);

/* The key's logo from the card into 'out' (ART_PX square). */
bool art_logo(int slot, uint16_t *out);

/* Any baseline JPEG to the ART_PX square, nearest neighbour. */
bool art_decode(const uint8_t *data, int len, uint16_t *out);

/* PSRAM on the board. */
void *art_big_alloc(unsigned n);
void  art_big_free(void *p);
