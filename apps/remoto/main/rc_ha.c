/*
 * Remoto - the conversation with Home Assistant. See rc_ha.h.
 */
#include "rc_ha.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Ceiling of a command's response. The body is not looked at -the HTTP code is
 * enough- but Home Assistant answers with the array of states that changed and
 * that can be long: the HAL cuts off on filling the buffer, which is exactly
 * what we want. */
#define CMD_MAX     1024

/* The template's response is the states separated by bars. Forty entries of
 * twenty characters fit into this with room to spare. */
#define TPL_MAX     1536

static void note(rc_ha_t *h, rc_ha_status_t st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(h->last, sizeof(h->last), fmt, ap);
    va_end(ap);
    h->status    = st;
    h->status_ms = (uint32_t)aos_hal_uptime_ms();
}

bool rc_ha_load(rc_ha_t *h)
{
    memset(h, 0, sizeof(*h));

    char url[80] = {0};
    char token[280] = {0};
    aos_hal_pref_get_str(RC_KEY_URL, url, sizeof(url));
    aos_hal_pref_get_str(RC_KEY_TOKEN, token, sizeof(token));

    /* The trailing slash is stripped here and not in the portal because the
     * endpoint can also be typed by hand and "http://ha:8123/" is what you
     * copy out of the browser's address bar. */
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        url[--n] = 0;
    }

    /* BOTH schemes, and it is not a transition: they are the two real cases.
     *
     * Against a Home Assistant on the LAN, http:// is the norm —it has a
     * self-signed certificate or none at all, and with VERIFY_REQUIRED an
     * https against that does not go out—. The token travels in the clear, and
     * on your own network against your own server that is acceptable: it is
     * the same network everything else already travels over.
     *
     * Against an installation exposed to the internet (Nabu Casa, a proxy with
     * a real certificate) it is the other way round: there https:// is not
     * optional, because a Home Assistant long-lived token does not expire and
     * gives control of the whole house.
     *
     * The user chooses when typing the address, being the only one who knows
     * which of the two cases they have. The app neither guesses nor
     * converts. */
    bool plano   = (strncmp(url, "http://", 7) == 0);
    bool cifrado = (strncmp(url, "https://", 8) == 0);
    if (n < 8 || (!plano && !cifrado)) {
        note(h, RC_HA_ERROR, "%s", _("falta la direccion de Home Assistant"));
        return false;
    }
    if (!token[0]) {
        note(h, RC_HA_ERROR, "%s", _("falta el token"));
        return false;
    }

    snprintf(h->base, sizeof(h->base), "%s", url);
    snprintf(h->hdr, sizeof(h->hdr), "Authorization: Bearer %s\r\n", token);
    h->configured = true;
    h->status     = RC_HA_IDLE;
    return true;
}

/* -------------------------------------------------------------------------- */

static bool start_service(rc_ha_t *h, const char *service, const char *body)
{
    if (!h->configured) {
        note(h, RC_HA_ERROR, "%s", _("sin configurar"));
        return false;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        note(h, RC_HA_ERROR, "%s", _("sin wifi"));
        return false;
    }
    if (h->req_cmd) {
        /* A tap while the previous one is still in flight. The old one is
         * released: what the user has just pressed matters more than the
         * answer to what they pressed a second ago, and there are only three
         * HTTP slots. */
        aos_hal_http_release(h->req_cmd);
        h->req_cmd = 0;
    }

    char domain[24], name[40];
    if (!rc_split_service(service, domain, sizeof(domain), name, sizeof(name))) {
        note(h, RC_HA_ERROR, _("servicio invalido: %s"), service);
        return false;
    }

    char url[192];
    snprintf(url, sizeof(url), "%s/api/services/%s/%s", h->base, domain, name);

    int id = aos_hal_http_request("POST", url, h->hdr, body,
                                  "application/json", CMD_MAX);
    if (id <= 0) {
        note(h, RC_HA_ERROR, _("no se pudo mandar (%d)"), id);
        return false;
    }
    h->req_cmd = id;
    note(h, RC_HA_SENDING, "%s...", service);
    return true;
}

bool rc_ha_call(rc_ha_t *h, const rc_action_t *a)
{
    if (!a || a->type != RC_ACT_SERVICE || !a->service[0]) {
        return false;
    }
    char body[RC_LEN_ENTITY + RC_LEN_DATA + 32];
    rc_action_body(a, body, sizeof(body));
    return start_service(h, a->service, body);
}

