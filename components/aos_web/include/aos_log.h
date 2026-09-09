/*
 * AmoledOS - the log, kept in a ring so the portal can show it.
 *
 * Everything ESP_LOGx prints goes on to the console as before, and a copy
 * lands in a 16 KB ring in PSRAM. /api/log serves it. It exists because the
 * USB console dies with light sleep and the board disappears from the Mac
 * until it is plugged in again: with this, the log is read over wifi.
 *
 * Internal RAM cost: a mutex and a handful of pointers. The ring and the line
 * buffer are in PSRAM.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_LOG_RING_BYTES  16384

/* Hooks esp_log. Call once, as early as possible in app_main: what is logged
 * before is not captured. Safe to call when PSRAM is missing: it then does
 * nothing and the reads return empty. */
void aos_log_init(void);

/* Bytes ever written, monotonic. The offset a reader keeps between calls. */
size_t aos_log_total(void);

/* Copies into 'out' up to 'out_len' bytes starting at offset 'from' and never
 * past 'until' (pass aos_log_total() taken beforehand, so a stream has a fixed
 * end while it is sent). If 'from' is older than what the ring still holds it
 * is moved forward; the offset actually used comes back in *actual_from.
 * Returns the number of bytes copied. */
size_t aos_log_read(size_t from, size_t until, char *out, size_t out_len,
                    size_t *actual_from);

#ifdef __cplusplus
}
#endif
