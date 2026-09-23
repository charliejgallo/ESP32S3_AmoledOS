/*
 * AmoledOS - The control centre.
 *
 * A swipe down on the watchface pulls a panel over it: the date, the watch's
 * and the phone's battery, Settings' six quick tiles (aos_quick.h), the
 * brightness and the volume, the phone's music while something is playing,
 * and a way into Settings. A swipe up or the button puts it away, and so
 * does the screen dimming.
 *
 * Only on the watchface: inside an app a swipe down belongs to the app.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the panel and slides it down. With the LVGL lock. */
void aos_control_open(void);

/* Puts it away; without animation when something else is about to take the
 * screen (an app opening, the clock after dimming). */
void aos_control_close(bool animate);

bool aos_control_visible(void);

/* From aos_ui_tick(): keeps the panel's state current while it is up. */
void aos_control_tick(void);

#ifdef __cplusplus
}
#endif
