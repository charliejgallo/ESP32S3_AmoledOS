/*
 * AmoledOS - bench for the notification policy.
 *
 * It links against components/aos_hal/aos_notif.c, which has not one line of
 * LVGL or NimBLE: it only asks for aos_hal_pref_* and time_t. Same division of
 * labour as qr_harness.c: the decision on one side, the drawing on the other.
 *
 *   cc -Wall -Wextra -DAOS_SIM -I components/aos_hal/include \
 *      tools/nt_harness.c components/aos_hal/aos_notif.c -o /tmp/nt && /tmp/nt
 *
 * Why it exists: the policy is a crossing of five conditions -do not disturb,
 * category filter, the phone's silence, "calls always" and those already there
 * on connecting- and none of the five is visible on screen. A mistake here
 * gives no symptom: it gives a notification that did not arrive, which is
 * indistinguishable from the phone never having sent it. The only way to know
 * the table is right is to write it out in full and run it.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "aos_hal.h"
#include "aos_notif_internal.h"

/* --- the minimum aos_notif.c asks of the HAL ----------------------------- */

static int32_t s_prefs[8];
static char    s_keys[8][16];
static int     s_nprefs;

bool aos_hal_pref_get_i32(const char *key, int32_t *out)
{
    for (int i = 0; i < s_nprefs; i++) {
        if (strcmp(s_keys[i], key) == 0) { *out = s_prefs[i]; return true; }
    }
    return false;
}

