/*
 * TURBO - what is written over the road: the clock, the speed, the pedals,
 * the progress bar and the banners
 *
 * The frame goes straight to the panel, so LVGL cannot draw on top of it:
 * everything here is IN the frame. Text is rendered once by LVGL at start
 * (turbo.c, in the LVGL task) into alpha masks, then baked here into sprites
 * with their colour, a vertical gradient and a dark outline, the look of an
 * arcade clock. Drawing a sprite per glyph is then one pass.
 */
#pragma once

#include "tb_game.h"
#include "tb_gfx.h"

#include <stdbool.h>
#include <stdint.h>

/* the words, rendered from _() by the app */
enum {
    TX_TIME = 0,        /* "TIEMPO" over the clock               */
    TX_KMH,
    TX_CHECKPOINT,
    TX_EXTRA,           /* "TIEMPO EXTRA"                        */
    TX_GO,
    TX_FINISH,
    TX_TIMEUP,
    TX_HURRY,           /* the last seconds                      */
    TX_N
};

/* glyphs: 0-9 then ':' '.' '+' '-' */
#define TB_GLYPHS   14

typedef struct {
    uint8_t *a;
    int16_t  w, h;
} tb_mask_t;

typedef struct {
    tb_sprite_t big[TB_GLYPHS];     /* the clock and the countdown (48 px, gradient) */
    tb_sprite_t mid[TB_GLYPHS];     /* the speed (36 px, white)                      */
    tb_sprite_t sml[TB_GLYPHS];     /* the elapsed time, splits (20 px)              */
    tb_sprite_t word[TX_N];
    tb_sprite_t pedal[2][2];        /* [brake, gas][up, pressed]                     */
    tb_sprite_t pause;
    bool        ok;
} tb_hud_t;

/* what the HUD shows beyond the game's own state */
typedef struct {
    bool   gas, brake;              /* the pedals as pressed                */
    int    banner;                  /* TX_* or -1                           */
    float  banner_t;                /* seconds left for it                  */
    float  added;                   /* seconds a checkpoint gave            */
    float  split_diff;              /* against the rival's split, s         */
    bool   split_show;
    bool   rival;                   /* show the rival on the bar            */
    float  rival_prog;              /* 0..1                                 */
    bool   low_time;
    /* the texts of this frame (tb_hud_prepare), so the bands do not format them */
    char   t_clock[12], t_elapsed[16], t_speed[12], t_gear[12], t_extra[16], t_final[16];
    int    count;                   /* the countdown's number, 0 none        */
    bool   blink;
} tb_hud_state_t;

/* bakes a mask into a sprite: gradient from top to bottom colour, outline px */
bool tb_hud_bake(tb_sprite_t *out, const tb_mask_t *m, uint32_t top, uint32_t bottom, int outline);
void tb_hud_make_pedals(tb_hud_t *h);
void tb_hud_free(tb_hud_t *h);

/* once a frame, before the bands: the texts */
void tb_hud_prepare(tb_hud_state_t *st, const tb_game_t *g);
/* the HUD over a frame of the race (or a band of it) */
void tb_hud_draw(const tb_hud_t *h, tb_img_t *im, const tb_game_t *g, const tb_hud_state_t *st);
/* the worker calls this with each step's events: banners and their clocks */
void tb_hud_events(tb_hud_state_t *st, const tb_game_t *g, uint32_t ev, float dt);

/* where the pedals and the pause button are, for the touch */
#define TB_PEDAL_Y      336
#define TB_PEDAL_H      104
#define TB_BRAKE_X0     6
#define TB_BRAKE_X1     122
#define TB_GAS_X0       262
#define TB_GAS_X1       362
#define TB_PAUSE_R      44          /* the top-left corner, this many px     */
