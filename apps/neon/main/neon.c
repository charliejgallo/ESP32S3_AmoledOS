/*
 * AmoledOS - NEON SNAKES
 *
 * Neon snakes eat neon fruit on a screen that is black everywhere else. No
 * score, no HUD: the snakes, the fruit and the frame of the arena.
 *
 *   Normal   one snake, one fruit at a time, 21x24 cells of 16 px: the
 *            classic. Hit a wall or yourself and it is over.
 *   Combate  34x40 cells of 10 px - the camera further away - and four
 *            snakes: you against three bots, or you and the watch you are
 *            paired with against two. A snake that dies flashes and leaves
 *            its body behind as sparks; everybody comes back after a while.
 *
 * Steering is by touch (swipe the way you want to go, or tap on the side of
 * the head you want to turn to) or by tilting the watch; the choice is kept.
 *
 * The files:
 *   ns_game.c  the rules: deterministic, integers only, no LVGL
 *   ns_art.c   every sprite, drawn by code at the cell size of the mode
 *   ns_draw.c  the compositor: the screen repainted cell by cell, only
 *              where something changed
 *   neon.c     this: menus, input, the link, the timer
 *   tools/ns_harness.c  checks the rules, the compositor and the lockstep
 *
 * TWO WATCHES are one match run twice (lockstep, as in Truco). The host (the
 * lower MAC) owns the clock: on every step it decides where both humans turn
 * - its own finger and the guest's TURN messages - and sends that decision
 * in a STEP on the reliable channel before applying it; the guest applies
 * exactly the STEPs it receives, in order, and nothing else. Both engines
 * start from the same seed, so the boards stay equal; each STEP carries the
 * host's hash of the board after it, and the guest checks it.
 */
#include "aos_app.h"
#include "aos_fonts.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_icon_ops.h"
#include "aos_theme.h"
#include "aos_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ns_art.h"
#include "ns_draw.h"
#include "ns_game.h"

#define FRAME_MS        33
#define IMU_EVERY       3               /* frames between two IMU reads    */
#define TILT_ON         0.17f           /* g: past this the snake turns     */
#define SWIPE_PX        22              /* a drag this long is a turn       */
#define STEP_COMBAT_MS  125
#define PREF_CTRL       "neon_ctrl"
#define PROTO           1
#define LINK_APP        "neon"

/* -------------------------------------------------------------------------- */
/* What goes over the air                                                      */

enum { MSG_HELLO = 1, MSG_START, MSG_READY, MSG_STEP, MSG_TURN, MSG_BYE };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t nonce;
    uint8_t  mac[6];
} msg_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t seed, host_nonce, guest_nonce;
} msg_start_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t host_nonce;
} msg_ready_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint32_t step;
    uint8_t  d0, d1;
    uint32_t hash;
} msg_step_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  dir;
} msg_turn_t;

/* -------------------------------------------------------------------------- */
/* The app                                                                     */

typedef enum {
    SCR_MENU = 0,
    SCR_COMBAT,         /* one player or two                       */
    SCR_LOBBY,          /* looking for the other watch             */
    SCR_PLAY,
} screen_t;

typedef enum {
    OV_NONE = 0,
    OV_PAUSE,
    OV_OVER,            /* normal mode: the snake is gone           */
    OV_GONE,            /* two watches: the other one left          */
} overlay_t;

enum { PLAY_NORMAL, PLAY_SOLO, PLAY_MULTI };

typedef struct {
    lv_obj_t   *root, *canvas, *touch, *msg;
    lv_obj_t   *btn[3], *btn_lbl[3];
    lv_timer_t *timer;

    uint16_t   *fb, *bg, *menu;     /* menu: the title screen, drawn once */
    ns_art_t    art_normal, art_combat;
    bool        have_normal, have_combat;
    ns_game_t  *g;
    ns_view_t   view;

    screen_t    scr;
    overlay_t   ov;
    uint8_t     play;               /* PLAY_*                                */
    uint8_t     me;                 /* my snake's index                      */
    bool        tilt;               /* steering by tilting                   */
    bool        autoplay;           /* NS_AUTO: the bot steers my snake      */

    /* input */
    uint8_t     q[2], nq;           /* turns waiting for the next step       */
    lv_point_t  p0;
    bool        pressed, dragged;
    uint32_t    last_turn_ms;
    uint8_t     imu_skip;
    bool        tilt_zeroed;
    float       zero_a, zero_b;
    int8_t      tilt_dir;           /* the last direction the tilt asked for */

    uint32_t    last_step_ms, over_at_ms;

    /* the link */
    bool        link_up, have_partner, is_host, role_known, started;
    uint32_t    nonce, their_nonce, lobby_ms, hello_ms, last_rx_ms, away_ms;
    uint8_t     their_mac[6];
    char        their_name[AOS_LINK_NAME_MAX + 1];
    uint8_t     guest_q[4], guest_nq;
    bool        desync_told;

    /* deferred from events and the button */
    bool        want_exit, leaving, closing, want_pause, want_back;
    int8_t      want_btn;           /* a button was clicked: its index      */

    /* stats */
    uint32_t    stat_t0, stat_frames, stat_px;
} app_t;

static void show_screen(app_t *a, screen_t s);
static void show_overlay(app_t *a, overlay_t ov);
static void start_play(app_t *a, uint8_t play);
static void link_down(app_t *a);
static void guest_apply(app_t *a, const msg_step_t *m);

static const uint32_t CYAN = 0x19F5FF, MAGENTA = 0xFF2FD0, LIME = 0x8CFF2E, AMBER = 0xFFC21A,
                      VIOLET = 0x8A6CFF;

