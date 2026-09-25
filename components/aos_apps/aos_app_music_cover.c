/*
 * AmoledOS - the Music app's cover.
 *
 * Where it comes from, in order: the picture inside the MP3 (ID3 APIC, whose
 * offset aos_audio found while reading the tags), or a cover.jpg, folder.jpg
 * or front.jpg beside the track, any case. JPEG, baseline: esp_new_jpeg
 * reads 4:2:0, 4:2:2 and 4:4:4 (covers of 96, 600 and 1200 px, measured) but
 * not progressive, and an embedded cover it cannot read falls back to the
 * folder's; with neither, the note stays.
 *
 * Decoded off the LVGL task, by a task of its own that lives for one cover:
 * reading 50-500 KB from the card and decoding take tens to hundreds of ms,
 * and the screen would stop for that long on every new track (the card
 * reads ~470 KB/s: a 600 KB cover is 1.3 s of reading alone).
 *
 * The app asks with music_cover_request() whenever the track changes and
 * collects with music_cover_take() from its timer. The same source twice (a
 * folder's cover.jpg, track after track) is not decoded again.
 */
#include "aos_app_music_cover.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef AOS_SIM
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define COVER_MAX_BYTES     (2 * 1024 * 1024)   /* a cover over 2 MB is not one */

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* the request (written by the app, read by the task) */
static char     s_req_track[256];
static uint32_t s_req_off, s_req_size;
static uint32_t s_req_gen;
static bool     s_busy;

/* the result (written by the task, taken by the app) */
static uint16_t *s_res_px;
static char      s_res_key[300];
static uint32_t  s_res_gen;
static bool      s_res_ready;

/* The cover on show and what it is of. Kept here, not in the app: closing
 * and opening the app, or the next track of the same folder, finds it
 * decoded already. */
static uint16_t *s_cur_px;
static char      s_cur_key[300];

static uint8_t *read_range(const char *path, uint32_t off, uint32_t size)
{
    if (size < 64 || size > COVER_MAX_BYTES) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    bool ok = buf && fseek(f, (long)off, SEEK_SET) == 0 && fread(buf, 1, size, f) == size;
    fclose(f);
    if (!ok) {
        free(buf);
        return NULL;
    }
    return buf;
}

static uint16_t *cover_alloc(void)
{
    return heap_caps_malloc(COVER_PX * COVER_PX * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
}

/* Any size to the square, nearest neighbour: esp_new_jpeg leaves it at most
 * 1.3x off (or smaller than the square), and for a 112 px thumbnail nearest
 * is fine. */
static uint16_t *resample(const uint16_t *src, int sw, int sh)
{
    uint16_t *out = cover_alloc();
    if (!out) {
        return NULL;
    }
    for (int y = 0; y < COVER_PX; y++) {
        const uint16_t *row = src + (y * sh / COVER_PX) * sw;
        for (int x = 0; x < COVER_PX; x++) {
            out[y * COVER_PX + x] = row[x * sw / COVER_PX];
        }
    }
    return out;
}

static int up8(int v)
{
    return (v + 7) & ~7;
}

/* esp_new_jpeg: fast (SIMD), scales while decoding, but baseline only, and
 * never below 1/8 of the original (a 1200 px cover gives
 * 150 at the least, measured: "scaled width should be greater than the
 * minimum 1/8 scale width"). */
static uint16_t *decode_esp(const uint8_t *data, uint32_t len)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = NULL;
    jpeg_dec_io_t io = {0};
    jpeg_dec_header_info_t info = {0};
    uint16_t *out = NULL;
    uint8_t *raw = NULL;

    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        return NULL;
    }
    io.inbuf = (uint8_t *)data;
    io.inbuf_len = (int)len;
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK || !info.width || !info.height) {
        goto done;
    }
    int sw = info.width, sh = info.height;
    if (info.width >= COVER_PX && info.height >= COVER_PX) {
        sw = up8((info.width + 7) / 8);
        sh = up8((info.height + 7) / 8);
        sw = sw > COVER_PX ? sw : COVER_PX;
        sh = sh > COVER_PX ? sh : COVER_PX;
        /* the scaler is set at open: reopen with the size wanted */
        jpeg_dec_close(dec);
        dec = NULL;
        cfg.scale.width = (uint16_t)sw;
        cfg.scale.height = (uint16_t)sh;
        if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
            goto done;
        }
        io.inbuf = (uint8_t *)data;
        io.inbuf_len = (int)len;
        if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) {
            goto done;
        }
    }
    int outlen = 0;
    if (jpeg_dec_get_outbuf_len(dec, &outlen) != JPEG_ERR_OK || outlen < sw * sh * 2) {
        goto done;
    }
    raw = heap_caps_aligned_alloc(16, (size_t)outlen, MALLOC_CAP_SPIRAM);
    if (!raw) {
        goto done;
    }
    io.inbuf = (uint8_t *)data;
    io.inbuf_len = (int)len;
    io.inbuf_remain = (int)len;
    io.outbuf = raw;
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK ||
        jpeg_dec_process(dec, &io) != JPEG_ERR_OK) {
        goto done;
    }
    if (sw == COVER_PX && sh == COVER_PX) {
        out = (uint16_t *)raw;                  /* already the square */
        raw = NULL;
    } else {
        out = resample((const uint16_t *)raw, sw, sh);
    }
done:
    free(raw);
    if (dec) {
        jpeg_dec_close(dec);
    }
    return out;
}


