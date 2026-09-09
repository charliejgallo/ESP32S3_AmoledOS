/*
 * AmoledOS - Web portal.
 *
 * A small HTTP server for uploading apps, photos and music to the microSD from
 * any browser on the network. The page is in portal.html and is embedded in
 * the binary.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the server. Requires the network to be connected already. */
esp_err_t aos_web_start(void);
void      aos_web_stop(void);
bool      aos_web_running(void);

#ifdef __cplusplus
}
#endif
