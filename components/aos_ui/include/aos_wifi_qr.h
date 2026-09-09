/*
 * AmoledOS - QR text for joining a WiFi network.
 *
 * It is what Android's and iOS's cameras expect out of the box:
 *
 *     WIFI:T:WPA;S:<name>;P:<password>;;
 *
 * It lives apart from the screen that draws it because it is pure string
 * building with an escaping rule that is easy to break and hard to see broken:
 * a malformed QR draws just as prettily and the phone simply does nothing. No
 * LVGL and no HAL, so tools/qr_harness.c tests it on the Mac.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the QR's text into 'out'. Returns false -and leaves 'out' empty- if
 * it does not fit whole: half a string would be a valid QR leading to the
 * wrong network.
 *
 * Inside a field the five characters that give the format its structure have
 * to be backslash-escaped: \ ; , : and ". An SSID with an unescaped semicolon
 * cuts the field off right there and the phone looks for another network. */
bool aos_wifi_qr_text(char *out, size_t out_len,
                      const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
