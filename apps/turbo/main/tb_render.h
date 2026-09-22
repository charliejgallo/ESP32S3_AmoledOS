/*
 * TURBO - the pseudo-3D renderer
 *
 * The camera is 2 m up, 3.2 m behind the car, with no pitch: the horizon of
 * a flat road is row 150 and vertical lines stay vertical. The road is drawn
 * front to back, segment by segment, one screen row at a time: each row is a
 * run of ground, rumble, asphalt, rumble, ground, so every pixel below the
 * horizon is written once. A segment hidden by a hill in front is skipped
 * and remembers how far down it is clipped; the sprites standing on it (the
 * scenery, the traffic) are then drawn back to front under that clip. The
 * player's car is a fixed sprite on top, then the HUD.
 */
#pragma once

#include "tb_art.h"
#include "tb_game.h"
#include "tb_gfx.h"
#include "tb_track.h"

#define TB_F            300.0f      /* focal length, px                       */
#define TB_CX           184
#define TB_HOR          150         /* the horizon row of a flat road         */
#define TB_DRAW         170         /* segments drawn: 850 m                   */
#define TB_CAR_Y        337         /* where the car's rear wheels touch       */

typedef struct tb_render tb_render_t;

tb_render_t *tb_render_new(void);
void tb_render_free(tb_render_t *r);
/* a stage's colours and textures (after tb_art_load_stage) */
void tb_render_stage(tb_render_t *r, const tb_track_t *t);
/* the player's paint (the garage) */
void tb_render_paint(tb_render_t *r, const tb_paint_t *p, const tb_paint_t *rival);
/* one frame of the world: sky, road, scenery, traffic, the car */
void tb_render_world(tb_render_t *r, tb_img_t *im, const tb_game_t *g, float dt);
/* the same in two steps, for drawing in bands of internal RAM: prepare
 * once per frame, then each band with im clipped to its rows (and px
 * pointing so that row y is px + y * TB_W) */
void tb_render_prepare(tb_render_t *r, const tb_game_t *g, float dt);
void tb_render_band(tb_render_t *r, tb_img_t *im, const tb_game_t *g, int y0, int y1);
/* cycles per part of the frame, summed since zeroed (the caller zeroes and
 * reads them; turbo.c adds the prepare, the HUD and the copies) */
enum { TB_PROF_PREP = 0, TB_PROF_SKY, TB_PROF_ROAD, TB_PROF_PROPS, TB_PROF_TRAFFIC, TB_PROF_CAR,
       TB_PROF_HUD, TB_PROF_COPY, TB_PROF_STEP, TB_PROF_WAIT, TB_PROF_N };
uint32_t *tb_render_cycles(tb_render_t *r);
/* ms the parts took, for the log: sky, road, sprites, car */
void tb_render_prof(const tb_render_t *r, uint32_t out[4]);
