/*
 * MILA - the casita: Mila's home, the hub (DESIGN.md section 6)
 *
 * A worker scene at 1.5 x the levels' scale (tools/blender/SPEC.md section
 * 6). The room is one picture drawn once into a full-screen cache (colour +
 * depth); Mila, the furniture and the toys are sprites depth-tested against
 * it. No bars, nothing to keep up: she wanders on her own, uses the toys the
 * player bought, chases what the finger drags, and answers a pet.
 *
 * Its buttons (play, shop, settings, a friend) are drawn in the frame.
 */
#pragma once

#include "ml_gfx.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct app app_t;

bool mlc_open(app_t *a);            /* worker                                */
void mlc_close(app_t *a);           /* worker (or at exit)                   */
void mlc_step(app_t *a, float dt);  /* worker                                */
void mlc_band(app_t *a, ml_img_t *im, int y0, int y1);
void mlc_touch(app_t *a, int code, int x, int y);   /* LVGL thread          */
/* the friend's Mila is visiting (ml_link.c): her outfit, or NULL to leave */
void mlc_guest(app_t *a, const char *hat, uint32_t hat_col, const char *neck, uint32_t neck_col, bool on);
