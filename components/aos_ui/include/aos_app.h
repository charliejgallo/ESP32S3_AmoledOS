/*
 * AmoledOS - Application model
 *
 * An app (built in, or loaded dynamically from a .so) implements this
 * contract. The runtime hands it a 368x448 LVGL container and takes care of
 * navigation, the status bar and the life cycle.
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever the ABI seen by dynamic apps changes.
 * The loader rejects any .so built against a different version.
 *
 * v2 (2026-08-26): the 'button' callback was added BETWEEN 'back' and 'tick'.
 * That shifts the offset of 'tick', so an old .so would write its tick where
 * the firmware now reads button: it would be called on every button press and
 * its real tick would never run, silently. None of the apps of the time used
 * tick, so we got away with it, but this is what the number is for. */
#define AOS_ABI_VERSION     2

typedef enum {
    AOS_APP_FLAG_NONE       = 0,
    AOS_APP_FLAG_KEEP_AWAKE = 1u << 0,  /* keep the screen on while this app is up */
    AOS_APP_FLAG_FULLSCREEN = 1u << 1,  /* no status bar */
    AOS_APP_FLAG_BACKGROUND = 1u << 2,  /* stays alive after exit (timer, music)   */
    AOS_APP_FLAG_NO_SWIPE   = 1u << 3,  /* the app handles the back gesture itself */

    /* A drag longer than 50px (LV_INDEV_DEF_GESTURE_LIMIT) fires the GLOBAL
     * gesture detection, which calls lv_indev_wait_release() so that
     * swipe-to-go-back does not also click whatever ended up under the
     * finger (aos_ui.c, gesture_cb). That aborts ANY touch in flight,
     * regardless of NO_SWIPE: an app that drags objects further than those
     * 50px -three cells of a board, say- loses its release event halfway
     * through. With this flag, gesture_cb does not call wait_release() for
     * this app: it need not, because NO_SWIPE already drains
     * handle_gesture() of any effect (see aos_ui.c). Deliberately a new flag
     * separate from NO_SWIPE: the existing apps (remoto, the calendar) rely
     * on the system still calling wait_release() on their own short
     * gestures, and this bit changes nothing for them because they do not
     * ask for it. */
    AOS_APP_FLAG_LONG_DRAG  = 1u << 4,
} aos_app_flags_t;


/* Vector icons drawn by the runtime (the built-in ones look good without
 * spending flash on bitmaps). Dynamic apps may use these or a glyph. */
typedef enum {
    AOS_ICON_NONE = 0,
    AOS_ICON_CLOCK,
    AOS_ICON_STOPWATCH,
    AOS_ICON_TIMER,
    AOS_ICON_ALARM,
    AOS_ICON_PHOTOS,
    AOS_ICON_MUSIC,
    AOS_ICON_SETTINGS,
    AOS_ICON_ACTIVITY,
    AOS_ICON_COMPASS,
    AOS_ICON_FLASHLIGHT,
    AOS_ICON_CALC,
    AOS_ICON_BATTERY,
    AOS_ICON_WIFI,
    AOS_ICON_APPS,
    AOS_ICON_MIC,
    AOS_ICON_INFO,
    AOS_ICON_WEATHER,
    AOS_ICON_LEVEL,
    AOS_ICON_REMOTE,

    /* Games. Deliberately at the END: the numeric value of everything above
     * cannot move, because the already-compiled .so files have it baked in.
     * Appending is compatible; inserting in the middle breaks every app. */
    AOS_ICON_PET,           /* Claudito */
    AOS_ICON_SHIP,          /* 2043     */
    AOS_ICON_GEM,           /* Gemas    */
    AOS_ICON_BRICKS,        /* Arkanos  */
    AOS_ICON_BIRD,          /* Flappy   */

    /* Generic ones, for a new game that has no icon of its own yet. */
    AOS_ICON_GAMEPAD,
    AOS_ICON_DICE,
    AOS_ICON_JOYSTICK,

    AOS_ICON_CALENDAR,      /* Calendar */

    AOS_ICON_POMODORO,      /* Pomodoro: the tomato with its leaf */
    AOS_ICON_GLOBE,         /* World clock: globe with a meridian */
    AOS_ICON_CONVERT,       /* Unit converter: two arrows */
    AOS_ICON_LIFE,          /* Game of Life: cells on a grid */

    AOS_ICON_SIMON,         /* Simon: the four coloured panels */
    AOS_ICON_MINES,         /* Minesweeper: the mine with its spikes */
    AOS_ICON_MAZE,          /* Maze: the ball between the walls */
    AOS_ICON_CARDS,         /* Truco: two Spanish playing cards overlapping */
    AOS_ICON_RADAR,         /* Network scanner: concentric rings and an echo */
    AOS_ICON_MONEY,         /* Exchange rates: the banknote with the coin on top */
    AOS_ICON_CHART,         /* Sensors: the jagged line over the two axes */
    AOS_ICON_JUMP,          /* Claude Jump: the critter jumping off its platform */
    AOS_ICON_PIXEL,         /* Pixel Art: a heart of cells on the grid */
} aos_icon_id_t;

