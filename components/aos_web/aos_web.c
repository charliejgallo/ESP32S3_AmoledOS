#include "aos_web.h"
#include <ctype.h>
#include <stdlib.h>
#include "aos_hal.h"
#include "aos_i18n.h"   /* AOS_LANG_CODE_MAX */
#include "aos_ui.h"     /* aos_ui_request_language */

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "aos_web";

/* The page is embedded with EMBED_TXTFILES; the linker defines these symbols. */
extern const uint8_t portal_html_start[] asm("_binary_portal_html_start");
extern const uint8_t portal_html_end[]   asm("_binary_portal_html_end");
extern const uint8_t wifi_html_start[]   asm("_binary_wifi_html_start");
extern const uint8_t wifi_html_end[]     asm("_binary_wifi_html_end");
extern const uint8_t clima_html_start[]  asm("_binary_clima_html_start");
extern const uint8_t clima_html_end[]    asm("_binary_clima_html_end");
extern const uint8_t cotiz_html_start[]  asm("_binary_cotiz_html_start");
extern const uint8_t cotiz_html_end[]    asm("_binary_cotiz_html_end");
extern const uint8_t sensores_html_start[] asm("_binary_sensores_html_start");
extern const uint8_t sensores_html_end[]   asm("_binary_sensores_html_end");
extern const uint8_t remoto_html_start[] asm("_binary_remoto_html_start");
extern const uint8_t red_html_start[]    asm("_binary_red_html_start");
extern const uint8_t remoto_html_end[]   asm("_binary_remoto_html_end");
extern const uint8_t red_html_end[]      asm("_binary_red_html_end");
extern const uint8_t ap_html_start[]     asm("_binary_ap_html_start");
extern const uint8_t ap_html_end[]       asm("_binary_ap_html_end");
extern const uint8_t aos_css_start[]     asm("_binary_aos_css_start");
extern const uint8_t aos_css_end[]       asm("_binary_aos_css_end");
extern const uint8_t aos_js_start[]      asm("_binary_aos_js_start");
extern const uint8_t aos_js_end[]        asm("_binary_aos_js_end");

#define UPLOAD_CHUNK        4096
#define MAX_UPLOAD_BYTES    (8 * 1024 * 1024)

static httpd_handle_t s_server;

/* --------------------------------------------------------------------------
 * Parameter validation
 *
 * Everything arriving from the network is treated as hostile: the folder has
 * to be one of the allowed ones, and of the name we keep only the last
 * component, so that a "../../something" cannot write out of place.
 * -------------------------------------------------------------------------- */

static const char *resolve_dir(const char *dir)
{
    static char path[160];

    if (!dir) {
        return NULL;
    }
    if (strcmp(dir, "apps") == 0) {
        snprintf(path, sizeof(path), "%s", aos_hal_path_apps());
    } else if (strcmp(dir, "photos") == 0) {
        snprintf(path, sizeof(path), "%s", aos_hal_path_photos());
    } else if (strcmp(dir, "music") == 0) {
        snprintf(path, sizeof(path), "%s", aos_hal_path_music());
    } else if (strcmp(dir, "recordings") == 0) {
        snprintf(path, sizeof(path), "%s", aos_hal_path_recordings());
    } else if (strcmp(dir, "redes") == 0) {
        /* With this branch, /api/list, /api/download and /api/delete serve the
         * network surveys without a single new handler. */
        snprintf(path, sizeof(path), "%s", aos_hal_path_scans());
    } else if (strcmp(dir, "lang") == 0) {
        snprintf(path, sizeof(path), "%s", aos_hal_path_lang());
    } else if (strncmp(dir, "lang/", 5) == 0) {
        /* Language packs each live in a subdirectory of their own, so this is
         * the only path with two levels. The code is validated by hand
         * -letters, digits, hyphens and nothing else- because it comes from
         * the network: without this a "lang/../.." would escape the tree. */
        const char *code = dir + 5;
        size_t n = strlen(code);
        if (n == 0 || n >= AOS_LANG_CODE_MAX) {
            return NULL;
        }
        for (size_t i = 0; i < n; i++) {
            if (!isalnum((unsigned char)code[i]) && code[i] != '-' && code[i] != '_') {
                return NULL;
            }
        }
        snprintf(path, sizeof(path), "%s/%s", aos_hal_path_lang(), code);
    } else {
        return NULL;
    }
    return path;
}

static bool safe_name(const char *name, char *out, size_t out_len)
{
    if (!name || !name[0]) {
        return false;
    }
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;

    if (base[0] == '.' || strchr(base, '\\') != NULL) {
        return false;
    }
    if (strlen(base) >= out_len) {
        return false;
    }
    snprintf(out, out_len, "%s", base);
    return true;
}

/* Pulls dir and name out of the query, already validated. */
static bool params(httpd_req_t *req, const char **dir_path, char *name, size_t name_len)
{
    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    char value[128];
    if (httpd_query_key_value(query, "dir", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    *dir_path = resolve_dir(value);
    if (!*dir_path) {
        return false;
    }

    if (name) {
        if (httpd_query_key_value(query, "name", value, sizeof(value)) != ESP_OK) {
            return false;
        }
        /* the browser sends the name percent-encoded */
        char decoded[128];
        size_t out = 0;
        for (size_t i = 0; value[i] && out < sizeof(decoded) - 1; i++) {
            if (value[i] == '%' && value[i + 1] && value[i + 2]) {
                char hex[3] = { value[i + 1], value[i + 2], 0 };
                decoded[out++] = (char)strtol(hex, NULL, 16);
                i += 2;
            } else if (value[i] == '+') {
                decoded[out++] = ' ';
            } else {
                decoded[out++] = value[i];
            }
        }
        decoded[out] = '\0';
        if (!safe_name(decoded, name, name_len)) {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------- */

static esp_err_t page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)portal_html_start,
                           portal_html_end - portal_html_start - 1);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    aos_battery_t batt;
    int percent = aos_hal_battery_read(&batt) ? batt.percent : -1;

    uint32_t internal = 0, psram = 0;
    aos_hal_heap_info(&internal, &psram);

    char json[224];
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"battery\":%d,\"heap\":%u,\"psram\":%u,"
             "\"sd\":%s,\"board\":\"%s\"}",
             aos_hal_firmware_version(), percent,
             (unsigned)internal, (unsigned)psram,
             aos_hal_sd_present() ? "true" : "false",
             aos_hal_board_name());

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t list_handler(httpd_req_t *req)
{
    const char *dir_path = NULL;
    if (!params(req, &dir_path, NULL, 0)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "dir invalido");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"files\":[");

    DIR *dir = opendir(dir_path);
    if (dir) {
        struct dirent *entry;
        bool first = true;
        char item[320];

        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            char full[320];
            snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name);

            struct stat info;
            if (stat(full, &info) != 0 || !S_ISREG(info.st_mode)) {
                continue;
            }
            snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"size\":%ld}",
                     first ? "" : ",", entry->d_name, (long)info.st_size);
            httpd_resp_sendstr_chunk(req, item);
            first = false;
        }
        closedir(dir);
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    const char *dir_path = NULL;
    char name[96];
    if (!params(req, &dir_path, name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametros invalidos");
        return ESP_FAIL;
    }
    if (req->content_len > MAX_UPLOAD_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "archivo demasiado grande");
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir_path, name);

    /* The first pack uploaded finds /lang and /lang/<code> not yet created.
     * mkdir both, ignoring the result: if they are there, EEXIST and done; if
     * they really fail, the fopen below says so with a useful error. */
    if (strncmp(dir_path, aos_hal_path_lang(), strlen(aos_hal_path_lang())) == 0) {
        mkdir(aos_hal_path_lang(), 0777);
        mkdir(dir_path, 0777);
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "could not create %s", path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no se pudo escribir");
        return ESP_FAIL;
    }

    char *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) {
        fclose(file);
        remove(path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int chunk = httpd_req_recv(req, buffer,
                                   remaining < UPLOAD_CHUNK ? remaining : UPLOAD_CHUNK);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buffer);
            fclose(file);
            remove(path);       /* better no file than half a one */
            ESP_LOGE(TAG, "upload of %s cut short", name);
            return ESP_FAIL;
        }
        if (fwrite(buffer, 1, (size_t)chunk, file) != (size_t)chunk) {
            free(buffer);
            fclose(file);
            remove(path);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "error de escritura");
            return ESP_FAIL;
        }
        remaining -= chunk;
    }

    free(buffer);
    fclose(file);
    ESP_LOGI(TAG, "uploaded %s (%d bytes)", path, req->content_len);

    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%d}", req->content_len);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* --------------------------------------------------------------------------
 * Firmware update
 *
 * The same shape as upload_handler: read the body in 4 KB pieces and hand each
 * one straight on. The difference is where they go -esp_ota_write instead of
 * fwrite- and that here there is no half-written file to delete if it fails,
 * because what gets written is the idle slot and the running one is not
 * touched until the very last step.
 *
 * It does NOT restart by itself. The answer has to reach the browser first,
 * otherwise the socket dies mid-reply and whoever pushed the update is left
 * not knowing whether it worked. The restart is a second call, POST
 * /api/ota/restart, which tools/install_fw.sh makes on its own.
 *
 * And the image arrives on trial: see aos_hal_ota_pending_verify() and the
 * confirmation in main.c.
 * -------------------------------------------------------------------------- */

