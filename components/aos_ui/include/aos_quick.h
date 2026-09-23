/*
 * AmoledOS - Quick controls: the six tiles and the pill slider.
 *
 * Shared by Settings' first page and the control centre on the watchface
 * (aos_control.h), so the two are the same code and change together.
 */
#pragma once

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wi-Fi, Bluetooth, Flashlight, Always on, Saver, Do not disturb: three per
 * row, filling 'width'. Each tile acts on its own (the flashlight opens its
 * app, deferred) and repaints the set. */
lv_obj_t *aos_quick_tiles_create(lv_obj_t *parent, int32_t width, int32_t tile_h);

/* Paints each tile from the current state; call it when the state may have
 * changed elsewhere (the portal, the other page). Cheap when nothing did. */
void      aos_quick_tiles_paint(lv_obj_t *tiles);

/* A pill slider with a glyph on the left and "N %" on the right, no knob.
 * 'cb' gets LV_EVENT_VALUE_CHANGED. */
lv_obj_t *aos_quick_slider(lv_obj_t *parent, const char *glyph, int value,
                           int min, int max, int32_t width, int32_t height,
                           lv_event_cb_t cb);

/* The actions behind three of the tiles, with their toast: Settings' own
 * switches call them too. */
void      aos_quick_set_wifi(bool on);
void      aos_quick_set_bt(bool on);
void      aos_quick_set_dnd(bool dnd);

#ifdef __cplusplus
}
#endif
