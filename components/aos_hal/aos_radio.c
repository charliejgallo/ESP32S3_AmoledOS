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
static bool feed_audio(radio_ctx_t *c, const uint8_t *p, int n, bool *let_go)
{
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

/* The first http(s) address in a .pls or .m3u. True if it found one; with
 * *hls when it is an HLS playlist instead, which this does not play. */
static bool playlist_first(const char *body, char *out, size_t cap, bool *hls)
{
    *hls = strstr(body, "#EXT-X-") != NULL;
    if (*hls) {
        return false;
    }
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

static int session(radio_ctx_t *c, char *url, size_t url_cap, uint8_t *buf)
{
    url_t u;
    if (!url_parse(url, &u)) {
        set_state(c, AOS_RADIO_FAILED, "not an http(s) address");
        return R_FATAL;
    }
    LOCK();
    snprintf(c->host, sizeof(c->host), "%s", u.host);
    c->tls = u.tls;
    UNLOCK();

    aos_http_stream_t *s = NULL;
    int rc = aos_http_stream_open(&s, u.host, u.port, u.tls);
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
                     "Icy-MetaData: 1\r\n"
                     "Connection: close\r\n\r\n",
                     u.path, u.host);
    if (n <= 0 || n >= RECV_CHUNK || !aos_http_stream_send(s, req, n)) {
        aos_http_stream_close(s);
        set_state(c, c->state, "could not send the request");
        return R_FAILED;
    }

    /* The headers, up to the blank line. */
    char *hdr = BIG_ALLOC(HDR_MAX + 1);
    if (!hdr) {
        aos_http_stream_close(s);
        return R_FAILED;
    }
    int hlen = 0, body_at = -1, waited = 0;
    while (body_at < 0) {
        if (c->stop) {
            free(hdr);
            aos_http_stream_close(s);
            return R_STOP;
        }
        int r = aos_http_stream_recv(s, hdr + hlen, HDR_MAX - hlen);
        if (r == -2) {
            if (++waited >= 10) {
                break;
            }
            continue;
        }
        if (r <= 0) {
            break;
        }
        hlen += r;
        hdr[hlen] = '\0';
        char *e = strstr(hdr, "\r\n\r\n");
        int skip = 4;
        if (!e) {
            e = strstr(hdr, "\n\n");
            skip = 2;
        }
        if (e) {
            body_at = (int)(e - hdr) + skip;
        } else if (hlen >= HDR_MAX) {
            break;
        }
    }
    if (body_at < 0) {
        free(hdr);
        aos_http_stream_close(s);
        set_state(c, c->state, "no answer from the server");
        return R_FAILED;
    }

    int status = 0;
    if (strncmp(hdr, "HTTP/", 5) == 0) {
        const char *sp = strchr(hdr, ' ');
        status = sp ? atoi(sp + 1) : 0;
    } else if (strncmp(hdr, "ICY", 3) == 0) {
        status = atoi(hdr + 4);
    }
    char v[256];

    if (status >= 300 && status < 400) {
        int ret = R_FAILED;
        if (hdr_get(hdr, "Location", v, sizeof(v))) {
            char next[512];
            url_resolve(next, sizeof(next), &u, v);
            snprintf(url, url_cap, "%s", next);
            ret = R_REDIRECT;
        } else {
            set_state(c, c->state, "a redirect with nowhere to go");
        }
        free(hdr);
        aos_http_stream_close(s);
        return ret;
    }
    if (status != 200) {
        char e[64];
        snprintf(e, sizeof(e), status ? "the server said %d" : "not an HTTP answer", status);
        free(hdr);
        aos_http_stream_close(s);
        /* 404, 403, 410: the station is not there, and will not be in ten
         * seconds either. 5xx ("server full") is worth another try. */
        set_state(c, (status >= 400 && status < 500) ? AOS_RADIO_FAILED : c->state, e);
        return (status >= 400 && status < 500) ? R_FATAL : R_FAILED;
    }

    char ctype[64] = "";
    hdr_get(hdr, "Content-Type", ctype, sizeof(ctype));
    for (char *p = ctype; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    bool playlist = strstr(ctype, "mpegurl") || strstr(ctype, "scpls") || strstr(ctype, "x-pls") ||
                    ends_with(u.path, ".pls") || ends_with(u.path, ".m3u") ||
                    ends_with(u.path, ".m3u8");
    if (playlist && !strstr(ctype, "audio/mpeg")) {
        char *body = BIG_ALLOC(PLAYLIST_MAX + 1);
        int blen = 0;
        if (body) {
            blen = hlen - body_at;
            if (blen > PLAYLIST_MAX) {
                blen = PLAYLIST_MAX;
            }
            memcpy(body, hdr + body_at, (size_t)blen);
            for (int tries = 0; blen < PLAYLIST_MAX && tries < 5 && !c->stop;) {
                int r = aos_http_stream_recv(s, body + blen, PLAYLIST_MAX - blen);
                if (r == -2) {
                    tries++;
                    continue;
                }
                if (r <= 0) {
                    break;
                }
                blen += r;
            }
            body[blen] = '\0';
        }
        free(hdr);
        aos_http_stream_close(s);
        bool hls = false;
        char next[512];
        bool found = body && playlist_first(body, next, sizeof(next), &hls);
        free(body);
        if (hls) {
            set_state(c, AOS_RADIO_FAILED, "HLS: not supported, only MP3 streams");
            return R_FATAL;
        }
        if (!found) {
            set_state(c, AOS_RADIO_FAILED, "an empty playlist");
            return R_FATAL;
        }
        snprintf(url, url_cap, "%s", next);
        return R_REDIRECT;
    }
    if (strstr(ctype, "aac") || strstr(ctype, "mp4") || strstr(ctype, "ogg") ||
        strstr(ctype, "opus") || strstr(ctype, "flac") || strstr(ctype, "wav")) {
        char e[64];
        snprintf(e, sizeof(e), "%.30s: only MP3 streams play", ctype);
        free(hdr);
        aos_http_stream_close(s);
        set_state(c, AOS_RADIO_FAILED, e);
        return R_FATAL;
    }

    /* The audio. What the station says of itself, then the bytes. */
    LOCK();
    snprintf(c->content_type, sizeof(c->content_type), "%s", ctype);
    if (hdr_get(hdr, "icy-name", v, sizeof(v))) {
        to_utf8(c->icy_name, sizeof(c->icy_name), v);
    }
    if (hdr_get(hdr, "icy-genre", v, sizeof(v))) {
        to_utf8(c->icy_genre, sizeof(c->icy_genre), v);
    }
    if (hdr_get(hdr, "icy-url", v, sizeof(v))) {
        scopy(c->icy_url, sizeof(c->icy_url), v);
    }
    if (hdr_get(hdr, "icy-description", v, sizeof(v))) {
        to_utf8(c->icy_desc, sizeof(c->icy_desc), v);
    }
    if (hdr_get(hdr, "icy-br", v, sizeof(v))) {
        c->kbps = (uint16_t)atoi(v);    /* "128,128" happens: atoi stops at the comma */
    }
    UNLOCK();
    c->metaint = hdr_get(hdr, "icy-metaint", v, sizeof(v)) ? (uint32_t)atoi(v) : 0;
    c->audio_left = c->metaint;
    c->meta_len = -1;
    c->chunked = false;
    if (hdr_get(hdr, "Transfer-Encoding", v, sizeof(v))) {
        for (char *p = v; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        c->chunked = strstr(v, "chunked") != NULL;
    }
    c->chunk_state = 0;
    c->chunk_left = 0;
    set_state(c, c->ready ? AOS_RADIO_PLAYING : AOS_RADIO_BUFFERING, "");

    bool let_go = false;
    int  ret = R_DROPPED;
    bool ok = feed(c, (const uint8_t *)hdr + body_at, hlen - body_at, &let_go);
    free(hdr);
    int idle = 0;
    while (ok && !c->stop) {
        int r = aos_http_stream_recv(s, buf, RECV_CHUNK);
        if (r == -2) {
            if (++idle >= IDLE_DROP_S) {
                set_state(c, c->state, "the station went quiet");
                break;
            }
            continue;
        }
        if (r <= 0) {
            set_state(c, c->state, r == 0 ? "the station closed the connection"
                                          : "the connection broke");
            break;
        }
        idle = 0;
        ok = feed(c, buf, r, &let_go);
    }
    aos_http_stream_close(s);
    if (c->stop) {
        return R_STOP;
    }
    if (let_go) {
        ret = R_LET_GO;
    }
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
    /* Internal stack: TLS and lwIP. Measured 4.7 KB at the deepest, on a
     * StreamTheWorld station (a redirect, two handshakes); 7 KB leaves room
     * for a server with a bigger certificate chain. The buffers are all in
     * PSRAM. Priority 3, under LVGL: the ring gives it seconds of slack, and
     * it never does floats, so it stays unpinned. */
    TaskHandle_t th = NULL;
    if (xTaskCreate(reader_task, "aos_radio", 7168, c, 3, &th) != pdPASS) {
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
