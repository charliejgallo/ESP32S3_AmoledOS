#pragma once
#include "aos_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registers every built-in app with the UI runtime. */
void aos_apps_register_builtin(void);

/* Each app exposes its descriptor+callbacks through this function. */
void aos_app_stopwatch_get(aos_app_t *app);
void aos_app_timer_get(aos_app_t *app);
void aos_app_alarm_get(aos_app_t *app);
void aos_app_photos_get(aos_app_t *app);
void aos_app_activity_get(aos_app_t *app);
void aos_app_flashlight_get(aos_app_t *app);
void aos_app_power_get(aos_app_t *app);
void aos_app_music_get(aos_app_t *app);
void aos_app_remote_get(aos_app_t *app);
void aos_app_level_get(aos_app_t *app);
void aos_app_calc_get(aos_app_t *app);
void aos_app_calendar_get(aos_app_t *app);
void aos_app_pomodoro_get(aos_app_t *app);
void aos_app_worldclock_get(aos_app_t *app);
void aos_app_convert_get(aos_app_t *app);
void aos_app_life_get(aos_app_t *app);
void aos_app_notifs_get(aos_app_t *app);
void aos_app_settings_get(aos_app_t *app);

/* Alarm service: it has to be called periodically from the main loop so the
 * alarms sound even with the app closed. */
void aos_alarm_service_tick(void);

#ifdef __cplusplus
}
#endif
