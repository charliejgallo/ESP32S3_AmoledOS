/*
 * AmoledOS - /api/ble: the apps' Bluetooth LE scanner, measured from the Mac.
 *
 * The same idea as /api/link: the numbers that settle a design question
 * (does scanning at 100 % starve the phone or the WiFi? how many packets a
 * second does the radio hand over? what does it cost in RAM?) come from the
 * watch, not from a guess. It drives the real HAL calls an app makes
 * (aos_hal_ble_*), so what it measures is what the app gets.
 *
 * While the bench scans it is the scanner's reader: an esp_timer drains the
 * ring five times a second and counts. An app that scans at the same time
 * would see half of the packets each, so this is for measuring with no BLE
 * app open.
 *
 * GET /api/ble                          counters, state and memory
 * GET /api/ble?do=bt&on=1               Bluetooth on or off (the Settings switch)
 * GET /api/ble?do=start&duty=100&active=1
 * GET /api/ble?do=stop | do=reset
 * GET /api/ble?do=connect&addr=AA:BB:CC:DD:EE:FF&type=1
 * GET /api/ble?do=disconnect | do=attrs | do=read&h=N
 * GET /api/ble?do=top                   the 24 loudest addresses heard
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "aos_hal.h"

#define SEEN_MAX   2048             /* distinct addresses remembered, PSRAM */
#define DRAIN_MAX  64

typedef struct {
    uint8_t  addr[6];
    uint8_t  type;
    uint8_t  used;
    int8_t   rssi_max;
    int8_t   rssi_last;
    uint16_t n;
} seen_t;

static seen_t *s_seen;              /* PSRAM */
static int s_nseen;
static aos_ble_adv_t *s_buf;        /* PSRAM, DRAIN_MAX */
static esp_timer_handle_t s_timer;
static bool s_on;
static int s_duty = 100, s_active = 1;
static uint32_t s_total, s_by_kind[5], s_t0;
static uint32_t s_full_seen;        /* packets whose address did not fit */
static uint32_t s_win_t, s_win_n, s_rate;   /* packets/s over the last ~5 s */
static uint32_t s_max_batch;        /* the most packets one drain found */

static bool query_int(httpd_req_t *req, const char *key, int *out)
{
    char query[160];
    char value[24];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return false;
    }
    *out = atoi(value);
    return true;
}

static bool query_str(httpd_req_t *req, const char *key, char *out, size_t len)
{
    char query[160];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, len) == ESP_OK;
}

static void seen_note(const aos_ble_adv_t *a)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ a->addr[i]) * 16777619u;
    for (int k = 0; k < SEEN_MAX; k++) {
        seen_t *s = &s_seen[(h + k) % SEEN_MAX];
        if (!s->used) {
            if (s_nseen >= SEEN_MAX * 3 / 4) { s_full_seen++; return; }
            memcpy(s->addr, a->addr, 6);
            s->type = a->addr_type;
            s->used = 1;
            s->rssi_max = a->rssi;
            s->rssi_last = a->rssi;
            s->n = 1;
            s_nseen++;
            return;
        }
        if (memcmp(s->addr, a->addr, 6) == 0) {
            if (a->rssi > s->rssi_max) s->rssi_max = a->rssi;
            s->rssi_last = a->rssi;
            if (s->n < 0xFFFF) s->n++;
            return;
        }
    }
}

static void drain(void *arg)
{
    (void)arg;
    uint32_t batch = 0;
    int n;
    while ((n = aos_hal_ble_scan_read(s_buf, DRAIN_MAX)) > 0) {
        for (int i = 0; i < n; i++) {
            s_total++;
            s_by_kind[s_buf[i].kind < 5 ? s_buf[i].kind : 3]++;
            seen_note(&s_buf[i]);
        }
        batch += n;
    }
    if (batch > s_max_batch) s_max_batch = batch;
    uint32_t now = aos_hal_uptime_ms();
    if (now - s_win_t >= 5000) {
        s_rate = (s_total - s_win_n) * 1000 / (now - s_win_t);
        s_win_t = now;
        s_win_n = s_total;
    }
}

static void counters_reset(void)
{
    if (s_seen) memset(s_seen, 0, sizeof(seen_t) * SEEN_MAX);
    s_nseen = 0;
    s_total = 0;
    s_full_seen = 0;
    s_max_batch = 0;
    s_rate = 0;
    memset(s_by_kind, 0, sizeof s_by_kind);
    s_t0 = s_win_t = aos_hal_uptime_ms();
    s_win_n = 0;
}

