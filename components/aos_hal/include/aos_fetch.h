/*
 * AmoledOS - A GET that follows redirects and hands the body over as it comes.
 *
 * aos_hal_http_* keeps a whole answer in memory and does not follow
 * redirects, which is right for a JSON from open-meteo and wrong for a file
 * of a GitHub release: that is two redirects away (releases/latest/download/x
 * -> releases/download/<tag>/x -> a signed address on another host) and 4 MB
 * long. This is the other half: one GET, redirects followed from host to
 * host, and the body passed to a function of the caller's piece by piece, so
 * it can go to the flash, to the card, or through a hash, without being kept.
 *
 * Internal to the firmware (the updater; the Store's downloads later). It is
 * synchronous: it runs in the caller's task, which must have its stack in
 * internal RAM when its sink writes the flash. The same file on the board and
 * in the simulator, on aos_http_stream.h.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Its own reasons, beside aos_hal.h's AOS_HTTP_ERR_* (the network's) and a
 * server's HTTP code. */
#define AOS_FETCH_ERR_URL       (-20)   /* not http(s)://, or too long */
#define AOS_FETCH_ERR_REDIRECT  (-21)   /* too many, to nowhere, or https -> http */
#define AOS_FETCH_ERR_STALL     (-22)   /* connected, and silent too long */
#define AOS_FETCH_ERR_SHORT     (-23)   /* closed before its Content-Length */
#define AOS_FETCH_ERR_SINK      (-24)   /* whoever takes the bytes said stop */
#define AOS_FETCH_ERR_CANCEL    (-25)
#define AOS_FETCH_ERR_BIG       (-26)   /* longer than max_bytes */
#define AOS_FETCH_ERR_CHUNKED   (-27)   /* a chunked body, asked in HTTP/1.0 */

/* Takes 'len' bytes of the body. false stops the download (AOS_FETCH_ERR_SINK). */
typedef bool (*aos_fetch_sink_t)(void *ctx, const uint8_t *data, size_t len);

typedef struct {
    /* in */
    const char       *url;
    aos_fetch_sink_t  sink;
    void             *ctx;
    uint32_t          max_bytes;    /* 0: no limit */
    volatile bool    *cancel;       /* may be NULL; seen within a second */
    /* out; got and total can be read from another task while it runs */
    volatile uint32_t got;
    volatile uint32_t total;        /* the Content-Length, 0 when the server did not say */
    int               redirects;
    char              host[96];     /* who sent the bytes: the chain's last host */
} aos_fetch_t;

/* Blocks until it ends. Returns the last HTTP code -2xx means the whole body
 * went to the sink- or < 0, an AOS_HTTP_ERR_* or AOS_FETCH_ERR_*. A code
 * that is not 2xx is returned as it is, with nothing passed to the sink. */
int aos_fetch_get(aos_fetch_t *f);

/* The whole body into a buffer of its own (PSRAM), NUL-terminated: for a
 * small file. *out is the caller's to free(), also when it failed (NULL then). */
int aos_fetch_text(const char *url, uint32_t max, char **out, uint32_t *len,
                   volatile bool *cancel);

/* Makes sure the date can check a certificate: when it is impossible (before
 * 2025-11-24) it syncs over NTP first, which blocks up to 8 s. Call it from a
 * job's task before an https:// fetch. */
void aos_fetch_clock(void);

/* For the log: a line about a result of aos_fetch_get, in English. */
const char *aos_fetch_reason(int rc);
