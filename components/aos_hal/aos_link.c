/*
 * AmoledOS - Link, phase 1: raw ESP-NOW frames between two watches.
 *
 * What this layer is: esp_now started on the station interface, a broadcast
 * peer, a task that takes what the WiFi task hands over so nothing heavy
 * runs in the driver's callback, a ring the app drains from its tick, and
 * counters for everything. What it is not yet: discovery, pairing, the
 * reliable channel (docs/LINK.md, phases 2 and 3).
 *
 * It also carries the test protocol /api/link drives from the Mac: numbered
 * frames one way to count losses, and an echo so the round trip can be
 * measured. Diagnostics that stay, like /api/jpegbench: the next radio
 * question will want the same numbers again.
 *
 * Frames stay within ESP-NOW v1 (250 bytes) until v2 is negotiated in a
 * later phase; the version the controller supports is reported anyway.
 */
#include "aos_hal.h"

#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "aos_link";

#define LINK_QUEUE_LEN     16
#define LINK_RING_LEN      16
#define LINK_TEST_MAGIC0   'L'
#define LINK_TEST_SEND     'T'      /* numbered frame, one way          */
#define LINK_TEST_ECHO_REQ 'E'      /* numbered frame, please echo it    */
#define LINK_TEST_ECHO     'R'      /* the echo                          */
#define LINK_RTT_SLOTS     64

typedef struct {
    uint8_t  mac[6];
    int8_t   rssi;
    uint16_t len;
    uint8_t  data[ESP_NOW_MAX_DATA_LEN];
} link_frame_t;

static bool             s_started;
static QueueHandle_t    s_queue;         /* WiFi task -> link task */
static TaskHandle_t     s_task;
static volatile bool    s_stop;
static aos_link_stats_t s_stats;
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;

/* what the app reads */
static link_frame_t     s_ring[LINK_RING_LEN];
static volatile uint32_t s_ring_head, s_ring_tail;

/* the test's sender side */
static uint32_t s_test_seq;
static uint32_t s_rtt_sent_us[LINK_RTT_SLOTS];
static uint32_t s_rtt_seq[LINK_RTT_SLOTS];
/* the test's receiver side */
static uint32_t s_test_last_seq;
static bool     s_test_have_seq;

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ---- driver callbacks (WiFi task: copy and leave) ------------------------ */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!s_queue || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }
    link_frame_t f;
    memcpy(f.mac, info->src_addr, 6);
    f.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
    f.len  = (uint16_t)len;
    memcpy(f.data, data, (size_t)len);
    if (xQueueSend(s_queue, &f, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_mux);
        s_stats.dropped++;
        portEXIT_CRITICAL(&s_mux);
    }
}

static TaskHandle_t s_waiting_sender;   /* a sender blocked until its frame went out */

static void on_sent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    portENTER_CRITICAL(&s_mux);
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_stats.ack_ok++;
    } else {
        s_stats.ack_fail++;
    }
    portEXIT_CRITICAL(&s_mux);
    if (s_waiting_sender) {
        xTaskNotifyGive(s_waiting_sender);
    }
}

/* ---- the link task ------------------------------------------------------- */

static bool ensure_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = 0;                   /* the interface's current channel */
    peer.encrypt = false;
    esp_err_t e = esp_now_add_peer(&peer);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "add peer: %s", esp_err_to_name(e));
    }
    return e == ESP_OK;
}