bool rc_ha_call_value(rc_ha_t *h, const char *service, const char *entity,
                      const char *field, int value)
{
    if (!service || !service[0] || !field || !field[0]) {
        return false;
    }
    char body[RC_LEN_ENTITY + RC_LEN_ATTR + 48];
    if (entity && entity[0]) {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"%s\":%d}",
                 entity, field, value);
    } else {
        snprintf(body, sizeof(body), "{\"%s\":%d}", field, value);
    }
    return start_service(h, service, body);
}

bool rc_ha_poll(rc_ha_t *h, const char *tpl)
{
    if (!h->configured || !tpl || !tpl[0] || h->req_state) {
        return false;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        return false;
    }

    /* The body is {"template":"..."} and the template uses single quotes, not
     * double ones: rc_build_template() builds it with state_attr('x','y')
     * precisely so nothing has to be escaped here. */
    size_t need = strlen(tpl) + 24;
    char  *body = malloc(need);
    if (!body) {
        return false;
    }
    snprintf(body, need, "{\"template\":\"%s\"}", tpl);

    char url[128];
    snprintf(url, sizeof(url), "%s/api/template", h->base);

    int id = aos_hal_http_request("POST", url, h->hdr, body,
                                  "application/json", TPL_MAX);
    free(body);
    if (id <= 0) {
        return false;
    }
    h->req_state = id;
    return true;
}

/* -------------------------------------------------------------------------- */

bool rc_ha_pump(rc_ha_t *h, rc_profile_t *p)
{
    bool fresh = false;

    if (h->req_cmd && aos_hal_http_state(h->req_cmd) != AOS_HTTP_BUSY) {
        int code = aos_hal_http_status(h->req_cmd);
        if (aos_hal_http_state(h->req_cmd) == AOS_HTTP_DONE) {
            note(h, RC_HA_OK, "%s", _("listo"));
            /* Home Assistant has already applied the change, but the new state
             * takes a moment to settle (a zigbee light answers afterwards). The
             * refresh is requested after a breath, not instantly. */
            h->next_poll_ms = (uint32_t)aos_hal_uptime_ms() + 800;
        } else if (code == 401 || code == 403) {
            note(h, RC_HA_ERROR, _("token rechazado (%d)"), code);
        } else if (code == 400) {
            note(h, RC_HA_ERROR, "%s", _("Home Assistant no entendio la llamada"));
        } else if (code == 404) {
            note(h, RC_HA_ERROR, "%s", _("ese servicio no existe"));
        } else if (code < 0) {
            note(h, RC_HA_ERROR, _("no llegue a Home Assistant (%d)"), code);
        } else {
            note(h, RC_HA_ERROR, _("error %d"), code);
        }
        aos_hal_http_release(h->req_cmd);
        h->req_cmd = 0;
    }

    if (h->req_state && aos_hal_http_state(h->req_state) != AOS_HTTP_BUSY) {
        if (aos_hal_http_state(h->req_state) == AOS_HTTP_DONE && p) {
            const char *body = aos_hal_http_body(h->req_state);
            int         len  = aos_hal_http_len(h->req_state);
            if (body && len > 0) {
                rc_apply_template(p, body, len);
                fresh = true;
            }
        } else if (h->status != RC_HA_SENDING) {
            /* The result of a command in flight is not overwritten: the user
             * is watching that and does not care about the background
             * refresh. */
            int code = aos_hal_http_status(h->req_state);
            if (code == 401 || code == 403) {
                note(h, RC_HA_ERROR, _("token rechazado (%d)"), code);
            }
        }
        aos_hal_http_release(h->req_state);
        h->req_state = 0;
    }
    return fresh;
}

void rc_ha_abort(rc_ha_t *h)
{
    /* It always goes, including with the request in flight: the HAL finishes
     * and cleans up by itself, but the slot has to be given back. There are
     * three for the whole system. */
    if (h->req_cmd) {
        aos_hal_http_release(h->req_cmd);
        h->req_cmd = 0;
    }
    if (h->req_state) {
        aos_hal_http_release(h->req_state);
        h->req_state = 0;
    }
}
