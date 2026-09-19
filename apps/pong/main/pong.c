/*
 * AmoledOS - Pong across two watches (docs/LINK.md, phase 4).
 *
 * The field is two screens tall. Each player holds a watch with the paddle
 * at the bottom of the screen; the ball leaves the top of one screen and
 * comes down the other one's. The watch with the lower MAC is the HOST: it
 * simulates the game at 30 Hz from its own paddle and the guest's, and
 * sends the state over the fast channel (send and forget, the last one
 * wins). The guest sends its paddle at 30 Hz and draws the last state it
 * got. Nothing here waits for anything: a lost frame is a skipped frame.
 *
 * World coordinates: x 0..367, y 0..895, the two screens glued top to top.
 * The host's screen is world y 448..895 (screen y = world y - 448, its
 * paddle at world 878, the bottom of its screen). The guest's screen is
 * world y 0..447 mirrored both ways (screen x = 367 - x, screen y = 447 -
 * y): its paddle at world 17 sits at the bottom of its own screen, and the
 * ball that leaves the host's top edge (world 448) appears at the guest's
 * top edge (world 447) going down towards it.
 *
 * Touch: the paddle follows the finger's x (LV_EVENT_PRESSING on a layer
 * that covers the screen). NO_SWIPE and LONG_DRAG, so a drag is a drag and
 * not a back gesture; the corner label at the top leaves.
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

#define PG_W            AOS_SCREEN_W
#define PG_H            AOS_SCREEN_H
#define PG_FIELD_H      (2 * PG_H)
#define PG_TICK_MS      33
#define PG_PADDLE_W     72
#define PG_PADDLE_H     10
#define PG_BALL         14
#define PG_HOST_PADDLE_Y   (PG_H + PG_H - 18)   /* world y of the host's paddle centre: 878 */
#define PG_GUEST_PADDLE_Y  (17)                  /* the guest's: the far end */
#define PG_SPEED0       6                        /* px per tick */
#define PG_SPEED_MAX    14
#define PG_WIN          7
#define PG_PROTO        1

/* Two paddles with the ball in between, a bit of net. */
static const uint8_t PONG_ICON[] = {
    AIC_HEADER,
    AIC_RECT(AIC_TOP_MID,    0, 12, 36, 7, 3, AIC_C_TEXT, 255),
    AIC_RECT(AIC_BOTTOM_MID, 0, 12, 36, 7, 3, AIC_C_TEXT, 255),
    AIC_RECT(AIC_CENTER,   -10, -4, 14, 14, AIC_CIRCLE, AIC_C_ACCENT, 255),
    AIC_RECT(AIC_CENTER,   -30, 0, 8, 3, 1, AIC_C_DIM, 255),
    AIC_RECT(AIC_CENTER,   -15, 0, 8, 3, 1, AIC_C_DIM, 255),
    AIC_RECT(AIC_CENTER,     0, 0, 8, 3, 1, AIC_C_DIM, 255),
    AIC_RECT(AIC_CENTER,    15, 0, 8, 3, 1, AIC_C_DIM, 255),
    AIC_RECT(AIC_CENTER,    30, 0, 8, 3, 1, AIC_C_DIM, 255),
    AIC_END
};

/* --- what goes over the air --------------------------------------------- */

enum { MSG_HELLO = 1, MSG_PADDLE = 2, MSG_STATE = 3 };

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    int16_t  ball_x, ball_y;
    int16_t  host_paddle, guest_paddle;
    uint8_t  host_score, guest_score;
    uint8_t  phase;                     /* 0 serving, 1 playing, 2 host won, 3 guest won */
    uint16_t seq;
} pg_state_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    int16_t  paddle;
    uint16_t seq;
} pg_paddle_t;

typedef struct __attribute__((packed)) {
    uint8_t  type, proto;
    uint8_t  mac[6];
} pg_hello_t;

/* --- the game ------------------------------------------------------------ */

