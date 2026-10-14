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
 *
 * And who may use the API at all depends on where the request came in
 * (aos_access.h): the cable, the watch's own network and a trusted Wi-Fi
 * network need nothing; any other network needs a session the watch
 * approved (the aos_s cookie) or the token (Authorization: Bearer). The
 * pages themselves are served to anyone: they hold no data, and the login
 * is one of them.
 */
#include "aos_web_access.h"
#include "aos_access.h"
#include "aos_hal.h"

#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

/* ---- where a request came in ------------------------------------------------ */

typedef enum { ZONE_CABLE, ZONE_AP, ZONE_TRUSTED, ZONE_OTHER } zone_t;

static const char *const ZONE_NAME[] = { "cable", "ap", "confianza", "otra" };

/* The watch's own address the request arrived at says which network it
 * came through; the peer's is what the watch shows when it asks. */
static zone_t zone_of(httpd_req_t *req, char *peer, size_t peer_len)
{
    int fd = httpd_req_to_sockfd(req);
    struct sockaddr_in local = { 0 }, remote = { 0 };
    socklen_t ll = sizeof local, rl = sizeof remote;
    char here[16] = "";
    if (getsockname(fd, (struct sockaddr *)&local, &ll) == 0) {
        inet_ntoa_r(local.sin_addr, here, sizeof here);
    }
    if (peer) {
        peer[0] = 0;
        if (getpeername(fd, (struct sockaddr *)&remote, &rl) == 0) {
            inet_ntoa_r(remote.sin_addr, peer, peer_len);
        }
    }
    if (strcmp(here, "192.168.7.1") == 0) {
        return ZONE_CABLE;
    }
    if (aos_hal_net_ap_active() && strcmp(here, aos_hal_net_ap_ip()) == 0) {
        return ZONE_AP;
    }
    if (here[0] && strcmp(here, aos_hal_net_ip()) == 0 && aos_access_is_trusted(aos_hal_net_ssid())) {
        return ZONE_TRUSTED;
    }
    return ZONE_OTHER;          /* another network, or one we cannot tell: the strict rule */
}

/* The session cookie, aos_s=<32 hex>, or empty. */
static void session_cookie(httpd_req_t *req, char *out, size_t len)
{
    out[0] = 0;
    char cookie[AOS_ACCESS_ID_LEN + 1];
    size_t n = sizeof cookie;
    if (httpd_req_get_cookie_val(req, "aos_s", cookie, &n) == ESP_OK) {
        snprintf(out, len, "%s", cookie);
    }
}

static bool has_token(httpd_req_t *req)
{
    char auth[16 + AOS_ACCESS_ID_LEN + 1];
    return httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof auth) == ESP_OK &&
           strncasecmp(auth, "Bearer ", 7) == 0 && aos_access_token_ok(auth + 7);
}

static bool authorized(httpd_req_t *req, zone_t zone)
{
    if (zone != ZONE_OTHER) {
        return true;
    }
    char s[AOS_ACCESS_ID_LEN + 1];
    session_cookie(req, s, sizeof s);
    return (s[0] && aos_access_session_ok(s)) || has_token(req);
}

/* What any browser may ask without being let in: whether it has to, to be
 * let in, and the language the login is drawn in. */
static bool open_to_all(httpd_req_t *req)
{
    static const char *const OPEN[] = { "/api/auth", "/api/pedir", "/api/salir" };
    const char *uri = req->uri;
    size_t n = strcspn(uri, "?");
    if (req->method == HTTP_GET && n == 9 && strncmp(uri, "/api/lang", 9) == 0) {
        return true;            /* reading it; setting it is the watch's */
    }
    for (size_t i = 0; i < sizeof OPEN / sizeof OPEN[0]; i++) {
        if (n == strlen(OPEN[i]) && strncmp(uri, OPEN[i], n) == 0) {
            return true;
        }
    }
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

    if (!open_to_all(req) && !authorized(req, zone_of(req, NULL, 0))) {
        ESP_LOGW(TAG, "refused %s: not let in on this network", req->uri);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"login\"}");
        return false;
    }
    return true;
}

/* ---- the API of the login ---------------------------------------------------- */

