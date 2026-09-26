/*
 * AmoledOS - Cameras: an RTSP client (RFC 2326), video only, over TCP.
 *
 * The whole session is one TCP connection: the requests, their answers and
 * the RTP packets interleaved between them ('$', channel, 16-bit length),
 * which is what "RTP/AVP/TCP;interleaved=0-1" asks for. Over UDP the watch
 * would need two more ports, a firewall-free path and its own jitter buffer;
 * over TCP the camera paces itself to what the watch reads, and a slow
 * watch shows up as lag the view can measure and drop (cam_view.c).
 *
 *   DESCRIBE  (401 -> Digest or Basic, then again)  -> SDP
 *   SETUP     the video track only; audio is not asked for
 *   PLAY
 *   ...       RTP on the interleaved channel, and a GET_PARAMETER every
 *             half session timeout so the camera does not hang up
 *   TEARDOWN  best effort, on the way out
 *
 * Hikvision asks for Digest without qop; RFC 2617's qop=auth is here too
 * because other cameras use it.
 */
#include "cam.h"
#include "cam_depay.h"
#include "cam_view.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_CAP          (70 * 1024)     /* one interleaved frame is at most 64 KB + 4 */
#define CONNECT_MS      5000
#define REPLY_MS        6000
#define SILENCE_MS      6000            /* no bytes at all for this long: dead */
#define RECV_SLICE_MS   200             /* how often the loop looks at should_stop */
#define YIELD_EVERY_MS  50

typedef struct {
    cam_view_t      *view;
    const cam_t     *cam;
    const cam_url_t *url;
    int              sock;
    char             uri[CAM_URL_LEN + 112];    /* the request URI, no credentials */
    char             base[CAM_URL_LEN + 112];   /* Content-Base, or uri */
    int              cseq;
    char             session[96];
    int              timeout_s;

    /* authentication */
    bool             digest, basic;
    char             realm[128], nonce[160], opaque[128];
    bool             qop_auth;
    unsigned         nc;

    /* the SDP's video track */
    cam_codec_t      codec;
    int              pt;
    int              clock;
    char             control[CAM_URL_LEN + 112];
    uint8_t          sps[128], pps[64];
    int              sps_len, pps_len;
    int              channel;

    /* reception */
    uint8_t         *rx;
    int              rx_len;
} rtsp_t;

/* ---- small text helpers ---------------------------------------------------- */

/* strncasecmp and strcasestr, which the firmware does not lend to apps. */
static int ci_ncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb || !ca) {
            return ca - cb;
        }
    }
    return 0;
}

/* Copies and cuts: what does not fit a field was never valid for it. */
static void cp_str(char *dst, size_t cap, const char *src)
{
    size_t n = strnlen(src, cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static const char *ci_find(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        if (ci_ncmp(hay, needle, n) == 0) {
            return hay;
        }
    }
    return NULL;
}

/* The value of a header in a block of "Name: value\r\n" lines, trimmed.
 * Case-insensitive, as RFC 2326 says header names are. */
static bool header(const char *hdrs, const char *name, char *out, size_t cap)
{
    size_t nl = strlen(name);
    const char *p = hdrs;
    while (p && *p) {
        if (ci_ncmp(p, name, nl) == 0 && p[nl] == ':') {
            p += nl + 1;
            while (*p == ' ' || *p == '\t') p++;
            const char *e = strstr(p, "\r\n");
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n >= cap) n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }
        p = strstr(p, "\r\n");
        if (p) p += 2;
    }
    return false;
}

/* key="value" or key=value out of a Digest challenge. */
static bool param(const char *s, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key);
    for (const char *p = s; (p = ci_find(p, key)) != NULL; p += kl) {
        if (p != s && p[-1] != ' ' && p[-1] != ',') {
            continue;
        }
        const char *v = p + kl;
        while (*v == ' ') v++;
        if (*v != '=') {
            continue;
        }
        v++;
        while (*v == ' ') v++;
        const char *e;
        if (*v == '"') {
            v++;
            e = strchr(v, '"');
        } else {
            e = v;
            while (*e && *e != ',' && *e != ' ' && *e != '\r' && *e != '\n') {
                e++;
            }
        }
        if (!e) {
            return false;
        }
        size_t n = (size_t)(e - v);
        if (n >= cap) n = cap - 1;
        memcpy(out, v, n);
        out[n] = '\0';
        return true;
    }
    return false;
}