static esp_err_t ota_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo vacio");
        return ESP_FAIL;
    }

    if (!aos_hal_ota_begin((size_t)req->content_len)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            aos_hal_ota_error());
        return ESP_FAIL;
    }

    char *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) {
        aos_hal_ota_abort();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int chunk = httpd_req_recv(req, buffer,
                                   remaining < UPLOAD_CHUNK ? remaining : UPLOAD_CHUNK);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            free(buffer);
            aos_hal_ota_abort();
            ESP_LOGE(TAG, "ota: upload cut short with %d B to go", remaining);
            return ESP_FAIL;
        }
        if (!aos_hal_ota_write(buffer, (size_t)chunk)) {
            free(buffer);
            /* write() already aborted and left the reason behind */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                aos_hal_ota_error());
            return ESP_FAIL;
        }
        remaining -= chunk;
    }
    free(buffer);

    if (!aos_hal_ota_end()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            aos_hal_ota_error());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ota: image of %d B installed, waiting for the restart",
             req->content_len);

    char json[128];
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"size\":%d,\"restart\":\"/api/ota/restart\"}",
             req->content_len);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* Separate so the answer to the upload gets out first. Half a second is enough
 * for the socket to drain; a plain esp_restart() here cuts the reply. */
static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    aos_hal_reboot();
    vTaskDelete(NULL);
}

static esp_err_t ota_restart_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    esp_err_t r = httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_task, "aos_restart", 2048, NULL, 5, NULL);
    return r;
}

/* --------------------------------------------------------------------------
 * Download
 *
 * Sent in chunks and not in one go: a one-minute WAV is 2 MB, and building the
 * whole response in memory for that would be throwing PSRAM away.
 *
 * With "&dl=1" it goes as an attachment and the browser saves it; without
 * that it goes as-is and the browser opens it with its own player, which is
 * what is needed to listen to a recording without downloading it. Note there
 * is no Range support: it is enough to play from start to finish, not to seek.
 * -------------------------------------------------------------------------- */

static const char *content_type_for(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcasecmp(dot, ".wav") == 0)  return "audio/wav";
    if (strcasecmp(dot, ".mp3") == 0)  return "audio/mpeg";
    if (strcasecmp(dot, ".jpg") == 0 ||
        strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".png") == 0)  return "image/png";
    if (strcasecmp(dot, ".bmp") == 0)  return "image/bmp";
    return "application/octet-stream";
}

static esp_err_t download_handler(httpd_req_t *req)
{
    const char *dir_path = NULL;
    char name[96];
    if (!params(req, &dir_path, name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametros invalidos");
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir_path, name);

    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no existe");
        return ESP_FAIL;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no se pudo abrir");
        return ESP_FAIL;
    }

    char *buffer = malloc(UPLOAD_CHUNK);
    if (!buffer) {
        fclose(file);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }

    /* attachment only if asked for: otherwise the browser opens and plays it */
    char query[256], value[16];
    bool attach = httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
                  httpd_query_key_value(query, "dl", value, sizeof(value)) == ESP_OK;

    char disposition[160];
    snprintf(disposition, sizeof(disposition), "%s; filename=\"%s\"",
             attach ? "attachment" : "inline", name);

    httpd_resp_set_type(req, content_type_for(name));
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);

    size_t got;
    esp_err_t ret = ESP_OK;
    while ((got = fread(buffer, 1, UPLOAD_CHUNK, file)) > 0) {
        if (httpd_resp_send_chunk(req, buffer, got) != ESP_OK) {
            /* the browser hung up: not an error of ours, but we have to leave */
            ret = ESP_FAIL;
            break;
        }
    }
    free(buffer);
    fclose(file);

    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "downloaded %s (%ld bytes)", path, (long)info.st_size);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    const char *dir_path = NULL;
    char name[96];
    if (!params(req, &dir_path, name, sizeof(name))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parametros invalidos");
        return ESP_FAIL;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir_path, name);
    if (remove(path) == 0) {
        ESP_LOGI(TAG, "deleted %s", path);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}


/* -------------------------------------------------------------------------- */
/* Network onboarding                                                          */
/* -------------------------------------------------------------------------- */

