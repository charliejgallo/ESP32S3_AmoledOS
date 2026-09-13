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
    AOS_USB_DEVICE,         /* OTG as a peripheral: today a CDC serial port with the console on it */
    AOS_USB_HOST,           /* OTG as a host: today the inspector (enumerate and describe what is plugged) */
} aos_usb_mode_t;

aos_usb_mode_t aos_usb_mode_get(void);
const char    *aos_usb_mode_name(aos_usb_mode_t mode);

/* Blocking; installs and uninstalls the stacks, holds a NO_LIGHT_SLEEP lock
 * while the OTG side is on. Returns false if the new mode did not come up, in
 * which case the port is back on the console. Not from the USB tasks. */
bool aos_usb_mode_set(aos_usb_mode_t mode);

/* Device mode only: whether stdout/stderr are routed to the CDC port (default
 * true). Takes effect at the next switch into device mode. */
void aos_usb_console_on_cdc(bool on);

/* {"mode":..,"devices":[..],"mem":{..}} — what /api/usb answers. */
int aos_usb_status_json(char *out, size_t len);
