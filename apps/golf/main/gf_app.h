/*
 * GOLF - the app's shared state
 *
 * golf.c      life cycle, preferences, the timer, touch, and the screens
 *             that are LVGL panels (menu, setup, pause, cards, settings)
 * gf_play.c   a hole being played: aiming on the map, the swing in 3D, the
 *             ball's flight, the putt; everything drawn on the canvas
 * gf_shop.c   the shop and its turntable
 * gf_link.c   playing against the paired watch
 *
 * The screen is ONE canvas, 368x448 RGB565, shown 1:1. What is behind the
 * moving things (the map, the 3D view) is a background buffer; every frame
 * the rectangles dirtied last time are restored from it, the moving things
 * are drawn and their rectangles recorded, and only the union is pushed.
 */
#pragma once

#include "aos_app.h"

#include "gf_art.h"
#include "gf_game.h"
#include "gf_gfx.h"
#include "gf_map.h"
#include "gf_outfit.h"
#include "gf_phys.h"
#include "gf_view3d.h"
#include "gf_world.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    ST_MENU = 0,
    ST_SETUP,
    ST_LOADING,         /* the hole's card, then the hole is built           */
    ST_AIM,             /* the map: aim and pick a club                      */
    ST_SWING,           /* the 3D view: the meter and the swing              */
    ST_FLIGHT,          /* the map: the ball flies and rolls                 */
    ST_PUTT,            /* the green close up                                */
    ST_ROLL,            /* a putt rolling                                    */
    ST_RESULT,          /* the banner after a shot                           */
    ST_HOLE_END,
    ST_ROUND_END,
    ST_SHOP,
    ST_SETTINGS,
    ST_REMOTE,          /* waiting for the other watch's shot                */
};

enum { MT_IDLE = 0, MT_UP, MT_DOWN, MT_SWING, MT_FOLLOW };

#define MAX_TRACE       16
#define TRACE_PTS       48

typedef struct {
    float   x[TRACE_PTS], y[TRACE_PTS];
    uint8_t n;
    uint8_t player;
} gf_trace_t;

typedef struct app app_t;

struct app {
    aos_app_t  *self;
    lv_obj_t   *root;
    lv_obj_t   *canvas;
    lv_obj_t   *touch;

    /* pixels */
    uint16_t   *fb;             /* the canvas                                */
    uint16_t   *mapbuf;         /* the map as rendered for this shot          */
    uint16_t   *v3dbuf;         /* the 3D view as rendered for this shot      */
    uint16_t   *depth;
    uint16_t   *albedo;
    int         atw, ath;
    float       ampp;
    const uint16_t *bg;         /* which of the two is behind the overlays    */
    gf_dirty_t  dprev, dcur;

    /* the game */
    gf_world_t  world;
    gf_game_t   game;
    gf_wardrobe_t wr;
    gf_shot_t   shot;
    gf_trace_t  trace[MAX_TRACE];
    int         ntrace;
    int         state;
    uint32_t    st_ms;          /* ms in the current state                    */
    int         mode, diff, nplayers, practice_hole;
    int         course;         /* the one picked in the setup screens         */
    int16_t     menu_ball_x16, menu_ball_y16;
    /* the loading screen */
    lv_obj_t   *p_boot, *lbl_boot_st, *boot_bar;
    int         boot_pct, boot_target;
    lv_obj_t   *lbl_menu_course;

    /* aiming */
    float       aim;
    int         club;
    gf_view_t   view;
    float       aim_dist;       /* metres to the aim marker                   */
    bool        dragging;
    bool        putting;

    /* the swing */
    gf_cam_t    cam;
    int         meter;          /* MT_*                                       */
    float       mval;           /* the meter's value                          */
    float       power, acc;
    float       anim;           /* golfer frame, fractional                   */
    int         seq;
    int         react_seq;      /* the hole card's: cheer, sad or idle       */
    int         hit_ms;         /* ms since impact                            */
    int         shown_player;   /* whose outfit the art is coloured with      */

    /* playback */
    float       play;           /* index into shot.trk, fractional            */
    bool        skip;
    int         result_ms;
    char        result_txt[64];

    /* preferences */
    bool        sfx;
    bool        metres;
    int32_t     best[3];        /* best round to par per mode (quick, tour)   */

    /* LVGL: the in-game HUD */
    lv_obj_t   *hud_top, *lbl_hole, *lbl_info, *lbl_wind;
    lv_obj_t   *hud_bot, *btn_prev, *btn_next, *lbl_club, *lbl_carry, *btn_hit, *lbl_hit;
    lv_obj_t   *lbl_lie, *btn_pause, *btn_back, *lbl_hint, *banner;
    /* panels */
    lv_obj_t   *p_menu, *p_setup, *p_pause, *p_hole, *p_round, *p_settings, *p_loading, *p_shop;
    lv_obj_t   *lbl_coins;
    lv_obj_t   *setup_title, *setup_box;
    lv_obj_t   *lbl_load_t, *lbl_load_s;
    lv_obj_t   *lbl_hole_t, *lbl_hole_s, *lbl_hole_c, *hole_table;
    lv_obj_t   *lbl_round_t, *lbl_round_s, *lbl_round_c, *round_table;
    lv_obj_t   *chip_units, *chip_sfx;

