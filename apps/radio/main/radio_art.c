/*
 * AmoledOS - Radio: the picture in the dial. See radio_art.h.
 *
 * The lookup is two requests through the HAL's HTTP task, driven from the
 * app's tick like every other data app (APP-GUIDE section 9): the search,
 * whose answer names a picture, and the picture. iTunes gives its covers at
 * any size by URL ("100x100bb.jpg" -> "160x160bb.jpg"), so the watch asks
 * for exactly what it shows and never scales a big one down.
 *
 * A search for a jingle finds somebody's record, so the hit must share its
 * artist with the title before its cover is taken: the first word of three
 * letters or more of the artist we have, inside the artist iTunes answers.
 */
#include "radio_art.h"

#include "aos_hal.h"
#include "esp_jpeg_dec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(AOS_SIM)
#include "esp_heap_caps.h"
#endif

#define SEARCH_MAX  (16 * 1024)
#define IMAGE_MAX   (96 * 1024)
#define LOGO_MAX    (96 * 1024)

void *art_big_alloc(unsigned n)
{
#if !defined(AOS_SIM)
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

void art_big_free(void *p)
{
    free(p);
}

void art_init(radio_art_t *a)
{
    memset(a, 0, sizeof(*a));
    a->px = art_big_alloc(ART_PX * ART_PX * 2);
}

void art_free(radio_art_t *a)
{
    if (a->req > 0) {
        aos_hal_http_release(a->req);
    }
    art_big_free(a->px);
    memset(a, 0, sizeof(*a));
}


/* A bounded copy that does not cut a UTF-8 character in half, and that the
 * board's GCC does not take for a truncated snprintf (an error there). */
static void txt_copy(char *dst, size_t cap, const char *src)
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

/* ---- JPEG ------------------------------------------------------------------ */

bool art_decode(const uint8_t *data, int len, uint16_t *out)
{
    if (!data || len < 64 || !out) {
        return false;
    }
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        return false;
    }
    bool ok = false;
    uint16_t *raw = NULL;
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)data, .inbuf_len = len, .inbuf_remain = len };
    jpeg_dec_header_info_t info = {0};
    int out_len = 0;
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK || !info.width || !info.height ||
        info.width > 1600 || info.height > 1600 ||
        jpeg_dec_get_outbuf_len(dec, &out_len) != JPEG_ERR_OK) {
        goto done;
    }
    /* the decoder may pad the rows: take the stride from what it asks */
    int stride = out_len / (info.height * 2);
    if (stride < info.width) {
        goto done;
    }
    raw = jpeg_calloc_align((size_t)out_len, 16);
    if (!raw) {
        goto done;
    }
    io.outbuf = (uint8_t *)raw;
    if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) {
        goto done;
    }
    /* centred square crop, then nearest to ART_PX */
    int side = info.width < info.height ? info.width : info.height;
    int x0 = (info.width - side) / 2, y0 = (info.height - side) / 2;
    for (int y = 0; y < ART_PX; y++) {
        const uint16_t *row = raw + (y0 + y * side / ART_PX) * stride + x0;
        for (int x = 0; x < ART_PX; x++) {
            out[y * ART_PX + x] = row[x * side / ART_PX];
        }
    }
    ok = true;
done:
    if (raw) {
        jpeg_free_align(raw);
    }
    jpeg_dec_close(dec);
    return ok;
}

bool art_logo(int slot, uint16_t *out)
{
    const char *root = aos_hal_path_sd_root();
    if (!root || !out) {
        return false;
    }
    char path[160];
    snprintf(path, sizeof(path), "%s/radio/logo%d.jpg", root, slot);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    uint8_t *buf = art_big_alloc(LOGO_MAX);
    int n = buf ? (int)fread(buf, 1, LOGO_MAX, f) : 0;
    fclose(f);
    bool ok = n > 0 && n < LOGO_MAX && art_decode(buf, n, out);
    art_big_free(buf);
    return ok;
}

/* ---- the search -------------------------------------------------------------- */

static void url_encode(char *out, size_t cap, const char *in)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < cap; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') {
            out[o++] = (char)*p;
        } else if (*p == ' ') {
            out[o++] = '+';
        } else {
            out[o++] = '%';
            out[o++] = hex[*p >> 4];
            out[o++] = hex[*p & 15];
        }
    }
    out[o] = '\0';
}

