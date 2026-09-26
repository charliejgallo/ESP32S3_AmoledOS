/*
 * AmoledOS - the portal's /radio page and its API (branch radio).
 *
 *   GET  /radio                             the page
 *   GET  /api/radio                         the nine keys, the switches, what plays
 *   POST /api/radio  do=set&k=N&name=&url=  put a station on key N (0..8)
 *                    do=clear&k=N           empty key N
 *                    do=swap&a=N&b=M        exchange two keys (and their logos)
 *                    do=play&k=N            play key N on the watch
 *                    do=test&name=&url=     play an address that is on no key
 *                    do=pause|resume|stop|next|prev
 *                    do=vol&v=0..100
 *                    do=art&on=0|1          look covers up on iTunes, or not
 *
 * The keys are NVS strings rad0..rad8 = "name \x1F url", the arrangement of
 * the cameras: the Radio app reads them and rebuilds its keys when rad_gen
 * moves. What is bigger lives on the card and the firmware never reads it:
 * the page's own list of stations (radio/library.json, through /api/upload
 * and /api/download) and the logo of each key (radio/logoN.jpg, a 160 px
 * JPEG the browser drew). Clearing or swapping keys takes their logos along.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "aos_hal.h"

#define TAG         "radio"
#define KEYS        9
#define SEP         '\x1f'

extern const uint8_t radio_html_start[] asm("_binary_radio_html_start");
extern const uint8_t radio_html_end[]   asm("_binary_radio_html_end");

esp_err_t aos_radio_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)radio_html_start,
                           radio_html_end - radio_html_start - 1);
}

/* ---- the keys --------------------------------------------------------------- */

static bool key_load(int i, aos_radio_station_t *st)
{
    char key[8], raw[sizeof(st->name) + sizeof(st->url) + 4];
    memset(st, 0, sizeof(*st));
    snprintf(key, sizeof(key), "rad%d", i);
    if (!aos_hal_pref_get_str(key, raw, sizeof(raw)) || !raw[0]) {
        return false;
    }
    char *sep = strchr(raw, SEP);
    if (!sep) {
        return false;
    }
    *sep = '\0';
    size_t n = strnlen(raw, sizeof(st->name) - 1);
    memcpy(st->name, raw, n);
    n = strnlen(sep + 1, sizeof(st->url) - 1);
    memcpy(st->url, sep + 1, n);
    return st->url[0] != '\0';
}

static void key_store(int i, const aos_radio_station_t *st)
{
    char key[8], raw[sizeof(st->name) + sizeof(st->url) + 4];
    snprintf(key, sizeof(key), "rad%d", i);
    if (st && st->url[0]) {
        snprintf(raw, sizeof(raw), "%s%c%s", st->name, SEP, st->url);
    } else {
        raw[0] = '\0';
    }
    aos_hal_pref_set_str(key, raw);
}

static void bump_gen(void)
{
    int32_t g = 0;
    aos_hal_pref_get_i32("rad_gen", &g);
    aos_hal_pref_set_i32("rad_gen", g + 1);
}

static bool logo_path(int i, char *out, size_t len)
{
    const char *root = aos_hal_path_sd_root();
    return root && snprintf(out, len, "%s/radio/logo%d.jpg", root, i) < (int)len;
}

static bool has_logo(int i)
{
    char p[96];
    struct stat st;
    return logo_path(i, p, sizeof(p)) && stat(p, &st) == 0 && st.st_size > 0;
}

/* Printable, and never the separator. */
static bool text_ok(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || *s == 0x7f) {
            return false;
        }
    }
    return true;
}

/* ---- JSON --------------------------------------------------------------------- */