/* GET /api/auth: {"zona","necesita","tiene","red","redes":[...],"sesiones"} */
esp_err_t aos_web_auth_handler(httpd_req_t *req)
{
    zone_t z = zone_of(req, NULL, 0);
    char buf[512];
    int n = snprintf(buf, sizeof buf, "{\"zona\":\"%s\",\"necesita\":%s,\"tiene\":%s,\"red\":\"",
                     ZONE_NAME[z], z == ZONE_OTHER ? "true" : "false",
                     authorized(req, z) ? "true" : "false");
    /* SSIDs are the neighbours' text: escaped */
    const char *ssid = aos_hal_net_state() == AOS_NET_CONNECTED ? aos_hal_net_ssid() : "";
    for (const char *p = ssid; *p && n < (int)sizeof buf - 8; p++) {
        if (*p == '"' || *p == '\\') buf[n++] = '\\';
        if ((unsigned char)*p >= 0x20) buf[n++] = *p;
    }
    n += snprintf(buf + n, sizeof buf - n, "\",\"redes\":[");
    char net[33];
    for (int i = 0; aos_access_trusted_at(i, net, sizeof net) && n < (int)sizeof buf - 48; i++) {
        buf[n++] = i ? ',' : ' ';
        buf[n++] = '"';
        for (const char *p = net; *p && n < (int)sizeof buf - 8; p++) {
            if (*p == '"' || *p == '\\') buf[n++] = '\\';
            if ((unsigned char)*p >= 0x20) buf[n++] = *p;
        }
        buf[n++] = '"';
    }
    snprintf(buf + n, sizeof buf - n, "],\"sesiones\":%d}", aos_access_session_count());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, buf);
}

/* POST /api/pedir: ask the watch -> {"id":N,"codigo":"1234"}; 409 while
 * another browser waits; 429 {"espera":s} after refusals.
 * GET /api/pedir?id=N -> {"estado":"esperando|aprobado|rechazado|vencido"};
 * "aprobado" comes with the session cookie. */
esp_err_t aos_web_ask_handler(httpd_req_t *req)
{
    char json[96];
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (req->method == HTTP_POST) {
        char peer[16];
        zone_of(req, peer, sizeof peer);
        uint32_t id = 0, wait = 0;
        uint16_t code = 0;
        if (!aos_access_ask(peer, &id, &code, &wait)) {
            if (wait) {
                httpd_resp_set_status(req, "429 Too Many Requests");
                snprintf(json, sizeof json, "{\"ok\":false,\"espera\":%u}", (unsigned)wait);
            } else {
                httpd_resp_set_status(req, "409 Conflict");
                snprintf(json, sizeof json, "{\"ok\":false,\"error\":\"otro pedido en curso\"}");
            }
            return httpd_resp_sendstr(req, json);
        }
        ESP_LOGI(TAG, "%s asks to be let in (code %04u)", peer, (unsigned)code);
        snprintf(json, sizeof json, "{\"ok\":true,\"id\":%u,\"codigo\":\"%04u\"}",
                 (unsigned)id, (unsigned)code);
        return httpd_resp_sendstr(req, json);
    }
    char query[32] = "", v[16] = "";
    uint32_t id = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK &&
        httpd_query_key_value(query, "id", v, sizeof v) == ESP_OK) {
        id = (uint32_t)strtoul(v, NULL, 10);
    }
    char session[AOS_ACCESS_ID_LEN + 1] = "";
    aos_access_ask_t st = aos_access_ask_state(id, session, sizeof session);
    static const char *const NAME[] = { "esperando", "aprobado", "rechazado", "vencido" };
    /* out here and not in the if: httpd_resp_set_hdr keeps the pointer, and
     * the header is only written when the answer is sent */
    char cookie[96];
    if (st == AOS_ACCESS_ASK_APPROVED) {
        /* HttpOnly: the page's scripts never see it; Strict: no other site
         * sends it; no Max-Age: it is the browser's session, like the watch's */
        snprintf(cookie, sizeof cookie, "aos_s=%s; Path=/; HttpOnly; SameSite=Strict", session);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);
        ESP_LOGI(TAG, "a browser was let in");
    }
    snprintf(json, sizeof json, "{\"estado\":\"%s\"}", NAME[st]);
    return httpd_resp_sendstr(req, json);
}

/* POST /api/salir: ends this browser's session. */
esp_err_t aos_web_logout_handler(httpd_req_t *req)
{
    char s[AOS_ACCESS_ID_LEN + 1];
    session_cookie(req, s, sizeof s);
    if (s[0]) {
        aos_access_session_end(s);
    }
    httpd_resp_set_hdr(req, "Set-Cookie", "aos_s=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

/* GET /api/token: the scripts' token (whoever may use the API may use it
 * from a script too). POST /api/token: a new one; the old stops working. */
esp_err_t aos_web_token_handler(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        aos_access_token_new();
    }
    char tok[AOS_ACCESS_ID_LEN + 1], json[80];
    aos_access_token(tok, sizeof tok);
    snprintf(json, sizeof json, "{\"token\":\"%s\"}", tok);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}
