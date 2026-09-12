/*
 * AmoledOS - Minimal HTTP client, shared between the board and the simulator.
 *
 * It is the same file on both platforms: lwIP gives the board the same POSIX
 * sockets macOS gives the simulator. All that changes is how the thread is
 * launched and how the lock is taken, and that is the eight lines below.
 *
 * Why by hand and not esp_http_client: the simulator does not have it, and for
 * a GET it is a hundred lines. The cost of maintaining it is lower than that
 * of having two different implementations and discovering the differences on
 * the board.
 *
 * Since the Remoto app came along it also sends POSTs with headers and a body
 * of their own (Home Assistant's REST API). It is the same path: the GET is a
 * POST with no body and no extra headers, not a separate function.
 *
 * HTTP/1.0 is requested on purpose. With 1.1 the server answers with
 * Transfer-Encoding chunked (measured against open-meteo) and it has to be
 * taken apart; with 1.0 it closes the connection on finishing and reading to
 * EOF is enough. Chunked and Content-Length are supported anyway, in case some
 * server uses them.
 *
 * --- TLS (2026-09-03) ------------------------------------------------------
 *
 * From now on it also speaks https://, and the API did NOT change: all that
 * can be seen from outside is that it stopped rejecting the scheme. For the
 * microSD's apps TLS is invisible —zero changes in the .so files,
 * AOS_ABI_VERSION is still 2— and that is why the whole job is in here and not
 * in a new symbol.
 *
 * The fear was that this would break the symmetry between the board and the
 * simulator and that the four #ifdefs would become forty. It did not happen,
 * and the reason is worth writing down: mbedtls 3.x is the same API on both
 * sides (the IDF ships 3.6.7, Homebrew 3.4.1), so setup, handshake, read,
 * write and close are common code. The ONLY thing that differs is where the
 * trusted certificate store comes from —the IDF's compiled-in bundle against
 * the Mac's /etc/ssl/cert.pem— and that is ONE function, trust_attach(). The
 * rest of the file does not know which platform it runs on.
 *
 * The other piece is that plain and TLS do not branch all over the place:
 * there is a conn_t with send/recv/close, and http_work() never asks again
 * whether there is encryption. That division is TLS-against-plain, not
 * board-against-Mac.
 *
 * What was measured and settles the design (docs/HANDOFF-HAL-RED-MIC.md,
 * section 3.1):
 *
 *  - With CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC the handshake costs 3.5 KB of
 *    internal RAM instead of 34.6, so HTTP_SLOTS stays at 3. Without that
 *    setting, two simultaneous requests sink internal RAM below the floor
 *    where the display's DMA buffers fail, and 3 would be irresponsible.
 *  - A full handshake is 1.6-1.8 s and a resumed one 0.6. That is why there is
 *    a session cache: without it the Remoto app would pay 1.6 s on every
 *    refresh.
 *  - With no valid time the certificate cannot be verified. That is checked
 *    BEFORE spending the handshake, and afterwards the BADCERT_FUTURE bit is
 *    translated, which is the only thing distinguishing "the board is in the
 *    past" from "I do not know that CA": both give the same return code.
 */
#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"

/* RAM audit: the http task's stack stays in internal RAM. Moving it to PSRAM
 * was tried (E1b) and crashed the board the moment Clima asked for the
 * weather: aos_hal_time_is_valid() reads a preference from NVS, and the
 * flash driver refuses a task whose stack is in PSRAM. */

#ifdef AOS_SIM
  #include <pthread.h>
  typedef pthread_mutex_t aos_lock_t;
  #define LOCK_INIT(l)    pthread_mutex_init(&(l), NULL)
  #define LOCK(l)         pthread_mutex_lock(&(l))
  #define UNLOCK(l)       pthread_mutex_unlock(&(l))
#else
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #include "freertos/semphr.h"
  #include "esp_heap_caps.h"
  #include "esp_crt_bundle.h"
  typedef SemaphoreHandle_t aos_lock_t;
  #define LOCK_INIT(l)    ((l) = xSemaphoreCreateMutex())
  #define LOCK(l)         xSemaphoreTake((l), portMAX_DELAY)
  #define UNLOCK(l)       xSemaphoreGive((l))
