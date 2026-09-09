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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define LOG_LINE_MAX 256

static char             *s_ring;        /* PSRAM, AOS_LOG_RING_BYTES */
static char             *s_line;        /* PSRAM, LOG_LINE_MAX */
static size_t            s_total;       /* bytes ever appended */
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
    s_ring = heap_caps_malloc(AOS_LOG_RING_BYTES, MALLOC_CAP_SPIRAM);
    s_line = heap_caps_malloc(LOG_LINE_MAX, MALLOC_CAP_SPIRAM);
    if (!s_ring || !s_line) {
        /* No PSRAM: the feature is off, the console is untouched. */
        free(s_ring);
        free(s_line);
        s_ring = NULL;
        s_line = NULL;
        return;
    }
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
