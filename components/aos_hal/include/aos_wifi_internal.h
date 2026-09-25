/*
 * AmoledOS - Borrowing the radio for a scan.
 *
 * Listing the networks around needs two things the watch does not always
 * have: the wifi stack up, and the station NOT in the middle of connecting.
 * Measured on the board (2026-09-25): with the wifi off, or on but unable to
 * reach its access point, the scanner app found "0 networks" instantly. The
 * first because there was no stack to scan with; the second because the
 * disconnect handler retries esp_wifi_connect() forever, and the IDF refuses
 * to scan while the station is connecting (ESP_ERR_WIFI_STATE, "WiFi still
 * connecting when invoke esp_wifi_scan_start").
 *
 * So a scan borrows the radio: prepare brings the stack up if it was down and
 * pauses the reconnect loop if it was running, and finish puts both back
 * exactly as they were. Used by aos_scan.c and by aos_hal_net_scan() (the
 * portal's /wifi page, which had the same bug in the one situation where you
 * most need it: fixing credentials that do not work). Nothing here is for apps.
 */
#pragma once

#include <stdbool.h>
#include "aos_hal.h"

typedef struct {
    bool            stack_was_off;  /* brought up just for this scan: taken down after */
    bool            held;           /* the reconnect loop was paused */
    aos_net_state_t state_before;
    bool            enabled_before; /* the user's wifi preference when it started */
} aos_wifi_scan_ctx_t;

/* Leaves the radio ready to scan. false if the stack could not come up. */
bool aos_wifi_scan_prepare(aos_wifi_scan_ctx_t *ctx);

/* Puts the radio back the way prepare found it. Always call it after a
 * successful prepare, also when the scan itself failed. */
void aos_wifi_scan_finish(const aos_wifi_scan_ctx_t *ctx);
