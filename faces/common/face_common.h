/*
 * AmoledOS - what the animated watchfaces of faces/ share (v0.15.0).
 *
 * Header-only on purpose: every face is a module of its own, and a static
 * inline in each costs less than a shared library the loader does not have.
 *
 * The drawing model, the one 2043 and Arkanos measured on the board:
 *
 *   - the scene is drawn at HALF resolution, 184x224, into 'small';
 *   - fc_present() doubles a rectangle of it by hand into 'big', the canvas'
 *     368x448 RGB565 buffer, writing 32 bits at a time, and pushes it
 *     straight to the panel when nothing is over the face; LVGL copies it
 *     flat otherwise (letting LVGL stretch the canvas cost 129 ms
 *     a frame, 7 fps);
 *   - the text (the time, the date) is NOT part of the scene: it is a mask
 *     at full resolution, blended over 'big' in the same pass, so the digits
 *     stay sharp while the water or the sand moves behind them;
 *   - only the rectangle that changed is presented and invalidated. A frame
 *     where nothing moved draws nothing, which is what lets an animated face
 *     sit on the wrist without eating the battery.
 */
#pragma once

#include "lvgl.h"
#include "aos_theme.h"
#include "aos_hal.h"
#include "aos_watchface.h"

#ifndef AOS_SIM_BUILTIN
#include "esp_heap_caps.h"
#endif

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FC_W        368
#define FC_H        448
#define FC_SW       (FC_W / 2)
#define FC_SH       (FC_H / 2)
#define FC_MASKS    4
#define FC_STRIP    20      /* rows the panel takes at a time (AOS_DRAW_ROWS) */
#define FC_PI       3.14159265f

