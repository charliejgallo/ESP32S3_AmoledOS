/*
 * AmoledOS - Cameras: YUV and RGB565 to the panel (cam_conv.h).
 *
 * The colour maths is tables: each chroma sample becomes three offsets (red,
 * green, blue), and each output pixel is three table reads that are already
 * shifted into place and byte-swapped for the panel, OR-ed together. The
 * clamp is in the tables too (index = Y + offset, from -256 to 511).
 *
 * Where things live matters more than the arithmetic on this board, so:
 * the tables and the column map are ONE block of internal RAM (the .so's own
 * data may sit in PSRAM, where every miss is a trip over the octal bus that
 * the decoder and the panel's DMA are also using), and the output goes into
 * the UI's strips, which are internal too. Only the source is read from
 * PSRAM, row by row.
 *
 * Nearest neighbour and not bilinear: the source is always bigger than the
 * panel (704 or 640 wide into 368), and at those ratios a filter costs a
 * third of the budget to soften what the panel's density already hides.
 */
/* The .so is compiled with -Os and per-file options do not reach that
 * compile (apps/turbo/main/tb_gfx.c found it): the pixel loops want -O2. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif
#include "cam_conv.h"

#include <stdlib.h>
#include <string.h>

#if !defined(AOS_SIM)
#include "esp_heap_caps.h"
#endif

#define PANEL_MAX 448

typedef struct {
    int16_t  rv[256], gu[256], gv[256], bu[256];
    uint16_t r[768], g[768], b[768];            /* index = value + 256 */
    uint16_t xmap[PANEL_MAX];
} tables_t;

static tables_t *s_t;

static inline uint16_t be(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

bool cam_conv_init(void)
{
    if (s_t) {
        return true;
    }
#if !defined(AOS_SIM)
    s_t = heap_caps_malloc(sizeof(*s_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
    if (!s_t) {
        s_t = malloc(sizeof(*s_t));
    }
    if (!s_t) {
        return false;
    }
    for (int i = 0; i < 256; i++) {
        int c = i - 128;
        s_t->rv[i] = (int16_t)((359 * c) >> 8);
        s_t->gu[i] = (int16_t)((88 * c) >> 8);
        s_t->gv[i] = (int16_t)((183 * c) >> 8);
        s_t->bu[i] = (int16_t)((454 * c) >> 8);
    }
    for (int i = 0; i < 768; i++) {
        int v = i - 256;
        uint16_t c = (uint16_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        s_t->r[i] = be((uint16_t)((c & 0xF8) << 8));
        s_t->g[i] = be((uint16_t)((c & 0xFC) << 3));
        s_t->b[i] = be((uint16_t)(c >> 3));
    }
    return true;
}

void cam_conv_free(void)
{
#if !defined(AOS_SIM)
    heap_caps_free(s_t);
#else
    free(s_t);
#endif
    s_t = NULL;
}

void cam_conv_prepare(const cam_geom_t *g)
{
    int ow = g->ow > PANEL_MAX ? PANEL_MAX : g->ow;
    for (int x = 0; x < ow; x++) {
        s_t->xmap[x] = (uint16_t)(g->sx + (x * g->sw + g->sw / 2) / g->ow);
    }
}

void cam_conv_i420_rows(const aos_h264_pic_t *pic, const cam_geom_t *g,
                        int row0, int rows, uint16_t *out)
{
    const tables_t *t = s_t;
    const uint16_t *R = t->r + 256, *G = t->g + 256, *B = t->b + 256;
    const uint16_t *xm = t->xmap;
    int ow = g->ow > PANEL_MAX ? PANEL_MAX : g->ow;
    for (int y = row0; y < row0 + rows; y++) {
        int sy = g->sy + (y * g->sh + g->sh / 2) / g->oh;
        const uint8_t *ry = pic->y + sy * pic->stride_y;
        const uint8_t *ru = pic->u + (sy >> 1) * pic->stride_uv;
        const uint8_t *rv = pic->v + (sy >> 1) * pic->stride_uv;
        uint16_t *o = out + (y - row0) * g->ow;
        for (int x = 0; x < ow; x++) {
            int sx = xm[x];
            int Y = ry[sx];
            int u = ru[sx >> 1];
            int v = rv[sx >> 1];
            o[x] = R[Y + t->rv[v]] | G[Y - t->gu[u] - t->gv[v]] | B[Y + t->bu[u]];
        }
    }
}

void cam_conv_rgb565_rows(const uint16_t *src, int src_w, const cam_geom_t *g,
                          int row0, int rows, uint16_t *out)
{
    const uint16_t *xm = s_t->xmap;
    int ow = g->ow > PANEL_MAX ? PANEL_MAX : g->ow;
    bool straight = g->sw == g->ow;
    for (int y = row0; y < row0 + rows; y++) {
        int sy = g->sy + (y * g->sh + g->sh / 2) / g->oh;
        const uint16_t *row = src + sy * src_w;
        uint16_t *o = out + (y - row0) * g->ow;
        if (straight) {
            memcpy(o, row + g->sx, (size_t)ow * 2);
        } else {
            for (int x = 0; x < ow; x++) {
                o[x] = row[xm[x]];
            }
        }
    }
}