static int jstr(char *out, int cap, const char *s)
{
    int o = 0;
    if (o < cap) out[o++] = '"';
    for (; *s && o < cap - 7; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += snprintf(out + o, (size_t)(cap - o), "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    if (o < cap) out[o++] = '"';
    return o;
}

static const char *state_name(aos_radio_state_t s)
{
    switch (s) {
    case AOS_RADIO_CONNECTING: return "connecting";
    case AOS_RADIO_BUFFERING:  return "buffering";
    case AOS_RADIO_PLAYING:    return "playing";
    case AOS_RADIO_RETRYING:   return "retrying";
    case AOS_RADIO_FAILED:     return "failed";
    default:                   return "off";
    }
}

esp_err_t aos_radio_get_handler(httpd_req_t *req)
{
    const int cap = 6144;
    char *j = malloc(cap);
    aos_radio_status_t *rs = malloc(sizeof(*rs));
    if (!j || !rs) {
        free(j);
        free(rs);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }
    int n = snprintf(j, cap, "{\"keys\":[");
    for (int i = 0; i < KEYS; i++) {
        aos_radio_station_t st;
        key_load(i, &st);
        n += snprintf(j + n, cap - n, "%s{\"name\":", i ? "," : "");
        n += jstr(j + n, cap - n, st.name);
        n += snprintf(j + n, cap - n, ",\"url\":");
        n += jstr(j + n, cap - n, st.url);
        n += snprintf(j + n, cap - n, ",\"logo\":%s}", st.url[0] && has_logo(i) ? "true" : "false");
    }
    int32_t gen = 0, art = 1, last = 0;
    aos_hal_pref_get_i32("rad_gen", &gen);
    aos_hal_pref_get_i32("rad_art", &art);
    aos_hal_pref_get_i32("rad_last", &last);
    aos_player_info_t pi;
    aos_hal_player_info(&pi);
    aos_hal_radio_status(rs);
    const char *pst = pi.state == AOS_PLAYER_PLAYING ? "playing"
                    : pi.state == AOS_PLAYER_PAUSED ? "paused" : "stopped";
    n += snprintf(j + n, cap - n,
                  "],\"gen\":%ld,\"art\":%s,\"last\":%ld,\"volume\":%d,\"device\":",
                  (long)gen, art ? "true" : "false", (long)last, aos_hal_volume_get());
    n += jstr(j + n, cap - n, aos_hal_device_name());
    n += snprintf(j + n, cap - n, ",\"player\":{\"state\":\"%s\",\"live\":%s,\"title\":",
                  pst, pi.live ? "true" : "false");
    n += jstr(j + n, cap - n, pi.title);
    n += snprintf(j + n, cap - n, ",\"artist\":");
    n += jstr(j + n, cap - n, pi.artist);
    n += snprintf(j + n, cap - n, "},\"radio\":{\"state\":\"%s\",\"index\":%d,\"station\":",
                  state_name(rs->state), rs->index);
    n += jstr(j + n, cap - n, rs->station);
    n += snprintf(j + n, cap - n, ",\"url\":");
    n += jstr(j + n, cap - n, rs->url);
    n += snprintf(j + n, cap - n, ",\"title\":");
    n += jstr(j + n, cap - n, rs->title);
    n += snprintf(j + n, cap - n, ",\"host\":");
    n += jstr(j + n, cap - n, rs->host);
    n += snprintf(j + n, cap - n, ",\"icy_name\":");
    n += jstr(j + n, cap - n, rs->icy_name);
    n += snprintf(j + n, cap - n, ",\"icy_genre\":");
    n += jstr(j + n, cap - n, rs->icy_genre);
    n += snprintf(j + n, cap - n, ",\"icy_url\":");
    n += jstr(j + n, cap - n, rs->icy_url);
    n += snprintf(j + n, cap - n, ",\"error\":");
    n += jstr(j + n, cap - n, rs->error);
    n += snprintf(j + n, cap - n,
                  ",\"tls\":%s,\"kbps\":%u,\"rate\":%lu,\"channels\":%u,\"buffer_ms\":%lu,"
                  "\"bytes\":%lu,\"reconnects\":%lu,\"listening_s\":%lu}}",
                  rs->tls ? "true" : "false", (unsigned)rs->kbps, (unsigned long)rs->sample_rate,
                  (unsigned)rs->channels, (unsigned long)rs->buffer_ms, (unsigned long)rs->bytes,
                  (unsigned long)rs->reconnects, (unsigned long)rs->listening_s);
    free(rs);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t r = httpd_resp_send(req, j, n < cap ? n : cap - 1);
    free(j);
    return r;
}

/* ---- changes ---------------------------------------------------------------------- */

/* A form field, percent-decoded. */
static bool field(const char *body, const char *key, char *out, size_t len)
{
    char *raw = malloc(len * 3 + 1);
    if (!raw) {
        return false;
    }
    bool ok = httpd_query_key_value(body, key, raw, len * 3 + 1) == ESP_OK;
    if (ok) {
        size_t o = 0;
        for (size_t i = 0; raw[i] && o + 1 < len; i++) {
            if (raw[i] == '%' && raw[i + 1] && raw[i + 2]) {
                char hex[3] = { raw[i + 1], raw[i + 2], 0 };
                out[o++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                out[o++] = raw[i] == '+' ? ' ' : raw[i];
            }
        }
        out[o] = '\0';
    }
    free(raw);
    return ok;
}

static esp_err_t reply(httpd_req_t *req, const char *error)
{
    char out[160];
    if (error) {
        snprintf(out, sizeof(out), "{\"ok\":false,\"error\":\"%s\"}", error);
    } else {
        snprintf(out, sizeof(out), "{\"ok\":true}");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static const char *check_station(const char *name, const char *url)
{
    if (!name[0] || strlen(name) >= sizeof(((aos_radio_station_t *)0)->name) || !text_ok(name)) {
        return "el nombre tiene que tener entre 1 y 47 bytes";
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return "la direccion tiene que empezar con http:// o https://";
    }
    if (strlen(url) >= sizeof(((aos_radio_station_t *)0)->url) || strlen(url) < 11 ||
        !text_ok(url) || strchr(url, ' ')) {
        return "la direccion no es valida";
    }
    return NULL;
}

static void play_keys(int k)
{
    aos_radio_station_t *list = calloc(KEYS, sizeof(aos_radio_station_t));
    if (!list) {
        return;
    }
    for (int i = 0; i < KEYS; i++) {
        key_load(i, &list[i]);
    }
    if (aos_hal_radio_play(list, KEYS, k)) {
        aos_hal_pref_set_i32("rad_last", k);
    }
    free(list);
}

esp_err_t aos_radio_set_handler(httpd_req_t *req)
{
    char body[1024];
    int want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < want) {
        int r = httpd_req_recv(req, body + got, want - got);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
            return ESP_FAIL;
        }
        got += r;
    }
    body[got] = '\0';

    char what[12] = "", num[8] = "-1";
    field(body, "do", what, sizeof(what));
    field(body, "k", num, sizeof(num));
    int k = atoi(num);
    bool key_ok = k >= 0 && k < KEYS;

    if (strcmp(what, "set") == 0 || strcmp(what, "test") == 0) {
        aos_radio_station_t st;
        memset(&st, 0, sizeof(st));
        char name[96] = "", url[300] = "";
        field(body, "name", name, sizeof(name));
        field(body, "url", url, sizeof(url));
        const char *err = check_station(name, url);
        if (err) {
            return reply(req, err);
        }
        memcpy(st.name, name, strlen(name));
        memcpy(st.url, url, strlen(url));
        if (what[0] == 't') {
            aos_hal_radio_play(&st, 1, 0);
            return reply(req, NULL);
        }
        if (!key_ok) {
            return reply(req, "no existe esa tecla");
        }
        key_store(k, &st);
        bump_gen();
        ESP_LOGI(TAG, "key %d: %s -> %s", k + 1, st.name, st.url);
        return reply(req, NULL);
    }
    if (strcmp(what, "clear") == 0) {
        if (!key_ok) {
            return reply(req, "no existe esa tecla");
        }
        key_store(k, NULL);
        char p[96];
        if (logo_path(k, p, sizeof(p))) {
            remove(p);
        }
        bump_gen();
        return reply(req, NULL);
    }
    if (strcmp(what, "swap") == 0) {
        char a_s[8] = "-1", b_s[8] = "-1";
        field(body, "a", a_s, sizeof(a_s));
        field(body, "b", b_s, sizeof(b_s));
        int a = atoi(a_s), b = atoi(b_s);
        if (a < 0 || a >= KEYS || b < 0 || b >= KEYS || a == b) {
            return reply(req, "teclas invalidas");
        }
        aos_radio_station_t sa, sb;
        key_load(a, &sa);
        key_load(b, &sb);
        key_store(a, &sb);
        key_store(b, &sa);
        char pa[96], pb[96], pt[100];
        if (logo_path(a, pa, sizeof(pa)) && logo_path(b, pb, sizeof(pb))) {
            snprintf(pt, sizeof(pt), "%s.tmp", pa);
            remove(pt);
            rename(pa, pt);             /* any of the three may be missing: fine */
            rename(pb, pa);
            rename(pt, pb);
        }
        bump_gen();
        return reply(req, NULL);
    }
    if (strcmp(what, "play") == 0) {
        aos_radio_station_t st;
        if (!key_ok || !key_load(k, &st)) {
            return reply(req, "esa tecla esta vacia");
        }
        play_keys(k);
        return reply(req, NULL);
    }
    if (strcmp(what, "vol") == 0) {
        char v[8] = "";
        field(body, "v", v, sizeof(v));
        int vol = atoi(v);
        aos_hal_volume_set(vol < 0 ? 0 : vol > 100 ? 100 : vol);
        return reply(req, NULL);
    }
    if (strcmp(what, "art") == 0) {
        char on[4] = "1";
        field(body, "on", on, sizeof(on));
        aos_hal_pref_set_i32("rad_art", on[0] == '1' ? 1 : 0);
        return reply(req, NULL);
    }
    if (strcmp(what, "pause") == 0) {
        aos_hal_player_pause();
    } else if (strcmp(what, "resume") == 0) {
        aos_hal_player_resume();
    } else if (strcmp(what, "stop") == 0) {
        aos_hal_player_stop();
    } else if (strcmp(what, "next") == 0) {
        aos_hal_player_next();
    } else if (strcmp(what, "prev") == 0) {
        aos_hal_player_prev();
    } else {
        return reply(req, "accion desconocida");
    }
    return reply(req, NULL);
}
