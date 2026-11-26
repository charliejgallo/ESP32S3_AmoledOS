/*
 * AmoledOS - The Store's side on the watch: the portal's /api/store calls.
 * What it is and what it checks is at the top of aos_store.c; the calls are
 * in docs/STORE.md.
 */
#pragma once

#include "esp_http_server.h"
#include "aos_app.h"            /* AOS_ABI_VERSION */

#define AOS_STORE_ABI   AOS_ABI_VERSION

esp_err_t aos_store_get_handler(httpd_req_t *req);        /* GET  /api/store */
esp_err_t aos_store_catalog_handler(httpd_req_t *req);    /* GET  /api/store/catalog */
esp_err_t aos_store_post_handler(httpd_req_t *req);       /* POST /api/store */
esp_err_t aos_store_install_handler(httpd_req_t *req);    /* POST /api/store/install */
esp_err_t aos_store_verify_handler(httpd_req_t *req);     /* POST /api/store/verify */
esp_err_t aos_store_look_handler(httpd_req_t *req);       /* POST /api/store/look */
esp_err_t aos_store_remove_handler(httpd_req_t *req);     /* POST /api/store/remove */
