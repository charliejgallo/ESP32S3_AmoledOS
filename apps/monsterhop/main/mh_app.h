/*
 * MONSTER HOP - the app's shared state
 *
 * monsterhop.c  life cycle, the worker and its jobs, the timer that pushes
 *               the frames, touch and the button, the states
 * mh_ui.c       the LVGL panels (title, map, house, shop, album...)
 * mh_hud.c      what is drawn over the game inside the frame
 * mh_game.c     the rules          mh_scene.c  the draw list
 * mh_world.c    the background cache   mh_render.c  the frame
 * mh_cast.c / mh_art.c   the art in monsterhop.pak
 * mh_shop.c / mh_prog.c  the catalogue, and what the player has
 * mh_audio.c    the sound          mh_link.c   racing the other watch
 *
 * While playing nothing of LVGL is on screen: the worker (core 0) steps the
 * game and renders frames into PSRAM buffers; the LVGL timer pushes the
 * newest to the panel with aos_hal_display_blit(). The panels are LVGL
 * objects over a canvas that shows the last frame.
 */
#pragma once

#include "aos_app.h"

#include "mh_art.h"
#include "mh_cast.h"
#include "mh_game.h"
#include "mh_gfx.h"
#include "mh_hud.h"
#include "mh_level.h"
#include "mh_prog.h"
#include "mh_render.h"
#include "mh_scene.h"
#include "mh_shop.h"
#include "mh_world.h"

#include <stdbool.h>
#include <stdint.h>

#define MH_NFB      4           /* at most; the 4th only when PSRAM allows */
#define MH_FB_SPARE (600 * 1024)
#define MH_BAND     64          /* rows per band of internal RAM           */
#define MH_LEVELS   MH_NLEVELS

enum { FB_FREE = 0, FB_BUSY, FB_READY, FB_SHOWN };

enum {
    ST_BOOT = 0,
    ST_MENU,            /* the title                                        */
    ST_MAP,             /* the world map: the house and the levels          */
    ST_HOUSE,           /* Tommy's house                                    */
    ST_SHOP,            /* wardrobe and shop                                */
    ST_ALBUM,
    ST_TROPHIES,
    ST_STATS,
    ST_SETTINGS,
    ST_LOADING,
    ST_INTRO,           /* the camera flies over the keys                   */
    ST_PLAY,
    ST_PAUSE,
    ST_RESULT,
    ST_LOBBY,           /* waiting for the other watch                      */
    ST_N,
};

enum { JOB_NONE = 0, JOB_BOOT, JOB_LEVEL, JOB_OUTFIT, JOB_UI };

/* the key race (mh_link.c) */
enum { LK_OFF = 0, LK_LOBBY, LK_LOADING, LK_PLAY, LK_GONE };
#define MH_LK_Q 32
typedef struct {
    uint8_t kind, idx;
    int8_t  x, y, z;
    uint8_t flag;
    float   at;
} mh_lk_op_t;

typedef struct app app_t;

typedef struct {
    const char *name;           /* lvl_<name> in the pack                    */
    uint8_t     zone;
} mh_level_info_t;

/* a picture for LVGL: RGB565A8, unpacked by the worker */
typedef struct {
    uint8_t       *buf;
    int16_t        w, h;
    lv_image_dsc_t dsc;
} mh_uimg_t;

struct app {
    aos_app_t  *self;
    lv_obj_t   *root, *canvas, *touch;

    /* frames */
    uint16_t   *fb[MH_NFB];
    uint16_t   *cv;                 /* the canvas's own, LVGL's byte order    */
    uint16_t   *band;               /* MH_BAND rows in internal RAM, or NULL  */
    volatile uint8_t  fb_state[MH_NFB];
    volatile uint32_t fb_seq[MH_NFB];
    uint32_t    seq;
    int         shown;
    volatile int nfb;
    bool        spare_checked;

    /* the worker */
    volatile int  job;
    volatile bool job_done;
    volatile bool job_ok;
    volatile int  ui_job;           /* UJ_*, for JOB_UI                       */
    volatile bool playing;          /* step + render                          */
    volatile bool frozen;           /* render only (the fly-over)             */
    int         job_level;
    uint64_t    w_last_ms;
    uint32_t    w_frames;
    uint64_t    w_fps_t0;
    volatile uint32_t ev_ring[16];  /* the game's events, worker -> UI        */
    volatile uint32_t ev_w;
    uint32_t    ev_r;