static esp_err_t wifi_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)wifi_html_start,
                           wifi_html_end - wifi_html_start - 1);
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    aos_wifi_ap_t aps[24];
    int n = aos_hal_net_scan(aps, (int)(sizeof(aps) / sizeof(aps[0])));

    httpd_resp_set_type(req, "application/json");
    if (n < 0) {
        return httpd_resp_send(req, "{\"error\":\"no se pudo escanear\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    /* Sent in parts so as not to build a large buffer for nothing. */
    httpd_resp_sendstr_chunk(req, "{\"redes\":[");
    for (int i = 0; i < n; i++) {
        /* A network name may carry quotes or backslashes and break the JSON. */
        char esc[70];
        size_t e = 0;
        for (size_t c = 0; aps[i].ssid[c] && e < sizeof(esc) - 2; c++) {
            char ch = aps[i].ssid[c];
            if (ch == '"' || ch == '\\') {
                esc[e++] = '\\';
            } else if ((unsigned char)ch < 0x20) {
                continue;
            }
            esc[e++] = ch;
        }
        esc[e] = 0;

        char item[160];
        snprintf(item, sizeof(item),
                 "%s{\"ssid\":\"%.68s\",\"rssi\":%d,\"segura\":%s}",
                 i ? "," : "", esc, aps[i].rssi,
                 aps[i].secure ? "true" : "false");
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

/* Decodes %XX and '+' in place. */
static void url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) &&
                                isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
}

static esp_err_t wifi_set_handler(httpd_req_t *req)
{
    char body[256];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char ssid[33] = {0};
    char pass[65] = {0};
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "pass", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);

    if (!ssid[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta el nombre de red");
        return ESP_FAIL;
    }

    /* The answer goes out BEFORE reconnecting: changing mode drops the AP and
     * with it the browser's connection, so the response would never arrive. */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    ESP_LOGI(TAG, "saving network '%s' from the portal", ssid);
    aos_hal_net_set_credentials(ssid, pass);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Name and password of our own access point
 *
 * Configured from the browser and not from the screen on purpose: typing an
 * SSID and a WPA2 password into 368 px with LVGL's keyboard is torture, and
 * the portal is already open exactly when you are connected to this AP.
 *
 * The "rotating" mode sends no password at all: the device generates it when
 * bringing the AP up and it is read off the screen, which also draws the QR.
 * That the HAL -and not the browser- generates the password is what lets it
 * rotate on its own with nobody watching the page.
 * -------------------------------------------------------------------------- */

/* Escapes what would break the JSON. The SSID is chosen by the user, so it may
 * carry quotes. */
static void json_escape(char *dst, size_t dst_len, const char *src)
{
    size_t w = 0;
    for (size_t r = 0; src[r] && w + 2 < dst_len; r++) {
        char c = src[r];
        if (c == '"' || c == '\\') {
            dst[w++] = '\\';
        } else if ((unsigned char)c < 0x20) {
            continue;
        }
        dst[w++] = c;
    }
    dst[w] = 0;
}

/* The two files every page shares. They are served with Cache-Control because
 * otherwise the browser asks for them again on every page and the only
 * advantage of having split them out is lost. An hour is enough: they change
 * when the board is reflashed, and at that point reloading by hand is normal
 * anyway. */
static esp_err_t estatico(httpd_req_t *req, const char *tipo,
                          const uint8_t *ini, const uint8_t *fin)
{
    httpd_resp_set_type(req, tipo);
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)ini, fin - ini - 1);
}

static esp_err_t css_handler(httpd_req_t *req)
{
    return estatico(req, "text/css; charset=utf-8", aos_css_start, aos_css_end);
}

static esp_err_t js_handler(httpd_req_t *req)
{
    return estatico(req, "application/javascript; charset=utf-8",
                    aos_js_start, aos_js_end);
}

static esp_err_t ap_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)ap_html_start,
                           ap_html_end - ap_html_start - 1);
}

static esp_err_t ap_get_handler(httpd_req_t *req)
{
    char ssid[80], pass[144], porde[80];
    json_escape(ssid,  sizeof(ssid),  aos_hal_net_ap_ssid());
    json_escape(pass,  sizeof(pass),  aos_hal_net_ap_pass());
    json_escape(porde, sizeof(porde), aos_hal_net_ap_default_ssid());

    char json[400];
    snprintf(json, sizeof(json),
             "{\"ssid\":\"%s\",\"clave\":\"%s\",\"modo\":\"%s\","
             "\"ssid_auto\":\"%s\",\"activo\":%s,\"ip\":\"%s\"}",
             ssid, pass,
             aos_hal_net_ap_pass_mode() == AOS_AP_PASS_ROTATING ? "rotativa"
                                                                : "fija",
             porde, aos_hal_net_ap_active() ? "true" : "false",
             aos_hal_net_ap_ip());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t ap_set_handler(httpd_req_t *req)
{
    char body[256];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char ssid[33] = {0};
    char pass[65] = {0};
    char modo[16] = {0};
    httpd_query_key_value(body, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body, "pass", pass, sizeof(pass));
    httpd_query_key_value(body, "modo", modo, sizeof(modo));
    url_decode(ssid);
    url_decode(pass);
    url_decode(modo);

    aos_ap_pass_mode_t mode = strcmp(modo, "rotativa") == 0
                                  ? AOS_AP_PASS_ROTATING : AOS_AP_PASS_FIXED;

    /* Validated HERE as well as in the HAL so we can answer why it failed: the
     * HAL returns a bool and the page needs to tell somebody the password is
     * too short. */
    size_t largo = strlen(pass);
    if (mode == AOS_AP_PASS_FIXED && largo && (largo < 8 || largo > 63)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"clave\"}");
    }
    if (strlen(ssid) > 32) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"ssid\"}");
    }

    /* The answer goes out BEFORE applying, just like /api/wifi: with the AP up,
     * aos_hal_net_ap_set_config() bounces it so the new name and password take
     * effect, and that drops the connection of the very browser making the
     * request. The response would never arrive afterwards. */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");

    ESP_LOGI(TAG, "AP from the portal: '%s', key %s",
             ssid[0] ? ssid : "(automatic)", modo[0] ? modo : "fixed");
    aos_hal_net_ap_set_config(ssid, pass, mode);
    return ESP_OK;
}

/* Switching the AP on and off from the browser.
 *
 * Until now the only switch was the button on the screen, so bringing it down
 * meant walking to the device -or restarting it, which is the detour that ended
 * up being used-. With the channel fixed, switching it on from the LAN no
 * longer drops the connection of whoever asked.
 *
 * Switching it off can drop it, for two different reasons: if the browser is on
 * the AP's side it is left without a network, and with no stored credentials
 * aos_hal_net_ap_stop() switches the whole radio off. So there the answer goes
 * first, just as in /api/wifi. */
static esp_err_t ap_estado_handler(httpd_req_t *req)
{
    char body[64];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char on[8] = {0};
    if (httpd_query_key_value(body, "on", on, sizeof(on)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta on");
        return ESP_FAIL;
    }
    bool prender = (on[0] == '1' || strcmp(on, "true") == 0);

    httpd_resp_set_type(req, "application/json");

    if (!prender) {
        httpd_resp_sendstr(req, "{\"ok\":true,\"activo\":false}");
        ESP_LOGI(TAG, "switching the AP off from the portal");
        aos_hal_net_ap_stop();
        return ESP_OK;
    }

    /* Switching on can answer afterwards, and it should: bringing the AP up can
     * fail and whoever asked wants to know. */
    bool ok = aos_hal_net_ap_start();
    ESP_LOGI(TAG, "AP requested from the portal: %s", ok ? "up" : "failed");
    return httpd_resp_sendstr(req, ok ? "{\"ok\":true,\"activo\":true}"
                                      : "{\"ok\":false,\"error\":\"levantar\"}");
}

/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Weather location
 *
 * The weather app is a .so on the microSD, so the firmware does not know it:
 * all they share is three preference keys. The portal writes them and the app
 * reads them. That is enough to configure it from the computer, which is far
 * more comfortable than a 368 px keyboard.
 *
 * The city search is done by the BROWSER against open-meteo (their API sends
 * Access-Control-Allow-Origin: *), so the firmware learns nothing about that
 * service: it only receives a name and two integers.
 *
 * If one day there is a second app with settings of its own, this asks to be
 * generalised into a prefix-scoped preferences endpoint; for a single one it is
 * not worth it.
 * -------------------------------------------------------------------------- */

#define CLIMA_KEY_CITY  "clima_city"
#define CLIMA_KEY_LAT   "clima_lat"
#define CLIMA_KEY_LON   "clima_lon"

/* The network page is HTML+JS and nothing else: it lists the files, downloads
 * whichever you pick and draws it in the browser. The firmware does NOT
 * understand the report format -the HAL writes it and /api/download serves it
 * verbatim-, the same as with 'remoto's profile. Even the manufacturer lookup
 * by MAC is resolved in there, with an OUI table living in the JavaScript. */
static esp_err_t red_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)red_html_start,
                           red_html_end - red_html_start - 1);
}

