/* aos_usb: who owns the USB port. See include/aos_usb.h and docs/USB.md.
 *
 * Phase 1 of the USB work: the switch, and the two smallest things each side
 * can do so the switch can be tested from the portal.
 *
 *   CONSOLE  the PHY on the USB-Serial-JTAG, as at boot. Nothing installed.
 *   DEVICE   TinyUSB with one CDC-ACM interface; stdout goes to it unless
 *            aos_usb_console_on_cdc(false) was called. The Mac sees an
 *            "AmoledOS" serial port.
 *   HOST     the USB Host Library with one client that opens every device
 *            that enumerates, writes its descriptors to the log and keeps a
 *            short table of them for /api/usb.
 *
 * The mux: usb_new_phy() (inside both installs) points the internal PHY at
 * the OTG controller. usb_del_phy() (inside both uninstalls) does NOT point it
 * back — it only clears the pull-up overrides — so after leaving an OTG mode
 * this file flips RTC_CNTL.usb_conf.sw_usb_phy_sel back itself, with the two
 * LL calls the Serial-JTAG driver uses at boot. Whether the Mac re-enumerates
 * the Serial-JTAG after that without a reset is test T2 of docs/USB.md.
 *
 * Every switch logs the internal / executable heap before and after, which is
 * test T3: the cost of each stack, in the log and in /api/usb ("mem"). */
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_pm.h"
#include "esp_intr_alloc.h"
#include "hal/usb_serial_jtag_ll.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "vfs_tinyusb.h"
#include "esp_vfs_console.h"
#include <fcntl.h>
#include <unistd.h>
#include "aos_usb.h"

static const char *TAG = "aos_usb";

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

#define AOS_USB_MAX_DEV   4        /* what the inspector's table holds */
#define AOS_USB_MAX_ITF   6

typedef struct {
    bool     used;
    uint8_t  addr;
    uint8_t  speed;                 /* usb_speed_t */
    uint16_t vid, pid;
    uint8_t  dev_class, dev_subclass, dev_protocol;
    uint8_t  n_itf;
    uint8_t  itf_class[AOS_USB_MAX_ITF];
    uint8_t  itf_subclass[AOS_USB_MAX_ITF];
    uint8_t  itf_protocol[AOS_USB_MAX_ITF];
    char     manufacturer[32];
    char     product[32];
    char     serial[24];
    usb_device_handle_t hdl;        /* kept open until it goes, like the IDF example */
} aos_usb_dev_t;

static aos_usb_mode_t       s_mode = AOS_USB_CONSOLE;
static bool                 s_console_on_cdc = true;
static bool                 s_console_redirected;
static SemaphoreHandle_t    s_lock;             /* mode changes and the device table */
static esp_pm_lock_handle_t s_pm_lock;
static aos_usb_dev_t        s_devs[AOS_USB_MAX_DEV];
static int                  s_seen;             /* devices enumerated since host mode came up */

/* Host mode: two tasks, as in the IDF usb_host_lib example. */
static usb_host_client_handle_t s_client;
static SemaphoreHandle_t        s_task_done;    /* counting: each task gives once on exit */
static SemaphoreHandle_t        s_installed;    /* binary: the library task reports usb_host_install() */
static esp_err_t                s_install_err;
static volatile bool            s_client_quit;
static volatile uint8_t         s_pending_new[AOS_USB_MAX_DEV];
static volatile int             s_pending_new_n;
static usb_device_handle_t      s_pending_gone[AOS_USB_MAX_DEV];
static volatile int             s_pending_gone_n;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                     */
/* -------------------------------------------------------------------------- */

static bool ensure_init(void)
{
    if (s_lock) {
        return true;
    }
    s_lock = xSemaphoreCreateMutex();
    s_task_done = xSemaphoreCreateCounting(2, 0);
    s_installed = xSemaphoreCreateBinary();
    if (!s_lock || !s_task_done || !s_installed) {
        ESP_LOGE(TAG, "no memory for the locks");
        return false;
    }
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "aos_usb", &s_pm_lock) != ESP_OK) {
        s_pm_lock = NULL;       /* no PM: fine, there is no light sleep to hold off */
    }
    return true;
}

static void mem_line(const char *when)
{
    ESP_LOGI(TAG, "mem %s: internal %u free, exec %u free, largest exec block %u",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));
}

