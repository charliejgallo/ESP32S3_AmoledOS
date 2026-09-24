/*
 * MILA - the app's shared state
 *
 * mila.c        life cycle, the worker and its jobs, the frames, touch and
 *               the button, the states
 * ml_rules.c    the rules (shared with the Mac's solver)
 * ml_level.c    the worlds table and the levels (text in the pack)
 * ml_world.c    the level's background cache and the whole-level picture
 * ml_play.c     a level being played: undo, animation, camera, draw list
 * ml_mila.c     Mila's frames and her outfit
 * ml_casita.c   Mila's home: her wandering, the toys, petting
 * ml_map.c      the world map (panels that chain, DESIGN.md section 4)
 * ml_hud.c      what is drawn over a frame (the level's and the scenes')
 * ml_ui.c       the LVGL panels (shop, settings, pause, results, lobby)
 * ml_prog.c     what the player has (saves)       ml_audio.c  the sound
 * ml_link.c     the other watch: visits and races
 * ml_art.c / ml_gfx.c / ml_render.c   Monster Hop's pack, pixels and bands
 *
 * The worker (core 0) renders every animated screen — a level, the casita,
 * the map — into PSRAM frames; the LVGL timer blits the newest to the panel.
 * The LVGL panels sit over a canvas that shows the last frame.
 */
#pragma once

#include "aos_app.h"

#include "ml_art.h"
#include "ml_gfx.h"
#include "ml_hud.h"
#include "ml_level.h"
#include "ml_mila.h"
#include "ml_play.h"
#include "ml_prog.h"
#include "ml_render.h"
#include "ml_world.h"

#include <stdbool.h>
#include <stdint.h>

#define ML_NFB      3
#define ML_BAND     64

enum { FB_FREE = 0, FB_BUSY, FB_READY, FB_SHOWN };

enum {
    ST_BOOT = 0,
    ST_CASITA,          /* the hub: Mila at home (worker scene)             */
    ST_MAP,             /* the world map (worker scene)                     */
    ST_SHOP,            /* LVGL                                             */
    ST_SETTINGS,
    ST_LOADING,
    ST_LEVEL,           /* a level (worker scene): its modes below          */
    ST_PAUSE,
    ST_RESULT,
    ST_LOBBY,
    ST_N,
};

/* what the worker is drawing */
enum { SC_NONE = 0, SC_LEVEL, SC_CASITA, SC_MAP };

enum { ML_LK_OFF = 0, ML_LK_LOBBY, ML_LK_LOADING, ML_LK_PLAY, ML_LK_VISIT, ML_LK_GONE };

/* a level's modes */
enum {
    LM_OVERVIEW = 0,    /* the whole level and its goal, until a tap        */
    LM_ZOOM,            /* onto Mila                                        */
    LM_PLAY,
    LM_PEEK,            /* the whole level while the finger holds Mila      */
    LM_WON,
};

enum { JOB_NONE = 0, JOB_BOOT, JOB_LEVEL, JOB_LEAVE, JOB_OUTFIT, JOB_PEEK, JOB_CASITA, JOB_MAP, JOB_UI };

typedef struct app app_t;

/* a picture for LVGL: RGB565A8, made by the worker (ml_ui.c) */
typedef struct {
    uint8_t       *buf;
    int16_t        w, h;
    lv_image_dsc_t dsc;
} ml_uimg_t;

struct app {
    aos_app_t  *self;
    lv_obj_t   *root, *canvas, *touch;

    /* frames */
    uint16_t   *fb[ML_NFB];
    uint16_t   *cv;                 /* the canvas's own, LVGL's byte order    */
    uint16_t   *band;               /* ML_BAND rows in internal RAM           */
    volatile uint8_t  fb_state[ML_NFB];
    volatile uint32_t fb_seq[ML_NFB];
    uint32_t    seq;
    int         shown;