static esp_err_t clima_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)clima_html_start,
                           clima_html_end - clima_html_start - 1);
}

static esp_err_t clima_get_handler(httpd_req_t *req)
{
    char    city[40] = {0};
    int32_t lat = 0, lon = 0;
    aos_hal_pref_get_str(CLIMA_KEY_CITY, city, sizeof(city));
    aos_hal_pref_get_i32(CLIMA_KEY_LAT, &lat);
    aos_hal_pref_get_i32(CLIMA_KEY_LON, &lon);

    /* The name is stored in ASCII already, but just in case, whatever could
     * break the JSON is escaped. */
    char esc[84];
    size_t e = 0;
    for (size_t c = 0; city[c] && e < sizeof(esc) - 2; c++) {
        if (city[c] == '"' || city[c] == '\\') {
            esc[e++] = '\\';
        } else if ((unsigned char)city[c] < 0x20) {
            continue;
        }
        esc[e++] = city[c];
    }
    esc[e] = 0;

    char json[160];
    snprintf(json, sizeof(json),
             "{\"ciudad\":\"%s\",\"lat10k\":%ld,\"lon10k\":%ld}",
             esc, (long)lat, (long)lon);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* -------------------------------------------------------------------------- */
/* Languages                                                                   */
/*                                                                             */
/* Upload packs with /api/upload?dir=lang/<code> and pick one with this:       */
/* uploading and choosing both stay in the browser, without touching the       */
/* screen or taking the card out.                                             */
/* -------------------------------------------------------------------------- */

static esp_err_t lang_get_handler(httpd_req_t *req)
{
    aos_lang_t langs[AOS_LANG_MAX];
    int n = aos_i18n_scan(langs, AOS_LANG_MAX);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"actual\":\"");
    httpd_resp_sendstr_chunk(req, aos_i18n_current());
    httpd_resp_sendstr_chunk(req, "\",\"idiomas\":[");
    for (int i = 0; i < n; i++) {
        char item[160];
        /* Explicit precision: both come from the card, that is, from outside,
         * and the compiler is right to ask for the limit to be written down. */
        snprintf(item, sizeof(item),
                 "%s{\"codigo\":\"%.*s\",\"nombre\":\"%.*s\",\"cadenas\":%d,"
                 "\"apps\":%d,\"origen\":\"%s\"}",
                 i ? "," : "",
                 AOS_LANG_CODE_MAX - 1, langs[i].code,
                 AOS_LANG_NAME_MAX - 1, langs[i].name,
                 langs[i].strings, langs[i].apps,
                 langs[i].origin == AOS_LANG_EMBEDDED ? "firmware"
                     : langs[i].origin == AOS_LANG_CARD ? "tarjeta" : "codigo");
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Screenshot                                                                 */
/*                                                                            */
/* GET /api/captura returns whatever is drawn, as a BMP.                      */
/*                                                                            */
/* BMP and not PNG because there is no encoder on the board: lodepng is built  */
/* as a DECODER, for the photo viewer. A BMP is 54 bytes of header and the raw */
/* pixels, with no dependency at all, and it opens anywhere. It is 483 KB over */
/* the local network, which for looking at a screen is fine.                   */
/* -------------------------------------------------------------------------- */

#define BMP_HEADER_LEN 54

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static esp_err_t captura_handler(httpd_req_t *req)
{
    /* The screen is woken unless asked otherwise. Dimmed, what you see is the
     * always-on face -the time alone on black- and not what you wanted to look
     * at; ?sin_despertar=1 takes it exactly as it is. */
    bool despertar = true;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char v[8];
        if (httpd_query_key_value(query, "sin_despertar", v, sizeof(v)) == ESP_OK &&
            v[0] == '1') {
            despertar = false;
        }
    }

    /* It keeps trying for a while before giving up. The slot is freed on the
     * tick AFTER this task calls aos_ui_snapshot_release(), and the main loop
     * comes round every 200 ms: two screenshots in a row, which is the most
     * natural thing in the world when you are watching something, collided
     * with a 503 out of pure race. */
    bool turno = false;
    for (int i = 0; i < 15 && !turno; i++) {
        turno = aos_ui_request_snapshot(despertar);
        if (!turno) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    }
    if (!turno) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "ya hay una captura en curso");
    }

    /* Taken by the interface task on its next tick. 3 s is a great deal: the
     * main loop comes round every few milliseconds. The limit exists so as not
     * to leave the browser hanging if the interface has jammed, which is
     * precisely one of the cases where you want to take a screenshot. */
    aos_ui_snapshot_t snap = { 0 };
    aos_snapshot_state_t st = AOS_SNAPSHOT_PENDING;
    for (int i = 0; i < 250; i++) {          /* 5 s: waking takes ~1 */
        st = aos_ui_snapshot_peek(&snap);
        if (st != AOS_SNAPSHOT_PENDING) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (st != AOS_SNAPSHOT_READY || !snap.data || !snap.w || !snap.h) {
        aos_ui_snapshot_release();
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, st == AOS_SNAPSHOT_PENDING
                                       ? "la interfaz no respondio en 5 s"
                                       : "no se pudo tomar la captura");
    }

    /* BMP rows are aligned to 4 bytes. With a width of 368 px the padding
     * comes out 0, but it is computed anyway: the day the screen changes, a
     * misaligned BMP looks skewed and it is not obvious why. */
    const uint32_t fila_bytes = ((uint32_t)snap.w * 3u + 3u) & ~3u;
    const uint32_t datos      = fila_bytes * snap.h;

    uint8_t cab[BMP_HEADER_LEN] = { 0 };
    cab[0] = 'B'; cab[1] = 'M';
    wr32(cab + 2,  BMP_HEADER_LEN + datos);      /* file size                */
    wr32(cab + 10, BMP_HEADER_LEN);              /* where the data starts    */
    wr32(cab + 14, 40);                          /* BITMAPINFOHEADER         */
    wr32(cab + 18, snap.w);
    wr32(cab + 22, snap.h);                      /* positive = bottom-up     */
    wr16(cab + 26, 1);                           /* planes                   */
    wr16(cab + 28, 24);                          /* bits per pixel           */
    wr32(cab + 34, datos);
    wr32(cab + 38, 2835);                        /* 72 dpi, in px/metre      */
    wr32(cab + 42, 2835);

    uint8_t *fila = malloc(fila_bytes);
    if (!fila) {
        aos_ui_snapshot_release();
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "sin memoria para la fila");
    }

    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=\"amoledos.bmp\"");

    esp_err_t r = httpd_resp_send_chunk(req, (const char *)cab, BMP_HEADER_LEN);

    /* Bottom-up, which is how a BMP is stored. And from RGB565 to 8-bit BGR
     * replicating the high bits -(v << 3) | (v >> 2)- rather than merely
     * shifting: without that, pure white comes out 0xF8, a dirty grey. */
    for (int y = snap.h - 1; y >= 0 && r == ESP_OK; y--) {
        const uint16_t *src = (const uint16_t *)(snap.data + (size_t)y * snap.stride);
        uint8_t *dst = fila;
        for (int x = 0; x < snap.w; x++) {
            uint16_t px = src[x];
            uint8_t r5 = (uint8_t)((px >> 11) & 0x1F);
            uint8_t g6 = (uint8_t)((px >> 5)  & 0x3F);
            uint8_t b5 = (uint8_t)( px        & 0x1F);
            *dst++ = (uint8_t)((b5 << 3) | (b5 >> 2));
            *dst++ = (uint8_t)((g6 << 2) | (g6 >> 4));
            *dst++ = (uint8_t)((r5 << 3) | (r5 >> 2));
        }
        for (uint32_t p = (uint32_t)snap.w * 3u; p < fila_bytes; p++) {
            fila[p] = 0;
        }
        r = httpd_resp_send_chunk(req, (const char *)fila, fila_bytes);
    }

    free(fila);
    aos_ui_snapshot_release();      /* ALWAYS: otherwise 322 KB of PSRAM walk away */

    if (r != ESP_OK) {
        return r;                   /* the socket was cut; the chunked stream is not closed */
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t lang_set_handler(httpd_req_t *req)
{
    char body[64];
    int want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char code[AOS_LANG_CODE_MAX] = {0};
    if (httpd_query_key_value(body, "code", code, sizeof(code)) != ESP_OK || !code[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta code");
        return ESP_FAIL;
    }

    /* aos_i18n_set() is NOT called from here. This runs in the server task and
     * changing language destroys LVGL objects: it has to be requested and left
     * for aos_ui_tick() to apply, which is the same path the Settings dropdown
     * uses. */
    aos_ui_request_language(code);
    ESP_LOGI(TAG, "language requested from the portal: %s", code);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t clima_set_handler(httpd_req_t *req)
{
    char body[256];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char name[64] = {0}, lat_s[16] = {0}, lon_s[16] = {0};
    httpd_query_key_value(body, "name", name, sizeof(name));
    httpd_query_key_value(body, "lat",  lat_s, sizeof(lat_s));
    httpd_query_key_value(body, "lon",  lon_s, sizeof(lon_s));
    url_decode(name);

    /* The compiled font only draws ASCII, so accents have to be stripped
     * BEFORE storing. The page already normalises them in the browser, but the
     * endpoint can also be used by hand and "Cordoba" reads far better than
     * "Crdoba": the 0xC3 UTF-8 block is transliterated, which is where the
     * accented vowels and the n-tilde live. */
    static const char latin1[] =
        "AAAAAA CEEEEIIIIDNOOOOO OUUUUY  aaaaaa ceeeeiiiidnooooo ouuuuy y";
    char clean[40];
    size_t w = 0;
    for (size_t r = 0; name[r] && w < sizeof(clean) - 1; r++) {
        unsigned char c = (unsigned char)name[r];
        if (c >= 0x20 && c < 0x7F) {
            clean[w++] = (char)c;
        } else if (c == 0xC3 && name[r + 1]) {
            unsigned char next = (unsigned char)name[++r];
            int idx = next - 0x80;
            clean[w++] = (idx >= 0 && idx < 64) ? latin1[idx] : ' ';
        } else if ((c & 0xE0) == 0xC0 && name[r + 1]) {
            r += 1;                     /* another two-byte one: dropped */
        } else if ((c & 0xF0) == 0xE0) {
            r += 2;
        } else if ((c & 0xF8) == 0xF0) {
            r += 3;
        }
    }
    while (w > 0 && clean[w - 1] == ' ') {
        w--;
    }
    clean[w] = 0;

    long lat = strtol(lat_s, NULL, 10);
    long lon = strtol(lon_s, NULL, 10);

    if (!clean[0] || lat < -900000 || lat > 900000 ||
                     lon < -1800000 || lon > 1800000) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"lugar invalido\"}",
                               HTTPD_RESP_USE_STRLEN);
    }

    aos_hal_pref_set_str(CLIMA_KEY_CITY, clean);
    aos_hal_pref_set_i32(CLIMA_KEY_LAT, (int32_t)lat);
    aos_hal_pref_set_i32(CLIMA_KEY_LON, (int32_t)lon);
    ESP_LOGI(TAG, "weather: place '%s' (%ld, %ld) from the portal", clean, lat, lon);

    char json[96];
    snprintf(json, sizeof(json), "{\"ok\":true,\"ciudad\":\"%s\"}", clean);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* --------------------------------------------------------------------------
 * The Remoto app
 *
 * There are two different things here and it is worth not mixing them up:
 *
 *   - Home Assistant's ADDRESS and TOKEN go to NVS. They are short, they are
 *     secret and there is no reason for them to be lying around in a file on
 *     the microSD readable from any card reader.
 *   - The PROFILE -pages, buttons, gestures- goes to a file on the microSD. In
 *     NVS a string has a ceiling of a few kilobytes and a six-page profile
 *     goes past it; besides, this way it can be copied, versioned and edited
 *     by hand.
 *
 * The firmware does NOT understand the profile: it receives it, writes it and
 * hands it back verbatim. Who interprets it is the .so, which is the one that
 * knows what it means. All they share is the rc_gen key, which goes up on
 * every save and which the app re-reads now and then to find out there is
 * something new.
 *
 * The two queries that DO go out to Home Assistant from here -testing the
 * connection and listing entities- exist so that configuring the remote is not
 * a matter of typing "light.cocina" from memory and discovering the mistake
 * three days later.
 * -------------------------------------------------------------------------- */

#define RC_KEY_URL      "rc_url"
#define RC_KEY_TOKEN    "rc_token"
#define RC_KEY_GEN      "rc_gen"

#define RC_PROFILE_MAX  (48 * 1024)

static void rc_profile_path(char *out, size_t len)
{
    snprintf(out, len, "%s/remoto.json", aos_hal_path_data());
}

/* -------------------------------------------------------------------------- */
/* Exchange rates (#45)                                                        */
/*                                                                             */
/* Two keys and nothing more: the list of instruments and a generation counter
 * that goes up on every save. It is 'clima's arrangement —the portal writes
 * preferences and the .so reads them— and not 'remoto's, which needs a file on
 * the microSD because its profile is kilobytes. Here the whole list is ~75
 * characters.
 *
 * And as in /red, THE FIRMWARE DOES NOT UNDERSTAND THE FORMAT: it stores a
 * comma-separated string of keys without knowing what they mean. Who
 * interprets them is the app, which is the one that knows. All that is
 * validated here is that they are reasonable characters and that they fit in
 * the key; adding an instrument does not touch the firmware.
 *
 * Why an endpoint of its own and not a generic "preferences by prefix" one,
 * which is what this file wondered about when 'remoto' turned up: because what
 * differs between the three apps is precisely THE VALIDATION —clima validates
 * coordinates, remoto validates the URL scheme, this one validates the shape
 * of the list— and a generic endpoint can validate nothing. It would be a way
 * for anybody to write any NVS key from the network.                          */
/* -------------------------------------------------------------------------- */

#define COTIZ_KEY_LIST  "cz_list"
#define COTIZ_KEY_GEN   "cz_gen"

static esp_err_t cotiz_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)cotiz_html_start,
                           cotiz_html_end - cotiz_html_start - 1);
}

