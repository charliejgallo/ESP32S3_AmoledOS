/*
 * MILA - what is drawn over a frame, inside it (the worker draws it; LVGL
 * only rasterises the words into masks, on its own thread)
 *
 * Touch zones of the level's buttons are defined here too, so the drawing
 * and the input agree.
 */
#pragma once

#include "ml_art.h"
#include "ml_gfx.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    MSG_TAP = 0,        /* "tap to start"                                  */
    MSG_PAR,            /* "par"                                           */
    MSG_WELL,           /* "Well done!"                                    */
    MSG_UNDO,
    MSG_RESTART,
    MSG_PLAY,           /* the casita's buttons                            */
    MSG_SHOP,
    MSG_SETTINGS,
    MSG_FRIEND,
    MSG_SOON,           /* the map: "more to come"                          */
    MSG_N,
};

/* the UI artist's icons (icon_*, 40 x 40), drawn in the frame when loaded */
enum { ICO_PLAY = 0, ICO_SHOP, ICO_GEAR, ICO_LINK, ICO_UNDO, ICO_RESTART, ICO_HOME, ICO_COIN, ICO_STAR, ICO_N };

enum { SYM_PAUSE = 0, SYM_UNDO, SYM_RESTART, SYM_PLAY, SYM_HOME, SYM_SHOP, SYM_GEAR, SYM_FRIEND, SYM_LOCK, SYM_N };

typedef struct {
    ml_mask_t dig[12];          /* 0-9 : /, big                           */
    ml_mask_t sdig[12];         /* small                                  */
    ml_mask_t msg[MSG_N];
    ml_mask_t sym[SYM_N];
    ml_mask_t title, sub;       /* the level's name and its world         */
    ml_anim_t ico[ICO_N];       /* loaded by the worker at boot            */
} ml_hud_t;

/* loads the icons (worker) */
void ml_hud_icons_load(ml_hud_t *h);
/* an icon centred at (cx, cy); false if it is not there (draw a glyph) */
bool ml_hud_icon(ml_img_t *im, const ml_hud_t *h, int ico, int cx, int cy, int alpha);

/* the round buttons of a level: centre and radius, screen px */
#define HUD_BTN_R       24
#define HUD_PAUSE_X     34
#define HUD_PAUSE_Y     30
#define HUD_UNDO_X      38
#define HUD_UNDO_Y      (ML_H - 38)
#define HUD_RESTART_X   (ML_W - 38)
#define HUD_RESTART_Y   (ML_H - 38)

typedef struct {
    int   moves, par;
    int   on, targets;          /* things on targets / targets             */
    float msg_t;                /* the "well done" banner, < 0 none        */
    float undo_flash;           /* a button's flash after a press          */
    float restart_flash;
    bool  race;                 /* the other watch's progress              */
    int   rival_on, rival_moves;
} ml_hud_level_t;

void ml_hud_free(ml_hud_t *h);
/* numbers, one mask per digit, at x (left) y (top); returns the width */
int  ml_hud_number(const ml_hud_t *h, ml_img_t *im, int n, int x, int y, bool big, uint16_t c, int alpha);
int  ml_hud_number_w(const ml_hud_t *h, int n, bool big);
void ml_hud_pill(ml_img_t *im, int x, int y, int w, int hgt, uint16_t c, int alpha);
void ml_hud_button(ml_img_t *im, const ml_mask_t *glyph, int cx, int cy, int r, bool lit);
/* the same round button with an icon (the glyph if the icon is missing) */
void ml_hud_button_ico(ml_img_t *im, const ml_hud_t *h, int ico, const ml_mask_t *glyph, int cx, int cy, int r, bool lit);
void ml_hud_star(ml_img_t *im, int cx, int cy, int r, uint16_t c);
void ml_hud_mask_centered(ml_img_t *im, const ml_mask_t *m, int cx, int cy, uint16_t c, int alpha);

void ml_hud_level(const ml_hud_t *h, ml_img_t *im, const ml_hud_level_t *s);
/* the whole-level view: the level's name, its world, the par, the hint */
void ml_hud_overview(const ml_hud_t *h, ml_img_t *im, int par, float t, bool hint);
