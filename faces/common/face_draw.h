/*
 * AmoledOS - shapes for the watchfaces that draw a scene (v0.16.0).
 *
 * Soft-edged ellipses, thick lines and triangles into a half-resolution
 * buffer of face_common.h (184x224 RGB565). Header-only, like face_common.h
 * and for the same reason. Everything clips to the buffer; coordinates are
 * floats in half-resolution pixels, so a shape can move by less than one.
 *
 * These are what the Pet, the Aquarium and the Landscape are made of: a
 * character built from a dozen ellipses can squash, lean and blink, where a
 * sprite can only be swapped for another sprite (Claudito's lesson).
 */
#pragma once

#include "face_common.h"

static inline uint16_t fd_lerp(uint16_t a, uint16_t b, float t)
{
    return fc_mix(a, b, (uint32_t)(LV_CLAMP(0.0f, t, 1.0f) * 255));
}

static inline uint16_t fd_light(uint16_t c, float t)
{
    return fd_lerp(c, 0xFFFF, t);
}

static inline uint16_t fd_dark(uint16_t c, float t)
{
    return fd_lerp(c, 0x0000, t);
}

static inline void fd_px(uint16_t *b, int x, int y, uint16_t c, uint32_t a)
{
    if (x >= 0 && y >= 0 && x < FC_SW && y < FC_SH) {
        uint16_t *p = &b[y * FC_SW + x];
        *p = fc_mix(*p, c, a);
    }
}

/* An ellipse turned by 'ang' radians, 'alpha' 0..255, its edge one pixel soft. */
static inline void fd_ellipse(uint16_t *b, float cx, float cy, float rx, float ry,
                              float ang, uint16_t c, uint32_t alpha)
{
    if (rx < 0.3f || ry < 0.3f) {
        return;
    }
    float cs = cosf(ang), sn = sinf(ang);
    float r = rx > ry ? rx : ry;
    int x0 = LV_MAX(0, (int)(cx - r - 1)), x1 = LV_MIN(FC_SW - 1, (int)(cx + r + 1));
    int y0 = LV_MAX(0, (int)(cy - r - 1)), y1 = LV_MIN(FC_SH - 1, (int)(cy + r + 1));
    float small = rx < ry ? rx : ry;
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx;
            float lx = (dx * cs + dy * sn) / rx, ly = (-dx * sn + dy * cs) / ry;
            float d = lx * lx + ly * ly;
            if (d >= 1.3f) {
                continue;
            }
            /* how far inside the edge, in pixels, roughly */
            float in = (1.0f - sqrtf(d)) * small + 0.5f;
            if (in <= 0) {
                continue;
            }
            uint32_t a = in >= 1.0f ? alpha : (uint32_t)(in * alpha);
            uint16_t *p = &b[y * FC_SW + x];
            *p = fc_mix(*p, c, a);
        }
    }
}

static inline void fd_circle(uint16_t *b, float cx, float cy, float r, uint16_t c, uint32_t alpha)
{
    fd_ellipse(b, cx, cy, r, r, 0, c, alpha);
}

/* A line 'w' wide with round ends. */
static inline void fd_line(uint16_t *b, float x0, float y0, float x1, float y1,
                           float w, uint16_t c, uint32_t alpha)
{
    float hw = w * 0.5f;
    int ax = LV_MAX(0, (int)(LV_MIN(x0, x1) - hw - 1)), bx = LV_MIN(FC_SW - 1, (int)(LV_MAX(x0, x1) + hw + 1));
    int ay = LV_MAX(0, (int)(LV_MIN(y0, y1) - hw - 1)), by = LV_MIN(FC_SH - 1, (int)(LV_MAX(y0, y1) + hw + 1));
    float vx = x1 - x0, vy = y1 - y0;
    float len2 = vx * vx + vy * vy;
    for (int y = ay; y <= by; y++) {
        for (int x = ax; x <= bx; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = len2 > 1e-4f ? (px * vx + py * vy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - t * vx, dy = py - t * vy;
            float in = hw - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (in <= 0) {
                continue;
            }
            uint16_t *p = &b[y * FC_SW + x];
            *p = fc_mix(*p, c, in >= 1.0f ? alpha : (uint32_t)(in * alpha));
        }
    }
}

static inline void fd_tri(uint16_t *b, float x0, float y0, float x1, float y1,
                          float x2, float y2, uint16_t c, uint32_t alpha)
{
    int ax = LV_MAX(0, (int)LV_MIN(x0, LV_MIN(x1, x2))), bx = LV_MIN(FC_SW - 1, (int)LV_MAX(x0, LV_MAX(x1, x2)) + 1);
    int ay = LV_MAX(0, (int)LV_MIN(y0, LV_MIN(y1, y2))), by = LV_MIN(FC_SH - 1, (int)LV_MAX(y0, LV_MAX(y1, y2)) + 1);
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (area > -1e-3f && area < 1e-3f) {
        return;
    }
    float sgn = area < 0 ? -1.0f : 1.0f;
    for (int y = ay; y <= by; y++) {
        for (int x = ax; x <= bx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sgn;
            float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sgn;
            float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sgn;
            if (e0 >= 0 && e1 >= 0 && e2 >= 0) {
                uint16_t *p = &b[y * FC_SW + x];
                *p = fc_mix(*p, c, alpha);
            }
        }
    }
}

static inline void fd_rect(uint16_t *b, int x0, int y0, int x1, int y1, uint16_t c, uint32_t alpha)
{
    x0 = LV_MAX(0, x0); y0 = LV_MAX(0, y0);
    x1 = LV_MIN(FC_SW, x1); y1 = LV_MIN(FC_SH, y1);
    for (int y = y0; y < y1; y++) {
        uint16_t *p = &b[y * FC_SW + x0];
        for (int x = x0; x < x1; x++, p++) {
            *p = fc_mix(*p, c, alpha);
        }
    }
}

/* Shows what changed between 'work' (the frame just drawn) and what is on
 * the screen: copies the rows that differ into fc->small and presents their
 * bounding box. A frame where nothing moved presents nothing. */
static inline void fd_show(fc_t *fc, const uint16_t *work)
{
    int dx0 = FC_SW, dy0 = FC_SH, dx1 = -1, dy1 = -1;
    for (int y = 0; y < FC_SH; y++) {
        uint16_t *dst = fc->small + y * FC_SW;
        const uint16_t *src = work + y * FC_SW;
        if (memcmp(dst, src, FC_SW * sizeof(uint16_t)) == 0) {
            continue;
        }
        int a = 0, b = FC_SW - 1;
        while (dst[a] == src[a]) a++;
        while (dst[b] == src[b]) b--;
        memcpy(dst + a, src + a, (size_t)(b - a + 1) * sizeof(uint16_t));
        if (a < dx0) dx0 = a;
        if (b > dx1) dx1 = b;
        if (y < dy0) dy0 = y;
        dy1 = y;
    }
    if (dx1 >= 0) {
        fc_present(fc, dx0, dy0, dx1 + 1, dy1 + 1);
    }
}
