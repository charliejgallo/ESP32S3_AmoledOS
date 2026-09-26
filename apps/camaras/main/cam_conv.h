/*
 * AmoledOS - Cameras: pictures to the panel's pixels.
 *
 * Both converters scale by nearest neighbour from a source rectangle (the
 * crop) to an output of ow x oh, a few rows at a time (the UI renders in
 * strips), and write RGB565 in the panel's byte order (big-endian), so the
 * result goes to aos_hal_display_blit() untouched. Only the UI's task calls
 * them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "aos_hal.h"

typedef struct {
    int sx, sy, sw, sh;         /* the crop, in source pixels */
    int ow, oh;                 /* the output */
} cam_geom_t;

bool cam_conv_init(void);       /* the tables, in internal RAM */
void cam_conv_free(void);

/* Once per picture, before its rows. */
void cam_conv_prepare(const cam_geom_t *g);

/* Output rows [row0, row0 + rows) into out (ow x rows). */
void cam_conv_i420_rows(const aos_h264_pic_t *pic, const cam_geom_t *g,
                        int row0, int rows, uint16_t *out);
void cam_conv_rgb565_rows(const uint16_t *src, int src_w, const cam_geom_t *g,
                          int row0, int rows, uint16_t *out);
