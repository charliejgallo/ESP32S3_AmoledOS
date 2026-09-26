/*
 * AmoledOS - Cameras: what an H.264 stream is, from its SPS.
 *
 * tinyh264 says nothing until it has decoded a picture, and some streams it
 * never decodes: a Main/High profile camera gets forty refused slices, a
 * 1080p one runs the decoder out of memory. Reading the sequence parameter
 * set first (ITU-T H.264 7.3.2.1.1) lets the app say which of the two it is,
 * and with what size, before any of that. It also gives the frame cropping
 * that tinyh264 ignores: 640x360 decodes as 640x368, and the view drops the
 * eight rows of padding.
 */
#include "cam_sps.h"

#include <string.h>

typedef struct {
    const uint8_t *p;
    int len;        /* bytes */
    int bit;        /* next bit to read */
} bits_t;

static int bit1(bits_t *b)
{
    if (b->bit >= b->len * 8) {
        return 0;
    }
    int v = (b->p[b->bit >> 3] >> (7 - (b->bit & 7))) & 1;
    b->bit++;
    return v;
}

static uint32_t bitsn(bits_t *b, int n)
{
    uint32_t v = 0;
    while (n--) {
        v = (v << 1) | (uint32_t)bit1(b);
    }
    return v;
}

static uint32_t ue(bits_t *b)
{
    int zeros = 0;
    while (!bit1(b) && zeros < 31) {
        zeros++;
    }
    return ((1u << zeros) - 1) + bitsn(b, zeros);
}

static int32_t se(bits_t *b)
{
    uint32_t k = ue(b);
    return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
}

static void skip_scaling_list(bits_t *b, int size)
{
    int last = 8, next = 8;
    for (int j = 0; j < size; j++) {
        if (next != 0) {
            next = (last + se(b) + 256) % 256;
        }
        last = next == 0 ? last : next;
    }
}

bool cam_sps_parse(const uint8_t *nal, int len, cam_sps_t *out)
{
    memset(out, 0, sizeof(*out));
    /* Emulation prevention bytes (00 00 03) out first, into a small copy:
     * everything this reads is in the first few dozen bytes. */
    uint8_t rbsp[96];
    int n = 0, zeros = 0;
    for (int i = 1; i < len && n < (int)sizeof(rbsp); i++) {    /* past the NAL header */
        if (zeros >= 2 && nal[i] == 3) {
            zeros = 0;
            continue;
        }
        zeros = nal[i] == 0 ? zeros + 1 : 0;
        rbsp[n++] = nal[i];
    }
    if (n < 4) {
        return false;
    }
    bits_t b = { rbsp, n, 0 };
    out->profile_idc = (int)bitsn(&b, 8);
    out->constraint = (int)bitsn(&b, 8);
    out->level_idc = (int)bitsn(&b, 8);
    ue(&b);                                         /* seq_parameter_set_id */
    int chroma_format_idc = 1;
    int p = out->profile_idc;
    if (p == 100 || p == 110 || p == 122 || p == 244 || p == 44 || p == 83 ||
        p == 86 || p == 118 || p == 128 || p == 138 || p == 139 || p == 134 || p == 135) {
        chroma_format_idc = (int)ue(&b);
        if (chroma_format_idc == 3) {
            bit1(&b);                               /* separate_colour_plane_flag */
        }
        ue(&b);                                     /* bit_depth_luma_minus8 */
        ue(&b);                                     /* bit_depth_chroma_minus8 */
        bit1(&b);                                   /* qpprime_y_zero_transform_bypass */
        if (bit1(&b)) {                             /* seq_scaling_matrix_present */
            int lists = chroma_format_idc != 3 ? 8 : 12;
            for (int i = 0; i < lists; i++) {
                if (bit1(&b)) {
                    skip_scaling_list(&b, i < 6 ? 16 : 64);
                }
            }
        }
    }
    ue(&b);                                         /* log2_max_frame_num_minus4 */
    uint32_t poc_type = ue(&b);
    if (poc_type == 0) {
        ue(&b);
    } else if (poc_type == 1) {
        bit1(&b);
        se(&b);
        se(&b);
        uint32_t cycle = ue(&b);
        for (uint32_t i = 0; i < cycle && i < 256; i++) {
            se(&b);
        }
    }
    out->ref_frames = (int)ue(&b);
    bit1(&b);                                       /* gaps_in_frame_num_allowed */
    int w_mbs = (int)ue(&b) + 1;
    int h_map = (int)ue(&b) + 1;
    int frame_mbs_only = bit1(&b);
    if (!frame_mbs_only) {
        bit1(&b);                                   /* mb_adaptive_frame_field */
    }
    bit1(&b);                                       /* direct_8x8_inference */
    int w = w_mbs * 16;
    int h = h_map * 16 * (2 - frame_mbs_only);
    out->coded_w = w;
    out->coded_h = h;
    if (bit1(&b)) {                                 /* frame_cropping_flag */
        int cl = (int)ue(&b), cr = (int)ue(&b), ct = (int)ue(&b), cb = (int)ue(&b);
        int cx = chroma_format_idc == 0 ? 1 : 2;
        int cy = (chroma_format_idc == 1 ? 2 : 1) * (2 - frame_mbs_only);
        w -= (cl + cr) * cx;
        h -= (ct + cb) * cy;
    }
    out->width = w;
    out->height = h;
    return w > 0 && h > 0 && w <= 8192 && h <= 8192;
}

bool cam_sps_baseline(const cam_sps_t *s)
{
    /* Baseline (66), or a stream that declares it obeys the baseline
     * constraints (constraint_set0_flag, the top bit). set1 means "obeys
     * Main's", which a Hikvision Main stream with CABAC also sets: the first
     * version read that bit and let Main through. */
    return s->profile_idc == 66 || (s->constraint & 0x80);
}
