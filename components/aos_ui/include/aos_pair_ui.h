/*
 * AmoledOS - the pairing request, full screen.
 *
 * It exists because **pairing is started by the phone**, not by the watch: the
 * six-digit code arrives when the user taps "AmoledOS" over there, and until
 * this existed it could only be seen if you happened to be standing on the
 * Settings -> Bluetooth -> Pair phone screen. Otherwise the bar's icon turned
 * the accent colour and nothing more, and the pairing timed out by itself.
 *
 * A pairing request is as interrupting as a notification, so it is drawn the
 * same way: top layer, full screen, opaque background.
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called by aos_ui_tick(). Brings the screen up when a code appears and closes
 * it when there stops being one -because it was confirmed, cancelled, or the
 * phone got bored-. */
void aos_pair_ui_tick(void);

bool aos_pair_ui_visible(void);

/* Cancels and closes. Used by the back gesture and the physical button. */
void aos_pair_ui_cancel(void);

/* Settings has a pairing screen of its own, with the same code and the same
 * buttons. While that one is open, this one keeps quiet: two screens saying
 * the same thing, one on top of the other, is worse than none. */
void aos_pair_ui_suppress(bool suprimir);

#ifdef __cplusplus
}
#endif
