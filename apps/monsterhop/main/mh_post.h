/*
 * MONSTER HOP - the desktop's finishing touches over a frame (MH_DESKTOP)
 *
 * The watch draws a frame and sends it. A computer has time to spare, so
 * before the HUD goes on: a zone's airborne bits (embers over the valley,
 * plankton over the bay, fireflies in the woods...), a soft bloom around
 * everything bright (lava, lamps, glowing eyes) and a vignette. All of it
 * in the frame's own pixels, so screenshots and both halves of a split
 * screen get it too.
 */
#pragma once

#ifdef MH_DESKTOP

#include "mh_gfx.h"
#include "mh_world.h"

#include <stdbool.h>
#include <stdint.h>

#define MHP_BITS 90

typedef struct {
    struct {
        float x, y;             /* level-plane pixels (they move with it) */
        float vx, vy;
        float t, life;
        float phase;
        uint8_t kind;
    } bit[MHP_BITS];
    int      n;
    uint32_t rnd;
    float    clock;
    /* the bloom's scratch, a quarter of the view each way */
    uint16_t *small_r, *small_g, *small_b, *tmp;
    int      sw, sh;
    /* the vignette, one darkening per pixel of the view */
    uint8_t *vig;
    int      vw, vh;
} mh_post_t;

/* the zone's bits: stepped and drawn over the view (cam = its top-left) */
void mhp_bits(mh_post_t *p, int zone, mh_img_t *im, int cam_x, int cam_y, int w, int h, float dt);
/* bloom and vignette over a whole view (native RGB565) */
void mhp_finish(mh_post_t *p, int zone, uint16_t *px, int stride, int w, int h);
void mhp_free(mh_post_t *p);
/* the zone's far scenery into the world (w->bd), if the pack has it
 * ("bd_<zone>", the HD pack only); kept until another zone's is asked for */
void mhp_backdrop(mh_world_t *w, int zone);

#endif
