/*
 * AmoledOS - store and policy for the phone's notifications.
 *
 * Shared by the board and the simulator (like aos_http.c), and deliberately
 * so: the policy -what is shown, what makes a sound, what is discarded- is the
 * part we want to test on the Mac and then never touch again. Each platform's
 * provider only pushes what reaches it through aos_notif_push().
 *
 *      provider                  this file                    UI
 *   ANCS / phone      -->   filter + policy + queues   -->  aos_ui_tick()
 *    imaginary               (any task)                     (LVGL task)
 *
 * About the queues: they are single-producer, single-consumer, with volatile
 * indices, and that is why they carry no mutex. The producer only moves the
 * write cursor and the consumer only the read one, so there are never two
 * tasks writing the same integer. It is the same decision -and for the same
 * reason- as the capture slot in aos_ui.c: a mutex here would only make it
 * possible for NimBLE's callback to stall the drawing, which is worse than
 * losing a notification when the queue fills up.
 */
#include "aos_hal.h"
#include "aos_notif_internal.h"

#include <stdio.h>
#include <string.h>

/* History. In PSRAM: it is 16 x ~400 B and on this board what is scarce is
 * internal RAM, which is also where the .text of dynamic apps comes from. */
#define HIST_MAX        16
#define PEND_MAX        8

/* The history is a ring that only GROWS at one end: the one that pushes is the
 * provider -the radio's task- and it writes the whole slot before raising the
 * counter, so a reader arriving in the middle sees the previous one and not
 * half a notification.
 *
 * Deleting does not break that because it moves nothing: the slot's tombstone
 * is set and readers skip it. Compacting the array -which would be the obvious
 * thing- would mean moving memory from under a reader that is walking it, and
 * then a lock really would be needed. */
typedef struct {
    aos_notif_t   n;
    volatile bool viva;
} ranura_t;

AOS_BSS_PSRAM static ranura_t s_hist[HIST_MAX];
static volatile int s_hist_count;       /* running total, not the ring's */

AOS_BSS_PSRAM static aos_notif_t s_pend[PEND_MAX];
static volatile uint32_t s_pend_w, s_pend_r;

static volatile uint32_t s_removed[PEND_MAX];
static volatile uint32_t s_rem_w, s_rem_r;

/* -------------------------------------------------------------------------- */
/* Settings                                                                    */
/* -------------------------------------------------------------------------- */

#define CAT_ALL     ((1u << AOS_NOTIF_CATEGORY_COUNT) - 1u)

static bool     s_cargado;
static bool     s_enabled     = true;
static bool     s_sound       = true;
static bool     s_calls       = true;
static uint32_t s_categories  = CAT_ALL;

static void cargar(void)
{
    if (s_cargado) {
        return;
    }
    s_cargado = true;

    int32_t v;
    if (aos_hal_pref_get_i32("nt_on",    &v)) s_enabled    = (v != 0);
    if (aos_hal_pref_get_i32("nt_snd",   &v)) s_sound      = (v != 0);
    if (aos_hal_pref_get_i32("nt_calls", &v)) s_calls      = (v != 0);
    if (aos_hal_pref_get_i32("nt_cat",   &v)) s_categories = (uint32_t)v & CAT_ALL;
}

void aos_hal_notif_enable(bool on)
{
    cargar();
    s_enabled = on;
    aos_hal_pref_set_i32("nt_on", on ? 1 : 0);
}

bool aos_hal_notif_enabled(void)
{
    cargar();
    return s_enabled;
}

void aos_hal_notif_sound_set(bool on)
{
    cargar();
    s_sound = on;
    aos_hal_pref_set_i32("nt_snd", on ? 1 : 0);
}

bool aos_hal_notif_sound(void)
{
    cargar();
    return s_sound;
}

void aos_hal_notif_calls_always_set(bool on)
{
    cargar();
    s_calls = on;
    aos_hal_pref_set_i32("nt_calls", on ? 1 : 0);
}

bool aos_hal_notif_calls_always(void)
{
    cargar();
    return s_calls;
}

void aos_hal_notif_categories_set(uint32_t mask)
{
    cargar();
    s_categories = mask & CAT_ALL;
    aos_hal_pref_set_i32("nt_cat", (int32_t)s_categories);
}

