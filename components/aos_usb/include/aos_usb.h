/* aos_usb: who owns the USB port.
 *
 * The ESP32-S3 has one USB PHY shared by two controllers: the USB-Serial-JTAG
 * (the console and flashing port, what the connector is at boot) and the OTG
 * controller (a device for a computer, or a host for a peripheral). This
 * module holds that mux. Three states, one at a time, and every reset comes
 * back as CONSOLE. See docs/USB.md.
 *
 * Nothing here is called at boot: the modes are opted into, from the portal
 * (/api/usb?mode=...) and later from Settings. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    AOS_USB_CONSOLE = 0,    /* USB-Serial-JTAG: log, idf.py monitor, esptool */
    AOS_USB_DEVICE,         /* OTG as a peripheral: a CDC serial port with the console on it */
    AOS_USB_HOST,           /* OTG as a host: the inspector, and a pendrive mounted at /usb */
    AOS_USB_DISK,           /* DEVICE plus the microSD as a USB drive of the computer (docs/USB.md D2) */
} aos_usb_mode_t;

/* From app_main, before anything: puts the PHY mux back on the Serial-JTAG.
 * RTC_CNTL.usb_conf survives a software reset, so a reboot (an OTA) from an
 * OTG mode would otherwise come up with a dead console and nothing on the
 * computer. Also hooks TinyUSB's own log (esp_rom_printf) into a ring for
 * /api/usb?tusblog=1, since in an OTG mode that output has nowhere to go. */
void aos_usb_init(void);

/* The captured TinyUSB / ROM printf output, oldest first. */
int aos_usb_tusb_log(char *out, size_t len);

aos_usb_mode_t aos_usb_mode_get(void);
const char    *aos_usb_mode_name(aos_usb_mode_t mode);

/* Blocking; installs and uninstalls the stacks, holds a NO_LIGHT_SLEEP lock
 * while the OTG side is on. Returns false if the new mode did not come up, in
 * which case the port is back on the console. Not from the USB tasks. */
bool aos_usb_mode_set(aos_usb_mode_t mode);

/* Device mode only: whether stdout/stderr are routed to the CDC port (default
 * true). Takes effect at the next switch into device mode. */
void aos_usb_console_on_cdc(bool on);

/* D3: the watch as a keyboard and media controller of the computer, in
 * device mode (CDC + HID composite). Every call blocks for the press and
 * the release (hold_ms between them, 20 if 0) and returns false when the
 * computer has not configured the interface yet. Keycodes and usages are
 * TinyUSB's HID_KEY_* / HID_USAGE_CONSUMER_*; the named ones below cover
 * what the apps need without including hid.h. */
bool aos_usb_hid_ready(void);
bool aos_usb_hid_key(uint8_t modifier, uint8_t keycode, int hold_ms);
bool aos_usb_hid_consumer(uint16_t usage, int hold_ms);
bool aos_usb_hid_named(const char *name);    /* "volup", "play", "pgdn", "cmd+tab"... see aos_usb.c */
int  aos_usb_hid_type(const char *ascii);    /* types text as a US keyboard; returns chars sent */
bool aos_usb_hid_mouse(int8_t dx, int8_t dy, int8_t wheel);   /* one relative report, no waiting */
bool aos_usb_hid_mouse_click(uint8_t buttons);               /* 1 left, 2 right: press and release */

/* D7: a MIDI controller (channel 1, cable 0), in device mode. */
bool aos_usb_midi_ready(void);
bool aos_usb_midi_note(uint8_t note, uint8_t velocity, bool on);
bool aos_usb_midi_cc(uint8_t control, uint8_t value);
bool aos_usb_midi_bend(int value);                             /* -8192..8191 */

/* Disk mode: true while the computer holds the card (the watch has no
 * /sdcard meanwhile); false once it ejected it or the mode was left. */
bool aos_usb_disk_card_away(void);

/* Host mode, H1: a pendrive (MSC) is mounted at /usb while it is plugged.
 * aos_usb_msc_root() is "/usb" when one is mounted, NULL otherwise. */
bool        aos_usb_msc_mounted(void);
const char *aos_usb_msc_root(void);

/* Whole-file copy, any path to any path, through a 32 KB PSRAM buffer.
 * Returns the bytes copied, or -1 (errno says why). */
long aos_usb_copy(const char *src, const char *dst);

/* {"mode":..,"devices":[..],"msc":{..},"regs":{..},"mem":{..}} — what /api/usb answers. */
int aos_usb_status_json(char *out, size_t len);
