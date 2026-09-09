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
    printf("  %s  %s\n", cond ? "OK " : "MAL", que);
    if (!cond) fallos++;
}

static void oks(const char *que, const char *dio, const char *esperaba)
{
    pruebas++;
    int bien = strcmp(dio, esperaba) == 0;
    printf("  %s  %-44s [%s]\n", bien ? "OK " : "MAL", que, dio);
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
    ok("el titulo se guarda",
       aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, "Cancion de cuna"), &st));
    oks("titulo", st.title, "Cancion de cuna");
    ok("y con el titulo ya hay datos que mostrar", st.hay_datos);

    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 0, "Los Simulados"), &st);
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 1, "Pruebas"), &st);
    oks("artista", st.artist, "Los Simulados");
    oks("album", st.album, "Pruebas");

    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 3, "214.000"), &st);
    ok("la duracion viene como texto decimal", st.duration_s == 214);

    printf("\nel reproductor\n");
    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "1,1.000,12.345"), &st);
    ok("estado reproduciendo", st.playing);
    ok("y la posicion sale del mismo aviso",
       st.elapsed_s == 12 && st.elapsed_nuevo);

    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "0,0.000,45.900"), &st);
    ok("estado en pausa", !st.playing && st.elapsed_s == 45);

    /* A track notice carries no position: the caller must not restart its
     * count because of it. */
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, "Otro tema"), &st);
    ok("un aviso de la pista no dice que haya posicion nueva",
       !st.elapsed_nuevo);

    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 0, "Musica"), &st);
    oks("el nombre del reproductor", st.player, "Musica");

    printf("\nlo que puede venir mal\n");
    aos_ams_entity_update(b, eu(b, AOS_AMS_PLAYER, 1, "1"), &st);
    ok("un estado sin comas no rompe nada", true);
    aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 3, "hola"), &st);
    ok("una duracion que no es un numero queda en cero", st.duration_s == 0);
    ok("un aviso corto se descarta", !aos_ams_entity_update(b, 2, &st));
    ok("NULL tampoco rompe", !aos_ams_entity_update(NULL, 10, &st));
    {
        /* A title longer than the box: it is clipped and ends in a zero. */
        char largo[200];
        memset(largo, 'a', sizeof(largo) - 1);
        largo[sizeof(largo) - 1] = '\0';
        aos_ams_entity_update(b, eu(b, AOS_AMS_TRACK, 2, largo), &st);
        ok("un titulo larguisimo entra recortado y terminado",
           strlen(st.title) == sizeof(st.title) - 1);
    }
    aos_ams_entity_update(b, eu(b, AOS_AMS_QUEUE, 0, "3"), &st);
    ok("una entidad que no pedimos se ignora", true);

    printf("\nlo que se le escribe al telefono\n");
    {
        uint8_t c[8];
        int n = aos_ams_cmd_subscribe(c, sizeof(c), AOS_AMS_TRACK);
        ok("la suscripcion a la pista pide los cuatro atributos",
           n == 5 && c[0] == AOS_AMS_TRACK && c[1] == 0 && c[2] == 1 &&
           c[3] == 2 && c[4] == 3);

        n = aos_ams_cmd_subscribe(c, sizeof(c), AOS_AMS_PLAYER);
        /* The name and the state, and NOTHING ELSE. The volume is left out on
         * purpose: it changes every time somebody touches the button on the
         * side of the phone, and every change would be one notice competing
         * for the air with the notifications. This test exists so nobody adds
         * it without noticing. */
        ok("la del reproductor pide el nombre y el estado, sin el volumen",
           n == 3 && c[0] == AOS_AMS_PLAYER && c[1] == 0 && c[2] == 1);

        ok("no escribe si no entra", aos_ams_cmd_subscribe(c, 1, AOS_AMS_TRACK) == 0);

        ok("play/pausa es el comando 2 de AMS", aos_ams_comando(0) == 2);
        ok("siguiente es el 3", aos_ams_comando(1) == 3);
        ok("anterior es el 4", aos_ams_comando(2) == 4);
        ok("un comando que AMS no tiene devuelve -1", aos_ams_comando(99) == -1);

        n = aos_ams_cmd_remote(c, sizeof(c), aos_ams_comando(1));
        ok("el comando es un solo byte", n == 1 && c[0] == 3);
    }

    printf("\n%d pruebas, %d fallos\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
