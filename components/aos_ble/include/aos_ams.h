/*
 * AmoledOS - Apple Media Service, the part that does not talk to NimBLE.
 *
 * Pure assembling and disassembling of bytes, without NimBLE and without
 * ESP-IDF, so it can be tested on the Mac with tools/ams_harness.c. Same
 * division of labour as aos_ancs.c.
 *
 * --------------------------------------------------------------------------
 * THIS PHASE'S RULE, and it comes first because it governs the rest:
 *
 *   **Notifications are worth more than music control.** If AMS interferes
 *   with ANCS, AMS leaves the device.
 *
 * Three decisions follow from that, and they are not matters of style:
 *
 *   1. AMS is the LAST link of the discovery chain. Everything ANCS needs is
 *      already subscribed before this starts, so if AMS fails at any step, it
 *      takes nothing with it.
 *   2. **It does not subscribe unless the user asks.** It starts off and is
 *      switched on from the BT Control app. Taking it off the air is a tap,
 *      not a recompilation.
 *   3. **Nothing that changes often is requested.** Of the track, the title,
 *      artist, album and duration are requested; of the player, the state and
 *      the app's name -which only changes when you change player-. The VOLUME
 *      is not requested: it changes every time somebody touches the button on
 *      the side of the phone. And neither is the track POSITION: it arrives
 *      inside the state when something changes and the watch goes on counting
 *      it by itself. Asking for it would be one notification a second
 *      competing for the air with the real notifications.
 * --------------------------------------------------------------------------
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AMS entities */
#define AOS_AMS_PLAYER  0
#define AOS_AMS_QUEUE   1
#define AOS_AMS_TRACK   2

typedef struct {
    char     player[32];    /* "Musica", "Spotify": which app is playing */
    char     title[64];
    char     artist[64];
    char     album[64];
    uint32_t duration_s;
    uint32_t elapsed_s;     /* as of the last notice, not now */
    bool     playing;
    bool     hay_datos;     /* at least one title has arrived */
    bool     elapsed_nuevo; /* the last notice carried a position: restart the count */
} aos_ams_state_t;

/* One Entity Update notice. Updates 'st' in place and returns true if
 * something worth redrawing changed. */
bool aos_ams_entity_update(const uint8_t *m, uint16_t len, aos_ams_state_t *st);

/* The subscription request for one entity. Returns the bytes written. */
int aos_ams_cmd_subscribe(uint8_t *out, size_t cap, int entidad);

/* One playback command, already translated from aos_media_cmd_t. -1 if that
 * command does not exist in AMS. */
int aos_ams_comando(int aos_media_cmd);
int aos_ams_cmd_remote(uint8_t *out, size_t cap, int comando_ams);

#ifdef __cplusplus
}
#endif
