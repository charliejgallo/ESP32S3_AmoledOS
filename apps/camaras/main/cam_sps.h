/*
 * AmoledOS - Cameras: the few things the app needs from an H.264 SPS.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int profile_idc;        /* 66 baseline, 77 main, 100 high */
    int constraint;         /* constraint_set flags, bit 7 = set0 */
    int level_idc;
    int ref_frames;
    int coded_w, coded_h;   /* in whole macroblocks, what the decoder allocates */
    int width, height;      /* after the frame cropping */
} cam_sps_t;

/* nal points at the NAL header byte (after the start code). */
bool cam_sps_parse(const uint8_t *nal, int len, cam_sps_t *out);
bool cam_sps_baseline(const cam_sps_t *s);
