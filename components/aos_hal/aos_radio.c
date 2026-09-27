/*
 * AmoledOS - an internet radio as a source of MP3 bytes. See aos_radio.h.
 *
 * What a station URL turns out to be, in the order this file meets it (all
 * seen against real stations of radio-browser.info on 2026-09-26):
 *
 *  - A redirect, often two. StreamTheWorld sends a 302 to a numbered edge
 *    server; Mediainbox sends a RELATIVE one ("Location: /cadena3/..."), so
 *    the Location is resolved against the URL that gave it. The address the
 *    redirect gives carries a token that expires: after a drop the reader
 *    starts again from the station's own URL, never from the last one.
 *  - A playlist (.pls, .m3u) instead of audio: its first http(s) line is the
 *    stream. An HLS playlist (.m3u8 with #EXT-X-...) is segments of AAC, not
 *    a stream, and is refused with that reason.
 *  - The audio, answered as "HTTP/1.0 200 OK" (Icecast), "HTTP/1.1 200 OK"
 *    or "ICY 200 OK" (Shoutcast 1). HTTP/1.0 is asked for, so none of them
 *    should send it chunked; one that does anyway is taken apart here.
 *  - With "Icy-MetaData: 1" in the request, the server says icy-metaint:
 *    every that many bytes of audio comes one length byte (x16) and that
 *    many bytes of "StreamTitle='Artist - Title';". They are taken out
 *    before the decoder sees anything, and the title is remembered with the
 *    audio byte where it arrived, so the player can show it when that audio
 *    is HEARD, seconds later, and not when it was downloaded.
 *
 * The ring is 192 KB of PSRAM: 12 s at 128 kbps, 5 at 320. It is what a
 * pause holds, and what a WiFi hiccup is paid from. When it stays full (the
 * user paused) for 15 s the connection is let go - the server would drop a
 * listener that does not read anyway - and taken again once the player has
 * drunk half of it.
 *
 * Everything the reader owns lives in one context, counted by two
 * references: the reader's and the current station's. Stopping never waits
 * for the reader: a TLS handshake can hold it for ten seconds, and stop is
 * called from the UI. The old reader notices on its next second, lets go
 * and frees what it owned, while the next station already has its own.
 */
#include "aos_radio.h"
#include "aos_http_stream.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef AOS_SIM
  #include <pthread.h>
  #include <unistd.h>
  typedef pthread_mutex_t radio_lock_t;
  static radio_lock_t s_lock = PTHREAD_MUTEX_INITIALIZER;
  #define LOCK()          pthread_mutex_lock(&s_lock)
  #define UNLOCK()        pthread_mutex_unlock(&s_lock)
  #define SLEEP_MS(ms)    usleep((useconds_t)(ms) * 1000)
  #define BIG_ALLOC(n)    calloc(1, (n))
#else
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_heap_caps.h"
  #include "esp_log.h"
  static SemaphoreHandle_t s_lock_h;
  static StaticSemaphore_t s_lock_buf;
  #define LOCK()          xSemaphoreTake(s_lock_h, portMAX_DELAY)
  #define UNLOCK()        xSemaphoreGive(s_lock_h)
  #define SLEEP_MS(ms)    vTaskDelay(pdMS_TO_TICKS(ms))
  #define BIG_ALLOC(n)    heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM)
#endif

/* AOS_RADIO_DEBUG=1 in the simulator (or the bench) prints the HLS reader's
 * steps with their times; on the board it is nothing. */
#ifdef AOS_SIM
  #include <time.h>
  static double dbg_now(void)
  {
      struct timespec t;
      clock_gettime(CLOCK_MONOTONIC, &t);
      return t.tv_sec + t.tv_nsec / 1e9;
  }
  #define DBG(...) do { if (getenv("AOS_RADIO_DEBUG")) { printf("[radio %8.2f] ", dbg_now()); \
                        printf(__VA_ARGS__); printf("\n"); } } while (0)
  #define WARN(...) do { printf("[radio] "); printf(__VA_ARGS__); printf("\n"); } while (0)
#else
  #define DBG(...) do { } while (0)
  #define WARN(...) ESP_LOGW("radio", __VA_ARGS__)
#endif

#define RING_BYTES      (192 * 1024)
#define RECV_CHUNK      2048
#define HDR_MAX         4096
#define PLAYLIST_MAX    8192
#define MAX_REDIRECTS   6
#define IDLE_DROP_S     12          /* connected and silent this long: reconnect */
#define FULL_DROP_MS    15000       /* ring full this long (paused): let go */
#define GIVE_UP_AFTER   8           /* failed attempts in a row, none with audio */
#define META_KEEP       4

typedef struct {
    uint32_t pos;                   /* audio byte (head count) where it arrived */
    uint32_t gen;
    char     title[128];
} meta_t;

typedef struct {
    int      refs;
    volatile bool stop;
    void    *task;                  /* the reader's TaskHandle_t, board only */
    char     url[256];              /* the station's own */

    uint8_t *ring;
    uint32_t head, tail;            /* bytes, counted forever */

    /* status, under the lock */
    aos_radio_state_t state;
    char     error[64];
    char     host[64];
    bool     tls;
    char     icy_name[64], icy_genre[48], icy_url[96], icy_desc[96];
    char     content_type[32];
    uint16_t kbps;
    uint32_t reconnects;
    bool     got_audio;             /* some audio ever arrived */
    bool     ready;                 /* the prebuffer was reached once */
    bool     failed;

    meta_t   meta[META_KEEP];
    int      meta_n;
    uint32_t meta_gen;
    char     last_title[128];

    /* the reader's parsing state, its alone */
    uint32_t metaint, audio_left;
    int      meta_len, meta_got;
    char     meta_buf[4096 + 1];
    bool     chunked;
    int      chunk_state;           /* 0 size, 1 size line rest, 2 data, 3 CRLF */
    uint32_t chunk_left;

    /* HLS (branch aac) */
    bool     hls;
    volatile bool fetching;         /* a segment or a playlist on its way */
    bool     ts;                    /* this segment is MPEG-TS */
    uint8_t  tsbuf[188];
    int      tsfill;
    int      pmt_pid, audio_pid, audio_type;
    uint32_t skip;                  /* bytes still to drop: a segment's ID3 tag */
} radio_ctx_t;

static radio_ctx_t        *s_cur;
static aos_radio_status_t  s_last;      /* the last station's, after it stopped */

static void lock_init(void)
{
#ifndef AOS_SIM
    if (!s_lock_h) {
        s_lock_h = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
#endif
}

static void ctx_release(radio_ctx_t *c)
{
    LOCK();
    bool last = --c->refs == 0;
    UNLOCK();
    if (last) {
        free(c->ring);
        free(c);
    }
}

static void set_state(radio_ctx_t *c, aos_radio_state_t st, const char *error)
{
    LOCK();
    c->state = st;
    if (error) {
        snprintf(c->error, sizeof(c->error), "%s", error);
    }
    UNLOCK();
}

/* ---- text ---------------------------------------------------------------- */

/* A bounded copy that does not cut a UTF-8 character in half. (And that GCC
 * does not take for a truncated snprintf, which is an error on the board.) */
static void scopy(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap - 1);
    if (n == cap - 1) {
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
            n--;
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void scat(char *dst, size_t cap, const char *src)
{
    size_t have = strnlen(dst, cap - 1);
    scopy(dst + have, cap - have, src);
}

static bool valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 :
                (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0) {
            return false;
        }
        p++;
        while (n--) {
            if ((*p & 0xC0) != 0x80) {
                return false;
            }
            p++;
        }
    }
    return true;
}

