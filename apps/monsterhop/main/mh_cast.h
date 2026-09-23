/*
 * MONSTER HOP - the cast: the animated art of Tommy, the monsters and the
 * level's things, loaded by name from the pack (tools/blender/SPEC.md)
 *
 * Tommy is four layers drawn in order: body, back item, hand item, cap,
 * each a sheet per animation and facing (tommy_hop_e, cap_beanie_hop_e...),
 * each coloured through its own palette. A monster is one rig. What is
 * missing from the pack is simply not drawn (the stand-in `tommy_test` is
 * used for anyone without art, so a level plays before its art exists).
 */
#pragma once

#include "mh_art.h"
#include "mh_game.h"
#include "mh_level.h"

#include <stdbool.h>
#include <stdint.h>

/* Tommy's animations */
enum { HA_IDLE = 0, HA_HOP, HA_SUPER, HA_PUSH, HA_USE, HA_WIN, HA_HURT, HA_SINK, HA_FALL, HA_N };
/* the monsters' */
enum {
    MA_IDLE = 0, MA_WALK, MA_NOTICE, MA_LUNGE, MA_GLIDE, MA_TRANSFORM, MA_FLY, MA_PUSH,
    MA_HOWL, MA_RUN, MA_STUN, MA_CRAWL, MA_PERCH, MA_DIVE, MA_STOMP, MA_CAST, MA_WHIP, MA_N,
};

#define MH_RIG_MAX 17

typedef struct {
    mh_anim_t a[MH_RIG_MAX][4];     /* [animation][facing]; one-facing ones in [DIR_S] */
    mh_anim_t sh[MH_RIG_MAX][4];    /* their shadows                                */
    bool      any;
} mh_rig_t;

/* the layers Tommy wears: an index in each list, -1 = none */
enum { CAP_CAP = 0, CAP_BACK, CAP_BEANIE, CAP_BUCKET, CAP_PROPELLER, CAP_CROWN, CAP_N };
enum { BACK_BACKPACK = 0, BACK_CAPE, BACK_TANK, BACK_WINGS, BACK_N };
enum { HAND_FLASHLIGHT = 0, HAND_TORCH, HAND_BALLOON, HAND_BUCKET, HAND_N };
enum { PET_DOG = 0, PET_CAT, PET_BAT, PET_N };

typedef struct {
    int cap, back, hand, pet;       /* -1 = none */
} mh_wear_t;

/* the moving things of a level and the common objects */
enum {
    OB_KEY = 0, OB_COIN, OB_HEART, OB_HOURGLASS, OB_CHEST, OB_LEVER, OB_LANTERN_OFF, OB_LANTERN_ON,
    OB_FX_DUST, OB_FX_SPLASH, OB_FX_POOF, OB_FX_SPARKLE, OB_FX_BUBBLES,
    OB_GATE, OB_CRATE, OB_PLATFORM, OB_SPIKES, OB_VENT, OB_DART_X, OB_DART_Y,
    OB_BOULDER_X, OB_BOULDER_Y, OB_RUNCAR_E, OB_RUNCAR_W, OB_LOG_W, OB_LOG_M, OB_LOG_E, OB_LILY,
    OB_BEAR, OB_COFFIN, OB_STICKER, OB_N,
};

typedef struct {
    mh_rig_t  body, cap, back, hand, pet;
    mh_rig_t  mon[MON_N];
    mh_rig_t  bat, scarab;          /* the vampire's other form, the swarms    */
    mh_anim_t ob[OB_N];
    mh_anim_t ob_sh[OB_N];
    mh_anim_t ob_gl[OB_N];
    mh_anim_t stand_in;             /* tommy_test                                  */
    mh_anim_t stand_in_sh;
    int       zone;
} mh_cast_t;

/* Tommy with what he wears (reloads only the layers that changed) */
bool mh_cast_hero(mh_cast_t *c, const mh_wear_t *w);
/* the monsters and objects a level uses (frees the others) */
bool mh_cast_level(mh_cast_t *c, const mh_level_t *lv);
void mh_cast_free(mh_cast_t *c);
/* the level's monsters and objects go (Tommy stays): back to the menus,
 * whose pictures need that PSRAM; the next level loads them again */
void mh_cast_level_free(mh_cast_t *c);

/* names in the pack (functions, not extern tables: see the GLOB_DAT trap) */
const char *mh_cap_name(int i);
const char *mh_back_name(int i);
const char *mh_hand_name(int i);
const char *mh_pet_name(int i);
const char *mh_zone_key(int zone);      /* "city", "castle", ...           */
