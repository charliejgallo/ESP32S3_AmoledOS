/*
 * AmoledOS - H.264 for apps, on the board: tinyh264 from esp_h264.
 *
 * Branch rtsp. esp_h264's own wrapper (esp_h264_dec_sw_*) is a thin layer
 * over tinyh264 that decides the second decoder task at build time; this
 * calls tinyh264 directly, the way /api/h264bench does, so the choice stays
 * ours. It stays single-task, and not for lack of trying the other way
 * (2026-09-26, the doorbell at 12 fps in the Cameras app):
 *
 *   one task             P 70 ms, I 225 ms, 10.9 fps on the panel
 *   helper at prio 5     P 65, I 165, but the conversion on the other core
 *                        went from 35 to 64 ms and the panel showed 4-8 fps
 *   helper at prio 3     P 70-84, I 165, and LVGL ran in fits: 4-8 fps
 *
 * Those two were measured before the app's worker dropped below LVGL's
 * priority (the freezes had another cause, apps/camaras/main/camaras.c), so
 * the helper at 3 was tried again after: P 106 ms, worse than alone. It
 * takes cache and PSRAM from the conversion on the other core even when it
 * yields the CPU.
 *
 * The helper works on every picture, not only keyframes, and the other core
 * is where LVGL, the conversion and the panel's SPI live (v0.4.4). The bench
 * had shown the same thing in quieter numbers: keyframes 218 -> 157 ms, P
 * frames unchanged. docs/CAMERAS.md has the rest.
 *
 * The simulator's twin is in sim/sim_codec.c, over libavcodec.
 */
#include "aos_hal.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"


/* tinyh264's interface; esp_h264 keeps its header private. These match
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

struct aos_h264 {
    h264bsd_hd_t hd;
    /* tinyh264 writes the size only when it activates a parameter set, not
     * with every picture: it has to live as long as the decoder does. The
     * bench divided by a zero read per call before learning this. */
    uint32_t width, height;
};

aos_h264_t *aos_hal_h264_open(void)
{
    aos_h264_t *dec = heap_caps_calloc(1, sizeof(*dec), MALLOC_CAP_SPIRAM);
    if (!dec) {
        return NULL;
    }
    h264bsd_cfg_t cfg = { .dualTaskEnable = 0, .dualTaskCore = 1, .dualTaskPriority = 5 };
    dec->hd = h264bsdAlloc(&cfg);
    if (!dec->hd) {
        heap_caps_free(dec);
        return NULL;
    }
    return dec;
}

int aos_hal_h264_decode(aos_h264_t *dec, const uint8_t *nal, int len, aos_h264_pic_t *pic)
{
    if (!dec || !nal || len <= 0 || !pic) {
        return -1;
    }
    uint8_t *p = (uint8_t *)nal;
    uint32_t left = (uint32_t)len;
    while (left > 0) {
        uint32_t remain = left;
        uint8_t *out = NULL;
        uint32_t rc = h264bsdDecode(dec->hd, p, &remain, &out, &dec->width, &dec->height);
        uint32_t used = left - remain;
        switch (rc) {
        case H264BSD_PIC_RDY:
            if (!out || dec->width == 0 || dec->height == 0) {
                return -1;
            }
            pic->width     = (int)dec->width;
            pic->height    = (int)dec->height;
            pic->stride_y  = (int)dec->width;
            pic->stride_uv = (int)dec->width / 2;
            pic->y = out;
            pic->u = out + dec->width * dec->height;
            pic->v = pic->u + (dec->width / 2) * (dec->height / 2);
            return 1;
        case H264BSD_ERROR:
        case H264BSD_PARAM_SET_ERROR:
        case H264BSD_MEMALLOC_ERROR:
            return -1;
        default:
            break;
        }
        if (used == 0) {
            break;
        }
        p += used;
        left = remain;
    }
    return 0;
}

void aos_hal_h264_close(aos_h264_t *dec)
{
    if (!dec) {
        return;
    }
    if (dec->hd) {
        h264bsdShutdown(dec->hd);
        h264bsdFree(dec->hd);
    }
    heap_caps_free(dec);
}