uint32_t aos_hal_notif_categories(void)
{
    cargar();
    return s_categories;
}

/* -------------------------------------------------------------------------- */
/* The policy, which is the whole point of this file                           */
/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Grouping bursts
 *
 * A WhatsApp conversation is eight alerts in forty seconds, all from the same
 * app and with the same title -the name of whoever is writing-. Showing them
 * as eight separate notifications is what makes the watch annoying to use
 * exactly while you are talking to somebody.
 *
 * They are not discarded: the front one is REPLACED by the new one -you want
 * to read the latest, not the first- and how many there have been is counted.
 * What is switched off is the noise: the first of the burst makes a sound and
 * none of the rest. Finding out once that somebody is writing is enough.
 * -------------------------------------------------------------------------- */

#define AGRUPA_MS   60000

static char                 s_ult_app[40];
static char                 s_ult_title[64];
static aos_notif_category_t s_ult_cat;
static uint64_t             s_ult_ms;
static uint16_t             s_ult_n;

static bool es_llamada(aos_notif_category_t c)
{
    return c == AOS_NOTIF_CALL_INCOMING || c == AOS_NOTIF_CALL_MISSED;
}

/* Returns false if the notification is discarded entirely: no alert and no
 * storing either. If it accepts it, it fills in 'alert' and 'sound'.
 *
 * The order matters and it is this:
 *
 *   1. a call with "calls always" set overrides everything else, the category
 *      filter and "do not disturb" included. It is the one category where not
 *      finding out has a real cost;
 *   2. the category filter discards entirely, because "I do not want the news
 *      ones" means that and not "store them quietly for me";
 *   3. those already on the phone at connection time NEVER alert. Without
 *      this, plugging the watch in is thirty screens in a row: iOS dumps
 *      everything pending with EventFlagPreExisting on connecting;
 *   4. "do not disturb" switches the alert off but keeps the history, which is
 *      precisely what distinguishes it from switching bluetooth off;
 *   5. the silence the phone sends is always honoured. On the other side
 *      somebody has already decided.
 */