/* usb_del_phy() leaves the mux on the OTG side; the two LL calls are what
 * the Serial-JTAG driver does at boot (usb_serial_jtag.c, driver install).
 *
 * The pad goes down first, for a moment: measured on the board (2026-09-12,
 * test T2), flipping the mux with the pad enabled keeps D+ high through the
 * change, the Mac never sees a detach, and it goes on talking to the address
 * of the device that is no longer there ("AmoledOS watch" stays in ioreg and
 * the Serial-JTAG never comes back until the cable is pulled). A few ms with
 * the pull-up gone is a detach; enabling the pad again is a fresh attach. */
static void phy_back_to_console(void)
{
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_serial_jtag_ll_phy_enable_external(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    usb_serial_jtag_ll_phy_enable_pad(true);
}

static void str_copy(char *out, size_t len, const usb_str_desc_t *d)
{
    out[0] = 0;
    if (!d || d->bLength < 2) {
        return;
    }
    int n = (d->bLength - 2) / 2;
    size_t o = 0;
    for (int i = 0; i < n && o + 1 < len; i++) {
        uint16_t c = d->wData[i];
        out[o++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[o] = 0;
}

static const char *class_name(uint8_t c)
{
    switch (c) {
    case USB_CLASS_PER_INTERFACE: return "per-interface";
    case USB_CLASS_AUDIO:         return "audio";
    case USB_CLASS_COMM:          return "cdc";
    case USB_CLASS_HID:           return "hid";
    case USB_CLASS_PRINTER:       return "printer";
    case USB_CLASS_MASS_STORAGE:  return "msc";
    case USB_CLASS_HUB:           return "hub";
    case USB_CLASS_CDC_DATA:      return "cdc-data";
    case USB_CLASS_VIDEO:         return "video";
    case USB_CLASS_WIRELESS_CONTROLLER: return "wireless";
    case USB_CLASS_MISC:          return "misc";
    case USB_CLASS_APP_SPEC:      return "app-specific";
    case 0xff:                    return "vendor";
    default:                      return "other";
    }
}

/* -------------------------------------------------------------------------- */
/* Device mode: TinyUSB, one CDC port                                          */
/* -------------------------------------------------------------------------- */

/* The console on the CDC port, by hand.
 *
 * esp_tinyusb ships tinyusb_console_init()/deinit() for this, and its deinit
 * restores the streams by reopening "/dev/uart/<CONFIG_ESP_CONSOLE_UART_NUM>".
 * This firmware's console is the USB-Serial-JTAG, so that number is -1, the
 * freopen() fails, the function assigns the NULL it got to stdout, and the
 * next log line from any task is a PANIC (measured on the board, 2026-09-12,
 * test T2). The real console is "/dev/console" whatever it sits on.
 *
 * freopen() reworks the FILE object in place, and every task's stdout points
 * at the same object, so one call moves everybody. On failure newlib leaves
 * the stream unusable, which is why each path is tried with open() first. */
static bool stream_to(const char *path)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        ESP_LOGW(TAG, "%s does not open, the console stays where it is", path);
        return false;
    }
    close(fd);
    FILE *o = freopen(path, "w", stdout);
    FILE *e = freopen(path, "w", stderr);
    FILE *i = freopen(path, "r", stdin);
    return o && e && i;
}

static bool device_start(void)
{
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    esp_err_t e = tinyusb_driver_install(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(e));
        return false;
    }
    tinyusb_config_cdcacm_t acm = { .cdc_port = TINYUSB_CDC_ACM_0 };
    e = tinyusb_cdcacm_init(&acm);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(e));
        tinyusb_driver_uninstall();
        return false;
    }
    if (s_console_on_cdc) {
        /* Non-blocking VFS (vfs_tinyusb.c): with nobody reading the port the
         * output is dropped, never stalls the tasks that log. */
        s_console_redirected = esp_vfs_tusb_cdc_register(TINYUSB_CDC_ACM_0, NULL) == ESP_OK &&
                               stream_to(VFS_TUSB_PATH_DEFAULT);
    }
    ESP_LOGI(TAG, "device mode: CDC up, console %s",
             s_console_redirected ? "on the CDC port" : "stays on the Serial-JTAG (now silent)");
    return true;
}

