/*
 * AmoledOS - Doom: the screen. Replaces doomgeneric's i_video.c.
 *
 * Doom draws 320x200 in 8-bit colour into I_VideoBuffer. Here each finished
 * frame is scaled to 368x230 (x1.15, nearest) through a 256-entry palette of
 * RGB565 already in the panel's byte order, into one of three slots that the
 * app blits from LVGL's task. The worker never touches LVGL or the SPI; the
 * slots are the whole conversation:
 *
 *   FREE -> WRITING (engine) -> READY -> SHOWN (app, blitting) -> FREE
 *
 * The app keeps two slots SHOWN for a moment (the new one is queued to the
 * panel while the old one may still be leaving), so the engine sometimes
 * finds no slot and waits a millisecond. It never waits otherwise: a READY
 * frame the app has not taken yet is simply overwritten by a newer one.
 */
#pragma GCC optimize("O2")

#include "config.h"
#include "v_video.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "i_system.h"
#include "z_zone.h"
#include "doomgeneric.h"
#include "w_wad.h"
#include "deh_str.h"
#include "tables.h"

#include "../doom_port.h"
#include "dg_port.h"
#include "dg_raw.h"
#include "aos_hal.h"

#include <stdint.h>
#include <string.h>

/* what the engine expects from i_video.c */
byte   *I_VideoBuffer = NULL;
boolean screensaver_mode = false;
boolean screenvisible;
float   mouse_acceleration = 2.0;
int     mouse_threshold = 10;
int     usegamma = 0;
int     usemouse = 1;

void I_GetEvent(void);

enum { SLOT_FREE = 0, SLOT_WRITING, SLOT_READY, SLOT_SHOWN };

static uint16_t        *s_slot[DP_SLOTS];
static volatile int     s_state[DP_SLOTS];
static volatile uint32_t s_seq[DP_SLOTS];
static uint32_t         s_frame_seq;
static int              s_prev_shown = -1;     /* app side */
static bool             s_swap;

static uint16_t        *s_pal;                 /* 256 entries, internal RAM */
static uint16_t         s_xmap[DP_W];
static uint8_t          s_ymap[DP_H];
static uint32_t         s_last_yield;