static esp_err_t cotiz_get_handler(httpd_req_t *req)
{
    char lista[160] = {0};
    aos_hal_pref_get_str(COTIZ_KEY_LIST, lista, sizeof(lista));

    char json[200];
    snprintf(json, sizeof(json), "{\"lista\":\"%s\"}", lista);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cotiz_set_handler(httpd_req_t *req)
{
    char body[256];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char lista[160] = {0};
    httpd_query_key_value(body, "lista", lista, sizeof(lista));
    url_decode(lista);

    /* All that is validated is the SHAPE, because the firmware does not know
     * which instruments exist: lower case and commas. Without this, this
     * endpoint would write anything arriving over the network into NVS. A key
     * the app does not know it ignores by itself, so no allow-list is needed
     * here —and having one would mean reflashing to add an instrument—. */
    for (const char *c = lista; *c; c++) {
        if ((*c < 'a' || *c > 'z') && *c != ',') {
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_send(req,
                "{\"ok\":false,\"error\":\"la lista solo puede tener letras "
                "minusculas y comas\"}", HTTPD_RESP_USE_STRLEN);
        }
    }

    aos_hal_pref_set_str(COTIZ_KEY_LIST, lista);

    /* The generation counter ALWAYS goes up, including when the list came out
     * unchanged: it is what makes an open app notice and reload, and saving
     * twice in a row has to work both times. */
    int32_t gen = 0;
    aos_hal_pref_get_i32(COTIZ_KEY_GEN, &gen);
    aos_hal_pref_set_i32(COTIZ_KEY_GEN, gen + 1);

    ESP_LOGI(TAG, "cotizaciones: [%s]", lista);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t remoto_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)remoto_html_start,
                           remoto_html_end - remoto_html_start - 1);
}