/* Titles come in UTF-8 or in Latin-1, and nothing says which: what is not
 * valid UTF-8 is taken as Latin-1, which is what the rest of the world's
 * stations send. */
/* Some stations encode twice: "Así" arrives as "AsÃ­" (C3 83 C2 AD), UTF-8
 * read as Latin-1 and encoded again (METRO 95.1, 2026-09-26). Valid UTF-8
 * whose characters all fit in a byte, and whose bytes are UTF-8 again, is
 * undone once. */
static bool undo_double_utf8(char *s)
{
    char tmp[256];
    size_t o = 0;
    bool high = false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned cp;
        if (*p < 0x80) {
            cp = *p;
        } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((unsigned)(*p & 0x1F) << 6) | (p[1] & 0x3F);
            p++;
        } else {
            return false;               /* a character past Latin-1: it is real UTF-8 */
        }
        if (cp > 0xFF || o + 1 >= sizeof(tmp)) {
            return false;
        }
        high |= cp >= 0x80;
        tmp[o++] = (char)cp;
    }
    tmp[o] = '\0';
    if (!high || !valid_utf8(tmp)) {
        return false;
    }
    memcpy(s, tmp, o + 1);
    return true;
}

static void to_utf8(char *dst, size_t cap, const char *src)
{
    if (valid_utf8(src)) {
        scopy(dst, cap, src);
        undo_double_utf8(dst);
        return;
    }
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && o + 3 < cap; p++) {
        if (*p < 0x80) {
            dst[o++] = (char)*p;
        } else {
            dst[o++] = (char)(0xC0 | (*p >> 6));
            dst[o++] = (char)(0x80 | (*p & 0x3F));
        }
    }
    dst[o] = '\0';
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (i) {
        memmove(s, s + i, n - i + 1);
    }
}

/* ---- URLs ---------------------------------------------------------------- */

typedef struct {
    bool tls;
    char host[64];
    int  port;
    char path[400];
} url_t;

static bool url_parse(const char *url, url_t *u)
{
    memset(u, 0, sizeof(*u));
    const char *h;
    if (strncasecmp(url, "http://", 7) == 0) {
        h = url + 7;
        u->port = 80;
    } else if (strncasecmp(url, "https://", 8) == 0) {
        h = url + 8;
        u->tls = true;
        u->port = 443;
    } else {
        return false;
    }
    const char *end = h + strcspn(h, "/?#");
    const char *at = memchr(h, '@', (size_t)(end - h));
    if (at) {
        h = at + 1;                     /* user:pass@ - not for radios */
    }
    const char *colon = memchr(h, ':', (size_t)(end - h));
    const char *hend = colon ? colon : end;
    if (hend == h || (size_t)(hend - h) >= sizeof(u->host)) {
        return false;
    }
    memcpy(u->host, h, (size_t)(hend - h));
    if (colon) {
        u->port = atoi(colon + 1);
        if (u->port <= 0 || u->port > 65535) {
            return false;
        }
    }
    if (*end == '/') {
        snprintf(u->path, sizeof(u->path), "%s", end);
    } else {
        snprintf(u->path, sizeof(u->path), "/%s", end);
    }
    char *hash = strchr(u->path, '#');
    if (hash) {
        *hash = '\0';
    }
    return true;
}

/* A Location resolved against the URL that sent it. */
static void url_resolve(char *out, size_t cap, const url_t *base, const char *loc)
{
    if (strncasecmp(loc, "http://", 7) == 0 || strncasecmp(loc, "https://", 8) == 0) {
        snprintf(out, cap, "%s", loc);
        return;
    }
    const char *scheme = base->tls ? "https" : "http";
    bool dflt = base->port == (base->tls ? 443 : 80);
    char hostport[80];
    if (dflt) {
        snprintf(hostport, sizeof(hostport), "%s", base->host);
    } else {
        snprintf(hostport, sizeof(hostport), "%s:%d", base->host, base->port);
    }
    if (loc[0] == '/' && loc[1] == '/') {
        snprintf(out, cap, "%s:%s", scheme, loc);
    } else if (loc[0] == '/') {
        snprintf(out, cap, "%s://%s%s", scheme, hostport, loc);
    } else {
        char dir[400];
        snprintf(dir, sizeof(dir), "%s", base->path);
        char *q = strchr(dir, '?');
        if (q) {
            *q = '\0';
        }
        char *slash = strrchr(dir, '/');
        if (slash) {
            slash[1] = '\0';
        }
        snprintf(out, cap, "%s://%s", scheme, hostport);
        scat(out, cap, dir);
        scat(out, cap, loc);
    }
}

static bool ends_with(const char *path, const char *ext)
{
    char p[400];
    snprintf(p, sizeof(p), "%s", path);
    char *q = strchr(p, '?');
    if (q) {
        *q = '\0';
    }
    size_t n = strlen(p), e = strlen(ext);
    return n >= e && strcasecmp(p + n - e, ext) == 0;
}

/* ---- the ring ------------------------------------------------------------ */

/* Puts audio in. Waits while it is full; false when told to stop, or when it
 * stayed full so long that the connection should go. */
static bool ring_put(radio_ctx_t *c, const uint8_t *data, int len, bool *let_go)
{
    int full_ms = 0;
    while (len > 0) {
        if (c->stop) {
            return false;
        }
        LOCK();
        uint32_t room = RING_BYTES - (c->head - c->tail);
        uint32_t n = (uint32_t)len < room ? (uint32_t)len : room;
        for (uint32_t done = 0; done < n;) {
            uint32_t at = (c->head + done) % RING_BYTES;
            uint32_t run = RING_BYTES - at;
            if (run > n - done) {
                run = n - done;
            }
            memcpy(c->ring + at, data + done, run);
            done += run;
        }
        c->head += n;
        UNLOCK();
        data += n;
        len -= (int)n;
        if (len > 0) {
            SLEEP_MS(50);
            full_ms += 50;
            if (full_ms >= FULL_DROP_MS) {
                *let_go = true;
                return false;
            }
        }
    }
    return true;
}

/* ---- ICY metadata ---------------------------------------------------------- */

static void meta_parse(radio_ctx_t *c, const char *block)
{
    const char *k = strstr(block, "StreamTitle='");
    if (!k) {
        return;
    }
    k += 13;
    /* the value ends at "';" - a title may carry apostrophes of its own */
    const char *e = strstr(k, "';");
    if (!e) {
        e = strrchr(k, '\'');
    }
    if (!e) {
        e = k + strlen(k);
    }
    char raw[128];
    size_t n = (size_t)(e - k);
    if (n >= sizeof(raw)) {
        n = sizeof(raw) - 1;
    }
    memcpy(raw, k, n);
    raw[n] = '\0';
    char title[128];
    to_utf8(title, sizeof(title), raw);
    trim(title);
    /* "-", " - ", "...": a station between songs. Nothing to show. */
    bool words = false;
    for (const unsigned char *p = (const unsigned char *)title; *p && !words; p++) {
        words = isalnum(*p) || *p >= 0x80;
    }
    if (!words) {
        title[0] = '\0';
    }

    LOCK();
    if (strcmp(title, c->last_title) != 0) {
        snprintf(c->last_title, sizeof(c->last_title), "%s", title);
        if (c->meta_n == META_KEEP) {
            memmove(&c->meta[0], &c->meta[1], sizeof(meta_t) * (META_KEEP - 1));
            c->meta_n--;
        }
        meta_t *m = &c->meta[c->meta_n++];
        m->pos = c->head;
        m->gen = ++c->meta_gen;
        snprintf(m->title, sizeof(m->title), "%s", title);
    }
    UNLOCK();
}

