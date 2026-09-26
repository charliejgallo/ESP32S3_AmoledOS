/*
 * AmoledOS - Cameras: the pipeline between the socket and the panel.
 *
 * cam_view.h has the split across the cores and the handshake. This file is
 * both ends of it: the worker's (cam_view_nal, cam_view_jpeg, cam_view_flush,
 * the worker loop) and the UI's (cam_view_render).
 *
 * The lag is measured, not guessed: the RTP clock says when the camera took
 * the picture, uptime says when it got here, and the smallest difference
 * ever seen is the pipe's own delay. Anything above that is lateness. What
 * to do about it depends on the codec:
 *
 *   H.264  every P frame is a reference of the next (the doorbell and the
 *          outdoor camera both mark every slice nal_ref_idc=3), so a P frame
 *          cannot be skipped alone. Past LAG_H264_MS the view drops
 *          everything up to the next IDR, which reading at full speed
 *          reaches quickly, and starts clean from there. With the camera at
 *          GOP = fps that is at most a second of frozen picture.
 *   JPEG   every frame stands alone: the newest one wins. The sessions hand
 *          over every frame they complete, and the view decodes only the
 *          last one when the socket has been drained (cam_view_flush). The
 *          first version dropped JPEGs by lateness instead, and on the
 *          outdoor camera showed nothing at all: that lag came from the
 *          network (4.2 Mbps through lwIP's 5.7 KB window), and no amount of
 *          not decoding makes the network faster.
 */
#include "cam_view.h"

#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The doorbell at 12 fps needs ~1.1 s of decoding a second, 10 % more than
 * there is, so the lag grows through every GOP whatever the threshold; the
 * threshold only picks how the deficit shows. At 700 ms: ~10 fps, and every
 * 7-10 s a skip to the next keyframe (up to a second frozen). At 350 ms,
 * tried to trade that for short hitches: a keyframe alone already puts the
 * lag near 300 (230 ms of decode, plus arrival jitter), so the first P frame
 * after it tripped the skip and the view showed 2 fps. */
#define LAG_H264_MS    700
#define RETRY_MS       3000
#define BAD_RUN_LIMIT  40       /* refused slices in a row with no picture yet */
#define JPEG_IN_CAP    (512 * 1024)

/* ---- state for the UI ------------------------------------------------------- */

void cam_view_state(cam_view_t *v, cam_state_t st, const char *detail)
{
    /* "Not for this watch" is final for the session: the SPS that says so
     * comes with the SDP, before PLAY, and the RTSP client's "waiting" that
     * follows it hid the message (H.264 Main, 2026-09-26). */
    if (v->state == CAM_ST_UNSUPPORTED && st != CAM_ST_UNSUPPORTED) {
        return;
    }
    if (detail) {
        snprintf(v->detail, sizeof(v->detail), "%s", detail);
        __sync_synchronize();
        v->detail_gen++;
    }
    v->state = st;
}

void cam_view_bytes(cam_view_t *v, int n)
{
    v->bytes += (uint32_t)n;
}

void cam_view_codec(cam_view_t *v, cam_codec_t codec)
{
    v->codec = codec;
}

/* ---- geometry --------------------------------------------------------------- */

void cam_view_geometry(int w, int h, cam_mode_t mode, cam_geom_t *g, int *x, int *y)
{
    const int W = AOS_SCREEN_W, H = AOS_SCREEN_H;
    memset(g, 0, sizeof(*g));
    if (mode == CAM_FIT) {
        g->sw = w;
        g->sh = h;
        g->ow = W;
        g->oh = (h * W + w / 2) / w;
        if (g->oh > H) {
            g->oh = H;
            g->ow = (w * H + h / 2) / h;
        }
    } else {
        g->ow = W;
        g->oh = H;
        if ((int64_t)w * H > (int64_t)h * W) {         /* wider than the panel: crop the sides */
            g->sh = h;
            g->sw = (h * W + H / 2) / H;
            if (g->sw > w) g->sw = w;
            g->sx = (w - g->sw) / 2;
        } else {                                       /* taller: crop top and bottom */
            g->sw = w;
            g->sh = (w * H + W / 2) / W;
            if (g->sh > h) g->sh = h;
            g->sy = (h - g->sh) / 2;
        }
    }
    /* The panel wants even edges. */
    g->ow &= ~1;
    g->oh &= ~1;
    *x = ((W - g->ow) / 2) & ~1;
    *y = ((H - g->oh) / 2) & ~1;
}