/* What the browser needs to know when opening the page. The token is NEVER
 * returned: only whether one is stored and its last four characters, which are
 * enough to recognise which it is without being able to reconstruct it. */
static esp_err_t remoto_config_get(httpd_req_t *req)
{
    char url[80]    = {0};
    char token[280] = {0};
    aos_hal_pref_get_str(RC_KEY_URL, url, sizeof(url));
    aos_hal_pref_get_str(RC_KEY_TOKEN, token, sizeof(token));

    size_t n = strlen(token);
    const char *cola = (n >= 4) ? token + n - 4 : "";

    int32_t gen = 0;
    aos_hal_pref_get_i32(RC_KEY_GEN, &gen);

    char json[220];
    snprintf(json, sizeof(json),
             "{\"url\":\"%s\",\"token\":%s,\"cola\":\"%s\",\"gen\":%ld}",
             url, n ? "true" : "false", cola, (long)gen);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static void rc_bump_gen(void)
{
    int32_t gen = 0;
    aos_hal_pref_get_i32(RC_KEY_GEN, &gen);
    aos_hal_pref_set_i32(RC_KEY_GEN, gen + 1);
}

static esp_err_t remoto_config_post(httpd_req_t *req)
{
    char body[512];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char url[80] = {0}, token[280] = {0};
    httpd_query_key_value(body, "url", url, sizeof(url));
    httpd_query_key_value(body, "token", token, sizeof(token));
    url_decode(url);
    url_decode(token);

    /* The trailing slash is also stripped by the app, but the sooner the
     * better: it is what you copy out of the browser's address bar. */
    for (size_t n = strlen(url); n > 0 && url[n - 1] == '/'; n = strlen(url)) {
        url[n - 1] = 0;
    }

    /* Both schemes: https:// for an installation exposed to the internet,
     * http:// for the one on the LAN, which usually has no certificate that
     * can be verified. The user chooses; see rc_ha_load() in the app. */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req,
            "{\"ok\":false,\"error\":\"tiene que empezar con http:// o https://\"}",
            HTTPD_RESP_USE_STRLEN);
    }
    aos_hal_pref_set_str(RC_KEY_URL, url);

    /* An empty token leaves the previous one: that way the address can be
     * corrected without having to go to Home Assistant for a new token. */
    if (token[0]) {
        aos_hal_pref_set_str(RC_KEY_TOKEN, token);
    }
    rc_bump_gen();
    ESP_LOGI(TAG, "remoto: %s, token %s", url, token[0] ? "new" : "unchanged");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

/* The profile, exactly as it is on the microSD. */
static esp_err_t remoto_profile_get(httpd_req_t *req)
{
    char path[160];
    rc_profile_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    httpd_resp_set_type(req, "application/json");
    if (!f) {
        return httpd_resp_send(req, "null", HTTPD_RESP_USE_STRLEN);
    }
    char *buf = malloc(2048);
    if (!buf) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }
    size_t n;
    while ((n = fread(buf, 1, 2048, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            break;
        }
    }
    free(buf);
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t remoto_profile_post(httpd_req_t *req)
{
    if (req->content_len <= 2 || req->content_len > RC_PROFILE_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "tamano invalido");
        return ESP_FAIL;
    }

    char path[160], tmp[176];
    rc_profile_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    /* Written to a temporary file and renamed at the end. If the connection
     * were cut halfway, the good profile stays where it was: the app reads
     * this file when it opens and a half-written one would leave it with no
     * buttons. */
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "no pude escribir en la microSD");
        return ESP_FAIL;
    }

    char *buf = malloc(UPLOAD_CHUNK);
    if (!buf) {
        fclose(f);
        remove(tmp);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin memoria");
        return ESP_FAIL;
    }

    int left = req->content_len;
    bool ok  = true;
    while (left > 0) {
        int want = left < UPLOAD_CHUNK ? left : UPLOAD_CHUNK;
        int got  = httpd_req_recv(req, buf, want);
        if (got <= 0) {
            ok = false;
            break;
        }
        if (fwrite(buf, 1, (size_t)got, f) != (size_t)got) {
            ok = false;
            break;
        }
        left -= got;
    }
    free(buf);
    fclose(f);

    if (!ok) {
        remove(tmp);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "se corto la escritura");
        return ESP_FAIL;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "no pude renombrar el archivo");
        return ESP_FAIL;
    }
    rc_bump_gen();
    ESP_LOGI(TAG, "remoto: profile saved, %d bytes", req->content_len);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

/* --------------------------------------------------------------------------
 * Queries to Home Assistant from the portal
 *
 * The HAL's HTTP client is asynchronous because whoever uses it is normally a
 * dynamic app, which cannot block the LVGL thread. Here it is the other way
 * round: this runs in the web server's task, which can wait without bothering
 * anybody, so it just polls and that is that.
 * -------------------------------------------------------------------------- */

