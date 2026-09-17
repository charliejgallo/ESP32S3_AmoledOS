/*
 * AmoledOS - /api/jpegbench: how fast esp_new_jpeg decodes on this board.
 *
 *   GET /api/jpegbench?file=<name in the photos folder>&n=<iterations>&scale=1
 *
 * Reads the JPEG into PSRAM once, then decodes it 'n' times (default 5) to a
 * 16-byte-aligned RGB565 buffer in PSRAM, timing header parse + decode as one
 * unit because that is what a video frame costs. With scale=1 the decoder
 * shrinks to half size (its own 1/2 scaler, multiples of 8), which is the
 * plan B of the video player: decode small and pixel-double.
 *
 * It also reports what the decoder took from internal RAM while open, which
 * is the number that decides whether a video app can afford it (see
 * docs/RAM-AUDIT.md: internal RAM is the scarce resource).
 *
 * Measurement code, in the spirit of /api/mem?spin=N: it stays because the
 * next decoder question will want the same numbers again.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_jpeg_dec.h"

#include "aos_hal.h"

#define BENCH_MAX_ITER   50
#define BENCH_MAX_BYTES  (1024 * 1024)

static bool query_int(httpd_req_t *req, const char *key, int *out)
{
    char query[160];
    char value[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return false;
    }
    *out = atoi(value);
    return true;
}

static bool query_str(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, len) == ESP_OK;
}

esp_err_t aos_jpegbench_handler(httpd_req_t *req)
{
    char name[64];
    if (!query_str(req, "file", name, sizeof(name)) || strchr(name, '/')) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"file=<name in photos>\"}");
        return ESP_OK;
    }
    int n = 5;
    query_int(req, "n", &n);
    if (n < 1) n = 1;
    if (n > BENCH_MAX_ITER) n = BENCH_MAX_ITER;
    int scale = 0;
    query_int(req, "scale", &scale);

    char path[192];
    snprintf(path, sizeof(path), "%s/%s", aos_hal_path_photos(), name);

    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "{\"error\":\"no such file\"}");
        return ESP_OK;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0 || size > BENCH_MAX_BYTES) {
        fclose(f);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "{\"error\":\"file too large\"}");
        return ESP_OK;
    }
    /* caps=int reads into internal RAM instead of PSRAM: the SDMMC driver
     * cannot DMA into PSRAM and goes through a bounce buffer, and read_ms
     * with the two placements says what that costs. */
    char caps[8] = "";
    query_str(req, "caps", caps, sizeof(caps));
    uint32_t data_caps = strcmp(caps, "int") == 0 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
    uint8_t *data = heap_caps_malloc((size_t)size, data_caps);
    if (!data) {
        fclose(f);
        httpd_resp_sendstr(req, "{\"error\":\"no psram\"}");
        return ESP_OK;
    }
    int64_t t_read0 = esp_timer_get_time();
    size_t got = fread(data, 1, (size_t)size, f);
    int64_t t_read = esp_timer_get_time() - t_read0;
    fclose(f);

    size_t int_before   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t dec = NULL;
    jpeg_error_t err = jpeg_dec_open(&cfg, &dec);
    size_t int_open   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_open = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    jpeg_dec_io_t io = {0};
    jpeg_dec_header_info_t info = {0};
    int outlen = 0;
    uint8_t *outbuf = NULL;
    int64_t us[BENCH_MAX_ITER];
    int done = 0;
    jpeg_error_t err_proc = JPEG_ERR_OK;
    size_t int_min = int_open;

    if (err == JPEG_ERR_OK) {
        io.inbuf     = data;
        io.inbuf_len = (int)got;
        err = jpeg_dec_parse_header(dec, &io, &info);
    }
    if (err == JPEG_ERR_OK && scale) {
        /* Half size, rounded down to the decoder's multiples of 8. The
         * handle has to be reopened for the scaler to take the new size. */
        jpeg_dec_close(dec);
        dec = NULL;
        cfg.scale.width  = (info.width  / 2) & ~7;
        cfg.scale.height = (info.height / 2) & ~7;
        err = jpeg_dec_open(&cfg, &dec);
        if (err == JPEG_ERR_OK) {
            io.inbuf     = data;
            io.inbuf_len = (int)got;
            err = jpeg_dec_parse_header(dec, &io, &info);
        }
    }
    if (err == JPEG_ERR_OK) {
        err = jpeg_dec_get_outbuf_len(dec, &outlen);
    }
    if (err == JPEG_ERR_OK) {
        outbuf = heap_caps_aligned_alloc(16, (size_t)outlen, MALLOC_CAP_SPIRAM);
        if (!outbuf) {
            err = JPEG_ERR_NO_MEM;
        }
    }
    if (err == JPEG_ERR_OK) {
        for (int i = 0; i < n; i++) {
            io.inbuf        = data;
            io.inbuf_len    = (int)got;
            io.inbuf_remain = (int)got;
            io.outbuf       = outbuf;
            int64_t t0 = esp_timer_get_time();
            err_proc = jpeg_dec_parse_header(dec, &io, &info);
            if (err_proc == JPEG_ERR_OK) {
                err_proc = jpeg_dec_process(dec, &io);
            }
            us[i] = esp_timer_get_time() - t0;
            size_t now_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (now_int < int_min) int_min = now_int;
            if (err_proc != JPEG_ERR_OK) {
                break;
            }
            done++;
        }
    }

    size_t int_after   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    /* A fingerprint of the output so a "fast" decode that wrote nothing
     * cannot pass for a result: a few pixels from the middle rows. */
    uint32_t fp = 0;
    if (outbuf && done) {
        const uint16_t *px = (const uint16_t *)outbuf;
        int w = scale ? (int)cfg.scale.width : info.width;
        int h = scale ? (int)cfg.scale.height : info.height;
        for (int y = 0; y < h; y += 37) {
            for (int x = 0; x < w; x += 41) {
                fp = fp * 31u + px[y * w + x];
            }
        }
    }

    if (dec) {
        jpeg_dec_close(dec);
    }
    size_t int_closed = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    int64_t sum = 0, mn = 0, mx = 0;
    for (int i = 0; i < done; i++) {
        sum += us[i];
        if (i == 0 || us[i] < mn) mn = us[i];
        if (i == 0 || us[i] > mx) mx = us[i];
    }

    char out[1200];
    int len = snprintf(out, sizeof(out),
        "{\"file\":\"%s\",\"bytes\":%u,\"read_ms\":%.1f,\"read_caps\":\"%s\","
        "\"width\":%u,\"height\":%u,\"out_w\":%u,\"out_h\":%u,\"outbuf\":%d,"
        "\"open_err\":%d,\"proc_err\":%d,\"iterations\":%d,"
        "\"avg_ms\":%.1f,\"min_ms\":%.1f,\"max_ms\":%.1f,"
        "\"internal_open_kb\":%.1f,\"internal_peak_kb\":%.1f,\"internal_leak_b\":%d,"
        "\"psram_open_kb\":%.1f,\"psram_delta_after_b\":%d,"
        "\"fingerprint\":%" PRIu32 ",\"times_ms\":[",
        name, (unsigned)got, t_read / 1000.0, data_caps == MALLOC_CAP_INTERNAL ? "internal" : "psram",
        info.width, info.height,
        scale ? (unsigned)cfg.scale.width : info.width,
        scale ? (unsigned)cfg.scale.height : info.height, outlen,
        (int)err, (int)err_proc, done,
        done ? (sum / 1000.0) / done : 0.0, mn / 1000.0, mx / 1000.0,
        (int_before - int_open) / 1024.0, (int_before - int_min) / 1024.0,
        (int)int_before - (int)int_closed,
        (psram_before - psram_open) / 1024.0,
        (int)psram_before - (int)psram_after,
        fp);
    for (int i = 0; i < done && len < (int)sizeof(out) - 16; i++) {
        len += snprintf(out + len, sizeof(out) - len, "%s%.1f",
                        i ? "," : "", us[i] / 1000.0);
    }
    len += snprintf(out + len, sizeof(out) - len, "]}");
    (void)int_after;

    if (outbuf) heap_caps_free(outbuf);
    heap_caps_free(data);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, len);
    return ESP_OK;
}
