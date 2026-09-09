/*
 * AmoledOS - startup on the Waveshare ESP32-S3-Touch-AMOLED-1.8 board.
 *
 * app_main() brings the hardware up through the HAL, builds the UI and then
 * settles into a slow loop handing out ticks. All the drawing is done by the
 * LVGL task esp_lvgl_port creates; that is why every access to LVGL objects
 * from here goes between aos_hal_lock()/aos_hal_unlock().
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#include "aos_hal.h"
#include "aos_ui.h"
#include "aos_apps.h"
#include "aos_dynapp.h"
#include "aos_web.h"
#include "aos_ble.h"   /* F0: measurement, see docs/HANDOFF-BLE-ANCS.md */

static const char *TAG = "amoledos";

/* The BOOT button acts as "back"; holding it down goes back to the clock. It
 * is offered to the front app first, which may want it for something else (in
 * a game it is the trigger). It runs in the HAL's background task, so it takes
 * the LVGL lock. */
static void button_cb(aos_button_t button, aos_button_action_t action)
{
    if (button != AOS_BUTTON_BOOT) {
        return;
    }
    if (aos_hal_lock(200)) {
        if (!aos_ui_button((int)action)) {
            if (action == AOS_BUTTON_LONG) {
                aos_ui_home();
            } else if (action == AOS_BUTTON_CLICK) {
                aos_ui_back();
            }
        }
        aos_hal_unlock();
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "AmoledOS %s starting up", aos_hal_firmware_version());

    /* If the board restarted by itself, this says why. Without that fact, a
     * spontaneous restart is indistinguishable from a power cut. */
    {
        const char *causa;
        switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  causa = "normal power-on";            break;
        case ESP_RST_SW:       causa = "software restart";       break;
        case ESP_RST_PANIC:    causa = "PANIC (exception)";           break;
        case ESP_RST_TASK_WDT: causa = "WATCHDOG, task hung";   break;
        case ESP_RST_INT_WDT:  causa = "WATCHDOG, interrupts";  break;
        case ESP_RST_WDT:      causa = "chip watchdog";           break;
        case ESP_RST_BROWNOUT: causa = "BROWNOUT (voltage dropped)";  break;
        case ESP_RST_USB:      causa = "USB restart";            break;
        default:               causa = "other";                        break;
        }
        ESP_LOGI(TAG, "boot reason: %s", causa);
    }

    if (!aos_hal_init()) {
        ESP_LOGE(TAG, "hardware initialisation failed");
        return;
    }
    ESP_LOGI(TAG, "board: %s", aos_hal_board_name());

    aos_hal_set_button_cb(button_cb);

    if (aos_hal_lock(portMAX_DELAY)) {
        aos_ui_init();
        aos_apps_register_builtin();
        aos_hal_unlock();
    }

    /* Dynamic apps: every .so in /sdcard/apps is loaded and registered in the
     * menu as one more app. */
    int loaded = aos_dynapp_scan();
    ESP_LOGI(TAG, "dynamic apps loaded: %d", loaded);

    /* This is where the BLE stack will start in phase F5. The F0 measurement
     * script -aos_ble_measure_start()- was left in components/aos_ble/ with
     * nobody calling it: it switches WiFi and the radio off and on to take the
     * figures, which is exactly what you do not want in an everyday watch. It
     * gets plugged back in here when measuring is needed again. See
     * docs/HANDOFF-BLE-ANCS.md section 2.4.
     *
     * With nobody calling it, the linker drops NimBLE's ~217 KB and the
     * firmware weighs what it always did. */

    uint32_t ticks = 0;

    /* An image installed over the air boots ON TRIAL: if nobody confirms it,
     * the bootloader goes back to the previous one at the next restart. The
     * confirmation is below, at 30 s.
     *
     * Why 30 s and not right here: what the trial protects against is an image
     * that compiles and then does not come up -a panic in aos_hal_init(), a
     * watchdog while the .so files load, a screen that never draws-. All of
     * that has already happened or not by the time this loop has been turning
     * for half a minute, so half a minute is the evidence.
     *
     * And why it does NOT wait for the network, which was the first idea: an
     * image is not broken because the router is down. Asking for wifi would
     * roll back a firmware that works perfectly in a house whose internet went
     * out, and the user would never find out why the watch went backwards. */
    bool trial = aos_hal_ota_pending_verify();
    if (trial) {
        ESP_LOGW(TAG, "this image is on trial: it is confirmed at 30 s "
                      "or the bootloader goes back to the previous one");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(200));

        if (trial && ticks >= 150) {        /* 150 * 200 ms = 30 s */
            aos_hal_ota_mark_valid();
            trial = false;
        }

        /* The portal starts with the network ready, and also with the setup
         * access point up: that is precisely where it is needed. */
        /* With no network there is no point leaving the server taking up
         * memory. */
        if (aos_web_running() &&
            aos_hal_net_state() != AOS_NET_CONNECTED && !aos_hal_net_ap_active()) {
            aos_web_stop();
            ESP_LOGI(TAG, "web portal off: there is no network");
        }

        if (!aos_web_running() &&
            (aos_hal_net_state() == AOS_NET_CONNECTED || aos_hal_net_ap_active())) {
            if (aos_web_start() == ESP_OK) {
                ESP_LOGI(TAG, "web portal at http://%s/", aos_hal_net_ip());
            }
        }

        if (aos_hal_lock(100)) {
            aos_ui_tick();
            aos_alarm_service_tick();
            aos_hal_unlock();

            aos_dynapp_tick();      /* closes the .so files that were left unused */
        }

        /* Diagnostic heartbeat. If the screen stops responding, this line says
         * whether the system is still alive, what state the display is in and
         * whether the touch panel is still reading and detecting fingers. */
        if (++ticks % 15 == 0) {
            uint32_t reads = 0, presses = 0;
            aos_ui_touch_stats(&reads, &presses);
            aos_display_state_t st = aos_hal_display_state();
            aos_ble_tick();
            aos_bt_state_t bt = aos_hal_bt_state();
            ESP_LOGI(TAG, "heartbeat: display=%s touch reads=%lu fingers=%lu "
                          "bt=%s heap_int=%u psram=%u exec=%u",
                     st == AOS_DISPLAY_ACTIVE ? "active" :
                     st == AOS_DISPLAY_AOD    ? "dimmed" : "off",
                     (unsigned long)reads, (unsigned long)presses,
                     bt == AOS_BT_CONNECTED   ? "connected" :
                     bt == AOS_BT_PAIRING     ? "pairing" :
                     bt == AOS_BT_ADVERTISING ? "advertising" : "off",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC));

            char cargadas[160];
            int n = aos_dynapp_loaded_list(cargadas, sizeof(cargadas));
            if (n > 0) {
                ESP_LOGI(TAG, "  %d .so still loaded (%u K executable "
                              "free): %s", n,
                         (unsigned)(heap_caps_get_free_size(MALLOC_CAP_EXEC) / 1024),
                         cargadas);
            }
        }
    }
}