typedef struct {
    lv_obj_t   *ball;
    lv_obj_t   *mine;           /* my paddle */
    lv_obj_t   *theirs;         /* the other one, drawn at the top */
    lv_obj_t   *score;
    lv_obj_t   *msg;
    lv_obj_t   *stats;
    lv_obj_t   *exit;
    lv_obj_t   *touch;          /* the layer that hears the finger */
    lv_timer_t *timer;

    bool        up;             /* the link came up */
    bool        have_partner;
    bool        is_host, role_known;
    uint8_t     partner_mac[6];
    int16_t     my_paddle;      /* screen x of my paddle centre */

    /* host: the simulation */
    int16_t     ball_x, ball_y, vx, vy;
    int16_t     guest_paddle;   /* world x, from the guest */
    uint8_t     host_score, guest_score, phase;
    uint32_t    serve_at_ms;
    uint16_t    seq;

    /* guest: the last state */
    pg_state_t  state;
    uint32_t    state_at_ms;

    /* stats */
    uint32_t    frames_in, frames_out, stats_t0, log_every;
    uint32_t    shown_in, shown_out;
    bool        leave;
} pong_t;

static pong_t s_pg;

/* rand() is not in the firmware's symbol table: a small LCG is all a serve needs */
static uint32_t s_lcg;
static int rnd(void)
{
    s_lcg = s_lcg * 1103515245u + 12345u;
    return (int)((s_lcg >> 16) & 0x7FFF);
}

static void world_to_screen(const pong_t *g, int16_t wx, int16_t wy, int16_t *sx, int16_t *sy)
{
    if (g->is_host) {
        *sx = wx;
        *sy = (int16_t)(wy - PG_H);
    } else {
        *sx = (int16_t)(PG_W - 1 - wx);
        *sy = (int16_t)(PG_H - 1 - wy);
    }
}

/* my paddle's centre in world x */
static int16_t my_paddle_world(const pong_t *g)
{
    return g->is_host ? g->my_paddle : (int16_t)(PG_W - 1 - g->my_paddle);
}

static void serve(pong_t *g, bool towards_guest)
{
    g->ball_x = PG_W / 2;
    g->ball_y = PG_FIELD_H / 2;
    int16_t dir = (rnd() & 1) ? 1 : -1;
    g->vx = (int16_t)(dir * (2 + rnd() % 4));
    g->vy = (int16_t)(towards_guest ? -PG_SPEED0 : PG_SPEED0);   /* the guest is at low y */
    g->phase = 0;
    g->serve_at_ms = aos_hal_uptime_ms() + 1000;
}