static void picture_out(cam_view_t *v, int w, int h, uint32_t dec_ms, bool key)
{
    v->src_w = w;
    v->src_h = h;
    v->pictures++;
    v->dec_ms += dec_ms;
    if (key) {
        v->i_pics++;
        v->i_ms += dec_ms;
    }
    if (dec_ms > v->dec_ms_max) {
        v->dec_ms_max = dec_ms;
    }
    v->ever_picture = true;
    if (v->state != CAM_ST_LIVE) {
        cam_view_state(v, CAM_ST_LIVE, NULL);
    }
}

/* Lateness of a picture stamped pts_ms by the source's clock. */
static int32_t lateness(cam_view_t *v, int64_t pts_ms)
{
    int64_t off = (int64_t)aos_hal_uptime_ms() - pts_ms;
    if (!v->have_base || off < v->base_off) {
        v->have_base = true;
        v->base_off = off;
    }
    int32_t lag = (int32_t)(off - v->base_off);
    v->lag_ms = lag;
    return lag;
}

/* ---- H.264 (worker) --------------------------------------------------------- */

/* The worker's half of the handshake in cam_view.h. */
static void before_slice(cam_view_t *v, bool idr)
{
    v->busy_seq = v->pend_seq + 1;
    __sync_synchronize();
    uint64_t t0 = 0;
    for (;;) {
        uint32_t conv = v->conv_seq;
        bool unsafe = conv && (conv != v->pend_seq || idr);
        if (!unsafe || aos_hal_worker_should_stop()) {
            break;
        }
        if (!t0) {
            t0 = aos_hal_uptime_ms();
        }
        aos_hal_worker_sleep(1);
    }
    if (t0) {
        v->waits_ms += (uint32_t)(aos_hal_uptime_ms() - t0);
    }
}

