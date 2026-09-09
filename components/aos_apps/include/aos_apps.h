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

/* The alarms, for the web portal. Six slots; a slot is a minute of the day
 * (0..1439, or -1 for empty) and whether it is enabled. get() reads the stored
 * value; set() stores it and asks the service to reload on its next tick,
 * which is what refreshes the app's list if it is open. Both are safe from
 * any task: they touch NVS, not the table the service checks. */
#define AOS_ALARM_MAX 6
/* 'days' is a seven-bit mask indexed by tm_wday (bit 0 = Sunday); 0x7F is
 * every day. set() refuses a live alarm with no day at all. */
bool aos_alarm_get(int index, int *minute_of_day, bool *enabled, int *days);
bool aos_alarm_set(int index, int minute_of_day, bool enabled, int days);

#ifdef __cplusplus
}
#endif
