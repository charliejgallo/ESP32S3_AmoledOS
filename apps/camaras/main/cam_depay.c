/*
 * AmoledOS - Cameras: RTP depacketisers (cam_depay.h says what and why).
 */
#include "cam_depay.h"

#include "aos_hal.h"

#include <stdlib.h>
#include <string.h>

#define NAL_CAP     (512 * 1024)    /* a 704x576 keyframe is ~30 KB; a busy 720p one 150 */
#define JPEG_CAP    (512 * 1024)    /* 640x480 MJPEG measured at ~44 KB a frame */
#define JPEG_HDR    1024            /* room in front of the scan for the rebuilt headers */

bool cam_depay_init(cam_depay_t *d, cam_view_t *view, cam_codec_t codec, int clock_hz)
{
    memset(d, 0, sizeof(*d));
    d->view = view;
    d->codec = codec;
    d->clock_hz = clock_hz > 0 ? clock_hz : 90000;
    d->cap = codec == CAM_CODEC_JPEG ? JPEG_CAP + JPEG_HDR : NAL_CAP;
    d->buf = malloc((size_t)d->cap);
    return d->buf != NULL;
}

void cam_depay_free(cam_depay_t *d)
{
    free(d->buf);
    d->buf = NULL;
}

static int64_t pts_ms(cam_depay_t *d, uint32_t ts)
{
    if (!d->have_ts) {
        d->have_ts = true;
        d->last_ts = ts;
        d->ext_ts = 0;
    } else {
        d->ext_ts += (int32_t)(ts - d->last_ts);
        d->last_ts = ts;
    }
    return d->ext_ts * 1000 / d->clock_hz;
}

/* ---- H.264 ---------------------------------------------------------------- */

static void nal_emit(cam_depay_t *d, const uint8_t *nal, int len, int64_t pts)
{
    if (len <= 0 || len + 4 > d->cap) {
        return;
    }
    d->buf[0] = 0;
    d->buf[1] = 0;
    d->buf[2] = 0;
    d->buf[3] = 1;
    memcpy(d->buf + 4, nal, (size_t)len);
    cam_view_nal(d->view, d->buf, len + 4, pts);
}

static void h264_payload(cam_depay_t *d, const uint8_t *p, int n, int64_t pts)
{
    if (n < 1) {
        return;
    }
    int type = p[0] & 0x1F;
    if (type >= 1 && type <= 23) {
        d->len = 0;
        nal_emit(d, p, n, pts);
    } else if (type == 24) {                    /* STAP-A */
        d->len = 0;
        int i = 1;
        while (i + 2 <= n) {
            int sz = (p[i] << 8) | p[i + 1];
            i += 2;
            if (sz <= 0 || i + sz > n) {
                break;
            }
            nal_emit(d, p + i, sz, pts);
            i += sz;
        }
    } else if (type == 28) {                    /* FU-A */
        if (n < 2) {
            return;
        }
        bool start = p[1] & 0x80;
        bool end   = p[1] & 0x40;
        if (start) {
            d->broken = false;
            d->buf[0] = 0;
            d->buf[1] = 0;
            d->buf[2] = 0;
            d->buf[3] = 1;
            d->buf[4] = (uint8_t)((p[0] & 0xE0) | (p[1] & 0x1F));
            d->len = 5;
        } else if (d->len == 0) {
            return;                             /* joined in the middle */
        }
        if (d->broken || d->len + (n - 2) > d->cap) {
            d->broken = true;
            d->len = 0;
            return;
        }
        memcpy(d->buf + d->len, p + 2, (size_t)(n - 2));
        d->len += n - 2;
        if (end) {
            cam_view_nal(d->view, d->buf, d->len, pts);
            d->len = 0;
        }
    }
    /* STAP-B, MTAP and FU-B are for interleaved mode, which no camera
     * negotiates unless asked. */
}

/* ---- JPEG (RFC 2435) -------------------------------------------------------- */

