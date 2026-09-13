/* The HAL's USB functions (aos_hal.h), implemented here and not in
 * aos_hal_esp32.c: aos_usb already depends on aos_hal (for the card), so the
 * other direction would be a cycle. The link does not care which library a
 * symbol declared in aos_hal.h comes from.
 *
 * The switch runs in a task of its own so that a click in Settings or in the
 * app does not hold LVGL for the seconds a host teardown can take. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "aos_hal.h"
#include "aos_usb.h"

static const char *TAG = "aos_usb";

static volatile bool          s_busy;
static aos_hal_usb_mode_t     s_want;

static aos_usb_mode_t to_usb(aos_hal_usb_mode_t m)
{
    switch (m) {
    case AOS_HAL_USB_KEYS: return AOS_USB_DEVICE;
    case AOS_HAL_USB_DISK: return AOS_USB_DISK;
    case AOS_HAL_USB_HOST: return AOS_USB_HOST;
    default:               return AOS_USB_CONSOLE;
    }
}

static aos_hal_usb_mode_t from_usb(aos_usb_mode_t m)
{
    switch (m) {
    case AOS_USB_DEVICE: return AOS_HAL_USB_KEYS;
    case AOS_USB_DISK:   return AOS_HAL_USB_DISK;
    case AOS_USB_HOST:   return AOS_HAL_USB_HOST;
    default:             return AOS_HAL_USB_CONSOLE;
    }
}

aos_hal_usb_mode_t aos_hal_usb_mode(void)
{
    return from_usb(aos_usb_mode_get());
}

static void switch_task(void *arg)
{
    (void)arg;
    if (!aos_usb_mode_set(to_usb(s_want))) {
        ESP_LOGW(TAG, "the switch to mode %d did not come up", (int)s_want);
    }
    s_busy = false;
    vTaskDelete(NULL);
}

bool aos_hal_usb_mode_set(aos_hal_usb_mode_t mode)
{
    if (s_busy) {
        return false;
    }
    if (mode == aos_hal_usb_mode()) {
        return true;
    }
    s_busy = true;
    s_want = mode;
    /* 6 K: the disk mode mounts a FAT volume on the way out, and the host
     * teardown waits on its tasks. Internal RAM, for the time of the switch. */
    if (xTaskCreatePinnedToCore(switch_task, "usb_sw", 6144, NULL, 3, NULL, 0) != pdPASS) {
        s_busy = false;
        return false;
    }
    return true;
}

bool aos_hal_usb_busy(void)
{
    return s_busy;
}

bool aos_hal_usb_keys_ready(void)
{
    return !s_busy && aos_usb_hid_ready();
}

bool aos_hal_usb_key(const char *name)
{
    return !s_busy && aos_usb_hid_named(name);
}

int aos_hal_usb_type(const char *ascii)
{
    return s_busy ? 0 : aos_usb_hid_type(ascii);
}

static int8_t clamp8(int v)
{
    return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v);
}

bool aos_hal_usb_mouse(int dx, int dy, int wheel)
{
    return !s_busy && aos_usb_hid_mouse(clamp8(dx), clamp8(dy), clamp8(wheel));
}

bool aos_hal_usb_click(int button)
{
    return !s_busy && aos_usb_hid_mouse_click(button == 2 ? 0x02 : 0x01);
}

bool aos_hal_usb_card_away(void)
{
    return aos_usb_disk_card_away();
}
