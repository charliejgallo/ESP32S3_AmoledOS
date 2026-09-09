/*
 * Recorder - waveform.
 *
 * A row of little bars symmetric about the axis. It is used two ways: live
 * while recording (each new sample comes in from the right and everything
 * shifts left) and still in the detail view, showing the whole file.
 *
 * They are ordinary LVGL objects and not a canvas: 40 rounded rectangles
 * redraw effortlessly, and they stay crisp at any size.
 */
#pragma once

#include "lvgl.h"
#include <stdint.h>

typedef struct rec_wave_s rec_wave_t;

rec_wave_t *rec_wave_create(lv_obj_t *parent, int32_t width, int32_t height,
                            int bars, lv_color_t color);
void rec_wave_delete(rec_wave_t *wave);     /* the context only: the objects are deleted by LVGL */

lv_obj_t *rec_wave_obj(rec_wave_t *wave);

void rec_wave_push(rec_wave_t *wave, uint8_t peak);
void rec_wave_push_many(rec_wave_t *wave, const uint8_t *peaks, int count);
void rec_wave_set(rec_wave_t *wave, const uint8_t *peaks, int count);
void rec_wave_clear(rec_wave_t *wave);
void rec_wave_set_color(rec_wave_t *wave, lv_color_t color);

/* Bars up to 'bars' painted in the vivid colour and the rest dimmed: it is the
 * playback cursor. -1 switches the effect off and paints them all alike. */
void rec_wave_set_progress(rec_wave_t *wave, int bars);

int rec_wave_count(rec_wave_t *wave);