    /* the worker */
    volatile int  job;
    volatile bool job_done, job_ok;
    volatile int  scene;            /* SC_*                                    */
    volatile uint32_t scene_seq;    /* frames after this one show the scene    */
    uint64_t    w_last_ms;
    uint32_t    w_frames;
    uint64_t    w_fps_t0;
    volatile uint32_t ev_ring[16];  /* play events, worker -> UI (sounds)      */
    volatile uint32_t ev_w;
    uint32_t    ev_r;

    /* the worlds */
    ml_worlds_t worlds;
    int         world, level;       /* what is loaded / being played           */
    int         job_world, job_level;

    /* the level */
    ml_level_t  lv;
    ml_world_t  w;
    ml_play_t   play;
    ml_dlist_t  dl;
    bool        level_ok;
    volatile int lmode;             /* LM_*                                    */
    float       mode_t;
    uint16_t   *ov;                 /* the whole-level picture (ML_W x ML_H)   */
    ml_ov_view_t ovv;
    volatile bool peek_ready;
    bool        result_shown;

    /* Mila */
    ml_mila_t   mila;

    /* the in-frame HUD */
    ml_hud_t    hud;
    ml_mask_t   wname[ML_MAX_WORLDS];   /* the worlds' names for the map    */
    float       hud_flash_undo, hud_flash_restart;
    int         pending_job;        /* asked for while another ran            */
    /* the loader's bar: bytes read against the last load of the kind */
    char        ld_key[12];
    uint32_t    ld_from, ld_est;

    /* the other watch (ml_link.c) */
    bool        link_on;
    int         link_state;         /* ML_LK_*                                */
    char        partner[40];
    bool        partner_ok;         /* a paired watch: the casita's button    */
    bool        race;               /* the level being played is a race       */
    bool        visit;              /* the friend's Mila is in the casita     */
    char        guest_hat[20], guest_neck[20];
    uint32_t    guest_hat_col, guest_neck_col;
    volatile int rival_on, rival_moves, rival_targets;
    volatile bool rival_won, race_over;
    bool        race_lost;          /* the other solved it first              */

    /* the scenes' own state (ml_casita.c, ml_map.c) */
    void       *casita;
    void       *map;

    /* input: written by the UI, read by the worker */
    lv_point_t  p0;
    bool        pressed, dragged, held_mila, repeating;
    bool        pinching;           /* two fingers on the glass (v0.6.0) */
    float       pinch_acc;          /* the pinch's scale since the last switch */
    uint32_t    press_ms, last_step_ms;
    int         held_dir;
    volatile bool want_pause;
    volatile int  tap_x, tap_y;     /* a tap for the scene, -1 none           */

    /* LVGL pictures and the shop (ml_ui.c) */
    ml_uimg_t   ui_logo, ui_coin, ui_star, ui_star_off, ui_preview;
    volatile int ui_job;            /* UJ_* the worker does next              */
    int         shop_tab, shop_item, shop_col, shop_turn;

    /* the player */
    ml_prog_t   prog;
    bool        dev_unlock;

    int         state;
    uint32_t    st_ms;
    bool        want_exit, closing;
    uint64_t    prev_ms;
    lv_timer_t *timer;
};

void mla_set_state(app_t *a, int st);
void mla_level_start(app_t *a, int world, int level);
void mla_level_leave(app_t *a);
void mla_resume(app_t *a);
void mla_outfit(app_t *a);
void mla_save(app_t *a);
/* a picture job for the worker (ml_ui.c's UJ_*) */
void mla_ui_job(app_t *a, int what);
bool mla_world_open(const app_t *a, int world);
bool mla_level_open(const app_t *a, int world, int level);
/* words into a mask for the in-frame HUD (LVGL thread) */
bool mla_text_mask(ml_mask_t *m, const char *txt, const lv_font_t *font);
/* the title of a world or a level in the current language */
const char *mla_world_name(const app_t *a, int world, char *buf, int n);
const char *mla_level_name(const app_t *a, int world, int level, char *buf, int n);
