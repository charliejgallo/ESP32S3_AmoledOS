/*
 * TOPOS - the sprites
 *
 * Every sprite is rendered ONCE, when the app opens, into a small RGB565
 * image with a colour key; after that drawing one is a keyed copy. The same
 * thing gemas does with its jewels, and for the same reason: the shading is
 * worked out per pixel (a mole body is an ellipse lit from the upper left,
 * quantised into four tones with an outline), and doing that per frame would
 * be wasted work. Faces, paws, stars and icons are hand-drawn ASCII, which is
 * where a pixel has to be exactly where it is.
 *
 * About 60 KB of PSRAM, allocated with malloc() and freed on close.
 */
#pragma once

#include "tp_pixel.h"

#include <stdbool.h>
#include <stdint.h>

#define TP_KEY          0xF81F      /* pure magenta: transparent             */

typedef struct {
    uint16_t *px;
    int16_t   w, h;
    int16_t   ax, ay;               /* the pixel that lands on (x,y)         */
} tp_img_t;

/* Mole geometry, relative to the head's top centre (see TP_HEAD_TOP). */
#define TP_BODY_W       27
#define TP_BODY_PAD     3           /* rows of tuft above the head           */
#define TP_SQUASH_DY    5           /* a whacked mole's head, this much lower */

/* Faces */
enum {
    EX_NORMAL = 0,  /* mischievous                                            */
    EX_LEFT,        /* glancing                                               */
    EX_RIGHT,
    EX_BLINK,
    EX_WIDE,        /* startled                                               */
    EX_HAPPY,       /* ^ ^ taunting                                           */
    EX_DIZZY,       /* X X                                                    */
    EX_SMUG,        /* half-lidded, the hard hat's                            */
    EX_ANGRY,       /* hat gone, and furious about it                         */
    EX_COUNT
};

enum {
    MO_TEETH = 0,   /* buck teeth                                             */
    MO_O,           /* small "o"                                              */
    MO_OPEN,        /* whacked                                                */
    MO_TONGUE,      /* nyah                                                   */
    MO_SMIRK,
    MO_GRIT,
    MO_COUNT
};

#define TP_HELMET_FRAMES    8
#define TP_SPARK_FRAMES     3
#define TP_MALLET_FRAMES    3       /* 0 raised, 1 halfway, 2 on target      */
#define TP_TWINKLE_FRAMES   2

enum {
    IMG_BODY = 0,
    IMG_BODY_SQ,
    IMG_BODY_GOLD,
    IMG_BODY_GOLD_SQ,
    IMG_EYES0,
    IMG_MOUTH0      = IMG_EYES0 + EX_COUNT,
    IMG_PAW_L       = IMG_MOUTH0 + MO_COUNT,
    IMG_PAW_R,
    IMG_HELMET0,
    IMG_BOMB        = IMG_HELMET0 + TP_HELMET_FRAMES,
    IMG_BOMB_HOT,
    IMG_BOMB_DUD,
    IMG_SPARK0,
    IMG_MALLET0     = IMG_SPARK0 + TP_SPARK_FRAMES,
    IMG_STAR        = IMG_MALLET0 + TP_MALLET_FRAMES,
    IMG_STAR_SMALL,
    IMG_TWINKLE0,
    IMG_SWEAT       = IMG_TWINKLE0 + TP_TWINKLE_FRAMES,
    IMG_HEART,
    IMG_HEART_EMPTY,
    IMG_CLOCK,
    TP_IMG_COUNT
};

/* Renders everything. false if memory ran out (then tp_art_free()). */
bool tp_art_init(void);
void tp_art_free(void);

const tp_img_t *tp_img(int id);

/* Draws image 'id' with its anchor at (x,y), at an integer scale. Honours the
 * buffer's clip and lip. */
void tp_img_draw(tp_buf_t *b, int id, int x, int y, int scale);
/* The rectangle that same call covers. */
void tp_img_rect(int id, int x, int y, int scale, tp_rect_t *out);

/* Palette of the hand-drawn sprites and of a few procedural bits, so the
 * drawing code speaks the same colours. */
uint16_t tp_pal(char ch);
