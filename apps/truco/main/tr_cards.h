/*
 * TRUCO - the cards, drawn in code
 *
 * There are no bitmaps: each card is drawn with LVGL's primitives onto a
 * canvas of its own, once, when it is dealt. After that the card is an object
 * that moves; LVGL redraws only the area it passed over.
 *
 * The canvas is ARGB8888 and not RGB565 for a concrete reason: the cards have
 * rounded corners and a shadow, and both need the green baize underneath to
 * show through. With RGB565 the background would have to be painted inside
 * each card and made to match the baize's gradient, which is exactly the kind
 * of seam that shows. It costs 4 bytes per pixel in PSRAM (31 KB per card) and
 * there is plenty of that.
 */
#pragma once

#include "lvgl.h"

#define TR_CARD_W   62
#define TR_CARD_H   96
#define TR_PAD      7                       /* room for the shadow */
#define TR_CV_W     (TR_CARD_W + TR_PAD * 2)
#define TR_CV_H     (TR_CARD_H + TR_PAD * 2)

/* Creates a card's canvas (with its buffer in PSRAM) inside 'parent'. */
lv_obj_t *tr_card_canvas(lv_obj_t *parent, void **buf_out);

/* Draws card 'card' (0..39) or the back if card < 0. */
void tr_card_render(lv_obj_t *canvas, int card);

/* The matchstick scoreboard. 'buf' has to be TR_SCORE_W x TR_SCORE_H in
 * ARGB8888. It draws both scores, from 0 to 30. */
#define TR_SCORE_W  368
#define TR_SCORE_H  66

void tr_score_render(lv_obj_t *canvas, int nos, int ellos, int mano_yo, int valor);