void cam_view_nal(cam_view_t *v, const uint8_t *nal, int len, int64_t pts_ms)
{
    int sc = (len > 4 && nal[2] == 0) ? 4 : 3;
    if (len <= sc) {
        return;
    }
    int type = nal[sc] & 0x1F;
    bool slice = type == 1 || type == 5;

    if (v->state == CAM_ST_UNSUPPORTED) {
        return;                     /* said once; decoding would only fail again */
    }
    if (type == 7 && !v->have_sps && cam_sps_parse(nal + sc, len - sc, &v->sps)) {
        v->have_sps = true;
        aos_hal_log("camaras", "sps: profile %d level %d refs %d, %dx%d (coded %dx%d)",
                    v->sps.profile_idc, v->sps.level_idc, v->sps.ref_frames,
                    v->sps.width, v->sps.height, v->sps.coded_w, v->sps.coded_h);
        v->src_w = v->sps.width;
        v->src_h = v->sps.height;
        if (!cam_sps_baseline(&v->sps)) {
            char msg[112];
            const char *name = v->sps.profile_idc == 77 ? "Main" :
                               v->sps.profile_idc == 100 ? "High" : "?";
            snprintf(msg, sizeof(msg), _("H.264 %s: el reloj sólo\ndecodifica el perfil Baseline."), name);
            cam_view_state(v, CAM_ST_UNSUPPORTED, msg);
            return;
        }
    }

    if (slice && pts_ms >= 0) {
        int32_t lag = lateness(v, pts_ms);
        if (type == 5) {
            v->skipping = false;
        } else if (v->skipping) {
            v->dropped++;
            return;
        } else if (lag > LAG_H264_MS) {
            aos_hal_log("camaras", "late by %d ms: dropping to the next keyframe", (int)lag);
            v->skipping = true;
            v->skips++;
            v->dropped++;
            return;
        }
    }
    if (!v->h264) {
        v->h264 = aos_hal_h264_open();
        if (!v->h264) {
            cam_view_state(v, CAM_ST_ERROR, _("El decodificador H.264 no abrió"));
            return;
        }
    }
    if (slice) {
        before_slice(v, type == 5);
    }

    uint64_t t0 = aos_hal_uptime_ms();
    aos_h264_pic_t pic;
    int r = aos_hal_h264_decode(v->h264, nal, len, &pic);
    uint32_t dec_ms = (uint32_t)(aos_hal_uptime_ms() - t0);
    /* busy_seq stays set until a picture is published: a picture cut into
     * several slices is being written into a buffer between calls too. */
    if (r == AOS_H264_ERR_MEM) {
        char msg[112];
        snprintf(msg, sizeof(msg), _("%dx%d no entra en\nla memoria del reloj."),
                 v->have_sps ? v->sps.width : 0, v->have_sps ? v->sps.height : 0);
        aos_hal_log("camaras", "decoder out of memory at %dx%d",
                    v->have_sps ? v->sps.coded_w : 0, v->have_sps ? v->sps.coded_h : 0);
        cam_view_state(v, CAM_ST_UNSUPPORTED, msg);
        return;
    }
    if (r < 0) {
        v->errors++;
        if (slice && !v->ever_picture && ++v->bad_run == BAD_RUN_LIMIT) {
            /* Every slice refused and not one picture: a stream this decoder
             * does not speak (Main/High, CABAC). Say so instead of "waiting". */
            v->undecodable = true;
            aos_hal_log("camaras", "%d slices refused, no picture: not constrained baseline?",
                        BAD_RUN_LIMIT);
        }
        return;
    }
    if (r == 0) {
        return;
    }
    v->bad_run = 0;
    /* tinyh264 ignores the SPS cropping: 640x360 comes out 640x368, the last
     * eight rows padding. The picture handed over is the cropped one. */
    if (v->have_sps && v->sps.width <= pic.width && v->sps.height <= pic.height) {
        pic.width = v->sps.width;
        pic.height = v->sps.height;
    }
    uint32_t s = v->pend_seq + 1;
    v->pend[s & 1] = pic;
    v->pend_dec_ms = dec_ms;
    __sync_synchronize();
    v->pend_seq = s;
    __sync_synchronize();
    v->busy_seq = 0;
    picture_out(v, pic.width, pic.height, dec_ms, type == 5);
}

/* ---- JPEG (worker) ------------------------------------------------------------ */

void cam_view_jpeg(cam_view_t *v, const uint8_t *jpeg, int len, int64_t pts_ms)
{
    if (!v->jpeg_in) {
        v->jpeg_in = malloc(JPEG_IN_CAP);
        v->jpeg_in_cap = v->jpeg_in ? JPEG_IN_CAP : 0;
    }
    if (len > v->jpeg_in_cap) {
        v->errors++;
        return;
    }
    if (v->jpeg_in_len) {
        v->dropped++;               /* a newer one came before this was decoded */
    }
    memcpy(v->jpeg_in, jpeg, (size_t)len);
    v->jpeg_in_len = len;
    v->jpeg_in_pts = pts_ms;
    if (pts_ms >= 0) {
        lateness(v, pts_ms);        /* measured and shown, not acted on */
    }
}

#ifdef CAM_HAS_JPEG
/* The decoder is asked for the picture at its own size, and the UI scales
 * while it copies into its strips. esp_new_jpeg can scale by itself to any
 * multiple of 8, and that was the first version; measured on the outdoor
 * camera (640x480, 45 KB frames, 2026-09-26): 150 ms a frame scaled to
 * 600x448, 57 ms at its own size. The copy with a column map costs the UI
 * a few milliseconds. */
/* The picture's own size, from its SOF marker. Not from the decoder: a
 * handle opened with a scale reports the SCALED size in its header info, and
 * taking that for the source's made the app decide every other frame that no
 * scaling was needed, reopen the decoder unscaled, and paint a 640x480
 * picture into a 600x448 frame: the "noise between frames" of 2026-09-26.
 * (Scaling is gone since, but the size is still checked against the SOF.) */
