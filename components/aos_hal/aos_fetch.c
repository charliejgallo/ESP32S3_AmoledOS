/*
 * AmoledOS - A GET that follows redirects. See aos_fetch.h.
 *
 * How it goes, and why:
 *
 *  - HTTP/1.0 and "Connection: close", like aos_http.c: the server closes on
 *    finishing and does not chunk, so the body is what comes after the
 *    headers, up to its Content-Length or to the close. GitHub's three hops
 *    answer 1.0 (checked with curl --http1.0, 2026-11-25). A server that
 *    chunks anyway is refused rather than misread.
 *  - Redirects are followed across hosts, six at most, never from https down
 *    to http. A Location may be absolute or a path on the same host.
 *  - The headers are read into a buffer of their own: the signed address of
 *    GitHub's last hop is ~900 characters and the headers that come back with
 *    it run to several KB. Whatever of the body arrived with them goes to the
 *    sink first.
 *  - Everything here is in PSRAM -the address, the headers, the block-, and
 *    the TLS context too (aos_http_stream_open). What is not is the stack of
 *    the task that calls: when the sink writes the flash it has to be
 *    internal.
 */
#include "aos_fetch.h"
#include "aos_hal.h"
#include "aos_http_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef AOS_SIM
  #define BIG(n)   calloc(1, (n))
#else
  #include "esp_heap_caps.h"
  #define BIG(n)   heap_caps_calloc(1, (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#endif

#define URL_MAX         2048
#define HDR_MAX         16384
#define BLOCK           4096
#define MAX_REDIRECTS   6
#define STALL_S         20      /* seconds connected without a byte */

typedef struct {
    bool        tls;
    char        host[96];
    int         port;
    const char *path;           /* into the address it was split from */
} url_t;

static bool url_split(const char *url, url_t *u)
{
    const char *h;
    if (strncmp(url, "https://", 8) == 0) {
        u->tls = true;
        h = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        u->tls = false;
        h = url + 7;
    } else {
        return false;
    }
    const char *slash = strchr(h, '/');
    const char *end = slash ? slash : h + strlen(h);
    const char *colon = memchr(h, ':', (size_t)(end - h));
    int hl = (int)((colon ? colon : end) - h);
    if (hl <= 0 || hl >= (int)sizeof(u->host)) {
        return false;
    }
    memcpy(u->host, h, (size_t)hl);
    u->host[hl] = 0;
    u->port = colon ? atoi(colon + 1) : (u->tls ? 443 : 80);
    if (u->port <= 0 || u->port > 65535) {
        return false;
    }
    u->path = slash ? slash : "/";
    return true;
}

/* The value of a header, case-insensitively, copied out; false when absent.
 * 'hdr' is the block of headers ending in its blank line, NUL-terminated. */
static bool header(const char *hdr, const char *name, char *out, size_t len)
{
    size_t nl = strlen(name);
    for (const char *l = strstr(hdr, "\r\n"); l && l[2]; l = strstr(l + 2, "\r\n")) {
        const char *s = l + 2;
        if (strncasecmp(s, name, nl) == 0 && s[nl] == ':') {
            s += nl + 1;
            while (*s == ' ' || *s == '\t') s++;
            const char *e = strstr(s, "\r\n");
            size_t n = e ? (size_t)(e - s) : strlen(s);
            if (n >= len) {
                return false;   /* a value that does not fit is not cut short */
            }
            memcpy(out, s, n);
            out[n] = 0;
            return true;
        }
    }
    return false;
}

/* Where a redirect leads, into 'url' (URL_MAX). 'loc' is absolute or a path
 * of the host 'from' is on. */
static int follow(char *url, const url_t *from, const char *loc)
{
    char *next = BIG(URL_MAX);
    if (!next) {
        return AOS_HTTP_ERR_MEM;
    }
    int n;
    if (strncmp(loc, "https://", 8) == 0 || strncmp(loc, "http://", 7) == 0) {
        n = snprintf(next, URL_MAX, "%s", loc);
    } else if (loc[0] == '/') {
        bool std = from->port == (from->tls ? 443 : 80);
        char port[8] = "";
        if (!std) {
            snprintf(port, sizeof port, ":%d", from->port);
        }
        n = snprintf(next, URL_MAX, "%s://%s%s%s", from->tls ? "https" : "http",
                     from->host, port, loc);
    } else {
        free(next);
        return AOS_FETCH_ERR_REDIRECT;
    }
    if (n <= 0 || n >= URL_MAX) {
        free(next);
        return AOS_FETCH_ERR_URL;
    }
    if (from->tls && strncmp(next, "https://", 8) != 0) {
        free(next);
        return AOS_FETCH_ERR_REDIRECT;      /* never down to plain text */
    }
    memcpy(url, next, (size_t)n + 1);
    free(next);
    return 0;
}

