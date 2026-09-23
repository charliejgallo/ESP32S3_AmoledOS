/*
 * AmoledOS - Doom: the pad, as Doom understands it.
 *
 * The app writes two things from LVGL's task: button presses into a ring,
 * and the stick's position. The engine reads both from its own task, here.
 *
 * The buttons are translated on this side because what FIRE means depends on
 * the engine's state, and only this side can read it without a lock: in the
 * game it fires, in a menu it is Enter, and on a "are you sure? (y/n)"
 * question it is 'y'. USE is its mirror (use / back / 'n').
 *
 * The stick is a mouse. Doom adds a mouse's motion to the tic being built
 * (G_BuildTiccmd: angleturn -= mousex * 8, forward += mousey), so posting
 * one ev_mouse per tic gives an analogue stick for free: a small push turns
 * slowly, the edge turns like the keyboard's fast turn. In the menus (and on
 * the title, intermission and finale screens) the stick becomes the arrow
 * keys, with auto-repeat, which is what Doom's menus read.
 */
#include "doomtype.h"
#include "doomkeys.h"
#include "d_event.h"
#include "doomstat.h"
#include "m_controls.h"
#include "doomgeneric.h"
#include "m_misc.h"

#include "../doom_port.h"
#include "dg_port.h"
#include "aos_hal.h"

#include <stdint.h>

/* ---- from the app ---- */

#define RING 32
static volatile uint8_t  s_ring_btn[RING];
static volatile uint8_t  s_ring_down[RING];
static volatile uint32_t s_head, s_tail;
static volatile int      s_stick_x, s_stick_y;

void dp_button(int btn, bool down)
{
    uint32_t h = s_head;
    if (h - s_tail >= RING) return;         /* full: the engine is stuck */
    s_ring_btn[h % RING] = (uint8_t)btn;
    s_ring_down[h % RING] = down;
    __atomic_store_n(&s_head, h + 1, __ATOMIC_RELEASE);
}

void dp_stick(int x, int y)
{
    s_stick_x = x;
    s_stick_y = y;
}

/* ---- on the engine's side ---- */

#define PENDING 16
static uint8_t  s_pend_key[PENDING];
static uint8_t  s_pend_down[PENDING];
static int      s_pend_n;
static uint8_t  s_sent[DP_BTN_N];           /* the key each held button sent */
static int      s_arrow;                    /* the arrow the stick holds, 0 none */
static uint32_t s_arrow_next;               /* when it repeats */

void dg_input_reset(void)
{
    s_head = s_tail = 0;
    s_stick_x = s_stick_y = 0;
    s_pend_n = 0;
    s_arrow = 0;
    for (int i = 0; i < DP_BTN_N; i++) s_sent[i] = 0;
}

static void pend(int key, bool down)
{
    if (s_pend_n < PENDING) {
        s_pend_key[s_pend_n] = (uint8_t)key;
        s_pend_down[s_pend_n] = down;
        s_pend_n++;
    }
}

static int translate(int btn)
{
    int ms = dg_menu_state();
    switch (btn) {
    case DP_BTN_FIRE:   return ms == 2 ? 'y' : ms == 1 ? KEY_ENTER : KEY_FIRE;
    case DP_BTN_USE:    return ms == 2 ? 'n' : ms == 1 ? KEY_BACKSPACE : KEY_USE;
    case DP_BTN_MENU:   return KEY_ESCAPE;
    case DP_BTN_MAP:    return KEY_TAB;
    case DP_BTN_WEAPON: return key_nextweapon ? key_nextweapon : ']';
    }
    return 0;
}

static void drain_ring(void)
{
    while (s_tail != __atomic_load_n(&s_head, __ATOMIC_ACQUIRE)) {
        uint32_t t = s_tail;
        int btn = s_ring_btn[t % RING];
        bool down = s_ring_down[t % RING];
        __atomic_store_n(&s_tail, t + 1, __ATOMIC_RELEASE);
        if (btn >= DP_BTN_N) continue;
        if (down) {
            if (s_sent[btn]) pend(s_sent[btn], false);
            s_sent[btn] = (uint8_t)translate(btn);
            pend(s_sent[btn], true);
        } else if (s_sent[btn]) {
            pend(s_sent[btn], false);
            s_sent[btn] = 0;
        }
    }
}

/* the stick as arrows, for everything that is not walking around a level */
static void stick_arrows(uint32_t now)
{
    int x = s_stick_x, y = s_stick_y;
    int want = 0;
    if (y < -55 && -y >= (x < 0 ? -x : x)) want = KEY_UPARROW;
    else if (y > 55 && y >= (x < 0 ? -x : x)) want = KEY_DOWNARROW;
    else if (x < -55) want = KEY_LEFTARROW;
    else if (x > 55) want = KEY_RIGHTARROW;

    if (want != s_arrow) {
        if (s_arrow) pend(s_arrow, false);
        s_arrow = want;
        if (want) {
            pend(want, true);
            s_arrow_next = now + 420;
        }
    } else if (want && (int32_t)(now - s_arrow_next) >= 0) {
        pend(want, true);                   /* Doom's menus step per keydown */
        s_arrow_next = now + 130;
    }
}

static int shape_turn(int v)
{
    int a = v < 0 ? -v : v;
    if (a <= 10) return 0;
    int t = (a - 10) * 100 / 90;            /* 0..100 */
    /* gentle at first, the keyboard's fast turn at the edge (160 * 8 = 1280) */
    int turn = (40 * t + 120 * t * t / 100) / 100;
    return v < 0 ? -turn : turn;
}

static int shape_move(int v)
{
    int a = v < 0 ? -v : v;
    if (a <= 14) return 0;
    int m = (a - 14) * 50 / 86;             /* up to 50, Doom's running speed */
    return v < 0 ? -m : m;
}

void dg_input_tic(void)
{
    dg_poll_stop();
    drain_ring();
    uint32_t now = DG_GetTicksMs();
    bool walking = gamestate == GS_LEVEL && dg_menu_state() == 0;
    if (!walking) {
        stick_arrows(now);
        return;
    }
    if (s_arrow) {
        pend(s_arrow, false);
        s_arrow = 0;
    }
    int x = s_stick_x, y = s_stick_y;
    if (x || y) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = 0;
        ev.data2 = shape_turn(x);
        ev.data3 = shape_move(-y);          /* up on the screen is forward */
        ev.data4 = 0;
        if (ev.data2 || ev.data3) D_PostEvent(&ev);
    }
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (!s_pend_n) return 0;
    *pressed = s_pend_down[0];
    *key = s_pend_key[0];
    for (int i = 1; i < s_pend_n; i++) {
        s_pend_key[i - 1] = s_pend_key[i];
        s_pend_down[i - 1] = s_pend_down[i];
    }
    s_pend_n--;
    return 1;
}

/* For the app's log line: where the engine is. Read from LVGL's task
 * without a lock, which is fine for a log. */
const char *dp_where(void)
{
    static char buf[32];
    const char *what = gamestate == GS_LEVEL ? (demoplayback ? "demo" : "level")
                     : gamestate == GS_INTERMISSION ? "intermission"
                     : gamestate == GS_FINALE ? "finale" : "title";
    if (gamestate == GS_LEVEL) {
        M_snprintf(buf, sizeof buf, "%s E%dM%d%s", what, gameepisode, gamemap,
                   dg_menu_state() ? " +menu" : "");
    } else {
        M_snprintf(buf, sizeof buf, "%s%s", what, dg_menu_state() ? " +menu" : "");
    }
    return buf;
}