static void ring_push(const link_frame_t *f)
{
    uint32_t next = (s_ring_head + 1) % LINK_RING_LEN;
    if (next == s_ring_tail) {
        portENTER_CRITICAL(&s_mux);
        s_stats.dropped++;
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_ring[s_ring_head] = *f;
    __sync_synchronize();
    s_ring_head = next;
}

static void handle_test(const link_frame_t *f)
{
    if (f->len < 6) {
        return;
    }
    uint32_t seq;
    memcpy(&seq, f->data + 2, 4);
    uint32_t now = (uint32_t)esp_timer_get_time();

    switch (f->data[1]) {
    case LINK_TEST_SEND:
    case LINK_TEST_ECHO_REQ:
        portENTER_CRITICAL(&s_mux);
        s_stats.test_rx++;
        if (s_test_have_seq && seq > s_test_last_seq + 1) {
            s_stats.test_lost += seq - s_test_last_seq - 1;
        }
        if (!s_test_have_seq || seq > s_test_last_seq) {
            s_test_last_seq = seq;
            s_test_have_seq = true;
        }
        portEXIT_CRITICAL(&s_mux);
        if (f->data[1] == LINK_TEST_ECHO_REQ) {
            uint8_t reply[6] = { LINK_TEST_MAGIC0, LINK_TEST_ECHO };
            memcpy(reply + 2, &seq, 4);
            if (ensure_peer(f->mac)) {
                esp_now_send(f->mac, reply, sizeof reply);
                portENTER_CRITICAL(&s_mux);
                s_stats.sent++;
                portEXIT_CRITICAL(&s_mux);
            }
        }
        break;
    case LINK_TEST_ECHO: {
        uint32_t slot = seq % LINK_RTT_SLOTS;
        if (s_rtt_seq[slot] == seq) {
            uint32_t rtt = now - s_rtt_sent_us[slot];
            portENTER_CRITICAL(&s_mux);
            s_stats.echo_rx++;
            s_stats.rtt_sum_us += rtt;
            if (s_stats.rtt_min_us == 0 || rtt < s_stats.rtt_min_us) s_stats.rtt_min_us = rtt;
            if (rtt > s_stats.rtt_max_us) s_stats.rtt_max_us = rtt;
            portEXIT_CRITICAL(&s_mux);
        }
        break;
    }
    default:
        break;
    }
}

static void link_task(void *arg)
{
    (void)arg;
    link_frame_t f;
    while (!s_stop) {
        if (xQueueReceive(s_queue, &f, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        portENTER_CRITICAL(&s_mux);
        s_stats.received++;
        s_stats.last_rssi = f.rssi;
        memcpy(s_stats.last_mac, f.mac, 6);
        portEXIT_CRITICAL(&s_mux);
        if (f.len >= 2 && f.data[0] == LINK_TEST_MAGIC0 &&
            (f.data[1] == LINK_TEST_SEND || f.data[1] == LINK_TEST_ECHO_REQ ||
             f.data[1] == LINK_TEST_ECHO)) {
            handle_test(&f);
        } else {
            ring_push(&f);
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ---- public ------------------------------------------------------------- */

bool aos_hal_link_start(void)
{
    if (s_started) {
        return true;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "wifi is not started: the link needs the radio on");
        return false;
    }
    if (!s_queue) {
        s_queue = xQueueCreate(LINK_QUEUE_LEN, sizeof(link_frame_t));
        if (!s_queue) {
            return false;
        }
    }
    esp_err_t e = esp_now_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(e));
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    ensure_peer(BROADCAST);

    memset(&s_stats, 0, sizeof s_stats);
    s_test_have_seq = false;
    s_ring_head = s_ring_tail = 0;
    esp_now_get_version(&s_stats.version);
    esp_read_mac(s_stats.own_mac, ESP_MAC_WIFI_STA);

    s_stop = false;
    if (xTaskCreate(link_task, "aos_link", 4096, NULL, 5, &s_task) != pdPASS) {
        esp_now_deinit();
        return false;
    }
    s_started = true;
    uint8_t ch = 0; wifi_second_chan_t sc;
    esp_wifi_get_channel(&ch, &sc);
    ESP_LOGI(TAG, "link up: esp-now v%lu, channel %u, mac %02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned long)s_stats.version, ch,
             s_stats.own_mac[0], s_stats.own_mac[1], s_stats.own_mac[2],
             s_stats.own_mac[3], s_stats.own_mac[4], s_stats.own_mac[5]);
    return true;
}

void aos_hal_link_stop(void)
{
    if (!s_started) {
        return;
    }
    s_stop = true;
    for (int i = 0; i < 30 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    s_started = false;
    ESP_LOGI(TAG, "link down: sent %lu, acked %lu, failed %lu, received %lu, dropped %lu",
             (unsigned long)s_stats.sent, (unsigned long)s_stats.ack_ok,
             (unsigned long)s_stats.ack_fail, (unsigned long)s_stats.received,
             (unsigned long)s_stats.dropped);
}

bool aos_hal_link_running(void)
{
    return s_started;
}

bool aos_hal_link_send(const uint8_t mac[6], const void *data, size_t len)
{
    if (!s_started || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return false;
    }
    const uint8_t *to = mac ? mac : BROADCAST;
    if (!ensure_peer(to)) {
        return false;
    }
    esp_err_t e = esp_now_send(to, data, len);
    portENTER_CRITICAL(&s_mux);
    if (e == ESP_OK) {
        s_stats.sent++;
    } else {
        s_stats.send_err++;
    }
    portEXIT_CRITICAL(&s_mux);
    return e == ESP_OK;
}

int aos_hal_link_recv(aos_link_frame_t *out)
{
    if (!out || s_ring_tail == s_ring_head) {
        return 0;
    }
    __sync_synchronize();
    const link_frame_t *f = &s_ring[s_ring_tail];
    memcpy(out->mac, f->mac, 6);
    out->rssi = f->rssi;
    out->len  = f->len > sizeof out->data ? sizeof out->data : f->len;
    memcpy(out->data, f->data, out->len);
    s_ring_tail = (s_ring_tail + 1) % LINK_RING_LEN;
    return (int)out->len;
}

bool aos_hal_link_stats(aos_link_stats_t *out)
{
    if (!out) {
        return false;
    }
    portENTER_CRITICAL(&s_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_mux);
    uint8_t ch = 0; wifi_second_chan_t sc;
    if (s_started && esp_wifi_get_channel(&ch, &sc) == ESP_OK) {
        out->channel = ch;
    }
    out->running = s_started;
    return true;
}

void aos_hal_link_stats_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    uint32_t v = s_stats.version;
    uint8_t mac[6];
    memcpy(mac, s_stats.own_mac, 6);
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.version = v;
    memcpy(s_stats.own_mac, mac, 6);
    s_test_have_seq = false;
    portEXIT_CRITICAL(&s_mux);
}

/* ---- the test: N numbered frames, spaced, optionally echoed -------------- */

typedef struct {
    uint8_t  mac[6];
    bool     broadcast;
    uint32_t n;
    uint32_t gap_ms;
    bool     echo;
    uint16_t len;
} link_test_t;

static volatile bool s_test_running;

static void test_task(void *arg)
{
    link_test_t t = *(link_test_t *)arg;
    free(arg);
    uint8_t frame[ESP_NOW_MAX_DATA_LEN];
    memset(frame, 0xA5, sizeof frame);
    int64_t t_start = esp_timer_get_time();
    frame[0] = LINK_TEST_MAGIC0;
    frame[1] = t.echo ? LINK_TEST_ECHO_REQ : LINK_TEST_SEND;
    for (uint32_t i = 0; i < t.n && s_started; i++) {
        uint32_t seq = ++s_test_seq;
        memcpy(frame + 2, &seq, 4);
        uint32_t slot = seq % LINK_RTT_SLOTS;
        s_rtt_seq[slot] = seq;
        s_rtt_sent_us[slot] = (uint32_t)esp_timer_get_time();
        /* gap 0 = as fast as the radio takes them: wait for the send
         * callback before the next one. Without the wait, esp_now_send()
         * refuses with NO_MEM once its queue is full: 395 of 1000 at 2 ms
         * apart, measured, none of them lost on the air. */
        if (t.gap_ms == 0) {
            s_waiting_sender = xTaskGetCurrentTaskHandle();
            ulTaskNotifyTake(pdTRUE, 0);            /* clear a stale one */
        }
        aos_hal_link_send(t.broadcast ? NULL : t.mac, frame, t.len);
        portENTER_CRITICAL(&s_mux);
        s_stats.test_tx++;
        portEXIT_CRITICAL(&s_mux);
        if (t.gap_ms) {
            vTaskDelay(pdMS_TO_TICKS(t.gap_ms));
        } else {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        }
    }
    s_waiting_sender = NULL;
    portENTER_CRITICAL(&s_mux);
    s_stats.test_ms = (uint32_t)((esp_timer_get_time() - t_start) / 1000);
    portEXIT_CRITICAL(&s_mux);
    s_test_running = false;
    vTaskDelete(NULL);
}

bool aos_hal_link_test(const uint8_t mac[6], uint32_t n, uint32_t gap_ms, bool echo, uint16_t len)
{
    if (!s_started || s_test_running || n == 0) {
        return false;
    }
    link_test_t *t = calloc(1, sizeof *t);
    if (!t) {
        return false;
    }
    if (mac) {
        memcpy(t->mac, mac, 6);
    } else {
        t->broadcast = true;
    }
    t->n = n;
    t->gap_ms = gap_ms;
    t->echo = echo;
    t->len = len < 6 ? 6 : (len > ESP_NOW_MAX_DATA_LEN ? ESP_NOW_MAX_DATA_LEN : len);
    s_test_running = true;
    if (xTaskCreate(test_task, "aos_linktest", 4096, t, 4, NULL) != pdPASS) {
        free(t);
        s_test_running = false;
        return false;
    }
    return true;
}

bool aos_hal_link_test_running(void)
{
    return s_test_running;
}