static bool cancelled(const aos_fetch_t *f)
{
    return f->cancel && *f->cancel;
}

int aos_fetch_get(aos_fetch_t *f)
{
    f->got = 0;
    f->total = 0;
    f->redirects = 0;
    f->host[0] = 0;

    char    *url = BIG(URL_MAX);
    char    *hdr = BIG(HDR_MAX);
    char    *req = BIG(URL_MAX + 256);
    uint8_t *blk = BIG(BLOCK);
    int rc;
    if (!url || !hdr || !req || !blk) {
        rc = AOS_HTTP_ERR_MEM;
        goto out;
    }
    if (!f->url || snprintf(url, URL_MAX, "%s", f->url) >= URL_MAX) {
        rc = AOS_FETCH_ERR_URL;
        goto out;
    }
    aos_http_stream_init();

    for (;;) {
        url_t u;
        if (!url_split(url, &u)) {
            rc = AOS_FETCH_ERR_URL;
            break;
        }
        snprintf(f->host, sizeof f->host, "%s", u.host);

        aos_http_stream_t *s = NULL;
        rc = aos_http_stream_open(&s, u.host, u.port, u.tls);
        if (rc != 0) {
            break;
        }

        char port[8] = "";
        if (u.port != (u.tls ? 443 : 80)) {
            snprintf(port, sizeof port, ":%d", u.port);
        }
        int n = snprintf(req, URL_MAX + 256,
                         "GET %s HTTP/1.0\r\n"
                         "Host: %s%s\r\n"
                         "User-Agent: AmoledOS/%s\r\n"
                         "Accept: */*\r\n"
                         "Connection: close\r\n\r\n",
                         u.path, u.host, port, aos_hal_firmware_version());
        if (n <= 0 || n >= URL_MAX + 256) {
            aos_http_stream_close(s);
            rc = AOS_FETCH_ERR_URL;
            break;
        }
        if (!aos_http_stream_send(s, req, n)) {
            aos_http_stream_close(s);
            rc = AOS_HTTP_ERR_SEND;
            break;
        }

        /* The headers, and whatever of the body came with them. */
        int hl = 0, idle = 0;
        char *end = NULL;
        rc = 0;
        while (!end) {
            if (cancelled(f)) {
                rc = AOS_FETCH_ERR_CANCEL;
                break;
            }
            int r = aos_http_stream_recv(s, hdr + hl, HDR_MAX - 1 - hl);
            if (r == -2) {
                if (++idle >= STALL_S) {
                    rc = AOS_FETCH_ERR_STALL;
                    break;
                }
                continue;
            }
            if (r <= 0) {
                rc = hl ? AOS_HTTP_ERR_PROTO : AOS_HTTP_ERR_RECV;
                break;
            }
            idle = 0;
            hl += r;
            hdr[hl] = 0;
            end = strstr(hdr, "\r\n\r\n");
            if (!end && hl >= HDR_MAX - 1) {
                rc = AOS_HTTP_ERR_PROTO;
                break;
            }
        }
        if (rc != 0) {
            aos_http_stream_close(s);
            break;
        }
        if (hl < 12 || strncmp(hdr, "HTTP/1.", 7) != 0) {
            aos_http_stream_close(s);
            rc = AOS_HTTP_ERR_PROTO;
            break;
        }
        int status = atoi(hdr + 9);
        const uint8_t *extra = (const uint8_t *)end + 4;
        int extra_len = hl - (int)((const char *)extra - hdr);
        end[2] = 0;                 /* the headers end at their last CRLF */

        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            /* the Location of a signed address is long: it goes in req */
            bool has = header(hdr, "location", req, URL_MAX);
            aos_http_stream_close(s);
            if (!has || ++f->redirects > MAX_REDIRECTS) {
                rc = AOS_FETCH_ERR_REDIRECT;
                break;
            }
            rc = follow(url, &u, req);
            if (rc != 0) {
                break;
            }
            continue;
        }
        if (status < 200 || status >= 300) {
            aos_http_stream_close(s);
            rc = status;
            break;
        }

        char val[24];
        if (header(hdr, "transfer-encoding", val, sizeof val) &&
            strncasecmp(val, "chunked", 7) == 0) {
            aos_http_stream_close(s);
            rc = AOS_FETCH_ERR_CHUNKED;
            break;
        }
        uint32_t total = 0;
        if (header(hdr, "content-length", val, sizeof val)) {
            total = (uint32_t)strtoul(val, NULL, 10);
        }
        if (f->max_bytes && total > f->max_bytes) {
            aos_http_stream_close(s);
            rc = AOS_FETCH_ERR_BIG;
            break;
        }
        f->total = total;

        /* The body. */
        rc = status;
        if (extra_len > 0) {
            if (total && (uint32_t)extra_len > total) {
                extra_len = (int)total;
            }
            if (f->max_bytes && (uint32_t)extra_len > f->max_bytes) {
                rc = AOS_FETCH_ERR_BIG;
            } else if (!f->sink(f->ctx, extra, (size_t)extra_len)) {
                rc = AOS_FETCH_ERR_SINK;
            } else {
                f->got = (uint32_t)extra_len;
            }
        }
        idle = 0;
        while (rc == status && (!total || f->got < total)) {
            if (cancelled(f)) {
                rc = AOS_FETCH_ERR_CANCEL;
                break;
            }
            uint32_t want = BLOCK;
            if (total && total - f->got < want) {
                want = total - f->got;
            }
            int r = aos_http_stream_recv(s, blk, (int)want);
            if (r == -2) {
                if (++idle >= STALL_S) {
                    rc = AOS_FETCH_ERR_STALL;
                }
                continue;
            }
            if (r == 0) {
                if (total) {
                    rc = AOS_FETCH_ERR_SHORT;
                }
                break;              /* with no length, the close is the end */
            }
            if (r < 0) {
                rc = total ? AOS_FETCH_ERR_SHORT : AOS_HTTP_ERR_RECV;
                break;
            }
            idle = 0;
            if (f->max_bytes && f->got + (uint32_t)r > f->max_bytes) {
                rc = AOS_FETCH_ERR_BIG;
                break;
            }
            if (!f->sink(f->ctx, blk, (size_t)r)) {
                rc = AOS_FETCH_ERR_SINK;
                break;
            }
            f->got += (uint32_t)r;
        }
        aos_http_stream_close(s);
        break;
    }

