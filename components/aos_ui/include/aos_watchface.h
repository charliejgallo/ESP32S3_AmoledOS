/*
 * AmoledOS - Interchangeable watchfaces.
 *
 * A face is built inside the container the runtime gives it and updated with
 * the time already resolved. It also has to know how to go into dimmed
 * (always-on) mode: fewer elements, no seconds, muted colours.
 *
 * A face is built into the firmware or comes as a module (.so) of its own,
 * from the card or uploaded through the portal (v0.15.0, docs/FACES.md). A
 * module's face is installed into the flash (/spiffs/faces) and runs from
 * there, so the face chosen is there at boot with or without the card.
 */
#pragma once

#include "lvgl.h"
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eight was exactly how many there were after adding the four new ones, and a
 * ninth would have been discarded silently inside aos_watchface_register().
 * Twelve cost 4 more descriptors in RAM and left room until the faces came
 * from modules (v0.15.0): seven built in and five on the release's card
 * already make twelve. */
#define AOS_MAX_WATCHFACES  24

/* Bumped whenever aos_watchface_t, or what a face module is promised, changes.
 * The loader refuses a face module built against another number. */
#define AOS_FACE_ABI_VERSION    1

typedef enum {
    AOS_FACE_FLAG_NONE  = 0,

    /* The face takes the touch (touch() below). Its long press is the face's
     * too when touch() takes it: the picker then opens with a double tap,
     * which opens it on every face. */
    AOS_FACE_FLAG_TOUCH = 1u << 0,
} aos_face_flags_t;

typedef enum {
    AOS_FACE_TOUCH_PRESS = 0,   /* the finger went down at x,y               */
    AOS_FACE_TOUCH_MOVE,        /* it moved (several per frame)              */
    AOS_FACE_TOUCH_RELEASE,     /* it went up, or a swipe took it            */
    AOS_FACE_TOUCH_TAP,         /* short and still, ~300 ms after the release
                                 * (a second tap there is the double tap that
                                 * opens the picker, and is not delivered)   */
    AOS_FACE_TOUCH_LONG,        /* still for LVGL's long press (~400 ms):
                                 * return false and the picker opens         */
} aos_face_touch_t;

typedef struct {
    const char *id;
    const char *name;

    /* Builds the face inside 'root' (368x448). Returns its context. */
    void *(*create)(lv_obj_t *root);

    /* Called several times a second while the screen is active, and once a
     * minute in dimmed mode. */
    void  (*refresh)(void *ctx, const struct tm *now);

    /* Switches between the normal look and the dimmed one. */
    void  (*set_aod)(void *ctx, bool aod);

    void  (*destroy)(void *ctx);

    /* --- from here on, v0.15.0: zero means "not used" --- */

    uint32_t flags;             /* aos_face_flags_t */

    /* With AOS_FACE_FLAG_TOUCH. Returns whether the face took the event:
     * what matters for TAP and LONG, which otherwise do what they do on any
     * face (LONG opens the picker). */
    bool  (*touch)(void *ctx, aos_face_touch_t what, int x, int y);

    /* An animated face: called every frame_ms (0 = 33, 30 fps) with the time
     * since the previous frame, ONLY while the face is what the screen shows,
     * lit (not dimmed, not under an app, the launcher or the control centre).
     * The host owns the timer: dimmed, it does not exist, so an animated face
     * costs nothing with the screen off and light sleep is not woken 30 times
     * a second. A face that has nothing to move should return at once (and
     * draw nothing): the timer is cheap, a repaint is not. */
    void  (*frame)(void *ctx, uint32_t dt_ms);
    uint16_t frame_ms;
} aos_watchface_t;

void aos_watchface_register(const aos_watchface_t *face);
void aos_watchfaces_register_builtin(void);

int                    aos_watchface_count(void);
const aos_watchface_t *aos_watchface_at(int index);
const char            *aos_watchface_current(void);
bool                   aos_watchface_select(const char *id);

/* Face picker: opened by long-pressing the clock, or with a double tap. */
void aos_watchface_open_picker(void);
/* The next (+1) or previous (-1) face, as the arrows do: a swipe sideways
 * with the picker open (aos_ui.c). */
void aos_watchface_picker_step(int delta);
bool aos_watchface_picker_visible(void);
void aos_watchface_close_picker(void);

/* Always-on mode. */
void aos_watchface_set_aod(bool aod);
bool aos_watchface_is_aod(void);

/* Whether the face is what the screen shows right now, lit: what frame()
 * already knows. For a face that animates from its own refresh(). */
bool aos_watchface_live(void);

