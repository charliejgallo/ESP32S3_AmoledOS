/*
 * TURBO - the app's shared state
 *
 * turbo.c     life cycle, preferences, the worker, the timer that pushes the
 *             frames, touch and the IMU, and the LVGL panels (menu, stage
 *             select, garage, results, pause, settings)
 * tb_link.c   racing the paired watch
 *
 * While racing nothing of LVGL is on screen: the worker (core 1) steps the
 * race and renders frames into three PSRAM buffers; the LVGL timer pushes
 * the newest to the panel with aos_hal_display_blit(). The panels are LVGL
 * objects over a canvas that shows the last frame (or a scene the worker
 * rendered for the menu), and only then does LVGL draw.
 */
#pragma once

#include "aos_app.h"

#include "tb_art.h"
#include "tb_game.h"
#include "tb_gfx.h"
#include "tb_hud.h"
#include "tb_render.h"
#include "tb_track.h"

#include <stdbool.h>
#include <stdint.h>

#define TB_NFB      3
#define TB_BAND     64          /* rows per band of internal RAM (46 KB)  */

enum { FB_FREE = 0, FB_BUSY, FB_READY, FB_SHOWN };

enum {
    ST_BOOT = 0,
    ST_MENU,
    ST_SELECT,          /* the stage, for a time trial or the link    */
    ST_GARAGE,
    ST_SETTINGS,
    ST_LOADING,
    ST_RACE,
    ST_RESULT,
    ST_LOBBY,           /* waiting for the other watch                */
};

enum { MODE_TOUR = 0, MODE_TRIAL, MODE_LINK };

enum { JOB_NONE = 0, JOB_BOOT, JOB_STAGE, JOB_SCENE };

typedef struct app app_t;

struct app {
    aos_app_t  *self;
    lv_obj_t   *root, *canvas, *touch;

    /* frames */
    uint16_t   *fb[TB_NFB];
    uint16_t   *cv;                /* the canvas's own, in LVGL's byte order */
    uint16_t   *band;              /* TB_BAND rows in internal RAM, or NULL  */
    volatile uint8_t  fb_state[TB_NFB];
    volatile uint32_t fb_seq[TB_NFB];
    uint32_t    seq;
    int         shown;              /* the buffer on the panel, -1 none      */

    /* the worker's side */
    volatile int  job;              /* JOB_*, set by the UI, cleared when done */
    volatile bool job_done;
    volatile bool racing;           /* render and step                       */
    volatile bool paused;
    int         job_stage;          /* JOB_STAGE / JOB_SCENE parameters      */
    int         scene_car, scene_paint;
    float       scene_yaw;
    int         loaded_stage;       /* -1 none                               */
    volatile uint32_t ev_ring[16];  /* the race's events, worker -> UI       */
    volatile uint32_t ev_w;
    uint32_t    ev_r;
    uint64_t    w_last_ms;
    uint32_t    w_frames, w_ms_render, w_ms_prep;
    uint64_t    w_cyc[TB_PROF_N];   /* cycles per part, the whole race        */
    uint64_t    w_fps_t0;

    /* the race */
    tb_track_t  trk;
    tb_game_t   game;
    tb_render_t *ren;
    tb_hud_t    hud;
    tb_hud_state_t hs;
    int         mode;
    int         stage;              /* being raced                           */
    float       tour_time;          /* the tour so far                        */
    int         tour_coins;
    bool        result_shown;
    bool        new_record;
    bool        res_rival_seen;     /* the result shows the rival's time      */
    bool        res_win_paid;
    int         coins_won;

    /* input: written by the UI, read by the worker */
    volatile float steer;
    volatile bool  gas, brake;
    float       zero_ay;
    bool        zeroed;
    int         imu_skip;
    bool        autoplay;           /* the bot drives (simulator switch)     */

    /* preferences */
    int32_t     coins;
    uint32_t    own_cars, own_paints;
    int         car;
    uint8_t     paint[CAR_N];
    int         diff, sens;
    bool        sfx;
    uint32_t    unlocked;           /* stages open in time trial              */
    int32_t     best[STAGE_N];      /* tenths of a second, 0 = none          */
    int32_t     best_tour;
    int32_t     rival_best[STAGE_N];
    char        rival_name[28];

    /* link (tb_link.c) */
    bool        link_on;
    int         link_state;
    uint32_t    link_nonce, link_peer_nonce;
    uint32_t    link_ms;
    char        partner[28];
    bool        is_host;
    int         rival_car, rival_paint;
    bool        rival_done;         /* its result arrived                    */
    bool        rival_finished;
    float       rival_time, rival_dist;
    uint32_t    pos_ms;

    /* LVGL */
    lv_obj_t   *p_boot, *boot_bar, *boot_lbl;
    lv_obj_t   *p_menu, *lbl_coins, *btn_link, *lbl_link, *btn_garage, *btn_settings;
    lv_obj_t   *p_select, *sel_list, *sel_title;
    lv_obj_t   *p_garage, *g_name, *g_stats[4], *g_price, *g_btn, *g_btn_lbl, *g_sw[16], *g_coins;
    int         g_car, g_paint;
    lv_obj_t   *p_settings, *chip_diff[DIFF_N], *chip_sens[3], *chip_sfx;
    lv_obj_t   *p_loading, *load_lbl;
    lv_obj_t   *p_result, *res_title, *res_body, *res_btn_next, *res_btn_next_lbl;
    lv_obj_t   *p_pause;
    lv_obj_t   *p_lobby, *lobby_lbl;

    int         state;
    uint32_t    st_ms;
    bool        want_exit, closing;
    uint32_t    last_gesture_ms;
    uint64_t    prev_ms;
    lv_timer_t *timer;
};

/* turbo.c */
void tba_set_state(app_t *a, int st);
void tba_race_start(app_t *a, int stage);
void tba_toast(app_t *a, const char *txt);
void tba_prefs_save(app_t *a);

/* tb_link.c */
bool tbl_available(app_t *a, char *name, int n);
void tbl_begin(app_t *a);
void tbl_end(app_t *a);
void tbl_tick(app_t *a);
void tbl_send_result(app_t *a);
void tbl_host_pick(app_t *a, int stage);     /* the host chose a stage in the lobby */
bool tbl_hold_loading(app_t *a);             /* both watches must have the stage */