    /* the level */
    mh_level_t  lv;
    mh_world_t  world;
    mh_game_t   game;
    mh_scene_t  scene;
    mh_cast_t   cast;
    mh_dlist_t  dl;
    mh_hud_t    hud;
    mh_hud_state_t hs;
    bool        level_ok;
    int         level;              /* index in the table, -1 = test         */
    int         loaded_level;
    float       intro_t;
    int         intro_i;
    volatile bool intro_skip;
    uint32_t    events_seen;        /* this level's, for the stats            */

    /* input: written by the UI, read by the worker */
    volatile int  in_hop;           /* 0 none, 1+dir                          */
    volatile bool in_action;
    lv_point_t  p0;
    bool        pressed, dragged;
    uint32_t    last_hop_ms;
    volatile bool want_pause, want_map;

    /* the player */
    mh_prog_t   prog;
    mh_wear_t   wear;
    mh_outfit_t outfit;
    int         skin_fx, trail;
    int8_t      try_eq[CAT_N];      /* what the shop tries on                 */
    volatile bool outfit_dirty;     /* the hero's layers must be reloaded     */
    bool        dev_auto;

    /* pictures (mh_ui.c) */
    mh_uimg_t   ui_map, ui_logo, ui_house, ui_marker, ui_emblem[7], ui_trophy[4];
    mh_uimg_t   ui_card[MH_LEVELS];
    int16_t     spots[19][2];       /* the map: 16 levels, house, 2 locked   */
    bool        spots_ok;
    bool        menu_art;           /* map, logo, house... are unpacked       */
    mh_anim_t   turn[5];            /* body, back, hand, cap, pet (the shop)  */
    mh_lut_t    turn_lut[5];

    /* the key race (mh_link.c) */
    bool        link_on;            /* the link is up for this game          */
    int         link_state;         /* LK_*                                  */
    uint32_t    link_nonce, link_peer_nonce, link_seed;
    bool        is_host;
    char        partner[40];
    int8_t      rival_eq[CAT_N];
    uint16_t    rival_open;         /* the levels open on the other watch    */
    int         link_level;
    mh_lk_op_t  lk_in[MH_LK_Q];     /* UI -> worker: what the other did      */
    volatile uint32_t lk_in_w, lk_in_r;
    uint16_t    lk_out[MH_LK_Q];    /* worker -> UI: what changed here       */
    volatile uint32_t lk_out_w, lk_out_r;
    bool        lk_loaded;          /* the level is built here               */
    bool        lk_abort;           /* the other left while loading          */
    bool        lk_race;            /* the level being played is a race      */
    uint32_t    pos_ms, over_ms;
    volatile float lk_behind;       /* guest: seconds behind the host's clock (< 0 ahead) */

    /* the loading bar: shown when a load takes more than 2 s, it measures the
     * bytes read against what the same load read last time (prefs) */
    bool        ld_on;
    uint32_t    ld_from, ld_est, ld_ms;
    char        ld_key[16];
    uint8_t     ld_tag;             /* from the pack's size: a new pack forgets */

    int         state;
    uint32_t    st_ms;
    bool        want_exit, closing;
    uint64_t    prev_ms;
    lv_timer_t *timer;
};

const mh_level_info_t *mha_level_info(int i);
const char *mha_level_title(int i);
const char *mha_zone_title(int z);
int  mha_zone_need(int z);
bool mha_level_open(const app_t *a, int i);
void mha_set_state(app_t *a, int st);
void mha_level_start(app_t *a, int i);
void mha_resume(app_t *a);
void mha_level_leave(app_t *a);     /* from the pause, back to the menus     */
void mha_outfit(app_t *a);          /* what prog.eq says: palettes + layers  */
void mha_save(app_t *a);
void mha_ui_job(app_t *a, int what);
bool mha_link_available(app_t *a, char *name, int n);
void mha_link_begin(app_t *a);
/* mh_link.c */
void mhl_end(app_t *a);
void mhl_tick(app_t *a);                /* the LVGL timer, every tick        */
void mhl_worker_before(app_t *a);       /* the worker, before a step         */
void mhl_worker_after(app_t *a);        /* and after it                      */
void mhl_pick(app_t *a, int delta);     /* the host's level, in the lobby    */
void mhl_go(app_t *a);
bool mhl_racing(const app_t *a);
/* monsterhop.c: both watches have the level, the race starts */
void mhl_start_play(app_t *a);
