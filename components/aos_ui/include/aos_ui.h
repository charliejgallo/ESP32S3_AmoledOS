/*
 * AmoledOS - UI runtime: app registry, launcher and navigation.
 */
#pragma once

#include "lvgl.h"
#include "aos_app.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_MAX_APPS    48

/* Starts the runtime on LVGL's active screen. Registers the built-in apps and
 * shows the watchface. Call with the LVGL lock held. */
void aos_ui_init(void);

/* Registry. The app is copied by value; the pointer may be temporary. */
bool aos_ui_register_app(const aos_app_t *app);
bool aos_ui_unregister_app(const char *id);

int               aos_ui_app_count(void);
const aos_app_t  *aos_ui_app_at(int index);
aos_app_t        *aos_ui_app_find(const char *id);

/* Navigation */
bool aos_ui_open(const char *id);       /* opens an app by id, with an animation */
void aos_ui_back(void);                 /* back to the launcher / watchface */
void aos_ui_home(void);                 /* straight to the watchface */
void aos_ui_show_launcher(void);

const char *aos_ui_current_app(void);   /* NULL if in the launcher */

/* --------------------------------------------------------------------------
 * Touch controller gesture, for apps that keep the gestures.
 *
 * The v2's CST820 detects swipes on its own and during a fast one it stops
 * sending intermediate coordinates, so LVGL never gathers the 50 px it needs
 * and LV_EVENT_GESTURE never arrives. An app with horizontal pages has to ask
 * here.
 *
 * It is only kept for the front app when it asked for AOS_APP_FLAG_NO_SWIPE
 * (otherwise the gesture is dispatched as usual and going back is a swipe
 * right). Returns an aos_touch_gesture_t as an int, so as not to drag
 * aos_hal.h in here, and consumes it: a second call in a row returns NONE.
 * -------------------------------------------------------------------------- */
int aos_ui_take_gesture(void);

/* Routes the physical button to the front app. 'action' is an
 * aos_button_action_t. Returns true if the app consumed it; the caller handles
 * the default behaviour when it returns false. */
bool aos_ui_button(int action);

/* Status bar */
void aos_ui_statusbar_set_visible(bool visible);
void aos_ui_statusbar_refresh(void);

/* Ephemeral toast-style notice, in the centre of the screen. */
void aos_ui_toast(const char *text, uint32_t ms);

/* Asks to go back to the clock and open the watchface picker. Done on the next
 * tick: calling it from a button's callback would destroy the app while that
 * same callback is running. */
void aos_ui_request_watchface_picker(void);

/* Changes the language on the next tick. Deferred on purpose: applying it
 * there and then frees the strings the screen is built from, the very screen
 * whose callback asked for it. Same reason as the watchface picker. */
void aos_ui_request_language(const char *code);

/* Screen capture, for looking at the board from outside.
 *
 * The point is the things a log cannot show: whether a glyph is missing, text
 * that runs off the panel, two widgets on top of each other. The audit in the
 * simulator catches a lot of that, but the simulator is not the board.
 *
 * Deferred like everything else here, and for a harder reason than usual:
 * lv_snapshot_take() walks the object tree and draws it, so it has to run on
 * the task that owns LVGL. The caller is the HTTP server, on its own task.
 *
 * Three calls, in this order:
 *
 *     aos_ui_request_snapshot()   ask for one; false if one is already going
 *     aos_ui_snapshot_peek()      poll until READY or FAILED
 *     aos_ui_snapshot_release()   ALWAYS, even after FAILED
 *
 * 'wake' is almost always what you want. The watch dims after a few seconds,
 * and a dimmed screen shows the always-on face - just the time on black - so
 * a capture taken without waking will nearly always be of that and not of what
 * you meant to look at. It costs about a second: the request lands on one
 * tick, the display state is applied on the next, and the face animates back.
 * Pass false to photograph the screen exactly as it is, always-on face
 * included, which is the right choice when the always-on face IS the thing
 * being looked at.
 *
 * Between READY and release the buffer belongs to the caller and nothing
 * frees it. Skipping the release leaks 322 KB of PSRAM and blocks every later
 * capture, because the slot stays busy. */
typedef enum {
    AOS_SNAPSHOT_PENDING = 0,   /* the UI task has not gotten to it yet */
    AOS_SNAPSHOT_READY,         /* 'out' is filled in and valid until release */
    AOS_SNAPSHOT_FAILED,        /* no memory, or LVGL refused */
} aos_snapshot_state_t;

typedef struct {
    const uint8_t *data;    /* native RGB565, one uint16_t per pixel   */
    uint32_t       stride;  /* BYTES per row; may exceed w * 2         */
    uint16_t       w;
    uint16_t       h;
} aos_ui_snapshot_t;

bool aos_ui_request_snapshot(bool wake);
aos_snapshot_state_t aos_ui_snapshot_peek(aos_ui_snapshot_t *out);
void aos_ui_snapshot_release(void);

/* Launcher style */
typedef enum {
    AOS_LAUNCHER_LIST = 0,      /* vertical watchOS-style list (default) */
    AOS_LAUNCHER_GRID,          /* 3-column grid */
    AOS_LAUNCHER_HONEYCOMB,     /* honeycomb */
} aos_launcher_style_t;

void                 aos_ui_launcher_set_style(aos_launcher_style_t style);
aos_launcher_style_t aos_ui_launcher_get_style(void);

/* Called by the main loop every ~200 ms: hands ticks out to the apps. */
void aos_ui_tick(void);

/* Diagnostics: accumulated touch reads and presses. */
void aos_ui_touch_stats(uint32_t *reads, uint32_t *presses);

/* Touch calibration: screen = a * raw + b, per axis. */
void aos_ui_touch_calibration_save(float ax, float bx, float ay, float by);
void aos_ui_touch_calibration_reset(void);
void aos_ui_touch_raw(bool raw);    /* true = uncorrected, for calibrating */


#ifdef __cplusplus
}

#endif
