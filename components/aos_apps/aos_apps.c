/* AmoledOS - Registry of the built-in apps. */
#include "aos_apps.h"
#include "aos_ui.h"

void aos_apps_register_builtin(void)
{
    void (*const getters[])(aos_app_t *) = {
        aos_app_activity_get,
        aos_app_stopwatch_get,
        aos_app_timer_get,
        aos_app_pomodoro_get,
        aos_app_worldclock_get,
        aos_app_alarm_get,
        aos_app_calendar_get,
        aos_app_music_get,
        aos_app_remote_get,
        aos_app_photos_get,
        aos_app_flashlight_get,
        aos_app_level_get,
        aos_app_calc_get,
        aos_app_convert_get,
        aos_app_life_get,
        aos_app_power_get,
        aos_app_notifs_get,
        aos_app_settings_get,
    };

    for (unsigned i = 0; i < sizeof(getters) / sizeof(getters[0]); i++) {
        aos_app_t app;
        getters[i](&app);
        aos_ui_register_app(&app);
    }
}
