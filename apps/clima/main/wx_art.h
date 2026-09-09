/*
 * CLIMA - weather icons, drawn in code.
 *
 * Eight icons times two variants (day and night) and three sizes would be
 * forty-eight bitmaps: storing them in the .so is hundreds of kilobytes of
 * flash and none of them can be rescaled without LVGL paying for a transform.
 * They come cheaper drawn: each shape is in thousandths of the side, so the
 * same code works for 104 px and for 26 px, and only the one being used is
 * generated.
 *
 * The format is RGB565A8 (colour and alpha in separate planes) because it is
 * the only one with an alpha channel that LVGL's drawer blends without
 * converting anything.
 */
#pragma once

#include "lvgl.h"
#include "wx_api.h"

typedef struct {
    lv_image_dsc_t dsc;
    uint8_t       *data;        /* w*h*2 of colour and then w*h of alpha */
    int            size;
    wx_icon_t      icon;
    bool           night;
} wx_sprite_t;

/* Draws the icon into the sprite. Returns false if there is no memory. */
bool wx_art_make(wx_sprite_t *sp, wx_icon_t icon, bool night, int size);
void wx_art_free(wx_sprite_t *sp);

/* Colour the main view's background is tinted with according to the weather. */
uint32_t wx_art_mood(wx_icon_t icon, bool night);
