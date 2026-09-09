/*
 * Claudito - pixel art engine
 *
 * Everything visible in the app is drawn into small RGB565 buffers that LVGL
 * then stretches x4 with nearest-neighbour (lv_image_set_inner_align with
 * STRETCH and antialiasing off). The screen is 368x448, so the art lives on a
 * 92x112 grid: each art pixel is 4x4 real pixels.
 *
 * The buffers are opaque: there is no alpha channel, things are drawn back to
 * front and that is that. The sprites are written as ASCII art and '.' means
 * "do not touch".
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Art grid. 92*4 = 368, 112*4 = 448. */
#define CL_SCALE        4
#define CL_ART_W        92
#define CL_ART_H        112

#define CL_HUD_H        17
#define CL_STAGE_H      78
#define CL_BAR_H        17
#define CL_STAGE_Y      CL_HUD_H            /* global row where the stage starts */
#define CL_BAR_Y        (CL_HUD_H + CL_STAGE_H)

typedef struct {
    uint16_t *px;
    int16_t   w;
    int16_t   h;
} cl_buf_t;

/* 0xRRGGBB -> RGB565 (the canvas's and the panel's native format) */
static inline uint16_t cl_rgb(uint32_t hex)
{
    return (uint16_t)(((hex >> 19) & 0x1F) << 11 |
                      ((hex >> 10) & 0x3F) << 5  |
                      ((hex >> 3)  & 0x1F));
}

/* Palette of the ASCII sprites. Returns false if the character is
 * transparent. */
bool cl_pal(char ch, uint16_t *out);

/* ---- primitives ---------------------------------------------------------- */

void cl_px(cl_buf_t *b, int x, int y, uint16_t c);
void cl_fill(cl_buf_t *b, uint16_t c);
void cl_copy(cl_buf_t *dst, const cl_buf_t *src);
void cl_rect(cl_buf_t *b, int x, int y, int w, int h, uint16_t c);
void cl_frame(cl_buf_t *b, int x, int y, int w, int h, uint16_t c);
void cl_hline(cl_buf_t *b, int x, int y, int len, uint16_t c);
void cl_vline(cl_buf_t *b, int x, int y, int len, uint16_t c);
void cl_disc(cl_buf_t *b, int cx, int cy, int r, uint16_t c);
void cl_ring(cl_buf_t *b, int cx, int cy, int r, uint16_t c);
/* rectangle with all four corners bitten off: the style's basic shape */
void cl_round(cl_buf_t *b, int x, int y, int w, int h, int cut, uint16_t c);
/* flat blend: darkens (f<0) or lightens (f>0) an area, in sixteenths */
void cl_shade(cl_buf_t *b, int x, int y, int w, int h, int f);

/* ---- ASCII sprites ------------------------------------------------------- */

#define CL_SPRITE(a)    (a), (int)(sizeof(a) / sizeof((a)[0]))

void cl_blit(cl_buf_t *b, int x, int y, const char *const *rows, int nrows, bool flip);
/* the same but painting everything one colour, for shadows or silhouettes */
void cl_blit_solid(cl_buf_t *b, int x, int y, const char *const *rows, int nrows,
                   bool flip, uint16_t c);

/* ---- text (our own 5x7 font) --------------------------------------------- */

#define CL_CH_W         5
#define CL_CH_H         7
#define CL_CH_ADV       6                   /* advance per character, including the space */

int  cl_text_w(const char *s);
void cl_text(cl_buf_t *b, int x, int y, const char *s, uint16_t c);
/* with a hard 1 px shadow bottom right: it reads over any background */
void cl_text_sh(cl_buf_t *b, int x, int y, const char *s, uint16_t c, uint16_t sh);
void cl_text_center(cl_buf_t *b, int cx, int y, const char *s, uint16_t c, uint16_t sh);
