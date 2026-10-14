/*
 * AmoledOS - who may talk to the portal. See aos_web_access.c.
 */
#pragma once

#include <stdbool.h>
#include "esp_http_server.h"

/* Remembers the watch's name at boot, which stays valid until the next one. */
void aos_web_access_init(void);

/* Every request goes through here before its handler. Adds the headers every
 * answer carries; when the request is refused (another site's page, a name
 * that is not the watch's), sends the answer itself and returns false. */
bool aos_web_access_ok(httpd_req_t *req);
