/*
 * GOLF - the sound (see gf_audio.c)
 */
#pragma once

#include <stdbool.h>

enum {
    SND_TICK = 0,
    SND_METER,
    SND_WHOOSH,
    SND_IMPACT,
    SND_IMPACT_WOOD,
    SND_BOUNCE,
    SND_TREE,
    SND_SPLASH,
    SND_CUP,
    SND_GOOD,
    SND_BAD,
    SND_APPLAUSE,
    SND_BUY,
    SND_NO,
};

void gf_audio_open(void);
void gf_audio_close(void);
void gf_audio_mute(bool mute);
/* birds and wind, on the course */
void gf_audio_ambience(bool on);
/* once a frame: keeps the speaker's ring topped up */
void gf_audio_tick(void);
void gf_snd(int id);
/* a plain tone, for the interface's little signals */
void gf_audio_tone(int hz, int ms);