/* ---- requests ----------------------------------------------------------------- */

static void auth_header(rtsp_t *r, const char *method, const char *uri, char *out, size_t cap)
{
    out[0] = '\0';
    if (r->digest) {
        char buf[512], ha1[33], ha2[33], resp[33];
        snprintf(buf, sizeof(buf), "%s:%s:%s", r->cam->user, r->realm, r->cam->pass);
        aos_hal_md5_hex(buf, strlen(buf), ha1);
        snprintf(buf, sizeof(buf), "%s:%s", method, uri);
        aos_hal_md5_hex(buf, strlen(buf), ha2);
        char extra[160] = "";
        if (r->qop_auth) {
            char cnonce[17];
            snprintf(cnonce, sizeof(cnonce), "%08lx%08lx",
                     (unsigned long)aos_hal_uptime_ms(), (unsigned long)(r->nc * 2654435761u));
            r->nc++;
            snprintf(buf, sizeof(buf), "%s:%s:%08x:%s:auth:%s", ha1, r->nonce, r->nc, cnonce, ha2);
            aos_hal_md5_hex(buf, strlen(buf), resp);
            snprintf(extra, sizeof(extra), ", qop=auth, nc=%08x, cnonce=\"%s\"", r->nc, cnonce);
        } else {
            snprintf(buf, sizeof(buf), "%s:%s:%s", ha1, r->nonce, ha2);
            aos_hal_md5_hex(buf, strlen(buf), resp);
        }
        char op[160] = "";
        if (r->opaque[0]) {
            snprintf(op, sizeof(op), ", opaque=\"%s\"", r->opaque);
        }
        snprintf(out, cap,
                 "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                 "uri=\"%s\", response=\"%s\"%s%s\r\n",
                 r->cam->user, r->realm, r->nonce, uri, resp, extra, op);
    } else if (r->basic) {
        char plain[2 * CAM_CRED_LEN + 2], b64[200];
        snprintf(plain, sizeof(plain), "%s:%s", r->cam->user, r->cam->pass);
        cam_b64_encode((const uint8_t *)plain, (int)strlen(plain), b64, sizeof(b64));
        snprintf(out, cap, "Authorization: Basic %s\r\n", b64);
    }
}

static bool send_request(rtsp_t *r, const char *method, const char *uri, const char *extra)
{
    char auth[800];
    auth_header(r, method, uri, auth, sizeof(auth));
    char sess[128] = "";
    if (r->session[0]) {
        snprintf(sess, sizeof(sess), "Session: %s\r\n", r->session);
    }
    char req[1600];
    int n = snprintf(req, sizeof(req),
                     "%s %s RTSP/1.0\r\n"
                     "CSeq: %d\r\n"
                     "User-Agent: AmoledOS\r\n"
                     "%s%s%s\r\n",
                     method, uri, ++r->cseq, auth, sess, extra ? extra : "");
    if (n <= 0 || n >= (int)sizeof(req)) {
        return false;
    }
    return aos_hal_tcp_send(r->sock, req, n, REPLY_MS) == n;
}

/* Reads until a whole response is in: status, headers (NUL-terminated in
 * hdrs), body (NUL-terminated in body). Interleaved packets that arrive
 * first are handed to the depacketiser, if there is one. */