#endif

/* It stays at 3 because the measurement allows it: with EXTERNAL_MEM_ALLOC two
 * simultaneous handshakes leave 93 KB of internal RAM free and do not
 * fragment. Without that setting the same number is 22 KB and it has to be
 * dropped to 1. */
#define HTTP_SLOTS      3       /* weather + geocoding + one spare */
#define HTTP_TIMEOUT_S  10
#define TAG             "http"

/* How many hosts remember their TLS session. Two is enough: the real pattern
 * is one app talking to one server and refreshing, not a browser. */
#define TLS_SESSIONS    2

typedef struct {
    int              id;        /* 0 = free */
    aos_http_state_t state;
    int              status;
    char            *body;      /* response */
    int              len;
    int              max;
    bool             abandoned; /* the app called release while it was still running */
    bool             tls;       /* the URL was https:// */
    char             host[96];
    char             path[512];
    int              port;

    /* Request. They are copied because the caller may have them on its stack
     * and the background task uses them long after that stack is gone. */
    char             method[8];
    char            *hdrs;      /* extra lines, or NULL */
    char            *req_body;  /* body, or NULL */
    int              req_len;
    char             ctype[48];
} slot_t;

AOS_BSS_PSRAM static slot_t    s_slots[HTTP_SLOTS];
static aos_lock_t s_lock;
static bool      s_ready;
static int       s_next_id = 1;

/* ==========================================================================
 * Transport: a bare socket or a TLS tunnel, behind the same interface.
 *
 * http_work() further down uses conn_send/conn_recv/conn_close and never asks
 * again whether there is encryption. That is the only division needed, and it
 * is not board-against-Mac: it is TLS-against-plain, and it holds equally on
 * both.
 * ========================================================================== */

typedef struct {
    int                      fd;
    bool                     tls;
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  ent;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt         ca;     /* only used by the simulator; see trust_attach */
} conn_t;

/* -------------------------------------------------------------------------- */
/* The session cache. A full handshake is 1.6-1.8 s on this board and a resumed
 * one 0.6: measured, not estimated. Without this the Remoto app would pay the
 * whole handshake on every screen refresh.
 *
 * mbedtls_ssl_set_session() and mbedtls_ssl_get_session() deep-copy, so the
 * stored entry can be shared between tasks as long as nobody frees it at the
 * same time. That is what s_lock is for. */
typedef struct {
    char                host[96];
    mbedtls_ssl_session sess;
    bool                valid;
} tls_cache_t;

static tls_cache_t s_sess[TLS_SESSIONS];

/* Copies the host's stored session into the context, if there is one. Under
 * the lock. */
static void session_restore(conn_t *c, const char *host)
{
    LOCK(s_lock);
    for (int i = 0; i < TLS_SESSIONS; i++) {
        if (s_sess[i].valid && strcmp(s_sess[i].host, host) == 0) {
            mbedtls_ssl_set_session(&c->ssl, &s_sess[i].sess);
            break;
        }
    }
    UNLOCK(s_lock);
}

/* Stores the freshly negotiated session. If there is no free slot it
 * overwrites the first one: with two entries it is not worth tracking which
 * was used last. */
static void session_save(conn_t *c, const char *host)
{
    mbedtls_ssl_session tmp;
    mbedtls_ssl_session_init(&tmp);
    if (mbedtls_ssl_get_session(&c->ssl, &tmp) != 0) {
        mbedtls_ssl_session_free(&tmp);
        return;
    }

    LOCK(s_lock);
    int libre = -1;
    for (int i = 0; i < TLS_SESSIONS; i++) {
        if (s_sess[i].valid && strcmp(s_sess[i].host, host) == 0) { libre = i; break; }
        if (!s_sess[i].valid && libre < 0)                          libre = i;
    }
    if (libre < 0) {
        libre = 0;
    }
    if (s_sess[libre].valid) {
        mbedtls_ssl_session_free(&s_sess[libre].sess);
    }
    s_sess[libre].sess  = tmp;          /* ownership is transferred */
    s_sess[libre].valid = true;
    snprintf(s_sess[libre].host, sizeof(s_sess[libre].host), "%s", host);
    UNLOCK(s_lock);
}

