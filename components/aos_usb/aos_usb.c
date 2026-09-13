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
#include "esp_rom_sys.h"
#include "esp_attr.h"
#include "esp_rom_uart.h"
#include "soc/usb_dwc_struct.h"
#include "soc/usb_wrap_struct.h"
#include "soc/rtc_cntl_struct.h"
#include "hal/usb_serial_jtag_ll.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "esp_vfs_fat.h"
#include "tinyusb_msc.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "class/net/net_device.h"
#include "aos_usb_net.h"
#include "esp_mac.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "bsp/esp32_s3_touch_amoled_1_8.h"   /* the SD pins and the mount point */
#include "aos_hal.h"
#include <errno.h>
#include <sys/stat.h>
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

/* D2, disk mode: the card as a USB drive. The BSP's mount is undone, the
 * card is initialised again here and handed to esp_tinyusb's MSC storage,
 * which owns it from then on: on the USB side while the computer has it,
 * mounted back at /sdcard for the watch when the computer ejects it (that
 * is the driver's auto-mount, and the event callback tells the HAL). */
static sdmmc_card_t               *s_card;
static tinyusb_msc_storage_handle_t s_storage;
static bool                        s_card_away;

/* H1: one pendrive at a time, mounted at /usb. The MSC class driver runs
 * its own task; its callback only takes note, and the client task does the
 * install and the mount (control transfers from inside the callback would
 * wait on the task that is running the callback). */
#define AOS_USB_MSC_ROOT "/usb"
static bool                     s_msc_installed;
static msc_host_device_handle_t s_msc_dev;
static msc_host_vfs_handle_t    s_msc_vfs;
static bool                     s_msc_mounted;
static uint32_t                 s_msc_sectors, s_msc_sector_size;
static volatile uint8_t         s_msc_pending_addr;
static volatile bool            s_msc_pending_gone;

/* -------------------------------------------------------------------------- */
/* Boot, and TinyUSB's own log                                                 */
/* -------------------------------------------------------------------------- */

/* Breadcrumbs across a reset: the teardown of an OTG mode has crashed in
 * ways that leave no core dump and no console, so every step writes its
 * number to RTC memory, which survives everything but a power cycle, and
 * /api/usb reports the last one seen at boot ("boot_step"). */
#define STEP_MAGIC 0x55534253u
static RTC_NOINIT_ATTR uint32_t s_step_magic;
static RTC_NOINIT_ATTR uint32_t s_step;
static uint32_t s_boot_step;                /* what the previous life left, 0 if nothing */
#define STEP(n) do { s_step_magic = STEP_MAGIC; s_step = (n); } while (0)

#define ROM_RING 8192
static char             *s_rom_ring;        /* PSRAM; written from the TinyUSB task and its ISR */
static volatile uint32_t s_rom_w;

static aos_usb_mode_t       s_mode = AOS_USB_CONSOLE;

static void rom_putc(char c)
{
    if (s_mode == AOS_USB_CONSOLE) {
        esp_rom_output_putc(c);             /* the console keeps everything it used to get */
    }                                       /* in an OTG mode the Serial-JTAG is not there: no waiting on its FIFO */
    if (s_rom_ring) {
        s_rom_ring[s_rom_w % ROM_RING] = c;
        s_rom_w++;
    }
}

