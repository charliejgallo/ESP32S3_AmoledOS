/*
 * AmoledOS - who may use the web portal. See aos_access.h and
 * docs/SECURITY.md.
 *
 * Preferences: web_redes, the trusted networks one per line; web_token, the
 * scripts' token; web_init, set once the first network was trusted (or the
 * user changed the list), so the first-run rule never runs again.
 */
#include "aos_access.h"
#include "aos_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef AOS_SIM
#include <time.h>
#define LOCK()      do { } while (0)    /* no HTTP server in the simulator */
#define UNLOCK()    do { } while (0)
static uint32_t random32(void)
{
    static bool seeded;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }
    return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}
#else
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_mx;
#define LOCK()      xSemaphoreTake(s_mx, portMAX_DELAY)
#define UNLOCK()    xSemaphoreGive(s_mx)
#define random32()  esp_random()
#endif

#define PREF_NETS   "web_redes"
#define PREF_TOKEN  "web_token"
#define PREF_INIT   "web_init"

#define ASK_TTL_MS      60000       /* a request unanswered this long is gone */
#define ANSWER_TTL_MS   30000       /* an answer not collected this long is gone */
#define WAIT_MAX_S      300

static bool     s_ready;
static char     s_nets[AOS_ACCESS_TRUSTED_MAX][33];
static int      s_n_nets;
static char     s_token[AOS_ACCESS_ID_LEN + 1];

static struct {
    char     id[AOS_ACCESS_ID_LEN + 1];
    uint32_t serial;                    /* the oldest goes when they are all taken */
} s_sessions[AOS_ACCESS_SESSIONS_MAX];
static uint32_t s_serial;

static struct {
    bool     active;
    uint32_t id;
    uint16_t code;
    char     from[16];
    uint64_t since_ms;
    aos_access_ask_t state;
    char     session[AOS_ACCESS_ID_LEN + 1];
} s_ask;
static uint32_t s_denials;              /* in a row: each doubles the wait */
static uint64_t s_wait_until_ms;

static void random_hex(char *out, size_t hex_len)
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t i = 0; i < hex_len; i += 8) {
        uint32_t r = random32();
        for (size_t j = 0; j < 8 && i + j < hex_len; j++) {
            out[i + j] = HEX[(r >> (4 * j)) & 0xF];
        }
    }
    out[hex_len] = 0;
}

/* The same time whatever the first difference: a session id is not guessed
 * one character at a time from how fast the watch says no. */
static bool same_id(const char *a, const char *b)
{
    if (!a || !b || strlen(a) != AOS_ACCESS_ID_LEN || strlen(b) != AOS_ACCESS_ID_LEN) {
        return false;
    }
    unsigned diff = 0;
    for (int i = 0; i < AOS_ACCESS_ID_LEN; i++) {
        diff |= (unsigned)(a[i] ^ b[i]);
    }
    return diff == 0;
}

static void nets_save(void)
{
    char buf[AOS_ACCESS_TRUSTED_MAX * 34 + 1] = "";
    size_t used = 0;
    for (int i = 0; i < s_n_nets; i++) {
        used += (size_t)snprintf(buf + used, sizeof buf - used, "%s%s", i ? "\n" : "", s_nets[i]);
    }
    aos_hal_pref_set_str(PREF_NETS, buf);
    aos_hal_pref_set_i32(PREF_INIT, 1);
}

void aos_access_init(void)
{
    if (s_ready) {
        return;
    }
#ifndef AOS_SIM
    s_mx = xSemaphoreCreateMutex();
#endif
    char buf[AOS_ACCESS_TRUSTED_MAX * 34 + 1] = "";
    s_n_nets = 0;
    if (aos_hal_pref_get_str(PREF_NETS, buf, sizeof buf)) {
        char *save = NULL;
        for (char *line = strtok_r(buf, "\n", &save); line && s_n_nets < AOS_ACCESS_TRUSTED_MAX;
             line = strtok_r(NULL, "\n", &save)) {
            snprintf(s_nets[s_n_nets++], sizeof s_nets[0], "%s", line);
        }
    }
    if (!aos_hal_pref_get_str(PREF_TOKEN, s_token, sizeof s_token) ||
        strlen(s_token) != AOS_ACCESS_ID_LEN) {
        random_hex(s_token, AOS_ACCESS_ID_LEN);
        aos_hal_pref_set_str(PREF_TOKEN, s_token);
    }
    s_ready = true;
}