/* What an animated face is getting, over the last two seconds: the frames
 * a second that frame() really ran, and the share of the time spent inside
 * it, in percent. Zero when no face is animating. /api/status reports them
 * (face_fps, face_busy): the way to measure a face on the watch. */
void aos_watchface_stats(int *fps, int *busy_pct);

/* Whether the face may push pixels straight to the panel
 * (aos_hal_display_blit) instead of invalidating them for LVGL: it is live
 * and NOTHING is drawn over it (the picker, a toast, a notification, the
 * status bar), since LVGL does not know what a blit put there. Measured with
 * the Video app: a full-screen canvas through LVGL costs ~95 ms a frame, the
 * blit 16.5 ms. A face that blits keeps its canvas up to date all the same,
 * so LVGL draws the right pixels whenever it has to (an overlay leaving). */
bool aos_watchface_can_blit(void);

/* Gravity in the SCREEN's axes, in g, from the accelerometer: x to the right
 * edge, y to the bottom edge, z into the glass. Standing in front of you the
 * watch reads (0, 1, 0); flat on the table, face up, (0, 0, 1). The board's
 * own axes are measured in docs/DECISIONES.md (+ax down, right is -ay, face up
 * is -az); this is the one place that knows them. False if there is no IMU. */
bool aos_watchface_gravity(float *x, float *y, float *z);

/* --------------------------------------------------------------------------
 * Faces from modules
 *
 * The host does not open files: aos_dynapp does (it has the loader and the
 * symbol table) and lends it these two. A module's face is registered with
 * its id and its name, copied, and its file; the module is opened when the
 * face is mounted and closed when another one is, so only the face on the
 * screen takes memory.
 * -------------------------------------------------------------------------- */

typedef struct {
    /* Opens 'path' and fills 'out' with the module's descriptor. Returns the
     * module's handle, NULL if it could not. */
    void *(*open)(const char *path, aos_watchface_t *out);
    void  (*close)(void *handle);
} aos_face_loader_t;

void aos_watchface_set_loader(const aos_face_loader_t *loader);

/* A module's face, with the file it lives in (copied). Replaces a module face
 * of the same id. False when the registry is full. */
bool aos_watchface_register_module(const char *id, const char *name, const char *path);

/* Takes a module's face out of the registry (its files are the caller's):
 * the portal's "remove". If it was the face chosen, the first face takes its
 * place. False for one of the firmware's faces or an unknown id. Call it with
 * the LVGL lock held. */
bool aos_watchface_unregister(const char *id);

/* The path of a module face's file ("/spiffs/faces/face_agua.so"), NULL for
 * a face built into the firmware. */
const char *aos_watchface_path(int index);

/* --------------------------------------------------------------------------
 * The entry point of a face module (.so)
 *
 * Two functions, not a structure, for the same reason as the apps'
 * (aos_app.h): the loader's dlsym only finds functions.
 *
 *     uint32_t aos_face_abi(void);
 *     bool     aos_face_init(aos_watchface_t *face);
 *
 * Always through the macro:
 *
 *     AOS_FACE_ENTRY(agua_get);
 *
 * In the simulator the face registers itself at startup instead, as the apps
 * do, so the same source is tried on the Mac.
 * -------------------------------------------------------------------------- */

#ifdef AOS_SIM_BUILTIN

void aos_sim_register_face(bool (*init)(aos_watchface_t *face));

#define AOS_FACE_ENTRY(init_fn)                                         \
    __attribute__((constructor)) static void aos__face_autoregister(void) \
    {                                                                   \
        aos_sim_register_face(init_fn);                                 \
    }

#else

#define AOS_FACE_ENTRY(init_fn)                                         \
    __attribute__((visibility("default")))                              \
    uint32_t aos_face_abi(void)                                         \
    {                                                                   \
        return AOS_FACE_ABI_VERSION;                                    \
    }                                                                   \
    __attribute__((visibility("default")))                              \
    bool aos_face_init(aos_watchface_t *face)                           \
    {                                                                   \
        return (init_fn)(face);                                         \
    }

#endif

/* Each built-in face exposes its descriptor through here. */
void aos_face_digital_get(aos_watchface_t *face);
void aos_face_analog_get(aos_watchface_t *face);
void aos_face_minimal_get(aos_watchface_t *face);
void aos_face_rings_get(aos_watchface_t *face);
void aos_face_binary_get(aos_watchface_t *face);
void aos_face_flip_get(aos_watchface_t *face);
void aos_face_nixie_get(aos_watchface_t *face);

#ifdef __cplusplus
}
#endif
