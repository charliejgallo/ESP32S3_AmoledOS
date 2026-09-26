/*
 * AmoledOS - /api/h264bench: how fast esp_h264 decodes on this board.
 *
 *   GET /api/h264bench?file=<name in videos>&dual=0|1&core=0|1&prio=5
 *                     &n=<max frames>&conv=0|1&rate=<fps>
 *
 * Branch rtsp. The camera viewer lives or dies by one number -milliseconds
 * per frame of the software decoder at the camera's resolution- and this is
 * where it is measured, before a line of RTSP gets written. The clip is a
 * raw Annex-B .h264 recorded from the camera itself (ffmpeg -c copy), so the
 * bitstream is the camera's, not a re-encode.
 *
 * The whole clip goes into PSRAM, then a task pinned to 'core' at 'prio'
 * (the priority a viewer's worker would have) feeds it to tinyh264 one NAL
 * at a time and times every call. The time of a picture is the sum of the
 * calls that built it, so I and P frames come out apart: a keyframe is the
 * worst case the viewer has to absorb.
 *
 * dual=1 asks tinyh264 for its second task on the other core. Espressif
 * gates that behind a Kconfig (ESP_H264_DUAL_TASK); calling h264bsdAlloc()
 * ourselves makes it a runtime choice, so both come out of one firmware.
 *
 * conv=1 also times what the viewer does to every frame after decoding it:
 * YUV 4:2:0 to RGB565 big-endian, scaled to the panel's width.
 *
 * rate=<fps> paces the feed like a live camera: a frame is only fed when
 * its time has come, and one that arrives while the decoder is still busy
 * waits. The answer is the lag at the end, which says if the decoder keeps
 * up (lag stays near zero) or not (it grows by the deficit every second).
 *
 * Measurement code, like /api/jpegbench: it stays because the next camera
 * will want the same numbers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "aos_hal.h"

/* tinyh264's own interface. esp_h264 keeps the header private and only lets
 * its wrapper pick the dual task at build time; these match
 * sw/libs/tinyh264_inc/h264bsd_decoder.h of esp_h264 1.4.1. */
enum {
    H264BSD_RDY,
    H264BSD_PIC_RDY,
    H264BSD_HDRS_RDY,
    H264BSD_ERROR,
    H264BSD_PARAM_SET_ERROR,
    H264BSD_MEMALLOC_ERROR
};
typedef void *h264bsd_hd_t;
typedef struct {
    uint32_t dualTaskEnable;
    uint32_t dualTaskCore;
    uint32_t dualTaskPriority;
} h264bsd_cfg_t;
uint32_t     h264bsdDecode(h264bsd_hd_t hd, uint8_t *byteStrm, uint32_t *len,
                           uint8_t **picture, uint32_t *width, uint32_t *height);
void         h264bsdShutdown(h264bsd_hd_t hd);
h264bsd_hd_t h264bsdAlloc(h264bsd_cfg_t *cfg);
void         h264bsdFree(h264bsd_hd_t hd);
const char  *esp_tinyh264_get_version(void);

#define BENCH_MAX_BYTES   (4 * 1024 * 1024)
#define BENCH_MAX_FRAMES  600
#define PANEL_W           368

/* ---------------------------------------------------------------------- */
/* YUV 4:2:0 (I420) to RGB565 big-endian, nearest neighbour, to 'ow' wide. */
/* Full-range BT.601: the cameras send yuvj420p. This is the viewer's      */
/* converter; it lives here until the app exists to measure it first.      */
/* ---------------------------------------------------------------------- */

static uint8_t s_clamp[256 + 512];

static void conv_init(void)
{
    for (int i = 0; i < (int)sizeof(s_clamp); i++) {
        int v = i - 256;
        s_clamp[i] = v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
    }
}

static void i420_to_rgb565be(const uint8_t *yuv, int w, int h,
                             uint16_t *out, int ow, int oh, uint16_t *xmap)
{
    const uint8_t *py = yuv;
    const uint8_t *pu = yuv + w * h;
    const uint8_t *pv = pu + (w / 2) * (h / 2);
    const uint8_t *cl = s_clamp + 256;

    for (int x = 0; x < ow; x++) {
        xmap[x] = (uint16_t)((x * w + w / 2) / ow);
    }
    for (int y = 0; y < oh; y++) {
        int sy = (y * h + h / 2) / oh;
        const uint8_t *ry = py + sy * w;
        const uint8_t *ru = pu + (sy / 2) * (w / 2);
        const uint8_t *rv = pv + (sy / 2) * (w / 2);
        uint16_t *o = out + y * ow;
        for (int x = 0; x < ow; x++) {
            int sx = xmap[x];
            int Y = ry[sx];
            int U = ru[sx >> 1] - 128;
            int V = rv[sx >> 1] - 128;
            int r = cl[Y + ((359 * V) >> 8)];
            int g = cl[Y - ((88 * U + 183 * V) >> 8)];
            int b = cl[Y + ((454 * U) >> 8)];
            uint16_t px = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            o[x] = (uint16_t)((px >> 8) | (px << 8));
        }
    }
}