static int ha_fetch(const char *path, const char *body, int max_bytes,
                    char **out, int *out_len)
{
    *out     = NULL;
    *out_len = 0;

    char url[80] = {0}, token[280] = {0};
    aos_hal_pref_get_str(RC_KEY_URL, url, sizeof(url));
    aos_hal_pref_get_str(RC_KEY_TOKEN, token, sizeof(token));
    /* Both schemes, the same as rc_ha_load() in the app and as the endpoint
     * that stores the address. It was overlooked when Remoto learned https and
     * the symptom was baffling: with the encrypted address stored correctly,
     * the "Probar" button answered "the address or the token is missing". */
    bool esquema_ok = (strncmp(url, "http://", 7) == 0) ||
                      (strncmp(url, "https://", 8) == 0);
    if (!esquema_ok || !token[0]) {
        return -100;                    /* not configured yet */
    }

    char hdr[320];
    snprintf(hdr, sizeof(hdr), "Authorization: Bearer %s\r\n", token);

    char full[200];
    snprintf(full, sizeof(full), "%s%s", url, path);

    int id = aos_hal_http_request(body ? "POST" : "GET", full, hdr, body,
                                  body ? "application/json" : NULL, max_bytes);
    if (id <= 0) {
        return -101;
    }
    /* Twelve seconds: the HAL's client gives up on its own at ten. */
    for (int i = 0; i < 240 && aos_hal_http_state(id) == AOS_HTTP_BUSY; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    int code = aos_hal_http_status(id);
    if (aos_hal_http_state(id) == AOS_HTTP_DONE) {
        const char *b = aos_hal_http_body(id);
        int         n = aos_hal_http_len(id);
        if (b && n > 0) {
            *out = malloc((size_t)n + 1);
            if (*out) {
                memcpy(*out, b, (size_t)n);
                (*out)[n] = 0;
                *out_len  = n;
            }
        }
    }
    aos_hal_http_release(id);
    return code;
}

static esp_err_t remoto_probe_handler(httpd_req_t *req)
{
    char *body = NULL;
    int   len  = 0;
    int   code = ha_fetch("/api/", NULL, 1024, &body, &len);

    char json[256];
    if (code == -100) {
        snprintf(json, sizeof(json),
                 "{\"ok\":false,\"msg\":\"falta la direccion o el token\"}");
    } else if (code == 200) {
        snprintf(json, sizeof(json),
                 "{\"ok\":true,\"msg\":\"Home Assistant contesta y el token sirve\"}");
    } else if (code == 401 || code == 403) {
        snprintf(json, sizeof(json),
                 "{\"ok\":false,\"msg\":\"llegue a Home Assistant pero rechazo el "
                 "token (%d)\"}", code);
    } else if (code < 0) {
        /* The order is that of the AOS_HTTP_ERR_* in aos_hal.h and the index
         * is -code, so adding an error over there forces adding it here: if
         * not, the message falls through to the generic one and the user sees
         * a bare number. */
        static const char *const porque[] = {
            "", "no resolvi el nombre", "no me pude conectar",
            "fallo al mandar", "se corto la respuesta",
            "eso no contesta HTTP", "me quede sin memoria",
            "la placa todavia no tiene la hora: espera unos segundos y proba de nuevo",
            "no pude verificar el certificado. Si Home Assistant esta en tu red "
            "y usa un certificado propio, poné la direccion con http://"
        };
        int i = -code;
        snprintf(json, sizeof(json), "{\"ok\":false,\"msg\":\"%s (%d)\"}",
                 (i >= 1 && i <= (int)(sizeof(porque) / sizeof(porque[0])) - 1)
                     ? porque[i] : "no pude preguntar", code);
    } else {
        snprintf(json, sizeof(json),
                 "{\"ok\":false,\"msg\":\"contesto %d\"}", code);
    }
    free(body);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/* The list of entities, so the page can offer autocompletion instead of making
 * you type "light.cocina" from memory. It is asked for with a template and not
 * with /api/states because that would return the state and attributes of
 * everything, which in a house with two hundred entities is hundreds of
 * kilobytes. */
/* Only the NUMERIC sensors, with name, unit and current value. It is what
 * #46's picker needs, and it is far smaller than the whole entity list: of the
 * 372 sensors of a real house, the ones with a number are the only ones that
 * can be plotted.
 *
 * The filtering is done by HOME ASSISTANT and not by the browser or the
 * firmware: it is the one with the data and a CPU. Same idea as Remoto's
 * single template. */
static esp_err_t sensores_handler(httpd_req_t *req)
{
    static const char *TPL =
        "{\"template\":\"{% for s in states.sensor %}"
        "{% if s.state is not none and is_number(s.state) %}"
        "{{s.entity_id}}|{{s.name}}|{{s.attributes.unit_of_measurement|default('')}}"
        "|{{s.state}}\\n{% endif %}{% endfor %}\"}";

    char *body = NULL;
    int   len  = 0;
    int   code = ha_fetch("/api/template", TPL, 32 * 1024, &body, &len);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (code == 200 && body) {
        esp_err_t r = httpd_resp_send(req, body, len);
        free(body);
        return r;
    }
    free(body);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "", 0);
}

#define SN_KEY_LIST  "sn_list"
#define SN_KEY_GEN   "sn_gen"

static esp_err_t sensores_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)sensores_html_start,
                           sensores_html_end - sensores_html_start - 1);
}

static esp_err_t sensores_conf_get(httpd_req_t *req)
{
    char lista[400] = {0};
    aos_hal_pref_get_str(SN_KEY_LIST, lista, sizeof(lista));

    /* Whatever would break the JSON is escaped. The names are written by the
     * browser in ASCII already, but this endpoint can also be called by hand. */
    char esc[440];
    size_t e = 0;
    for (size_t c = 0; lista[c] && e < sizeof(esc) - 2; c++) {
        if (lista[c] == '"' || lista[c] == '\\') {
            esc[e++] = '\\';
        } else if ((unsigned char)lista[c] < 0x20) {
            continue;
        }
        esc[e++] = lista[c];
    }
    esc[e] = 0;

    char json[480];
    snprintf(json, sizeof(json), "{\"lista\":\"%s\"}", esc);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sensores_conf_post(httpd_req_t *req)
{
    char body[600];
    int  want = req->content_len;
    if (want <= 0 || want >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    int got = httpd_req_recv(req, body, want);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no llego el cuerpo");
        return ESP_FAIL;
    }
    body[got] = 0;

    char lista[400] = {0};
    httpd_query_key_value(body, "lista", lista, sizeof(lista));
    url_decode(lista);

    /* Accents are stripped a second time, just as in /clima: the page already
     * normalises in the browser, but a curl by hand would store "Presión" and
     * the watch would show little boxes. Dropping the non-ASCII bytes is safe
     * but looks broken ("Presin"); transliterating the 0xC3 block costs ten
     * lines and is the same table clima and its endpoint use. */
    static const char latin1[] =
        "AAAAAA CEEEEIIIIDNOOOOO OUUUUY  aaaaaa ceeeeiiiidnooooo ouuuuy y";
    char limpio[400];
    size_t w = 0;
    for (size_t r = 0; lista[r] && w < sizeof(limpio) - 1; r++) {
        unsigned char c = (unsigned char)lista[r];
        if (c >= 0x20 && c < 0x7F) {
            limpio[w++] = (char)c;
        } else if (c == 0xC3 && lista[r + 1]) {
            unsigned char next = (unsigned char)lista[++r];
            int idx = next - 0x80;
            limpio[w++] = (idx >= 0 && idx < 64) ? latin1[idx] : ' ';
        } else if (c == 0xC2 && (unsigned char)lista[r + 1] == 0xB0 &&
                   w + 2 < sizeof(limpio)) {
            /* The degree sign IS in the compiled font, and it is what
             * distinguishes "22,9 C" from "22,9 °C" on half the sensors of a
             * house. It is two-byte UTF-8 (\xC2\xB0) and passes through
             * verbatim; the rule was written down by clima: never 0xB0 on its
             * own. Without this branch it fell into the two-byte drop below. */
            limpio[w++] = (char)0xC2;
            limpio[w++] = (char)0xB0;
            r += 1;
        } else if ((c & 0xE0) == 0xC0 && lista[r + 1]) {
            r += 1;
        } else if ((c & 0xF0) == 0xE0) {
            r += 2;
        } else if ((c & 0xF8) == 0xF0) {
            r += 3;
        }
    }
    limpio[w] = 0;

    /* Four records at most, which is what the app draws. Counted here as well
     * as in the browser because the endpoint can be called by hand and a fifth
     * would go into NVS never to be seen. */
    int registros = limpio[0] ? 1 : 0;
    for (const char *c = limpio; *c; c++) {
        if (*c == ';' && c[1]) {
            registros++;
        }
    }
    if (registros > 4) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req,
            "{\"ok\":false,\"error\":\"como mucho cuatro sensores\"}",
            HTTPD_RESP_USE_STRLEN);
    }

    aos_hal_pref_set_str(SN_KEY_LIST, limpio);

    int32_t gen = 0;
    aos_hal_pref_get_i32(SN_KEY_GEN, &gen);
    aos_hal_pref_set_i32(SN_KEY_GEN, gen + 1);

    ESP_LOGI(TAG, "sensores: %d chosen [%s]", registros, limpio);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t remoto_entities_handler(httpd_req_t *req)
{
    static const char *TPL =
        "{\"template\":\"{{ states | map(attribute='entity_id') | join(',') }}\"}";

    char *body = NULL;
    int   len  = 0;
    int   code = ha_fetch("/api/template", TPL, 24 * 1024, &body, &len);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (code == 200 && body) {
        esp_err_t r = httpd_resp_send(req, body, len);
        free(body);
        return r;
    }
    free(body);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, "", 0);
}

