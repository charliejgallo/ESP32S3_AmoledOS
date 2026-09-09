/*
 * AmoledOS - ANCS client: from the iPhone's bytes to an aos_notif_t.
 *
 * This half does not talk to NimBLE: it receives the packets already
 * assembled and hands notifications to the store. Separate from the rest so it
 * can be tested on the Mac with tools/ancs_harness.c, which is the only part
 * of phase F5 verifiable without the phone.
 *
 * The two packets that arrive:
 *
 *   Notification Source (8 bytes, unfragmented)
 *     [0] EventID  [1] EventFlags  [2] CategoryID  [3] CategoryCount
 *     [4..7] NotificationUID, little endian
 *
 *   Data Source (reply to GetNotificationAttributes, FRAGMENTED)
 *     [0] CommandID  [1..4] NotificationUID
 *     then, per attribute: [id] [len lo] [len hi] [len bytes of text]
 *
 * The text is NOT zero-terminated and is NOT necessarily complete UTF-8: ANCS
 * clips by byte count, so the last character may arrive cut in half.
 * aos_text_safe() deals with that when drawing; here all we have to do is copy
 * without overrunning.
 */
#include "aos_ancs.h"
#include "aos_notif_internal.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

/* --- ANCS, from Apple's document ----------------------------------------- */

#define EVENT_ADDED             0
#define EVENT_MODIFIED          1
#define EVENT_REMOVED           2

#define FLAG_SILENT             (1u << 0)
#define FLAG_IMPORTANT          (1u << 1)
#define FLAG_PRE_EXISTING       (1u << 2)
#define FLAG_POSITIVE_ACTION    (1u << 3)
#define FLAG_NEGATIVE_ACTION    (1u << 4)

#define ATTR_APP_ID             0
#define ATTR_TITLE              1
#define ATTR_SUBTITLE           2
#define ATTR_MESSAGE            3
#define ATTR_MESSAGE_SIZE       4
#define ATTR_DATE               5

#define CMD_GET_ATTRIBUTES      0

/* How many attributes are requested, in this order. The reply arrives in the
 * same order, and that is what makes it possible to know when it has finished
 * without depending on a timer (see aos_ancs_data_source). */
#define ATTRS_PEDIDOS           5

/* --------------------------------------------------------------------------
 * Translations
 * -------------------------------------------------------------------------- */

/* ANCS's CategoryIDs are 0..11 and aos_notif_category_t was written in the
 * same order on purpose, but the equality is checked and not assumed: if
 * either list changes, this has to fail at compile time and not on somebody's
 * wrist. */
_Static_assert((int)AOS_NOTIF_ENTERTAINMENT == 11,
               "el orden de aos_notif_category_t dejo de coincidir con ANCS");
_Static_assert((int)AOS_NOTIF_CATEGORY_COUNT == 12,
               "ANCS define doce categorias");

static aos_notif_category_t categoria(uint8_t ancs)
{
    return (ancs < AOS_NOTIF_CATEGORY_COUNT) ? (aos_notif_category_t)ancs
                                             : AOS_NOTIF_OTHER;
}

/* ANCS's date is "yyyyMMdd'T'HHmmSS", 15 characters and with no time zone: it
 * is the phone's local time. Since the watch also runs on local time, it is
 * built with mktime and that is that.
 *
 * If it cannot be read, 0 is returned and the caller uses the current time. A
 * freshly arrived notification has that date anyway; the only ones where the
 * difference matters are those already on the phone at connection time. */
time_t aos_ancs_fecha(const char *s, int len)
{
    if (!s || len < 15 || s[8] != 'T') {
        return 0;
    }
    for (int i = 0; i < 15; i++) {
        if (i != 8 && (s[i] < '0' || s[i] > '9')) {
            return 0;
        }
    }

    #define N2(p)  ((s[p] - '0') * 10 + (s[p + 1] - '0'))
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = N2(0) * 100 + N2(2) - 1900;
    t.tm_mon  = N2(4) - 1;
    t.tm_mday = N2(6);
    t.tm_hour = N2(9);
    t.tm_min  = N2(11);
    t.tm_sec  = N2(13);
    t.tm_isdst = -1;
    #undef N2

    if (t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_hour > 23) {
        return 0;
    }
    time_t r = mktime(&t);
    return (r == (time_t)-1) ? 0 : r;
}

/* Copies without overrunning and zero-terminates. ANCS's text is not
 * terminated. */