/* ---- trusted networks ------------------------------------------------------ */

bool aos_access_is_trusted(const char *ssid)
{
    aos_access_init();
    if (!ssid || !ssid[0]) {
        return false;
    }
    LOCK();
    bool yes = false;
    for (int i = 0; i < s_n_nets && !yes; i++) {
        yes = strcmp(s_nets[i], ssid) == 0;
    }
    UNLOCK();
    return yes;
}

void aos_access_set_trusted(const char *ssid, bool trusted)
{
    aos_access_init();
    if (!ssid || !ssid[0]) {
        return;
    }
    LOCK();
    int at = -1;
    for (int i = 0; i < s_n_nets; i++) {
        if (strcmp(s_nets[i], ssid) == 0) {
            at = i;
        }
    }
    if (trusted && at < 0) {
        if (s_n_nets == AOS_ACCESS_TRUSTED_MAX) {
            memmove(s_nets[0], s_nets[1], sizeof s_nets[0] * (AOS_ACCESS_TRUSTED_MAX - 1));
            s_n_nets--;                 /* full: the oldest goes */
        }
        snprintf(s_nets[s_n_nets++], sizeof s_nets[0], "%s", ssid);
    } else if (!trusted && at >= 0) {
        memmove(s_nets[at], s_nets[at + 1], sizeof s_nets[0] * (size_t)(s_n_nets - at - 1));
        s_n_nets--;
    }
    nets_save();
    UNLOCK();
}

bool aos_access_trusted_at(int i, char *out, size_t len)
{
    aos_access_init();
    LOCK();
    bool ok = i >= 0 && i < s_n_nets;
    if (ok) {
        snprintf(out, len, "%s", s_nets[i]);
    }
    UNLOCK();
    return ok;
}

/* ---- sessions and the token ------------------------------------------------ */

bool aos_access_session_ok(const char *id)
{
    aos_access_init();
    LOCK();
    bool ok = false;
    for (int i = 0; i < AOS_ACCESS_SESSIONS_MAX && !ok; i++) {
        ok = s_sessions[i].id[0] && same_id(s_sessions[i].id, id);
    }
    UNLOCK();
    return ok;
}

void aos_access_session_end(const char *id)
{
    aos_access_init();
    LOCK();
    for (int i = 0; i < AOS_ACCESS_SESSIONS_MAX; i++) {
        if (s_sessions[i].id[0] && same_id(s_sessions[i].id, id)) {
            memset(&s_sessions[i], 0, sizeof s_sessions[i]);
        }
    }
    UNLOCK();
}

int aos_access_session_count(void)
{
    aos_access_init();
    LOCK();
    int n = 0;
    for (int i = 0; i < AOS_ACCESS_SESSIONS_MAX; i++) {
        n += s_sessions[i].id[0] != 0;
    }
    UNLOCK();
    return n;
}

void aos_access_sessions_close_all(void)
{
    aos_access_init();
    LOCK();
    memset(s_sessions, 0, sizeof s_sessions);
    UNLOCK();
}

/* with the lock held */
static void session_new(char *out)
{
    int slot = 0;
    for (int i = 0; i < AOS_ACCESS_SESSIONS_MAX; i++) {
        if (!s_sessions[i].id[0]) {
            slot = i;
            break;
        }
        if (s_sessions[i].serial < s_sessions[slot].serial) {
            slot = i;
        }
    }
    random_hex(s_sessions[slot].id, AOS_ACCESS_ID_LEN);
    s_sessions[slot].serial = ++s_serial;
    memcpy(out, s_sessions[slot].id, AOS_ACCESS_ID_LEN + 1);
}

bool aos_access_token_ok(const char *token)
{
    aos_access_init();
    LOCK();
    bool ok = same_id(s_token, token);
    UNLOCK();
    return ok;
}