static int read_reply(rtsp_t *r, cam_depay_t *dp, char *hdrs, size_t hcap, char *body, size_t bcap)
{
    uint64_t t0 = aos_hal_uptime_ms();
    for (;;) {
        /* Skip whole interleaved frames at the front. */
        while (r->rx_len >= 4 && r->rx[0] == '$') {
            int len = (r->rx[2] << 8) | r->rx[3];
            if (r->rx_len < 4 + len) {
                break;
            }
            if (dp && r->rx[1] == r->channel) {
                cam_depay_packet(dp, r->rx + 4, len);
            }
            memmove(r->rx, r->rx + 4 + len, (size_t)(r->rx_len - 4 - len));
            r->rx_len -= 4 + len;
        }
        if (r->rx_len > 0 && r->rx[0] != '$') {
            r->rx[r->rx_len < RX_CAP ? r->rx_len : RX_CAP - 1] = '\0';
            char *end = strstr((char *)r->rx, "\r\n\r\n");
            if (end) {
                int hlen = (int)(end - (char *)r->rx) + 4;
                size_t hn = (size_t)hlen < hcap ? (size_t)hlen : hcap - 1;
                memcpy(hdrs, r->rx, hn);
                hdrs[hn] = '\0';
                char cl[16] = "0";
                header(hdrs, "Content-Length", cl, sizeof(cl));
                int blen = atoi(cl);
                if (blen < 0 || hlen + blen > RX_CAP - 1) {
                    return -1;
                }
                if (r->rx_len >= hlen + blen) {
                    size_t bn = (size_t)blen < bcap ? (size_t)blen : bcap - 1;
                    if (body) {
                        memcpy(body, r->rx + hlen, bn);
                        body[bn] = '\0';
                    }
                    memmove(r->rx, r->rx + hlen + blen, (size_t)(r->rx_len - hlen - blen));
                    r->rx_len -= hlen + blen;
                    int status = 0;
                    if (sscanf(hdrs, "RTSP/1.0 %d", &status) != 1) {
                        return -1;
                    }
                    return status;
                }
            }
        }
        if (aos_hal_worker_should_stop() || aos_hal_uptime_ms() - t0 > REPLY_MS) {
            return -1;
        }
        if (r->rx_len >= RX_CAP - 1) {
            return -1;
        }
        int n = aos_hal_tcp_recv(r->sock, r->rx + r->rx_len, RX_CAP - 1 - r->rx_len, RECV_SLICE_MS);
        if (n < 0) {
            return -1;
        }
        r->rx_len += n;
        cam_view_bytes(r->view, n);
    }
}

/* Takes the camera's challenge from a 401. Digest wins over Basic. */
static bool take_challenge(rtsp_t *r, const char *hdrs)
{
    r->digest = r->basic = false;
    const char *p = hdrs;
    while ((p = ci_find(p, "WWW-Authenticate:")) != NULL) {
        p += 17;
        while (*p == ' ') p++;
        if (ci_ncmp(p, "Digest", 6) == 0) {
            const char *e = strstr(p, "\r\n");
            char line[512];
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            memcpy(line, p, n);
            line[n] = '\0';
            param(line, "realm", r->realm, sizeof(r->realm));
            param(line, "nonce", r->nonce, sizeof(r->nonce));
            r->opaque[0] = '\0';
            param(line, "opaque", r->opaque, sizeof(r->opaque));
            char qop[64] = "";
            r->qop_auth = param(line, "qop", qop, sizeof(qop)) && strstr(qop, "auth");
            r->nc = 0;
            r->digest = true;
        } else if (ci_ncmp(p, "Basic", 5) == 0 && !r->digest) {
            r->basic = true;
        }
    }
    return r->digest || r->basic;
}

/* ---- SDP ------------------------------------------------------------------ */

static void resolve(const rtsp_t *r, const char *ctl, char *out, size_t cap)
{
    if (!ctl[0] || strcmp(ctl, "*") == 0) {
        snprintf(out, cap, "%s", r->base);
    } else if (ci_ncmp(ctl, "rtsp://", 7) == 0) {
        snprintf(out, cap, "%s", ctl);
    } else {
        size_t bl = strlen(r->base);
        snprintf(out, cap, "%s%s%s", r->base, (bl && r->base[bl - 1] == '/') ? "" : "/", ctl);
    }
}