static bool cas(volatile int *p, int from, int to)
{
    int expected = from;
    return __atomic_compare_exchange_n(p, &expected, to, false,
                                       __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

bool dg_video_alloc(bool swap_bytes)
{
    s_swap = swap_bytes;
    for (int i = 0; i < DP_SLOTS; i++) {
        s_slot[i] = dg_raw_alloc((size_t)DP_W * DP_H * 2, false);
        if (!s_slot[i]) {
            dg_video_release();
            return false;
        }
        memset(s_slot[i], 0, (size_t)DP_W * DP_H * 2);
        s_state[i] = SLOT_FREE;
        s_seq[i] = 0;
    }
    s_pal = dg_raw_alloc(256 * sizeof(uint16_t), true);
    if (!s_pal) {
        dg_video_release();
        return false;
    }
    memset(s_pal, 0, 256 * sizeof(uint16_t));
    for (int x = 0; x < DP_W; x++) s_xmap[x] = (uint16_t)(x * SCREENWIDTH / DP_W);
    for (int y = 0; y < DP_H; y++) s_ymap[y] = (uint8_t)(y * SCREENHEIGHT / DP_H);
    s_frame_seq = 0;
    s_prev_shown = -1;
    return true;
}

void dg_video_release(void)
{
    for (int i = 0; i < DP_SLOTS; i++) {
        dg_raw_free(s_slot[i]);
        s_slot[i] = NULL;
        s_state[i] = SLOT_FREE;
    }
    dg_raw_free(s_pal);
    s_pal = NULL;
}

/* ---- the app's side ---- */

const uint16_t *dp_frame_take(void)
{
    int best = -1;
    uint32_t bs = 0;
    for (int i = 0; i < DP_SLOTS; i++) {
        if (s_state[i] == SLOT_READY && (best < 0 || s_seq[i] > bs)) {
            best = i;
            bs = s_seq[i];
        }
    }
    if (best < 0 || !cas(&s_state[best], SLOT_READY, SLOT_SHOWN)) return NULL;
    s_prev_shown = best;
    return s_slot[best];
}

void dp_frame_blitted(void)
{
    /* the frame before stays SHOWN until the new one is queued: until then
     * its own transfer may still be reading it (Turbo's order, push_frame) */
    for (int i = 0; i < DP_SLOTS; i++) {
        if (i != s_prev_shown && s_state[i] == SLOT_SHOWN) {
            __atomic_store_n(&s_state[i], SLOT_FREE, __ATOMIC_RELEASE);
        }
    }
}

uint32_t dp_frames(void)
{
    return s_frame_seq;
}

/* ---- the engine's side ---- */

static int grab_slot(void)
{
    for (;;) {
        for (int i = 0; i < DP_SLOTS; i++) {
            if (cas(&s_state[i], SLOT_FREE, SLOT_WRITING)) return i;
        }
        /* no free slot: a frame nobody took yet is replaced by this one */
        for (int i = 0; i < DP_SLOTS; i++) {
            if (cas(&s_state[i], SLOT_READY, SLOT_WRITING)) return i;
        }
        dg_poll_stop();
        aos_hal_worker_sleep(1);
    }
}

void I_InitGraphics(void)
{
    I_VideoBuffer = (byte *)Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    screenvisible = true;
}

void I_ShutdownGraphics(void)
{
    Z_Free(I_VideoBuffer);
}

void I_StartFrame(void)
{
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
    dg_poll_stop();
    int i = grab_slot();
    uint16_t *out = s_slot[i];
    const uint16_t *pal = s_pal;
    const byte *prev_src = NULL;
    uint16_t *prev_out = NULL;

    for (int y = 0; y < DP_H; y++, out += DP_W) {
        const byte *src = I_VideoBuffer + s_ymap[y] * SCREENWIDTH;
        if (src == prev_src) {
            memcpy(out, prev_out, DP_W * 2);
            continue;
        }
        /* two screen pixels per store: rows are 736 bytes, so aligned */
        const uint16_t *xm = s_xmap;
        for (int x = 0; x < DP_W; x += 2) {
            uint32_t a = pal[src[xm[x]]];
            uint32_t b = pal[src[xm[x + 1]]];
            *(uint32_t *)(out + x) = a | (b << 16);
        }
        prev_src = src;
        prev_out = out;
    }

    s_seq[i] = ++s_frame_seq;
    __atomic_store_n(&s_state[i], SLOT_READY, __ATOMIC_RELEASE);

    /* core 0 also runs its idle task, which the task watchdog watches: a
     * render loop that never blocks starves it. Turbo measured the cadence
     * that costs nothing: one short sleep a second. */
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (now - s_last_yield > 1000) {
        s_last_yield = now;
        aos_hal_worker_sleep(1);
    }
}

void I_ReadScreen(byte *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

void I_SetPalette(byte *palette)
{
    if (!s_pal) return;
    for (int i = 0; i < 256; i++) {
        int r = gammatable[usegamma][*palette++];
        int g = gammatable[usegamma][*palette++];
        int b = gammatable[usegamma][*palette++];
        uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        s_pal[i] = s_swap ? (uint16_t)((c >> 8) | (c << 8)) : c;
    }
}

int I_GetPaletteIndex(int r, int g, int b)
{
    /* only used by the automap's colours in some IWADs; the palette's own
     * bytes are the truth, so read them back from PLAYPAL */
    byte *pal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);
    int best = 0, best_diff = 0x7FFFFFFF;
    for (int i = 0; i < 256; i++) {
        int dr = r - pal[i * 3], dg = g - pal[i * 3 + 1], db = b - pal[i * 3 + 2];
        int d = dr * dr + dg * dg + db * db;
        if (d < best_diff) {
            best = i;
            best_diff = d;
            if (!d) break;
        }
    }
    return best;
}

void I_BeginRead(void) {}
void I_EndRead(void) {}
void I_SetWindowTitle(char *title) { (void)title; }
void I_GraphicsCheckCommandLine(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_EnableLoadingDisk(void) {}
void I_BindVideoVariables(void) {}
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_CheckIsScreensaver(void) {}
void I_InitWindowTitle(void) {}
void I_InitWindowIcon(void) {}

void I_StartTic(void)
{
    I_GetEvent();
}
