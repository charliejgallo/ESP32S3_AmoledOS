/*
 * GEMAS - sound
 *
 * The only instrument there is is aos_hal_beep(): one tone at a time. So the
 * game's music is short melodies queued in a queue of our own, released note
 * by note from the frame loop.
 *
 * The queue has priority: if a new level turns up while the cascade is
 * playing, the fanfare overrides what was playing instead of sounding five
 * seconds late. The chains rise in pitch with the combo, which is what makes
 * chaining feel good.
 */
#pragma once

#include <stdbool.h>

typedef enum {
    GM_SFX_SELECT = 0,
    GM_SFX_SWAP,
    GM_SFX_DENY,
    GM_SFX_MATCH,       /* arg = cascade number (1 = the first) */
    GM_SFX_MATCH4,
    GM_SFX_MATCH5,
    GM_SFX_FLAME,
    GM_SFX_STAR,
    GM_SFX_HYPER,
    GM_SFX_LEVELUP,
    GM_SFX_GAMEOVER,
    GM_SFX_SHUFFLE,
    GM_SFX_TICK,        /* low-time warning */
    GM_SFX_START,
} gm_sfx_t;

void gm_snd_init(void);
void gm_snd_enable(bool on);
bool gm_snd_enabled(void);
void gm_snd_play(gm_sfx_t sfx, int arg);
void gm_snd_tick(void);     /* once per frame */
void gm_snd_stop(void);
