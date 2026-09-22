/*
 * MONSTER HOP - from the game's state to a frame's draw list
 *
 * Picks each actor's animation and frame, places the sprites by their
 * world anchors, colours the recoloured ones through their palettes, and
 * keeps what is purely visual (the pet that follows, the camera).
 */
#pragma once

#include "mh_art.h"
#include "mh_cast.h"
#include "mh_game.h"
#include "mh_render.h"
#include "mh_world.h"

#include <stdbool.h>
#include <stdint.h>

/* Tommy's colours: regions of the body and of each layer */
typedef struct {
    mh_pal_t body, cap, back, hand, pet;
} mh_outfit_t;

#define MH_SCENE_BITS 40

typedef struct {
    mh_lut_t body, cap, back, hand, pet, rival;
    mh_lut_t mon[MON_N][3];         /* up to three variants per monster      */
    int      mon_var[MON_N];
    mh_lut_t bat, scarab, car[4];
    uint32_t tint;
    mh_pal_t body_pal;              /* for the rainbow skin                   */
    int      skin_fx;               /* SKIN_FX_* (mh_shop.h)                  */
    float    fx_t;
    /* the pet: it follows Tommy's trail one cell behind */
    float    px, py, pz;            /* where it is                            */
    float    pfx, pfy, pfz, ptx, pty, ptz, pt;   /* its hop                  */
    int      pdir;
    int      trail[8][2];           /* Tommy's last cells                     */
    int      ntrail;
    /* the camera: LP of the screen's top-left */
    float    cam_x, cam_y;
    int      icam_x, icam_y;
    bool     cam_set;
    float    anim_t;                /* a clock for idle animations            */
    /* the other watch's Tommy, eased between its 15 Hz positions */
    float    gx, gy, gz;
    bool     g_set;
    /* the trail from the shop: little bits drawn over the frame */
    int      trail_style;           /* TRAIL_* (mh_shop.h)                    */
    struct {
        float    x, y, z, vx, vy, vz, t, life;
        uint32_t col;
        uint8_t  kind;
    } bit[MH_SCENE_BITS];
    int      nbit;
    float    emit_t;
    int      last_state, step_side;
    uint32_t rnd;
} mh_scene_t;

void mh_scene_init(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_outfit_t *o, int skin_fx);
void mh_scene_outfit(mh_scene_t *s, const mh_outfit_t *o, uint32_t tint, int skin_fx);
/* the ghost wears the other player's colours */
void mh_scene_rival(mh_scene_t *s, const mh_pal_t *body);
/* the trail (TRAIL_*), and its bits over a band of the frame */
void mh_scene_trail(mh_scene_t *s, int trail);
void mh_scene_bits_draw(const mh_scene_t *s, const mh_world_t *w, mh_img_t *im, int cam_x, int cam_y);
/* advances the camera and the pet; then fills the draw list */
void mh_scene_build(mh_scene_t *s, const mh_world_t *w, const mh_game_t *g, const mh_cast_t *c,
                    mh_dlist_t *l, float dt);
/* the camera looking at a world point, for fly-overs (instant = no easing) */
void mh_scene_look(mh_scene_t *s, const mh_world_t *w, float x, float y, float z, float dt, bool instant);