/* Audio with the metadata taken out, into the ring. */
static bool ts_feed(radio_ctx_t *c, const uint8_t *p, int n, bool *let_go);

static bool feed_audio(radio_ctx_t *c, const uint8_t *p, int n, bool *let_go)
{
    if (c->skip) {
        uint32_t drop = (uint32_t)n < c->skip ? (uint32_t)n : c->skip;
        c->skip -= drop;
        p += drop;
        n -= (int)drop;
    }
    if (c->ts) {
        return ts_feed(c, p, n, let_go);
    }
    while (n > 0) {
        if (!c->metaint) {
            c->got_audio = true;
            return ring_put(c, p, n, let_go);
        }
        if (c->audio_left > 0) {
            int take = (uint32_t)n < c->audio_left ? n : (int)c->audio_left;
            c->got_audio = true;
            if (!ring_put(c, p, take, let_go)) {
                return false;
            }
            c->audio_left -= (uint32_t)take;
            p += take;
            n -= take;
            continue;
        }
        if (c->meta_len < 0) {          /* the length byte */
            c->meta_len = p[0] * 16;
            c->meta_got = 0;
            p++;
            n--;
            if (c->meta_len == 0) {
                c->meta_len = -1;
                c->audio_left = c->metaint;
            }
            continue;
        }
        int take = n < c->meta_len - c->meta_got ? n : c->meta_len - c->meta_got;
        memcpy(c->meta_buf + c->meta_got, p, (size_t)take);
        c->meta_got += take;
        p += take;
        n -= take;
        if (c->meta_got == c->meta_len) {
            c->meta_buf[c->meta_got] = '\0';
            meta_parse(c, c->meta_buf);
            c->meta_len = -1;
            c->audio_left = c->metaint;
        }
    }
    return true;
}

/* Chunked transfer taken apart, for the rare server that sends it anyway. */
static bool feed(radio_ctx_t *c, const uint8_t *p, int n, bool *let_go)
{
    if (!c->chunked) {
        return feed_audio(c, p, n, let_go);
    }
    while (n > 0) {
        if (c->chunk_state == 0 || c->chunk_state == 1) {
            char ch = (char)*p++;
            n--;
            if (ch == '\n') {
                c->chunk_state = c->chunk_left ? 2 : 0;
            } else if (c->chunk_state == 0 && isxdigit((unsigned char)ch)) {
                c->chunk_left = c->chunk_left * 16 +
                                (uint32_t)(isdigit((unsigned char)ch) ? ch - '0'
                                                                      : (tolower((unsigned char)ch) - 'a' + 10));
            } else if (ch != '\r') {
                c->chunk_state = 1;     /* chunk extensions: skip to the end of the line */
            }
        } else if (c->chunk_state == 2) {
            int take = (uint32_t)n < c->chunk_left ? n : (int)c->chunk_left;
            if (!feed_audio(c, p, take, let_go)) {
                return false;
            }
            c->chunk_left -= (uint32_t)take;
            p += take;
            n -= take;
            if (c->chunk_left == 0) {
                c->chunk_state = 3;
            }
        } else {                        /* the CRLF after the data */
            if (*p == '\n') {
                c->chunk_state = 0;
                c->chunk_left = 0;
            }
            p++;
            n--;
        }
    }
    return true;
}

/* ---- one connection -------------------------------------------------------- */

enum {
    R_REDIRECT = 1,         /* 'url' changed: go there at once          */
    R_DROPPED,              /* it played, then stopped: reconnect       */
    R_LET_GO,               /* paused too long: reconnect when drained  */
    R_FAILED,               /* could not: retry with a pause            */
    R_FATAL,                /* never will: give up                      */
    R_STOP,
};

static const char *http_err_text(int rc)
{
    switch (rc) {
    case AOS_HTTP_ERR_DNS:      return "the name did not resolve";
    case AOS_HTTP_ERR_CONNECT:  return "could not connect";
    case AOS_HTTP_ERR_SIN_HORA: return "no clock yet for https";
    case AOS_HTTP_ERR_TLS:      return "TLS failed";
    case AOS_HTTP_ERR_MEM:      return "out of memory";
    default:                    return "network error";
    }
}

/* The value of header 'name' in the block, or NULL. Case ignored, spaces
 * after the colon skipped ("icy-br:128" and "icy-br: 128" both happen). */
static const char *hdr_get(const char *hdrs, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    for (const char *line = hdrs; line && *line;) {
        if (strncasecmp(line, name, nl) == 0 && line[nl] == ':') {
            const char *v = line + nl + 1;
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            size_t n = strcspn(v, "\r\n");
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            return out;
        }
        line = strchr(line, '\n');
        if (line) {
            line++;
        }
    }
    return NULL;
}

static void lower_str(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* The first http(s) address in a .pls or .m3u. */
static bool playlist_first(const char *body, char *out, size_t cap)
{
    for (const char *line = body; line && *line;) {
        while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
            line++;
        }
        const char *v = line;
        if (strncasecmp(v, "File", 4) == 0) {           /* .pls: File1=http://... */
            const char *eq = strchr(v, '=');
            const char *nl = strchr(v, '\n');
            if (eq && (!nl || eq < nl)) {
                v = eq + 1;
            }
        }
        if (strncasecmp(v, "http://", 7) == 0 || strncasecmp(v, "https://", 8) == 0) {
            size_t n = strcspn(v, "\r\n \t");
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            return true;
        }
        line = strchr(line, '\n');
    }
    return false;
}

/* One GET, up to the end of the headers: the connection is left open in
 * *out with the headers in 'hdr' and the first bytes of the body after
 * *body_at. Returns the HTTP status (ICY 200 is 200), R_REDIRECT with 'url'
 * rewritten for a 3xx, R_STOP or R_FAILED. 'icy' asks for the metadata. */
typedef struct {
    aos_http_stream_t *s;
    char *hdr;                      /* HDR_MAX + 1, PSRAM */
    int   hlen, body_at;
    url_t u;
} http_resp_t;

static void resp_close(http_resp_t *r)
{
    aos_http_stream_close(r->s);
    free(r->hdr);
    r->s = NULL;
    r->hdr = NULL;
}