static void host_step(pong_t *g)
{
    if (g->phase >= 2) {
        return;
    }
    if (g->phase == 0) {
        if (aos_hal_uptime_ms() < g->serve_at_ms) {
            return;
        }
        g->phase = 1;
    }
    g->ball_x = (int16_t)(g->ball_x + g->vx);
    g->ball_y = (int16_t)(g->ball_y + g->vy);
    if (g->ball_x < PG_BALL / 2) {
        g->ball_x = PG_BALL / 2;
        g->vx = (int16_t)-g->vx;
    } else if (g->ball_x > PG_W - 1 - PG_BALL / 2) {
        g->ball_x = (int16_t)(PG_W - 1 - PG_BALL / 2);
        g->vx = (int16_t)-g->vx;
    }
    /* the host's paddle, at the bottom of world */
    int16_t hp = my_paddle_world(g);
    if (g->vy > 0 && g->ball_y + PG_BALL / 2 >= PG_HOST_PADDLE_Y - PG_PADDLE_H / 2 &&
        g->ball_y - PG_BALL / 2 <= PG_HOST_PADDLE_Y + PG_PADDLE_H / 2 &&
        abs(g->ball_x - hp) <= PG_PADDLE_W / 2 + PG_BALL / 2) {
        g->vy = (int16_t)-g->vy;
        g->ball_y = (int16_t)(PG_HOST_PADDLE_Y - PG_PADDLE_H / 2 - PG_BALL / 2);
        g->vx = (int16_t)(g->vx + (g->ball_x - hp) / 8);
        if (abs(g->vy) < PG_SPEED_MAX) g->vy = (int16_t)(g->vy - 1);
    }
    /* the guest's paddle, at the top of world */
    if (g->vy < 0 && g->ball_y - PG_BALL / 2 <= PG_GUEST_PADDLE_Y + PG_PADDLE_H / 2 &&
        g->ball_y + PG_BALL / 2 >= PG_GUEST_PADDLE_Y - PG_PADDLE_H / 2 &&
        abs(g->ball_x - g->guest_paddle) <= PG_PADDLE_W / 2 + PG_BALL / 2) {
        g->vy = (int16_t)-g->vy;
        g->ball_y = (int16_t)(PG_GUEST_PADDLE_Y + PG_PADDLE_H / 2 + PG_BALL / 2);
        g->vx = (int16_t)(g->vx + (g->ball_x - g->guest_paddle) / 8);
        if (abs(g->vy) < PG_SPEED_MAX) g->vy = (int16_t)(g->vy + 1);
    }
    if (g->vx > PG_SPEED_MAX) g->vx = PG_SPEED_MAX;
    if (g->vx < -PG_SPEED_MAX) g->vx = -PG_SPEED_MAX;
    /* the ball got past someone */
    if (g->ball_y > PG_FIELD_H - 1 + PG_BALL) {
        g->guest_score++;
        aos_hal_beep(300, 80);
        if (g->guest_score >= PG_WIN) g->phase = 3; else serve(g, false);
    } else if (g->ball_y < -PG_BALL) {
        g->host_score++;
        aos_hal_beep(300, 80);
        if (g->host_score >= PG_WIN) g->phase = 2; else serve(g, true);
    }
}

static void send_state(pong_t *g)
{
    pg_state_t st = {
        .type = MSG_STATE, .proto = PG_PROTO,
        .ball_x = g->ball_x, .ball_y = g->ball_y,
        .host_paddle = my_paddle_world(g), .guest_paddle = g->guest_paddle,
        .host_score = g->host_score, .guest_score = g->guest_score,
        .phase = g->phase, .seq = ++g->seq,
    };
    if (aos_hal_link_send_partner(&st, sizeof st)) {
        g->frames_out++;
    }
}

static void send_paddle(pong_t *g)
{
    pg_paddle_t p = { .type = MSG_PADDLE, .proto = PG_PROTO,
                      .paddle = my_paddle_world(g), .seq = ++g->seq };
    if (aos_hal_link_send_partner(&p, sizeof p)) {
        g->frames_out++;
    }
}

static void send_hello(pong_t *g)
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    pg_hello_t h = { .type = MSG_HELLO, .proto = PG_PROTO };
    memcpy(h.mac, st.own_mac, 6);
    aos_hal_link_send_partner(&h, sizeof h);
    (void)g;
}

static void decide_role(pong_t *g, const uint8_t their_mac[6])
{
    aos_link_stats_t st;
    aos_hal_link_stats(&st);
    g->is_host = memcmp(st.own_mac, their_mac, 6) < 0;
    g->role_known = true;
    if (g->is_host) {
        g->host_score = g->guest_score = 0;
        g->guest_paddle = PG_W / 2;
        serve(g, true);
    }
    aos_hal_log("pong", "role: %s", g->is_host ? "host" : "guest");
}