out:
    free(url);
    free(hdr);
    free(req);
    free(blk);
    return rc;
}

/* ---- the whole body in memory -------------------------------------------- */

typedef struct {
    char    *buf;
    uint32_t len, max;
} text_t;

static bool text_sink(void *ctx, const uint8_t *data, size_t len)
{
    text_t *t = ctx;
    if (t->len + len > t->max) {
        return false;
    }
    memcpy(t->buf + t->len, data, len);
    t->len += (uint32_t)len;
    return true;
}

int aos_fetch_text(const char *url, uint32_t max, char **out, uint32_t *len,
                   volatile bool *cancel)
{
    *out = NULL;
    if (len) {
        *len = 0;
    }
    text_t t = { .buf = BIG(max + 1), .len = 0, .max = max };
    if (!t.buf) {
        return AOS_HTTP_ERR_MEM;
    }
    aos_fetch_t f = { .url = url, .sink = text_sink, .ctx = &t,
                      .max_bytes = max, .cancel = cancel };
    int rc = aos_fetch_get(&f);
    if (rc < 200 || rc >= 300) {
        free(t.buf);
        return rc;
    }
    t.buf[t.len] = 0;
    *out = t.buf;
    if (len) {
        *len = t.len;
    }
    return rc;
}

const char *aos_fetch_reason(int rc)
{
    switch (rc) {
    case AOS_HTTP_ERR_DNS:        return "could not resolve the name";
    case AOS_HTTP_ERR_CONNECT:    return "could not connect";
    case AOS_HTTP_ERR_SEND:       return "could not send the request";
    case AOS_HTTP_ERR_RECV:       return "the connection broke";
    case AOS_HTTP_ERR_PROTO:      return "the answer is not HTTP";
    case AOS_HTTP_ERR_MEM:        return "out of memory";
    case AOS_HTTP_ERR_SIN_HORA:   return "no valid time yet: the certificate cannot be checked";
    case AOS_HTTP_ERR_TLS:        return "the server's certificate does not verify";
    case AOS_FETCH_ERR_URL:       return "not an http(s) address";
    case AOS_FETCH_ERR_REDIRECT:  return "a redirect that cannot be followed";
    case AOS_FETCH_ERR_STALL:     return "the server went silent";
    case AOS_FETCH_ERR_SHORT:     return "the download was cut short";
    case AOS_FETCH_ERR_SINK:      return "stopped by whoever took the bytes";
    case AOS_FETCH_ERR_CANCEL:    return "cancelled";
    case AOS_FETCH_ERR_BIG:       return "larger than expected";
    case AOS_FETCH_ERR_CHUNKED:   return "the server sent a chunked body";
    case 404:                     return "not on the server (404)";
    default:                      return rc >= 200 ? "the server refused it" : "failed";
    }
}