void aos_usb_init(void)
{
    s_boot_step = (s_step_magic == STEP_MAGIC) ? s_step : 0;
    STEP(0);
    /* Pad down for a moment, as in phy_back_to_console(): after a reboot
     * from an OTG mode the computer may still hold the OTG device's address
     * (a reset is too short a detach for some hosts), and then the
     * Serial-JTAG never enumerates until the cable is pulled. */
    usb_serial_jtag_ll_phy_enable_pad(false);
    usb_serial_jtag_ll_phy_enable_external(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    usb_serial_jtag_ll_phy_enable_pad(true);
    /* TinyUSB logs through esp_rom_printf, from its ISR included. Level 1
     * (errors and asserts) only: at level 2 it prints on every transfer,
     * from the ISR, and disk mode died of the interrupt watchdog while the
     * Mac read the card (2026-09-12). */
    s_rom_ring = heap_caps_calloc(ROM_RING, 1, MALLOC_CAP_SPIRAM);
    esp_rom_install_channel_putc(1, rom_putc);
}

int aos_usb_tusb_log(char *out, size_t len)
{
    if (!s_rom_ring || len == 0) {
        if (len) out[0] = 0;
        return 0;
    }
    uint32_t w = s_rom_w;
    uint32_t have = w < ROM_RING ? w : ROM_RING;
    uint32_t start = w - have;
    size_t n = 0;
    for (uint32_t i = 0; i < have && n + 1 < len; i++) {
        out[n++] = s_rom_ring[(start + i) % ROM_RING];
    }
    out[n] = 0;
    return (int)n;
}

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

/* Device mode gets its own configuration: CDC + HID (a keyboard and a
 * consumer-control device, D3). esp_tinyusb's default descriptor lists
 * every class that is compiled in, MSC included, and with no MSC driver
 * installed the class callbacks dereference a NULL driver (tinyusb_msc.c,
 * _msc_storage_get_by_lun) the moment the computer asks TEST UNIT READY: a
 * panic seconds after enumeration, measured 2026-09-12. And it has no
 * default for HID at all. Disk mode installs the MSC driver and keeps the
 * default CDC+MSC descriptor. */
/* The S3's OTG has FIVE IN endpoints counting EP0 (dwc2_esp32.h, ep_in_count):
 * four for the classes. CDC takes two (notification + data), NCM two, HID
 * one, so the three do not fit together - measured 2026-09-13: TinyUSB
 * asserts opening the NCM, and on the Mac's retry the half-opened CDC
 * asserts too. Keys mode is HID + NCM: the keyboard and the network, and the
 * log and the whole portal reach the computer over the cable at
 * 192.168.7.1, which is what the serial port was for. The CDC port stays in
 * disk mode, beside the MSC. */
enum { AOS_ITF_HID = 0, AOS_ITF_NET, AOS_ITF_NET_DATA, AOS_ITF_DEVICE_TOTAL };
enum { AOS_ITF_DISK_CDC = 0, AOS_ITF_DISK_CDC_DATA, AOS_ITF_DISK_MSC, AOS_ITF_DISK_TOTAL };
#define AOS_EP_HID_IN      0x81
#define AOS_EP_NET_NOTIF   0x82
#define AOS_EP_NET_OUT     0x03
#define AOS_EP_NET_IN      0x83
#define AOS_EP_CDC_NOTIF   0x81
#define AOS_EP_CDC_OUT     0x02
#define AOS_EP_CDC_IN      0x82
#define AOS_EP_MSC_OUT     0x03
#define AOS_EP_MSC_IN      0x83
/* esp_tinyusb takes at most 8 string descriptors (USB_STRING_DESCRIPTOR_ARRAY_SIZE):
 * the MSC interface borrows the product's. */
enum { AOS_STR_LANG = 0, AOS_STR_MANUFACTURER, AOS_STR_PRODUCT, AOS_STR_SERIAL, AOS_STR_CDC, AOS_STR_HID,
       AOS_STR_NET, AOS_STR_MAC, AOS_STR_COUNT, AOS_STR_MSC = AOS_STR_PRODUCT };
enum { AOS_HID_REPORT_KEYBOARD = 1, AOS_HID_REPORT_CONSUMER = 2 };

static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(AOS_HID_REPORT_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(AOS_HID_REPORT_CONSUMER)),
};
/* Keys mode: HID (D3) + NCM (D6). Three interfaces, three IN endpoints. */
static const uint8_t s_cfg_device[] = {
    TUD_CONFIG_DESCRIPTOR(1, AOS_ITF_DEVICE_TOTAL, 0,
                          TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_NCM_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(AOS_ITF_HID, AOS_STR_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc),
                       AOS_EP_HID_IN, 16, 10),
    TUD_CDC_NCM_DESCRIPTOR(AOS_ITF_NET, AOS_STR_NET, AOS_STR_MAC, AOS_EP_NET_NOTIF, 64,
                           AOS_EP_NET_OUT, AOS_EP_NET_IN, 64, CFG_TUD_NET_MTU),
};
/* Disk mode: CDC + MSC, and nothing else. With HID and NCM compiled in,
 * esp_tinyusb's default descriptor would list them too, uninitialised. */