static void device_stop(void)
{
    if (s_console_redirected) {
        stream_to(ESP_VFS_DEV_CONSOLE);
        esp_vfs_tusb_cdc_unregister(NULL);
        s_console_redirected = false;
    }
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    esp_err_t e = tinyusb_driver_uninstall();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "tinyusb_driver_uninstall: %s", esp_err_to_name(e));
    }
}

/* -------------------------------------------------------------------------- */
/* Host mode: the inspector                                                    */
/* -------------------------------------------------------------------------- */

static void client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    /* Runs inside usb_host_client_handle_events(), in the client task. Only
     * take note here; the opening is done by the task loop, as the IDF
     * example does. */
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (s_pending_new_n < AOS_USB_MAX_DEV) {
            s_pending_new[s_pending_new_n++] = msg->new_dev.address;
        }
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        if (s_pending_gone_n < AOS_USB_MAX_DEV) {
            s_pending_gone[s_pending_gone_n++] = msg->dev_gone.dev_hdl;
        }
    }
}

static void inspect(uint8_t addr)
{
    usb_device_handle_t hdl;
    esp_err_t e = usb_host_device_open(s_client, addr, &hdl);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "open device %d: %s", addr, esp_err_to_name(e));
        return;
    }
    usb_device_info_t info;
    const usb_device_desc_t *dd;
    const usb_config_desc_t *cd;
    if (usb_host_device_info(hdl, &info) != ESP_OK ||
        usb_host_get_device_descriptor(hdl, &dd) != ESP_OK ||
        usb_host_get_active_config_descriptor(hdl, &cd) != ESP_OK) {
        ESP_LOGW(TAG, "device %d: could not read its descriptors", addr);
        usb_host_device_close(s_client, hdl);
        return;
    }

    /* To the log, in full, with IDF's printers. */
    ESP_LOGI(TAG, "new device at address %d, %s speed", addr,
             (const char *[]) { "low", "full", "high" }[info.speed]);
    usb_print_device_descriptor(dd);
    usb_print_config_descriptor(cd, NULL);
    if (info.str_desc_manufacturer) usb_print_string_descriptor(info.str_desc_manufacturer);
    if (info.str_desc_product)      usb_print_string_descriptor(info.str_desc_product);
    if (info.str_desc_serial_num)   usb_print_string_descriptor(info.str_desc_serial_num);

    /* And to the table, for /api/usb. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    aos_usb_dev_t *d = NULL;
    for (int i = 0; i < AOS_USB_MAX_DEV; i++) {
        if (!s_devs[i].used) { d = &s_devs[i]; break; }
    }
    if (d) {
        memset(d, 0, sizeof(*d));
        d->used = true;
        d->hdl = hdl;
        d->addr = addr;
        d->speed = info.speed;
        d->vid = dd->idVendor;
        d->pid = dd->idProduct;
        d->dev_class = dd->bDeviceClass;
        d->dev_subclass = dd->bDeviceSubClass;
        d->dev_protocol = dd->bDeviceProtocol;
        for (int i = 0; i < cd->bNumInterfaces && d->n_itf < AOS_USB_MAX_ITF; i++) {
            int off = 0;
            const usb_intf_desc_t *itf = usb_parse_interface_descriptor(cd, i, 0, &off);
            if (!itf) {
                continue;
            }
            d->itf_class[d->n_itf] = itf->bInterfaceClass;
            d->itf_subclass[d->n_itf] = itf->bInterfaceSubClass;
            d->itf_protocol[d->n_itf] = itf->bInterfaceProtocol;
            d->n_itf++;
        }
        str_copy(d->manufacturer, sizeof(d->manufacturer), info.str_desc_manufacturer);
        str_copy(d->product, sizeof(d->product), info.str_desc_product);
        str_copy(d->serial, sizeof(d->serial), info.str_desc_serial_num);
        s_seen++;
        ESP_LOGI(TAG, "device %d: %04x:%04x \"%s\" \"%s\", %d interface(s), first class %s",
                 addr, d->vid, d->pid, d->manufacturer, d->product, d->n_itf,
                 d->n_itf ? class_name(d->itf_class[0]) : "-");
    } else {
        ESP_LOGW(TAG, "device %d: the table is full, closing it", addr);
        usb_host_device_close(s_client, hdl);
    }
    xSemaphoreGive(s_lock);
}

static void drop_gone(usb_device_handle_t hdl)
{
    /* The handle stays valid until it is closed; closing it is what lets the
     * library free the device. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < AOS_USB_MAX_DEV; i++) {
        if (s_devs[i].used && s_devs[i].hdl == hdl) {
            ESP_LOGI(TAG, "device %d gone", s_devs[i].addr);
            usb_host_device_close(s_client, hdl);
            s_devs[i].used = false;
        }
    }
    xSemaphoreGive(s_lock);
}

static void close_all(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < AOS_USB_MAX_DEV; i++) {
        if (s_devs[i].used) {
            usb_host_device_close(s_client, s_devs[i].hdl);
            s_devs[i].used = false;
        }
    }
    xSemaphoreGive(s_lock);
}

static void host_client_task(void *arg)
{
    usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = { .client_event_callback = client_event_cb, .callback_arg = NULL },
    };
    esp_err_t e = usb_host_client_register(&cfg, &s_client);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_client_register: %s", esp_err_to_name(e));
        s_client = NULL;
        xSemaphoreGive(s_task_done);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "host mode: waiting for a device");
    while (!s_client_quit) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(250));
        while (s_pending_new_n > 0) {
            uint8_t addr = s_pending_new[--s_pending_new_n];
            inspect(addr);
        }
        while (s_pending_gone_n > 0) {
            drop_gone(s_pending_gone[--s_pending_gone_n]);
        }
    }
    close_all();
    usb_host_client_deregister(s_client);
    s_client = NULL;
    xSemaphoreGive(s_task_done);
    vTaskDelete(NULL);
}

/* usb_host_install() allocates the OTG interrupt on the core it is called
 * from. Called from the portal's httpd task it failed with ESP_ERR_NOT_FOUND
 * (measured 2026-09-12: "Interrupt alloc error", no free level-1 slot on that
 * core - WiFi, BT, the display's DMA and the rest live there), while TinyUSB
 * came up fine because it installs from its own task, pinned to core 1. So
 * the library task is pinned to core 1 and does the install itself, first
 * with level 1 and then with any low/medium level. */