static bool jpeg_size(const uint8_t *p, int len, int *w, int *h)
{
    for (int i = 2; i + 9 < len; ) {
        if (p[i] != 0xFF) {
            return false;
        }
        int m = p[i + 1];
        if (m == 0xC0 || m == 0xC1 || m == 0xC2) {
            *h = (p[i + 5] << 8) | p[i + 6];
            *w = (p[i + 7] << 8) | p[i + 8];
            return *w > 0 && *h > 0;
        }
        if (m == 0xDA || m == 0xD9) {
            return false;                   /* scan data: no SOF before it */
        }
        i += 2 + ((p[i + 2] << 8) | p[i + 3]);
    }
    return false;
}

static bool jpeg_open(cam_view_t *v)
{
    if (v->jpeg) {
        return true;
    }
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    if (jpeg_dec_open(&cfg, &v->jpeg) != JPEG_ERR_OK) {
        v->jpeg = NULL;
        return false;
    }
    return true;
}

/* Debugging aid: with a file named 'camdump' in the card's videos folder,
 * every 25th frame of the first 100 is written next to it, compressed and
 * decoded, to compare the board's decoder against the desktop's. */
static void dump_frame(cam_view_t *v, int len, const cam_jslot_t *s)
{
    static int n, dumps = -1;
    const char *root = aos_hal_path_sd_root();
    if (!root) {
        return;
    }
    char path[160];
    if (dumps < 0) {
        snprintf(path, sizeof(path), "%s/videos/camdump", root);
        FILE *f = fopen(path, "rb");
        dumps = f ? 0 : 99;
        if (f) fclose(f);
    }
    if (dumps >= 4 || n++ % 25 != 0) {
        return;
    }
    snprintf(path, sizeof(path), "%s/videos/cd%d.jpg", root, dumps);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(v->jpeg_in, 1, (size_t)len, f); fclose(f); }
    snprintf(path, sizeof(path), "%s/videos/cd%d_%dx%d.raw", root, dumps, s->w, s->h);
    f = fopen(path, "wb");
    if (f) { fwrite(s->px, 2, (size_t)s->w * s->h, f); fclose(f); }
    aos_hal_log("camaras", "dump %d: %d B jpeg, %dx%d out", dumps, len, s->w, s->h);
    dumps++;
}

/* The first few JPEGs the view refuses go to the log with the stage and the
 * decoder's code, and to the card next to the dumps: a refused frame is
 * otherwise just a counter. */
static void jpeg_refused(cam_view_t *v, int len, int stage, int rc)
{
    static int n;
    v->errors++;
    if (n >= 3) {
        return;
    }
    aos_hal_log("camaras", "jpeg refused: stage %d rc %d, %d bytes", stage, rc, len);
    const char *root = aos_hal_path_sd_root();
    if (root) {
        char path[160];
        snprintf(path, sizeof(path), "%s/videos/jerr%d.jpg", root, n);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(v->jpeg_in, 1, (size_t)len, f); fclose(f); }
    }
    n++;
}

static cam_jslot_t *jslot_free(cam_view_t *v)
{
    for (int i = 0; i < CAM_JSLOTS; i++) {
        if (v->jslots[i].state == CAM_SLOT_FREE) {
            v->jslots[i].state = CAM_SLOT_BUSY;
            return &v->jslots[i];
        }
    }
    return NULL;
}
#endif

