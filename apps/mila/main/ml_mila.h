/*
 * MILA - Mila's frames and what she wears (tools/blender/SPEC.md section 4)
 *
 * Her body is final colour (COL: she is always black); a hat and a neck
 * item are layers over every frame (LID), coloured through a palette: the
 * item's default from the pack (pal_<item>) with region 1 replaced by the
 * colour the player picked.
 */
#pragma once

#include "ml_art.h"

#include <stdbool.h>
#include <stdint.h>

enum { MD_N = 0, MD_E, MD_S, MD_W };        /* facing                      */

enum {
    /* the levels */
    MA_IDLE = 0, MA_WALK, MA_PUSH, MA_WIN, MA_YAWN,
    /* the casita */
    MA_C_WALK, MA_C_RUN, MA_C_SIT, MA_C_SLEEP, MA_C_BELLY, MA_C_POUNCE, MA_C_BAT,
    MA_C_JUMP, MA_C_SCRATCH, MA_C_EAT, MA_C_GROOM, MA_C_MEOW, MA_C_PURR, MA_C_PEEK,
    MA_C_LIE,
    /* the shop */
    MA_TURN,
    MA_N,
};

enum { ML_SET_GAME = 1, ML_SET_CASITA = 2, ML_SET_TURN = 4 };

typedef struct {
    ml_anim_t body, sh, hat, neck;      /* sh: her shadow on the floor      */
} ml_frames_t;

typedef struct {
    ml_frames_t a[MA_N][4];     /* per anim and facing (one-way anims: all four point at the same) */
    uint8_t  loaded[MA_N];
    ml_lut_t hat_lut, neck_lut;
    char     hat[20], neck[20]; /* item names, "" none                      */
    uint32_t hat_col, neck_col; /* 0xRRGGBB, 0 = the item's own            */
    int      sets;
    bool     layers_only;       /* a guest: her body is the host's frames   */
} ml_mila_t;

/* the name, directions and frame time of an anim */
const char *ml_anim_name(int anim);
/* the facings an anim was rendered in: a bit per MD_* */
int  ml_anim_dirs(int anim);

/* loads the sets (ML_SET_*) with the outfit; what is loaded and not asked
 * for is freed */
void ml_mila_load(ml_mila_t *m, int sets, const char *hat, uint32_t hat_col, const char *neck,
                  uint32_t neck_col);
void ml_mila_free(ml_mila_t *m);
/* the frames of anim facing d (the nearest facing it has), NULL if none */
const ml_frames_t *ml_mila_frames(const ml_mila_t *m, int anim, int d);
/* frame index at time t (seconds) of an anim, looping or held at the end */
int  ml_mila_frame(const ml_frames_t *f, float t, bool loop);
