/*
 * AmoledOS - A folder's icon: a hexagon, pointy side up, with a glyph.
 *
 * Apps are circles; folders are hexagons so the two never get confused, and
 * pointy-up is the orientation that sits in the honeycomb launcher. The
 * hexagon's colour, fill and glyph come from the folder's line in menu.txt
 * (aos_menu.h); the glyphs are a curated set of Material Design Icons
 * (tools/gen_folder_glyphs.py).
 *
 * The icon is computed into ONE image per folder when the launcher is built
 * and then only copied: scrolling the menu costs a blit, like any image.
 */
#pragma once

#include "lvgl.h"
#include "aos_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;           /* MDI's own name, what menu.txt stores */
    uint32_t    codepoint;      /* in aos_folder_font */
} aos_folder_glyph_t;

extern const aos_folder_glyph_t aos_folder_glyphs[];
extern const int                aos_folder_glyph_count;

/* The glyph's code point by name; the catalogue's first ("folder") if the
 * name is not in it. */
uint32_t aos_folder_glyph_codepoint(const char *name);

/* An image of size x size, transparent outside the hexagon. The pixels are
 * freed with the object. NULL only when there is no memory for them. */
lv_obj_t *aos_folder_icon_create(lv_obj_t *parent, const aos_menu_folder_t *folder,
                                 int32_t size);

#ifdef __cplusplus
}
#endif