static int http_begin(radio_ctx_t *c, char *url, size_t url_cap, bool icy, http_resp_t *r,
                      uint8_t *buf)
{
    memset(r, 0, sizeof(*r));
    if (!url_parse(url, &r->u)) {
        set_state(c, AOS_RADIO_FAILED, "not an http(s) address");
        return R_FATAL;
    }
    LOCK();
    scopy(c->host, sizeof(c->host), r->u.host);
    c->tls = r->u.tls;
    UNLOCK();

    int rc = aos_http_stream_open(&r->s, r->u.host, r->u.port, r->u.tls);
    if (rc != 0) {
        set_state(c, c->state, http_err_text(rc));
        return R_FAILED;
    }
    char *req = (char *)buf;
    int n = snprintf(req, RECV_CHUNK,
                     "GET %s HTTP/1.0\r\n"
                     "Host: %s\r\n"
                     "User-Agent: AmoledOS/1.0\r\n"
                     "Accept: */*\r\n"
                     "%s"
                     "Connection: close\r\n\r\n",
                     r->u.path, r->u.host, icy ? "Icy-MetaData: 1\r\n" : "");
    if (n <= 0 || n >= RECV_CHUNK || !aos_http_stream_send(r->s, req, n)) {
        resp_close(r);
        set_state(c, c->state, "could not send the request");
        return R_FAILED;
    }
    r->hdr = BIG_ALLOC(HDR_MAX + 1);
    if (!r->hdr) {
        resp_close(r);
        return R_FAILED;
    }
    r->body_at = -1;
    int waited = 0;
    while (r->body_at < 0) {
        if (c->stop) {
            resp_close(r);
            return R_STOP;
        }
        int got = aos_http_stream_recv(r->s, r->hdr + r->hlen, HDR_MAX - r->hlen);
        if (got == -2) {
            if (++waited >= 10) {
                break;
            }
            continue;
        }
        if (got <= 0) {
            break;
        }
        r->hlen += got;
        r->hdr[r->hlen] = '\0';
        char *e = strstr(r->hdr, "\r\n\r\n");
        int skip = 4;
        if (!e) {
            e = strstr(r->hdr, "\n\n");
            skip = 2;
        }
        if (e) {
            r->body_at = (int)(e - r->hdr) + skip;
        } else if (r->hlen >= HDR_MAX) {
            break;
        }
    }
    if (r->body_at < 0) {
        resp_close(r);
        set_state(c, c->state, "no answer from the server");
        return R_FAILED;
    }
    int status = 0;
    if (strncmp(r->hdr, "HTTP/", 5) == 0) {
        const char *sp = strchr(r->hdr, ' ');
        status = sp ? atoi(sp + 1) : 0;
    } else if (strncmp(r->hdr, "ICY", 3) == 0) {
        status = atoi(r->hdr + 4);
    }
    if (status >= 300 && status < 400) {
        /* on the heap: the reader's stack is spent on TLS (see aos_radio_start) */
        char *loc = BIG_ALLOC(256 + 512);
        if (loc && hdr_get(r->hdr, "Location", loc, 256)) {
            url_resolve(loc + 256, 512, &r->u, loc);
            scopy(url, url_cap, loc + 256);
            free(loc);
            resp_close(r);
            return R_REDIRECT;
        }
        free(loc);
        resp_close(r);
        set_state(c, c->state, "a redirect with nowhere to go");
        return R_FAILED;
    }
    if (status != 200) {
        char e[64];
        snprintf(e, sizeof(e), status ? "the server said %d" : "not an HTTP answer", status);
        resp_close(r);
        /* 404, 403, 410: the station is not there, and will not be in ten
         * seconds either. 5xx ("server full") is worth another try. */
        bool gone = status >= 400 && status < 500;
        set_state(c, gone ? AOS_RADIO_FAILED : c->state, e);
        return gone ? R_FATAL : R_FAILED;
    }
    char v[64];
    c->chunked = false;
    if (hdr_get(r->hdr, "Transfer-Encoding", v, sizeof(v))) {
        lower_str(v);
        c->chunked = strstr(v, "chunked") != NULL;
    }
    c->chunk_state = 0;
    c->chunk_left = 0;
    return 200;
}

/* Undoes a chunked body in place. */
static int dechunk(char *body, int len)
{
    int r = 0, w = 0;
    while (r < len) {
        int size = 0, digits = 0;
        while (r < len && isxdigit((unsigned char)body[r])) {
            char ch = body[r++];
            size = size * 16 + (isdigit((unsigned char)ch) ? ch - '0' : tolower((unsigned char)ch) - 'a' + 10);
            digits++;
        }
        if (!digits) {
            break;
        }
        while (r < len && body[r] != '\n') r++;
        r++;
        if (size == 0 || r + size > len) {
            break;
        }
        memmove(body + w, body + r, (size_t)size);
        w += size;
        r += size + 2;
    }
    body[w] = '\0';
    return w;
}

/* A whole body (a playlist) into 'out', redirects followed. Returns its
 * length, or an R_ code (negative-free: R_* are all > 0, so -R_*). */
static int http_fetch_body(radio_ctx_t *c, char *url, size_t url_cap, char *out, int max,
                           uint8_t *buf);

static int http_fetch(radio_ctx_t *c, char *url, size_t url_cap, char *out, int max, uint8_t *buf)
{
    c->fetching = true;
    int r = http_fetch_body(c, url, url_cap, out, max, buf);
    c->fetching = false;
    return r;
}

static int http_fetch_body(radio_ctx_t *c, char *url, size_t url_cap, char *out, int max,
                           uint8_t *buf)
{
    for (int hops = 0; hops <= MAX_REDIRECTS; hops++) {
        http_resp_t r;
        int st = http_begin(c, url, url_cap, false, &r, buf);
        if (st == R_REDIRECT) {
            continue;
        }
        if (st != 200) {
            return -st;
        }
        int len = r.hlen - r.body_at;
        if (len > max) {
            len = max;
        }
        memcpy(out, r.hdr + r.body_at, (size_t)len);
        for (int tries = 0; len < max && tries < 8 && !c->stop;) {
            int got = aos_http_stream_recv(r.s, out + len, max - len);
            if (got == -2) {
                tries++;
                continue;
            }
            if (got <= 0) {
                break;
            }
            len += got;
        }
        out[len] = '\0';
        if (c->chunked) {
            len = dechunk(out, len);
        }
        resp_close(&r);
        return c->stop ? -R_STOP : len;
    }
    set_state(c, AOS_RADIO_FAILED, "too many redirects");
    return -R_FATAL;
}

/* Streams an open response's body through feed() until it ends. */
static int pump(radio_ctx_t *c, http_resp_t *r, uint8_t *buf, bool live)
{
    bool let_go = false;
    bool ok = feed(c, (const uint8_t *)r->hdr + r->body_at, r->hlen - r->body_at, &let_go);
    int idle = 0;
    int ret = live ? R_DROPPED : 0;
    while (ok && !c->stop) {
        int got = aos_http_stream_recv(r->s, buf, RECV_CHUNK);
        if (got == -2) {
            if (++idle >= IDLE_DROP_S) {
                set_state(c, c->state, "the station went quiet");
                ret = R_DROPPED;
                break;
            }
            continue;
        }
        if (got <= 0) {
            if (live) {
                set_state(c, c->state, got == 0 ? "the station closed the connection"
                                                : "the connection broke");
            } else if (got < 0) {
                ret = R_DROPPED;
            }
            break;
        }
        idle = 0;
        ok = feed(c, buf, got, &let_go);
    }
    if (c->stop) {
        return R_STOP;
    }
    return let_go ? R_LET_GO : ret;
}

/* ---- HLS (branch aac) -------------------------------------------------------
 *
 * A station that answers with an .m3u8 does not stream: it lists segments
 * of a few seconds each, and a player fetches them one after the other and
 * reloads the list for the next ones. What the reader does:
 *
 *  - A master playlist (#EXT-X-STREAM-INF) lists the same station at
 *    several rates: the audio-only variant (no avc1/hvc1 in CODECS) with
 *    the highest BANDWIDTH up to 160 kbit/s, else the lowest there is.
 *  - A live media playlist is joined three segments from its end, as
 *    players do, and reloaded when its segments run out, every half target
 *    duration while nothing is new.
 *  - A segment is MPEG-TS (0x47 every 188 bytes: PAT -> PMT -> the audio
 *    PID's PES payload, which is ADTS AAC or MP3), or packed audio (an ID3
 *    tag, skipped, then ADTS or MP3 as it is). Either way what reaches the
 *    ring is bytes the decoder already knows.
 *  - Refused with a reason: encrypted segments (#EXT-X-KEY), fMP4 segments
 *    (#EXT-X-MAP), LATM audio in the TS.
 */

