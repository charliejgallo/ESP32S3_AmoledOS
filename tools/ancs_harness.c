/*
 * AmoledOS - bench for the ANCS client.
 *
 * It links against components/aos_ble/aos_ancs.c and
 * components/aos_hal/aos_notif.c, which between them have not one line of
 * NimBLE or ESP-IDF.
 *
 *   cc -Wall -Wextra -DAOS_SIM -I components/aos_hal/include \
 *      -I components/aos_ble/include tools/ancs_harness.c \
 *      components/aos_ble/aos_ancs.c components/aos_hal/aos_notif.c \
 *      -o /tmp/ancs && /tmp/ancs
 *
 * Why it exists: it is the ONLY part of phase F5 verifiable without the iPhone
 * in front of you. The rest -advertising, pairing, GATT discovery- needs a
 * real phone on the other side, and until there is one it is code that looks
 * like it works. These bytes, by contrast, are bytes: the format is written
 * down in Apple's document and can be reproduced in full here.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "aos_hal.h"
#include "aos_ancs.h"
#include "aos_notif_internal.h"

/* --- the minimum aos_notif.c asks of the HAL ----------------------------- */

static int32_t s_val[8];
static char    s_key[8][16];
static int     s_n;

bool aos_hal_pref_get_i32(const char *key, int32_t *out)
{
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_key[i], key) == 0) { *out = s_val[i]; return true; }
    }
    return false;
}

bool aos_hal_pref_set_i32(const char *key, int32_t value)
{
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_key[i], key) == 0) { s_val[i] = value; return true; }
    }
    snprintf(s_key[s_n], sizeof(s_key[0]), "%s", key);
    s_val[s_n++] = value;
    return true;
}


/* A fake clock, so time can be moved by hand: the burst grouping depends on a
 * one-minute window and really waiting for it would be absurd. */
static uint64_t g_ms = 1000;
uint64_t aos_hal_uptime_ms(void) { return g_ms; }

/* --- scaffolding --------------------------------------------------------- */

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
    printf("  %s  %-42s [%s]\n", bien ? "OK " : "BAD", que, dio);
    if (!bien) { printf("       esperaba [%s]\n", esperaba); fallos++; }
}

/* Builds a Notification Source packet. */
static void ns(uint8_t *b, uint8_t evento, uint8_t flags, uint8_t cat, uint32_t uid)
{
    b[0] = evento; b[1] = flags; b[2] = cat; b[3] = 1;
    b[4] = (uint8_t)uid; b[5] = (uint8_t)(uid >> 8);
    b[6] = (uint8_t)(uid >> 16); b[7] = (uint8_t)(uid >> 24);
}

/* Builds a Data Source response with the five attributes, in order. */
static uint16_t ds(uint8_t *b, uint32_t uid, const char *app, const char *titulo,
                   const char *sub, const char *msg, const char *fecha)
{
    const char *v[5] = { app, titulo, sub, msg, fecha };
    uint16_t i = 0;
    b[i++] = 0;
    b[i++] = (uint8_t)uid; b[i++] = (uint8_t)(uid >> 8);
    b[i++] = (uint8_t)(uid >> 16); b[i++] = (uint8_t)(uid >> 24);
    for (uint8_t a = 0; a < 5; a++) {
        uint16_t n = (uint16_t)strlen(v[a]);
        b[i++] = a;
        b[i++] = (uint8_t)(n & 0xFF);
        b[i++] = (uint8_t)(n >> 8);
        memcpy(&b[i], v[a], n);
        i = (uint16_t)(i + n);
    }
    return i;
}

static void limpiar(void)
{
    aos_hal_notif_clear();
    aos_hal_notif_enable(true);
    aos_hal_notif_sound_set(true);
    aos_hal_notif_calls_always_set(true);
    aos_hal_notif_categories_set(0xFFFFFFFFu);
}

/* What aos_ble.c does: it keeps the notice's category and flags and hands them
 * back to the parser when the text arrives. */
static uint32_t g_uid;
static uint8_t  g_cat, g_flags;

static aos_ancs_accion_t aviso(uint8_t *b, uint16_t len)
{
    return aos_ancs_notification_source(b, len, &g_uid, &g_cat, &g_flags);
}

static bool texto(uint8_t *b, uint16_t len, aos_notif_t *n)
{
    return aos_ancs_data_source(b, len, g_uid, g_cat, g_flags, n);
}