/* ---------------------------------------------------------------------- */

typedef struct {
    /* in */
    uint8_t  *data;
    size_t    size;
    int       dual;
    int       core;
    int       prio;
    int       max_frames;
    int       conv;
    int       rate;
    SemaphoreHandle_t done;
    /* out */
    int       frames, iframes, pframes, errors, nals;
    uint32_t  width, height;
    int64_t   i_sum, i_max, p_sum, p_max, p_min;
    int64_t   conv_sum;
    int64_t   wall_us;
    int64_t   lag_end_us, lag_max_us;
    uint32_t  frame_us[BENCH_MAX_FRAMES];
    uint8_t   frame_type[BENCH_MAX_FRAMES];
    size_t    int_before, int_alloc, int_min, psram_before, psram_alloc, psram_run;
    int       int_leak;
    int       out_w, out_h;
    uint32_t  fingerprint;
    bool      alloc_failed;
} bench_t;

/* Next Annex-B start code at or after p; returns end if none. */
static const uint8_t *next_start(const uint8_t *p, const uint8_t *end)
{
    for (; p + 3 <= end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1) {
            return p;
        }
    }
    return end;
}

static void bench_task(void *arg)
{
    bench_t *b = arg;
    conv_init();

    b->int_before   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    b->psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    b->int_min      = b->int_before;

    h264bsd_cfg_t cfg = {
        .dualTaskEnable   = b->dual ? 1 : 0,
        .dualTaskCore     = b->core ? 0 : 1,
        .dualTaskPriority = (uint32_t)b->prio,
    };
    h264bsd_hd_t hd = h264bsdAlloc(&cfg);
    b->int_alloc   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    b->psram_alloc = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (!hd) {
        b->alloc_failed = true;
        xSemaphoreGive(b->done);
        vTaskDelete(NULL);
        return;
    }

    uint16_t *rgb  = NULL;
    uint16_t *xmap = NULL;

    const uint8_t *p   = b->data;
    const uint8_t *end = b->data + b->size;
    int64_t frame_acc = 0;
    int     frame_nal = -1;
    /* tinyh264 writes the size only when it activates a parameter set, not
     * on every picture (esp_h264's wrapper keeps them in its handle too). */
    uint32_t w = 0, h = 0;
    int64_t t_start = esp_timer_get_time();

    while (p < end && b->frames < b->max_frames) {
        /* One NAL per call: the decoder would take the whole buffer and
         * stop at the next start code anyway, but slicing it here tells us
         * which NAL type built the picture. */
        const uint8_t *s = next_start(p, end);
        if (s == end) break;
        const uint8_t *nal = s + 3;
        const uint8_t *n2  = next_start(nal, end);
        /* A 4-byte start code belongs to the next NAL: trim its zero. */
        const uint8_t *nal_end = n2;
        if (n2 < end && n2 > nal && n2[-1] == 0) nal_end = n2 - 1;
        int type = nal[0] & 0x1F;
        b->nals++;

        /* Live pacing: a picture's first slice waits for its turn. */
        if (b->rate > 0 && (type == 1 || type == 5) && frame_acc == 0) {
            int64_t due = t_start + (int64_t)b->frames * 1000000 / b->rate;
            int64_t now = esp_timer_get_time();
            if (now < due) {
                vTaskDelay(pdMS_TO_TICKS((due - now) / 1000));
            } else {
                int64_t lag = now - due;
                if (lag > b->lag_max_us) b->lag_max_us = lag;
                b->lag_end_us = lag;
            }
        }

        uint32_t len = (uint32_t)(nal_end - s);
        uint32_t left = len;
        uint8_t *pic = NULL;
        int64_t t0 = esp_timer_get_time();
        uint32_t rc = h264bsdDecode(hd, (uint8_t *)s, &left, &pic, &w, &h);
        int64_t dt = esp_timer_get_time() - t0;
        p = nal_end;

        if (type == 1 || type == 5) {
            frame_acc += dt;
            frame_nal = type;
        }
        if (rc == H264BSD_ERROR || rc == H264BSD_PARAM_SET_ERROR || rc == H264BSD_MEMALLOC_ERROR) {
            b->errors++;
            if (rc == H264BSD_MEMALLOC_ERROR) break;
            continue;
        }
        if (rc != H264BSD_PIC_RDY || w == 0 || h == 0) {
            continue;
        }

        /* A picture. */
        int i = b->frames++;
        b->width = w;
        b->height = h;
        if (i < BENCH_MAX_FRAMES) {
            b->frame_us[i]   = (uint32_t)frame_acc;
            b->frame_type[i] = frame_nal == 5 ? 'I' : 'P';
        }
        if (frame_nal == 5) {
            b->iframes++;
            b->i_sum += frame_acc;
            if (frame_acc > b->i_max) b->i_max = frame_acc;
        } else {
            b->pframes++;
            b->p_sum += frame_acc;
            if (frame_acc > b->p_max) b->p_max = frame_acc;
            if (b->p_min == 0 || frame_acc < b->p_min) b->p_min = frame_acc;
        }
        frame_acc = 0;

        if (b->conv && pic) {
            if (!rgb) {
                b->out_w = PANEL_W;
                b->out_h = (int)((h * PANEL_W + w / 2) / w);
                rgb  = heap_caps_malloc((size_t)b->out_w * b->out_h * 2, MALLOC_CAP_SPIRAM);
                xmap = heap_caps_malloc(PANEL_W * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
            }
            if (rgb && xmap) {
                int64_t c0 = esp_timer_get_time();
                i420_to_rgb565be(pic, (int)w, (int)h, rgb, b->out_w, b->out_h, xmap);
                b->conv_sum += esp_timer_get_time() - c0;
            }
        }
        if (pic) {
            /* Fingerprint of the luma of the last picture: a decoder that
             * returns fast and wrong cannot pass for a result. */
            uint32_t fp = 0;
            for (uint32_t y = 0; y < h; y += 37) {
                for (uint32_t x = 0; x < w; x += 41) {
                    fp = fp * 31u + pic[y * w + x];
                }
            }
            b->fingerprint = fp;
        }

        size_t now_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (now_int < b->int_min) b->int_min = now_int;
        if (i == 0) {
            b->psram_run = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        }
        /* The idle task of this core has to breathe, or the task watchdog
         * bites during a long clip. One tick per picture, paced or not: a
         * paced run that falls behind never sleeps in the pacing wait, and
         * that is exactly how the first paced run reset the board. */
        vTaskDelay(1);
    }
    b->wall_us = esp_timer_get_time() - t_start;

    h264bsdShutdown(hd);
    h264bsdFree(hd);
    if (rgb) heap_caps_free(rgb);
    if (xmap) heap_caps_free(xmap);
    b->int_leak = (int)b->int_before - (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    xSemaphoreGive(b->done);
    vTaskDelete(NULL);
}

static bool query_str(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char query[200];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, len) == ESP_OK;
}

static int query_int(httpd_req_t *req, const char *key, int def)
{
    char v[16];
    return query_str(req, key, v, sizeof(v)) ? atoi(v) : def;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

esp_err_t aos_h264bench_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char name[64];
    if (!query_str(req, "file", name, sizeof(name)) || strchr(name, '/')) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"file=<name in videos>\"}");
        return ESP_OK;
    }
    const char *root = aos_hal_path_sd_root();
    if (!root) {
        httpd_resp_sendstr(req, "{\"error\":\"no card\"}");
        return ESP_OK;
    }
    char path[192];
    snprintf(path, sizeof(path), "%s/videos/%s", root, name);

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
        httpd_resp_sendstr(req, "{\"error\":\"file too large\"}");
        return ESP_OK;
    }

    bench_t *b = heap_caps_calloc(1, sizeof(*b), MALLOC_CAP_SPIRAM);
    uint8_t *data = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    if (!b || !data) {
        fclose(f);
        heap_caps_free(b);
        heap_caps_free(data);
        httpd_resp_sendstr(req, "{\"error\":\"no psram\"}");
        return ESP_OK;
    }
    b->size = fread(data, 1, (size_t)size, f);
    fclose(f);
    b->data       = data;
    b->dual       = query_int(req, "dual", 0);
    b->core       = query_int(req, "core", 0) ? 1 : 0;
    b->prio       = query_int(req, "prio", 5);
    b->max_frames = query_int(req, "n", BENCH_MAX_FRAMES);
    b->conv       = query_int(req, "conv", 1);
    b->rate       = query_int(req, "rate", 0);
    if (b->prio < 1) b->prio = 1;
    if (b->prio > 20) b->prio = 20;
    if (b->max_frames < 1 || b->max_frames > BENCH_MAX_FRAMES) b->max_frames = BENCH_MAX_FRAMES;
    b->done = xSemaphoreCreateBinary();

    /* The decoder's own stack is small; the task's is for our loop. */
    if (xTaskCreatePinnedToCore(bench_task, "h264bench", 8192, b, b->prio, NULL, b->core) != pdPASS) {
        vSemaphoreDelete(b->done);
        heap_caps_free(data);
        heap_caps_free(b);
        httpd_resp_sendstr(req, "{\"error\":\"no task\"}");
        return ESP_OK;
    }
    xSemaphoreTake(b->done, portMAX_DELAY);
    vSemaphoreDelete(b->done);

    /* Percentiles of every picture, I and P together: what a live viewer
     * sees frame after frame. */
    int nf = b->frames < BENCH_MAX_FRAMES ? b->frames : BENCH_MAX_FRAMES;
    uint32_t *sorted = heap_caps_malloc((size_t)(nf ? nf : 1) * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    uint32_t p50 = 0, p90 = 0, p99 = 0;
    int64_t all_sum = 0;
    if (sorted && nf) {
        memcpy(sorted, b->frame_us, (size_t)nf * sizeof(uint32_t));
        qsort(sorted, (size_t)nf, sizeof(uint32_t), cmp_u32);
        p50 = sorted[nf / 2];
        p90 = sorted[(nf * 9) / 10];
        p99 = sorted[(nf * 99) / 100];
        for (int i = 0; i < nf; i++) all_sum += b->frame_us[i];
    }
    heap_caps_free(sorted);

    double avg_ms = nf ? all_sum / 1000.0 / nf : 0;
    char *out = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    int len = 0;
    if (out) {
        len = snprintf(out, 8192,
            "{\"file\":\"%s\",\"bytes\":%u,\"tinyh264\":\"%s\",\"dual\":%d,\"core\":%d,\"prio\":%d,"
            "\"rate\":%d,\"alloc_failed\":%s,"
            "\"width\":%" PRIu32 ",\"height\":%" PRIu32 ",\"nals\":%d,\"frames\":%d,\"errors\":%d,"
            "\"i_frames\":%d,\"i_avg_ms\":%.1f,\"i_max_ms\":%.1f,"
            "\"p_frames\":%d,\"p_avg_ms\":%.1f,\"p_min_ms\":%.1f,\"p_max_ms\":%.1f,"
            "\"avg_ms\":%.1f,\"p50_ms\":%.1f,\"p90_ms\":%.1f,\"p99_ms\":%.1f,\"decode_fps\":%.2f,"
            "\"wall_ms\":%.0f,\"wall_fps\":%.2f,\"lag_end_ms\":%.0f,\"lag_max_ms\":%.0f,"
            "\"conv_w\":%d,\"conv_h\":%d,\"conv_avg_ms\":%.1f,"
            "\"internal_alloc_kb\":%.1f,\"internal_peak_kb\":%.1f,\"internal_leak_b\":%d,"
            "\"psram_alloc_kb\":%.1f,\"psram_run_kb\":%.1f,\"fingerprint\":%" PRIu32 ",\"times\":\"",
            name, (unsigned)b->size, esp_tinyh264_get_version(), b->dual, b->core, b->prio,
            b->rate, b->alloc_failed ? "true" : "false",
            b->width, b->height, b->nals, b->frames, b->errors,
            b->iframes, b->iframes ? b->i_sum / 1000.0 / b->iframes : 0, b->i_max / 1000.0,
            b->pframes, b->pframes ? b->p_sum / 1000.0 / b->pframes : 0, b->p_min / 1000.0, b->p_max / 1000.0,
            avg_ms, p50 / 1000.0, p90 / 1000.0, p99 / 1000.0, avg_ms > 0 ? 1000.0 / avg_ms : 0,
            b->wall_us / 1000.0, b->wall_us ? b->frames * 1e6 / b->wall_us : 0,
            b->lag_end_us / 1000.0, b->lag_max_us / 1000.0,
            b->out_w, b->out_h, b->frames && b->conv ? b->conv_sum / 1000.0 / b->frames : 0,
            ((int)b->int_before - (int)b->int_alloc) / 1024.0,
            ((int)b->int_before - (int)b->int_min) / 1024.0, b->int_leak,
            ((int)b->psram_before - (int)b->psram_alloc) / 1024.0,
            b->psram_run ? ((int)b->psram_before - (int)b->psram_run) / 1024.0 : 0,
            b->fingerprint);
        /* Every picture as <type><ms>, compact enough for 600 frames. */
        for (int i = 0; i < nf && len < 8192 - 16; i++) {
            len += snprintf(out + len, 8192 - len, "%c%" PRIu32 " ",
                            b->frame_type[i], (b->frame_us[i] + 500) / 1000);
        }
        len += snprintf(out + len, 8192 - len, "\"}");
        httpd_resp_send(req, out, len);
        heap_caps_free(out);
    } else {
        httpd_resp_sendstr(req, "{\"error\":\"no memory for the answer\"}");
    }
    heap_caps_free(data);
    heap_caps_free(b);
    return ESP_OK;
}