    /* the shop (gf_shop.c) */
    int         shop_cat, shop_item, shop_turn;
    float       shop_rot;
    uint8_t     shop_eq[CAT_N];
    lv_obj_t   *shop_tabs[CAT_N], *shop_list, *shop_name, *shop_btn, *shop_btn_l, *shop_coins;

    /* link (gf_link.c) */
    bool        link_on;
    int         local_player;   /* which player this watch is (link mode)     */
    uint32_t    link_nonce, link_peer_nonce;
    int         link_state;
    uint32_t    link_ms;
    char        partner[32];
    uint8_t     partner_eq[CAT_N];
    bool        remote_shot_ready;
    bool        remote_check;       /* compare where the ball stopped         */
    float       remote_x, remote_y;

    bool        want_exit, leaving, closing, paused, render_pending;
    bool        autoplay;
    uint32_t    st_frames;      /* frames drawn in this state: the fps log   */
    uint64_t    st_t0;
    bool        dev_card;       /* GF_SCREEN=card: the hole card, for pictures */
    /* the golfer is being coloured by the worker: do not draw it */
    bool        art_busy, art_dirty;
    uint8_t     job_eq[CAT_N];
    unsigned    job_mask;
    bool        job_recolor;
    /* the 3D view is rendered ahead, for the line being aimed */
    bool        v3d_valid;
    /* easy mode: where the shot will really go (wind, roll, the green's slope) */
    bool        prev_valid;
    float       prev_aim;
    int         prev_club;
    float       prev_x[32], prev_y[32];
    uint8_t     prev_n;
    float       v3d_aim, job_aim;
    uint32_t    aim_changed_ms;
    bool        booting;
    bool        menu_ready, map_ready, turn_over, job_green, link_on_lobby;
    uint32_t    last_gesture_ms;
    uint64_t    prev_ms;
    lv_timer_t *timer;
};

/* golf.c */
void gf_sfx(int freq, int ms);
void gf_sound(int id);          /* SND_*, honours the sound switch */
void gfa_set_state(app_t *a, int st);
void gfa_banner(app_t *a, const char *txt, uint32_t color, int ms);
void gfa_prefs_save(app_t *a);
void gfa_hud_show(app_t *a, bool top, bool bottom);
void gfa_invalidate_all(app_t *a);
lv_obj_t *gfa_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                     uint32_t accent, const lv_font_t *font, lv_event_cb_t cb, void *data);
lv_obj_t *gfa_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y, int w);
lv_obj_t *gfa_panel(lv_obj_t *parent, bool dim);
void gfa_show_panel(app_t *a, lv_obj_t *p);
void gf_fmt_dist(const app_t *a, char *buf, int n, float metres);

/* gf_play.c */
void gfp_round_start(app_t *a);
void gfp_hole_start(app_t *a);
void gfp_worker_start(app_t *a);
void gfp_worker_stop(void);
bool gfp_busy(void);
void gfp_boot(app_t *a);
void gfp_poll(app_t *a);
/* colour the golfer (in the worker): the outfit, which sequences, and
 * whether the frames coloured so far are thrown away first */
void gfp_outfit(app_t *a, const uint8_t eq[CAT_N], unsigned mask, bool recolor);
void gfa_booted(app_t *a);
void gfa_menu_ready(app_t *a);
void gfp_frame(app_t *a, int dt);
void gfp_touch(app_t *a, int x, int y, int ev);     /* ev: 0 press, 1 drag, 2 release */
void gfp_hit_pressed(app_t *a);
void gfp_club_step(app_t *a, int d);
bool gfp_back(app_t *a);
void gfp_menu_scene(app_t *a);
void gfp_menu_frame(app_t *a, int dt);
void gfp_react_begin(app_t *a, int seq);
void gfp_react_frame(app_t *a);
void gfp_apply_remote_shot(app_t *a, int club, float aim, float power, float acc);
void gfo_begin(app_t *a);
void gfo_end(app_t *a);
void gfo_mark(app_t *a, int x0, int y0, int x1, int y1);
void gfo_set_bg(app_t *a, const uint16_t *bg);

/* gf_shop.c */
void gfs_build(app_t *a);
void gfs_open(app_t *a);
void gfs_frame(app_t *a, int dt);
void gfs_touch(app_t *a, int x, int y, int ev);

/* gf_link.c */
bool gfl_available(app_t *a, char *name, int n);
void gfl_begin(app_t *a);
void gfl_end(app_t *a);
void gfl_tick(app_t *a);
void gfl_send_shot(app_t *a, int club, float aim, float power, float acc);
bool gfl_is_local_turn(app_t *a);
