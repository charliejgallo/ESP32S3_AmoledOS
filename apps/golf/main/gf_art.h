/*
 * GOLF - the art rendered in Blender: the golfer and the props
 *
 * Everything comes from one file, golf.pak (tools/pack_assets.py), read when
 * the app opens:
 *
 *   - the TREES and the flag, baked in colour: seen from the side for the 3D
 *     view and from above (with their shadow) for the map, each with mip
 *     levels so a tree 10 px wide is filtered and not sampled;
 *   - the GOLFER, as a lighting pass plus a region id per pixel
 *     (tools/blender/SPEC.md). gf_art colours each frame with the outfit's
 *     palette the first time it is needed and keeps it, until the outfit
 *     changes. Hats are layers of their own composited on top.
 *
 * Frames are positioned in the 368x448 swing camera's frame: drawing one at
 * its (x0, y0) puts the golfer exactly beside the ball the 3D view draws.
 */
#pragma once

#include "gf_gfx.h"
#include "gf_outfit.h"
#include "gf_world.h"

#include <stdbool.h>
#include <stdint.h>

enum { SEQ_SWING = 0, SEQ_IDLE, SEQ_CHEER, SEQ_SAD, SEQ_TURN, SEQ_N };

#define GF_SWING_TOP     10     /* last frame of the backswing               */
#define GF_SWING_IMPACT  14

typedef struct gf_tree_art gf_tree_art_t;

/* Reads the pack. false if the file is missing or broken: the game still
 * runs, with trees drawn by code and no golfer. */
bool gf_art_load(const char *path);
void gf_art_free(void);
bool gf_art_have_golfer(void);

/* trees */
const gf_tree_art_t *gf_art_trees(void);
bool  gf_art_tree_top(const gf_tree_art_t *a, int kind);
/* ppm: screen pixels per metre; size: the tree's scale (1 = normal) */
void  gf_art_tree_draw_top(const gf_tree_art_t *a, gf_img_t *im, int kind, float cx, float cy, float ppm, float size, int tint);
void  gf_art_tree_shadow(const gf_tree_art_t *a, gf_img_t *im, int kind, float cx, float cy, float ppm, float size);
const gf_mip_t *gf_art_tree_side(int kind);
const gf_mip_t *gf_art_flag(int frame);         /* 4 frames, NULL if none  */
/* the art's own size of a kind, metres: the world uses it for collisions */
float gf_art_tree_height(int kind);
float gf_art_tree_radius(int kind);

/* the golfer */
int   gf_art_frames(int seq);
/* Recolours with a new outfit (drops every frame coloured so far). */
void  gf_art_outfit(const uint8_t eq[CAT_N]);
/* A coloured frame and where its top-left goes on the screen. NULL if the
 * frame does not exist or is not coloured yet (gf_art_prepare). */
const gf_sprite_t *gf_art_golfer(int seq, int frame, int *x0, int *y0);
/* the ground shadow of a frame: alpha only */
const gf_sprite_t *gf_art_golfer_shadow(int seq, int frame, int *x0, int *y0);
/* Colours (and decodes the shadows of) the sequences in mask, 1 << SEQ_*.
 * From the worker only: it reads the card. Returns the bytes they hold. */
size_t gf_art_prepare(unsigned mask);
/* the sequences fully coloured so far (1 << SEQ_*): the UI may draw those
 * while the worker is still colouring the rest */
unsigned gf_art_ready(void);
/* frees the coloured frames of one sequence (the shop's turntable) */
void  gf_art_release(int seq);