void cam_view_flush(cam_view_t *v)
{
    if (!v->jpeg_in_len) {
        return;
    }
#ifdef CAM_HAS_JPEG
    int len = v->jpeg_in_len;
    v->jpeg_in_len = 0;
    cam_jslot_t *s = jslot_free(v);
    if (!s) {
        v->dropped++;               /* the UI has not taken the last ones */
        return;
    }
    uint64_t t0 = aos_hal_uptime_ms();
    jpeg_dec_io_t io = { .inbuf = v->jpeg_in, .inbuf_len = len, .inbuf_remain = len };
    jpeg_dec_header_info_t info = {0};

    int src_w, src_h;
    if (!jpeg_size(v->jpeg_in, len, &src_w, &src_h) || src_w > 2048 || src_h > 2048) {
        jpeg_refused(v, len, 1, 0);
        s->state = CAM_SLOT_FREE;
        return;
    }
    if (!jpeg_open(v)) {
        s->state = CAM_SLOT_FREE;
        cam_view_state(v, CAM_ST_ERROR, _("El decodificador JPEG no abrió"));
        return;
    }
    jpeg_error_t jr = jpeg_dec_parse_header(v->jpeg, &io, &info);
    if (jr != JPEG_ERR_OK) {
        jpeg_refused(v, len, 2, jr);
        s->state = CAM_SLOT_FREE;
        return;
    }
    int out_len = 0;
    jpeg_dec_get_outbuf_len(v->jpeg, &out_len);
    int out_w = src_w;
    int out_h = src_h;
    if (out_len != out_w * out_h * 2) {
        /* The decoder would write a picture of another shape than the one the
         * UI will read: a frame of stripes. Never show it. */
        s->state = CAM_SLOT_FREE;
        jpeg_refused(v, len, 3, out_len);
        return;
    }
    if (!s->px || s->cap < (size_t)out_len) {
        if (s->px) {
            jpeg_free_align(s->px);
        }
        s->px = jpeg_calloc_align((size_t)out_len, 16);
        s->cap = s->px ? (size_t)out_len : 0;
        if (!s->px) {
            s->state = CAM_SLOT_FREE;
            cam_view_state(v, CAM_ST_ERROR, _("Sin memoria"));
            return;
        }
    }
    io.outbuf = (uint8_t *)s->px;
    jr = jpeg_dec_process(v->jpeg, &io);
    if (jr != JPEG_ERR_OK) {
        jpeg_refused(v, len, 4, jr);
        s->state = CAM_SLOT_FREE;
        return;
    }
    uint32_t dec_ms = (uint32_t)(aos_hal_uptime_ms() - t0);
    s->w = out_w;
    s->h = out_h;
    s->seq = ++v->jseq;
    s->dec_ms = dec_ms;
    dump_frame(v, len, s);
    __sync_synchronize();
    s->state = CAM_SLOT_READY;
    picture_out(v, src_w, src_h, dec_ms, false);
#else
    v->jpeg_in_len = 0;
    v->errors++;
#endif
}

/* ---- the UI's end ------------------------------------------------------------ */

bool cam_view_render(cam_view_t *v, uint16_t *strip[2], int strip_rows,
                     cam_strip_fn out, void *ctx, int *px, int *py, int *pw, int *ph,
                     uint32_t *render_ms)
{
    cam_geom_t g;
    int x, y;
    uint64_t t0 = aos_hal_uptime_ms();

    /* JPEG: the newest READY slot; older ones are let go. */
    int js = -1;
    for (int i = 0; i < CAM_JSLOTS; i++) {
        if (v->jslots[i].state == CAM_SLOT_READY &&
            (js < 0 || v->jslots[i].seq > v->jslots[js].seq)) {
            js = i;
        }
    }
    if (js >= 0) {
        __sync_synchronize();
        for (int i = 0; i < CAM_JSLOTS; i++) {
            if (i != js && v->jslots[i].state == CAM_SLOT_READY) {
                v->jslots[i].state = CAM_SLOT_FREE;
            }
        }
        cam_jslot_t *s = &v->jslots[js];
        cam_view_geometry(s->w, s->h, (cam_mode_t)v->mode, &g, &x, &y);
        cam_conv_prepare(&g);
        int k = 0;
        for (int row = 0; row < g.oh; row += strip_rows, k ^= 1) {
            int rows = g.oh - row < strip_rows ? g.oh - row : strip_rows;
            cam_conv_rgb565_rows(s->px, s->w, &g, row, rows, strip[k]);
            out(ctx, x, y + row, g.ow, rows, strip[k]);
        }
        s->state = CAM_SLOT_FREE;
        goto shown;
    }

    /* H.264: the newest picture, with the UI's half of the handshake. */
    uint32_t s = v->pend_seq;
    if (s == 0 || s == v->done_seq) {
        return false;
    }
    v->conv_seq = s;
    __sync_synchronize();
    if (v->busy_seq > s + 1 || v->pend_seq > s + 1) {
        v->conv_seq = 0;            /* the worker is past the safe point: next time */
        return false;
    }
    aos_h264_pic_t pic = v->pend[s & 1];
    cam_view_geometry(pic.width, pic.height, (cam_mode_t)v->mode, &g, &x, &y);
    cam_conv_prepare(&g);
    int k = 0;
    for (int row = 0; row < g.oh; row += strip_rows, k ^= 1) {
        int rows = g.oh - row < strip_rows ? g.oh - row : strip_rows;
        cam_conv_i420_rows(&pic, &g, row, rows, strip[k]);
        out(ctx, x, y + row, g.ow, rows, strip[k]);
    }
    __sync_synchronize();
    v->conv_seq = 0;
    v->done_seq = s;

shown:
    *px = x;
    *py = y;
    *pw = g.ow;
    *ph = g.oh;
    *render_ms = (uint32_t)(aos_hal_uptime_ms() - t0);
    return true;
}

