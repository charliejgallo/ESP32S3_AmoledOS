/*
 * ATASCO - draws each car in code, like Gemas's jewels: no bitmaps, a handful
 * of lv_obj (body, cab and an ornament) created once. They are moved
 * afterwards with lv_obj_set_pos, never rebuilt.
 */
#pragma once

#include "lvgl.h"
#include "at_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a car's view (body + cab + wheels + the model's ornament) at the
 * size that corresponds to car->len and car->horizontal, at (0,0). The caller
 * repositions it with lv_obj_set_pos. 'cell_px' is the side of one board cell
 * and 'gap_px' the margin it leaves free between neighbouring cars. */
lv_obj_t *at_car_view_create(lv_obj_t *parent, const at_car_t *car,
                             int cell_px, int gap_px);

#ifdef __cplusplus
}
#endif
