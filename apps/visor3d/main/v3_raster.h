/*
 * VISOR 3D - drawing a mesh: transform, flat shading, z-buffer. Runs in the
 * worker; writes RGB565 in the panel's byte order (big-endian), which is
 * what aos_hal_display_blit() takes.
 */
#pragma once

#include "v3_mesh.h"

typedef struct {
    float yaw, pitch;       /* radians                                      */
    float scale;            /* 1 = the model fills about 70% of the view    */
    float px, py;           /* pan, in pixels of the target                 */
    int   mode;             /* V3_SOLID or V3_WIRE                          */
} v3_view_t;

enum { V3_SOLID = 0, V3_WIRE, V3_MODES };

/* Per-mesh scratch: the transformed vertices. */
typedef struct {
    float *sx, *sy, *sz;
    int    nv;
} v3_scratch_t;

bool v3_scratch_alloc(v3_scratch_t *s, int nv);
void v3_scratch_free(v3_scratch_t *s);

/* Draws into fb (w x h, big-endian RGB565) with zb (w x h) as depth.
 * Returns the triangles actually drawn. */
int v3_render(const v3_mesh_t *m, const v3_view_t *view, v3_scratch_t *s,
              uint16_t *fb, uint16_t *zb, int w, int h);

/* CPU cycles spent since the last call: clear, transform, rasterise. */
void v3_prof(uint32_t out[3]);

/* fb (w x h) from small (w/2 x h/2): each pixel twice each way. */
void v3_upscale2(const uint16_t *small, uint16_t *fb, int w, int h);
