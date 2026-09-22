/*
 * MONSTER HOP - the HUD, drawn inside the frame
 *
 * Keys, lives, the clock and the coins across the top; an arrow at the
 * screen's edge towards the nearest key (the exit once it opens); banners
 * in the middle. Words and digits are rendered by LVGL into masks when the
 * app opens (monsterhop.c), so they follow the language.
 */
#pragma once

#include "mh_game.h"
#include "mh_gfx.h"
#include "mh_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    MSG_KEY = 0, MSG_OPEN, MSG_CHECK, MSG_TIMEUP, MSG_READY, MSG_GO, MSG_LIFE, MSG_TIME,
    MSG_LOW, MSG_N,
};

typedef struct {
    mh_mask_t dig[12];          /* 0-9, ':', '/'  (big)                     */
    mh_mask_t sdig[12];         /* the same, small                          */
    mh_mask_t msg[MSG_N];
    mh_mask_t title;            /* the level's name, for its start          */
} mh_hud_t;

typedef struct {
    int   msg;                  /* the banner, -1 none                      */
    float msg_t;
    float key_flash;            /* the key row's glow after a pick-up       */
    float coin_flash;
    float title_t;              /* the level's name, over the fly-over     */
    bool  show_title;
    bool  pause_icon;
} mh_hud_state_t;

void mh_hud_events(mh_hud_state_t *s, uint32_t ev, float dt);
void mh_hud_draw(const mh_hud_t *h, mh_img_t *im, const mh_game_t *g, const mh_hud_state_t *s,
                 const mh_world_t *w, int cam_x, int cam_y);
void mh_hud_free(mh_hud_t *h);

#define MH_PAUSE_R 46           /* the pause corner, top-left              */