#define HLS_MAX_SEGS    64

/* ---- MPEG-TS: the audio out of 188-byte packets ---- */

static bool ts_packet(radio_ctx_t *c, const uint8_t *p, bool *let_go)
{
    bool pusi = (p[1] & 0x40) != 0;
    int pid = ((p[1] & 0x1F) << 8) | p[2];
    int afc = (p[3] >> 4) & 3;
    int off = 4;
    if (afc & 2) {
        off += 1 + p[4];
    }
    if (!(afc & 1) || off >= 188) {
        return true;
    }
    if (pid == 0 || (pid == c->pmt_pid && c->pmt_pid > 0)) {
        if (pusi) {
            off += 1 + p[off];          /* pointer field */
        }
        if (off + 12 > 188) {
            return true;
        }
        const uint8_t *t = p + off;
        int sec_len = ((t[1] & 0x0F) << 8) | t[2];
        int end = off + 3 + sec_len - 4;        /* the CRC is not wanted */
        if (end > 188) {
            end = 188;
        }
        if (pid == 0 && t[0] == 0x00) {
            for (int i = off + 8; i + 4 <= end; i += 4) {
                int prog = (p[i] << 8) | p[i + 1];
                if (prog != 0) {
                    c->pmt_pid = ((p[i + 2] & 0x1F) << 8) | p[i + 3];
                    break;
                }
            }
        } else if (t[0] == 0x02 && off + 12 <= 188) {
            int pil = ((t[10] & 0x0F) << 8) | t[11];
            for (int i = off + 12 + pil; i + 5 <= end;) {
                int type = p[i];
                int es = ((p[i + 1] & 0x1F) << 8) | p[i + 2];
                int eil = ((p[i + 3] & 0x0F) << 8) | p[i + 4];
                if (type == 0x0F || type == 0x03 || type == 0x04 || type == 0x11) {
                    c->audio_pid = es;
                    c->audio_type = type;
                    break;
                }
                i += 5 + eil;
            }
        }
        return true;
    }
    if (pid != c->audio_pid || c->audio_pid <= 0) {
        return true;
    }
    if (c->audio_type == 0x11) {
        return true;                    /* LATM: hls_run() refuses it */
    }
    if (pusi) {
        if (off + 9 > 188 || p[off] != 0 || p[off + 1] != 0 || p[off + 2] != 1) {
            return true;
        }
        off += 9 + p[off + 8];          /* the PES header */
        if (off >= 188) {
            return true;
        }
    }
    c->got_audio = true;
    return ring_put(c, p + off, 188 - off, let_go);
}

static bool ts_feed(radio_ctx_t *c, const uint8_t *p, int n, bool *let_go)
{
    while (n > 0) {
        if (c->tsfill == 0 && *p != 0x47) {
            p++;                        /* lost sync: to the next 0x47 */
            n--;
            continue;
        }
        int take = 188 - c->tsfill < n ? 188 - c->tsfill : n;
        memcpy(c->tsbuf + c->tsfill, p, (size_t)take);
        c->tsfill += take;
        p += take;
        n -= take;
        if (c->tsfill == 188) {
            c->tsfill = 0;
            if (!ts_packet(c, c->tsbuf, let_go)) {
                return false;
            }
        }
    }
    return true;
}

/* ---- the playlists ---- */

/* The value of NAME= in an attribute list ("BANDWIDTH=64000,CODECS=..."). */
static bool attr(const char *line, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    for (const char *p = line; (p = strstr(p, name)) != NULL; p += nl) {
        if ((p == line || p[-1] == ',' || p[-1] == ':') && p[nl] == '=') {
            const char *v = p + nl + 1;
            size_t n;
            if (*v == '"') {
                v++;
                n = strcspn(v, "\"");
            } else {
                n = strcspn(v, ",\r\n");
            }
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, v, n);
            out[n] = '\0';
            return true;
        }
    }
    return false;
}

/* From a master playlist, the variant to play, into 'url' (resolved). */
static bool hls_variant(char *body, char *url, size_t url_cap, uint16_t *kbps)
{
    url_t base;
    if (!url_parse(url, &base)) {
        return false;
    }
    long best_bw = -1, low_bw = -1;
    char *mem = BIG_ALLOC(400 * 3 + 512);   /* off the reader's stack */
    if (!mem) {
        return false;
    }
    char *best = mem, *low = mem + 400, *one = mem + 800, *next = mem + 1200;
    for (char *line = strstr(body, "#EXT-X-STREAM-INF:"); line;
         line = strstr(line + 1, "#EXT-X-STREAM-INF:")) {
        char v[96] = "";
        long bw = attr(line, "BANDWIDTH", v, sizeof(v)) ? atol(v) : 0;
        bool video = false;
        if (attr(line, "CODECS", v, sizeof(v))) {
            video = strstr(v, "avc1") || strstr(v, "hvc1") || strstr(v, "hev1");
        }
        const char *uri = strchr(line, '\n');
        while (uri && (*uri == '\n' || *uri == '\r' || *uri == '#')) {
            if (*uri == '#') {
                uri = strchr(uri, '\n');
                continue;
            }
            uri++;
        }
        if (!uri || !*uri) {
            continue;
        }
        size_t n = strcspn(uri, "\r\n");
        if (n >= 400) {
            continue;
        }
        memcpy(one, uri, n);
        one[n] = '\0';
        if (low_bw < 0 || bw < low_bw) {
            low_bw = bw;
            scopy(low, 400, one);
        }
        if (!video && bw <= 160000 && bw > best_bw) {
            best_bw = bw;
            scopy(best, 400, one);
        }
    }
    const char *pick = best[0] ? best : low;
    bool ok = pick[0] != '\0';
    if (ok) {
        long bw = best[0] ? best_bw : low_bw;
        if (bw > 0) {
            *kbps = (uint16_t)(bw / 1000);
        }
        url_resolve(next, 512, &base, pick);
        scopy(url, url_cap, next);
    }
    free(mem);
    return ok;
}

typedef struct {
    uint32_t id;                    /* FNV-1a of its URI up to the '?'      */
    int      at;                    /* offset of its URI in the body */
} hls_seg_t;

/* A segment is known by its name, not by EXT-X-MEDIA-SEQUENCE: RMC's server
 * (ads stitched in on the server) sends sequence 1 on every reload while
 * the segments move on, and a reader that trusted the number waited for
 * ever. The query string changes per session on some CDNs and is left out. */
static uint32_t seg_id(const char *uri)
{
    uint32_t h = 2166136261u;
    for (; *uri && *uri != '?' && *uri != '\r' && *uri != '\n'; uri++) {
        h = (h ^ (uint8_t)*uri) * 16777619u;
    }
    return h;
}

#define HLS_SEEN 32