/* -------------------------------------------------------------------------- */
/* Pushing pixels                                                              */

static void invalidate(app_t *a, int x0, int y0, int x1, int y1)
{
    lv_area_t co;
    lv_obj_get_coords(a->canvas, &co);
    lv_area_t ar = { co.x1 + x0, co.y1 + y0, co.x1 + x1 - 1, co.y1 + y1 - 1 };
    lv_obj_invalidate_area(a->canvas, &ar);
}

static void push_view(app_t *a)
{
    for (int i = 0; i < a->view.nrects; i++) {
        const ns_rect_t *r = &a->view.rects[i];
        invalidate(a, r->x0, r->y0, r->x1, r->y1);
    }
    a->stat_px += a->view.pixels;
}

/* The menus' background: the title and a row of fruit, on black. */
static void draw_menu_bg(app_t *a)
{
    const size_t px = (size_t)NS_SCREEN_W * NS_SCREEN_H * 2;
    /* the tube letters are a few hundred thousand distances: once is enough */
    if (a->menu) {
        memcpy(a->fb, a->menu, px);
        invalidate(a, 0, 0, NS_SCREEN_W, NS_SCREEN_H);
        return;
    }
    memset(a->fb, 0, px);
    ns_art_title(a->fb, NS_SCREEN_W, NS_SCREEN_H, 30);
    if (a->have_normal) {
        const ns_art_t *art = &a->art_normal;
        const int S = art->size, pitch = 44;
        int x0 = (NS_SCREEN_W - NS_FRUIT_COUNT * pitch) / 2 + (pitch - S) / 2;
        for (int k = 0; k < NS_FRUIT_COUNT; k++) {
            const uint16_t *spr = art->fruit[k][1];
            for (int y = 0; y < S; y++) {
                uint16_t *d = a->fb + (188 + y) * NS_SCREEN_W + x0 + k * pitch;
                for (int x = 0; x < S; x++) d[x] = ns_max565(d[x], spr[y * S + x]);
            }
        }
    }
    a->menu = malloc(px);
    if (a->menu) memcpy(a->menu, a->fb, px);
    invalidate(a, 0, 0, NS_SCREEN_W, NS_SCREEN_H);
}

/* -------------------------------------------------------------------------- */
/* Buttons: three neon outlines, reused by every screen                        */

static void btn_set(app_t *a, int i, const char *text, uint32_t rgb, int y)
{
    lv_obj_t *b = a->btn[i];
    if (!text) {
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(b, y);
    lv_obj_set_style_border_color(b, lv_color_hex(rgb), 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_color(a->btn_lbl[i], lv_color_hex(rgb), 0);
    /* the long ones ("CONTROL: INCLINACIÓN", and every German word) drop to
     * the body font rather than spill over the outline */
    lv_obj_set_style_text_font(a->btn_lbl[i], strlen(text) > 13 ? aos_font_body : aos_font_title, 0);
    lv_label_set_text(a->btn_lbl[i], text);
}

static void btn_cb(lv_event_t *e)
{
    app_t *a = lv_event_get_user_data(e);
    if (a->closing) return;
    lv_obj_t *t = lv_event_get_current_target(e);
    for (int i = 0; i < 3; i++) {
        if (t == a->btn[i]) a->want_btn = (int8_t)i;       /* acted on in the timer */
    }
}

static lv_obj_t *make_btn(app_t *a, int i)
{
    lv_obj_t *b = lv_obj_create(a->root);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 260, 52);
    lv_obj_set_x(b, (NS_SCREEN_W - 260) / 2);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_shadow_width(b, 16, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A28), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, a);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, aos_font_title, 0);
    lv_obj_center(l);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    a->btn_lbl[i] = l;
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

static const char *ctrl_text(const app_t *a)
{
    return a->tilt ? _("CONTROL: INCLINACIÓN") : _("CONTROL: TÁCTIL");
}

