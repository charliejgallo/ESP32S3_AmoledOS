/*
 * AmoledOS - Cameras: the list the portal writes, and URLs.
 *
 * The portal's /camaras page keeps each camera in one NVS string, cam0 ..
 * cam7, as four fields separated by 0x1F (the ASCII unit separator, which no
 * name, URL or password typed in a form carries):
 *
 *     name \x1F url \x1F user \x1F password
 *
 * The URL never carries the credentials: the portal takes a
 * rtsp://user:pass@host/ apart before saving, so the page can show the URL
 * back without showing the password. cam_gen goes up on every save; the app
 * re-reads it from its timer and rebuilds the list when it moves (the
 * arrangement of 'cotiz' and 'clima', APP-GUIDE section 10).
 */
#include "cam.h"

#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_GEN "cam_gen"

static void field(char *dst, size_t cap, const char *src, const char *end)
{
    size_t n = (size_t)(end - src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int cam_cfg_load(cam_t *cams, int max)
{
    int n = 0;
    char raw[CAM_NAME_LEN + CAM_URL_LEN + 2 * CAM_CRED_LEN + 8];
    for (int i = 0; i < CAM_MAX && n < max; i++) {
        char key[8];
        snprintf(key, sizeof(key), "cam%d", i);
        if (!aos_hal_pref_get_str(key, raw, sizeof(raw)) || !raw[0]) {
            continue;
        }
        const char *f[4];
        const char *e[4];
        const char *p = raw;
        int k = 0;
        f[0] = p;
        while (*p && k < 4) {
            if (*p == '\x1f') {
                e[k] = p;
                if (++k < 4) {
                    f[k] = p + 1;
                }
            }
            p++;
        }
        if (k < 4) {
            e[k++] = p;
        }
        while (k < 4) {
            f[k] = e[k] = p;
            k++;
        }
        cam_t *c = &cams[n];
        field(c->name, sizeof(c->name), f[0], e[0]);
        field(c->url,  sizeof(c->url),  f[1], e[1]);
        field(c->user, sizeof(c->user), f[2], e[2]);
        field(c->pass, sizeof(c->pass), f[3], e[3]);
        if (c->url[0]) {
            n++;
        }
    }
    return n;
}

int32_t cam_cfg_gen(void)
{
    int32_t gen = 0;
    aos_hal_pref_get_i32(KEY_GEN, &gen);
    return gen;
}

bool cam_url_parse(const char *url, cam_url_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *p;
    if (strncmp(url, "rtsp://", 7) == 0) {
        out->rtsp = true;
        out->port = 554;
        p = url + 7;
    } else if (strncmp(url, "http://", 7) == 0) {
        out->port = 80;
        p = url + 7;
    } else {
        return false;
    }
    /* Credentials in the URL are skipped here: the portal keeps them apart
     * and the session takes them from cam_t. */
    const char *slash = strchr(p, '/');
    const char *at = strchr(p, '@');
    if (at && (!slash || at < slash)) {
        p = at + 1;
    }
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(host_end - p));
    const char *name_end = colon ? colon : host_end;
    if (name_end == p || (size_t)(name_end - p) >= sizeof(out->host)) {
        return false;
    }
    memcpy(out->host, p, (size_t)(name_end - p));
    if (colon) {
        out->port = atoi(colon + 1);
        if (out->port <= 0 || out->port > 65535) {
            return false;
        }
    }
    snprintf(out->path, sizeof(out->path), "%s", slash ? slash : "/");
    return true;
}

/* ---- base64 --------------------------------------------------------------- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(char c)
{
    const char *p = strchr(B64, c);
    return (c && p) ? (int)(p - B64) : -1;
}

int cam_b64_decode(const char *in, int in_len, uint8_t *out, int out_max)
{
    int n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < in_len; i++) {
        int v = b64_val(in[i]);
        if (v < 0) {
            if (in[i] == '=') {
                break;
            }
            continue;
        }
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= out_max) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return n;
}

int cam_b64_encode(const uint8_t *in, int in_len, char *out, int out_max)
{
    int n = 0;
    for (int i = 0; i < in_len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) v |= in[i + 2];
        if (n + 4 >= out_max) {
            return -1;
        }
        out[n++] = B64[(v >> 18) & 63];
        out[n++] = B64[(v >> 12) & 63];
        out[n++] = i + 1 < in_len ? B64[(v >> 6) & 63] : '=';
        out[n++] = i + 2 < in_len ? B64[v & 63] : '=';
    }
    out[n] = '\0';
    return n;
}
