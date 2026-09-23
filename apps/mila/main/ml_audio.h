/*
 * MILA - the sound: Monster Hop's small synthesiser on the streaming speaker
 * (integer voices at 16 kHz, topped up from the frame timer) with Mila's
 * effects and a cosy looping tune per world, the casita and the map. A
 * world's tune is its `music` number in the worlds table.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SND_STEP = 0, SND_PUSH, SND_SLIDE, SND_ROLL, SND_FALL, SND_TARGET, SND_GATE, SND_FLAP,
    SND_BUMP, SND_UNDO, SND_ZOOM, SND_WIN, SND_MEOW, SND_PURR, SND_TOY, SND_POUNCE, SND_COIN,
    SND_BUY, SND_GIFT, SND_SELECT, SND_N,
};

enum { MUS_NONE = -1, MUS_W0 = 0, MUS_CASITA = 5, MUS_MAP = 6, MUS_N = 7 };

void ml_audio_open(void);
void ml_audio_close(void);
void ml_audio_tick(void);           /* from the frame timer                  */
void ml_snd(int id);
void ml_music(int theme);           /* MUS_NONE stops; worlds 0..4           */
void ml_audio_enable(bool sfx, bool music);
