/* AmoledOS - Declarations shared between the pieces of the UI runtime. */
#pragma once

#include "lvgl.h"
#include "aos_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *aos_launcher_create(lv_obj_t *parent, aos_launcher_style_t style);

/* Closes the open folder, if there is one, and forgets it. Returns whether
 * there was one: "back" in the launcher closes the folder before the
 * launcher. */
bool      aos_launcher_close_folder(bool animate);

/* The watchface under an overlay that is not an app or the launcher (the
 * control centre): hidden once covered, shown before it uncovers. */
void      aos_ui_face_hide_if_covered(void);
void      aos_ui_face_show(void);
lv_obj_t *aos_watchface_create(lv_obj_t *parent);
void      aos_watchface_refresh(void);

/* The face under an app is torn down and built again when the app goes: its
 * objects and buffers (the analogue face's dial is a 185 KB canvas) are
 * PSRAM the app can use. Selecting, refreshing or dimming while suspended is
 * safe: the face is mounted again when it is needed. */
void      aos_watchface_suspend(void);
void      aos_watchface_resume(void);

#ifdef __cplusplus
}
#endif