static void set_msg(app_t *a, const char *text)
{
    if (!text || !*text) {
        lv_obj_add_flag(a->msg, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strcmp(lv_label_get_text(a->msg), text) != 0) {
        lv_label_set_text(a->msg, text);
    }
    lv_obj_remove_flag(a->msg, LV_OBJ_FLAG_HIDDEN);
}

/* -------------------------------------------------------------------------- */
/* Screens                                                                     */

#define BTN_Y0  238
#define BTN_Y1  300
#define BTN_Y2  362

static void show_screen(app_t *a, screen_t s)
{
    a->scr = s;
    a->ov  = OV_NONE;
    set_msg(a, NULL);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_HIDDEN);
    switch (s) {
    case SCR_MENU:
        draw_menu_bg(a);
        btn_set(a, 0, _("NORMAL"), CYAN, BTN_Y0);
        btn_set(a, 1, _("COMBATE"), MAGENTA, BTN_Y1);
        btn_set(a, 2, ctrl_text(a), a->tilt ? LIME : AMBER, BTN_Y2);
        break;
    case SCR_COMBAT:
        draw_menu_bg(a);
        btn_set(a, 0, _("UN JUGADOR"), MAGENTA, BTN_Y0);
        btn_set(a, 1, _("MULTIJUGADOR"), LIME, BTN_Y1);
        btn_set(a, 2, _("VOLVER"), VIOLET, BTN_Y2);
        break;
    case SCR_LOBBY:
        draw_menu_bg(a);
        btn_set(a, 0, NULL, 0, 0);
        btn_set(a, 1, NULL, 0, 0);
        btn_set(a, 2, _("CANCELAR"), VIOLET, BTN_Y2);
        break;
    case SCR_PLAY:
        btn_set(a, 0, NULL, 0, 0);
        btn_set(a, 1, NULL, 0, 0);
        btn_set(a, 2, NULL, 0, 0);
        lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void show_overlay(app_t *a, overlay_t ov)
{
    a->ov = ov;
    switch (ov) {
    case OV_NONE:
        btn_set(a, 0, NULL, 0, 0);
        btn_set(a, 1, NULL, 0, 0);
        btn_set(a, 2, NULL, 0, 0);
        set_msg(a, NULL);
        break;
    case OV_PAUSE:
        btn_set(a, 0, _("SEGUIR"), CYAN, 150);
        btn_set(a, 1, _("MENÚ"), MAGENTA, 212);
        btn_set(a, 2, NULL, 0, 0);
        break;
    case OV_OVER:
        btn_set(a, 0, _("OTRA VEZ"), CYAN, 150);
        btn_set(a, 1, _("MENÚ"), MAGENTA, 212);
        btn_set(a, 2, NULL, 0, 0);
        break;
    case OV_GONE:
        btn_set(a, 0, NULL, 0, 0);
        btn_set(a, 1, _("MENÚ"), MAGENTA, 212);
        btn_set(a, 2, NULL, 0, 0);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* Steering                                                                    */

/* The direction the snake will have once the queue is done: what a new turn
 * is measured against. */
static uint8_t last_dir(const app_t *a)
{
    if (a->nq) return a->q[a->nq - 1];
    return a->g->s[a->me].dir;
}

static void queue_turn(app_t *a, uint8_t d)
{
    if (a->scr != SCR_PLAY || a->ov == OV_OVER || a->ov == OV_GONE || !a->g) return;
    uint8_t cur = last_dir(a);
    if (d == cur || d == ((cur + 2) & 3)) return;
    if (a->nq < 2) {
        a->q[a->nq++] = d;
    } else {
        a->q[1] = d;            /* a third turn replaces the second */
    }
    a->last_turn_ms = (uint32_t)aos_hal_uptime_ms();
    if (a->play == PLAY_MULTI && !a->is_host) {
        msg_turn_t m = { MSG_TURN, PROTO, d };
        aos_hal_link_send_reliable(&m, sizeof m);
    }
}

static uint8_t pop_turn(app_t *a)
{
    if (!a->nq) return NS_NODIR;
    uint8_t d = a->q[0];
    a->q[0] = a->q[1];
    a->nq--;
    return d;
}

static void touch_cb(lv_event_t *e)
{
    app_t *a = lv_event_get_user_data(e);
    if (a->closing || a->scr != SCR_PLAY || a->ov != OV_NONE || !a->g) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        a->p0 = p;
        a->pressed = true;
        a->dragged = false;
        return;
    }
    if (!a->pressed) return;
    if (code == LV_EVENT_PRESSING) {
        if (a->tilt) return;
        int dx = p.x - a->p0.x, dy = p.y - a->p0.y;
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax >= SWIPE_PX || ay >= SWIPE_PX) {
            /* one drag can turn several times: each leg is measured from
             * where the last turn was taken */
            queue_turn(a, ax > ay ? (dx > 0 ? NS_RIGHT : NS_LEFT) : (dy > 0 ? NS_DOWN : NS_UP));
            a->p0 = p;
            a->dragged = true;
        }
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        a->pressed = false;
        if (a->dragged || code == LV_EVENT_PRESS_LOST) return;
        if (a->tilt) {
            a->tilt_zeroed = false;     /* a tap levels the tilt again */
            return;
        }
        /* a tap: turn towards the side of the head it landed on */
        const ns_snake_t *s = &a->g->s[a->me];
        if (!s->alive) return;
        int hx = a->view.ox + ns_seg_x(s, 0) * a->view.cell + a->view.cell / 2;
        int hy = a->view.oy + ns_seg_y(s, 0) * a->view.cell + a->view.cell / 2;
        uint8_t cur = last_dir(a);
        if (cur == NS_UP || cur == NS_DOWN) {
            queue_turn(a, p.x < hx ? NS_LEFT : NS_RIGHT);
        } else {
            queue_turn(a, p.y < hy ? NS_UP : NS_DOWN);
        }
    }
}

/* The chip's own swipe: on the board a fast swipe never reaches LVGL. */
static void take_gesture(app_t *a)
{
    int gst = aos_ui_take_gesture();
    if (gst == AOS_TOUCH_GESTURE_NONE) return;
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (a->scr != SCR_PLAY) {
        if (a->scr == SCR_MENU && gst == AOS_TOUCH_GESTURE_RIGHT) a->want_exit = true;
        else if (gst == AOS_TOUCH_GESTURE_RIGHT) a->want_back = true;
        return;
    }
    if (a->tilt || now - a->last_turn_ms < 250) return;   /* LVGL already turned */
    switch (gst) {
    case AOS_TOUCH_GESTURE_UP:    queue_turn(a, NS_UP);    break;
    case AOS_TOUCH_GESTURE_DOWN:  queue_turn(a, NS_DOWN);  break;
    case AOS_TOUCH_GESTURE_LEFT:  queue_turn(a, NS_LEFT);  break;
    case AOS_TOUCH_GESTURE_RIGHT: queue_turn(a, NS_RIGHT); break;
    }
}

/* Tilt: the board's axes as measured (HARDWARE.md): +ax is the bottom of the
 * screen, the right is -ay. Past TILT_ON on the stronger axis the snake
 * turns that way; back under half of it, the next tilt counts again. */
static void read_tilt(app_t *a)
{
    if (a->imu_skip) {
        a->imu_skip--;
        return;
    }
    a->imu_skip = IMU_EVERY - 1;
    aos_imu_t imu;
    if (!aos_hal_imu_read(&imu)) return;
    if (!a->tilt_zeroed) {
        a->zero_a = imu.ax;
        a->zero_b = imu.ay;
        a->tilt_zeroed = true;
        a->tilt_dir = -1;
        return;
    }
    float h = -(imu.ay - a->zero_b), v = imu.ax - a->zero_a;
    float ah = h < 0 ? -h : h, av = v < 0 ? -v : v;
    float m = ah > av ? ah : av;
    if (m < TILT_ON * 0.5f) {
        a->tilt_dir = -1;
        return;
    }
    if (m < TILT_ON) return;
    int8_t d = (int8_t)(ah > av ? (h > 0 ? NS_RIGHT : NS_LEFT) : (v > 0 ? NS_DOWN : NS_UP));
    if (d != a->tilt_dir) {
        a->tilt_dir = d;
        queue_turn(a, (uint8_t)d);
    }
}

/* -------------------------------------------------------------------------- */
/* Playing                                                                     */

static bool ensure_art(app_t *a, bool combat)
{
    uint32_t t0 = (uint32_t)aos_hal_uptime_ms();
    if (combat && !a->have_combat) {
        a->have_combat = ns_art_build(&a->art_combat, 10, NS_MAX_SNAKES);
    } else if (!combat && !a->have_normal) {
        a->have_normal = ns_art_build(&a->art_normal, 16, 1);
    } else {
        return combat ? a->have_combat : a->have_normal;
    }
    aos_hal_log("neon", "%s sprites drawn in %u ms", combat ? "combat" : "normal",
                (unsigned)((uint32_t)aos_hal_uptime_ms() - t0));
    return combat ? a->have_combat : a->have_normal;
}

static void begin_view(app_t *a)
{
    bool combat = a->g->mode == NS_MODE_COMBAT;
    ns_view_init(&a->view, a->fb, a->bg, combat ? &a->art_combat : &a->art_normal, a->g);
    ns_view_full(&a->view, a->g);
    ns_view_halo(&a->view, a->me, 75);
    push_view(a);
}

static void start_play(app_t *a, uint8_t play)
{
    bool combat = play != PLAY_NORMAL;
    if (!ensure_art(a, combat)) {
        aos_ui_toast(_("Sin memoria"), 2000);
        return;
    }
    a->play = play;
    uint32_t seed = (uint32_t)aos_hal_uptime_ms() * 2654435761u;
    if (play == PLAY_NORMAL) {
        ns_init(a->g, NS_MODE_NORMAL, seed, 1, 1);
        a->me = 0;
    } else if (play == PLAY_SOLO) {
        ns_init(a->g, NS_MODE_COMBAT, seed, 1, 4);
        a->me = 0;
    }
    /* PLAY_MULTI: the engine was already started by the handshake */
    a->nq = 0;
    a->guest_nq = 0;
    a->tilt_zeroed = false;
    a->last_step_ms = (uint32_t)aos_hal_uptime_ms();
    /* the partner's clocks start now, not at zero: a beacon that has not
     * come round yet is not a partner that left */
    a->away_ms = a->last_rx_ms = a->last_step_ms;
    a->over_at_ms = 0;
    show_screen(a, SCR_PLAY);
    begin_view(a);
}

static uint32_t step_ms(const app_t *a)
{
    if (a->g->mode == NS_MODE_COMBAT) return STEP_COMBAT_MS;
    /* the classic gets faster as the snake grows, down to 85 ms */
    int len = a->g->s[0].len;
    int ms = 150 - (len - 4) * 2;
    return (uint32_t)(ms < 85 ? 85 : ms);
}

/* What the bot would do for my snake, without touching the dice the engine
 * shares with the other watch. */
static uint8_t auto_choice(app_t *a)
{
    const ns_snake_t *s = &a->g->s[a->me];
    if (!s->alive || s->dying) return NS_NODIR;
    uint32_t keep = a->g->rng;
    uint8_t d = ns_bot_choice(a->g, a->me);
    a->g->rng = keep;
    return d;
}

static void after_step(app_t *a)
{
    ns_game_t *g = a->g;
    for (int i = 0; i < g->nev; i++) {
        const ns_event_t *e = &g->ev[i];
        bool mine = e->snake == a->me;
        switch (e->type) {
        case NS_EV_EAT:
            ns_view_fx(&a->view, NS_FX_RING, e->x, e->y, ns_fruit_rgb(e->kind));
            if (mine) aos_hal_beep(e->kind >= NS_SPARK_0 ? 1500 : 1100, 25);
            break;
        case NS_EV_DIE:
            ns_view_fx(&a->view, NS_FX_BURST, e->x, e->y, ns_snake_rgb(g->s[e->snake].colour));
            if (mine) {
                aos_hal_beep(220, 90);
                aos_hal_beep(140, 160);
            }
            break;
        case NS_EV_SPAWN:
            if (mine && g->step_no > 1) ns_view_halo(&a->view, a->me, 60);
            break;
        }
    }
    g->nev = 0;
}

static void local_step(app_t *a)
{
    uint8_t dirs[NS_MAX_SNAKES];
    memset(dirs, NS_NODIR, sizeof dirs);
    dirs[a->me] = a->autoplay ? auto_choice(a) : pop_turn(a);
    ns_step(a->g, dirs);
    after_step(a);
}

/* -------------------------------------------------------------------------- */
/* Two watches                                                                 */

static void send_hello(app_t *a)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    msg_hello_t h = { .type = MSG_HELLO, .proto = PROTO, .nonce = a->nonce };
    memcpy(h.mac, st.own_mac, 6);
    aos_hal_link_send_partner(&h, sizeof h);
}

static void link_down(app_t *a)
{
    if (!a->link_up) return;
    uint8_t bye[2] = { MSG_BYE, PROTO };
    for (int i = 0; i < 3; i++) aos_hal_link_send_partner(bye, sizeof bye);
    aos_hal_link_offer("");
    aos_hal_link_stop();
    a->link_up = false;
    a->role_known = a->started = false;
}

static void enter_lobby(app_t *a)
{
    show_screen(a, SCR_LOBBY);
    if (!a->link_up) {
        a->link_up = aos_hal_link_start();
        if (a->link_up) aos_hal_link_offer(LINK_APP);
    }
    a->have_partner = false;
    a->role_known = a->started = false;
    a->their_nonce = 0;
    a->desync_told = false;
    a->nonce = ((uint32_t)aos_hal_uptime_ms() * 2654435761u) | 1u;
    a->lobby_ms = (uint32_t)aos_hal_uptime_ms();
    a->hello_ms = 0;
    aos_hal_link_reliable_reset();
    set_msg(a, a->link_up ? _("Buscando al otro reloj...") : _("El enlace no arrancó"));
}

static void gone(app_t *a, const char *why)
{
    if (a->scr != SCR_PLAY && a->scr != SCR_LOBBY) return;
    char buf[96];
    snprintf(buf, sizeof buf, why, a->their_name[0] ? a->their_name : "?");
    if (a->scr == SCR_PLAY) {
        show_overlay(a, OV_GONE);
    }
    set_msg(a, buf);
    a->started = false;
}

/* Host: the other watch is here and in the app - deal. */
static void host_start(app_t *a)
{
    msg_start_t m = { .type = MSG_START, .proto = PROTO,
                      .seed = ((uint32_t)aos_hal_uptime_ms() * 2246822519u) ^ a->nonce,
                      .host_nonce = a->nonce, .guest_nonce = a->their_nonce };
    ns_init(a->g, NS_MODE_COMBAT, m.seed, 2, 4);
    aos_hal_link_send_reliable(&m, sizeof m);
    a->started = false;                 /* until READY */
}

static void handle_frame(app_t *a, const uint8_t *d, int len, bool reliable)
{
    if (len < 2 || d[1] != PROTO) return;
    a->last_rx_ms = (uint32_t)aos_hal_uptime_ms();
    switch (d[0]) {
    case MSG_HELLO: {
        if (len < (int)sizeof(msg_hello_t)) return;
        const msg_hello_t *h = (const msg_hello_t *)d;
        if (a->role_known && a->their_nonce && h->nonce != a->their_nonce && a->scr == SCR_PLAY) {
            /* the other one left and came back: that match is over */
            gone(a, _("%s salió del juego"));
            return;
        }
        if (a->scr != SCR_LOBBY) return;
        bool fresh = h->nonce != a->their_nonce;
        a->their_nonce = h->nonce;
        memcpy(a->their_mac, h->mac, 6);
        if (!a->role_known) {
            aos_link_stats_t st;
            aos_hal_link_stats(&st);
            a->is_host = memcmp(st.own_mac, h->mac, 6) < 0;
            a->role_known = true;
            send_hello(a);              /* so the other decides too */
        }
        if (a->is_host && fresh) host_start(a);
        break;
    }
    case MSG_START: {
        if (!reliable || a->is_host || a->scr != SCR_LOBBY || len < (int)sizeof(msg_start_t)) return;
        const msg_start_t *m = (const msg_start_t *)d;
        if (m->guest_nonce != a->nonce) return;     /* a deal for an older me */
        /* On the boards the START (reliable) can arrive BEFORE the host's
         * HELLO (fast): whoever the host is comes from here, or a HELLO that
         * shows up later looks like the host re-entering the app. */
        a->their_nonce = m->host_nonce;
        ns_init(a->g, NS_MODE_COMBAT, m->seed, 2, 4);
        a->me = 1;
        a->role_known = true;
        a->started = true;
        msg_ready_t r = { MSG_READY, PROTO, m->host_nonce };
        aos_hal_link_send_reliable(&r, sizeof r);
        start_play(a, PLAY_MULTI);
        break;
    }
    case MSG_READY: {
        if (!reliable || !a->is_host || a->scr != SCR_LOBBY || len < (int)sizeof(msg_ready_t)) return;
        const msg_ready_t *r = (const msg_ready_t *)d;
        if (r->host_nonce != a->nonce) return;
        a->me = 0;
        a->started = true;
        start_play(a, PLAY_MULTI);
        break;
    }
    case MSG_STEP: {
        if (!reliable || a->is_host || a->scr != SCR_PLAY || len < (int)sizeof(msg_step_t)) return;
        msg_step_t m;
        memcpy(&m, d, sizeof m);
        guest_apply(a, &m);
        break;
    }
    case MSG_TURN:
        if (!reliable || !a->is_host || len < (int)sizeof(msg_turn_t)) return;
        if (a->guest_nq < 4) a->guest_q[a->guest_nq++] = ((const msg_turn_t *)d)->dir;
        break;
    case MSG_BYE:
        gone(a, _("%s salió del juego"));
        break;
    }
}

static void link_tick(app_t *a)
{
    if (!a->link_up) return;
    aos_link_frame_t f;
    while (aos_hal_link_recv_reliable(&f) > 0) handle_frame(a, f.data, f.len, true);
    while (aos_hal_link_recv(&f) > 0) handle_frame(a, f.data, f.len, false);
    if (aos_hal_link_reliable_lost()) {
        aos_hal_link_reliable_reset();
        if (a->scr == SCR_PLAY && a->play == PLAY_MULTI) gone(a, _("Sin señal de %s"));
    }

    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    aos_link_partner_t p;
    if (aos_hal_link_partner(&p) && p.valid) {
        a->have_partner = true;
        /* the partner's MAC from Link, not from a HELLO that may never have
         * been read: the beacon check below compares against it */
        memcpy(a->their_mac, p.mac, 6);
        snprintf(a->their_name, sizeof a->their_name, "%s", p.name);
    }

    if (a->scr == SCR_LOBBY) {
        if (!a->have_partner) {
            if (now - a->lobby_ms > 2000) set_msg(a, _("Sin pareja: apareá los relojes en Enlace"));
            return;
        }
        if (now - a->hello_ms > 400) {
            a->hello_ms = now;
            send_hello(a);
        }
        char buf[96];
        snprintf(buf, sizeof buf, _("Esperando a %s..."), a->their_name);
        set_msg(a, buf);
        return;
    }

    if (a->scr == SCR_PLAY && a->play == PLAY_MULTI && a->ov != OV_GONE) {
        /* the other watch left the app: its beacon stops offering us */
        aos_link_neighbour_t nb[AOS_LINK_NEIGHBOURS];
        int n = aos_hal_link_neighbours(nb, AOS_LINK_NEIGHBOURS);
        bool offering = false;
        for (int i = 0; i < n; i++) {
            if (memcmp(nb[i].mac, a->their_mac, 6) == 0 && strcmp(nb[i].app, LINK_APP) == 0) offering = true;
        }
        if (offering) a->away_ms = now;
        else if (now - a->away_ms > 6000) gone(a, _("%s salió del juego"));
        if (!a->is_host && now - a->last_rx_ms > 4000) gone(a, _("Sin señal de %s"));
    }
}

static void host_step(app_t *a)
{
    if (aos_hal_link_reliable_pending() > 8) return;     /* let the air catch up */
    uint8_t dirs[NS_MAX_SNAKES];
    memset(dirs, NS_NODIR, sizeof dirs);
    dirs[0] = a->autoplay ? auto_choice(a) : pop_turn(a);
    if (a->guest_nq) {
        dirs[1] = a->guest_q[0];
        memmove(a->guest_q, a->guest_q + 1, --a->guest_nq);
    }
    ns_step(a->g, dirs);
    msg_step_t m = { .type = MSG_STEP, .proto = PROTO, .step = a->g->step_no,
                     .d0 = dirs[0], .d1 = dirs[1], .hash = ns_hash(a->g) };
    aos_hal_link_send_reliable(&m, sizeof m);
    after_step(a);
}

/* Guest: a STEP from the host, applied as soon as it arrives. There is no
 * inbox to overflow: if this watch falls behind, the link's own receive ring
 * fills, stops acknowledging, and the host - which will not step with more
 * than eight frames unacknowledged - waits for it. */
static void guest_apply(app_t *a, const msg_step_t *m)
{
    if (m->step != a->g->step_no + 1) {
        aos_hal_log("neon", "step %u arrived, expected %u", (unsigned)m->step,
                    (unsigned)(a->g->step_no + 1));
        return;
    }
    uint8_t dirs[NS_MAX_SNAKES];
    memset(dirs, NS_NODIR, sizeof dirs);
    dirs[0] = m->d0;
    dirs[1] = m->d1;
    ns_step(a->g, dirs);
    /* my own turns only count once the host has decided them; one the host
     * applied, or one that no longer makes sense (already the heading, or a
     * reversal), leaves the queue, or it would block every turn after it */
    if (m->d1 != NS_NODIR && a->nq) pop_turn(a);
    const ns_snake_t *me = &a->g->s[a->me];
    while (a->nq && (a->q[0] == me->dir || a->q[0] == ((me->dir + 2) & 3))) pop_turn(a);
    if (ns_hash(a->g) != m->hash && !a->desync_told) {
        a->desync_told = true;
        aos_hal_log("neon", "DESYNC at step %u", (unsigned)m->step);
    }
    after_step(a);
}

/* NS_AUTO on the guest: the bot's choice goes to the host as a turn, like a
 * finger would. */
static void guest_auto(app_t *a)
{
    if (a->autoplay && a->nq == 0) {
        uint8_t d = auto_choice(a);
        if (d != NS_NODIR && d != a->g->s[a->me].dir) queue_turn(a, d);
    }
}

/* -------------------------------------------------------------------------- */
/* The timer                                                                   */

static void on_button(app_t *a, int i)
{
    if (a->ov == OV_PAUSE) {
        if (i == 0) {
            show_overlay(a, OV_NONE);
            a->last_step_ms = (uint32_t)aos_hal_uptime_ms();
            ns_view_full(&a->view, a->g);   /* the buttons leave their glow */
            push_view(a);
        } else {
            link_down(a);
            show_screen(a, SCR_MENU);
        }
        return;
    }
    if (a->ov == OV_OVER) {
        if (i == 0) start_play(a, PLAY_NORMAL);
        else show_screen(a, SCR_MENU);
        return;
    }
    if (a->ov == OV_GONE) {
        link_down(a);
        show_screen(a, SCR_MENU);
        return;
    }
    switch (a->scr) {
    case SCR_MENU:
        if (i == 0) start_play(a, PLAY_NORMAL);
        else if (i == 1) show_screen(a, SCR_COMBAT);
        else {
            a->tilt = !a->tilt;
            aos_hal_pref_set_i32(PREF_CTRL, a->tilt ? 1 : 0);
            btn_set(a, 2, ctrl_text(a), a->tilt ? LIME : AMBER, BTN_Y2);
        }
        break;
    case SCR_COMBAT:
        if (i == 0) start_play(a, PLAY_SOLO);
        else if (i == 1) enter_lobby(a);
        else show_screen(a, SCR_MENU);
        break;
    case SCR_LOBBY:
        link_down(a);
        show_screen(a, SCR_COMBAT);
        break;
    default:
        break;
    }
}

static void go_back(app_t *a)
{
    if (a->scr == SCR_PLAY) {
        if (a->ov == OV_NONE) show_overlay(a, OV_PAUSE);
        else on_button(a, 1);           /* from any overlay: to the menu */
    } else if (a->scr == SCR_LOBBY) {
        on_button(a, 2);
    } else if (a->scr == SCR_COMBAT) {
        show_screen(a, SCR_MENU);
    } else {
        a->want_exit = true;
    }
}

static void frame(lv_timer_t *timer)
{
    app_t *a = lv_timer_get_user_data(timer);
    if (a->want_exit) {
        a->want_exit = false;
        a->leaving = true;
        aos_ui_back();                  /* destroys the app: nothing after */
        return;
    }
    if (a->want_btn >= 0) {
        int i = a->want_btn;
        a->want_btn = -1;
        on_button(a, i);
    }
    if (a->want_pause || a->want_back) {
        a->want_pause = a->want_back = false;
        go_back(a);
    }
    take_gesture(a);
    link_tick(a);

    if (a->scr != SCR_PLAY) return;
    ns_game_t *g = a->g;
    uint32_t now = (uint32_t)aos_hal_uptime_ms();

    if (a->tilt && a->ov == OV_NONE) read_tilt(a);

    bool running = a->ov == OV_NONE || (a->play == PLAY_MULTI && a->ov == OV_PAUSE);
    if (running && !g->over) {
        if (a->play == PLAY_MULTI) {
            if (!a->is_host) {
                guest_auto(a);
            } else if (now - a->last_step_ms >= step_ms(a)) {
                a->last_step_ms += step_ms(a);
                if (now - a->last_step_ms > 500) a->last_step_ms = now;
                host_step(a);
            }
        } else if (now - a->last_step_ms >= step_ms(a)) {
            a->last_step_ms += step_ms(a);
            if (now - a->last_step_ms > 500) a->last_step_ms = now;
            local_step(a);
        }
    }
    if (g->over && a->ov == OV_NONE) {
        if (!a->over_at_ms) a->over_at_ms = now;
        if (now - a->over_at_ms > 700) {
            if (a->autoplay) start_play(a, PLAY_NORMAL);
            else show_overlay(a, OV_OVER);
        }
    }

    ns_view_frame(&a->view, g, (uint8_t)((now / 150) & 3));
    push_view(a);

    a->stat_frames++;
    if (now - a->stat_t0 >= 5000) {
        aos_hal_log("neon", "%u fps, %u %% of the screen per frame%s",
                    (unsigned)(a->stat_frames * 1000 / (now - a->stat_t0)),
                    (unsigned)(a->stat_frames ? a->stat_px / a->stat_frames * 100 / (NS_SCREEN_W * NS_SCREEN_H) : 0),
                    a->play == PLAY_MULTI ? (a->is_host ? " (host)" : " (guest)") : "");
        a->stat_t0 = now;
        a->stat_frames = a->stat_px = 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Life cycle                                                                  */

static bool app_back(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    if (!a || a->leaving) return false;
    if (a->scr == SCR_MENU) return false;
    a->want_back = true;
    return true;
}

static bool app_button(aos_app_t *self, void *inst, int action)
{
    (void)self;
    app_t *a = inst;
    if (!a || a->leaving) return false;
    if (action == AOS_BUTTON_LONG) return false;            /* the clock, as always */
    if (a->scr == SCR_MENU) return false;                   /* click = leave the app */
    if (action == AOS_BUTTON_CLICK) a->want_pause = true;   /* on the next frame */
    return true;
}

static void app_hide(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    if (a && a->scr == SCR_PLAY && a->ov == OV_NONE && a->play != PLAY_MULTI) {
        show_overlay(a, OV_PAUSE);
    }
}

static void *app_create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    app_t *a = lv_malloc_zeroed(sizeof *a);
    if (!a) return NULL;
    a->want_btn = -1;
    size_t px = (size_t)NS_SCREEN_W * NS_SCREEN_H * 2;
    a->fb = malloc(px);
    a->bg = malloc(px);
    a->g  = calloc(1, sizeof(ns_game_t));
    if (!a->fb || !a->bg || !a->g) {
        free(a->fb); free(a->bg); free(a->g);
        lv_free(a);
        return NULL;
    }
    memset(a->fb, 0, px);
    memset(a->bg, 0, px);
    ensure_art(a, false);

    int32_t ctrl = 0;
    if (aos_hal_pref_get_i32(PREF_CTRL, &ctrl)) a->tilt = ctrl == 1;

    a->root = root;
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    a->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(a->canvas, a->fb, NS_SCREEN_W, NS_SCREEN_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(a->canvas, NS_SCREEN_W, NS_SCREEN_H);
    lv_obj_set_pos(a->canvas, 0, 0);
    lv_image_set_antialias(a->canvas, false);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_SCROLLABLE);

    a->touch = lv_obj_create(root);
    lv_obj_remove_style_all(a->touch);
    lv_obj_set_size(a->touch, NS_SCREEN_W, NS_SCREEN_H);
    lv_obj_set_pos(a->touch, 0, 0);
    lv_obj_add_flag(a->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESSING, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_RELEASED, a);
    lv_obj_add_event_cb(a->touch, touch_cb, LV_EVENT_PRESS_LOST, a);

    a->msg = lv_label_create(root);
    lv_obj_set_width(a->msg, NS_SCREEN_W - 60);
    lv_label_set_long_mode(a->msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(a->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(a->msg, aos_font_body, 0);
    lv_obj_set_style_text_color(a->msg, lv_color_hex(0xC8C8FF), 0);
    /* over a game it sits on the snakes: a dark plate keeps it legible */
    lv_obj_set_style_bg_color(a->msg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(a->msg, LV_OPA_80, 0);
    lv_obj_set_style_radius(a->msg, 12, 0);
    lv_obj_set_style_pad_ver(a->msg, 6, 0);
    lv_obj_align(a->msg, LV_ALIGN_TOP_MID, 0, 262);
    lv_obj_remove_flag(a->msg, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(a->msg, "");

    for (int i = 0; i < 3; i++) a->btn[i] = make_btn(a, i);

    a->stat_t0 = (uint32_t)aos_hal_uptime_ms();
    show_screen(a, SCR_MENU);

    /* Development switches; on the board getenv() is always NULL.
     *   NS_MODE=normal|solo|multi   straight into a game
     *   NS_AUTO=1                   the bot steers my snake too
     *   NS_TILT=1                   steering by tilt */
    const char *env;
    if ((env = getenv("NS_AUTO")) && env[0] == '1') a->autoplay = true;
    if ((env = getenv("NS_TILT")) && env[0]) a->tilt = env[0] == '1';
    if ((env = getenv("NS_MODE")) && env[0]) {
        if (strcmp(env, "normal") == 0) start_play(a, PLAY_NORMAL);
        else if (strcmp(env, "solo") == 0) start_play(a, PLAY_SOLO);
        else if (strcmp(env, "multi") == 0) enter_lobby(a);
        else if (strcmp(env, "combat") == 0) show_screen(a, SCR_COMBAT);
    }

    a->timer = lv_timer_create(frame, FRAME_MS, a);
    uint32_t heap_int = 0, heap_psram = 0;
    aos_hal_heap_info(&heap_int, &heap_psram);
    aos_hal_log("neon", "ready | internal %u B, psram %u B", (unsigned)heap_int, (unsigned)heap_psram);
    return a;
}

static void app_destroy(aos_app_t *self, void *inst)
{
    (void)self;
    app_t *a = inst;
    if (!a) return;
    if (a->timer) lv_timer_delete(a->timer);
    a->closing = true;
    link_down(a);
    if (a->root) lv_obj_clean(a->root);
    if (a->have_normal) ns_art_free(&a->art_normal);
    if (a->have_combat) ns_art_free(&a->art_combat);
    free(a->fb);
    free(a->bg);
    free(a->menu);
    free(a->g);
    lv_free(a);
}

/* The launcher icon: an S of neon snake with its head and a cherry, on a
 * night-blue circle. Percent coordinates. */
static const uint8_t NEON_ICON[] = {
    AIC_HEADER,
    /* the body: two bowls that make an S, from the tail (lower left) up to
     * the head (upper right); arc angles clockwise from three o'clock */
    AIC_ARC(AIC_CENTER, 0, 16, 34, 0, 9, 0, 0, 270, 135, 0,
            AIC_C_BG, 0, AIC_C_LIT(0x19F5FF), 255),
    AIC_ARC(AIC_CENTER, 0, -16, 34, 0, 9, 0, 0, 90, 315, 0,
            AIC_C_BG, 0, AIC_C_LIT(0x19F5FF), 255),
    /* the head at the top end, with an eye and the tongue */
    AIC_RECT(AIC_CENTER, 13, -28, 17, 17, AIC_CIRCLE, AIC_C_LIT(0x19F5FF), 255),
    AIC_INTO,
    AIC_RECT(AIC_CENTER, 2, -2, 5, 5, AIC_CIRCLE, AIC_C_BG, 255),
    AIC_OUT,
    AIC_RECT(AIC_CENTER, 25, -28, 8, 2, 1, AIC_C_LIT(0xFF3060), 255),
    /* a cherry on the left, where the S leaves room */
    AIC_RECT(AIC_CENTER, -21, 4, 3, 13, 1, AIC_C_LIT(0x8CFF2E), 255),
    AIC_RECT(AIC_CENTER, -24, 12, 15, 15, AIC_CIRCLE, AIC_C_LIT(0xFF2FD0), 255),
    AIC_END
};

static bool neon_init(aos_app_t *app)
{
    app->desc.id       = "demo.neon";
    app->desc.name     = "Neon Snakes";
    app->desc.icon     = LV_SYMBOL_SHUFFLE;     /* the fallback, if ever refused */
    app->desc.icon_vec = AOS_ICON_NONE;
    aos_icon_set_ops(app, NEON_ICON, sizeof NEON_ICON);
    app->desc.color_a  = 0x1A1040;
    app->desc.color_b  = 0x05030F;
    app->desc.order    = 157;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    app->create  = app_create;
    app->destroy = app_destroy;
    app->back    = app_back;
    app->button  = app_button;
    app->hide    = app_hide;
    return true;
}

AOS_APP_ENTRY(neon_init);
