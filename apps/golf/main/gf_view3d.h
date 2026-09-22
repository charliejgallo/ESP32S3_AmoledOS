/*
 * GOLF - the 3D view from behind the golfer
 *
 * A voxel-space renderer (the Comanche technique) over the hole's height
 * grid: for every screen column a ray walks the ground from near to far and
 * paints the part of the column each sample rises above, so hills hide what
 * is behind them and the green can sit up on its plateau. The ground's
 * colour is the map renderer's own texture (gf_map_albedo), lit here with a
 * light that stays behind the camera whatever way the player aims, and
 * fogged with distance. Then the sky with clouds on a plane far above, the
 * hills on the horizon, and the trees and the flag as billboards tested
 * against the depth of the ground.
 *
 * It runs once per shot (the camera does not move while the golfer swings),
 * so it can afford to be thorough. The golfer is a sprite rendered in Blender
 * with THIS camera (tools/blender/SPEC.md), which is why it stands on the
 * ground this draws.
 */
#pragma once

#include "gf_world.h"
#include "gf_gfx.h"

typedef struct {
    float x, y, z;          /* position, metres                              */
    float yaw;              /* heading, radians; 0 = +y                      */
    float pitch;            /* looking down by this much, radians            */
    float f;                /* focal length, pixels                          */
    float cx, cy;           /* principal point                               */
    /* derived */
    float fx, fy, rx, ry;   /* forward and right, on the ground              */
    float cp, sp;
} gf_cam_t;

/* The swing camera: behind the ball at (bx, by) looking along 'aim'. */
void gf_cam_swing(gf_cam_t *c, const gf_world_t *w, float bx, float by, float aim);
/* Any camera: for the flyover and the ball's flight */
void gf_cam_set(gf_cam_t *c, float x, float y, float z, float yaw, float pitch, float vfov_deg);

/* World point to screen; false when it is behind the camera. zc = depth. */
bool gf_cam_project(const gf_cam_t *c, float x, float y, float z, float *sx, float *sy, float *zc);

typedef struct {
    const uint16_t *tex;    /* gf_map_albedo() output                        */
    int             tw, th;
    float           mpp;
} gf_albedo_t;

/* The whole picture into out (GF_W x GF_H, dithered). depth, if not NULL,
 * receives the ground's depth per pixel in decimetres (0xFFFF = sky). */
void gf_view3d_render(const gf_world_t *w, const gf_cam_t *c, const gf_albedo_t *alb,
                      uint16_t *out, uint16_t *depth);