static void host_lib_task(void *arg)
{
    usb_host_config_t cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    s_install_err = usb_host_install(&cfg);
    if (s_install_err == ESP_ERR_NOT_FOUND) {
        cfg.intr_flags = ESP_INTR_FLAG_LOWMED;
        s_install_err = usb_host_install(&cfg);
    }
    if (s_install_err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s (interrupts below, on the console)",
                 esp_err_to_name(s_install_err));
        esp_intr_dump(stdout);
        xSemaphoreGive(s_installed);
        vTaskDelete(NULL);
        return;
    }
    xSemaphoreGive(s_installed);

    bool clients = true, wait_free = false;
    while (clients) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            if (usb_host_device_free_all() == ESP_OK) {
                clients = false;
            } else {
                wait_free = true;
            }
        }
        if (wait_free && (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            clients = false;
        }
    }
    xSemaphoreGive(s_task_done);
    vTaskDelete(NULL);
}

static bool host_start(void)
{
    memset(s_devs, 0, sizeof(s_devs));
    s_seen = 0;
    s_client_quit = false;
    s_pending_new_n = 0;
    s_pending_gone_n = 0;
    /* Drain the semaphores, in case a previous stop timed out. */
    while (xSemaphoreTake(s_task_done, 0) == pdTRUE) { }
    while (xSemaphoreTake(s_installed, 0) == pdTRUE) { }

    if (xTaskCreatePinnedToCore(host_lib_task, "usb_lib", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "could not create the host library task");
        return false;
    }
    if (xSemaphoreTake(s_installed, pdMS_TO_TICKS(3000)) != pdTRUE || s_install_err != ESP_OK) {
        return false;      /* the task logged it and is gone */
    }
    if (xTaskCreatePinnedToCore(host_client_task, "usb_cli", 5120, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "could not create the host client task");
        /* The library task exits on NO_CLIENTS once nobody registers; wait
         * for it, then uninstall. */
        xSemaphoreTake(s_task_done, pdMS_TO_TICKS(2000));
        usb_host_uninstall();
        return false;
    }
    return true;
}

