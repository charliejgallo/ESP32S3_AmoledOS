/*
 * AmoledOS - Doom: the Sound Blaster's OPL2, in software, for the music.
 *
 * Doom's music is MUS, played by DMX on the OPL2 FM chip with the
 * instruments in the WAD's GENMIDI lump. Chocolate Doom's player
 * (doomgeneric/i_oplmusic.c) does exactly that through the small API of
 * opl.h: write a register, and "call me back in N microseconds" to step
 * through the song. Its SDL driver ran the emulator in SDL's audio thread and
 * fired those callbacks as the samples went by; OPL_Delay() even blocked
 * the game until the audio thread signalled it.
 *
 * Here there is one thread. The effects mixer (port/dg_sound.c) calls
 * dg_opl_mix() from the engine's task once a frame for the samples it is
 * about to queue, and this file generates them with DOSBox's DBOPL
 * (doomgeneric/dbopl.c), stopping at each callback's time to run it. The
 * song's clock is therefore the sample count, which is what the speaker
 * plays: the music keeps its tempo whatever the frame rate does. Nothing
 * here needs a lock, and OPL_Delay has nobody to wait for (the only caller
 * is the chip detection, which a software chip does not need).
 */
#pragma GCC optimize("O2")

#include "doomtype.h"
#include "opl.h"
#include "opl_queue.h"
#include "dbopl.h"

#include "dg_port.h"

#include <stdint.h>
#include <string.h>

#define RATE    16000           /* the speaker's, opened by the app */
#define CHUNK   256

static Chip                   s_chip;
static opl_callback_queue_t  *s_queue;
static uint64_t               s_samples;    /* generated since OPL_Init */
static uint64_t               s_now;        /* the same, in microseconds */
static uint64_t               s_pause_offset;
static int                    s_paused;
static int                    s_reg;
static bool                   s_on;
static Bit32s                 s_buf[CHUNK];
static int32_t                s_dc_x, s_dc_y;   /* the DC blocker's memory */

opl_init_result_t OPL_Init(unsigned int port_base)
{
    (void)port_base;
    s_queue = OPL_Queue_Create();
    if (!s_queue) return OPL_INIT_NONE;
    s_samples = s_now = s_pause_offset = 0;
    s_dc_x = s_dc_y = 0;
    s_paused = 0;
    DBOPL_InitTables();
    Chip__Chip(&s_chip);
    Chip__Setup(&s_chip, RATE);
    s_on = true;
    return OPL_INIT_OPL2;       /* DMX's OPL3 mode needs -opl3; Doom is OPL2 */
}

void OPL_Shutdown(void)
{
    s_on = false;
    if (s_queue) OPL_Queue_Destroy(s_queue);
    s_queue = NULL;
}

void OPL_SetSampleRate(unsigned int rate)
{
    (void)rate;                 /* always the speaker's */
}

static void write_reg(unsigned int reg, unsigned int value)
{
    switch (reg) {
    case OPL_REG_TIMER1:
    case OPL_REG_TIMER2:
    case OPL_REG_TIMER_CTRL:
        break;                  /* the timers are only for detecting a chip */
    default:
        Chip__WriteReg(&s_chip, reg, (Bit8u)value);
        break;
    }
}

void OPL_WritePort(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT) s_reg = (int)value;
    else if (port == OPL_REGISTER_PORT_OPL3) s_reg = (int)(value | 0x100);
    else if (port == OPL_DATA_PORT) write_reg((unsigned)s_reg, value);
}

unsigned int OPL_ReadPort(opl_port_t port)
{
    return port == OPL_REGISTER_PORT_OPL3 ? 0xff : 0;
}

unsigned int OPL_ReadStatus(void)
{
    return 0;
}

void OPL_WriteRegister(int reg, int value)
{
    if (s_on) write_reg((unsigned)reg, (unsigned)value);
}

opl_init_result_t OPL_Detect(void)
{
    return OPL_INIT_OPL2;
}

/* Chocolate Doom's own, which is DMX's: "registers that actually don't
 * exist, but this is what Doom does" */