bool aos_hal_pref_set_i32(const char *key, int32_t value)
{
    for (int i = 0; i < s_nprefs; i++) {
        if (strcmp(s_keys[i], key) == 0) { s_prefs[i] = value; return true; }
    }
    snprintf(s_keys[s_nprefs], sizeof(s_keys[0]), "%s", key);
    s_prefs[s_nprefs++] = value;
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

static uint32_t s_uid;

/* Pushes one and returns what the policy decided: 0 discarded, 1 history only,
 * 2 alerts without sound, 3 alerts and sounds. */
static int empujar(aos_notif_category_t cat, bool silent, bool pre_existing)
{
    aos_notif_t n;
    memset(&n, 0, sizeof(n));
    n.uid          = ++s_uid;
    n.category     = cat;
    n.silent       = silent;
    n.pre_existing = pre_existing;
    snprintf(n.app, sizeof(n.app), "banco");

    if (!aos_notif_push(&n)) {
        return 0;
    }
    aos_notif_t leida;
    if (!aos_hal_notif_pop(&leida)) {
        return -1;                  /* accepted but not queued: that is a bug */
    }
    return !leida.alert ? 1 : (leida.sound ? 3 : 2);
}

static void limpiar(void)
{
    aos_hal_notif_clear();
    aos_hal_notif_enable(true);
    aos_hal_notif_sound_set(true);
    aos_hal_notif_calls_always_set(true);
    aos_hal_notif_categories_set(0xFFFFFFFFu);
}

int main(void)
{
    printf("\n== notification policy ==\n");

    printf("\ntodo encendido\n");
    limpiar();
    ok("an ordinary social one alerts and sounds",
       empujar(AOS_NOTIF_SOCIAL, false, false) == 3);
    ok("if the phone sent it silent, it alerts without noise",
       empujar(AOS_NOTIF_SOCIAL, true, false) == 2);
    ok("the ones already there on connecting do NOT alert",
       empujar(AOS_NOTIF_SOCIAL, false, true) == 1);

    printf("\nsonido apagado\n");
    limpiar();
    aos_hal_notif_sound_set(false);
    ok("a social one alerts without noise",
       empujar(AOS_NOTIF_SOCIAL, false, false) == 2);
    ok("a call sounds anyway: 'calls always' overrides the sound",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 3);

    printf("\ndo not disturb (the link is still alive)\n");
    limpiar();
    aos_hal_notif_enable(false);
    ok("a social one does not alert but is stored",
       empujar(AOS_NOTIF_SOCIAL, false, false) == 1);
    ok("an incoming call gets through anyway",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 3);
    aos_hal_notif_calls_always_set(false);
    ok("without 'calls always', the call does not alert either",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 1);

    printf("\nfilter by category\n");
    limpiar();
    aos_hal_notif_categories_set(~(1u << AOS_NOTIF_NEWS));
    ok("the news ones are dropped entirely",
       empujar(AOS_NOTIF_NEWS, false, false) == 0);
    ok("and they do not stay in the history",
       aos_hal_notif_count() == 0);
    ok("the rest still gets through",
       empujar(AOS_NOTIF_SOCIAL, false, false) == 3);

    limpiar();
    aos_hal_notif_categories_set(0);
    ok("with everything filtered, a call still gets through by 'calls always'",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 3);
    aos_hal_notif_calls_always_set(false);
    ok("and without that exception, it does not get through either",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 0);

    printf("\nburst grouping\n");
    limpiar();
    {
        /* Five messages in a row from the same person: WhatsApp sends one per
         * message. The first sounds, the rest do not, and each one knows how
         * many there have been. */
        aos_notif_t leidas[5];
        for (int k = 0; k < 5; k++) {
            aos_notif_t n2;
            memset(&n2, 0, sizeof(n2));
            n2.uid = (uint32_t)(500 + k);
            n2.category = AOS_NOTIF_SOCIAL;
            snprintf(n2.app, sizeof(n2.app), "WhatsApp");
            snprintf(n2.title, sizeof(n2.title), "Mariana");
            snprintf(n2.message, sizeof(n2.message), "mensaje %d", k);
            aos_notif_push(&n2);
            aos_hal_notif_pop(&leidas[k]);
        }
        ok("all five alert: the last one is always the one shown",
           leidas[0].alert && leidas[4].alert);
        ok("only the first one sounds",
           leidas[0].sound && !leidas[1].sound && !leidas[4].sound);
        ok("and each one knows how many there are",
           leidas[0].repeticiones == 1 && leidas[4].repeticiones == 5);

        /* Another person breaks the burst. */
        aos_notif_t n3;
        memset(&n3, 0, sizeof(n3));
        n3.uid = 600;
        n3.category = AOS_NOTIF_SOCIAL;
        snprintf(n3.app, sizeof(n3.app), "WhatsApp");
        snprintf(n3.title, sizeof(n3.title), "Other");
        aos_notif_push(&n3);
        aos_notif_t n4;
        aos_hal_notif_pop(&n4);
        ok("another sender starts from zero and sounds again",
           n4.repeticiones == 1 && n4.sound);

        /* And once the minute has passed, the same sender counts from one
         * again and sounds: otherwise two messages half an hour apart would
         * look like a conversation. */
        g_ms += 61000;
        aos_notif_t n5;
        memset(&n5, 0, sizeof(n5));
        n5.uid = 601;
        n5.category = AOS_NOTIF_SOCIAL;
        snprintf(n5.app, sizeof(n5.app), "WhatsApp");
        snprintf(n5.title, sizeof(n5.title), "Other");
        aos_notif_push(&n5);
        aos_notif_t n6;
        aos_hal_notif_pop(&n6);
        ok("past the minute the burst breaks by itself",
           n6.repeticiones == 1 && n6.sound);
    }

    /* And the one that had to be added: the grouping cannot silence a call.
     * The first version compared app and title only, and since in this bench
     * every notification shares both, an incoming call behind a message was
     * left without a sound. The one thing that cannot be lost. */
    limpiar();
    ok("a message sounds", empujar(AOS_NOTIF_SOCIAL, false, false) == 3);
    ok("and the call behind it does too, even though it looks alike",
       empujar(AOS_NOTIF_CALL_INCOMING, false, false) == 3);

    printf("\nhistorial\n");
    limpiar();
    for (int i = 0; i < 20; i++) {
        empujar(AOS_NOTIF_SOCIAL, false, false);
    }
    ok("the ring stays at 16", aos_hal_notif_count() == 16);
    aos_notif_t n;
    ok("index 0 is the newest",
       aos_hal_notif_at(0, &n) && n.uid == s_uid);
    ok("15 is the oldest that survived",
       aos_hal_notif_at(15, &n) && n.uid == s_uid - 15);
    ok("16 does not exist", !aos_hal_notif_at(16, &n));

    printf("\ndeleting one at a time\n");
    limpiar();
    for (int k = 0; k < 5; k++) {
        aos_notif_t n7;
        memset(&n7, 0, sizeof(n7));
        n7.uid = (uint32_t)(700 + k);
        n7.category = AOS_NOTIF_SOCIAL;
        snprintf(n7.app, sizeof(n7.app), "app%d", k);
        aos_notif_push(&n7);
    }
    ok("five stored", aos_hal_notif_count() == 5);
    ok("one from the middle is deleted", aos_hal_notif_remove(702));
    ok("four are left", aos_hal_notif_count() == 4);
    {
        /* What matters: those remaining stay in order and none is duplicated
         * or skipped when walking the list. */
        aos_notif_t v;
        bool bien = true;
        uint32_t esperados[4] = { 704, 703, 701, 700 };
        for (int k = 0; k < 4; k++) {
            if (!aos_hal_notif_at(k, &v) || v.uid != esperados[k]) {
                bien = false;
            }
        }
        ok("and the walk skips the hole without shifting the rest", bien);
        ok("index 4 no longer exists", !aos_hal_notif_at(4, &v));
    }
    ok("deleting one that is not there returns false", !aos_hal_notif_remove(999));

    /* And what the user asked for: that the phone dismissing it takes it off
     * the list too, and does not merely close the screen. */
    aos_notif_push_removed(703);
    ok("when the phone withdraws it, it leaves the list",
       aos_hal_notif_count() == 3);

    printf("\nretiradas\n");
    limpiar();
    aos_notif_push_removed(4242);
    uint32_t uid = 0;
    ok("the one the phone withdrew is read",
       aos_hal_notif_pop_removed(&uid) && uid == 4242);
    ok("and it is not read twice", !aos_hal_notif_pop_removed(&uid));

    printf("\ncategoria invalida\n");
    limpiar();
    ok("a category out of range falls into 'other' and breaks nothing",
       empujar((aos_notif_category_t)99, false, false) == 3);

    printf("\n%d tests, %d failures\n\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
