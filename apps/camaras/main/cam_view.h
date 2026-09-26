/*
 * AmoledOS - Cameras: where the worker and the UI meet.
 *
 * The work is split across the two cores, because on the board neither
 * alone keeps up with 12 fps (measured 2026-09-26, doorbell 704x576): a P
 * frame decodes in 74-80 ms with the network and the panel running, and
 * converting it to the panel's pixels is another 33. In one task that is
 * 107 ms a picture, 9 fps, and the view kept falling behind and skipping to
 * the next keyframe. Split, the decoder (core 0, the worker) and the
 * conversion plus the push to the panel (core 1, LVGL's timer) overlap.
 *
 * H.264: the worker hands over the decoder's own picture, not a copy (a copy
 * of 600 KB of PSRAM would cost what the split saves). That is safe because
 * of how the decoder keeps its pictures: picture N is the reference of N+1,
 * so decoding N+1 does not touch N's planes. Decoding N+2 may, and so may an
 * IDR, which references nothing. So the worker, before each slice:
 *
 *     busy = seq of the picture it is about to decode;  barrier
 *     wait while the UI converts a picture older than the last one out,
 *     or while it converts anything and this slice is an IDR
 *
 * and the UI, taking picture s:
 *
 *     converting = s;  barrier
 *     if busy > s + 1: the worker already went past the safe point: let it go
 *
 * One of the two always sees the other's store (both sides fence between
 * their store and their load), so the UI never reads planes being rewritten.
 * Two pending entries (s & 1) keep the worker from overwriting the
 * description the UI is reading.
 *
 * JPEG: every frame stands alone, so the rule is simply "the newest wins".
 * The worker decodes into one of CAM_JSLOTS buffers of its own, at the
 * picture's own size, and the UI scales while it copies from it.
 *
 *   FREE -> BUSY -> READY        the worker
 *   READY -> FREE                the UI, once copied into its strips (or
 *                                when a newer one makes it stale)
 */
#pragma once

#include "cam.h"
#include "cam_conv.h"

#include "aos_hal.h"

#if __has_include("esp_jpeg_dec.h")
#include "esp_jpeg_dec.h"
#define CAM_HAS_JPEG 1
#endif

#define CAM_JSLOTS 3

enum { CAM_SLOT_FREE = 0, CAM_SLOT_BUSY, CAM_SLOT_READY };

typedef enum { CAM_FIT = 0, CAM_FILL } cam_mode_t;

typedef struct {
    volatile int state;
    uint16_t    *px;            /* RGB565 BE, w x h, 16-byte aligned */
    size_t       cap;           /* bytes allocated */
    int          w, h;
    uint32_t     seq;
    uint32_t     dec_ms;
} cam_jslot_t;

struct cam_view {
    /* set by the UI before the worker starts */
    cam_t        cam;
    cam_url_t    url;
    volatile int mode;          /* cam_mode_t, read by the worker per picture */

    /* H.264 pictures handed over (see above) */
    aos_h264_pic_t    pend[2];
    volatile uint32_t pend_seq;     /* last picture out of the decoder; 0 = none */
    volatile uint32_t pend_dec_ms;
    volatile uint32_t busy_seq;     /* worker: the picture being decoded, 0 = idle */
    volatile uint32_t conv_seq;     /* UI: the picture being converted, 0 = none */
    uint32_t          done_seq;     /* UI: last picture shown */

    /* JPEG frames */
    cam_jslot_t  jslots[CAM_JSLOTS];
    uint32_t     jseq;
    uint8_t     *jpeg_in;           /* the newest whole JPEG, waiting for flush */
    int          jpeg_in_len;
    int          jpeg_in_cap;
    int64_t      jpeg_in_pts;

    /* written by the worker, read by the UI */
    volatile int      state;    /* cam_state_t */
    char              detail[112];
    volatile uint32_t detail_gen;
    volatile int      codec;    /* cam_codec_t */
    volatile int      src_w, src_h;
    volatile bool     undecodable;

    /* counters: the worker adds, the UI takes differences */
    volatile uint32_t pictures;         /* decoded */
    volatile uint32_t dropped;          /* not decoded, by policy */
    volatile uint32_t errors;           /* refused by a decoder */
    volatile uint32_t bytes;
    volatile uint32_t dec_ms;           /* summed over 'pictures' */
    volatile uint32_t i_pics, i_ms;     /* keyframes alone: the worst case */
    volatile uint32_t dec_ms_max;       /* slowest since the UI last looked */
    volatile uint32_t skips;            /* times the view gave up up to the next IDR */
    volatile uint32_t waits_ms;         /* worker time spent waiting on the UI */
    volatile int32_t  lag_ms;

    /* the worker's own */
    aos_h264_t  *h264;
#ifdef CAM_HAS_JPEG
    jpeg_dec_handle_t jpeg;
#endif
    bool         have_base;
    int64_t      base_off;
    bool         skipping;
    uint32_t     bad_run;           /* consecutive refused slices */
    bool         ever_picture;
};

void cam_view_worker(void *arg);

/* Frees what the worker left for the UI (JPEG slots and input). Only after
 * aos_hal_worker_stop() has returned. */
void cam_view_release(cam_view_t *v);

/* JPEG: decodes the newest frame handed over since the last flush, if any.
 * The sessions call it whenever they have drained what the socket had. */
void cam_view_flush(cam_view_t *v);

/* Where a src_w x src_h picture goes on the panel, and which part of it. */
void cam_view_geometry(int src_w, int src_h, cam_mode_t mode, cam_geom_t *g, int *x, int *y);

/* ---- the UI's side ---------------------------------------------------------- */

/* Called for each strip the UI renders: rows [y, y+h) of the panel, x..x+w. */
typedef void (*cam_strip_fn)(void *ctx, int x, int y, int w, int h, const uint16_t *px);

/* Renders the newest picture, if there is one the panel has not shown, in
 * strips through 'out'. strip/strip2 are two buffers of AOS_SCREEN_W x
 * strip_rows pixels, used in turn. Returns true when a picture went out, and
 * where (for the UI to know when fit/fill moved the bands). */
bool cam_view_render(cam_view_t *v, uint16_t *strip[2], int strip_rows,
                     cam_strip_fn out, void *ctx, int *x, int *y, int *w, int *h,
                     uint32_t *render_ms);