void OPL_InitRegisters(int opl3)
{
    int r;
    for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r)
        OPL_WriteRegister(r, 0x3f);
    for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r)
        OPL_WriteRegister(r, 0x00);
    for (r = 1; r < OPL_REGS_LEVEL; ++r)
        OPL_WriteRegister(r, 0x00);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x60);
    OPL_WriteRegister(OPL_REG_TIMER_CTRL, 0x80);
    OPL_WriteRegister(OPL_REG_WAVEFORM_ENABLE, 0x20);
    if (opl3) {
        OPL_WriteRegister(OPL_REG_NEW, 0x01);
        for (r = OPL_REGS_LEVEL; r <= OPL_REGS_LEVEL + OPL_NUM_OPERATORS; ++r)
            OPL_WriteRegister(r | 0x100, 0x3f);
        for (r = OPL_REGS_ATTACK; r <= OPL_REGS_WAVEFORM + OPL_NUM_OPERATORS; ++r)
            OPL_WriteRegister(r | 0x100, 0x00);
        for (r = 1; r < OPL_REGS_LEVEL; ++r)
            OPL_WriteRegister(r | 0x100, 0x00);
    }
    OPL_WriteRegister(OPL_REG_FM_MODE, 0x40);
    if (opl3) OPL_WriteRegister(OPL_REG_NEW, 0x01);
}

void OPL_SetCallback(uint64_t us, opl_callback_t callback, void *data)
{
    if (s_queue) OPL_Queue_Push(s_queue, callback, data, s_now - s_pause_offset + us);
}

void OPL_AdjustCallbacks(float factor)
{
    if (s_queue) OPL_Queue_AdjustCallbacks(s_queue, s_now, factor);
}

void OPL_ClearCallbacks(void)
{
    if (s_queue) OPL_Queue_Clear(s_queue);
}

void OPL_Lock(void) {}
void OPL_Unlock(void) {}

void OPL_Delay(uint64_t us)
{
    (void)us;
}

void OPL_SetPaused(int paused)
{
    s_paused = paused;
}

/* 16 kHz: a sample is 62.5 us, so the clock is exact in half-microseconds */
static void advance(unsigned int n)
{
    uint64_t before = s_now;
    s_samples += n;
    s_now = s_samples * 125 / 2;
    if (s_paused) s_pause_offset += s_now - before;
    opl_callback_t cb;
    void *data;
    while (!OPL_Queue_IsEmpty(s_queue) &&
           s_now >= OPL_Queue_Peek(s_queue) + s_pause_offset) {
        if (!OPL_Queue_Pop(s_queue, &cb, &data)) break;
        cb(data);
    }
}

void dg_opl_mix(int32_t *acc, int n)
{
    if (!s_on || !s_queue) return;
    int filled = 0;
    while (filled < n) {
        unsigned int todo = (unsigned)(n - filled);
        if (todo > CHUNK) todo = CHUNK;
        if (!s_paused && !OPL_Queue_IsEmpty(s_queue)) {
            /* stop exactly where the next event is due */
            uint64_t due = OPL_Queue_Peek(s_queue) + s_pause_offset;
            uint64_t k = due > s_now ? ((due - s_now) * 2 + 124) / 125 : 0;
            if (k == 0) {
                advance(0);
                continue;
            }
            if (k < todo) todo = (unsigned)k;
        }
        Chip__GenerateBlock2(&s_chip, todo, s_buf);
        /* DBOPL's output is not centred (its log-sine table leans), and with
         * nine voices the offset eats headroom and clicks when notes stop:
         * a one-pole high-pass at ~13 Hz, y = x - x1 + 0.995 y1.
         * Then x4, and the mix is doubled on the way out (dg_sound.c): at
         * its own level E1M1 measured 470 RMS against the pistol's 20000
         * peaks, 16 dB under the effects; DMX kept the two much closer. */
        for (unsigned int i = 0; i < todo; i++) {
            int32_t x = s_buf[i];
            /* "/ 1024" and not ">> 10": the shift rounds negatives down and
             * the filter sticks at any value under ~200 below zero */
            s_dc_y = x - s_dc_x + s_dc_y * 1019 / 1024;
            s_dc_x = x;
            acc[filled + (int)i] += s_dc_y * 4;
        }
        filled += (int)todo;
        advance(todo);
    }
}