/* --------------------------------------------------------------------------
 * THE ONLY DIFFERENCE BETWEEN THE TWO PLATFORMS, and that is why it sits alone
 * in a function of its own rather than scattered through the file: where the
 * list of trusted authorities comes from.
 *
 * On the board it is the bundle ESP-IDF compiles into the binary (CMN profile:
 * measured, it validates the same servers as FULL and takes 48 KB less). On
 * the Mac it is the system's file, which is already kept up to date without
 * anybody maintaining it.
 * -------------------------------------------------------------------------- */
static bool trust_attach(conn_t *c)
{
#ifdef AOS_SIM
    mbedtls_x509_crt_init(&c->ca);
    if (mbedtls_x509_crt_parse_file(&c->ca, "/etc/ssl/cert.pem") < 0) {
        return false;
    }
    mbedtls_ssl_conf_ca_chain(&c->conf, &c->ca, NULL);
    return true;
#else
    (void)c->ca;
    return esp_crt_bundle_attach(&c->conf) == 0;
#endif
}

/* -------------------------------------------------------------------------- */

static void slot_free(slot_t *s)     /* with the lock held */
{
    free(s->body);
    free(s->hdrs);
    free(s->req_body);
    memset(s, 0, sizeof(*s));
}

/* Connects with a time limit. SO_RCVTIMEO does not cover connect, so it has to
 * be set non-blocking and waited on with select. */