/* A cover.jpg (folder.jpg, front.jpg, any case) beside the track. */
static bool folder_cover(const char *track, char *out, size_t len)
{
    static const char *const names[] = { "cover.jpg", "folder.jpg", "front.jpg",
                                         "cover.jpeg", "folder.jpeg" };
    const char *slash = strrchr(track, '/');
    if (!slash) {
        return false;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "%.*s", (int)(slash - track), track);
    DIR *d = opendir(dir);
    if (!d) {
        return false;
    }
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strcasecmp(e->d_name, names[i]) == 0) {
                found = snprintf(out, len, "%s/%s", dir, e->d_name) < (int)len;
                break;
            }
        }
    }
    closedir(d);
    return found;
}

static void cover_task(void *arg)
{
    (void)arg;
    for (;;) {
        char track[256];
        uint32_t off, size, gen;
        portENTER_CRITICAL(&s_mux);
        memcpy(track, s_req_track, sizeof(track));
        off = s_req_off;
        size = s_req_size;
        gen = s_req_gen;
        portEXIT_CRITICAL(&s_mux);

        /* which picture, and is it the one already on show */
        char src[256] = "", key[300];
        uint32_t src_off = 0, src_size = 0;
        if (off && size) {
            snprintf(src, sizeof(src), "%s", track);
            src_off = off;
            src_size = size;
        } else if (folder_cover(track, src, sizeof(src))) {
            FILE *f = fopen(src, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                long n = ftell(f);
                fclose(f);
                src_size = n > 0 ? (uint32_t)n : 0;
            }
        }
        snprintf(key, sizeof(key), "%s@%u", src, (unsigned)src_off);
        uint16_t *px = NULL;
        bool same = false;
        for (int attempt = 0; attempt < 2 && !px && src[0]; attempt++) {
            snprintf(key, sizeof(key), "%s@%u", src, (unsigned)src_off);
            portENTER_CRITICAL(&s_mux);
            same = s_cur_px && strcmp(key, s_cur_key) == 0;
            portEXIT_CRITICAL(&s_mux);
            if (same) {
                break;
            }
            int64_t t0 = esp_timer_get_time();
            uint8_t *data = read_range(src, src_off, src_size);
            int64_t t1 = esp_timer_get_time();
            if (data) {
                px = decode_esp(data, src_size);
                free(data);
            }
            ESP_LOGI("music", "cover %s (%u B): %s, read %lld ms, decode %lld ms",
                     strrchr(src, '/') ? strrchr(src, '/') + 1 : src, (unsigned)src_size,
                     px ? "ok" : "unreadable", (t1 - t0) / 1000,
                     (esp_timer_get_time() - t1) / 1000);
            /* an embedded picture it cannot read (progressive JPEG, PNG):
             * the folder's cover, if there is one */
            if (!px && src_off && folder_cover(track, src, sizeof(src))) {
                src_off = 0;
                FILE *f = fopen(src, "rb");
                src_size = 0;
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long n = ftell(f);
                    fclose(f);
                    src_size = n > 0 ? (uint32_t)n : 0;
                }
            } else {
                break;
            }
        }

        uint16_t *untaken = NULL;
        portENTER_CRITICAL(&s_mux);
        bool stale = gen != s_req_gen;          /* asked again meanwhile */
        if (!stale) {
            if (s_res_px != s_cur_px) {
                untaken = s_res_px;             /* one nobody took */
            }
            s_res_px = same ? s_cur_px : px;
            px = NULL;
            snprintf(s_res_key, sizeof(s_res_key), "%s", s_res_px ? key : "");
            s_res_gen = gen;
            s_res_ready = true;
            s_busy = false;
        }
        portEXIT_CRITICAL(&s_mux);
        free(untaken);                          /* the heap is not for critical sections */
        free(px);
        if (!stale) {
            break;
        }
    }
    vTaskDelete(NULL);
}

void music_cover_request(const char *track, uint32_t cover_offset, uint32_t cover_size)
{
    bool start;
    portENTER_CRITICAL(&s_mux);
    snprintf(s_req_track, sizeof(s_req_track), "%s", track ? track : "");
    s_req_off = cover_offset;
    s_req_size = cover_size;
    s_req_gen++;
    start = !s_busy;
    s_busy = true;
    portEXIT_CRITICAL(&s_mux);
    /* 6 KB: the FAT path of an open and the decoder's own calls; internal,
     * because the file may be on SPIFFS */
    if (start && xTaskCreate(cover_task, "aos_cover", 6144, NULL, 3, NULL) != pdPASS) {
        portENTER_CRITICAL(&s_mux);
        s_busy = false;
        portEXIT_CRITICAL(&s_mux);
    }
}

bool music_cover_take(const uint16_t **px, uint16_t **old)
{
    bool ready;
    *old = NULL;
    portENTER_CRITICAL(&s_mux);
    ready = s_res_ready && s_res_gen == s_req_gen;
    if (ready) {
        if (s_res_px != s_cur_px) {
            *old = s_cur_px;                    /* free it once off the screen */
            s_cur_px = s_res_px;
            memcpy(s_cur_key, s_res_key, sizeof(s_cur_key));
        }
        *px = s_cur_px;
        s_res_px = NULL;
        s_res_ready = false;
    }
    portEXIT_CRITICAL(&s_mux);
    return ready;
}

#else   /* the simulator: no esp_new_jpeg, so the note stays */

void music_cover_request(const char *track, uint32_t cover_offset, uint32_t cover_size)
{
    (void)track; (void)cover_offset; (void)cover_size;
}

bool music_cover_take(const uint16_t **px, uint16_t **old)
{
    (void)px;
    *old = NULL;
    return false;
}

#endif
