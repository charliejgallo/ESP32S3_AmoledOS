/*
 * AmoledOS - who may use the web portal (docs/SECURITY.md).
 *
 * Where a request comes in decides what it needs:
 *
 *   the USB cable (192.168.7.1)      nothing: whoever holds the cable holds the watch
 *   the watch's own access point     nothing: it has its own WPA2 password
 *   a trusted Wi-Fi network          nothing
 *   any other Wi-Fi network          a session the watch approved, or the token
 *
 * Trusted networks are marked on the watch, Settings > Portal web, and only
 * there: someone who got into the portal cannot open it further. The network
 * the watch is on when this firmware first runs becomes trusted, so the update
 * does not close the portal on its owner.
 *
 * On another network the portal asks the watch: the browser shows a code,
 * the watch wakes with "Allow this browser?" and the same code, and a tap
 * gives the browser a session (an HttpOnly cookie). Sessions live in RAM:
 * a restart closes them. The token is for scripts (Authorization: Bearer).
 *
 * The state is shared by the HTTP server's task and the UI's: every call
 * takes its own lock, and none of them touches LVGL.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_ACCESS_TRUSTED_MAX  8
#define AOS_ACCESS_SESSIONS_MAX 8
#define AOS_ACCESS_ID_LEN       32      /* hex characters of a session or the token */

/* Reads the trusted networks and the token from the preferences. Any task;
 * every other call does it the first time if nobody did. */
void aos_access_init(void);

/* ---- trusted networks ---- */
bool aos_access_is_trusted(const char *ssid);
void aos_access_set_trusted(const char *ssid, bool trusted);
/* Copies the i-th trusted network's name; false past the end. */
bool aos_access_trusted_at(int i, char *out, size_t len);

/* ---- sessions and the token ---- */
bool aos_access_session_ok(const char *id);
void aos_access_session_end(const char *id);
int  aos_access_session_count(void);
void aos_access_sessions_close_all(void);
bool aos_access_token_ok(const char *token);
void aos_access_token(char *out, size_t len);     /* the current one, AOS_ACCESS_ID_LEN + 1 */
void aos_access_token_new(void);

/* ---- asking the watch ---- */
typedef enum {
    AOS_ACCESS_ASK_PENDING = 0,
    AOS_ACCESS_ASK_APPROVED,    /* the session is in 'session', once */
    AOS_ACCESS_ASK_DENIED,
    AOS_ACCESS_ASK_GONE,        /* expired, replaced, or never was */
} aos_access_ask_t;

/* A browser asks to be let in, from address 'from'. Returns false when it
 * cannot ask now (another request is waiting, or the last ones were refused
 * and the watch makes it wait: *wait_s says how long). Otherwise *id is what
 * the browser asks about and *code what both screens show. */
bool aos_access_ask(const char *from, uint32_t *id, uint16_t *code, uint32_t *wait_s);
/* What became of request 'id'. When approved, the new session's id is copied
 * into 'session' (AOS_ACCESS_ID_LEN + 1) and the request is gone. */
aos_access_ask_t aos_access_ask_state(uint32_t id, char *session, size_t len);

/* For the UI: the request waiting for an answer, if there is one. */
bool aos_access_pending(uint16_t *code, char *from, size_t from_len);
void aos_access_answer(bool allow);

/* Called from the UI's tick: shows the request on the watch (aos_access_ui.c)
 * and, the first time, trusts the network the watch is on. */
void aos_access_ui_tick(void);
bool aos_access_ui_visible(void);
void aos_access_ui_cancel(void);

#ifdef __cplusplus
}
#endif