static void host_stop(void)
{
    s_client_quit = true;
    if (s_client) {
        usb_host_client_unblock(s_client);
    }
    /* Client first (it deregisters, which is the NO_CLIENTS the library task
     * waits for), then the library task. 2 s each; never hang the caller. */
    for (int i = 0; i < 2; i++) {
        if (xSemaphoreTake(s_task_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGW(TAG, "a host task did not finish in time");
        }
    }
    esp_err_t e = usb_host_uninstall();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_uninstall: %s", esp_err_to_name(e));
    }
}

/* -------------------------------------------------------------------------- */
/* The switch                                                                  */
/* -------------------------------------------------------------------------- */

aos_usb_mode_t aos_usb_mode_get(void)
{
    return s_mode;
}

const char *aos_usb_mode_name(aos_usb_mode_t mode)
{
    switch (mode) {
    case AOS_USB_CONSOLE: return "console";
    case AOS_USB_DEVICE:  return "device";
    case AOS_USB_HOST:    return "host";
    default:              return "?";
    }
}

void aos_usb_console_on_cdc(bool on)
{
    s_console_on_cdc = on;
}

bool aos_usb_mode_set(aos_usb_mode_t mode)
{
    if (!ensure_init()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (mode == s_mode) {
        xSemaphoreGive(s_lock);
        return true;
    }
    mem_line("before the switch");

    /* Leave the current mode. The table lock is released around the host
     * teardown because its tasks take it to close their devices. */
    if (s_mode == AOS_USB_DEVICE) {
        device_stop();
    } else if (s_mode == AOS_USB_HOST) {
        xSemaphoreGive(s_lock);
        host_stop();
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    if (s_mode != AOS_USB_CONSOLE) {
        phy_back_to_console();
        if (s_pm_lock) esp_pm_lock_release(s_pm_lock);
        s_mode = AOS_USB_CONSOLE;
        mem_line("back on the console");
    }

    bool ok = true;
    if (mode != AOS_USB_CONSOLE) {
        if (s_pm_lock) esp_pm_lock_acquire(s_pm_lock);
        ok = (mode == AOS_USB_DEVICE) ? device_start() : host_start();
        if (ok) {
            s_mode = mode;
        } else {
            phy_back_to_console();
            if (s_pm_lock) esp_pm_lock_release(s_pm_lock);
        }
        mem_line(ok ? aos_usb_mode_name(mode) : "mode failed, console");
    }
    ESP_LOGI(TAG, "usb: %s", aos_usb_mode_name(s_mode));
    xSemaphoreGive(s_lock);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* /api/usb                                                                    */
/* -------------------------------------------------------------------------- */

int aos_usb_status_json(char *out, size_t len)
{
    ensure_init();
    int n = snprintf(out, len, "{\"mode\":\"%s\",\"console_on_cdc\":%s,\"seen\":%d,\"devices\":[",
                     aos_usb_mode_name(s_mode), s_console_on_cdc ? "true" : "false", s_seen);
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
    bool first = true;
    for (int i = 0; i < AOS_USB_MAX_DEV && n < (int)len; i++) {
        const aos_usb_dev_t *d = &s_devs[i];
        if (!d->used) {
            continue;
        }
        n += snprintf(out + n, len - n, "%s{\"addr\":%d,\"speed\":\"%s\",\"vid\":\"%04x\",\"pid\":\"%04x\","
                      "\"class\":%d,\"manufacturer\":\"%s\",\"product\":\"%s\",\"serial\":\"%s\",\"itf\":[",
                      first ? "" : ",", d->addr,
                      (const char *[]) { "low", "full", "high" }[d->speed < 3 ? d->speed : 1],
                      d->vid, d->pid, d->dev_class, d->manufacturer, d->product, d->serial);
        for (int j = 0; j < d->n_itf && n < (int)len; j++) {
            n += snprintf(out + n, len - n, "%s{\"class\":%d,\"name\":\"%s\",\"sub\":%d,\"proto\":%d}",
                          j ? "," : "", d->itf_class[j], class_name(d->itf_class[j]),
                          d->itf_subclass[j], d->itf_protocol[j]);
        }
        if (n < (int)len) n += snprintf(out + n, len - n, "]}");
        first = false;
    }
    if (s_lock) xSemaphoreGive(s_lock);
    if (n < (int)len) {
        n += snprintf(out + n, len - n, "],\"mem\":{\"internal\":%u,\"exec\":%u,\"exec_block\":%u}}",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));
    }
    return n;
}
