/* AmoledOS - Declarations shared between the pieces of the UI runtime. */
#pragma once

#include "lvgl.h"
#include "aos_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *aos_launcher_create(lv_obj_t *parent, aos_launcher_style_t style);
lv_obj_t *aos_watchface_create(lv_obj_t *parent);
void      aos_watchface_refresh(void);

#ifdef __cplusplus
}
#endif
