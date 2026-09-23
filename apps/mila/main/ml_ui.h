/*
 * MILA - the LVGL panels: boot, loading, pause, results, the shop,
 * settings, the lobby
 *
 * They sit over the canvas that shows the last frame. Pictures from the pack
 * are made by the worker (ml_ui_job: no LVGL there) and only wrapped here
 * (ml_ui_job_done), so the pack is only ever read from one thread.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct app app_t;

enum { UJ_ICONS = 1, UJ_PREVIEW };

void ml_ui_build(app_t *a, lv_obj_t *root);
void ml_ui_show(app_t *a, int state);
void ml_ui_tick(app_t *a, int dt_ms);
void ml_ui_free(app_t *a);

void ml_ui_job(app_t *a, int what);         /* worker                        */
void ml_ui_job_done(app_t *a, int what);    /* LVGL                          */

void ml_ui_boot_text(app_t *a, const char *txt);
/* the colourful loading screen, over everything, with its bar at pct */
void ml_ui_loader(app_t *a, bool show, int pct);
bool ml_ui_loader_on(void);
void ml_ui_loading_text(app_t *a, const char *txt);
/* gift: the item won by finishing the world, or NULL */
void ml_ui_result_fill(app_t *a, int stars, int earned, int moves, int par, const char *gift);
void ml_ui_lobby_fill(app_t *a, const char *txt, bool host);
/* a race's end: won, the moves on both sides, the coins */
void ml_ui_race_fill(app_t *a, bool won, int me, int them, int earned);
