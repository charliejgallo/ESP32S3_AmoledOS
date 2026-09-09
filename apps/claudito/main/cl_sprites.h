/*
 * Claudito - object sprites
 *
 * ASCII art, one character per colour from cl_pixel.c's palette. The '.' paints
 * nothing. Every row of a sprite has to be the same length: cl_blit() mirrors
 * using each row's length.
 */
#pragma once

#include "cl_pixel.h"

/* food */
extern const char *const cl_spr_apple[11];
extern const char *const cl_spr_pizza[11];
extern const char *const cl_spr_cookie[11];
extern const char *const cl_spr_cake[11];

/* toys */
extern const char *const cl_spr_ball[11];
extern const char *const cl_spr_balloon[11];
extern const char *const cl_spr_dice[9];
extern const char *const cl_spr_bubbles[11];

/* tools */
extern const char *const cl_spr_sponge[11];
extern const char *const cl_spr_hand[9];
extern const char *const cl_spr_moon[9];
extern const char *const cl_spr_door[11];
extern const char *const cl_spr_tree_icon[11];

/* scene ornaments */
extern const char *const cl_spr_plant[16];
extern const char *const cl_spr_flower[6];
extern const char *const cl_spr_butterfly[7];
extern const char *const cl_spr_bowl[7];
extern const char *const cl_spr_lamp[13];
extern const char *const cl_spr_bone[5];
