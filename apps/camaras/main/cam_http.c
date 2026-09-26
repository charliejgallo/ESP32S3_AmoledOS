/*
 * AmoledOS - Cameras: MJPEG over HTTP (multipart/x-mixed-replace).
 *
 * The way to a stream the watch cannot decode (1080p, H.265, Main profile):
 * a transcoder on the LAN hands it over as JPEGs at the watch's size. go2rtc,
 * which Home Assistant already runs, does it with one URL:
 *
 *     http://<host>:1984/api/stream.mjpeg?src=<stream>
 *
 * The parts are not parsed by their multipart headers: the JPEGs are found by
 * their own markers, SOI (FF D8) to EOI (FF D9). Inside the entropy-coded
 * data a 0xFF is always followed by 00 or a restart marker, so FF D9 only
 * ever means the end. That works whatever boundary or Content-Length the
 * server writes, and the watch has only met go2rtc so far.
 *
 * HTTP gives no timestamps, so the lag policy is the simplest one there is:
 * after each decode, read whatever has piled up and decode only the NEWEST
 * whole frame in it. A slow watch shows fewer frames, never older ones.
 */
#include "cam.h"
#include "cam_view.h"

#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_CAP        (768 * 1024)
#define CONNECT_MS     5000
#define SILENCE_MS     6000
#define RECV_SLICE_MS  200

/* The last whole JPEG in buf[0..len): its start and length, or false. */
static bool newest_jpeg(const uint8_t *buf, int len, int *start, int *n)
{
    int eoi = -1;
    for (int i = len - 2; i >= 1; i--) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD9) {
            eoi = i + 2;
            break;
        }
    }
    if (eoi < 0) {
        return false;
    }
    for (int i = eoi - 3; i >= 0; i--) {
        if (buf[i] == 0xFF && buf[i + 1] == 0xD8 && buf[i + 2] == 0xFF) {
            *start = i;
            *n = eoi - i;
            return true;
        }
    }
    return false;
}

bool cam_http_run(cam_view_t *view, const cam_t *cam, const cam_url_t *url,
                  char *why, size_t why_len)
{
    uint8_t *buf = malloc(BUF_CAP + 1);
    if (!buf) {
        snprintf(why, why_len, "%s", _("Sin memoria"));
        return false;
    }
    bool ok = false;
    int len = 0;
    cam_view_state(view, CAM_ST_CONNECTING, NULL);
    int sock = aos_hal_tcp_connect(url->host, url->port, CONNECT_MS);
    if (sock <= 0) {
        snprintf(why, why_len, "%s", sock == AOS_TCP_ERR_DNS ? _("No se encuentra el servidor")
                                                             : _("El servidor no responde"));
        free(buf);
        return false;
    }
    cam_view_state(view, CAM_ST_NEGOTIATING, NULL);

    char auth[256] = "";
    if (cam->user[0]) {
        char plain[2 * CAM_CRED_LEN + 2], b64[200];
        snprintf(plain, sizeof(plain), "%s:%s", cam->user, cam->pass);
        cam_b64_encode((const uint8_t *)plain, (int)strlen(plain), b64, sizeof(b64));
        snprintf(auth, sizeof(auth), "Authorization: Basic %s\r\n", b64);
    }
    char req[512 + CAM_URL_LEN];
    int rn = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: AmoledOS\r\n%s\r\n",
                      url->path, url->host, auth);
    if (aos_hal_tcp_send(sock, req, rn, CONNECT_MS) != rn) {
        snprintf(why, why_len, "%s", _("El servidor cortó la conexión"));
        goto out;
    }

    /* The response headers. */
    uint64_t t0 = aos_hal_uptime_ms();
    int body = -1;
    while (body < 0) {
        if (aos_hal_worker_should_stop()) {
            ok = true;
            goto out;
        }
        if (aos_hal_uptime_ms() - t0 > CONNECT_MS || len >= 8192) {
            snprintf(why, why_len, "%s", _("El servidor no contestó"));
            goto out;
        }
        int n = aos_hal_tcp_recv(sock, buf + len, 8192 - len, RECV_SLICE_MS);
        if (n < 0) {
            snprintf(why, why_len, "%s", _("El servidor cortó la conexión"));
            goto out;
        }
        len += n;
        cam_view_bytes(view, n);
        buf[len] = '\0';
        char *e = strstr((char *)buf, "\r\n\r\n");
        if (e) {
            body = (int)(e - (char *)buf) + 4;
        }
    }
    int status = 0;
    sscanf((char *)buf, "HTTP/%*s %d", &status);
    if (status == 401) {
        snprintf(why, why_len, "%s", cam->user[0] ? _("Usuario o contraseña incorrectos")
                                                  : _("El servidor pide usuario y contraseña"));
        goto out;
    }
    if (status != 200) {
        snprintf(why, why_len, _("El servidor contestó %d"), status);
        goto out;
    }
    memmove(buf, buf + body, (size_t)(len - body));
    len -= body;
    cam_view_codec(view, CAM_CODEC_JPEG);
    cam_view_state(view, CAM_ST_WAITING, NULL);

    uint64_t last_rx = aos_hal_uptime_ms();
    uint64_t last_yield = last_rx;
    while (!aos_hal_worker_should_stop()) {
        /* Let core 0's idle task breathe: with a backlog this never blocks. */
        if (aos_hal_uptime_ms() - last_yield > 50) {
            aos_hal_worker_sleep(1);
            last_yield = aos_hal_uptime_ms();
        }
        /* Take everything that is waiting, not one slice: the newest frame
         * is the one at the end. */
        int got = 0;
        for (;;) {
            if (len >= BUF_CAP) {
                /* No EOI in 768 KB: not a JPEG stream, or a frame too big.
                 * Keep the tail, where a start may be. */
                memmove(buf, buf + BUF_CAP / 2, BUF_CAP / 2);
                len = BUF_CAP / 2;
            }
            int n = aos_hal_tcp_recv(sock, buf + len, BUF_CAP - len, got ? 0 : RECV_SLICE_MS);
            if (n < 0) {
                snprintf(why, why_len, "%s", _("El servidor cortó la conexión"));
                goto out;
            }
            if (n == 0) {
                break;
            }
            len += n;
            got += n;
            cam_view_bytes(view, n);
        }
        uint64_t now = aos_hal_uptime_ms();
        if (got) {
            last_rx = now;
        } else if (now - last_rx > SILENCE_MS) {
            snprintf(why, why_len, "%s", _("El servidor dejó de mandar video"));
            goto out;
        }
        int start, n;
        if (newest_jpeg(buf, len, &start, &n)) {
            cam_view_jpeg(view, buf + start, n, -1);
            cam_view_flush(view);
            int used = start + n;
            memmove(buf, buf + used, (size_t)(len - used));
            len -= used;
        }
    }
    ok = true;

out:
    aos_hal_tcp_close(sock);
    free(buf);
    return ok;
}