int main(void)
{
    uint8_t b[1024];
    aos_notif_t n;

    printf("\n== ANCS client ==\n");

    printf("\nNotification Source\n");
    limpiar();
    ns(b, 0 /*added*/, 0, 4 /*social*/, 0x11223344);
    ok("a new one asks for the attributes",
       aviso(b, 8) == AOS_ANCS_PEDIR_ATRIBUTOS);
    ok("and it carries the UID in little endian", g_uid == 0x11223344);

    ns(b, 2 /*removed*/, 0, 4, 0x55667788);
    ok("a withdrawn one asks for nothing",
       aviso(b, 8) == AOS_ANCS_NADA);
    uint32_t quitado = 0;
    ok("and goes straight to the removed queue",
       aos_hal_notif_pop_removed(&quitado) && quitado == 0x55667788);

    ns(b, 1 /*modified*/, 0, 1, 7);
    ok("a modified one is asked for too (the missed call arrives that way)",
       aviso(b, 8) == AOS_ANCS_PEDIR_ATRIBUTOS);

    ok("a short packet breaks nothing",
       aviso(b, 3) == AOS_ANCS_NADA);
    ok("y NULL tampoco",
       aos_ancs_notification_source(NULL, 8, &g_uid, &g_cat, &g_flags) ==
       AOS_ANCS_NADA);

    printf("\nData Source\n");
    limpiar();
    ns(b, 0, 0, 4, 100);
    aviso(b, 8);
    uint16_t len = ds(b, 100, "com.burbn.instagram", "Mariana", "",
                      "vamos a comer?", "20260906T213000");
    ok("a complete answer is assembled", texto(b, len, &n));
    oks("the app name comes from the identifier", n.app, "instagram");
    oks("titulo", n.title, "Mariana");
    oks("mensaje", n.message, "vamos a comer?");
    ok("the category comes from the Notification Source", n.category == AOS_NOTIF_SOCIAL);

    printf("\nfragmentation\n");
    limpiar();
    ns(b, 0, 0, 4, 101);
    aviso(b, 8);
    len = ds(b, 101, "com.a.b", "Titulo", "", "A message that arrives in pieces",
             "20260906T213000");
    int cortes = 0;
    for (uint16_t corte = 5; corte < len; corte += 7) {
        if (texto(b, corte, &n)) { cortes++; }
    }
    ok("no incomplete fragment is taken as good", cortes == 0);
    ok("and with the last byte it is assembled whole", texto(b, len, &n));
    oks("with the complete message", n.message, "A message that arrives in pieces");

    printf("\nsubtitulo\n");
    limpiar();
    ns(b, 0, 0, 4, 102);
    aviso(b, 8);
    len = ds(b, 102, "com.x.y", "Equipo", "Mariana", "the parcel arrived",
             "20260906T213000");
    texto(b, len, &n);
    oks("it goes in front of the message, otherwise you lose who it is from",
        n.message, "Mariana \xC2\xB7 the parcel arrived");

    limpiar();
    ns(b, 0, 0, 4, 103);
    aviso(b, 8);
    len = ds(b, 103, "com.x.y", "Equipo", "Mariana", "", "20260906T213000");
    texto(b, len, &n);
    oks("and on its own, if there is no message", n.message, "Mariana");

    printf("\nbanderas\n");
    limpiar();
    ns(b, 0, 0x01 /*silent*/, 4, 110);
    aviso(b, 8);
    len = ds(b, 110, "com.x.y", "T", "", "M", "20260906T213000");
    texto(b, len, &n);
    ok("silenciosa", n.silent && !n.pre_existing &&
                     !n.can_positive && !n.can_negative);

    limpiar();
    ns(b, 0, 0x04 /*pre existing*/, 4, 111);
    aviso(b, 8);
    len = ds(b, 111, "com.x.y", "T", "", "M", "20260906T213000");
    texto(b, len, &n);
    ok("it was already on the phone", n.pre_existing);

    limpiar();
    ns(b, 0, 0x18 /*positive|negative*/, 1, 112);
    aviso(b, 8);
    len = ds(b, 112, "com.apple.mobilephone", "Papa", "", "", "20260906T213000");
    texto(b, len, &n);
    ok("with both flags, both actions",
       n.can_positive && n.can_negative &&
       n.category == AOS_NOTIF_CALL_INCOMING);

    /* What iOS sends on a messaging notification: the negative one only
     * -dismiss the alert-. Asking it for the positive returns 0xA3. An
     * incoming call, by contrast, arrives with both (flags=0x1A) and both
     * work. */
    limpiar();
    ns(b, 0, 0x10 /*negative only*/, 4, 113);
    aviso(b, 8);
    len = ds(b, 113, "net.whatsapp.WhatsApp", "Nat", "", "hola", "20260906T213000");
    texto(b, len, &n);
    ok("with a single flag, a single action",
       n.can_negative && !n.can_positive);

    /* The exact flags of a real incoming call, taken from the board's log:
     * 0x1A is negative + positive + important. */
    limpiar();
    ns(b, 0, 0x1A, 1, 114);
    aviso(b, 8);
    len = ds(b, 114, "com.apple.mobilephone", "Nat", "", "", "20260906T213000");
    texto(b, len, &n);
    ok("a real incoming call offers answer and reject",
       n.can_positive && n.can_negative && n.important &&
       n.category == AOS_NOTIF_CALL_INCOMING);

    printf("\nburst: the metadata travels with the request\n");
    limpiar();
    /* Twenty notices before the first one's text arrives, which is what really
     * happens on connecting. While the category and the flags lived in a table
     * inside aos_ancs.c, that table overflowed and the first ones came out
     * with zeroed flags: that is, "not pre-existing", that is, full screen. On
     * the board it was eight old notifications in a row. Now every request
     * carries its own and there is no table to overflow. */
    {
        uint32_t uids[20];
        uint8_t  cats[20], flgs[20];
        for (int k = 0; k < 20; k++) {
            ns(b, 0, 0x04 /*pre-existing*/, (uint8_t)(k % 12), (uint32_t)(300 + k));
            aos_ancs_notification_source(b, 8, &uids[k], &cats[k], &flgs[k]);
        }
        int mal = 0;
        for (int k = 0; k < 20; k++) {
            len = ds(b, uids[k], "com.a.b", "T", "", "M", "20260906T213000");
            if (!aos_ancs_data_source(b, len, uids[k], cats[k], flgs[k], &n)) {
                mal++;
            } else if (n.category != (aos_notif_category_t)(k % 12) ||
                       !n.pre_existing) {
                mal++;
            }
        }
        ok("twenty in a row each keep their category and their flags",
           mal == 0);
    }

    printf("\nan answer that arrives late\n");
    limpiar();
    ns(b, 0, 0, 4, 900); aviso(b, 8);
    len = ds(b, 901, "com.a.b", "T", "", "M", "20260906T213000");
    ok("an answer for another UID is discarded and does not get mixed in",
       !texto(b, len, &n));

    printf("\nfechas\n");
    ok("an ordinary date", aos_ancs_fecha("20260906T213045", 15) > 0);
    ok("without the T it is invalid",  aos_ancs_fecha("20260906X213045", 15) == 0);
    ok("with letters it is invalid", aos_ancs_fecha("2026090aT213045", 15) == 0);
    ok("a short one is invalid",      aos_ancs_fecha("20260906T2130", 12) == 0);
    ok("month 13 is invalid",     aos_ancs_fecha("20261306T213045", 15) == 0);
    ok("an empty one is invalid",      aos_ancs_fecha("", 0) == 0);

    printf("\ncommands to the Control Point\n");
    {
        uint8_t c[64];
        int k = aos_ancs_cmd_atributos(c, sizeof(c), 0xAABBCCDD);
        /* 1 of command + 4 of UID + 5 attributes, of which three carry a
         * two-byte length: 5 + 5 + 6 = 16. */
        ok("the attributes command is 16 bytes long", k == 16);
        ok("it starts with command 0 and the UID in little endian",
           c[0] == 0 && c[1] == 0xDD && c[2] == 0xCC && c[3] == 0xBB && c[4] == 0xAA);
        /* AppIdentifier (0) goes WITHOUT a length: sending one makes the phone
         * answer "invalid command" and nothing arrives. */
        ok("AppIdentifier goes without a length and Title with one",
           c[5] == 0 && c[6] == 1 && c[7] == 48 && c[8] == 0);
        ok("it does not write if it does not fit", aos_ancs_cmd_atributos(c, 4, 1) == 0);

        k = aos_ancs_cmd_accion(c, sizeof(c), 0x01020304, true);
        ok("the positive action is 6 bytes and ends in 0",
           k == 6 && c[0] == 2 && c[1] == 0x04 && c[5] == 0);
        aos_ancs_cmd_accion(c, sizeof(c), 1, false);
        ok("the negative one ends in 1", c[5] == 1);
    }

    printf("\nbasura\n");
    limpiar();
    ok("a Data Source with the wrong command is discarded",
       !aos_ancs_data_source((const uint8_t *)"\x09\x01\x02\x03\x04", 5,
                             0x04030201, 0, 0, &n));
    memset(b, 0xFF, sizeof(b));
    ok("an impossible length does not read outside the buffer",
       !aos_ancs_data_source(b, 20, 0xFFFFFFFFu, 0, 0, &n));

    printf("\n%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