/* A media playlist's segments; returns how many, -1 for what is refused. */
static int hls_parse(radio_ctx_t *c, char *body, hls_seg_t *segs, int *target, bool *ended)
{
    if (strstr(body, "#EXT-X-KEY") && !strstr(body, "METHOD=NONE")) {
        set_state(c, AOS_RADIO_FAILED, "HLS: encrypted segments");
        return -1;
    }
    if (strstr(body, "#EXT-X-MAP")) {
        set_state(c, AOS_RADIO_FAILED, "HLS: fMP4 segments are not supported");
        return -1;
    }
    const char *t = strstr(body, "#EXT-X-TARGETDURATION:");
    *target = t ? atoi(t + 22) : 6;
    if (*target <= 0 || *target > 60) {
        *target = 6;
    }
    *ended = strstr(body, "#EXT-X-ENDLIST") != NULL;
    int n = 0;
    for (char *line = body; line && *line && n < HLS_MAX_SEGS;) {
        char *nl = strchr(line, '\n');
        if (strncmp(line, "#EXTINF", 7) == 0) {
            char *uri = nl ? nl + 1 : NULL;
            while (uri && (*uri == '#' || *uri == '\r' || *uri == '\n')) {
                char *x = strchr(uri, '\n');
                uri = x ? x + 1 : NULL;
            }
            if (uri && *uri) {
                segs[n].id = seg_id(uri);
                segs[n].at = (int)(uri - body);
                n++;
                nl = strchr(uri, '\n');
            }
        }
        line = nl ? nl + 1 : NULL;
    }
    return n;
}

/* One segment: fetched, its container found from its first bytes, poured. */
static int hls_segment_body(radio_ctx_t *c, char *url, size_t url_cap, uint8_t *buf);

/* While a segment comes, the HAL keeps the WiFi out of power save
 * (aos_radio_busy()): on the watch, with modem sleep, a 6 s segment over a
 * fresh TLS connection took up to 5.7 s to arrive (2026-09-26). */
static int hls_segment(radio_ctx_t *c, char *url, size_t url_cap, uint8_t *buf)
{
    c->fetching = true;
    int r = hls_segment_body(c, url, url_cap, buf);
    c->fetching = false;
    return r;
}

static int hls_segment_body(radio_ctx_t *c, char *url, size_t url_cap, uint8_t *buf)
{
    http_resp_t r;
    int st = R_REDIRECT;
    for (int hops = 0; st == R_REDIRECT && hops <= MAX_REDIRECTS; hops++) {
        st = http_begin(c, url, url_cap, false, &r, buf);
    }
    if (st != 200) {
        return st == R_REDIRECT ? R_FAILED : st;
    }
    /* enough of the body to see what it is */
    for (int tries = 0; r.hlen - r.body_at < 10 && tries < 5 && !c->stop;) {
        int got = aos_http_stream_recv(r.s, r.hdr + r.hlen, HDR_MAX - r.hlen);
        if (got == -2) {
            tries++;
            continue;
        }
        if (got <= 0) {
            break;
        }
        r.hlen += got;
    }
    const uint8_t *b = (const uint8_t *)r.hdr + r.body_at;
    int have = r.hlen - r.body_at;
    c->ts = have > 0 && b[0] == 0x47;
    c->tsfill = 0;
    c->skip = 0;
    if (!c->ts && have >= 10 && b[0] == 'I' && b[1] == 'D' && b[2] == '3') {
        c->skip = 10 + ((uint32_t)(b[6] & 0x7F) << 21 | (uint32_t)(b[7] & 0x7F) << 14 |
                        (uint32_t)(b[8] & 0x7F) << 7 | (b[9] & 0x7F));
    }
    int ret = pump(c, &r, buf, false);
    resp_close(&r);
    c->ts = false;
    return ret;
}

static int hls_run(radio_ctx_t *c, char *url, size_t url_cap, char *body, uint8_t *buf)
{
    LOCK();
    c->hls = true;
    scopy(c->content_type, sizeof(c->content_type), "hls");
    UNLOCK();
    c->metaint = 0;
    c->pmt_pid = c->audio_pid = c->audio_type = -1;

    if (strstr(body, "#EXT-X-STREAM-INF")) {
        uint16_t kbps = 0;
        if (!hls_variant(body, url, url_cap, &kbps)) {
            set_state(c, AOS_RADIO_FAILED, "HLS: no variant to play");
            return R_FATAL;
        }
        LOCK();
        c->kbps = kbps;
        UNLOCK();
        int len = http_fetch(c, url, url_cap, body, PLAYLIST_MAX, buf);
        if (len < 0) {
            return -len;
        }
    }

    hls_seg_t *segs = BIG_ALLOC(sizeof(hls_seg_t) * HLS_MAX_SEGS);
    char *seg_url = BIG_ALLOC(512);
    char *pl_url = BIG_ALLOC(512);
    char *one = BIG_ALLOC(400);
    if (!segs || !seg_url || !pl_url || !one) {
        free(segs);
        free(seg_url);
        free(pl_url);
        free(one);
        return R_FAILED;
    }
    scopy(pl_url, 512, url);
    set_state(c, c->ready ? AOS_RADIO_PLAYING : AOS_RADIO_BUFFERING, "");
    bool first = true;
    uint32_t seen[HLS_SEEN] = {0};
    int seen_at = 0;
    int fails = 0, ret = R_DROPPED;
    while (!c->stop) {
        int target = 6;
        bool ended = false;
        int n = hls_parse(c, body, segs, &target, &ended);
        if (n < 0) {
            ret = R_FATAL;
            break;
        }
        url_t base;
        url_parse(pl_url, &base);
        int from = 0;
        if (first) {
            from = n > 3 && !ended ? n - 3 : 0;
            first = false;
        }
        int fetched = 0;
        for (int i = from; i < n && !c->stop; i++) {
            bool known = false;
            for (int k = 0; k < HLS_SEEN && !known; k++) {
                known = seen[k] == segs[i].id && segs[i].id != 0;
            }
            if (known) {
                continue;
            }
            seen[seen_at] = segs[i].id;
            seen_at = (seen_at + 1) % HLS_SEEN;
            size_t len = strcspn(body + segs[i].at, "\r\n");
            if (len >= 400) {
                continue;
            }
            memcpy(one, body + segs[i].at, len);
            one[len] = '\0';
            url_resolve(seg_url, 512, &base, one);
            DBG("segment %d/%d get (ring %u B)", i + 1, n, (unsigned)(c->head - c->tail));
            int64_t t0 = (int64_t)aos_hal_uptime_ms();
            uint32_t head0 = c->head;
            int r = hls_segment(c, seg_url, 512, buf);
            if (r == R_FAILED && c->head == head0 && !c->stop) {
                /* nothing of it arrived: once more before it is lost */
                WARN("hls: segment failed (%s), again", c->error);
                r = hls_segment(c, seg_url, 512, buf);
            }
            int ms = (int)((int64_t)aos_hal_uptime_ms() - t0);
            if (r != 0 || ms > 4000) {
                WARN("hls: segment r=%d in %d ms, %u B buffered", r, ms,
                     (unsigned)(c->head - c->tail));
            }
            DBG("segment %d/%d done r=%d (ring %u B)", i + 1, n, r, (unsigned)(c->head - c->tail));
            if (r == R_STOP || r == R_LET_GO || r == R_FATAL) {
                ret = r;
                goto out;
            }
            if (r == 0) {
                fails = 0;
                fetched++;
            } else if (++fails >= 3) {
                goto out;               /* R_DROPPED: start the station again */
            }
            if (c->audio_type == 0x11) {
                set_state(c, AOS_RADIO_FAILED, "HLS: LATM audio is not supported");
                ret = R_FATAL;
                goto out;
            }
        }
        if (ended && !fetched) {
            set_state(c, AOS_RADIO_FAILED, "the playlist ended");
            ret = R_FATAL;
            break;
        }
        DBG("playlist: %d segments, %d new, target %d s", n, fetched, target);
        if (!fetched) {
            int wait = target * 500;
            for (int t = 0; t < wait && !c->stop; t += 100) {
                SLEEP_MS(100);
            }
        }
        scopy(url, url_cap, pl_url);
        int len = http_fetch(c, url, url_cap, body, PLAYLIST_MAX, buf);
        if (len < 0) {
            WARN("hls: the playlist did not reload (%s)", c->error);
            ret = -len == R_STOP ? R_STOP : R_DROPPED;
            break;
        }
    }
    if (c->stop) {
        ret = R_STOP;
    }
out:
    free(segs);
    free(seg_url);
    free(pl_url);
    free(one);
    return ret;
}

