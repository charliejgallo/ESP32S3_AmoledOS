/*
 * AmoledOS - Doom: the seam between the app (doom.c, LVGL's task) and the
 * engine (doomgeneric + port/, the worker on core 0).
 *
 * Neither side includes the other's headers: doom.c knows nothing of the
 * engine's thousand globals, and the engine knows nothing of LVGL. What
 * crosses is plain memory: three frame slots, a ring of button events and
 * the stick, all written by one side and read by the other.
 */
#ifndef DOOM_PORT_H
#define DOOM_PORT_H

#include <stdbool.h>
#include <stdint.h>

/* The picture: 320x200 scaled by 1.15 to the screen's width. Square pixels
 * (Doom was drawn for 4:3, i.e. 1.2 tall; 368x276 would be exact but leaves
 * no room for the pad above the touch's lower limit). */
#define DP_W        368
#define DP_H        230
#define DP_SLOTS    3

/* Buttons of the pad. What each one sends depends on the engine's state
 * (a menu, a y/n question, the game), so the translation is on its side. */
enum {
    DP_BTN_FIRE = 0,
    DP_BTN_USE,
    DP_BTN_MENU,
    DP_BTN_MAP,
    DP_BTN_WEAPON,
    DP_BTN_N
};

typedef enum {
    DP_IDLE = 0,
    DP_LOADING,     /* reading the WAD */
    DP_RUNNING,
    DP_QUIT,        /* the player quit from Doom's menu */
    DP_STOPPED,     /* stopped from outside (the app closed) */
    DP_ERROR,       /* I_Error: dp_error() says what */
} dp_state_t;

/* Where Doom's things live on the card. data_dir ends with '/'. */
bool        dp_find_wad(char *out, int len);
const char *dp_data_dir(void);

/* Starts the engine in the app's worker. false: no memory or no worker. */
bool        dp_start(const char *wad_path, bool swap_bytes);

/* Stops it (from destroy) and gives every byte back. */
void        dp_stop(void);

dp_state_t  dp_state(void);
const char *dp_error(void);

/* The newest finished frame, or NULL. The slot stays untouched by the
 * engine until dp_frame_blitted() is called after the NEXT frame's blit:
 * the blit is asynchronous and reads the slot after the call returns. */
const uint16_t *dp_frame_take(void);
void        dp_frame_blitted(void);     /* after the blit of what take() gave */
uint32_t    dp_frames(void);        /* frames finished since start */
const char *dp_where(void);
void        dp_audio_cycles(uint32_t *music, uint32_t *total);  /* running sums */         /* "level E1M1", "title +menu"... for the log */

/* Input, from LVGL's task. */
void        dp_button(int btn, bool down);
void        dp_stick(int x, int y);     /* -100..100 each, 0,0 = released */

/* Sound: the app opens the speaker on its side (the codec lives on the
 * I2C bus the touch uses), the engine only mixes into it. */
void        dp_sound_enable(bool on);

#endif
