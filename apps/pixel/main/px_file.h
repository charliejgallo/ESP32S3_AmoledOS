/*
 * PIXEL ART - the document and its file format.
 *
 * No LVGL, no HAL: this file and px_export.c compile with a bare `cc` on the
 * Mac (see tools/px_harness.c), which is how the format and the encoders are
 * verified without the board.
 *
 * A document ("lienzo") is a stack of up to 16 frames of 8x8 or 16x16 cells.
 * Each cell is an index into a 32-colour palette; index 0 is black, which on
 * the AMOLED is a pixel switched off. Nothing is stored as RGB: a 16x16 frame
 * is 256 bytes, so the biggest possible document is 4 KB and a thumbnail costs
 * nothing to read.
 *
 * The file (.pix) is what the watch writes and what the web portal reads and
 * writes back. It is deliberately self-describing -it carries its palette-
 * so that a file made anywhere else with different colours still opens: the
 * loader maps a foreign palette onto ours by nearest colour.
 *
 *     0   "PIX1"
 *     4   size          8 or 16
 *     5   frames        1..16
 *     6   ncolors       32 (any 1..256 is accepted on read)
 *     7   flags         0, reserved
 *     8   delay_ms      u16 little endian, per frame, for the animation
 *    10   reserved      u16, 0
 *    12   palette       ncolors * 3 bytes, RGB888
 *    ...  pixels        frames * size * size bytes, row-major, one index each
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PX_MAX_SIZE     16
#define PX_MAX_FRAMES   16
#define PX_COLORS       32
#define PX_SLOTS        8                   /* documents the app keeps */
#define PX_CELLS        (PX_MAX_SIZE * PX_MAX_SIZE)

#define PX_DELAY_MIN    50
#define PX_DELAY_MAX    2000
#define PX_DELAY_DEF    200

typedef struct {
    uint8_t  size;                          /* 8 or 16                      */
    uint8_t  frames;                        /* 1..PX_MAX_FRAMES             */
    uint16_t delay_ms;
    uint8_t  px[PX_MAX_FRAMES][PX_CELLS];   /* frame f, cell y*size+x       */
} px_doc_t;

/* The palette, RGB888. Index 0 is black. */
extern const uint8_t px_palette[PX_COLORS][3];

uint16_t px_rgb565(int idx);                /* palette index -> RGB565      */
int      px_nearest(uint8_t r, uint8_t g, uint8_t b);   /* -> palette index */

void px_doc_init(px_doc_t *d, int size);
bool px_doc_load(px_doc_t *d, const char *path);
bool px_doc_save(const px_doc_t *d, const char *path);

/* Reads only the header: size and frame count, without the pixels. For the
 * gallery, which has eight files to look at. */
bool px_doc_peek(const char *path, int *size, int *frames);

/* Frame operations. All of them keep the document consistent; the ones that
 * insert return the index of the new frame, or -1 when there is no room. */
int  px_doc_frame_dup(px_doc_t *d, int at);         /* copy of 'at', after it */
int  px_doc_frame_blank(px_doc_t *d, int at);       /* black frame after 'at' */
bool px_doc_frame_delete(px_doc_t *d, int at);      /* false if it is the last */

/* Four-neighbour flood fill on one frame. */
void px_doc_fill(px_doc_t *d, int frame, int x, int y, uint8_t color);