static void drain(pong_t *g)
{
    aos_link_frame_t f;
    while (aos_hal_link_recv(&f) > 0) {
        if (f.len < 2 || f.data[1] != PG_PROTO) {
            continue;
        }
        g->frames_in++;
        switch (f.data[0]) {
        case MSG_HELLO:
            if (f.len >= sizeof(pg_hello_t)) {
                const pg_hello_t *h = (const pg_hello_t *)f.data;
                memcpy(g->partner_mac, h->mac, 6);
                if (!g->role_known) {
                    decide_role(g, h->mac);
                    send_hello(g);      /* answer, so the other decides too */
                }
            }
            break;
        case MSG_PADDLE:
            if (g->is_host && f.len >= sizeof(pg_paddle_t)) {
                const pg_paddle_t *p = (const pg_paddle_t *)f.data;
                g->guest_paddle = p->paddle;
            }
            break;
        case MSG_STATE:
            if (!g->is_host && f.len >= sizeof(pg_state_t)) {
                memcpy(&g->state, f.data, sizeof g->state);
                g->state_at_ms = aos_hal_uptime_ms();
                if (!g->role_known) {
                    g->is_host = false;
                    g->role_known = true;
                }
            }
            break;
        default:
            break;
        }
    }
}

static void draw(pong_t *g)
{
    int16_t bx, by, hp, gp;
    uint8_t hs, gs, phase;
    if (g->is_host) {
        bx = g->ball_x; by = g->ball_y; hp = my_paddle_world(g); gp = g->guest_paddle;
        hs = g->host_score; gs = g->guest_score; phase = g->phase;
    } else {
        bx = g->state.ball_x; by = g->state.ball_y; hp = g->state.host_paddle; gp = g->state.guest_paddle;
        hs = g->state.host_score; gs = g->state.guest_score; phase = g->state.phase;
    }
    int16_t sx, sy;
    world_to_screen(g, bx, by, &sx, &sy);
    /* the ball is drawn where it is, even off this screen (then it is hidden) */
    if (sy < -PG_BALL || sy > PG_H + PG_BALL) {
        lv_obj_add_flag(g->ball, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(g->ball, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g->ball, sx - PG_BALL / 2, sy - PG_BALL / 2);
    }
    /* my paddle from my finger; theirs from the state, drawn at the top as a
     * thin bar that shows where they are */
    lv_obj_set_x(g->mine, g->my_paddle - PG_PADDLE_W / 2);
    int16_t theirs_world = g->is_host ? gp : hp;
    int16_t theirs_screen = g->is_host ? theirs_world : (int16_t)(PG_W - 1 - theirs_world);
    lv_obj_set_x(g->theirs, theirs_screen - PG_PADDLE_W / 2);

    char buf[48];
    uint8_t me = g->is_host ? hs : gs, them = g->is_host ? gs : hs;
    snprintf(buf, sizeof buf, "%u  :  %u", me, them);
    lv_label_set_text(g->score, buf);

    const char *m = "";
    if (!g->up)                  m = _("El enlace no arrancó");
    else if (!g->have_partner)   m = _("Sin pareja: apareá los relojes en Enlace");
    else if (!g->role_known)     m = _("Esperando al otro reloj...");
    else if (phase == 2)         m = g->is_host ? _("Ganaste") : _("Perdiste");
    else if (phase == 3)         m = g->is_host ? _("Perdiste") : _("Ganaste");
    else if (!g->is_host && aos_hal_uptime_ms() - g->state_at_ms > 1500) m = _("Sin señal del otro reloj");
    lv_label_set_text(g->msg, m);
}

static void frame(lv_timer_t *timer)
{
    pong_t *g = lv_timer_get_user_data(timer);
    if (g->leave) {
        g->leave = false;
        aos_ui_back();
        return;
    }
    if (g->up) {
        if (!g->have_partner) {
            aos_link_partner_t p;
            g->have_partner = aos_hal_link_partner(&p) && p.valid;
            if (g->have_partner) {
                memcpy(g->partner_mac, p.mac, 6);
            }
        }
        drain(g);
        if (g->have_partner && !g->role_known) {
            static uint32_t last_hello;
            uint32_t now = aos_hal_uptime_ms();
            if (now - last_hello > 500) {
                last_hello = now;
                send_hello(g);
            }
        } else if (g->role_known) {
            if (g->is_host) {
                host_step(g);
                send_state(g);
            } else {
                send_paddle(g);
            }
        }
    }
    draw(g);

    uint32_t now = aos_hal_uptime_ms();
    if (now - g->stats_t0 >= 1000) {
        char buf[64];
        snprintf(buf, sizeof buf, "%s  in %u/s  out %u/s", g->role_known ? (g->is_host ? "host" : "guest") : "-",
                 (unsigned)g->frames_in, (unsigned)g->frames_out);
        lv_label_set_text(g->stats, buf);
        if (++g->log_every % 3 == 0) {
            aos_hal_log("pong", "%s", buf);
        }
        g->frames_in = g->frames_out = 0;
        g->stats_t0 = now;
    }
}

/* --- input ---------------------------------------------------------------- */

static void touch_cb(lv_event_t *e)
{
    pong_t *g = lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int16_t x = (int16_t)p.x;
    if (x < PG_PADDLE_W / 2) x = PG_PADDLE_W / 2;
    if (x > PG_W - 1 - PG_PADDLE_W / 2) x = (int16_t)(PG_W - 1 - PG_PADDLE_W / 2);
    g->my_paddle = x;
    /* a tap after the game ended starts another one (the host decides) */
    if (g->is_host && g->phase >= 2 && lv_event_get_code(e) == LV_EVENT_CLICKED) {
        g->host_score = g->guest_score = 0;
        serve(g, true);
    }
}

static void exit_cb(lv_event_t *e)
{
    pong_t *g = lv_event_get_user_data(e);
    g->leave = true;                    /* on the next tick, not in the event */
}

static bool back(aos_app_t *self, void *inst)
{
    (void)self; (void)inst;
    return false;                       /* the button leaves; the swipe is off */
}

/* --- life cycle ----------------------------------------------------------- */

static void *create(aos_app_t *self, lv_obj_t *root)
{
    (void)self;
    pong_t *g = &s_pg;
    memset(g, 0, sizeof *g);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    /* the net: a dotted line along the top edge, where the other screen is */
    for (int x = 8; x < PG_W; x += 24) {
        lv_obj_t *dot = lv_obj_create(root);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 12, 3);
        lv_obj_set_style_bg_color(dot, AOS_C_CARD2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_pos(dot, x, 22);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }

    g->theirs = lv_obj_create(root);
    lv_obj_remove_style_all(g->theirs);
    lv_obj_set_size(g->theirs, PG_PADDLE_W, 4);
    lv_obj_set_style_bg_color(g->theirs, AOS_C_DIM, 0);
    lv_obj_set_style_bg_opa(g->theirs, LV_OPA_COVER, 0);
    lv_obj_set_pos(g->theirs, PG_W / 2 - PG_PADDLE_W / 2, 26);
    lv_obj_remove_flag(g->theirs, LV_OBJ_FLAG_CLICKABLE);

    g->mine = lv_obj_create(root);
    lv_obj_remove_style_all(g->mine);
    lv_obj_set_size(g->mine, PG_PADDLE_W, PG_PADDLE_H);
    lv_obj_set_style_bg_color(g->mine, AOS_C_TEXT, 0);
    lv_obj_set_style_bg_opa(g->mine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g->mine, 4, 0);
    lv_obj_set_pos(g->mine, PG_W / 2 - PG_PADDLE_W / 2, PG_H - 18 - PG_PADDLE_H / 2);
    lv_obj_remove_flag(g->mine, LV_OBJ_FLAG_CLICKABLE);
    g->my_paddle = PG_W / 2;

    g->ball = lv_obj_create(root);
    lv_obj_remove_style_all(g->ball);
    lv_obj_set_size(g->ball, PG_BALL, PG_BALL);
    lv_obj_set_style_radius(g->ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g->ball, AOS_C_ACCENT, 0);
    lv_obj_set_style_bg_opa(g->ball, LV_OPA_COVER, 0);
    lv_obj_add_flag(g->ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g->ball, LV_OBJ_FLAG_CLICKABLE);

    g->score = aos_label(root, "0  :  0", aos_font_title, AOS_C_DIM);
    lv_obj_align(g->score, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_remove_flag(g->score, LV_OBJ_FLAG_CLICKABLE);

    g->msg = aos_label(root, "", aos_font_body, AOS_C_TEXT);
    lv_obj_set_width(g->msg, PG_W - 40);
    lv_label_set_long_mode(g->msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g->msg, LV_ALIGN_CENTER, 0, -20);
    lv_obj_remove_flag(g->msg, LV_OBJ_FLAG_CLICKABLE);

    g->stats = aos_label(root, "", aos_font_small, AOS_C_DIM);
    lv_obj_align(g->stats, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_remove_flag(g->stats, LV_OBJ_FLAG_CLICKABLE);

    /* the finger: a transparent layer over everything but the exit label */
    g->touch = lv_obj_create(root);
    lv_obj_remove_style_all(g->touch);
    lv_obj_set_size(g->touch, PG_W, PG_H);
    lv_obj_set_pos(g->touch, 0, 0);
    lv_obj_add_flag(g->touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g->touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g->touch, touch_cb, LV_EVENT_PRESSING, g);
    lv_obj_add_event_cb(g->touch, touch_cb, LV_EVENT_CLICKED, g);

    g->exit = aos_label(root, LV_SYMBOL_CLOSE, aos_font_body, AOS_C_DIM);
    lv_obj_set_size(g->exit, 60, 44);
    lv_obj_set_style_text_align(g->exit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(g->exit, 12, 0);
    lv_obj_align(g->exit, LV_ALIGN_TOP_LEFT, 4, 24);
    lv_obj_add_flag(g->exit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g->exit, exit_cb, LV_EVENT_CLICKED, g);

    g->up = aos_hal_link_start();
    if (g->up) {
        aos_hal_link_offer("pong");
        aos_link_partner_t p;
        g->have_partner = aos_hal_link_partner(&p) && p.valid;
        if (g->have_partner) {
            memcpy(g->partner_mac, p.mac, 6);
        }
    }
    g->stats_t0 = aos_hal_uptime_ms();
    s_lcg = (uint32_t)g->stats_t0 ^ 0x9E3779B9u;
    g->timer = lv_timer_create(frame, PG_TICK_MS, g);
    draw(g);
    return g;
}

static void destroy(aos_app_t *self, void *inst)
{
    (void)self;
    pong_t *g = inst;
    if (g->timer) {
        lv_timer_delete(g->timer);
        g->timer = NULL;
    }
    if (g->exit) {
        lv_obj_delete(g->exit);
        g->exit = NULL;
    }
    if (g->touch) {
        lv_obj_delete(g->touch);
        g->touch = NULL;
    }
    if (g->up) {
        aos_hal_link_offer("");
        aos_hal_link_stop();
    }
}

static bool pong_init(aos_app_t *app)
{
    app->desc.id       = "aos.pong";
    app->desc.name     = "Pong";
    app->desc.icon     = LV_SYMBOL_PLAY;
    app->desc.icon_vec = AOS_ICON_NONE;
    app->desc.color_a  = 0x30D158;
    app->desc.color_b  = 0x0F5A26;
    app->desc.order    = 127;
    app->desc.flags    = AOS_APP_FLAG_KEEP_AWAKE | AOS_APP_FLAG_FULLSCREEN |
                         AOS_APP_FLAG_NO_SWIPE | AOS_APP_FLAG_LONG_DRAG;
    aos_icon_set_ops(app, PONG_ICON, sizeof PONG_ICON);
    app->create  = create;
    app->destroy = destroy;
    app->back    = back;
    return true;
}

AOS_APP_ENTRY(pong_init);