typedef struct {
    const char *id;             /* "aos.timer", unique */
    const char *name;           /* what shows up in the menu */
    const char *icon;           /* LVGL glyph (LV_SYMBOL_*) or short text */
    aos_icon_id_t icon_vec;     /* if != NONE, takes precedence over 'icon' */
    uint32_t    color_a;        /* 0xRRGGBB, start of the icon gradient */
    uint32_t    color_b;        /* 0xRRGGBB, end of the gradient */
    uint32_t    flags;          /* aos_app_flags_t */
    int32_t     order;          /* order in the menu; lower comes first */
} aos_app_desc_t;

typedef struct aos_app_s aos_app_t;

struct aos_app_s {
    aos_app_desc_t desc;

    /* Builds the UI inside 'root' (368x448 minus the status bar). Returns the
     * instance context, or NULL if it needs no state. */
    void *(*create)(aos_app_t *self, lv_obj_t *root);

    /* Frees the context. LVGL objects under 'root' are deleted by the runtime. */
    void  (*destroy)(aos_app_t *self, void *inst);

    void  (*show)(aos_app_t *self, void *inst);   /* back in the foreground */
    void  (*hide)(aos_app_t *self, void *inst);   /* sent to the background */

    /* Back gesture/button. true = the app consumed it (internal navigation). */
    bool  (*back)(aos_app_t *self, void *inst);

    /* Physical button on the board. 'action' is an aos_button_action_t from
     * aos_hal.h, passed as an int so the app model does not have to drag in
     * the hardware header. Return true if the app consumed it; otherwise the
     * runtime does the usual (click = back, long press = clock).
     *
     * Runs with the LVGL lock held, but from the HAL's background task: best
     * to note it down and act on the next frame. */
    bool  (*button)(aos_app_t *self, void *inst, int action);

    /* Called ~5 times per second for as long as the app exists, even while
     * hidden if it has AOS_APP_FLAG_BACKGROUND. For stopwatches, timers, etc. */
    void  (*tick)(aos_app_t *self, void *inst);

    /* --- managed by the runtime, not to be touched by the app --- */
    void     *inst;
    lv_obj_t *root;
    void     *dl_handle;    /* dlopen() handle if this is a dynamic app */
    bool      running;
};

/* --------------------------------------------------------------------------
 * Entry point of dynamic apps (.so)
 *
 * The .so exports TWO FUNCTIONS, not a structure:
 *
 *     uint32_t aos_app_abi(void);
 *     bool     aos_app_init(aos_app_t *app);
 *
 * Their being functions is not a whim. Espressif's loader builds its symbol
 * table filtering on STT_FUNC (esp_elf.c: "ELF_ST_TYPE(...) == STT_FUNC"), so
 * **dlsym() cannot find a data object**: the previous version exported a
 * structure and the firmware failed with "does not export aos_app_entry" even
 * though nm showed it in the .so perfectly well.
 *
 * Always used through the macro:
 *
 *     AOS_APP_ENTRY(my_app_init);
 * -------------------------------------------------------------------------- */

#ifdef AOS_SIM_BUILTIN

/* In the simulator the app registers itself at startup, so the same source is
 * tested on the desktop without the board. */
void aos_sim_register_app(bool (*init)(aos_app_t *app));

#define AOS_APP_ENTRY(init_fn)                                       \
    __attribute__((constructor)) static void aos__autoregister(void) \
    {                                                                \
        aos_sim_register_app(init_fn);                               \
    }

#else

/* visibility("default") because .so files are compiled with -fvisibility=hidden */
#define AOS_APP_ENTRY(init_fn)                                       \
    __attribute__((visibility("default")))                           \
    uint32_t aos_app_abi(void)                                       \
    {                                                                \
        return AOS_ABI_VERSION;                                      \
    }                                                                \
    __attribute__((visibility("default")))                           \
    bool aos_app_init(aos_app_t *app)                                \
    {                                                                \
        return (init_fn)(app);                                       \
    }

#endif

#ifdef __cplusplus
}
#endif