/* -------------------------------------------------------------------------- */
/* The routes                                                                  */
/*                                                                             */
/* Outside aos_web_start() so they can be COUNTED before configuring the
 * server. The ceiling used to be written by hand ("24, that is 21 routes +
 * margin") and on adding the exchange-rate and sensor ones the table reached
 * 26: the last two were not registered, httpd_register_uri_handler returned an
 * error, nobody looked at it, and the endpoints gave 404 without a single line
 * in the log. Now the ceiling comes from the table and adding a route cannot
 * fail silently.                                                              */
/* -------------------------------------------------------------------------- */
static const httpd_uri_t ROUTES[] = {
        { .uri = "/",            .method = HTTP_GET,  .handler = page_handler },
        { .uri = "/api/status",  .method = HTTP_GET,  .handler = status_handler },
        { .uri = "/api/list",    .method = HTTP_GET,  .handler = list_handler },
        { .uri = "/api/upload",  .method = HTTP_POST, .handler = upload_handler },
        { .uri = "/api/ota",     .method = HTTP_POST, .handler = ota_handler },
        { .uri = "/api/ota/restart", .method = HTTP_POST, .handler = ota_restart_handler },
        { .uri = "/api/download",.method = HTTP_GET,  .handler = download_handler },
        { .uri = "/api/delete",  .method = HTTP_POST, .handler = delete_handler },
        { .uri = "/wifi",        .method = HTTP_GET,  .handler = wifi_page_handler },
        { .uri = "/api/scan",    .method = HTTP_GET,  .handler = scan_handler },
        { .uri = "/api/wifi",    .method = HTTP_POST, .handler = wifi_set_handler },
        { .uri = "/ap",          .method = HTTP_GET,  .handler = ap_page_handler },
        { .uri = "/api/ap",      .method = HTTP_GET,  .handler = ap_get_handler },
        { .uri = "/api/ap",      .method = HTTP_POST, .handler = ap_set_handler },
        { .uri = "/api/ap/estado", .method = HTTP_POST, .handler = ap_estado_handler },
        { .uri = "/aos.css",     .method = HTTP_GET,  .handler = css_handler },
        { .uri = "/aos.js",      .method = HTTP_GET,  .handler = js_handler },
        { .uri = "/red",         .method = HTTP_GET,  .handler = red_page_handler },
        { .uri = "/api/lang",    .method = HTTP_GET,  .handler = lang_get_handler },
        { .uri = "/api/lang",    .method = HTTP_POST, .handler = lang_set_handler },
        { .uri = "/api/captura", .method = HTTP_GET, .handler = captura_handler },
        { .uri = "/clima",       .method = HTTP_GET,  .handler = clima_page_handler },
        { .uri = "/api/clima",   .method = HTTP_GET,  .handler = clima_get_handler },
        { .uri = "/api/clima",   .method = HTTP_POST, .handler = clima_set_handler },
        { .uri = "/cotiz",       .method = HTTP_GET,  .handler = cotiz_page_handler },
        { .uri = "/api/cotiz",   .method = HTTP_GET,  .handler = cotiz_get_handler },
        { .uri = "/api/cotiz",   .method = HTTP_POST, .handler = cotiz_set_handler },
        { .uri = "/remoto",              .method = HTTP_GET,  .handler = remoto_page_handler },
        { .uri = "/api/remoto/config",   .method = HTTP_GET,  .handler = remoto_config_get },
        { .uri = "/api/remoto/config",   .method = HTTP_POST, .handler = remoto_config_post },
        { .uri = "/api/remoto/perfil",   .method = HTTP_GET,  .handler = remoto_profile_get },
        { .uri = "/api/remoto/perfil",   .method = HTTP_POST, .handler = remoto_profile_post },
        { .uri = "/api/remoto/probar",   .method = HTTP_GET,  .handler = remoto_probe_handler },
        { .uri = "/api/remoto/entidades",.method = HTTP_GET,  .handler = remoto_entities_handler },
        { .uri = "/api/sensores",        .method = HTTP_GET,  .handler = sensores_handler },
        { .uri = "/sensores",            .method = HTTP_GET,  .handler = sensores_page_handler },
        { .uri = "/api/sensoresconf",    .method = HTTP_GET,  .handler = sensores_conf_get },
        { .uri = "/api/sensoresconf",    .method = HTTP_POST, .handler = sensores_conf_post },
};
#define N_ROUTES ((uint16_t)(sizeof(ROUTES) / sizeof(ROUTES[0])))

esp_err_t aos_web_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* From the size of the array, NOT from a hand-written number. It was at 24
     * with a comment saying "21 routes + margin", and on adding the
     * exchange-rate and sensor ones the array reached 26: for the last two
     * httpd_register_uri_handler() returned an error and NOBODY looked at it,
     * so the endpoints simply gave 404 without a line in the log. Tied to the
     * array, adding a route cannot fail silently again. */
    config.max_uri_handlers = N_ROUTES;
    /* 8 KB and not 6: the /remoto handlers that query Home Assistant build the
     * Authorization header (320 B) and the URL on the stack, on top of what
     * the server already uses. An overflow here does not give a clean error,
     * and it is 2 KB of internal RAM in a single task. */
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "the server did not start: %s", esp_err_to_name(ret));
        return ret;
    }

    for (unsigned i = 0; i < N_ROUTES; i++) {
        esp_err_t r = httpd_register_uri_handler(s_server, &ROUTES[i]);
        if (r != ESP_OK) {
            /* Should never happen, because max_uri_handlers comes from the
             * size of the table. If it does, let it be seen. */
            ESP_LOGE(TAG, "could not register %s: %s",
                     ROUTES[i].uri, esp_err_to_name(r));
        }
    }

    ESP_LOGI(TAG, "portal at http://%s/  (wifi, clima, cotiz, sensores, remoto, red)",
             aos_hal_net_ap_active() ? aos_hal_net_ap_ip() : aos_hal_net_ip());
    return ESP_OK;
}

void aos_web_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool aos_web_running(void)
{
    return s_server != NULL;
}