static const uint8_t s_cfg_disk[] = {
    TUD_CONFIG_DESCRIPTOR(1, AOS_ITF_DISK_TOTAL, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(AOS_ITF_DISK_CDC, AOS_STR_CDC, AOS_EP_CDC_NOTIF, 8, AOS_EP_CDC_OUT, AOS_EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(AOS_ITF_DISK_MSC, AOS_STR_MSC, AOS_EP_MSC_OUT, AOS_EP_MSC_IN, 64),
};
static const tusb_desc_device_t s_dev_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,        /* IAD: the NCM (and the CDC of disk mode) are associations */
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,                     /* Espressif */
    .idProduct = 0x4024,                    /* esp_tinyusb's PID map: HID (0x04), plus a bit of our own (0x20) for NCM */
    .bcdDevice = CONFIG_TINYUSB_DESC_BCD_DEVICE,
    .iManufacturer = AOS_STR_MANUFACTURER, .iProduct = AOS_STR_PRODUCT, .iSerialNumber = AOS_STR_SERIAL,
    .bNumConfigurations = 1,
};
static char s_serial[13];                   /* the chip's MAC, so two watches are two devices */
static char s_net_mac[13];                  /* the NCM interface's MAC, as the descriptor wants it */
static tusb_desc_device_t s_dev_disk;       /* s_dev_device with the disk PID, filled at first use */
static const char *s_strings[AOS_STR_COUNT] = {
    (const char[]) { 0x09, 0x04 },          /* English (US) */
    "AmoledOS",
    "AmoledOS watch",
    s_serial,
    "AmoledOS console",
    "AmoledOS keys",
    "AmoledOS network",
    s_net_mac,
};

/* TinyUSB's HID class asks for these. */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    /* Keyboard LEDs (caps lock and friends): nothing to light. */
}

bool aos_usb_hid_ready(void)
{
    return s_mode == AOS_USB_DEVICE && tud_mounted() && tud_hid_ready();
}

