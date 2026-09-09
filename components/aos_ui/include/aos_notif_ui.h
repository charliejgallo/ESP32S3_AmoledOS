/*
 * AmoledOS - the phone's notification, full screen.
 *
 * It hangs off lv_layer_top(), which is above the clock, the launcher, any app
 * and the status bar. It is the same place Settings' modal screens already
 * live.
 *
 * Everything it does is triggered from aos_ui_tick(), that is, from the LVGL
 * task. The HAL's queue is what separates this file from the bluetooth task.
 */
#pragma once

#include "lvgl.h"
#include "aos_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shows one. If another was already on screen, it replaces it: the last thing
 * that arrived is what you want to see.
 *
 * 'desde_historial' distinguishes the two ways of reaching this screen, which
 * call for different things: one that has just arrived is read and closed, and
 * one you went to look for in the list you nearly always want to take off it.
 * Only the second shows the delete button. */
void aos_notif_ui_show(const aos_notif_t *n, bool desde_historial);

bool     aos_notif_ui_visible(void);
uint32_t aos_notif_ui_uid(void);        /* 0 if there is none */
void     aos_notif_ui_close(void);

/* From aos_ui_tick(): keeps the screen awake for a while and no longer. */
void aos_notif_ui_tick(void);

#ifdef __cplusplus
}
#endif
