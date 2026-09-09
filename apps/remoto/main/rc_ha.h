/*
 * Remoto - the conversation with Home Assistant
 *
 * Two things, and both over the REST API:
 *
 *   send   POST /api/services/<domain>/<service>  with {"entity_id": ...}
 *   read   POST /api/template  with a template that brings ALL the states of
 *          all the pages in a single response
 *
 * The second deserves an explanation. /api/states/<entity> could be asked for
 * one at a time, but each request over wifi costs close to a second (measured
 * on this board against open-meteo, and written down in the apps handoff):
 * with fifteen entities, refreshing the screen would take a quarter of a
 * minute and there would not be three HTTP slots enough. With a Jinja template
 * -"{{states('light.a')}}|{{states('light.b')}}|..."- Home Assistant answers
 * "on|off|..." in plain text: one request, a two-hundred-byte response and the
 * work of joining them done by the server, which has a CPU for that.
 *
 * The token travels in the clear, because the HAL deliberately does no TLS
 * (docs/DECISIONES.md). That is acceptable against your own Home Assistant on
 * the LAN and it is not against anything reachable from the internet. It is
 * also said on the portal's page, where it is entered.
 */
#pragma once

#include "rc_model.h"
#include <stdint.h>
#include <stdbool.h>

/* NVS keys. The only part of the remote that does not live in the microSD's
 * file: they are short, they are secret and they are written by the firmware,
 * not by the app. */
#define RC_KEY_URL      "rc_url"
#define RC_KEY_TOKEN    "rc_token"
#define RC_KEY_GEN      "rc_gen"    /* goes up every time the portal saves */

typedef enum {
    RC_HA_IDLE = 0,
    RC_HA_SENDING,
    RC_HA_OK,
    RC_HA_ERROR,
} rc_ha_status_t;

typedef struct {
    char base[80];          /* "http://192.168.1.10:8123", with no trailing slash.
                             * Both http:// and https:// are valid: the scheme
                             * is chosen by the user in the portal according to
                             * where their Home Assistant is. See
                             * rc_ha_load(). */
    char hdr[320];          /* "Authorization: Bearer ...\r\n" already built  */
    bool configured;

    int  req_cmd;           /* command request in flight, 0 = none */
    int  req_state;         /* states request in flight            */

    rc_ha_status_t status;
    char           last[56];        /* what happened, for the bottom line */
    uint32_t       status_ms;

    uint32_t next_poll_ms;  /* when the next refresh is due */
} rc_ha_t;

/* Reads the URL and the token from NVS. false if either is missing: the app
 * says so on screen instead of failing on every touch. */
bool rc_ha_load(rc_ha_t *h);

/* Calls a service. false if there was no slot or the action is no good. */
bool rc_ha_call(rc_ha_t *h, const rc_action_t *a);

/* The same, but with a numeric field: what the dial sends. */
bool rc_ha_call_value(rc_ha_t *h, const char *service, const char *entity,
                      const char *field, int value);

/* Asks for every state at once with rc_build_template()'s template. */
bool rc_ha_poll(rc_ha_t *h, const char *tpl);

/* Deals with whatever is in flight; called from the tick. Returns true if a
 * batch of states has just arrived and a repaint is needed. */
bool rc_ha_pump(rc_ha_t *h, rc_profile_t *p);

/* Releases everything in flight. Mandatory in destroy(). */
void rc_ha_abort(rc_ha_t *h);