/* ---- a station, from the top ---- */

static int session(radio_ctx_t *c, char *url, size_t url_cap, uint8_t *buf)
{
    http_resp_t r;
    int st = http_begin(c, url, url_cap, true, &r, buf);
    if (st != 200) {
        return st;
    }
    char ctype[64] = "";
    hdr_get(r.hdr, "Content-Type", ctype, sizeof(ctype));
    lower_str(ctype);
    bool playlist = strstr(ctype, "mpegurl") || strstr(ctype, "scpls") || strstr(ctype, "x-pls") ||
                    ends_with(r.u.path, ".pls") || ends_with(r.u.path, ".m3u") ||
                    ends_with(r.u.path, ".m3u8");
    if (playlist && !strstr(ctype, "audio/mpeg") && !strstr(ctype, "aac")) {
        char *body = BIG_ALLOC(PLAYLIST_MAX + 1);
        int blen = 0;
        if (body) {
            blen = r.hlen - r.body_at;
            if (blen > PLAYLIST_MAX) {
                blen = PLAYLIST_MAX;
            }
            memcpy(body, r.hdr + r.body_at, (size_t)blen);
            for (int tries = 0; blen < PLAYLIST_MAX && tries < 5 && !c->stop;) {
                int got = aos_http_stream_recv(r.s, body + blen, PLAYLIST_MAX - blen);
                if (got == -2) {
                    tries++;
                    continue;
                }
                if (got <= 0) {
                    break;
                }
                blen += got;
            }
            body[blen] = '\0';
            if (c->chunked) {
                dechunk(body, blen);
            }
        }
        resp_close(&r);
        if (!body) {
            return R_FAILED;
        }
        int ret;
        if (strstr(body, "#EXT-X-")) {
            ret = hls_run(c, url, url_cap, body, buf);
        } else {
            char next[512];
            if (playlist_first(body, next, sizeof(next))) {
                scopy(url, url_cap, next);
                ret = R_REDIRECT;
            } else {
                set_state(c, AOS_RADIO_FAILED, "an empty playlist");
                ret = R_FATAL;
            }
        }
        free(body);
        return ret;
    }
    /* What the decoder cannot take. AAC (ADTS) and MP3 go on: the decoder
     * finds out which from the bytes. */
    if (strstr(ctype, "ogg") || strstr(ctype, "opus") || strstr(ctype, "flac") ||
        strstr(ctype, "wav") || strstr(ctype, "audio/mp4") || strstr(ctype, "video/")) {
        char e[64];
        snprintf(e, sizeof(e), "%.30s: only MP3 and AAC play", ctype);
        resp_close(&r);
        set_state(c, AOS_RADIO_FAILED, e);
        return R_FATAL;
    }

    /* The audio. What the station says of itself, then the bytes. */
    char v[160];
    LOCK();
    c->hls = false;
    scopy(c->content_type, sizeof(c->content_type), ctype);
    if (hdr_get(r.hdr, "icy-name", v, sizeof(v))) {
        to_utf8(c->icy_name, sizeof(c->icy_name), v);
    }
    if (hdr_get(r.hdr, "icy-genre", v, sizeof(v))) {
        to_utf8(c->icy_genre, sizeof(c->icy_genre), v);
    }
    if (hdr_get(r.hdr, "icy-url", v, sizeof(v))) {
        scopy(c->icy_url, sizeof(c->icy_url), v);
    }
    if (hdr_get(r.hdr, "icy-description", v, sizeof(v))) {
        to_utf8(c->icy_desc, sizeof(c->icy_desc), v);
    }
    if (hdr_get(r.hdr, "icy-br", v, sizeof(v))) {
        c->kbps = (uint16_t)atoi(v);    /* "128,128" happens: atoi stops at the comma */
    }
    UNLOCK();
    c->metaint = hdr_get(r.hdr, "icy-metaint", v, sizeof(v)) ? (uint32_t)atoi(v) : 0;
    c->audio_left = c->metaint;
    c->meta_len = -1;
    set_state(c, c->ready ? AOS_RADIO_PLAYING : AOS_RADIO_BUFFERING, "");
    int ret = pump(c, &r, buf, true);
    resp_close(&r);
    return ret;
}

/* ---- the reader ------------------------------------------------------------ */

static void reader(radio_ctx_t *c)
{
    char *url = BIG_ALLOC(512);
    uint8_t *buf = BIG_ALLOC(RECV_CHUNK);
    int redirects = 0, failures = 0;
    if (url && buf) {
        snprintf(url, 512, "%s", c->url);
    } else {
        set_state(c, AOS_RADIO_FAILED, "out of memory");
        c->failed = true;
    }

    while (url && buf && !c->stop) {
        uint32_t audio_before = c->head;
        int r = session(c, url, 512, buf);
        if (r == R_STOP || c->stop) {
            break;
        }
        if (r == R_REDIRECT) {
            if (++redirects > MAX_REDIRECTS) {
                set_state(c, AOS_RADIO_FAILED, "too many redirects");
                c->failed = true;
                break;
            }
            continue;
        }
        if (r == R_FATAL) {
            c->failed = true;
            break;
        }
        /* Every other ending starts again from the station's own URL: a
         * redirect's token expires. */
        snprintf(url, 512, "%s", c->url);
        redirects = 0;
        if (r == R_LET_GO) {
            set_state(c, AOS_RADIO_PLAYING, "paused: let go of the connection");
            while (!c->stop && (c->head - c->tail) > RING_BYTES / 2) {
                SLEEP_MS(100);
            }
            continue;
        }
        bool played = c->head != audio_before;
        failures = played ? 1 : failures + 1;
        if (failures >= GIVE_UP_AFTER) {
            LOCK();
            c->state = AOS_RADIO_FAILED;
            UNLOCK();
            c->failed = true;
            break;
        }
        LOCK();
        c->state = AOS_RADIO_RETRYING;
        c->reconnects++;
        UNLOCK();
        /* 1, 2, 4, 8, then 15 s between tries */
        int wait_ms = failures >= 5 ? 15000 : 1000 << (failures - 1);
        for (int t = 0; t < wait_ms && !c->stop; t += 100) {
            SLEEP_MS(100);
        }
    }
    free(url);
    free(buf);
    ctx_release(c);
}

#ifdef AOS_SIM
static void *reader_thread(void *arg)
{
    reader((radio_ctx_t *)arg);
    return NULL;
}
#else
static void reader_task(void *arg)
{
    reader((radio_ctx_t *)arg);
    vTaskDelete(NULL);
}
#endif

/* ---- the face ---------------------------------------------------------------- */