static bool hid_wait_ready(void)
{
    for (int i = 0; i < 20 && !tud_hid_ready(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return tud_hid_ready();
}

bool aos_usb_hid_key(uint8_t modifier, uint8_t keycode, int hold_ms)
{
    if (!aos_usb_hid_ready() || !hid_wait_ready()) {
        return false;
    }
    uint8_t keys[6] = { keycode, 0, 0, 0, 0, 0 };
    if (!tud_hid_keyboard_report(AOS_HID_REPORT_KEYBOARD, modifier, keys)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(hold_ms > 0 ? hold_ms : 20));
    hid_wait_ready();
    tud_hid_keyboard_report(AOS_HID_REPORT_KEYBOARD, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool aos_usb_hid_consumer(uint16_t usage, int hold_ms)
{
    if (!aos_usb_hid_ready() || !hid_wait_ready()) {
        return false;
    }
    if (!tud_hid_report(AOS_HID_REPORT_CONSUMER, &usage, sizeof(usage))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(hold_ms > 0 ? hold_ms : 20));
    hid_wait_ready();
    uint16_t none = 0;
    tud_hid_report(AOS_HID_REPORT_CONSUMER, &none, sizeof(none));
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

/* Names the apps and the portal use. "cmd+", "ctrl+", "alt+", "shift+"
 * prefixes stack on a key: "cmd+tab", "ctrl+shift+t". */
typedef struct { const char *name; uint16_t code; bool consumer; } aos_hid_name_t;
static const aos_hid_name_t s_hid_names[] = {
    { "play",    HID_USAGE_CONSUMER_PLAY_PAUSE,        true },
    { "pause",   HID_USAGE_CONSUMER_PLAY_PAUSE,        true },
    { "next",    HID_USAGE_CONSUMER_SCAN_NEXT_TRACK,         true },
    { "prev",    HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK,     true },
    { "stop",    HID_USAGE_CONSUMER_STOP,              true },
    { "mute",    HID_USAGE_CONSUMER_MUTE,              true },
    { "volup",   HID_USAGE_CONSUMER_VOLUME_INCREMENT,  true },
    { "voldown", HID_USAGE_CONSUMER_VOLUME_DECREMENT,  true },
    { "brightup",   HID_USAGE_CONSUMER_BRIGHTNESS_INCREMENT, true },
    { "brightdown", HID_USAGE_CONSUMER_BRIGHTNESS_DECREMENT, true },
    { "pgup",    HID_KEY_PAGE_UP,     false },
    { "pgdn",    HID_KEY_PAGE_DOWN,   false },
    { "up",      HID_KEY_ARROW_UP,    false },
    { "down",    HID_KEY_ARROW_DOWN,  false },
    { "left",    HID_KEY_ARROW_LEFT,  false },
    { "right",   HID_KEY_ARROW_RIGHT, false },
    { "enter",   HID_KEY_ENTER,       false },
    { "esc",     HID_KEY_ESCAPE,      false },
    { "space",   HID_KEY_SPACE,       false },
    { "tab",     HID_KEY_TAB,         false },
    { "home",    HID_KEY_HOME,        false },
    { "end",     HID_KEY_END,         false },
    { "b",       HID_KEY_B,           false },     /* blank screen, Keynote and PowerPoint */
    { "f5",      HID_KEY_F5,          false },
    { "delete",  HID_KEY_BACKSPACE,   false },
};

bool aos_usb_hid_named(const char *name)
{
    uint8_t mod = 0;
    for (;;) {
        if      (!strncmp(name, "cmd+",   4)) { mod |= KEYBOARD_MODIFIER_LEFTGUI;   name += 4; }
        else if (!strncmp(name, "ctrl+",  5)) { mod |= KEYBOARD_MODIFIER_LEFTCTRL;  name += 5; }
        else if (!strncmp(name, "alt+",   4)) { mod |= KEYBOARD_MODIFIER_LEFTALT;   name += 4; }
        else if (!strncmp(name, "shift+", 6)) { mod |= KEYBOARD_MODIFIER_LEFTSHIFT; name += 6; }
        else break;
    }
    for (size_t i = 0; i < sizeof(s_hid_names) / sizeof(s_hid_names[0]); i++) {
        if (!strcmp(name, s_hid_names[i].name)) {
            return s_hid_names[i].consumer ? aos_usb_hid_consumer(s_hid_names[i].code, 0)
                                           : aos_usb_hid_key(mod, (uint8_t)s_hid_names[i].code, 0);
        }
    }
    if (name[0] && !name[1]) {              /* a single character: type it */
        return aos_usb_hid_type(name) == 1;
    }
    return false;
}

int aos_usb_hid_type(const char *ascii)
{
    static const uint8_t table[128][2] = { HID_ASCII_TO_KEYCODE };
    int sent = 0;
    for (; *ascii; ascii++) {
        unsigned char c = (unsigned char)*ascii;
        if (c >= 128) continue;
        uint8_t mod = table[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
        uint8_t key = table[c][1];
        if (!key) continue;
        if (!aos_usb_hid_key(mod, key, 0)) break;
        sent++;
    }
    return sent;
}

static bool s_device_with_msc;              /* set by disk_start() before device_start() */
static bool s_cdc_up;                       /* the CDC port exists (disk mode only) */

static bool device_start(void)
{
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    if (!s_serial[0]) {
        uint8_t mac[6] = { 0 };
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        aos_usb_net_mac(mac);
        snprintf(s_net_mac, sizeof(s_net_mac), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        s_dev_disk = s_dev_device;
        s_dev_disk.idProduct = 0x4003;      /* esp_tinyusb's PID for CDC + MSC */
    }
    cfg.descriptor.device = s_device_with_msc ? &s_dev_disk : &s_dev_device;
    cfg.descriptor.full_speed_config = s_device_with_msc ? s_cfg_disk : s_cfg_device;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = AOS_STR_COUNT;
    esp_err_t e = tinyusb_driver_install(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(e));
        return false;
    }
    if (s_device_with_msc) {
        tinyusb_config_cdcacm_t acm = { .cdc_port = TINYUSB_CDC_ACM_0 };
        e = tinyusb_cdcacm_init(&acm);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(e));
            tinyusb_driver_uninstall();
            return false;
        }
        s_cdc_up = true;
        if (s_console_on_cdc) {
            /* Non-blocking VFS (vfs_tinyusb.c): with nobody reading the port the
             * output is dropped, never stalls the tasks that log. */
            s_console_redirected = esp_vfs_tusb_cdc_register(TINYUSB_CDC_ACM_0, NULL) == ESP_OK &&
                                   stream_to(VFS_TUSB_PATH_DEFAULT);
        }
    }
    if (s_device_with_msc) {
        ESP_LOGI(TAG, "disk mode: CDC up, console %s",
                 s_console_redirected ? "on the CDC port" : "stays on the Serial-JTAG (now silent)");
    } else if (aos_usb_net_start()) {
        ESP_LOGI(TAG, "keys mode: keyboard and network up; the log is at http://192.168.7.1/registro");
    } else {
        ESP_LOGW(TAG, "keys mode: the USB network did not come up; keyboard only");
    }
    return true;
}

static void device_stop(void)
{
    STEP(20);
    aos_usb_net_stop();
    if (s_console_redirected) {
        stream_to(ESP_VFS_DEV_CONSOLE);
        STEP(21);
        esp_vfs_tusb_cdc_unregister(NULL);
        s_console_redirected = false;
    }
    STEP(22);
    if (s_cdc_up) {
        tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
        s_cdc_up = false;
    }
    STEP(23);
    esp_err_t e = tinyusb_driver_uninstall();
    STEP(24);
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

/* ---- H1: the pendrive ---------------------------------------------------- */

static void msc_event_cb(const msc_host_event_t *ev, void *arg)
{
    if (ev->event == MSC_DEVICE_CONNECTED) {
        s_msc_pending_addr = ev->device.address;
    } else if (ev->event == MSC_DEVICE_DISCONNECTED) {
        s_msc_pending_gone = true;
    }
}

static void msc_unmount(void)
{
    if (s_msc_vfs) {
        msc_host_vfs_unregister(s_msc_vfs);
        s_msc_vfs = NULL;
    }
    if (s_msc_dev) {
        msc_host_uninstall_device(s_msc_dev);
        s_msc_dev = NULL;
    }
    if (s_msc_mounted) {
        ESP_LOGI(TAG, "pendrive unmounted from " AOS_USB_MSC_ROOT);
    }
    s_msc_mounted = false;
}

static void msc_mount(uint8_t addr)
{
    if (s_msc_dev) {
        ESP_LOGW(TAG, "a second MSC device (address %d): one at a time, ignored", addr);
        return;
    }
    esp_err_t e = msc_host_install_device(addr, &s_msc_dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "msc_host_install_device: %s", esp_err_to_name(e));
        s_msc_dev = NULL;
        return;
    }
    msc_host_device_info_t info;
    if (msc_host_get_device_info(s_msc_dev, &info) == ESP_OK) {
        s_msc_sectors = info.sector_count;
        s_msc_sector_size = info.sector_size;
        ESP_LOGI(TAG, "pendrive: %lu sectors of %lu B = %lu MB",
                 (unsigned long)info.sector_count, (unsigned long)info.sector_size,
                 (unsigned long)((uint64_t)info.sector_count * info.sector_size >> 20));
    }
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    e = msc_host_vfs_register(s_msc_dev, AOS_USB_MSC_ROOT, &cfg, &s_msc_vfs);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "msc_host_vfs_register: %s (not FAT?)", esp_err_to_name(e));
        s_msc_vfs = NULL;
        msc_host_uninstall_device(s_msc_dev);
        s_msc_dev = NULL;
        return;
    }
    s_msc_mounted = true;
    uint64_t total = 0, free_b = 0;
    esp_vfs_fat_info(AOS_USB_MSC_ROOT, &total, &free_b);
    ESP_LOGI(TAG, "pendrive mounted at " AOS_USB_MSC_ROOT ": %llu MB, %llu MB free",
             (unsigned long long)(total >> 20), (unsigned long long)(free_b >> 20));
}

bool aos_usb_msc_mounted(void)
{
    return s_msc_mounted;
}

const char *aos_usb_msc_root(void)
{
    return s_msc_mounted ? AOS_USB_MSC_ROOT : NULL;
}

long aos_usb_copy(const char *src, const char *dst)
{
    const size_t chunk = 32 * 1024;
    uint8_t *buf = heap_caps_malloc(chunk, MALLOC_CAP_SPIRAM);
    if (!buf) {
        errno = ENOMEM;
        return -1;
    }
    FILE *in = fopen(src, "rb");
    if (!in) {
        free(buf);
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        int err = errno;
        fclose(in);
        free(buf);
        errno = err;
        return -1;
    }
    long total = 0;
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, chunk, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
        total += (long)n;
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    free(buf);
    if (!ok) {
        unlink(dst);
        return -1;
    }
    return total;
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
        if (s_msc_pending_gone) {
            s_msc_pending_gone = false;
            msc_unmount();
        }
        if (s_msc_pending_addr) {
            uint8_t a = s_msc_pending_addr;
            s_msc_pending_addr = 0;
            msc_mount(a);
        }
    }
    msc_unmount();
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
    s_msc_pending_addr = 0;
    s_msc_pending_gone = false;
    const msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 1,
        .callback = msc_event_cb,
    };
    esp_err_t e = msc_host_install(&msc_cfg);
    s_msc_installed = (e == ESP_OK);
    if (!s_msc_installed) {
        ESP_LOGW(TAG, "msc_host_install: %s (host mode without pendrives)", esp_err_to_name(e));
    }
    return true;
}

