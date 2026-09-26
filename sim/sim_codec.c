/*
 * AmoledOS simulator - the board's video decoders, over libavcodec.
 *
 * Branch rtsp. The camera viewer decodes H.264 through aos_hal_h264_*
 * (tinyh264 on the board, components/aos_hal/aos_h264.c) and JPEG through
 * esp_new_jpeg, which the firmware exports to apps. Neither exists on the
 * Mac, so this file gives the simulator both with the same interfaces, and
 * the app's source has no #ifdef around its decoders: the RTSP client, the
 * depacketisers and the lag policy run here against the real cameras before
 * a byte goes to the watch.
 *
 * What it does NOT reproduce is the board's limits: libavcodec decodes Main
 * and High profile, CABAC and B-frames, and does it fast. A stream that plays
 * here and not on the watch is a stream the watch cannot decode
 * (docs/CAMERAS.md).
 *
 *   brew install ffmpeg
 */
#include "aos_hal.h"
#if __has_include("esp_jpeg_dec.h")
#include "esp_jpeg_dec.h"
#define SIM_JPEG 1
#endif

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

/* ---- H.264 ---------------------------------------------------------------- */

struct aos_h264 {
    AVCodecContext *ctx;
    AVPacket       *pkt;
    AVFrame        *frame;
    AVFrame        *out;        /* the last picture, I420, owned by us */
};

aos_h264_t *aos_hal_h264_open(void)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        return NULL;
    }
    aos_h264_t *dec = calloc(1, sizeof(*dec));
    if (!dec) {
        return NULL;
    }
    dec->ctx = avcodec_alloc_context3(codec);
    dec->ctx->thread_count = 1;
    dec->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    /* One NAL per packet, like the board: libavcodec takes a lone slice as
     * a picture when told the stream may be cut anywhere. */
    dec->ctx->flags2 |= AV_CODEC_FLAG2_CHUNKS;
    if (avcodec_open2(dec->ctx, codec, NULL) < 0) {
        avcodec_free_context(&dec->ctx);
        free(dec);
        return NULL;
    }
    dec->pkt   = av_packet_alloc();
    dec->frame = av_frame_alloc();
    dec->out   = av_frame_alloc();
    return dec;
}

int aos_hal_h264_decode(aos_h264_t *dec, const uint8_t *nal, int len, aos_h264_pic_t *pic)
{
    if (!dec || !nal || len <= 0 || !pic) {
        return -1;
    }
    if (av_new_packet(dec->pkt, len) < 0) {
        return -1;
    }
    memcpy(dec->pkt->data, nal, (size_t)len);
    int r = avcodec_send_packet(dec->ctx, dec->pkt);
    av_packet_unref(dec->pkt);
    if (r < 0 && r != AVERROR(EAGAIN)) {
        return -1;
    }
    int got = 0;
    while (avcodec_receive_frame(dec->ctx, dec->frame) == 0) {
        /* Keep the newest; a converted copy in plain I420 so the planes the
         * app sees look like tinyh264's (the app reads strides anyway). */
        av_frame_unref(dec->out);
        av_frame_move_ref(dec->out, dec->frame);
        got = 1;
    }
    if (!got) {
        return 0;
    }
    AVFrame *f = dec->out;
    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
        return -1;
    }
    pic->y = f->data[0];
    pic->u = f->data[1];
    pic->v = f->data[2];
    pic->width     = f->width;
    pic->height    = f->height;
    pic->stride_y  = f->linesize[0];
    pic->stride_uv = f->linesize[1];
    return 1;
}

void aos_hal_h264_close(aos_h264_t *dec)
{
    if (!dec) {
        return;
    }
    av_frame_free(&dec->frame);
    av_frame_free(&dec->out);
    av_packet_free(&dec->pkt);
    avcodec_free_context(&dec->ctx);
    free(dec);
}

/* ---- esp_new_jpeg's decoder, the subset apps use ---------------------------- */

#ifdef SIM_JPEG

typedef struct {
    jpeg_dec_config_t cfg;
    AVCodecContext   *ctx;
    AVPacket         *pkt;
    AVFrame          *frame;
    struct SwsContext *sws;
    int               out_w, out_h;
} sim_jpeg_t;

void *jpeg_calloc_align(size_t size, int aligned)
{
    void *p = NULL;
    if (posix_memalign(&p, aligned < (int)sizeof(void *) ? sizeof(void *) : (size_t)aligned, size) != 0) {
        return NULL;
    }
    memset(p, 0, size);
    return p;
}

void jpeg_free_align(void *data)
{
    free(data);
}

