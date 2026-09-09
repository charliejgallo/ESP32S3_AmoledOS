/*
 * AmoledOS - bench for the Apple Media Service.
 *
 *   cc -Wall -Wextra -I components/aos_ble/include tools/ams_harness.c \
 *      components/aos_ble/aos_ams.c -o /tmp/ams && /tmp/ams
 *
 * Same reason as ancs_harness.c: the format is in Apple's document and can be
 * reproduced in full here, so half of F11's bytes are verified without the
 * phone. What needs the iPhone on the other side is the other half.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "aos_ams.h"

static int pruebas, fallos;

static void ok(const char *que, int cond)
{
    pruebas++;
    printf("  %s  %s\n", cond ? "OK " : "BAD", que);
    if (!cond) fallos++;
}

static void oks(const char *que, const char *dio, const char *esperaba)
{
    pruebas++;
    int bien = strcmp(dio, esperaba) == 0;
    printf("  %s  %-44s [%s]\n", bien ? "OK " : "BAD", que, dio);
    if (!bien) { printf("       esperaba [%s]\n", esperaba); fallos++; }
}

/* One Entity Update notice. */
static uint16_t eu(uint8_t *b, uint8_t ent, uint8_t attr, const char *v)
{
    b[0] = ent; b[1] = attr; b[2] = 0;
    uint16_t n = (uint16_t)strlen(v);
    memcpy(&b[3], v, n);
    return (uint16_t)(3 + n);
}

int main(void)
{
    uint8_t b[256];
    aos_ams_state_t st;
    memset(&st, 0, sizeof(st));

    printf("\n== Apple Media Service ==\n");

    printf("\nla pista\n");
    ok("the title is stored",
       aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, "Lullaby"), &st));
    oks("titulo", st.title, "Lullaby");
    ok("and with the title there is already something to show", st.hay_datos);

    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 0, "The Simulated"), &st);
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 1, "Test Runs"), &st);
    oks("artista", st.artist, "The Simulated");
    oks("album", st.album, "Test Runs");

    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 3, "214.000"), &st);
    ok("the duration comes as decimal text", st.duration_s == 214);

    printf("\nel reproductor\n");
    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "1,1.000,12.345"), &st);
    ok("state playing", st.playing);
    ok("and the position comes from the same notification",
       st.elapsed_s == 12 && st.elapsed_nuevo);

    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "0,0.000,45.900"), &st);
    ok("state paused", !st.playing && st.elapsed_s == 45);

    /* A track notice carries no position: the caller must not restart its
     * count because of it. */
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, "Another Track"), &st);
    ok("a track notification does not say there is a new position",
       !st.elapsed_nuevo);

    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 0, "Musica"), &st);
    oks("the player name", st.player, "Musica");

    printf("\nwhat can arrive malformed\n");
    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "1"), &st);
    ok("a state with no commas breaks nothing", true);
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 3, "hola"), &st);
    ok("a duration that is not a number ends up zero", st.duration_s == 0);
    ok("a short notification is discarded", !aos_ams_entity_update(b, 2, &st));
    ok("NULL tampoco rompe", !aos_ams_entity_update(NULL, 10, &st));
    {
        /* A title longer than the box: it is clipped and ends in a zero. */
        char largo[200];
        memset(largo, 'a', sizeof(largo) - 1);
        largo[sizeof(largo) - 1] = '\0';
        aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, largo), &st);
        ok("a very long title comes in clipped and terminated",
           strlen(st.title) == sizeof(st.title) - 1);
    }
    aos_ams_entity_update(b, eu(b, AOS_AMS_QUEUE, 0, "3"), &st);
    ok("an entity we did not ask for is ignored", true);

    printf("\nwhat gets written to the phone\n");
    {
        uint8_t c[8];
        int n = aos_ams_cmd_subscribe(c, sizeof(c), AOS_AMS_TRACK);
        ok("the track subscription asks for the four attributes",
           n == 5 && c[0] == AOS_AMS_TRACK && c[1] == 0 && c[2] == 1 &&
           c[3] == 2 && c[4] == 3);

        n = aos_ams_cmd_subscribe(c, sizeof(c), AOS_AMS_PLAYER);
        /* The name and the state, and NOTHING ELSE. The volume is left out on
         * purpose: it changes every time somebody touches the button on the
         * side of the phone, and every change would be one notice competing
         * for the air with the notifications. This test exists so nobody adds
         * it without noticing. */
        ok("the player one asks for the name and the state, without the volume",
           n == 3 && c[0] == AOS_AMS_PLAYER && c[1] == 0 && c[2] == 1);

        ok("it does not write if it does not fit", aos_ams_cmd_subscribe(c, 1, AOS_AMS_TRACK) == 0);

        ok("play/pause is AMS command 2", aos_ams_comando(0) == 2);
        ok("next is 3", aos_ams_comando(1) == 3);
        ok("previous is 4", aos_ams_comando(2) == 4);
        ok("a command AMS does not have returns -1", aos_ams_comando(99) == -1);

        n = aos_ams_cmd_remote(c, sizeof(c), aos_ams_comando(1));
        ok("the command is a single byte", n == 1 && c[0] == 3);
    }

    printf("\n%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
