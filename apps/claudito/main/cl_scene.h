/*
 * Claudito - the two scenes
 *
 * The background is drawn once per scene into a separate buffer and then
 * copied onto the stage on every frame. Redrawing the parquet plank by plank
 * fifty times a second makes no sense at all when a 14 KB memcpy does the same
 * thing.
 */
#pragma once

#include "cl_pixel.h"

typedef enum {
    CL_SCENE_HOME = 0,      /* living room: parquet, window, plant, rug */
    CL_SCENE_PARK,          /* yard: grass, tree, fence, sun            */
    CL_SCENE_COUNT
} cl_scene_id_t;

/* Row of the stage where the feet rest, the same in both scenes so changing
 * setting does not move the critter. */
#define CL_FLOOR_Y      66

void cl_scene_draw(cl_buf_t *b, cl_scene_id_t scene);

/* What moves by itself (clouds, butterfly) goes on top of the already copied
 * background, in areas where it covers nothing: that way the background stays
 * a memcpy. */
void cl_scene_anim(cl_buf_t *b, cl_scene_id_t scene, int frame);

/* Night: it dims everything and adds a moon and stars. Used when sleeping. */
void cl_scene_night(cl_buf_t *b, cl_scene_id_t scene, int frame);

const char *cl_scene_name(cl_scene_id_t scene);
