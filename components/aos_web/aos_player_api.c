/*
 * AmoledOS - /api/player: the local player from the Mac.
 *
 *   GET /api/player                              state + pipeline figures
 *   GET /api/player?do=play&dir=music/x&i=3      the 4th track of a folder, and on
 *   GET /api/player?do=play&file=music/x/y.mp3   one file, then the rest of its folder
 *   GET /api/player?do=pause|resume|stop|next|prev|resume_last
 *   GET /api/player?do=seek&ms=200000
 *   GET /api/player?do=shuffle&on=1
 *   GET /api/player?do=volume&v=40
 *
 * Paths are relative to the card's root. The figures are what the MP3 work
 * was measured with (ring, underruns, decoder load, stacks, RAM), and like
 * /api/link it stays for the next question about the same thing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"

#include "aos_hal.h"
#include "../aos_hal/aos_audio.h"

static char s_query[512];

static bool arg(const char *key, char *out, size_t len)
{
    char raw[256];
    if (httpd_query_key_value(s_query, key, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    /* percent-decoding: the names carry spaces, accents and '&' */
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
    return true;
}

/* A path under the card's root, refusing any way out of it. */
static bool card_path(const char *rel, char *out, size_t len)
{
    const char *root = aos_hal_path_sd_root();
    if (!root || !rel[0] || strstr(rel, "..")) {
        return false;
    }
    while (*rel == '/') {
        rel++;
    }
    return snprintf(out, len, "%s/%s", root, rel) < (int)len;
}

static void json_str(char *out, size_t len, const char *s)
{
    size_t o = 0;
    for (; *s && o + 7 < len; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, len - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static const char *state_name(aos_player_state_t s)
{
    return s == AOS_PLAYER_PLAYING ? "playing" : s == AOS_PLAYER_PAUSED ? "paused" : "stopped";
}

esp_err_t aos_player_handler(httpd_req_t *req)
{
    s_query[0] = '\0';
    httpd_req_get_url_query_str(req, s_query, sizeof(s_query));

    char what[16] = "", result[96] = "";
    if (arg("do", what, sizeof(what))) {
        char rel[256], path[300], v[16];
        if (!strcmp(what, "play")) {
            bool ok = false;
            if (arg("file", rel, sizeof(rel)) && card_path(rel, path, sizeof(path))) {
                ok = aos_hal_player_play_folder(path);
            } else if (arg("dir", rel, sizeof(rel)) && card_path(rel, path, sizeof(path))) {
                int i = arg("i", v, sizeof(v)) ? atoi(v) : 0;
                aos_audio_list_t list = {0};
                if (aos_audio_list_scan(&list, path, 512) && i >= 0 && i < list.count) {
                    char full[560];
                    if (snprintf(full, sizeof(full), "%s/%s", path,
                                 aos_audio_list_name(&list, i)) < (int)sizeof(full)) {
                        ok = aos_hal_player_play_folder(full);
                    }
                }
                aos_audio_list_free(&list);
            }
            snprintf(result, sizeof(result), "%s", ok ? "playing" : "play failed");
        } else if (!strcmp(what, "resume_last")) {
            snprintf(result, sizeof(result), "%s",
                     aos_hal_player_resume_last() ? "playing" : "nothing to resume");
        } else if (!strcmp(what, "pause")) {
            aos_hal_player_pause();
        } else if (!strcmp(what, "resume")) {
            aos_hal_player_resume();
        } else if (!strcmp(what, "stop")) {
            aos_hal_player_stop();
        } else if (!strcmp(what, "next")) {
            aos_hal_player_next();
        } else if (!strcmp(what, "prev")) {
            aos_hal_player_prev();
        } else if (!strcmp(what, "seek") && arg("ms", v, sizeof(v))) {
            aos_hal_player_seek((uint32_t)atol(v));
        } else if (!strcmp(what, "shuffle") && arg("on", v, sizeof(v))) {
            aos_hal_player_set_shuffle(atoi(v) != 0);
        } else if (!strcmp(what, "volume") && arg("v", v, sizeof(v))) {
            aos_hal_volume_set(atoi(v));
        } else {
            snprintf(result, sizeof(result), "unknown");
        }
        if (!result[0]) {
            snprintf(result, sizeof(result), "%s", what);
        }
    }

    aos_player_info_t in;
    aos_player_stats_t st;
    aos_hal_player_info(&in);
    aos_hal_player_stats(&st);

    char path[400], title[200], artist[200], album[140];
    json_str(path, sizeof(path), in.path);
    json_str(title, sizeof(title), in.title);
    json_str(artist, sizeof(artist), in.artist);
    json_str(album, sizeof(album), in.album);

    char last[256] = "", last_js[400];
    uint32_t last_pos = 0;
    aos_hal_player_last(last, sizeof(last), &last_pos);
    json_str(last_js, sizeof(last_js), last);

    char *out = malloc(2560);
    if (!out) {
        return httpd_resp_send_500(req);
    }
    snprintf(out, 2560,
             "{\"result\":\"%s\",\"state\":\"%s\",\"path\":\"%s\",\"title\":\"%s\","
             "\"artist\":\"%s\",\"album\":\"%s\",\"format\":\"%s\",\"kbps\":%u,\"vbr\":%s,"
             "\"rate\":%u,\"channels\":%u,\"duration_ms\":%u,\"position_ms\":%u,"
             "\"index\":%d,\"count\":%d,\"shuffle\":%s,\"yielded\":%s,\"volume\":%d,"
             "\"ring_ms\":%u,\"ring_cap_ms\":%u,\"ring_min_ms\":%u,\"underruns\":%u,"
             "\"load_permille\":%u,\"decode_permille\":%u,\"chunk_us_max\":%u,"
             "\"decoder_prio\":%d,\"stack_free_dec\":%u,\"stack_free_out\":%u,"
             "\"internal_free\":%u,\"internal_min\":%u,\"psram_free\":%u,"
             "\"last_path\":\"%s\",\"last_pos_ms\":%u}",
             result, state_name(in.state), path, title, artist, album,
             in.format ? in.format : "", in.kbps, in.vbr ? "true" : "false",
             (unsigned)in.sample_rate, in.channels, (unsigned)in.duration_ms,
             (unsigned)in.position_ms, in.index, in.count,
             in.shuffle ? "true" : "false", in.yielded ? "true" : "false",
             aos_hal_volume_get(),
             (unsigned)st.ring_ms, (unsigned)st.ring_cap_ms, (unsigned)st.ring_min_ms,
             (unsigned)st.underruns, (unsigned)st.load_permille, (unsigned)st.decode_permille,
             (unsigned)st.chunk_us_max, st.decoder_prio,
             (unsigned)st.stack_free_dec, (unsigned)st.stack_free_out,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), last_js, (unsigned)last_pos);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_sendstr(req, out);
    free(out);
    return e;
}