static void host_stop(void)
{
    s_client_quit = true;
    if (s_client) {
        usb_host_client_unblock(s_client);
    }
    /* The client task unmounts the pendrive and deregisters; then the MSC
     * driver's own client goes; NO_CLIENTS is what the library task waits
     * for. 2 s each; never hang the caller. */
    if (xSemaphoreTake(s_task_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "the client task did not finish in time");
    }
    if (s_msc_installed) {
        esp_err_t e = msc_host_uninstall();
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "msc_host_uninstall: %s", esp_err_to_name(e));
        }
        s_msc_installed = false;
    }
    if (xSemaphoreTake(s_task_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGW(TAG, "the library task did not finish in time");
    }
    esp_err_t e = usb_host_uninstall();
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_uninstall: %s", esp_err_to_name(e));
    }
}

/* -------------------------------------------------------------------------- */
/* Disk mode: the microSD as a USB drive                                       */
/* -------------------------------------------------------------------------- */

static void msc_storage_cb(tinyusb_msc_storage_handle_t h, tinyusb_msc_event_t *ev, void *arg)
{
    /* From the TinyUSB task. */
    switch (ev->id) {
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE: {
        bool on_watch = ev->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP;
        s_card_away = !on_watch;
        aos_hal_sd_mark_mounted(on_watch);
        ESP_LOGI(TAG, "%s", on_watch ? "card back on the watch (the computer ejected it)"
                                     : "card handed to the computer");
        break;
    }
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
        ESP_LOGW(TAG, "card mount/unmount failed (%s side)",
                 ev->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP ? "watch" : "USB");
        break;
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGE(TAG, "the card has no filesystem the watch can mount; it is NOT formatted here");
        break;
    default:
        break;
    }
}