static int tcp_connect(const char *host, int port)
{
    char service[8];
    snprintf(service, sizeof(service), "%d", port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;        /* the board does not always have IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, service, &hints, &res) != 0 || !res) {
        return AOS_HTTP_ERR_DNS;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return AOS_HTTP_ERR_CONNECT;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (rc != 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return AOS_HTTP_ERR_CONNECT;
        }
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd, &w);
        struct timeval tv = { .tv_sec = HTTP_TIMEOUT_S, .tv_usec = 0 };
        if (select(fd + 1, NULL, &w, NULL, &tv) <= 0) {
            close(fd);
            return AOS_HTTP_ERR_CONNECT;
        }
        int err = 0;
        socklen_t elen = sizeof(err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
            close(fd);
            return AOS_HTTP_ERR_CONNECT;
        }
    }

    fcntl(fd, F_SETFL, flags);          /* back to blocking, now with a timeout */
    struct timeval tv = { .tv_sec = HTTP_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

/* mbedtls talks to the socket through these two. An SO_RCVTIMEO expiry arrives
 * as EAGAIN and is translated to WANT_READ: the handshake loop retries it, and
 * what really cuts is conn_open()'s deadline. */
static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    int n = send(*(int *)ctx, buf, len, 0);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE
                                                         : MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return n;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    int n = recv(*(int *)ctx, buf, len, 0);
    if (n < 0) {
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ
                                                         : MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return n;
}

static void conn_close(conn_t *c)
{
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        mbedtls_ctr_drbg_free(&c->drbg);
        mbedtls_entropy_free(&c->ent);
        mbedtls_x509_crt_free(&c->ca);
        c->tls = false;
    }
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

/* Opens the connection. With tls==false it is the usual socket; with true it
 * also negotiates the tunnel. Returns 0, or the appropriate AOS_HTTP_ERR_*. */
static int conn_open(conn_t *c, const char *host, int port, bool tls)
{
    memset(c, 0, sizeof(*c));
    c->fd = -1;
    mbedtls_x509_crt_init(&c->ca);

    /* The time first, and BEFORE spending the handshake. A certificate is
     * verified against the clock: with no time there is no way to know whether
     * it is still valid, and the error coming out of the handshake does not
     * explain it (see below). */
    if (tls && !aos_hal_time_is_valid()) {
        return AOS_HTTP_ERR_SIN_HORA;
    }

    int fd = tcp_connect(host, port);
    if (fd < 0) {
        return fd;                      /* already an AOS_HTTP_ERR_* */
    }
    c->fd = fd;
    if (!tls) {
        return 0;
    }

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_entropy_init(&c->ent);
    mbedtls_ctr_drbg_init(&c->drbg);
    c->tls = true;

    if (mbedtls_ctr_drbg_seed(&c->drbg, mbedtls_entropy_func, &c->ent,
                              (const unsigned char *)"aos", 3) != 0 ||
        mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        conn_close(c);
        return AOS_HTTP_ERR_TLS;
    }

    /* VERIFY_REQUIRED and not OPTIONAL: if the chain cannot be verified, the
     * connection does not go out. A TLS that accepts any certificate gives the
     * same feeling of security with none of the properties. */
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->drbg);

    if (!trust_attach(c)) {
        conn_close(c);
        return AOS_HTTP_ERR_TLS;
    }

    /* set_hostname is mandatory, and for two different reasons: it sends SNI
     * (without it, a server with several sites answers with the wrong
     * certificate) and it is against this name that the certificate's is
     * compared. */
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0 ||
        mbedtls_ssl_set_hostname(&c->ssl, host) != 0) {
        conn_close(c);
        return AOS_HTTP_ERR_TLS;
    }

    session_restore(c, host);
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, bio_send, bio_recv, NULL);

    time_t limite = time(NULL) + HTTP_TIMEOUT_S;
    int rc;
    while ((rc = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
        if (time(NULL) > limite) {
            conn_close(c);
            return AOS_HTTP_ERR_TLS;
        }
    }

    if (rc != 0) {
        /* The return code is not enough to know what happened: "the board is
         * in the past" and "I do not know that CA" are both -0x2700. What
         * separates them is in the verification flags, and with the clock
         * wrong BOTH come back set at once —if the board believes it is 2015,
         * the bundle's root is not valid yet either—, so BADCERT_FUTURE is
         * looked at first. */
        uint32_t flags = mbedtls_ssl_get_verify_result(&c->ssl);
        conn_close(c);
        if (flags & MBEDTLS_X509_BADCERT_FUTURE) {
            return AOS_HTTP_ERR_SIN_HORA;
        }
        return AOS_HTTP_ERR_TLS;
    }

    session_save(c, host);
    return 0;
}

/* WANT_READ/WANT_WRITE on a BLOCKING socket does not mean "not yet, try again
 * right away": the socket has SO_RCVTIMEO, so getting here means
 * HTTP_TIMEOUT_S seconds have already gone by without a byte. Retrying without
 * a deadline would be an infinite loop against a server that has gone quiet
 * —the plain path did not have this, because there an expired recv returns -1
 * and cuts—. It is given ONE more round in case the expiry fell right on a
 * split record, and then it gives up. */
#define TLS_REINTENTOS 1

/* Sends everything or fails. Returns true if it all went out. */
static bool conn_send(conn_t *c, const char *buf, int len)
{
    int sent = 0;
    int reintentos = 0;
    while (sent < len) {
        if (!c->tls) {
            int w = send(c->fd, buf + sent, len - sent, 0);
            if (w <= 0) {
                return false;
            }
            sent += w;
            continue;
        }
        int w = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf + sent, len - sent);
        if (w == MBEDTLS_ERR_SSL_WANT_READ || w == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (++reintentos > TLS_REINTENTOS) {
                return false;
            }
            continue;
        }
        if (w <= 0) {
            return false;
        }
        reintentos = 0;
        sent += w;
    }
    return true;
}

/* Just like recv(): >0 bytes, 0 if the other side closed cleanly, <0 if it
 * broke. */
static int conn_recv(conn_t *c, char *buf, int max)
{
    if (!c->tls) {
        return recv(c->fd, buf, max, 0);
    }
    for (int reintentos = 0; ; reintentos++) {
        int r = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, max);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (reintentos >= TLS_REINTENTOS) {
                return -1;
            }
            continue;
        }
        /* An announced close is a legitimate ending, just like the bare
         * socket's EOF: with HTTP/1.0 that is how every response ends. */
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            return 0;
        }
        return r;
    }
}

