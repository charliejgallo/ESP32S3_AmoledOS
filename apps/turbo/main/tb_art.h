/*
 * TURBO - the art rendered in Blender (tools/blender/SPEC.md)
 *
 * One file, turbo.pak (tools/pack_assets.py), next to turbo.so on the card.
 * Its table is read when the app opens; the entries of a stage (props,
 * backdrop) when the stage is loaded, always in the worker.
 *
 * VEHICLES are not stored in colour: each pixel is a region id, a 4-bit
 * coverage and a light level, and the renderer looks the colour up in a
 * 16 x 32 table built from the paint (tb_lut_t). A red car and a blue car
 * are the same pixels; traffic comes in any colour for nothing.
 */
#pragma once

#include "tb_game.h"
#include "tb_gfx.h"
#include "tb_track.h"

#include <stdbool.h>
#include <stdint.h>

#define TB_NEAR_FRAMES  7       /* yaw -24 .. +24 degrees, 3 is straight     */
#define TB_FAR_VIEWS    3       /* vehicle left of the camera, centre, right */
#define TB_BG_H         160     /* the backdrop's height; its bottom row is the horizon */

/* region ids (SPEC.md) */
enum {
    RG_EMPTY = 0, RG_PAINT_A, RG_PAINT_B, RG_GLASS, RG_CHROME, RG_TRIM, RG_TYRE, RG_RIM,
    RG_TAIL, RG_HEAD, RG_PLATE, RG_INTERIOR, RG_UNDER, RG_AMBER, RG_EXTRA, RG_SPARE, RG_N
};

/* a vehicle sprite: per pixel (id << 12) | (alpha4 << 8) | light */
typedef struct {
    uint16_t *px;
    int16_t   w, h, ox, oy;
} tb_vspr_t;

typedef struct {
    tb_vspr_t lv[TB_MIPS];
    uint8_t   n;
    float     ppm;              /* pixels per metre at the reference plane    */
} tb_vmip_t;

/* colours of the 16 regions (8-bit RGB) */
typedef struct {
    uint32_t c[RG_N];
} tb_paint_t;

/* the colour of each region at 32 light levels, panel pixels */
typedef struct {
    uint16_t c[RG_N][32];
} tb_lut_t;

void tb_lut_build(tb_lut_t *l, const tb_paint_t *p, uint32_t fog, int fog_k, bool brake, bool lights);

/* the paints: the garage's for the player's car, random ones for traffic */
int  tb_paint_n(void);
void tb_paint_get(int i, tb_paint_t *out);
int  tb_paint_price(int i);
void tb_paint_traffic(int i, tb_paint_t *out);

/* ---- the pack ---- */
bool tb_art_open(const char *path);         /* the table                      */
void tb_art_close(void);                    /* everything                     */
bool tb_art_ok(void);
/* every vehicle's far views and the cars' shadows (~1.5 MB) */
bool tb_art_load_vehicles(void);
/* one car's near frames (the one driven, or looked at in the garage) */
bool tb_art_load_near(int car);
/* the backdrop is only needed until the renderer composites it */
void tb_art_drop_backdrop(void);
/* the props a track uses and its backdrop (drops the previous stage's) */
bool tb_art_load_stage(const tb_track_t *t);

const tb_vspr_t *tb_art_near(int car, int frame);
const tb_sprite_t *tb_art_near_shadow(int car, int frame);  /* alpha only    */
const tb_vmip_t *tb_art_far(int model, int view);
const tb_mip_t *tb_art_far_shadow(int model, int view);
const tb_mip_t *tb_art_prop(int kind);
const tb_mip_t *tb_art_prop_shadow(int kind);
/* the backdrop: colour + alpha, TB_BG_H tall, its bottom row on the horizon;
 * the renderer composites it over the stage's sky */
const tb_sprite_t *tb_art_backdrop(void);

/* draws a vehicle through a LUT; clip_y as tb_mip_draw; opa 0..255 */
void tb_vmip_draw(tb_img_t *im, const tb_vmip_t *m, const tb_lut_t *lut, float x, float y,
                  float scale, int clip_y, int opa);
void tb_vspr_draw(tb_img_t *im, const tb_vspr_t *s, const tb_lut_t *lut, int x, int y, int opa);