void aos_access_token(char *out, size_t len)
{
    aos_access_init();
    LOCK();
    snprintf(out, len, "%s", s_token);
    UNLOCK();
}

void aos_access_token_new(void)
{
    aos_access_init();
    LOCK();
    random_hex(s_token, AOS_ACCESS_ID_LEN);
    aos_hal_pref_set_str(PREF_TOKEN, s_token);
    UNLOCK();
}

/* ---- asking the watch ------------------------------------------------------ */

/* with the lock held: forget a request nobody answered or collected */
static void ask_expire(uint64_t now)
{
    if (!s_ask.active) {
        return;
    }
    uint64_t ttl = s_ask.state == AOS_ACCESS_ASK_PENDING ? ASK_TTL_MS : ANSWER_TTL_MS;
    if (now - s_ask.since_ms > ttl) {
        memset(&s_ask, 0, sizeof s_ask);
    }
}

bool aos_access_ask(const char *from, uint32_t *id, uint16_t *code, uint32_t *wait_s)
{
    aos_access_init();
    uint64_t now = aos_hal_uptime_ms();
    *wait_s = 0;
    LOCK();
    ask_expire(now);
    if (now < s_wait_until_ms) {
        *wait_s = (uint32_t)((s_wait_until_ms - now + 999) / 1000);
        UNLOCK();
        return false;
    }
    if (s_ask.active && s_ask.state == AOS_ACCESS_ASK_PENDING) {
        if (strcmp(s_ask.from, from) != 0) {
            UNLOCK();
            return false;           /* someone else is waiting for an answer */
        }
        *id = s_ask.id;             /* the same browser again: the same request */
        *code = s_ask.code;
        UNLOCK();
        return true;
    }
    memset(&s_ask, 0, sizeof s_ask);
    s_ask.active = true;
    s_ask.id = random32() | 1;
    s_ask.code = (uint16_t)(1000 + random32() % 9000);
    snprintf(s_ask.from, sizeof s_ask.from, "%s", from);
    s_ask.since_ms = now;
    s_ask.state = AOS_ACCESS_ASK_PENDING;
    *id = s_ask.id;
    *code = s_ask.code;
    UNLOCK();
    return true;
}

aos_access_ask_t aos_access_ask_state(uint32_t id, char *session, size_t len)
{
    aos_access_init();
    LOCK();
    ask_expire(aos_hal_uptime_ms());
    aos_access_ask_t st = AOS_ACCESS_ASK_GONE;
    if (s_ask.active && s_ask.id == id) {
        st = s_ask.state;
        if (st == AOS_ACCESS_ASK_APPROVED) {
            snprintf(session, len, "%s", s_ask.session);
        }
        if (st != AOS_ACCESS_ASK_PENDING) {
            memset(&s_ask, 0, sizeof s_ask);    /* told once */
        }
    }
    UNLOCK();
    return st;
}

bool aos_access_pending(uint16_t *code, char *from, size_t from_len)
{
    aos_access_init();
    LOCK();
    ask_expire(aos_hal_uptime_ms());
    bool yes = s_ask.active && s_ask.state == AOS_ACCESS_ASK_PENDING;
    if (yes) {
        *code = s_ask.code;
        snprintf(from, from_len, "%s", s_ask.from);
    }
    UNLOCK();
    return yes;
}

void aos_access_answer(bool allow)
{
    aos_access_init();
    LOCK();
    if (s_ask.active && s_ask.state == AOS_ACCESS_ASK_PENDING) {
        s_ask.since_ms = aos_hal_uptime_ms();
        if (allow) {
            session_new(s_ask.session);
            s_ask.state = AOS_ACCESS_ASK_APPROVED;
            s_denials = 0;
        } else {
            s_ask.state = AOS_ACCESS_ASK_DENIED;
            /* 10, 20, 40 s... up to five minutes before the next request */
            uint32_t wait = 10u << (s_denials < 5 ? s_denials : 5);
            if (wait > WAIT_MAX_S) {
                wait = WAIT_MAX_S;
            }
            s_denials++;
            s_wait_until_ms = s_ask.since_ms + (uint64_t)wait * 1000;
        }
    }
    UNLOCK();
}