static void copiar(char *dst, size_t cap, const uint8_t *src, uint16_t len)
{
    if (len >= cap) {
        len = (uint16_t)(cap - 1);
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* --------------------------------------------------------------------------
 * Notification Source
 * -------------------------------------------------------------------------- */

aos_ancs_accion_t aos_ancs_notification_source(const uint8_t *m, uint16_t len,
                                               uint32_t *uid_out,
                                               uint8_t *cat_out,
                                               uint8_t *flags_out)
{
    if (!m || len < 8 || !uid_out || !cat_out || !flags_out) {
        return AOS_ANCS_NADA;
    }

    uint8_t  evento  = m[0];
    uint8_t  flags   = m[1];
    uint8_t  cat     = m[2];
    uint32_t uid     = (uint32_t)m[4] | ((uint32_t)m[5] << 8) |
                       ((uint32_t)m[6] << 16) | ((uint32_t)m[7] << 24);
    *uid_out   = uid;
    *cat_out   = cat;
    *flags_out = flags;

    if (evento == EVENT_REMOVED) {
        aos_notif_push_removed(uid);
        return AOS_ANCS_NADA;
    }

    /* Modified ones are requested too: a missed call arrives as a
     * "modification" of the incoming one, and the title changes. */
    return AOS_ANCS_PEDIR_ATRIBUTOS;
}

/* --------------------------------------------------------------------------
 * Data Source
 *
 * It arrives fragmented into chunks of MTU-3 bytes. To know when it has
 * finished, Espressif's example starts a half-second timer every time a chunk
 * arrives full; that works nearly always and fails exactly when the message is
 * an exact multiple of the MTU.
 *
 * Here it is counted instead: ATTRS_PEDIDOS attributes were requested, the
 * reply brings them in the same order, and each one declares its length.
 * Whatever is there is walked and it is known with certainty whether it is
 * complete or something is missing. No timer and no heuristic.
 * -------------------------------------------------------------------------- */

bool aos_ancs_data_source(const uint8_t *m, uint16_t len,
                          uint32_t uid_esperado, uint8_t cat, uint8_t flags,
                          aos_notif_t *out)
{
    if (!m || !out || len < 5 || m[0] != CMD_GET_ATTRIBUTES) {
        return false;
    }

    uint32_t uid = (uint32_t)m[1] | ((uint32_t)m[2] << 8) |
                   ((uint32_t)m[3] << 16) | ((uint32_t)m[4] << 24);

    /* The reply has to be the one for the request in flight. If it is not, it
     * is one that arrived late after a timeout: discarding it is better than
     * mixing it with another one's category. */
    if (uid != uid_esperado) {
        return false;
    }

    /* First pass: are all five attributes there in full? */
    uint16_t i = 5;
    int      vistos = 0;
    while (vistos < ATTRS_PEDIDOS) {
        if ((uint32_t)i + 3 > len) {
            return false;                       /* the header is missing */
        }
        uint16_t alargo = (uint16_t)(m[i + 1] | (m[i + 2] << 8));
        if ((uint32_t)i + 3 + alargo > len) {
            return false;                       /* the text is missing */
        }
        i = (uint16_t)(i + 3 + alargo);
        vistos++;
    }

    /* Second pass: we now know it is complete, so we copy. */
    memset(out, 0, sizeof(*out));
    out->uid = uid;

    char subtitulo[64] = "";
    time_t cuando = 0;

    i = 5;
    for (int n = 0; n < ATTRS_PEDIDOS; n++) {
        uint8_t        aid    = m[i];
        uint16_t       alargo = (uint16_t)(m[i + 1] | (m[i + 2] << 8));
        const uint8_t *texto  = &m[i + 3];

        switch (aid) {
        case ATTR_APP_ID:
            /* It arrives as "com.burbn.instagram". The pretty name is
             * requested with another command; for now the last component is
             * enough, which is what you recognise at a glance. */
            {
                char id[64];
                copiar(id, sizeof(id), texto, alargo);
                const char *punto = strrchr(id, '.');
                const char *corto = (punto && punto[1]) ? punto + 1 : id;
                /* With an explicit precision and not a bare %s: the identifier
                 * can be longer than the box and snprintf would truncate
                 * anyway, but -Werror=format-truncation treats that as an
                 * error. Saying how much fits is also more honest than leaving
                 * it implicit. */
                snprintf(out->app, sizeof(out->app), "%.*s",
                         (int)sizeof(out->app) - 1, corto);
            }
            break;
        case ATTR_TITLE:    copiar(out->title, sizeof(out->title), texto, alargo); break;
        case ATTR_SUBTITLE: copiar(subtitulo, sizeof(subtitulo), texto, alargo);   break;
        case ATTR_MESSAGE:  copiar(out->message, sizeof(out->message), texto, alargo); break;
        case ATTR_DATE:
            cuando = aos_ancs_fecha((const char *)texto, alargo);
            break;
        default:
            break;
        }
        i = (uint16_t)(i + 3 + alargo);
    }

    /* iOS's subtitle is the second line of the header -the group's name in a
     * chat, the sender in a thread-, and without it a group notification loses
     * whose it is. It goes before the message separated by a middle dot, which
     * is a character the font has. */
    if (subtitulo[0] && out->message[0]) {
        char junto[sizeof(out->message)];
        snprintf(junto, sizeof(junto), "%.*s \xC2\xB7 %.*s",
                 (int)sizeof(subtitulo) - 1, subtitulo,
                 (int)sizeof(out->message) - (int)sizeof(subtitulo) - 4,
                 out->message);
        memcpy(out->message, junto, sizeof(out->message));
    } else if (subtitulo[0]) {
        snprintf(out->message, sizeof(out->message), "%.*s",
                 (int)sizeof(subtitulo) - 1, subtitulo);
    }

    out->category     = categoria(cat);
    out->silent       = (flags & FLAG_SILENT) != 0;
    out->important    = (flags & FLAG_IMPORTANT) != 0;
    out->pre_existing = (flags & FLAG_PRE_EXISTING) != 0;
    out->can_positive = (flags & FLAG_POSITIVE_ACTION) != 0;
    out->can_negative = (flags & FLAG_NEGATIVE_ACTION) != 0;
    out->when         = cuando ? cuando : time(NULL);
    return true;
}

/* --------------------------------------------------------------------------
 * Building the commands written to the Control Point
 * -------------------------------------------------------------------------- */

int aos_ancs_cmd_atributos(uint8_t *out, size_t cap, uint32_t uid)
{
    /* Maximum length per attribute, IN BYTES. It is the number that makes the
     * last character able to arrive cut in half, and aos_text_safe() deals
     * with that when drawing. 200 of message are requested and not 256: the
     * overlay's box shows no more than that, and every extra byte is one more
     * radio fragment. */
    static const struct { uint8_t id; uint16_t max; } P[ATTRS_PEDIDOS] = {
        { ATTR_APP_ID,   0   },
        { ATTR_TITLE,    48  },
        { ATTR_SUBTITLE, 48  },
        { ATTR_MESSAGE,  200 },
        { ATTR_DATE,     0   },
    };

    if (cap < 5 + ATTRS_PEDIDOS * 3) {
        return 0;
    }

    size_t i = 0;
    out[i++] = CMD_GET_ATTRIBUTES;
    out[i++] = (uint8_t)(uid & 0xFF);
    out[i++] = (uint8_t)((uid >> 8) & 0xFF);
    out[i++] = (uint8_t)((uid >> 16) & 0xFF);
    out[i++] = (uint8_t)((uid >> 24) & 0xFF);

    for (int n = 0; n < ATTRS_PEDIDOS; n++) {
        out[i++] = P[n].id;
        /* The length is only sent for the attributes that accept it. Sending
         * it where it does not belong -in AppIdentifier or in Date- makes the
         * phone answer "invalid command" and nothing arrives, with no further
         * explanation. */
        if (P[n].max) {
            out[i++] = (uint8_t)(P[n].max & 0xFF);
            out[i++] = (uint8_t)((P[n].max >> 8) & 0xFF);
        }
    }
    return (int)i;
}

int aos_ancs_cmd_accion(uint8_t *out, size_t cap, uint32_t uid, bool positiva)
{
    if (cap < 6) {
        return 0;
    }
    out[0] = 2;                     /* CommandIDPerformNotificationAction */
    out[1] = (uint8_t)(uid & 0xFF);
    out[2] = (uint8_t)((uid >> 8) & 0xFF);
    out[3] = (uint8_t)((uid >> 16) & 0xFF);
    out[4] = (uint8_t)((uid >> 24) & 0xFF);
    out[5] = positiva ? 0 : 1;      /* ActionIDPositive / Negative */
    return 6;
}
