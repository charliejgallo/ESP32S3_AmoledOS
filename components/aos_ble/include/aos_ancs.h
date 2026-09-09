/*
 * AmoledOS - ANCS client, the part that does not talk to NimBLE.
 *
 * Pure assembling and disassembling of bytes: no LVGL, no NimBLE and no
 * ESP-IDF, so it can be tested on the Mac with tools/ancs_harness.c. It is the
 * only part of phase F5 verifiable without the iPhone in front of you.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "aos_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AOS_ANCS_NADA = 0,
    AOS_ANCS_PEDIR_ATRIBUTOS,
} aos_ancs_accion_t;

/* One Notification Source packet. If it returns PEDIR_ATRIBUTOS, the command
 * from aos_ancs_cmd_atributos() has to be written to the Control Point.
 * Withdrawals it pushes to the store itself.
 *
 * The category and the flags come out THROUGH HERE and are stored by the
 * caller, alongside the UID it queues. They used to live in a table inside
 * this file, and that table overflowed: on connecting, iOS announces
 * everything it had pending all at once -twenty alerts in half a second- while
 * each one's text is requested one at a time. The first entries were evicted
 * before their text arrived, the lookup by UID failed, and the flags came back
 * zero: that is, **"not pre-existing", that is, alert on it**. What was
 * measured on the board was eight full screens of old notifications in a row.
 *
 * Travelling with the request there is no table to overflow and no default
 * value that can lie. */
aos_ancs_accion_t aos_ancs_notification_source(const uint8_t *m, uint16_t len,
                                               uint32_t *uid_out,
                                               uint8_t *cat_out,
                                               uint8_t *flags_out);

/* What has accumulated from the Data Source, with the category and the flags
 * stored when the request was queued. Returns false if a fragment is still
 * missing -in which case keep accumulating-, if the reply is not the one for
 * the request in flight, and true with 'out' complete when it has all
 * arrived. */
bool aos_ancs_data_source(const uint8_t *m, uint16_t len,
                          uint32_t uid_esperado, uint8_t cat, uint8_t flags,
                          aos_notif_t *out);

/* "yyyyMMddTHHmmSS" -> time_t. 0 on failure. */
time_t aos_ancs_fecha(const char *s, int len);

/* Commands for the Control Point. They return the bytes written, 0 if it does
 * not fit. */
int aos_ancs_cmd_atributos(uint8_t *out, size_t cap, uint32_t uid);
int aos_ancs_cmd_accion(uint8_t *out, size_t cap, uint32_t uid, bool positiva);

#ifdef __cplusplus
}
#endif
