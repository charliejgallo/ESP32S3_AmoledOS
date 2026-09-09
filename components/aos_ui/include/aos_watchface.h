/*
 * AmoledOS - Interchangeable watchfaces.
 *
 * A face is built inside the container the runtime gives it and updated with
 * the time already resolved. It also has to know how to go into dimmed
 * (always-on) mode: fewer elements, no seconds, muted colours.
 */
#pragma once

#include "lvgl.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Eight was exactly how many there were after adding the four new ones, and a
 * ninth would have been discarded silently inside aos_watchface_register().
 * Twelve cost 4 more descriptors in RAM and leave room. */
#define AOS_MAX_WATCHFACES  12

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
} aos_watchface_t;

void aos_watchface_register(const aos_watchface_t *face);
void aos_watchfaces_register_builtin(void);

int                    aos_watchface_count(void);
const aos_watchface_t *aos_watchface_at(int index);
const char            *aos_watchface_current(void);
bool                   aos_watchface_select(const char *id);

/* Face picker: opened by long-pressing the clock. */
void aos_watchface_open_picker(void);
bool aos_watchface_picker_visible(void);
void aos_watchface_close_picker(void);

/* Always-on mode. */
void aos_watchface_set_aod(bool aod);
bool aos_watchface_is_aod(void);

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
