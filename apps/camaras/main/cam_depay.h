/*
 * AmoledOS - Cameras: RTP packets back into what the decoders eat.
 *
 * H.264 (RFC 6184): single NAL units, STAP-A aggregates and FU-A fragments
 * become one Annex-B NAL unit each (00 00 00 01 + NAL), handed on as soon as
 * it is whole. A fragment that arrives after a gap is dropped with the rest
 * of its NAL: the decoder resynchronises by itself at the next keyframe.
 *
 * JPEG (RFC 2435): the camera sends only the entropy-coded scan, plus a
 * type, a quality factor and sometimes the quantisation tables. The JPEG
 * headers the decoder needs (DQT, SOF0, DHT, SOS) are rebuilt here from the
 * RFC's reference tables, exactly as the RFC's appendix does.
 */
#pragma once

#include "cam.h"

typedef struct {
    cam_view_t  *view;
    cam_codec_t  codec;
    int          clock_hz;

    uint8_t     *buf;           /* one NAL, or one JPEG with room for its header */
    int          cap;
    int          len;

    bool         have_seq;
    uint16_t     next_seq;
    bool         broken;        /* lost a packet in the middle of a unit */

    bool         have_ts;
    uint32_t     last_ts;
    int64_t      ext_ts;        /* RTP clock, extended past the 32-bit wrap */

    /* JPEG */
    uint32_t     jpeg_ts;       /* the frame being assembled */
    bool         jpeg_open;
    uint8_t      qt[128];       /* in-band tables, when Q >= 128 */
    int          qt_len;
} cam_depay_t;

bool cam_depay_init(cam_depay_t *d, cam_view_t *view, cam_codec_t codec, int clock_hz);
void cam_depay_free(cam_depay_t *d);

/* One RTP packet, header included. */
void cam_depay_packet(cam_depay_t *d, const uint8_t *rtp, int len);
