/*
 * AmoledOS - who may talk to the portal (2026-10-14, from P4OS's
 * aos_portal_access.c).
 *
 * Two attacks that need no access to the network, only the user's browser
 * on it, and that worked until v0.12.0:
 *
 *   A page from anywhere sends requests to the watch. A form or a fetch()
 *   with a text/plain body is a "simple" request: the browser sends it with
 *   no preflight, and the watch acted on it (delete a file, change the
 *   Wi-Fi, install firmware). The browser says where a request comes from
 *   (Sec-Fetch-Site, Origin): the API answers only the portal's own pages.
 *   A tool (curl, the scripts in tools/) sends neither header and is not
 *   affected.
 *
 *   DNS rebinding: a name of the attacker's that first points to their
 *   server and then to the watch makes the browser treat the watch as that
 *   site, and read its answers. The Host the browser sends is then the
 *   attacker's name: the portal answers only to its own (an IP, or the
 *   watch's name, bare or with .local and the suffixes home routers add).
 *
 * Every answer also carries nosniff and X-Frame-Options: DENY.
 */
#include "aos_web_access.h"
#include "aos_hal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"

static const char *TAG = "aos_web";

/* The name the watch had at boot. Renaming it in /ajustes does not reload
 * the page, which goes on talking to the old name until the next restart. */
static char s_boot_name[33];

/* "192.168.1.107", "[fe80::1]" */
static bool is_ip_literal(const char *h, size_t n)
{
    if (n && h[0] == '[') {
        return true;
    }
    int dots = 0;
    for (size_t i = 0; i < n; i++) {
        if (h[i] == '.') {
            dots++;
        } else if (!isdigit((unsigned char)h[i])) {
            return false;
        }
    }
    return dots == 3;
}

static bool is_name(const char *h, size_t n, const char *name)
{
    static const char *const SUFFIX[] = { "", ".local", ".lan", ".home", ".home.arpa", ".localdomain" };
    size_t nl = strlen(name);
    if (!nl || n < nl || strncasecmp(h, name, nl)) {
        return false;
    }
    for (size_t i = 0; i < sizeof SUFFIX / sizeof SUFFIX[0]; i++) {
        if (n - nl == strlen(SUFFIX[i]) && !strncasecmp(h + nl, SUFFIX[i], n - nl)) {
            return true;
        }
    }
    return false;
}

static bool is_our_name(const char *h, size_t n)
{
    if (n == 9 && !strncasecmp(h, "localhost", 9)) {
        return true;
    }
    return is_name(h, n, aos_hal_device_name()) || is_name(h, n, s_boot_name);
}

/* The host part of "name:port" (or of a whole "http://name:port"). */
static void host_part(const char *s, const char **h, size_t *n)
{
    const char *p = strstr(s, "://");
    p = p ? p + 3 : s;
    const char *end = p[0] == '[' ? strchr(p, ']') : NULL;
    if (end) {
        end++;
    } else {
        end = p;
        while (*end && *end != ':' && *end != '/') {
            end++;
        }
    }
    *h = p;
    *n = (size_t)(end - p);
}

static bool deny(httpd_req_t *req, const char *status, const char *why)
{
    ESP_LOGW(TAG, "refused %s: %s", req->uri, why);
    char json[160];
    snprintf(json, sizeof json, "{\"ok\":false,\"error\":\"%s\"}", why);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    return false;
}

void aos_web_access_init(void)
{
    snprintf(s_boot_name, sizeof s_boot_name, "%s", aos_hal_device_name());
}

bool aos_web_access_ok(httpd_req_t *req)
{
    /* nosniff: a file from the card is what its type says; DENY: no other
     * site can frame the portal and make the user click on it */
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");

    char host[96] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) == ESP_OK && host[0]) {
        const char *h;
        size_t n;
        host_part(host, &h, &n);
        if (!is_ip_literal(h, n) && !is_our_name(h, n)) {
            return deny(req, "421 Misdirected Request",
                        "nombre desconocido: entrá por la IP del reloj o por su nombre .local");
        }
    }
    if (strncmp(req->uri, "/api/", 5) != 0) {
        return true;        /* a page or a script: nothing to protect */
    }

    /* only the portal's own pages call the API from a browser */
    char site[24];
    if (httpd_req_get_hdr_value_str(req, "Sec-Fetch-Site", site, sizeof site) == ESP_OK &&
        strcasecmp(site, "same-origin") != 0 && strcasecmp(site, "none") != 0) {
        return deny(req, "403 Forbidden", "pedido desde otra página: rechazado");
    }
    char origin[128];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof origin) == ESP_OK) {
        if (strcmp(origin, "null") == 0) {
            /* a sandboxed frame, a file:// page */
            return deny(req, "403 Forbidden", "pedido sin origen: rechazado");
        }
        const char *oh, *hh;
        size_t on, hn;
        host_part(origin, &oh, &on);
        host_part(host, &hh, &hn);
        /* the port too: another service on the same address is another site */
        const char *op = oh + on, *hp = hh + hn;
        bool same = on == hn && !strncasecmp(oh, hh, on) &&
                    !strcmp(*op == ':' ? op : "", *hp == ':' ? hp : "");
        if (!same) {
            return deny(req, "403 Forbidden", "pedido desde otra página: rechazado");
        }
    }
    return true;
}