/* The BSP's slot, pin for pin (bsp_sdcard_mount). */
static bool sd_take(void)
{
    if (!aos_hal_sd_release()) {
        ESP_LOGW(TAG, "no card to hand over");
        return false;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    const sdmmc_slot_config_t slot = {
        .clk = BSP_SD_CLK, .cmd = BSP_SD_CMD, .d0 = BSP_SD_D0,
        .d1 = GPIO_NUM_NC, .d2 = GPIO_NUM_NC, .d3 = GPIO_NUM_NC, .d4 = GPIO_NUM_NC,
        .d5 = GPIO_NUM_NC, .d6 = GPIO_NUM_NC, .d7 = GPIO_NUM_NC,
        .cd = SDMMC_SLOT_NO_CD, .wp = SDMMC_SLOT_NO_WP, .width = 1, .flags = 0,
    };
    esp_err_t e = sdmmc_host_init();
    if (e == ESP_OK) e = sdmmc_host_init_slot(host.slot, &slot);
    if (e == ESP_OK) {
        s_card = calloc(1, sizeof(sdmmc_card_t));
        e = s_card ? sdmmc_card_init(&host, s_card) : ESP_ERR_NO_MEM;
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "card init for disk mode: %s", esp_err_to_name(e));
        free(s_card);
        s_card = NULL;
        sdmmc_host_deinit();
        aos_hal_sd_reclaim();
        return false;
    }
    ESP_LOGI(TAG, "card taken: %s, %llu MB", s_card->cid.name,
             (unsigned long long)((uint64_t)s_card->csd.capacity * s_card->csd.sector_size >> 20));
    return true;
}