static const char *bench_start(void)
{
    if (!s_seen) s_seen = heap_caps_calloc(SEEN_MAX, sizeof(seen_t), MALLOC_CAP_SPIRAM);
    if (!s_buf) s_buf = heap_caps_calloc(DRAIN_MAX, sizeof(aos_ble_adv_t), MALLOC_CAP_SPIRAM);
    if (!s_seen || !s_buf) return "no memory";
    if (!s_timer) {
        const esp_timer_create_args_t a = { .callback = drain, .name = "ble_bench" };
        if (esp_timer_create(&a, &s_timer) != ESP_OK) return "no timer";
    }
    if (!s_on) counters_reset();
    if (!aos_hal_ble_scan_start(s_active != 0, s_duty)) return "scan refused (Bluetooth off?)";
    if (!s_on) esp_timer_start_periodic(s_timer, 200 * 1000);
    s_on = true;
    return "scanning";
}

static void bench_stop(void)
{
    aos_hal_ble_scan_stop();
    if (s_timer && s_on) esp_timer_stop(s_timer);
    s_on = false;
}

static bool parse_addr(const char *text, uint8_t out[6])
{
    unsigned v[6];
    if (sscanf(text, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

static int cmp_loud(const void *a, const void *b)
{
    const seen_t *x = a, *y = b;
    if (x->used != y->used) return y->used - x->used;
    return y->rssi_max - x->rssi_max;
}

static void send_top(httpd_req_t *req)
{
    httpd_resp_send_chunk(req, ",\"top\":[", HTTPD_RESP_USE_STRLEN);
    if (!s_seen) {
        httpd_resp_send_chunk(req, "]", 1);
        return;
    }
    seen_t *copy = heap_caps_malloc(sizeof(seen_t) * SEEN_MAX, MALLOC_CAP_SPIRAM);
    if (!copy) {
        httpd_resp_send_chunk(req, "]", 1);
        return;
    }
    memcpy(copy, s_seen, sizeof(seen_t) * SEEN_MAX);
    qsort(copy, SEEN_MAX, sizeof(seen_t), cmp_loud);
    char line[112];
    for (int i = 0; i < 24 && copy[i].used; i++) {
        int n = snprintf(line, sizeof line,
                         "%s{\"addr\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"type\":%u,\"n\":%u,\"max\":%d,\"last\":%d}",
                         i ? "," : "", copy[i].addr[0], copy[i].addr[1], copy[i].addr[2], copy[i].addr[3],
                         copy[i].addr[4], copy[i].addr[5], copy[i].type, copy[i].n, copy[i].rssi_max,
                         copy[i].rssi_last);
        httpd_resp_send_chunk(req, line, n);
    }
    heap_caps_free(copy);
    httpd_resp_send_chunk(req, "]", 1);
}

static void send_attrs(httpd_req_t *req)
{
    httpd_resp_send_chunk(req, ",\"attrs\":[", HTTPD_RESP_USE_STRLEN);
    int total = aos_hal_ble_gatt_attrs(NULL, 0);
    aos_ble_attr_t *t = total > 0 ? heap_caps_malloc(sizeof(aos_ble_attr_t) * total, MALLOC_CAP_SPIRAM) : NULL;
    int n = t ? aos_hal_ble_gatt_attrs(t, total) : 0;
    char line[120];
    for (int i = 0; i < n; i++) {
        char uuid[40] = "";
        int p = 0;
        for (int k = t[i].uuid_len - 1; k >= 0 && p < (int)sizeof uuid - 3; k--) {
            p += snprintf(uuid + p, sizeof uuid - p, "%02X", t[i].uuid[k]);
        }
        int m = snprintf(line, sizeof line, "%s{\"k\":%u,\"h\":%u,\"end\":%u,\"props\":%u,\"uuid\":\"%s\"}",
                         i ? "," : "", t[i].kind, t[i].handle, t[i].end, t[i].props, uuid);
        httpd_resp_send_chunk(req, line, m);
    }
    if (t) heap_caps_free(t);
    httpd_resp_send_chunk(req, "]", 1);
}

/* The GATT events since the last call, as hex, so a read can be seen. */
static void send_events(httpd_req_t *req)
{
    httpd_resp_send_chunk(req, ",\"events\":[", HTTPD_RESP_USE_STRLEN);
    aos_ble_gatt_ev_t *ev = heap_caps_malloc(sizeof *ev, MALLOC_CAP_SPIRAM);
    char *line = heap_caps_malloc(1200, MALLOC_CAP_SPIRAM);
    int i = 0;
    while (ev && line && aos_hal_ble_gatt_events(ev, 1) == 1 && i < 16) {
        int p = snprintf(line, 1200, "%s{\"type\":%u,\"h\":%u,\"status\":%d,\"hex\":\"", i ? "," : "",
                         ev->type, ev->handle, ev->status);
        for (int k = 0; k < ev->len && p < 1190; k++) p += snprintf(line + p, 1200 - p, "%02x", ev->data[k]);
        p += snprintf(line + p, 1200 - p, "\"}");
        httpd_resp_send_chunk(req, line, p);
        i++;
    }
    if (ev) heap_caps_free(ev);
    if (line) heap_caps_free(line);
    httpd_resp_send_chunk(req, "]", 1);
}

esp_err_t aos_ble_api_handler(httpd_req_t *req)
{
    char what[16] = "";
    query_str(req, "do", what, sizeof(what));
    const char *result = "";
    bool top = false, attrs = false;

    if (strcmp(what, "bt") == 0) {
        int on = 1;
        query_int(req, "on", &on);
        if (!on) bench_stop();
        aos_hal_bt_enable(on != 0);
        result = on ? "bluetooth on" : "bluetooth off";
    } else if (strcmp(what, "start") == 0) {
        query_int(req, "duty", &s_duty);
        query_int(req, "active", &s_active);
        result = bench_start();
    } else if (strcmp(what, "stop") == 0) {
        bench_stop();
        result = "stopped";
    } else if (strcmp(what, "reset") == 0) {
        counters_reset();
        result = "reset";
    } else if (strcmp(what, "top") == 0) {
        top = true;
    } else if (strcmp(what, "connect") == 0) {
        char text[24] = "";
        uint8_t addr[6];
        int type = 0;
        query_int(req, "type", &type);
        if (query_str(req, "addr", text, sizeof text) && parse_addr(text, addr)) {
            result = aos_hal_ble_gatt_connect(addr, (uint8_t)type) ? "connecting" : "connect refused";
        } else {
            result = "bad addr";
        }
    } else if (strcmp(what, "disconnect") == 0) {
        aos_hal_ble_gatt_disconnect();
        result = "disconnecting";
    } else if (strcmp(what, "attrs") == 0) {
        attrs = true;
    } else if (strcmp(what, "read") == 0) {
        int h = 0;
        query_int(req, "h", &h);
        result = aos_hal_ble_gatt_read((uint16_t)h) ? "reading" : "read refused";
    }

    int reason = 0;
    aos_ble_gatt_state_t gs = aos_hal_ble_gatt_state(&reason);
    int8_t crssi = 0;
    bool has_rssi = aos_hal_ble_gatt_rssi(&crssi);
    uint32_t secs = s_on ? (aos_hal_uptime_ms() - s_t0) / 1000 : 0;
    char out[900];
    int n = snprintf(out, sizeof out,
        "{\"result\":\"%s\",\"bt\":%d,\"bt_enabled\":%s,\"scanning\":%s,\"bench\":%s,"
        "\"duty\":%d,\"active\":%d,\"secs\":%lu,\"packets\":%lu,\"rate\":%lu,\"avg_rate\":%lu,"
        "\"devices\":%d,\"devices_full\":%lu,\"lost\":%lu,\"max_batch\":%lu,"
        "\"kinds\":[%lu,%lu,%lu,%lu,%lu],"
        "\"gatt\":%d,\"gatt_reason\":%d,\"mtu\":%u,\"conn_rssi\":%d,"
        "\"internal_free\":%u,\"internal_min\":%u,\"exec_free\":%u,\"exec_largest\":%u,"
        "\"psram_free\":%u,\"wifi\":%d,\"wifi_rssi\":%d",
        result, (int)aos_hal_bt_state(), aos_hal_bt_enabled() ? "true" : "false",
        aos_hal_ble_scanning() ? "true" : "false", s_on ? "true" : "false",
        s_duty, s_active, (unsigned long)secs, (unsigned long)s_total, (unsigned long)s_rate,
        (unsigned long)(secs ? s_total / secs : 0), s_nseen, (unsigned long)s_full_seen,
        (unsigned long)aos_hal_ble_scan_lost(), (unsigned long)s_max_batch,
        (unsigned long)s_by_kind[0], (unsigned long)s_by_kind[1], (unsigned long)s_by_kind[2],
        (unsigned long)s_by_kind[3], (unsigned long)s_by_kind[4],
        (int)gs, reason, aos_hal_ble_gatt_mtu(), has_rssi ? crssi : 0,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (int)aos_hal_net_state(), aos_hal_net_rssi());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, out, n);
    if (top) send_top(req);
    if (attrs) send_attrs(req);
    send_events(req);
    httpd_resp_send_chunk(req, "}", 1);
    return httpd_resp_send_chunk(req, NULL, 0);
}