static inline uint16_t fc_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t fc_hex(uint32_t rgb)
{
    return fc_rgb((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
}

/* a over b, a in 0..255 */
static inline uint16_t fc_mix(uint16_t b, uint16_t a_col, uint32_t a)
{
    if (a == 0) {
        return b;
    }
    if (a >= 255) {
        return a_col;
    }
    uint32_t rb = b & 0xF81F, gb = b & 0x07E0;
    uint32_t ra = a_col & 0xF81F, ga = a_col & 0x07E0;
    uint32_t k = (a + 4) >> 3;                  /* 0..32 */
    uint32_t r = (rb + (((ra - rb) * k) >> 5)) & 0xF81F;
    uint32_t g = (gb + (((ga - gb) * k) >> 5)) & 0x07E0;
    return (uint16_t)(r | g);
}

/* colour scaled by v (0..255): for darkening */
static inline uint16_t fc_scale(uint16_t c, uint32_t v)
{
    return fc_mix(0, c, v);
}

/* A piece of text rendered once (when it changes) into an alpha mask at the
 * screen's resolution. */
typedef struct {
    uint8_t  *a;            /* w*h alpha, NULL when empty */
    int16_t   x, y, w, h;   /* where on the screen */
    uint16_t  color;
    bool      shadow;       /* a dark halo, for text over a bright scene */
} fc_mask_t;

typedef struct {
    lv_obj_t  *canvas;
    uint16_t  *small;       /* FC_SW x FC_SH, the scene */
    uint16_t  *big;         /* FC_W x FC_H, the canvas */
    uint16_t  *strip[2];    /* FC_STRIP rows each, big-endian, in PSRAM: what goes
                             * to the panel, one being sent, one being made */
    uint8_t    flip;        /* which strip is next */
    bool       no_blit;     /* the panel refused (the simulator): through LVGL */
    uint8_t    stale[FC_H]; /* rows that went to the panel only: 'big' is behind */
    bool       any_stale;
    fc_mask_t  mask[FC_MASKS];
} fc_t;

static inline void fc_draw_cb(lv_event_t *e);

static inline bool fc_init(fc_t *fc, lv_obj_t *root)
{
    memset(fc, 0, sizeof(*fc));
    fc->small = calloc(FC_SW * FC_SH, sizeof(uint16_t));
    fc->big   = calloc(FC_W * FC_H, sizeof(uint16_t));
    if (!fc->small || !fc->big) {
        free(fc->small);
        free(fc->big);
        fc->small = fc->big = NULL;
        return false;
    }
    /* The two strips, 29 KB, in PSRAM like everything else a face keeps:
     * internal RAM is the scarce one here (the first version of this had
     * them there, for the DMA to send without a copy, and they were moved).
     * The SPI driver copies each strip through its own bounce buffer as it
     * goes, which is what LVGL's flush already costs. Without them (no room)
     * the face goes through LVGL, slower. */
#ifndef AOS_SIM_BUILTIN
    for (int i = 0; i < 2; i++) {
        fc->strip[i] = heap_caps_malloc(FC_W * FC_STRIP * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if (!fc->strip[0] || !fc->strip[1]) {
        heap_caps_free(fc->strip[0]);
        heap_caps_free(fc->strip[1]);
        fc->strip[0] = fc->strip[1] = NULL;
        fc->no_blit = true;
    }
#else
    fc->no_blit = true;         /* the simulator has no panel to push to */
#endif
    fc->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(fc->canvas, fc->big, FC_W, FC_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(fc->canvas, FC_W, FC_H);
    lv_obj_set_pos(fc->canvas, 0, 0);
    lv_image_set_antialias(fc->canvas, false);
    lv_obj_remove_flag(fc->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(fc->canvas, fc_draw_cb, LV_EVENT_DRAW_MAIN_BEGIN, fc);
    return true;
}

static inline void fc_mask_free(fc_mask_t *m)
{
    free(m->a);
    m->a = NULL;
    m->w = m->h = 0;
}

static inline void fc_deinit(fc_t *fc)
{
    for (int i = 0; i < FC_MASKS; i++) {
        fc_mask_free(&fc->mask[i]);
    }
    /* the canvas goes with the face's root; its buffer is ours */
    free(fc->small);
    free(fc->big);
#ifndef AOS_SIM_BUILTIN
    if (fc->strip[0]) {
        /* the last strip pushed may still be on its way to the panel (the
         * HAL queues it and returns): 14.7 KB take under a millisecond, and
         * the modules have no sleep to call, so this waits it out */
        uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
        while ((uint32_t)aos_hal_uptime_ms() - t0 < 3) {
        }
        heap_caps_free(fc->strip[0]);
        heap_caps_free(fc->strip[1]);
    }
#endif
    fc->strip[0] = fc->strip[1] = NULL;
    fc->small = fc->big = NULL;
}

static inline uint16_t fc_swap(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

/* One row of the screen: 'small' doubled, with the text over it, written to
 * 't' (the row's first pixel) between bx0 and bx1, byte-swapped for the
 * panel or not for the canvas. */
static inline void fc_row(const fc_t *fc, int sy, int bx0, int bx1, uint16_t *t, bool swap)
{
    const uint16_t *s = fc->small + (sy >> 1) * FC_SW;
    uint32_t *t32 = (uint32_t *)(void *)(t + bx0);
    if (swap) {
        for (int x = bx0 >> 1; x < bx1 >> 1; x++) {
            uint32_t c = fc_swap(s[x]);
            *t32++ = c | (c << 16);
        }
    } else {
        for (int x = bx0 >> 1; x < bx1 >> 1; x++) {
            uint32_t c = s[x];
            *t32++ = c | (c << 16);
        }
    }
    /* then the text, only where a mask has something on this row: the
     * pixels it leaves alone are already right */
    for (int i = 0; i < FC_MASKS; i++) {
        const fc_mask_t *m = &fc->mask[i];
        if (!m->a || sy < m->y || sy >= m->y + m->h) {
            continue;
        }
        int mx0 = LV_MAX(bx0, m->x), mx1 = LV_MIN(bx1, m->x + m->w);
        const uint8_t *a = m->a + (sy - m->y) * m->w + (mx0 - m->x);
        for (int x = mx0; x < mx1; x++, a++) {
            uint32_t v = *a;
            if (!v) {
                continue;
            }
            /* what is under it now: an earlier mask may have drawn there */
            uint16_t c = swap ? fc_swap(t[x]) : t[x];
            if (m->shadow) {
                /* the low half of the alpha is the halo, the high half the
                 * glyph: see fc_mask_text() */
                if (v < 128) {
                    c = fc_mix(c, 0, v * 3 / 2);
                    t[x] = swap ? fc_swap(c) : c;
                    continue;
                }
                v = (v - 128) * 2 + 1;
            }
            c = fc_mix(c, m->color, v);
            t[x] = swap ? fc_swap(c) : c;
        }
    }
}

/* Brings the canvas up to date with what went to the panel: every row marked
 * stale is made again, from 'small' and the masks, into 'big'. */
static inline void fc_sync(fc_t *fc)
{
    for (int y = 0; y < FC_H; y++) {
        if (fc->stale[y]) {
            fc_row(fc, y, 0, FC_W, fc->big + y * FC_W, false);
            fc->stale[y] = 0;
        }
    }
    fc->any_stale = false;
}

/* LVGL is about to draw the canvas (an overlay left, the face came back from
 * the launcher or the control centre): whatever went only to the panel is
 * made in the canvas first, or LVGL would paint frames that are long gone. */
static inline void fc_draw_cb(lv_event_t *e)
{
    fc_t *fc = (fc_t *)lv_event_get_user_data(e);
    if (fc && fc->any_stale) {
        fc_sync(fc);
    }
}

/* Doubles small[x0..x1) x [y0..y1) (half-resolution coordinates), blends the
 * masks over it, and shows it.
 *
 * When nothing is over the face (aos_watchface_can_blit) it goes straight to
 * the panel, a strip of FC_STRIP rows at a time: each strip is made in one of
 * two small buffers (PSRAM) while the other is still on its way, so making
 * and sending overlap. The rows sent are marked, and made again in
 * the canvas just before LVGL next draws it (fc_draw_cb). Otherwise the rows
 * are made in the canvas and invalidated for LVGL.
 *
 * What this replaced, measured on the board: a full-screen canvas through
 * LVGL, ~95 ms a frame (the Kaleidoscope at 9-10 fps); then the whole frame
 * built in a second PSRAM buffer and pushed, ~45 ms (15-16 fps; the Water
 * with the charger's bubbles, 14). */
static inline void fc_present(fc_t *fc, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > FC_SW) x1 = FC_SW;
    if (y1 > FC_SH) y1 = FC_SH;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    int bx0 = 2 * x0, by0 = 2 * y0, bx1 = 2 * x1, by1 = 2 * y1;

    if (!fc->no_blit && aos_watchface_can_blit()) {
        int sy = by0;
        for (; sy < by1; sy += FC_STRIP) {
            int rows = LV_MIN(FC_STRIP, by1 - sy);
            uint16_t *buf = fc->strip[fc->flip ^= 1];
            /* the panel wants whole rows */
            for (int r = 0; r < rows; r++) {
                fc_row(fc, sy + r, 0, FC_W, buf + r * FC_W, true);
            }
            if (!aos_hal_display_blit(0, sy, FC_W, rows, buf)) {
                break;
            }
            memset(fc->stale + sy, 1, (size_t)rows);
            fc->any_stale = true;
        }
        if (sy >= by1) {
            return;
        }
        by0 = sy;               /* what the panel did not take goes by LVGL */
    }
    if (fc->any_stale) {
        fc_sync(fc);            /* the columns outside the band must be current */
    }
    for (int sy = by0; sy < by1; sy++) {
        fc_row(fc, sy, bx0, bx1, fc->big + sy * FC_W, false);
    }
    lv_area_t area = { bx0, by0, bx1 - 1, by1 - 1 };
    lv_obj_invalidate_area(fc->canvas, &area);
}

static inline void fc_present_all(fc_t *fc)
{
    fc_present(fc, 0, 0, FC_SW, FC_SH);
}

/* Re-presents the area of a mask (in half-resolution units, rounded out). */
static inline void fc_present_mask(fc_t *fc, const fc_mask_t *m)
{
    if (m->w > 0) {
        fc_present(fc, m->x / 2, m->y / 2, (m->x + m->w + 1) / 2, (m->y + m->h + 1) / 2);
    }
}

/* Renders 'text' with 'font', scaled by 'scale' (1.0 = the font's size),
 * into 'm', centred on (cx, cy) of the screen. LVGL draws it once into a
 * transparent ARGB8888 canvas at the font's size; the scaling is bilinear,
 * done here, once per change of the text (a minute, for the time). With
 * 'shadow' a soft dark halo is folded into the same mask. 'scratch' is any
 * object of the face: the canvas is made under it and deleted before
 * returning. */
static inline bool fc_mask_text(fc_mask_t *m, lv_obj_t *scratch, const char *text,
                                const lv_font_t *font, float scale, int cx, int cy,
                                uint16_t color, bool shadow)
{
    fc_mask_free(m);
    m->color = color;
    m->shadow = shadow;
    if (!text || !*text) {
        return true;
    }
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int sw = size.x + 4, sh = size.y + 2;
    lv_draw_buf_t *buf = lv_draw_buf_create(sw, sh, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!buf) {
        return false;
    }
    lv_obj_t *canvas = lv_canvas_create(scratch);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(canvas, buf);
    lv_canvas_fill_bg(canvas, lv_color_hex(0), LV_OPA_TRANSP);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = font;
    dsc.color = lv_color_hex(0xFFFFFF);
    dsc.text = text;
    lv_area_t area = { 2, 1, sw - 1, sh - 1 };
    lv_draw_label(&layer, &dsc, &area);
    lv_canvas_finish_layer(canvas, &layer);

    int pad = shadow ? (int)(6 * scale) : 0;
    int w = (int)(sw * scale) + 2 * pad, h = (int)(sh * scale) + 2 * pad;
    uint8_t *a = calloc((size_t)w * h, 1);
    if (!a) {
        lv_obj_delete(canvas);
        lv_draw_buf_destroy(buf);
        return false;
    }
    const uint8_t *src = buf->data;
    uint32_t stride = buf->header.stride;
    float inv = 1.0f / scale;
    for (int y = 0; y < h - 2 * pad; y++) {
        float fy = (y + 0.5f) * inv - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        for (int x = 0; x < w - 2 * pad; x++) {
            float fx = (x + 0.5f) * inv - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            float acc = 0;
            for (int j = 0; j < 2; j++) {
                int yy = y0 + j;
                if (yy < 0 || yy >= sh) continue;
                float wy = j ? ty : 1 - ty;
                for (int i = 0; i < 2; i++) {
                    int xx = x0 + i;
                    if (xx < 0 || xx >= sw) continue;
                    float wx = i ? tx : 1 - tx;
                    acc += wx * wy * src[yy * stride + xx * 4 + 3];
                }
            }
            a[(y + pad) * w + x + pad] = (uint8_t)(acc > 255 ? 255 : acc);
        }
    }
    lv_obj_delete(canvas);
    lv_draw_buf_destroy(buf);

    if (shadow) {
        /* halo: the glyph blurred (a box blur, twice, each way) and pushed
         * up, under it. The glyph keeps 128..255, the halo 0..127. */
        uint8_t *tmp = malloc((size_t)w * h);
        uint8_t *halo = malloc((size_t)w * h);
        if (tmp && halo) {
            memcpy(halo, a, (size_t)w * h);
            int r = pad / 2 > 0 ? pad / 2 : 1;
            for (int pass = 0; pass < 2; pass++) {
                for (int y = 0; y < h; y++) {           /* rows: halo -> tmp */
                    uint32_t sum = 0;
                    const uint8_t *row = halo + y * w;
                    for (int x = -r; x < w + r; x++) {
                        if (x + r < w) sum += row[x + r];
                        if (x - r - 1 >= 0) sum -= row[x - r - 1];
                        if (x >= 0 && x < w) tmp[y * w + x] = (uint8_t)(sum / (2 * r + 1));
                    }
                }
                for (int x = 0; x < w; x++) {           /* columns: tmp -> halo */
                    uint32_t sum = 0;
                    for (int y = -r; y < h + r; y++) {
                        if (y + r < h) sum += tmp[(y + r) * w + x];
                        if (y - r - 1 >= 0) sum -= tmp[(y - r - 1) * w + x];
                        if (y >= 0 && y < h) halo[y * w + x] = (uint8_t)(sum / (2 * r + 1));
                    }
                }
            }
            for (int i = 0; i < w * h; i++) {
                uint32_t hv = (uint32_t)halo[i] * 3;
                a[i] = a[i] > 8 ? (uint8_t)(128 + a[i] / 2)
                                : (uint8_t)(hv > 127 ? 127 : hv);
            }
        }
        free(tmp);
        free(halo);
    }

    m->a = a;
    m->w = (int16_t)w;
    m->h = (int16_t)h;
    m->x = (int16_t)(cx - w / 2);
    m->y = (int16_t)(cy - h / 2);
    return true;
}

/* A small xorshift: the faces need noise, not cryptography, and the same
 * sequence on the board and in the simulator. */
static inline uint32_t fc_rand(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

static inline float fc_randf(uint32_t *s)
{
    return (float)(fc_rand(s) & 0xFFFFFF) / 16777216.0f;
}

/* Low-pass gravity, the screen's axes (aos_watchface_gravity), with a
 * fallback to "standing" when there is no IMU. */
typedef struct {
    float x, y, z;
    bool  primed;
} fc_grav_t;

static inline void fc_grav_update(fc_grav_t *g, float k)
{
    float x, y, z;
    if (!aos_watchface_gravity(&x, &y, &z)) {
        x = 0; y = 1; z = 0;
    }
    if (!g->primed) {
        g->x = x; g->y = y; g->z = z;
        g->primed = true;
        return;
    }
    g->x += (x - g->x) * k;
    g->y += (y - g->y) * k;
    g->z += (z - g->z) * k;
}