jpeg_error_t jpeg_dec_open(jpeg_dec_config_t *config, jpeg_dec_handle_t *jpeg_dec)
{
    if (!config || !jpeg_dec) {
        return JPEG_ERR_INVALID_PARAM;
    }
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    sim_jpeg_t *j = calloc(1, sizeof(*j));
    if (!codec || !j) {
        free(j);
        return JPEG_ERR_NO_MEM;
    }
    j->cfg = *config;
    j->ctx = avcodec_alloc_context3(codec);
    j->ctx->thread_count = 1;
    if (avcodec_open2(j->ctx, codec, NULL) < 0) {
        avcodec_free_context(&j->ctx);
        free(j);
        return JPEG_ERR_FAIL;
    }
    j->pkt   = av_packet_alloc();
    j->frame = av_frame_alloc();
    *jpeg_dec = j;
    return JPEG_ERR_OK;
}

/* The board parses the header here and decodes in process(); libavcodec
 * does both at once, so the decode happens here and process() converts. */
jpeg_error_t jpeg_dec_parse_header(jpeg_dec_handle_t jpeg_dec, jpeg_dec_io_t *io,
                                   jpeg_dec_header_info_t *out_info)
{
    sim_jpeg_t *j = jpeg_dec;
    if (!j || !io || !io->inbuf || io->inbuf_len <= 0) {
        return JPEG_ERR_INVALID_PARAM;
    }
    if (av_new_packet(j->pkt, io->inbuf_len) < 0) {
        return JPEG_ERR_NO_MEM;
    }
    memcpy(j->pkt->data, io->inbuf, (size_t)io->inbuf_len);
    int r = avcodec_send_packet(j->ctx, j->pkt);
    av_packet_unref(j->pkt);
    if (r < 0 || avcodec_receive_frame(j->ctx, j->frame) < 0) {
        return JPEG_ERR_BAD_DATA;
    }
    j->out_w = j->cfg.scale.width  ? j->cfg.scale.width  : j->frame->width;
    j->out_h = j->cfg.scale.height ? j->cfg.scale.height : j->frame->height;
    if (out_info) {
        out_info->width  = (uint16_t)j->frame->width;
        out_info->height = (uint16_t)j->frame->height;
    }
    io->inbuf_remain = 0;
    return JPEG_ERR_OK;
}

jpeg_error_t jpeg_dec_get_outbuf_len(jpeg_dec_handle_t jpeg_dec, int *outbuf_len)
{
    sim_jpeg_t *j = jpeg_dec;
    if (!j || !outbuf_len) {
        return JPEG_ERR_INVALID_PARAM;
    }
    int bpp = j->cfg.output_type == JPEG_PIXEL_FORMAT_RGB888 ? 3 : 2;
    *outbuf_len = j->out_w * j->out_h * bpp;
    return JPEG_ERR_OK;
}

jpeg_error_t jpeg_dec_process(jpeg_dec_handle_t jpeg_dec, jpeg_dec_io_t *io)
{
    sim_jpeg_t *j = jpeg_dec;
    if (!j || !io || !io->outbuf || !j->frame->data[0]) {
        return JPEG_ERR_INVALID_PARAM;
    }
    enum AVPixelFormat dst_fmt =
        j->cfg.output_type == JPEG_PIXEL_FORMAT_RGB565_BE ? AV_PIX_FMT_RGB565BE :
        j->cfg.output_type == JPEG_PIXEL_FORMAT_RGB565_LE ? AV_PIX_FMT_RGB565LE : AV_PIX_FMT_RGB24;
    j->sws = sws_getCachedContext(j->sws, j->frame->width, j->frame->height, j->frame->format,
                                  j->out_w, j->out_h, dst_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!j->sws) {
        return JPEG_ERR_FAIL;
    }
    uint8_t *dst[4] = { io->outbuf };
    int dst_stride[4] = { j->out_w * (dst_fmt == AV_PIX_FMT_RGB24 ? 3 : 2) };
    sws_scale(j->sws, (const uint8_t * const *)j->frame->data, j->frame->linesize, 0,
              j->frame->height, dst, dst_stride);
    io->out_size = dst_stride[0] * j->out_h;
    return JPEG_ERR_OK;
}

jpeg_error_t jpeg_dec_close(jpeg_dec_handle_t jpeg_dec)
{
    sim_jpeg_t *j = jpeg_dec;
    if (!j) {
        return JPEG_ERR_OK;
    }
    sws_freeContext(j->sws);
    av_frame_free(&j->frame);
    av_packet_free(&j->pkt);
    avcodec_free_context(&j->ctx);
    free(j);
    return JPEG_ERR_OK;
}

#endif /* SIM_JPEG */
