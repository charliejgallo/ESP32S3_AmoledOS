/*
 * MAPAS - drawing into an RGB565 (big-endian, the panel's order) buffer:
 * filled polygons, anti-aliased thick lines and text from a glyph atlas.
 *
 * Nothing here touches LVGL, so all of it runs in the worker. The one LVGL
 * step, turning a font into glyph masks, is mp_font_bake(), called once from
 * create().
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
    uint16_t *px;
    int       w, h;                 /* the buffer */
    int       cx0, cy0, cx1, cy1;   /* the clip: [cx0, cx1) x [cy0, cy1) */
} mp_fb_t;

static inline uint16_t mp_be565(uint32_t rgb)
{
    uint16_t v = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    return (uint16_t)((v >> 8) | (v << 8));
}

void mp_fb_clip(mp_fb_t *fb, int x0, int y0, int x1, int y1);
void mp_fb_clip_all(mp_fb_t *fb);
void mp_fill_rect(mp_fb_t *fb, int x, int y, int w, int h, uint32_t rgb);

/* Blend one pixel (alpha 0..255). */
void mp_blend_px(mp_fb_t *fb, int x, int y, uint32_t rgb, int alpha);

/* Filled polygon, even-odd over all its rings (holes come out by themselves).
 * xy: 2 floats per point; ring_n: points per ring. The left and right end of
 * every span are blended by their coverage, which smooths the steep edges. */
void mp_fill_poly(mp_fb_t *fb, const float *xy, const uint32_t *ring_n, int nring, uint32_t rgb);

/* A polyline of width w (px), round joins and ends, anti-aliased. Widths
 * under one pixel are drawn one pixel wide and fainter. alpha 0..255. */
void mp_polyline(mp_fb_t *fb, const float *xy, int n, float w, uint32_t rgb, int alpha);

/* Filled disc with an anti-aliased edge. */
void mp_disc(mp_fb_t *fb, float cx, float cy, float r, uint32_t rgb, int alpha);

/* ---------------------------------------------------------------------------
 * Text
 * ------------------------------------------------------------------------- */

#define MP_GLYPH_FIRST 0x20
#define MP_GLYPH_LAST  0xFF
#define MP_GLYPHS      (MP_GLYPH_LAST - MP_GLYPH_FIRST + 1)
#define MP_HALO        2            /* px of halo around every glyph */

typedef struct {
    int16_t  adv;                   /* advance, px */
    int16_t  w, h;                  /* the masks, halo margin included */
    uint8_t *a;                     /* the glyph's coverage */
    uint8_t *halo;                  /* the same, grown by MP_HALO */
} mp_glyph_t;

typedef struct {
    mp_glyph_t g[MP_GLYPHS];
    uint8_t   *atlas;               /* every mask, one block */
    int        line_h;
    bool       ok;
} mp_font_t;

/* LVGL thread only. */
bool mp_font_bake(mp_font_t *f, const lv_font_t *font);
void mp_font_free(mp_font_t *f);

/* Next codepoint of a UTF-8 string; unknown ones come back as '?'. */
uint32_t mp_utf8_next(const char **s);

int mp_text_width(const mp_font_t *f, const char *s);

/* Horizontal text, its top-left corner at x, y; halo first. */
void mp_text(mp_fb_t *fb, const mp_font_t *f, int x, int y, const char *s,
             uint32_t rgb, uint32_t halo_rgb, bool halo);

/* One glyph centred at (cx, cy), turned by angle (radians, clockwise on the
 * screen). pass 0 draws its halo, pass 1 the glyph. */
void mp_glyph_rot(mp_fb_t *fb, const mp_font_t *f, uint32_t cp, float cx, float cy,
                  float angle, uint32_t rgb, int pass);
