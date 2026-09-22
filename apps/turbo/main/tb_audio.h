/*
 * TURBO - the sound (see tb_audio.c)
 */
#pragma once

#include <stdbool.h>

enum {
    SND_TICK = 0,
    SND_COUNT,
    SND_GO,
    SND_CHECKPOINT,
    SND_CRASH,
    SND_BUMP,
    SND_PASS,
    SND_GEAR,
    SND_FINISH,
    SND_TIMEUP,
    SND_LOW,
    SND_BUY,
    SND_NO,
};

void tb_audio_open(void);
void tb_audio_close(void);
bool tb_audio_is_open(void);
/* once a frame: keeps the speaker's ring topped up */
void tb_audio_tick(void);
void tb_snd(int id);
void tb_audio_tone(int hz, int ms);
/* the engine and the road: revs, throttle and speed 0..1, squeal and gravel 0..1 */
void tb_audio_engine(bool on, float rpm, float throttle, float speed01, float squeal, float gravel);