/* ---- the worker ------------------------------------------------------------- */

static void close_decoders(cam_view_t *v)
{
    if (v->h264) {
        /* The UI may be converting the last picture: it lives in the decoder. */
        while (v->conv_seq) {
            aos_hal_worker_sleep(1);
        }
        v->pend_seq = 0;
        v->done_seq = 0;
        aos_hal_h264_close(v->h264);
        v->h264 = NULL;
    }
#ifdef CAM_HAS_JPEG
    if (v->jpeg) {
        jpeg_dec_close(v->jpeg);
        v->jpeg = NULL;
    }
#endif
}

void cam_view_worker(void *arg)
{
    cam_view_t *v = arg;
    while (!aos_hal_worker_should_stop()) {
        char why[112] = "";
        /* A new session starts clean: its clock is another camera's, or the
         * same camera's after a reconnection. */
        v->have_base = false;
        v->skipping = false;
        v->bad_run = 0;
        v->ever_picture = false;
        v->jpeg_in_len = 0;
        v->have_sps = false;
        bool ok = v->url.rtsp ? cam_rtsp_run(v, &v->cam, &v->url, why, sizeof(why))
                              : cam_http_run(v, &v->cam, &v->url, why, sizeof(why));
        /* A decoder keeps the previous session's references; the next one
         * starts at a keyframe anyway. */
        close_decoders(v);
        if (ok || aos_hal_worker_should_stop()) {
            break;
        }
        if (v->state == CAM_ST_UNSUPPORTED) {
            /* Retrying shows the same stream again: wait to be told to stop. */
            aos_hal_log("camaras", "%s: %s", v->cam.name, v->detail);
            while (!aos_hal_worker_should_stop()) {
                aos_hal_worker_sleep(100);
            }
            break;
        }
        aos_hal_log("camaras", "%s: %s", v->cam.name, why);
        cam_view_state(v, CAM_ST_ERROR, why);
        for (int t = 0; t < RETRY_MS && !aos_hal_worker_should_stop(); t += 100) {
            aos_hal_worker_sleep(100);
        }
    }
    close_decoders(v);
}

/* The JPEG slots and the input buffer outlive the worker: the UI frees them
 * once it has joined it. */
void cam_view_release(cam_view_t *v)
{
    for (int i = 0; i < CAM_JSLOTS; i++) {
#ifdef CAM_HAS_JPEG
        if (v->jslots[i].px) {
            jpeg_free_align(v->jslots[i].px);
        }
#endif
        v->jslots[i].px = NULL;
        v->jslots[i].cap = 0;
        v->jslots[i].state = CAM_SLOT_FREE;
    }
    free(v->jpeg_in);
    v->jpeg_in = NULL;
    v->jpeg_in_cap = 0;
    v->jpeg_in_len = 0;
}