static bool parse_sdp(rtsp_t *r, const char *sdp)
{
    bool in_video = false;
    char ctl[CAM_URL_LEN + 112] = "";
    r->codec = CAM_CODEC_NONE;
    r->pt = -1;
    r->clock = 90000;
    const char *p = sdp;
    while (*p) {
        const char *e = strpbrk(p, "\r\n");
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[512];
        if (n >= sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';

        if (strncmp(line, "m=", 2) == 0) {
            if (in_video && r->codec != CAM_CODEC_NONE) {
                break;                              /* the first usable video track wins */
            }
            in_video = strncmp(line, "m=video", 7) == 0;
            if (in_video) {
                /* m=video 0 RTP/AVP 96 */
                const char *last = strrchr(line, ' ');
                r->pt = last ? atoi(last + 1) : -1;
                ctl[0] = '\0';
                if (r->pt == 26) {
                    r->codec = CAM_CODEC_JPEG;     /* static type: may come with no rtpmap */
                }
            }
        } else if (in_video && strncmp(line, "a=rtpmap:", 9) == 0) {
            int pt = atoi(line + 9);
            const char *enc = strchr(line, ' ');
            if (pt == r->pt && enc) {
                enc++;
                if (ci_ncmp(enc, "H264/", 5) == 0) {
                    r->codec = CAM_CODEC_H264;
                    r->clock = atoi(enc + 5);
                } else if (ci_ncmp(enc, "JPEG/", 5) == 0) {
                    r->codec = CAM_CODEC_JPEG;
                    r->clock = atoi(enc + 5);
                } else {
                    r->codec = CAM_CODEC_NONE;      /* H265, MP4V...: not for this watch */
                }
            }
        } else if (in_video && strncmp(line, "a=control:", 10) == 0) {
            cp_str(ctl, sizeof(ctl), line + 10);
        } else if (in_video && strncmp(line, "a=fmtp:", 7) == 0) {
            char sets[256];
            if (param(line, "sprop-parameter-sets", sets, sizeof(sets)) ||
                (strstr(line, "sprop-parameter-sets=") &&
                 sscanf(strstr(line, "sprop-parameter-sets=") + 21, "%255[^; \t]", sets) == 1)) {
                char *comma = strchr(sets, ',');
                if (comma) {
                    *comma = '\0';
                    r->pps_len = cam_b64_decode(comma + 1, (int)strlen(comma + 1), r->pps, sizeof(r->pps));
                }
                r->sps_len = cam_b64_decode(sets, (int)strlen(sets), r->sps, sizeof(r->sps));
            }
        }
        if (!e) {
            break;
        }
        p = e + 1;
        while (*p == '\r' || *p == '\n') p++;
    }
    resolve(r, ctl, r->control, sizeof(r->control));
    if (r->clock <= 0) {
        r->clock = 90000;
    }
    return r->codec != CAM_CODEC_NONE;
}

/* ---- the session ------------------------------------------------------------ */

static void why_set(char *why, size_t cap, const char *text)
{
    snprintf(why, cap, "%s", text);
}

static bool reconnect(rtsp_t *r)
{
    if (r->sock > 0) {
        aos_hal_tcp_close(r->sock);
    }
    r->rx_len = 0;
    r->sock = aos_hal_tcp_connect(r->url->host, r->url->port, CONNECT_MS);
    return r->sock > 0;
}

bool cam_rtsp_run(cam_view_t *view, const cam_t *cam, const cam_url_t *url,
                  char *why, size_t why_len)
{
    rtsp_t *r = calloc(1, sizeof(*r));
    if (!r) {
        why_set(why, why_len, _("Sin memoria"));
        return false;
    }
    r->view = view;
    r->cam = cam;
    r->url = url;
    r->timeout_s = 60;
    r->channel = 0;
    r->rx = malloc(RX_CAP);
    cam_depay_t dp = {0};
    bool ok = false;
    char *hdrs = malloc(4096);
    char *body = malloc(4096);
    if (!r->rx || !hdrs || !body) {
        why_set(why, why_len, _("Sin memoria"));
        goto out;
    }
    if (url->port == 554) {
        snprintf(r->uri, sizeof(r->uri), "rtsp://%s%s", url->host, url->path);
    } else {
        snprintf(r->uri, sizeof(r->uri), "rtsp://%s:%d%s", url->host, url->port, url->path);
    }
    snprintf(r->base, sizeof(r->base), "%s", r->uri);

    cam_view_state(view, CAM_ST_CONNECTING, NULL);
    r->sock = aos_hal_tcp_connect(url->host, url->port, CONNECT_MS);
    if (r->sock <= 0) {
        why_set(why, why_len, r->sock == AOS_TCP_ERR_DNS ? _("No se encuentra la cámara")
                                                         : _("La cámara no responde"));
        r->sock = 0;
        goto out;
    }
    cam_view_state(view, CAM_ST_NEGOTIATING, NULL);

    /* DESCRIBE, with the challenge if it comes. */
    int st = -1;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!send_request(r, "DESCRIBE", r->uri, "Accept: application/sdp\r\n")) {
            st = -1;
        } else {
            st = read_reply(r, NULL, hdrs, 4096, body, 4096);
        }
        if (st == 401 && attempt == 0 && take_challenge(r, hdrs)) {
            continue;
        }
        if (st < 0 && attempt < 2 && reconnect(r)) {
            continue;                               /* some cameras hang up after a 401 */
        }
        break;
    }
    if (st == 401) {
        why_set(why, why_len, cam->user[0] ? _("Usuario o contraseña incorrectos")
                                           : _("La cámara pide usuario y contraseña"));
        goto out;
    }
    if (st != 200) {
        if (st < 0) {
            why_set(why, why_len, _("La cámara no contestó"));
        } else {
            snprintf(why, why_len, _("La cámara contestó %d"), st);
        }
        goto out;
    }
    char cb[CAM_URL_LEN + 112];
    if (header(hdrs, "Content-Base", cb, sizeof(cb)) && ci_ncmp(cb, "rtsp://", 7) == 0) {
        snprintf(r->base, sizeof(r->base), "%s", cb);
    }
    if (!parse_sdp(r, body)) {
        why_set(why, why_len, _("El video no es H.264 ni MJPEG"));
        goto out;
    }
    cam_view_codec(view, r->codec);
    aos_hal_log("camaras", "sdp: %s pt %d clock %d sps %d pps %d control %s",
                r->codec == CAM_CODEC_H264 ? "H264" : "JPEG", r->pt, r->clock,
                r->sps_len, r->pps_len, r->control);

    /* SETUP: the video track, interleaved on this same connection. */
    if (!send_request(r, "SETUP", r->control,
                      "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n") ||
        (st = read_reply(r, NULL, hdrs, 4096, body, 4096)) != 200) {
        snprintf(why, why_len, _("La cámara rechazó el pedido (SETUP %d)"), st);
        goto out;
    }
    char sess[160];
    if (!header(hdrs, "Session", sess, sizeof(sess))) {
        why_set(why, why_len, _("La cámara no abrió la sesión"));
        goto out;
    }
    char *semi = strchr(sess, ';');
    if (semi) {
        const char *to = ci_find(semi, "timeout=");
        if (to) {
            r->timeout_s = atoi(to + 8);
        }
        *semi = '\0';
    }
    cp_str(r->session, sizeof(r->session), sess);
    char tr[160];
    if (header(hdrs, "Transport", tr, sizeof(tr))) {
        const char *il = ci_find(tr, "interleaved=");
        if (il) {
            r->channel = atoi(il + 12);
        }
    }
    if (r->timeout_s < 10) {
        r->timeout_s = 60;
    }

    if (!cam_depay_init(&dp, view, r->codec, r->clock)) {
        why_set(why, why_len, _("Sin memoria"));
        goto out;
    }
    /* The parameter sets from the SDP go first: some cameras only repeat
     * them in-band before each keyframe, and the first keyframe may be two
     * seconds away. */
    if (r->codec == CAM_CODEC_H264) {
        uint8_t nal[4 + 128];
        if (r->sps_len > 0) {
            memcpy(nal, "\0\0\0\1", 4);
            memcpy(nal + 4, r->sps, (size_t)r->sps_len);
            cam_view_nal(view, nal, 4 + r->sps_len, -1);
        }
        if (r->pps_len > 0) {
            memcpy(nal, "\0\0\0\1", 4);
            memcpy(nal + 4, r->pps, (size_t)r->pps_len);
            cam_view_nal(view, nal, 4 + r->pps_len, -1);
        }
    }

    if (!send_request(r, "PLAY", r->base, "Range: npt=0.000-\r\n") ||
        (st = read_reply(r, &dp, hdrs, 4096, body, 4096)) != 200) {
        snprintf(why, why_len, _("La cámara rechazó el pedido (PLAY %d)"), st);
        goto out;
    }
    cam_view_state(view, CAM_ST_WAITING, NULL);

    /* ---- streaming ---- */
    uint64_t last_rx = aos_hal_uptime_ms();
    uint64_t last_ka = last_rx;
    uint32_t ka_ms = (uint32_t)r->timeout_s * 1000u / 2u;
    bool ka_options = false;
    uint64_t last_yield = last_rx;
    while (!aos_hal_worker_should_stop()) {
        int n = aos_hal_tcp_recv(r->sock, r->rx + r->rx_len, RX_CAP - r->rx_len, RECV_SLICE_MS);
        uint64_t now = aos_hal_uptime_ms();
        /* The idle task of core 0 must run now and then (task watchdog), and
         * with a backlog in the socket this loop never blocks. */
        if (now - last_yield > YIELD_EVERY_MS) {
            aos_hal_worker_sleep(1);
            last_yield = now;
        }
        if (n < 0) {
            why_set(why, why_len, _("La cámara cortó la conexión"));
            goto out;
        }
        if (n == 0) {
            if (now - last_rx > SILENCE_MS) {
                why_set(why, why_len, _("La cámara dejó de mandar video"));
                goto out;
            }
        } else {
            last_rx = now;
            r->rx_len += n;
            cam_view_bytes(view, n);
        }

        int i = 0;
        while (i < r->rx_len) {
            uint8_t *p = r->rx + i;
            int left = r->rx_len - i;
            if (p[0] == '$') {
                if (left < 4) break;
                int len = (p[2] << 8) | p[3];
                if (left < 4 + len) break;
                if (p[1] == r->channel) {
                    cam_depay_packet(&dp, p + 4, len);
                }
                i += 4 + len;
            } else if (left >= 8 && memcmp(p, "RTSP/1.0", 8) == 0) {
                /* The answer to a keep-alive. Only its length matters, and
                 * whether GET_PARAMETER was refused. */
                char *end = NULL;
                for (int k = 0; k + 3 < left; k++) {
                    if (p[k] == '\r' && p[k + 1] == '\n' && p[k + 2] == '\r' && p[k + 3] == '\n') {
                        end = (char *)p + k;
                        break;
                    }
                }
                if (!end) break;
                int hlen = (int)(end - (char *)p) + 4;
                char h[512];
                int hn = hlen < (int)sizeof(h) - 1 ? hlen : (int)sizeof(h) - 1;
                memcpy(h, p, (size_t)hn);
                h[hn] = '\0';
                char cl[16] = "0";
                header(h, "Content-Length", cl, sizeof(cl));
                int blen = atoi(cl);
                if (left < hlen + blen) break;
                int code = 0;
                sscanf(h, "RTSP/1.0 %d", &code);
                if (code == 405 || code == 501 || code == 454) {
                    ka_options = true;
                }
                i += hlen + blen;
            } else {
                i++;                                /* lost sync: find the next '$' */
            }
        }
        /* Everything the socket had is parsed: a JPEG stream decodes the
         * newest frame of the lot now (cam_view.c). */
        cam_view_flush(view);
        if (i > 0) {
            memmove(r->rx, r->rx + i, (size_t)(r->rx_len - i));
            r->rx_len -= i;
        }
        if (r->rx_len >= RX_CAP) {
            r->rx_len = 0;                          /* garbage that never parsed */
        }

        if (now - last_ka > ka_ms) {
            last_ka = now;
            send_request(r, ka_options ? "OPTIONS" : "GET_PARAMETER", r->base, NULL);
        }
    }
    ok = true;

out:
    if (r->sock > 0) {
        if (r->session[0]) {
            send_request(r, "TEARDOWN", r->base, NULL);
        }
        aos_hal_tcp_close(r->sock);
    }
    cam_depay_free(&dp);
    free(hdrs);
    free(body);
    free(r->rx);
    free(r);
    return ok;
}
