/*
 * AmoledOS - Radio: internet stations on a front panel from the sixties.
 *
 * The sound is the firmware's: aos_hal_radio_play() hands the list of keys
 * to the player, which keeps the station playing with the app closed (and
 * the control centre drives it). This app is the panel: the dial with the
 * station, the song and its cover, the needle over the numbered scale, the
 * transport, the volume, and nine keys the portal's /radio page fills.
 *
 * The keys live in NVS as rad0..rad8 = "name \x1F url", written by the
 * portal; rad_gen goes up on every save and the app rebuilds its keys when
 * it moves (APP-GUIDE section 10). The key's logo, if the portal could make
 * one, is radio/logoN.jpg on the card.
 */
#pragma once

#include "lvgl.h"
#include "aos_hal.h"
#include "radio_art.h"

#define RADIO_KEYS      9
#define SCALE_W         320
#define SCALE_H         28
#define DIAL_COVER      96

typedef struct {
    lv_obj_t *root;
    bool      closing;

    /* the dial */
    lv_obj_t *dial;
    lv_obj_t *cover;            /* canvas DIAL_COVER square                 */
    uint16_t *cover_px;
    lv_obj_t *station, *title, *artist, *meta, *onair;
    lv_obj_t *scale;            /* canvas, ARGB8888, drawn once             */
    uint8_t  *scale_px;
    lv_obj_t *scale_num[RADIO_KEYS];
    lv_obj_t *needle;
    int       needle_x, needle_to;      /* in 1/16 px */

    /* the panel */
    lv_obj_t *b_mute, *b_prev, *b_play, *b_next, *b_info;
    lv_obj_t *i_mute, *i_play;
    lv_obj_t *vol, *vol_lbl;
    lv_obj_t *key[RADIO_KEYS], *key_led[RADIO_KEYS], *key_lbl[RADIO_KEYS];
    lv_obj_t *status;

    /* the info card */
    lv_obj_t *info, *info_cover, *info_title, *info_text;
    uint16_t *info_px;

    lv_timer_t *anim;

    /* the keys */
    aos_radio_station_t st[RADIO_KEYS];
    int32_t   gen;
    bool      logo[RADIO_KEYS];

    /* what is on screen, to write only what changed (LVGL repaints the same
     * text as if it were new) */
    char      s_station[48], s_title[96], s_artist[96], s_meta[96], s_status[96];
    int       s_active, s_state, s_vol, s_muted, s_onair;
    uint32_t  s_title_gen;
    int       s_cover_kind;     /* 0 drawn, 1 logo, 2 art                   */
    int       s_cover_slot;
    uint32_t  info_refresh;

    radio_art_t art;
} radio_t;

/* radio_ui.c */
void radio_ui_build(radio_t *r, lv_obj_t *root);
void radio_ui_keys(radio_t *r);                 /* labels and LEDs of the keys */
void radio_ui_cover(radio_t *r, const uint16_t *px160);  /* NULL: the drawn one */
void radio_ui_info_open(radio_t *r);
void radio_ui_info_close(radio_t *r);
void radio_ui_needle_to(radio_t *r, int key);   /* -1: rest at the left */
void radio_ui_anim(lv_timer_t *t);

/* radio.c */
void radio_on_key(radio_t *r, int i);
void radio_on_play(radio_t *r);
void radio_on_step(radio_t *r, int step);
void radio_on_mute(radio_t *r);
void radio_on_volume(radio_t *r, int v);
void radio_info_text(radio_t *r, char *out, int cap);