static void snapshot(radio_ctx_t *c, aos_radio_status_t *o)
{
    o->state = c->state;
    snprintf(o->host, sizeof(o->host), "%s", c->host);
    o->tls = c->tls;
    snprintf(o->icy_name, sizeof(o->icy_name), "%s", c->icy_name);
    snprintf(o->icy_genre, sizeof(o->icy_genre), "%s", c->icy_genre);
    snprintf(o->icy_url, sizeof(o->icy_url), "%s", c->icy_url);
    snprintf(o->icy_desc, sizeof(o->icy_desc), "%s", c->icy_desc);
    snprintf(o->content_type, sizeof(o->content_type), "%s", c->content_type);
    snprintf(o->error, sizeof(o->error), "%s", c->error);
    o->kbps = c->kbps;
    o->hls = c->hls;
    o->reconnects = c->reconnects;
    o->bytes = c->head;
    uint32_t kbps = c->kbps ? c->kbps : 128;
    o->buffer_ms = (uint32_t)((uint64_t)(c->head - c->tail) * 8 / kbps);
}

bool aos_radio_start(const char *url)
{
    lock_init();
    aos_http_stream_init();
    aos_radio_stop();
    url_t u;
    if (!url || !url_parse(url, &u)) {
        LOCK();
        memset(&s_last, 0, sizeof(s_last));
        s_last.state = AOS_RADIO_FAILED;
        snprintf(s_last.error, sizeof(s_last.error), "not an http(s) address");
        UNLOCK();
        return false;
    }
    radio_ctx_t *c = BIG_ALLOC(sizeof(*c));
    uint8_t *ring = BIG_ALLOC(RING_BYTES);
    if (!c || !ring) {
        free(c);
        free(ring);
        return false;
    }
    c->ring = ring;
    c->refs = 2;
    c->state = AOS_RADIO_CONNECTING;
    snprintf(c->url, sizeof(c->url), "%s", url);

#ifdef AOS_SIM
    pthread_t t;
    if (pthread_create(&t, NULL, reader_thread, c) != 0) {
        free(ring);
        free(c);
        return false;
    }
    pthread_detach(t);
#else
    /* Internal stack: TLS and lwIP. Measured 4.7 KB at the deepest on a
     * StreamTheWorld station (a redirect, two handshakes), and 6.7 KB on
     * ipanel.instream.audio and on an HLS master playlist over https
     * (v0.8.1): 360 bytes were left of 7 KB. What was big on it moved to
     * the heap, and it has 9 KB. Priority 3, under LVGL: the ring gives it
     * seconds of slack, and it never does floats, so it stays unpinned. */
    TaskHandle_t th = NULL;
    if (xTaskCreate(reader_task, "aos_radio", 9216, c, 3, &th) != pdPASS) {
        free(ring);
        free(c);
        return false;
    }
    c->task = th;
#endif
    LOCK();
    s_cur = c;
    UNLOCK();
    return true;
}

void aos_radio_stop(void)
{
    lock_init();
    LOCK();
    radio_ctx_t *c = s_cur;
    s_cur = NULL;
    if (c) {
        memset(&s_last, 0, sizeof(s_last));
        snapshot(c, &s_last);
        if (!c->failed) {
            s_last.state = AOS_RADIO_OFF;
        }
        c->stop = true;
    }
    UNLOCK();
    if (c) {
        ctx_release(c);
    }
}

int aos_radio_ready(void)
{
    lock_init();
    LOCK();
    radio_ctx_t *c = s_cur;
    int r = 0;
    if (!c) {
        r = -1;
    } else if (c->ready) {
        r = 1;
    } else {
        /* A second and a half of audio at the stated rate, 16 KB at least:
         * enough to find the first frame and to ride out the first hiccup.
         * Icecast sends a burst on connecting, so it is usually there at once. */
        uint32_t kbps = c->kbps ? c->kbps : 128;
        uint32_t want = kbps * 1000 / 8 * 3 / 2;
        if (want < 16 * 1024) {
            want = 16 * 1024;
        }
        if (c->head - c->tail >= want) {
            c->ready = true;
            c->state = AOS_RADIO_PLAYING;
            r = 1;
        } else if (c->failed) {
            r = -1;
        }
    }
    UNLOCK();
    return r;
}

int aos_radio_read(void *ctx, void *buf, int max)
{
    (void)ctx;
    LOCK();
    radio_ctx_t *c = s_cur;
    if (!c) {
        UNLOCK();
        return -1;
    }
    uint32_t avail = c->head - c->tail;
    if (avail == 0) {
        bool gone = c->failed;
        UNLOCK();
        return gone ? -1 : 0;
    }
    uint32_t n = (uint32_t)max < avail ? (uint32_t)max : avail;
    for (uint32_t done = 0; done < n;) {
        uint32_t at = (c->tail + done) % RING_BYTES;
        uint32_t run = RING_BYTES - at;
        if (run > n - done) {
            run = n - done;
        }
        memcpy((uint8_t *)buf + done, c->ring + at, run);
        done += run;
    }
    c->tail += n;
    UNLOCK();
    return (int)n;
}

uint32_t aos_radio_title_at_read(char *title, size_t len)
{
    uint32_t gen = 0;
    if (title && len) {
        title[0] = '\0';
    }
    LOCK();
    radio_ctx_t *c = s_cur;
    if (c) {
        for (int i = c->meta_n - 1; i >= 0; i--) {
            if ((int32_t)(c->tail - c->meta[i].pos) >= 0) {
                gen = c->meta[i].gen;
                if (title && len) {
                    snprintf(title, len, "%s", c->meta[i].title);
                }
                break;
            }
        }
    }
    UNLOCK();
    return gen;
}

void aos_radio_fill_status(aos_radio_status_t *out)
{
    lock_init();
    LOCK();
    if (s_cur) {
        snapshot(s_cur, out);
    } else {
        aos_radio_state_t st = s_last.state;
        snprintf(out->host, sizeof(out->host), "%s", s_last.host);
        out->tls = s_last.tls;
        snprintf(out->icy_name, sizeof(out->icy_name), "%s", s_last.icy_name);
        snprintf(out->icy_genre, sizeof(out->icy_genre), "%s", s_last.icy_genre);
        snprintf(out->icy_url, sizeof(out->icy_url), "%s", s_last.icy_url);
        snprintf(out->icy_desc, sizeof(out->icy_desc), "%s", s_last.icy_desc);
        snprintf(out->content_type, sizeof(out->content_type), "%s", s_last.content_type);
        snprintf(out->error, sizeof(out->error), "%s", s_last.error);
        out->kbps = s_last.kbps;
        out->hls = s_last.hls;
        out->reconnects = s_last.reconnects;
        out->bytes = s_last.bytes;
        out->state = st;
    }
    UNLOCK();
}

uint32_t aos_radio_buffered(void)
{
    lock_init();
    LOCK();
    uint32_t n = s_cur ? s_cur->head - s_cur->tail : 0;
    UNLOCK();
    return n;
}

uint32_t aos_radio_stack_free(void)
{
    uint32_t n = 0;
#ifndef AOS_SIM
    lock_init();
    LOCK();
    if (s_cur && s_cur->task && !s_cur->stop) {
        n = (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)s_cur->task);
    }
    UNLOCK();
#endif
    return n;
}

void aos_radio_fail(const char *why)
{
    lock_init();
    LOCK();
    if (s_cur) {
        s_cur->failed = true;
        s_cur->state = AOS_RADIO_FAILED;
        scopy(s_cur->error, sizeof(s_cur->error), why);
    }
    UNLOCK();
}

bool aos_radio_busy(void)
{
    lock_init();
    LOCK();
    radio_ctx_t *c = s_cur;
    bool busy = c && (c->fetching || c->state == AOS_RADIO_CONNECTING ||
                      c->state == AOS_RADIO_RETRYING || c->state == AOS_RADIO_BUFFERING);
    UNLOCK();
    return busy;
}
