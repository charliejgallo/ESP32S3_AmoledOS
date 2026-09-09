/* AmoledOS - Apple Media Service, taking bytes apart. See aos_ams.h. */
#include "aos_ams.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Attributes, from Apple's document */
#define TRACK_ARTIST    0
#define TRACK_ALBUM     1
#define TRACK_TITLE     2
#define TRACK_DURATION  3

#define PLAYER_NAME     0
#define PLAYER_INFO     1
#define PLAYER_VOLUME   2

#define FLAG_TRUNCADO   (1u << 0)

/* AMS commands */
#define CMD_PLAY        0
#define CMD_PAUSE       1
#define CMD_TOGGLE      2
#define CMD_NEXT        3
#define CMD_PREV        4
#define CMD_VOL_UP      5
#define CMD_VOL_DOWN    6

/* aos_media_cmd_t, without dragging aos_hal.h in here: the order is fixed in
 * the public header and checked on the caller's side. */
int aos_ams_comando(int aos_media_cmd)
{
    switch (aos_media_cmd) {
    case 0: return CMD_TOGGLE;      /* AOS_MEDIA_PLAY_PAUSE */
    case 1: return CMD_NEXT;
    case 2: return CMD_PREV;
    case 3: return CMD_VOL_UP;
    case 4: return CMD_VOL_DOWN;
    default: return -1;
    }
}

int aos_ams_cmd_remote(uint8_t *out, size_t cap, int comando_ams)
{
    if (!out || cap < 1 || comando_ams < 0) {
        return 0;
    }
    out[0] = (uint8_t)comando_ams;
    return 1;
}

/* The subscription request: the entity and then the attributes of interest.
 *
 * Of the player, the state and the app's name are requested. The name only
 * changes when you change player, so it adds no traffic in steady state. The
 * VOLUME is not requested: it changes every time somebody touches the little
 * button on the side of the phone, and every change would be one notice on the
 * air competing with the notifications, which is what this phase cannot do. */
int aos_ams_cmd_subscribe(uint8_t *out, size_t cap, int entidad)
{
    if (!out) {
        return 0;
    }
    if (entidad == AOS_AMS_TRACK) {
        if (cap < 5) return 0;
        out[0] = AOS_AMS_TRACK;
        out[1] = TRACK_ARTIST;
        out[2] = TRACK_ALBUM;
        out[3] = TRACK_TITLE;
        out[4] = TRACK_DURATION;
        return 5;
    }
    if (entidad == AOS_AMS_PLAYER) {
        if (cap < 3) return 0;
        out[0] = AOS_AMS_PLAYER;
        out[1] = PLAYER_NAME;
        out[2] = PLAYER_INFO;
        return 3;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Entity Update
 *
 *   [0] EntityID  [1] AttributeID  [2] Flags  [3..] the value, in UTF-8
 *
 * The value is NOT zero-terminated and may arrive truncated -bit 0 of the
 * flags says so-. A truncated title is shown anyway: half a song is more use
 * than none.
 * -------------------------------------------------------------------------- */

static void copiar(char *dst, size_t cap, const uint8_t *src, uint16_t len)
{
    if (len >= cap) {
        len = (uint16_t)(cap - 1);
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* AMS's numbers arrive as decimal text: "230.000" is 230 seconds. strtod is
 * used and not atoi because atoi would stop at the full stop and lose nothing,
 * but it would also accept "hola" as zero without saying so. */
static uint32_t segundos(const uint8_t *src, uint16_t len)
{
    char buf[24];
    copiar(buf, sizeof(buf), src, len);
    char *fin = NULL;
    double v = strtod(buf, &fin);
    if (fin == buf || v < 0 || v > 86400.0) {
        return 0;
    }
    return (uint32_t)v;
}

bool aos_ams_entity_update(const uint8_t *m, uint16_t len, aos_ams_state_t *st)
{
    if (!m || !st || len < 3) {
        return false;
    }

    uint8_t        entidad = m[0];
    uint8_t        atributo = m[1];
    const uint8_t *valor = &m[3];
    uint16_t       vlen = (uint16_t)(len - 3);

    st->elapsed_nuevo = false;

    if (entidad == AOS_AMS_TRACK) {
        switch (atributo) {
        case TRACK_TITLE:
            copiar(st->title, sizeof(st->title), valor, vlen);
            st->hay_datos = st->title[0] != '\0';
            return true;
        case TRACK_ARTIST:
            copiar(st->artist, sizeof(st->artist), valor, vlen);
            return true;
        case TRACK_ALBUM:
            copiar(st->album, sizeof(st->album), valor, vlen);
            return true;
        case TRACK_DURATION:
            st->duration_s = segundos(valor, vlen);
            return true;
        default:
            return false;
        }
    }

    if (entidad == AOS_AMS_PLAYER && atributo == PLAYER_NAME) {
        copiar(st->player, sizeof(st->player), valor, vlen);
        return true;
    }

    if (entidad == AOS_AMS_PLAYER && atributo == PLAYER_INFO) {
        /* "state,rate,elapsed" -> "1,1.000,12.345"
         *
         * Of the three only the first and the third are of interest. The rate
         * exists for fast-forward and the watch does not draw it. */
        char buf[64];
        copiar(buf, sizeof(buf), valor, vlen);

        char *coma1 = strchr(buf, ',');
        if (!coma1) {
            return false;
        }
        *coma1 = '\0';
        st->playing = (atoi(buf) == 1);

        char *coma2 = strchr(coma1 + 1, ',');
        if (coma2) {
            st->elapsed_s = segundos((const uint8_t *)(coma2 + 1),
                                     (uint16_t)strlen(coma2 + 1));
            /* The position arrives ONLY inside this notice, and this notice
             * arrives when something changes. The caller has to note the
             * moment and go on counting by itself: asking the phone for the
             * position would be one notice a second. */
            st->elapsed_nuevo = true;
        }
        return true;
    }

    return false;
}