/* Unpacks a chunked body in place. Returns the new length. */
static int dechunk(char *body, int len)
{
    int r = 0, w = 0;
    while (r < len) {
        int size = 0, digits = 0;
        while (r < len) {
            char c = body[r];
            int v;
            if      (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else break;
            size = size * 16 + v;
            digits++;
            r++;
        }
        if (!digits) {
            break;
        }
        while (r < len && body[r] != '\n') r++;   /* rest of the line */
        r++;
        if (size == 0 || r + size > len) {
            break;
        }
        memmove(body + w, body + r, size);
        w += size;
        r += size + 2;                            /* the closing CRLF */
    }
    body[w] = 0;
    return w;
}

/* The real work. Runs in the background thread. */
static void http_work(slot_t *s)
{
    int status = AOS_HTTP_ERR_CONNECT;
    int blen   = 0;
    conn_t c;

    int rc = conn_open(&c, s->host, s->port, s->tls);
    if (rc != 0) {
        status = rc;
        goto done;
    }

    /* The header is built in a purpose-allocated buffer and not on the stack:
     * a Home Assistant token is ~180 characters and a template's body can go
     * past a kilobyte, and this runs in a task with a 5 KB stack. */
    int hlen  = s->hdrs     ? (int)strlen(s->hdrs) : 0;
    int cap   = 320 + (int)strlen(s->path) + (int)strlen(s->host) + hlen;
    char *req = malloc(cap);
    if (!req) {
        status = AOS_HTTP_ERR_MEM;
        conn_close(&c);
        goto done;
    }

    int n = snprintf(req, cap,
                     "%s %s HTTP/1.0\r\n"
                     "Host: %s\r\n"
                     "User-Agent: AmoledOS/1.0\r\n"
                     "Accept: application/json\r\n",
                     s->method, s->path, s->host);
    if (hlen) {
        n += snprintf(req + n, cap - n, "%s", s->hdrs);
    }
    /* Content-Length always goes whenever there is a body, including a
     * zero-byte one: several servers (Home Assistant's among them) answer 411
     * if a POST arrives without it. */
    if (s->req_body) {
        n += snprintf(req + n, cap - n,
                      "Content-Type: %s\r\n"
                      "Content-Length: %d\r\n",
                      s->ctype[0] ? s->ctype : "application/json", s->req_len);
    }
    n += snprintf(req + n, cap - n, "\r\n");

    if (n <= 0 || n >= cap) {
        status = AOS_HTTP_ERR_PROTO;
        free(req);
        conn_close(&c);
        goto done;
    }

    /* Header and body are sent in two goes over the same connection. */
    bool sent_ok = true;
    for (int part = 0; part < 2 && sent_ok; part++) {
        const char *buf = part ? s->req_body : req;
        int         len = part ? s->req_len  : n;
        if (!buf || len <= 0) {
            continue;
        }
        sent_ok = conn_send(&c, buf, len);
    }
    free(req);
    if (!sent_ok) {
        status = AOS_HTTP_ERR_SEND;
        conn_close(&c);
        goto done;
    }

    /* Everything (headers included) is read into the same buffer and then the
     * body is moved to the front. A GET of this size does not justify two. */
    int got = 0;
    for (;;) {
        int r = conn_recv(&c, s->body + got, s->max - 1 - got);
        if (r < 0) {
            if (got == 0) {
                status = AOS_HTTP_ERR_RECV;
                conn_close(&c);
                goto done;
            }
            break;                       /* it was cut, but something arrived */
        }
        if (r == 0) {
            break;                       /* clean close */
        }
        got += r;
        if (got >= s->max - 1) {
            break;                       /* the ceiling the app asked for */
        }
    }
    conn_close(&c);
    s->body[got] = 0;

    if (got < 13 || strncmp(s->body, "HTTP/1.", 7) != 0) {
        status = AOS_HTTP_ERR_PROTO;
        goto done;
    }
    status = atoi(s->body + 9);

    char *sep = strstr(s->body, "\r\n\r\n");
    if (!sep) {
        status = AOS_HTTP_ERR_PROTO;
        goto done;
    }
    *sep = 0;
    bool chunked = (strstr(s->body, "chunked") != NULL);
    char *body = sep + 4;
    blen = got - (int)(body - s->body);
    memmove(s->body, body, blen);
    s->body[blen] = 0;
    if (chunked) {
        blen = dechunk(s->body, blen);
    }

done:
    LOCK(s_lock);
    if (s->abandoned) {
        slot_free(s);
    } else {
        s->status = status;
        s->len    = blen;
        s->state  = (status >= 200 && status < 400) ? AOS_HTTP_DONE : AOS_HTTP_FAILED;
    }
    UNLOCK(s_lock);
}

#ifdef AOS_SIM
static void *http_thread(void *arg)
{
    http_work((slot_t *)arg);
    return NULL;
}
#else
static void http_task(void *arg)
{
    http_work((slot_t *)arg);
    vTaskDelete(NULL);
}
#endif

/* -------------------------------------------------------------------------- */

static slot_t *find(int id)      /* with the lock held */
{
    if (id <= 0) {
        return NULL;
    }
    for (int i = 0; i < HTTP_SLOTS; i++) {
        /* An abandoned one keeps its id so the slot is not reused while its
         * thread is still alive, but nobody finds it any more. */
        if (s_slots[i].id == id && !s_slots[i].abandoned) {
            return &s_slots[i];
        }
    }
    return NULL;
}

int aos_hal_http_get(const char *url, int max_bytes)
{
    return aos_hal_http_request("GET", url, NULL, NULL, NULL, max_bytes);
}

int aos_hal_http_request(const char *method, const char *url,
                         const char *headers, const char *body,
                         const char *content_type, int max_bytes)
{
    bool tls;
    int  esquema;
    if (url && strncmp(url, "http://", 7) == 0) {
        tls = false; esquema = 7;
    } else if (url && strncmp(url, "https://", 8) == 0) {
        tls = true;  esquema = 8;
    } else {
        return -1;
    }
    if (!method || !method[0] || strlen(method) >= 8) {
        return -1;
    }
    if (max_bytes < 512) {
        max_bytes = 512;
    }

    /* The lock is created on the first request and not in a separate init. It
     * is safe because this function is called by the apps, and the apps always
     * run in the LVGL thread: there are no two first calls at once. Background
     * tasks only take it, never create it. */
    if (!s_ready) {
        LOCK_INIT(s_lock);
        s_ready = true;
    }

    /* host[:port]/path */
    const char *h = url + esquema;
    const char *slash = strchr(h, '/');
    const char *colon = strchr(h, ':');
    if (colon && slash && colon > slash) {
        colon = NULL;
    }
    int hlen = (int)((colon ? colon : (slash ? slash : h + strlen(h))) - h);
    if (hlen <= 0 || hlen >= 96) {
        return -1;
    }

    LOCK(s_lock);
    slot_t *s = NULL;
    for (int i = 0; i < HTTP_SLOTS; i++) {
        if (s_slots[i].id == 0) {
            s = &s_slots[i];
            break;
        }
    }
    if (!s) {
        UNLOCK(s_lock);
        return -2;                      /* all busy */
    }

    memset(s, 0, sizeof(*s));
    memcpy(s->host, h, hlen);
    s->host[hlen] = 0;
    s->tls  = tls;
    s->port = colon ? atoi(colon + 1) : (tls ? 443 : 80);
    snprintf(s->path, sizeof(s->path), "%s", slash ? slash : "/");
    snprintf(s->method, sizeof(s->method), "%s", method);
    if (content_type) {
        snprintf(s->ctype, sizeof(s->ctype), "%s", content_type);
    }

    /* Headers and body are copied here, with the lock held and before the task
     * exists: the caller may have them on its stack. */
    if (headers && headers[0]) {
        size_t n = strlen(headers);
        s->hdrs = malloc(n + 1);
        if (!s->hdrs) {
            memset(s, 0, sizeof(*s));
            UNLOCK(s_lock);
            return -3;
        }
        memcpy(s->hdrs, headers, n + 1);
    }
    if (body) {
        s->req_len = (int)strlen(body);
        s->req_body = malloc((size_t)s->req_len + 1);
        if (!s->req_body) {
            slot_free(s);
            UNLOCK(s_lock);
            return -3;
        }
        memcpy(s->req_body, body, (size_t)s->req_len + 1);
    }

#ifdef AOS_SIM
    s->body = malloc(max_bytes);
#else
    /* To PSRAM: it is a few kilobytes that have no reason to come out of
     * internal RAM. */
    s->body = heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s->body) {
        s->body = malloc(max_bytes);
    }
#endif
    if (!s->body) {
        slot_free(s);
        UNLOCK(s_lock);
        return -3;
    }
    s->max   = max_bytes;
    s->state = AOS_HTTP_BUSY;
    s->id    = s_next_id++;
    if (s_next_id <= 0) {
        s_next_id = 1;
    }
    int id = s->id;
    UNLOCK(s_lock);

#ifdef AOS_SIM
    pthread_t th;
    if (pthread_create(&th, NULL, http_thread, s) != 0) {
        LOCK(s_lock);
        slot_free(s);
        UNLOCK(s_lock);
        return -4;
    }
    pthread_detach(th);
#else
    /* 5120 is enough for plain HTTP: the deepest point is getaddrinfo. With
     * TLS the deepest point becomes verifying the certificate chain, and more
     * is needed. Both numbers are MEASURED with uxTaskGetStackHighWaterMark()
     * on the board, not picked out of the air: plain uses 2960 bytes, and
     * encrypted 4432 in the worst case seen over the chains of open-meteo,
     * howsmyssl and badssl.
     *
     * Dropping it to 6144 to save internal RAM was tried and reverted: there
     * the worst case left 1712 bytes free, 28%. That sounds like enough and it
     * is not —what is measured is THREE certificate chains, and verification
     * is recursive: a chain deeper than these uses more, and the failure mode
     * is a stack overflow, not an error—. With 7168 the worst case observed
     * leaves 2672 (37%). The kilobyte of difference per request changes
     * nothing: with all three encrypted at once the minimum general executable
     * heap is 21,828 with 6144 and 18,572 with 7168, and the apps do not eat
     * from there —their 48 KB contiguous reservation is asked for at startup,
     * before the network exists—.
     *
     * It is only paid when the URL is https because task stacks come out of
     * INTERNAL RAM. With all three requests encrypted at once the internal peak
     * is 27.9 KB and the stacks are most of it: mbedtls's share lives in PSRAM.
     *
     * Priority 4, the same as LVGL, because it spends nearly all its time
     * waiting. */
    if (xTaskCreate(http_task, "aos_http", s->tls ? 7168 : 5120, s, 4, NULL) != pdPASS) {
        LOCK(s_lock);
        slot_free(s);
        UNLOCK(s_lock);
        return -4;
    }
#endif
    return id;
}

