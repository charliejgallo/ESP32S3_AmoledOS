/*
 * AmoledOS simulator - AAC through libavcodec, behind aos_aac.h.
 *
 * The board decodes with Espressif's prebuilt esp_audio_codec, which has no
 * build for the Mac; this is the same three calls on ffmpeg's decoder, the
 * way sim_codec.c stands in for the H.264 and JPEG ones. libavcodec reads
 * the ADTS header at the front of each packet itself, and does SBR and PS,
 * so an HE-AAC station sounds here as it does on the watch.
 */
#include "aos_aac.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

struct aos_aac {
    AVCodecContext *ctx;
    AVPacket       *pkt;
    AVFrame        *frame;
};

aos_aac_t *aos_aac_open(void)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    aos_aac_t *d = calloc(1, sizeof(*d));
    if (!codec || !d) {
        free(d);
        return NULL;
    }
    d->ctx = avcodec_alloc_context3(codec);
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->ctx || !d->pkt || !d->frame || avcodec_open2(d->ctx, codec, NULL) < 0) {
        aos_aac_close(d);
        return NULL;
    }
    return d;
}

static int16_t clip16(float v)
{
    int s = (int)(v * 32767.0f);
    return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
}

int aos_aac_decode(aos_aac_t *d, const uint8_t *frame, int len, int16_t *pcm,
                   uint32_t *rate, uint8_t *channels)
{
    d->pkt->data = (uint8_t *)frame;
    d->pkt->size = len;
    if (avcodec_send_packet(d->ctx, d->pkt) < 0) {
        return -1;
    }
    int n = 0;
    while (avcodec_receive_frame(d->ctx, d->frame) == 0) {
        AVFrame *f = d->frame;
        int ch = f->ch_layout.nb_channels > 2 ? 2 : f->ch_layout.nb_channels;
        int take = f->nb_samples;
        if ((n + take) * ch > 4096) {
            take = 4096 / ch - n;
        }
        for (int i = 0; i < take; i++) {
            for (int c = 0; c < ch; c++) {
                float v;
                if (f->format == AV_SAMPLE_FMT_FLTP) {
                    v = ((const float *)f->extended_data[c])[i];
                } else if (f->format == AV_SAMPLE_FMT_S16P) {
                    v = ((const int16_t *)f->extended_data[c])[i] / 32768.0f;
                } else if (f->format == AV_SAMPLE_FMT_S16) {
                    v = ((const int16_t *)f->data[0])[i * f->ch_layout.nb_channels + c] / 32768.0f;
                } else {
                    v = ((const float *)f->data[0])[i * f->ch_layout.nb_channels + c];
                }
                pcm[(n + i) * ch + c] = clip16(v);
            }
        }
        n += take;
        *rate = (uint32_t)f->sample_rate;
        *channels = (uint8_t)ch;
    }
    return n;
}

void aos_aac_close(aos_aac_t *d)
{
    if (!d) {
        return;
    }
    av_frame_free(&d->frame);
    av_packet_free(&d->pkt);
    avcodec_free_context(&d->ctx);
    free(d);
}
