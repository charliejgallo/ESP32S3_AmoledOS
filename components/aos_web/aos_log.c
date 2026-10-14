/*
 * AmoledOS - the log ring. See aos_log.h.
 *
 * How it hooks in: esp_log_set_vprintf() replaces the function esp_log_write
 * calls with the formatted line. Ours prints through the previous one -the
 * console keeps working exactly as before- and then formats the same line a
 * second time into a buffer in PSRAM, under a mutex, and appends it to the
 * ring.
 *
 * Why format twice and not capture the console's output: the console path is
 * vprintf straight into the UART/USB driver, there is no buffer to copy from.
 * Formatting twice costs microseconds per line; a log line is already a slow
 * thing.
 *
 * The ring survives a restart (2026-10-14, from P4OS's aos_logring.c): it
 * lives in a part of PSRAM a restart does not clear (EXT_RAM_NOINIT_ATTR;
 * the boot's PSRAM test skips it), with a magic and a check written after
 * every line. At the next boot, if the ring left behind is whole, it is
 * copied aside as "the previous boot" (aos_log_prev, GET /api/log?prev=1):
 * what happened before a panic, a watchdog or a hang, with no cable. A power
 * cut -the battery running out- loses it. Every line is pushed out of the
 * cache as it is written: a reset loses what is only in the cache, and the
 * last lines are the ones wanted.
 *
 * The mutex is taken with no wait. If two tasks log at the same instant the
 * second one loses its line in the ring (never on the console): dropping a
 * line beats making a task wait inside ESP_LOGx, which callers assume is
 * cheap and never blocks.
 */
#include "aos_log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LOG_LINE_MAX 256

#define RING_MAGIC   0x414F4C47u        /* "AOLG" */

typedef struct {
    uint32_t magic, check;              /* check = magic ^ total, written last */
    uint32_t total;                     /* bytes ever appended in that boot */
    uint32_t pad;
    char     data[AOS_LOG_RING_BYTES];
} ring_t;

static EXT_RAM_NOINIT_ATTR ring_t s_nr;
static char             *s_ring;        /* s_nr.data once the ring is open */
static char             *s_line;        /* PSRAM, LOG_LINE_MAX */
static size_t            s_total;       /* bytes ever appended */
static char             *s_prev;        /* the previous boot's ring, PSRAM, or NULL */
static size_t            s_prev_len;
static SemaphoreHandle_t s_mutex;
static vprintf_like_t    s_previous;

static void append(const char *data, size_t len)
{
    /* Longer than the ring is only possible with a line bigger than the
     * ring, which LOG_LINE_MAX rules out; kept for safety. */
    if (len > AOS_LOG_RING_BYTES) {
        data += len - AOS_LOG_RING_BYTES;
        len = AOS_LOG_RING_BYTES;
    }
    size_t pos = s_total % AOS_LOG_RING_BYTES;
    size_t first = AOS_LOG_RING_BYTES - pos;
    if (first > len) {
        first = len;
    }
    memcpy(s_ring + pos, data, first);
    if (len > first) {
        memcpy(s_ring, data + first, len - first);
    }
    s_total += len;

    s_nr.total = (uint32_t)s_total;
    s_nr.check = RING_MAGIC ^ s_nr.total;
    esp_cache_msync(s_ring + pos, first, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    if (len > first) {
        esp_cache_msync(s_ring, len - first, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    }
    esp_cache_msync(&s_nr, 16, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static int hook(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int n = s_previous ? s_previous(fmt, args) : vprintf(fmt, args);

    if (s_ring && s_mutex && xSemaphoreTake(s_mutex, 0) == pdTRUE) {
        int len = vsnprintf(s_line, LOG_LINE_MAX, fmt, copy);
        if (len > 0) {
            if (len >= LOG_LINE_MAX) {
                /* Truncated: keep the newline so the next line does not glue
                 * itself to this one on the page. */
                len = LOG_LINE_MAX - 1;
                s_line[len - 1] = '\n';
            }
            append(s_line, (size_t)len);
        }
        xSemaphoreGive(s_mutex);
    }
    va_end(copy);
    return n;
}

void aos_log_init(void)
{
    if (s_ring) {
        return;
    }
    s_line = heap_caps_malloc(LOG_LINE_MAX, MALLOC_CAP_SPIRAM);
    if (!s_line) {
        /* No PSRAM: the feature is off, the console is untouched. */
        return;
    }
    /* what the last boot left, if it is whole: its last AOS_LOG_RING_BYTES */
    if (s_nr.magic == RING_MAGIC && s_nr.check == (RING_MAGIC ^ s_nr.total) && s_nr.total) {
        size_t n = s_nr.total < AOS_LOG_RING_BYTES ? s_nr.total : AOS_LOG_RING_BYTES;
        s_prev = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (s_prev) {
            size_t from = s_nr.total - n;
            for (size_t i = 0; i < n; i++) {
                s_prev[i] = s_nr.data[(from + i) % AOS_LOG_RING_BYTES];
            }
            s_prev_len = n;
        }
    }
    s_nr.magic = RING_MAGIC;
    s_nr.total = 0;
    s_nr.check = RING_MAGIC;
    s_ring = s_nr.data;
    s_mutex = xSemaphoreCreateMutex();
    s_previous = esp_log_set_vprintf(hook);
}

size_t aos_log_total(void)
{
    return s_total;
}

size_t aos_log_read(size_t from, size_t until, char *out, size_t out_len,
                    size_t *actual_from)
{
    if (!s_ring || !s_mutex) {
        if (actual_from) *actual_from = 0;
        return 0;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        if (actual_from) *actual_from = from;
        return 0;
    }
    size_t total = s_total;
    if (until > total) until = total;
    size_t oldest = total > AOS_LOG_RING_BYTES ? total - AOS_LOG_RING_BYTES : 0;
    if (from < oldest) from = oldest;
    if (from > until)  from = until;

    size_t n = until - from;
    if (n > out_len) n = out_len;

    size_t pos = from % AOS_LOG_RING_BYTES;
    size_t first = AOS_LOG_RING_BYTES - pos;
    if (first > n) first = n;
    memcpy(out, s_ring + pos, first);
    if (n > first) {
        memcpy(out + first, s_ring, n - first);
    }
    xSemaphoreGive(s_mutex);

    if (actual_from) *actual_from = from;
    return n;
}

const char *aos_log_prev(size_t *len)
{
    if (len) {
        *len = s_prev_len;
    }
    return s_prev;
}