static const uint8_t LUMA_Q[64] = {
    16, 11, 12, 14, 12, 10, 16, 14, 13, 14, 18, 17, 16, 19, 24, 40,
    26, 24, 22, 22, 24, 49, 35, 37, 29, 40, 58, 51, 61, 60, 57, 51,
    56, 55, 64, 72, 92, 78, 64, 68, 87, 69, 55, 56, 80, 109, 81, 87,
    95, 98, 103, 104, 103, 62, 77, 113, 121, 112, 100, 120, 92, 101, 103, 99,
};
static const uint8_t CHROMA_Q[64] = {
    17, 18, 18, 24, 21, 24, 47, 26, 26, 47, 99, 66, 56, 66, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

static const uint8_t LUM_DC_BITS[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t LUM_DC_VALS[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t LUM_AC_BITS[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t LUM_AC_VALS[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};
static const uint8_t CHM_DC_BITS[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t CHM_DC_VALS[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t CHM_AC_BITS[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
static const uint8_t CHM_AC_VALS[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
};

/* RFC 2435 appendix A: the tables for a quality factor Q of 1..99. */
static void make_tables(int q, uint8_t *lqt, uint8_t *cqt)
{
    int factor = q < 1 ? 1 : q > 99 ? 99 : q;
    int scale = q < 50 ? 5000 / factor : 200 - factor * 2;
    for (int i = 0; i < 64; i++) {
        int lq = (LUMA_Q[i] * scale + 50) / 100;
        int cq = (CHROMA_Q[i] * scale + 50) / 100;
        lqt[i] = (uint8_t)(lq < 1 ? 1 : lq > 255 ? 255 : lq);
        cqt[i] = (uint8_t)(cq < 1 ? 1 : cq > 255 ? 255 : cq);
    }
}

static uint8_t *put_dht(uint8_t *p, const uint8_t *bits, const uint8_t *vals, int nvals, int cls_id)
{
    int len = 3 + 16 + nvals;
    *p++ = 0xFF; *p++ = 0xC4;
    *p++ = (uint8_t)(len >> 8); *p++ = (uint8_t)len;
    *p++ = (uint8_t)cls_id;
    memcpy(p, bits, 16);
    p += 16;
    memcpy(p, vals, (size_t)nvals);
    return p + nvals;
}

/* RFC 2435 appendix B, MakeHeaders(). Returns the length written. */
static int make_headers(uint8_t *out, int type, int w, int h,
                        const uint8_t *lqt, const uint8_t *cqt, int dri)
{
    uint8_t *p = out;
    *p++ = 0xFF; *p++ = 0xD8;                               /* SOI */
    for (int t = 0; t < 2; t++) {                           /* DQT, 8-bit */
        *p++ = 0xFF; *p++ = 0xDB; *p++ = 0; *p++ = 67;
        *p++ = (uint8_t)t;
        memcpy(p, t ? cqt : lqt, 64);
        p += 64;
    }
    if (dri) {                                              /* DRI */
        *p++ = 0xFF; *p++ = 0xDD; *p++ = 0; *p++ = 4;
        *p++ = (uint8_t)(dri >> 8); *p++ = (uint8_t)dri;
    }
    *p++ = 0xFF; *p++ = 0xC0; *p++ = 0; *p++ = 17;          /* SOF0 */
    *p++ = 8;
    *p++ = (uint8_t)(h >> 8); *p++ = (uint8_t)h;
    *p++ = (uint8_t)(w >> 8); *p++ = (uint8_t)w;
    *p++ = 3;
    *p++ = 1; *p++ = (type & 1) ? 0x22 : 0x21; *p++ = 0;    /* Y: 4:2:0 or 4:2:2 */
    *p++ = 2; *p++ = 0x11; *p++ = 1;
    *p++ = 3; *p++ = 0x11; *p++ = 1;
    p = put_dht(p, LUM_DC_BITS, LUM_DC_VALS, sizeof(LUM_DC_VALS), 0x00);
    p = put_dht(p, LUM_AC_BITS, LUM_AC_VALS, sizeof(LUM_AC_VALS), 0x10);
    p = put_dht(p, CHM_DC_BITS, CHM_DC_VALS, sizeof(CHM_DC_VALS), 0x01);
    p = put_dht(p, CHM_AC_BITS, CHM_AC_VALS, sizeof(CHM_AC_VALS), 0x11);
    *p++ = 0xFF; *p++ = 0xDA; *p++ = 0; *p++ = 12;          /* SOS */
    *p++ = 3;
    *p++ = 1; *p++ = 0x00;
    *p++ = 2; *p++ = 0x11;
    *p++ = 3; *p++ = 0x11;
    *p++ = 0; *p++ = 63; *p++ = 0;
    return (int)(p - out);
}

static void jpeg_payload(cam_depay_t *d, const uint8_t *p, int n, bool marker, uint32_t ts)
{
    if (n < 8) {
        return;
    }
    int off  = (p[1] << 16) | (p[2] << 8) | p[3];
    int type = p[4];
    int q    = p[5];
    int w    = p[6] * 8;
    int h    = p[7] * 8;
    const uint8_t *q_p = p + 8;
    int left = n - 8;
    int dri = 0;
    if (type >= 64 && type <= 127) {                        /* restart marker header */
        if (left < 4) {
            return;
        }
        dri = (q_p[0] << 8) | q_p[1];
        q_p += 4;
        left -= 4;
    }
    if (off == 0) {
        /* A new frame. Whatever was open and did not end is lost. */
        d->jpeg_open = true;
        d->broken = false;
        d->jpeg_ts = ts;
        d->len = 0;
        if (q >= 128) {
            if (left < 4) {
                return;
            }
            int qlen = (q_p[2] << 8) | q_p[3];
            q_p += 4;
            left -= 4;
            if (qlen > 0) {
                if (qlen > left || qlen > (int)sizeof(d->qt)) {
                    d->jpeg_open = false;
                    return;
                }
                memcpy(d->qt, q_p, (size_t)qlen);
                d->qt_len = qlen;
                q_p += qlen;
                left -= qlen;
            }
        }
    }
    if (!d->jpeg_open || ts != d->jpeg_ts || d->broken) {
        return;
    }
    if (off != d->len || JPEG_HDR + off + left + 2 > d->cap) {
        d->broken = true;                                   /* a hole: drop the frame */
        return;
    }
    memcpy(d->buf + JPEG_HDR + off, q_p, (size_t)left);
    d->len = off + left;
    if (!marker) {
        return;
    }

    /* The last packet: headers in front, EOI behind, and out it goes. */
    d->jpeg_open = false;
    uint8_t lqt[64], cqt[64];
    if (q >= 128) {
        if (d->qt_len < 128) {
            return;                                         /* tables never came */
        }
        memcpy(lqt, d->qt, 64);
        memcpy(cqt, d->qt + 64, 64);
    } else {
        make_tables(q, lqt, cqt);
    }
    uint8_t hdr[JPEG_HDR];
    int hl = make_headers(hdr, type & 0x3F, w, h, lqt, cqt, dri);
    uint8_t *start = d->buf + JPEG_HDR - hl;
    memcpy(start, hdr, (size_t)hl);
    int total = hl + d->len;
    if (d->len < 2 || start[total - 2] != 0xFF || start[total - 1] != 0xD9) {
        start[total++] = 0xFF;
        start[total++] = 0xD9;
    }
    cam_view_jpeg(d->view, start, total, pts_ms(d, ts));
}

/* ---- RTP ------------------------------------------------------------------ */

void cam_depay_packet(cam_depay_t *d, const uint8_t *rtp, int len)
{
    if (len < 12 || (rtp[0] >> 6) != 2) {
        return;
    }
    int cc = rtp[0] & 0x0F;
    bool pad = rtp[0] & 0x20;
    bool ext = rtp[0] & 0x10;
    bool marker = rtp[1] & 0x80;
    uint16_t seq = (uint16_t)((rtp[2] << 8) | rtp[3]);
    /* Byte by byte in a loop, not as one expression: gcc reads four shifted
     * bytes as a byte swap, Xtensa has no instruction for it, and the call
     * goes to libgcc's __bswapsi2, which the firmware does not lend
     * (APP-GUIDE, "A libgcc helper you write yourself can call itself"). */
    uint32_t ts = 0;
    for (int k = 4; k < 8; k++) {
        ts = (ts << 8) | rtp[k];
    }
    int hl = 12 + 4 * cc;
    if (ext) {
        if (len < hl + 4) {
            return;
        }
        hl += 4 + 4 * ((rtp[hl + 2] << 8) | rtp[hl + 3]);
    }
    int n = len - hl;
    if (pad && n > 0) {
        n -= rtp[len - 1];
    }
    if (n <= 0) {
        return;
    }
    if (d->have_seq && seq != d->next_seq) {
        d->broken = true;           /* a gap: the unit in progress is gone */
        if (d->codec == CAM_CODEC_H264) {
            d->len = 0;
        }
    }
    d->have_seq = true;
    d->next_seq = (uint16_t)(seq + 1);

    if (d->codec == CAM_CODEC_H264) {
        h264_payload(d, rtp + hl, n, pts_ms(d, ts));
    } else if (d->codec == CAM_CODEC_JPEG) {
        jpeg_payload(d, rtp + hl, n, marker, ts);
    }
}