static bool politica(aos_notif_t *n)
{
    cargar();

    bool prioritaria = s_calls && es_llamada(n->category);

    if (!prioritaria && !(s_categories & (1u << (unsigned)n->category))) {
        return false;                                   /* 2 */
    }

    if (n->pre_existing) {                              /* 3 */
        n->alert = false;
        n->sound = false;
        return true;
    }

    n->alert = prioritaria || s_enabled;                /* 1, 4 */
    n->sound = n->alert && (prioritaria || (s_sound && !n->silent));  /* 5 */

    /* 6. Grouping: same app, same title and same category, within the minute.
     *    A call is NEVER grouped: the bench caught it when the grouping took
     *    the sound away from an incoming call for sharing an app and title
     *    with the previous message. The one thing that cannot be lost is
     *    precisely that. */
    n->repeticiones = 1;
    if (n->alert && !prioritaria) {
        uint64_t ahora = aos_hal_uptime_ms();
        bool misma = s_ult_n > 0 &&
                     (ahora - s_ult_ms) < AGRUPA_MS &&
                     s_ult_cat == n->category &&
                     strcmp(s_ult_app, n->app) == 0 &&
                     strcmp(s_ult_title, n->title) == 0;
        if (misma) {
            if (s_ult_n < 0xFFFF) {
                s_ult_n++;
            }
            n->sound = false;               /* the first of the burst and no more */
        } else {
            s_ult_n   = 1;
            s_ult_cat = n->category;
            snprintf(s_ult_app, sizeof(s_ult_app), "%s", n->app);
            snprintf(s_ult_title, sizeof(s_ult_title), "%s", n->title);
        }
        n->repeticiones = s_ult_n;
        s_ult_ms = ahora;
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Input: only the providers use this                                          */
/* -------------------------------------------------------------------------- */

bool aos_notif_push(const aos_notif_t *in)
{
    if (!in) {
        return false;
    }

    aos_notif_t n = *in;
    if (n.category >= AOS_NOTIF_CATEGORY_COUNT) {
        n.category = AOS_NOTIF_OTHER;
    }
    if (!politica(&n)) {
        return false;
    }

    /* History. The WHOLE slot is written and only then published by raising
     * the counter: that way a reader arriving in the middle sees the previous
     * one and not half a notification. */
    s_hist[s_hist_count % HIST_MAX].n = n;
    s_hist[s_hist_count % HIST_MAX].viva = true;
    __sync_synchronize();
    s_hist_count++;

    /* Queue of new arrivals. If it is full the oldest is lost: we would rather
     * show the latest thing that arrived than stall the provider. */
    uint32_t w = s_pend_w;
    if (w - s_pend_r >= PEND_MAX) {
        s_pend_r++;
    }
    s_pend[w % PEND_MAX] = n;
    __sync_synchronize();
    s_pend_w = w + 1;
    return true;
}

void aos_notif_push_removed(uint32_t uid)
{
    /* The phone dismissed it: it goes off the watch's list too. */
    aos_hal_notif_remove(uid);

    uint32_t w = s_rem_w;
    if (w - s_rem_r >= PEND_MAX) {
        s_rem_r++;
    }
    s_removed[w % PEND_MAX] = uid;
    __sync_synchronize();
    s_rem_w = w + 1;
}

void aos_notif_reset_pending(void)
{
    s_pend_r = s_pend_w;
    s_rem_r  = s_rem_w;
}

static volatile bool s_accion_fallo;

void aos_notif_action_failed(void)
{
    s_accion_fallo = true;
}

bool aos_hal_notif_action_failed(void)
{
    bool v = s_accion_fallo;
    s_accion_fallo = false;
    return v;
}

/* -------------------------------------------------------------------------- */
/* Output: the UI                                                              */
/* -------------------------------------------------------------------------- */

bool aos_hal_notif_pop(aos_notif_t *out)
{
    uint32_t r = s_pend_r;
    if (r == s_pend_w) {
        return false;
    }
    if (out) {
        *out = s_pend[r % PEND_MAX];
    }
    s_pend_r = r + 1;
    return true;
}

bool aos_hal_notif_pop_removed(uint32_t *uid)
{
    uint32_t r = s_rem_r;
    if (r == s_rem_w) {
        return false;
    }
    if (uid) {
        *uid = s_removed[r % PEND_MAX];
    }
    s_rem_r = r + 1;
    return true;
}

/* How many slots of the ring are occupied, deleted or not. */
static int guardadas(void)
{
    int n = s_hist_count;
    return n > HIST_MAX ? HIST_MAX : n;
}

int aos_hal_notif_count(void)
{
    int total = s_hist_count;
    int vivas = 0;
    for (int i = 0; i < guardadas(); i++) {
        if (s_hist[(total - 1 - i) % HIST_MAX].viva) {
            vivas++;
        }
    }
    return vivas;
}

/* 0 is the newest of those remaining. */
bool aos_hal_notif_at(int index, aos_notif_t *out)
{
    if (!out || index < 0) {
        return false;
    }
    int total = s_hist_count;
    int visto = 0;
    for (int i = 0; i < guardadas(); i++) {
        const ranura_t *r = &s_hist[(total - 1 - i) % HIST_MAX];
        if (!r->viva) {
            continue;
        }
        if (visto++ == index) {
            *out = r->n;
            return true;
        }
    }
    return false;
}

/* Deleting a single one.
 *
 * Two sides call this: the user, from the list, and **the phone itself** when
 * it dismisses the notification over there. The second is what makes the
 * Reject button take it off the list and not merely close the screen:
 * rejecting makes the phone dismiss it, the phone says so, and it disappears
 * here. A watch showing alerts that are no longer on the phone is a watch that
 * lies. */
bool aos_hal_notif_remove(uint32_t uid)
{
    int total = s_hist_count;
    for (int i = 0; i < guardadas(); i++) {
        ranura_t *r = &s_hist[(total - 1 - i) % HIST_MAX];
        if (r->viva && r->n.uid == uid) {
            r->viva = false;
            return true;
        }
    }
    return false;
}

void aos_hal_notif_clear(void)
{
    s_hist_count = 0;
    s_ult_n      = 0;
    for (int i = 0; i < HIST_MAX; i++) {
        s_hist[i].viva = false;
    }
    aos_notif_reset_pending();
}