aos_http_state_t aos_hal_http_state(int id)
{
    if (!s_ready) {
        return AOS_HTTP_FAILED;
    }
    LOCK(s_lock);
    slot_t *s = find(id);
    aos_http_state_t st = s ? s->state : AOS_HTTP_FAILED;
    UNLOCK(s_lock);
    return st;
}

int aos_hal_http_status(int id)
{
    if (!s_ready) {
        return AOS_HTTP_ERR_MEM;
    }
    LOCK(s_lock);
    slot_t *s = find(id);
    int st = s ? s->status : AOS_HTTP_ERR_MEM;
    UNLOCK(s_lock);
    return st;
}

const char *aos_hal_http_body(int id)
{
    if (!s_ready) {
        return NULL;
    }
    LOCK(s_lock);
    slot_t *s = find(id);
    const char *b = (s && s->state != AOS_HTTP_BUSY) ? s->body : NULL;
    UNLOCK(s_lock);
    return b;
}

int aos_hal_http_len(int id)
{
    if (!s_ready) {
        return 0;
    }
    LOCK(s_lock);
    slot_t *s = find(id);
    int n = (s && s->state != AOS_HTTP_BUSY) ? s->len : 0;
    UNLOCK(s_lock);
    return n;
}

void aos_hal_http_release(int id)
{
    if (!s_ready) {
        return;
    }
    LOCK(s_lock);
    slot_t *s = find(id);
    if (s) {
        if (s->state == AOS_HTTP_BUSY) {
            /* The thread is still inside the recv: the buffer cannot be pulled
             * out from under it. It is marked and the thread itself cleans up
             * on the way out. */
            s->abandoned = true;
        } else {
            slot_free(s);
        }
    }
    UNLOCK(s_lock);
}