static void sd_give_back(void)
{
    sdmmc_host_deinit();
    free(s_card);
    s_card = NULL;
    aos_hal_sd_reclaim();
}

static bool disk_start(void)
{
    if (!sd_take()) {
        return false;
    }
    const tinyusb_msc_driver_config_t dcfg = { .callback = msc_storage_cb };   /* auto-mount on */
    esp_err_t e = tinyusb_msc_install_driver(&dcfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_install_driver: %s", esp_err_to_name(e));
        sd_give_back();
        return false;
    }
    const tinyusb_msc_storage_config_t scfg = {
        .medium.card = s_card,
        .fat_fs = {
            .base_path = BSP_SD_MOUNT_POINT,
            .config = { .format_if_mount_failed = false, .max_files = 8, .allocation_unit_size = 16 * 1024 },
            .do_not_format = true,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    e = tinyusb_msc_new_storage_sdmmc(&scfg, &s_storage);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_sdmmc: %s", esp_err_to_name(e));
        s_storage = NULL;
        tinyusb_msc_uninstall_driver();
        sd_give_back();
        return false;
    }
    s_card_away = true;
    s_device_with_msc = true;
    if (!device_start()) {
        s_device_with_msc = false;
        tinyusb_msc_delete_storage(s_storage);
        s_storage = NULL;
        tinyusb_msc_uninstall_driver();
        s_card_away = false;
        sd_give_back();
        return false;
    }
    ESP_LOGI(TAG, "disk mode: the card is the computer's until it ejects it");
    return true;
}

static void disk_stop(void)
{
    STEP(30);
    device_stop();
    s_device_with_msc = false;
    STEP(31);
    if (s_storage) {
        esp_err_t e = tinyusb_msc_delete_storage(s_storage);     /* unmounts /sdcard if it was back */
        if (e != ESP_OK) ESP_LOGW(TAG, "tinyusb_msc_delete_storage: %s", esp_err_to_name(e));
        s_storage = NULL;
    }
    STEP(32);
    tinyusb_msc_uninstall_driver();
    s_card_away = false;
    aos_hal_sd_mark_mounted(false);
    STEP(33);
    sd_give_back();
    STEP(34);
}

bool aos_usb_disk_card_away(void)
{
    return s_card_away;
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
    case AOS_USB_DISK:    return "disk";
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

    /* Leave the current mode: teardown first, the PHY back afterwards.
     * Measured both ways (2026-09-12): with the mux flipped before the
     * uninstall the computer never sees a detach (the OTG side's pull-up
     * override is still in force through the pad toggle) and keeps the old
     * device's node until the cable is pulled; with the uninstall first the
     * overrides are gone, the pad toggle is a real detach, and the
     * Serial-JTAG is back on the computer 0.6 s later. The table lock is
     * released around the host teardown because its tasks take it to close
     * their devices. */
    STEP(10);
    if (s_mode == AOS_USB_DEVICE) {
        device_stop();
    } else if (s_mode == AOS_USB_DISK) {
        disk_stop();
    } else if (s_mode == AOS_USB_HOST) {
        xSemaphoreGive(s_lock);
        host_stop();
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    STEP(11);
    if (s_mode != AOS_USB_CONSOLE) {
        phy_back_to_console();
        if (s_pm_lock) esp_pm_lock_release(s_pm_lock);
        s_mode = AOS_USB_CONSOLE;
        mem_line("back on the console");
    }

    bool ok = true;
    if (mode != AOS_USB_CONSOLE) {
        if (s_pm_lock) esp_pm_lock_acquire(s_pm_lock);
        ok = (mode == AOS_USB_DEVICE) ? device_start()
           : (mode == AOS_USB_DISK)   ? disk_start()
           :                            host_start();
        if (ok) {
            s_mode = mode;
        } else {
            phy_back_to_console();
            if (s_pm_lock) esp_pm_lock_release(s_pm_lock);
        }
        mem_line(ok ? aos_usb_mode_name(mode) : "mode failed, console");
    }
    ESP_LOGI(TAG, "usb: %s", aos_usb_mode_name(s_mode));
    STEP(40);
    xSemaphoreGive(s_lock);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* /api/usb                                                                    */
/* -------------------------------------------------------------------------- */

int aos_usb_status_json(char *out, size_t len)
{
    ensure_init();
    int n = snprintf(out, len, "{\"mode\":\"%s\",\"console_on_cdc\":%s,\"card_away\":%s,\"net_up\":%s,\"net_ip\":\"%s\",\"hid_ready\":%s,"
                     "\"boot_step\":%lu,\"seen\":%d,\"devices\":[",
                     aos_usb_mode_name(s_mode), s_console_on_cdc ? "true" : "false",
                     s_card_away ? "true" : "false", aos_usb_net_up() ? "true" : "false",
                     aos_usb_net_up() ? "192.168.7.1" : "", aos_usb_hid_ready() ? "true" : "false",
                     (unsigned long)s_boot_step, s_seen);
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
        /* Raw registers, for the days a device is plugged and nothing
         * happens: HPRT bit 0 = connected, 1 = connect detected, 2 = port
         * enabled, 12 = port powered, 17-18 = speed; GOTGCTL bit 16 = ID
         * (0 host), 18/19 = A/B session valid; RTC usb_conf bit 19 =
         * sw_usb_phy_sel (1 = the PHY is on the OTG), 20 = sw override on.
         *
         * ONLY while an OTG mode is on. Leaving a mode gates the OTG
         * peripheral's clock, and on the S3 a read of a clock-gated
         * peripheral hangs the bus: the CPU stalls with interrupts dead and
         * the interrupt watchdog resets the chip, with no panic and no core
         * dump. Every "crash on leaving a mode" of 2026-09-12 was this read,
         * one HTTP response after the switch had already finished. */
        usb_host_lib_info_t li = { 0 };
        if (s_mode == AOS_USB_HOST) usb_host_lib_info(&li);
        uint64_t total = 0, free_b = 0;
        if (s_msc_mounted) esp_vfs_fat_info(AOS_USB_MSC_ROOT, &total, &free_b);
        n += snprintf(out + n, len - n, "],\"msc\":{\"mounted\":%s,\"root\":\"%s\",\"sectors\":%lu,"
                      "\"sector_size\":%lu,\"kb_total\":%llu,\"kb_free\":%llu},",
                      s_msc_mounted ? "true" : "false", s_msc_mounted ? AOS_USB_MSC_ROOT : "",
                      (unsigned long)s_msc_sectors, (unsigned long)s_msc_sector_size,
                      (unsigned long long)(total >> 10), (unsigned long long)(free_b >> 10));
        bool otg = s_mode != AOS_USB_CONSOLE;
        n += snprintf(out + n, len - n, "\"regs\":{\"hprt\":\"0x%08x\",\"gotgctl\":\"0x%08x\","
                      "\"gusbcfg\":\"0x%08x\",\"gintsts\":\"0x%08x\",\"wrap_otg_conf\":\"0x%08x\","
                      "\"rtc_usb_conf\":\"0x%08x\",\"lib_devices\":%d,\"lib_clients\":%d},",
                      otg ? (unsigned)USB_DWC.hprt_reg.val : 0, otg ? (unsigned)USB_DWC.gotgctl_reg.val : 0,
                      otg ? (unsigned)USB_DWC.gusbcfg_reg.val : 0, otg ? (unsigned)USB_DWC.gintsts_reg.val : 0,
                      otg ? (unsigned)USB_WRAP.otg_conf.val : 0, (unsigned)RTCCNTL.usb_conf.val,
                      li.num_devices, li.num_clients);
    }
    if (n < (int)len) {
        n += snprintf(out + n, len - n, "\"mem\":{\"internal\":%u,\"exec\":%u,\"exec_block\":%u}}",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC));
    }
    return n;
}
