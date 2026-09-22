/*
 * MONSTER HOP - the sound: a small synthesiser on the streaming speaker
 *
 * Turbo's and Golf's machine (integer voices at 16 kHz, topped up from the
 * frame timer) plus a sequencer: each zone has a short looping theme
 * (bass, lead and drums) and the menu one of its own. Once the app opens
 * aos_hal_spk_*, aos_hal_beep() goes quiet: the app owns all its sound.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    SND_HOP = 0, SND_SUPER, SND_LAND, SND_KEY, SND_COIN, SND_HURT, SND_SPLASH, SND_FALL,
    SND_CHECK, SND_OPEN, SND_WIN, SND_LEVER, SND_CHEST, SND_PUSH, SND_BUMP, SND_HOWL,
    SND_TIMEUP, SND_OVER, SND_LIFE, SND_TIME, SND_STOMP, SND_CAST, SND_LOW, SND_SELECT,
    SND_GO, SND_N,
};

enum { MUS_NONE = -1, MUS_CITY = 0, MUS_CASTLE, MUS_DESERT, MUS_FOREST, MUS_MENU, MUS_N };

void mh_audio_open(void);
void mh_audio_close(void);
void mh_audio_tick(void);           /* from the frame timer                  */
void mh_snd(int id);
void mh_music(int theme, bool boss); /* MUS_NONE stops; boss = faster, lower */
void mh_audio_enable(bool sfx, bool music);