/* "key":"value" in a JSON body, with \/ and \" undone; false if absent. */
static bool json_str(const char *body, const char *key, char *out, size_t cap)
{
    char k[40];
    snprintf(k, sizeof(k), "\"%s\":\"", key);
    const char *p = strstr(body, k);
    if (!p) {
        return false;
    }
    p += strlen(k);
    size_t o = 0;
    while (*p && *p != '"' && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    return o > 0;
}

static void lower(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

/* The first word of three letters or more of 'artist', inside 'theirs'.
 * (By hand: strtok and strncasecmp are not in the firmware's table.) */
static bool same_artist(const char *ours, const char *theirs)
{
    char a[96], b[96];
    txt_copy(a, sizeof(a), ours);
    txt_copy(b, sizeof(b), theirs);
    lower(a);
    lower(b);
    for (char *w = a; *w;) {
        while (*w && !isalnum((unsigned char)*w)) {
            w++;
        }
        char *e = w;
        while (*e && isalnum((unsigned char)*e)) {
            e++;
        }
        if (e - w >= 3) {
            *e = '\0';
            return strstr(b, w) != NULL;
        }
        w = e;
    }
    return true;                /* nothing to compare with */
}

static void cancel(radio_art_t *a)
{
    if (a->req > 0) {
        aos_hal_http_release(a->req);
    }
    a->req = 0;
    a->step = 0;
}

void art_request(radio_art_t *a, const char *title, const char *station)
{
    if (!a->px || strcmp(title, a->want) == 0) {
        return;
    }
    cancel(a);
    txt_copy(a->want, sizeof(a->want), title);
    a->have = false;
    a->fresh = false;
    a->album[0] = '\0';
    a->hit[0] = '\0';

    const char *sep = strstr(title, " - ");
    if (!sep || sep == title || !sep[3]) {
        return;
    }
    char artist[96];
    snprintf(artist, sizeof(artist), "%.*s", (int)(sep - title), title);
    char al[96], sl[96];
    txt_copy(al, sizeof(al), artist);
    txt_copy(sl, sizeof(sl), station ? station : "");
    lower(al);
    lower(sl);
    if (sl[0] && strncmp(al, sl, strlen(al)) == 0) {
        return;                 /* "METRO - Dance": the station talking */
    }
    int32_t on = 1;
    aos_hal_pref_get_i32("rad_art", &on);
    if (!on || aos_hal_net_state() != AOS_NET_CONNECTED) {
        return;
    }
    char term[200], url[320];
    char plain[140];
    snprintf(plain, sizeof(plain), "%s %s", artist, sep + 3);
    url_encode(term, sizeof(term), plain);
    snprintf(url, sizeof(url),
             "https://itunes.apple.com/search?media=music&entity=song&limit=1&term=%s", term);
    a->req = aos_hal_http_get(url, SEARCH_MAX);
    a->step = a->req > 0 ? 1 : 0;
}

void art_tick(radio_art_t *a)
{
    if (a->req <= 0 || aos_hal_http_state(a->req) == AOS_HTTP_BUSY) {
        return;
    }
    bool done = aos_hal_http_state(a->req) == AOS_HTTP_DONE && aos_hal_http_status(a->req) == 200;
    if (a->step == 1) {
        char art[200] = "", artist[96] = "", song[96] = "";
        const char *body = done ? aos_hal_http_body(a->req) : NULL;
        bool found = body && json_str(body, "artworkUrl100", art, sizeof(art)) &&
                     json_str(body, "artistName", artist, sizeof(artist));
        if (found) {
            json_str(body, "trackName", song, sizeof(song));
            json_str(body, "collectionName", a->album, sizeof(a->album));
        }
        aos_hal_http_release(a->req);
        a->req = 0;
        a->step = 0;
        const char *sep = strstr(a->want, " - ");
        char ours[96];
        snprintf(ours, sizeof(ours), "%.*s", sep ? (int)(sep - a->want) : 0, a->want);
        if (!found || !same_artist(ours, artist)) {
            a->album[0] = '\0';
            return;
        }
        snprintf(a->hit, sizeof(a->hit), "%.44s - %.44s", artist, song);
        char *size = strstr(art, "100x100");
        if (size) {
            memcpy(size, "160x160", 7);
        }
        a->req = aos_hal_http_get(art, IMAGE_MAX);
        a->step = a->req > 0 ? 2 : 0;
        return;
    }
    /* step 2: the picture */
    if (done && art_decode((const uint8_t *)aos_hal_http_body(a->req),
                           aos_hal_http_len(a->req), a->px)) {
        a->have = true;
        a->fresh = true;
    }
    aos_hal_http_release(a->req);
    a->req = 0;
    a->step = 0;
}
