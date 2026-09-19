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

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "aos_board.h"

static const char *TAG = "aos_link";

/* ---- phase 2: beacons, neighbours, the bump, the partner ----------------- */

#define LINK_MAGIC          "AOSL"
#define LINK_PROTO          1
#define LINK_T_BEACON       'B'
#define LINK_T_BUMP         'P'
#define LINK_T_CONFIRM      'C'
#define LINK_T_CONFIRM_ACK  'K'
#define LINK_BEACON_MS      1000
#define LINK_NEIGHBOUR_TTL  5000
#define LINK_BUMP_WINDOW_MS 400
#define LINK_RSSI_NEAR      (-50)
#define LINK_PMK            "AmoledOS-link-01"   /* 16 bytes; the per-partner key is derived */

typedef struct {
    bool     used;
    uint8_t  mac[6];
    char     name[AOS_LINK_NAME_MAX + 1];
    char     app[AOS_LINK_NAME_MAX + 1];
    int8_t   rssi;
    uint32_t seen_ms;
} neighbour_t;

static char        s_offer[AOS_LINK_NAME_MAX + 1];
static neighbour_t s_neighbours[AOS_LINK_NEIGHBOURS];
static bool        s_pairing;
static uint32_t    s_pair_events;
static uint32_t    s_last_beacon_ms;
static uint32_t    s_last_confirm_ms;
static uint32_t    s_pair_quiet_until;   /* no bumps count until then: the case rings after a knock */

/* my bump, and the last bump frame that came in */
static uint32_t    s_my_bump_ms;
static uint32_t    s_my_nonce;
static struct {
    bool     valid;
    uint8_t  mac[6];
    char     name[AOS_LINK_NAME_MAX + 1];
    uint32_t nonce;
    uint32_t at_ms;
    int8_t   rssi;
} s_their_bump;

static struct {
    bool     valid;
    bool     confirmed;
    uint8_t  mac[6];
    char     name[AOS_LINK_NAME_MAX + 1];
    uint8_t  lmk[16];
} s_partner;

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

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void hex_of(const uint8_t *b, size_t n, char *out)
{
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = '\0';
}

static bool unhex(const char *in, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(in + 2 * i, "%2x", &v) != 1) {
            return false;
        }
        out[i] = (uint8_t)v;
    }
    return true;
}

static neighbour_t *neighbour_slot(const uint8_t mac[6])
{
    neighbour_t *free_slot = NULL, *oldest = NULL;
    for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
        neighbour_t *n = &s_neighbours[i];
        if (n->used && memcmp(n->mac, mac, 6) == 0) {
            return n;
        }
        if (!n->used && !free_slot) {
            free_slot = n;
        }
        if (n->used && (!oldest || n->seen_ms < oldest->seen_ms)) {
            oldest = n;
        }
    }
    neighbour_t *n = free_slot ? free_slot : oldest;
    memset(n, 0, sizeof *n);
    n->used = true;
    memcpy(n->mac, mac, 6);
    return n;
}

static void send_beacon(void)
{
    uint8_t f[4 + 2 + AOS_LINK_NAME_MAX + AOS_LINK_NAME_MAX];
    memcpy(f, LINK_MAGIC, 4);
    f[4] = LINK_PROTO;
    f[5] = LINK_T_BEACON;
    memset(f + 6, 0, sizeof f - 6);
    memcpy(f + 6, aos_hal_device_name(), strnlen(aos_hal_device_name(), AOS_LINK_NAME_MAX));
    memcpy(f + 6 + AOS_LINK_NAME_MAX, s_offer, strnlen(s_offer, AOS_LINK_NAME_MAX));
    aos_hal_link_send(NULL, f, sizeof f);
}

/* The key both sides compute alike: the two MACs in order, and the two
 * nonces combined so the order does not matter. Sixteen bytes of SHA-256. */
static void derive_lmk(const uint8_t a[6], const uint8_t b[6], uint32_t na, uint32_t nb, uint8_t out[16])
{
    uint8_t material[6 + 6 + 4 + 8];
    const uint8_t *lo = memcmp(a, b, 6) < 0 ? a : b;
    const uint8_t *hi = lo == a ? b : a;
    memcpy(material, lo, 6);
    memcpy(material + 6, hi, 6);
    uint32_t mix = na ^ nb;
    memcpy(material + 12, &mix, 4);
    memcpy(material + 16, LINK_PMK, 8);
    uint8_t digest[32];
    mbedtls_sha256(material, sizeof material, digest, 0);
    memcpy(out, digest, 16);
}

static bool partner_peer_install(void)
{
    if (!s_partner.valid) {
        return false;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, s_partner.mac, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = true;
    memcpy(peer.lmk, s_partner.lmk, 16);
    esp_err_t e = esp_now_is_peer_exist(s_partner.mac) ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "partner peer: %s", esp_err_to_name(e));
    }
    return e == ESP_OK;
}

static void partner_save(void)
{
    char hex[33];
    hex_of(s_partner.mac, 6, hex);
    aos_hal_pref_set_str("lk_peer", s_partner.valid ? hex : "");
    hex_of(s_partner.lmk, 16, hex);
    aos_hal_pref_set_str("lk_lmk", s_partner.valid ? hex : "");
    aos_hal_pref_set_str("lk_pname", s_partner.valid ? s_partner.name : "");
}

static void partner_load(void)
{
    char hex[40], name[AOS_LINK_NAME_MAX + 1];
    memset(&s_partner, 0, sizeof s_partner);
    if (aos_hal_pref_get_str("lk_peer", hex, sizeof hex) && strlen(hex) == 12 &&
        unhex(hex, s_partner.mac, 6) &&
        aos_hal_pref_get_str("lk_lmk", hex, sizeof hex) && strlen(hex) == 32 &&
        unhex(hex, s_partner.lmk, 16)) {
        s_partner.valid = true;
        if (aos_hal_pref_get_str("lk_pname", name, sizeof name)) {
            snprintf(s_partner.name, sizeof s_partner.name, "%s", name);
        }
    }
}

static void send_confirm(uint8_t type)
{
    uint8_t f[6] = { 'A', 'O', 'S', 'L', LINK_PROTO, type };
    aos_hal_link_send(s_partner.mac, f, sizeof f);
}

static void pair_with(const uint8_t mac[6], const char *name, uint32_t their_nonce)
{
    if (esp_now_is_peer_exist(s_partner.mac) && s_partner.valid &&
        memcmp(s_partner.mac, mac, 6) != 0) {
        esp_now_del_peer(s_partner.mac);   /* one partner at a time */
    }
    memset(&s_partner, 0, sizeof s_partner);
    s_partner.valid = true;
    memcpy(s_partner.mac, mac, 6);
    snprintf(s_partner.name, sizeof s_partner.name, "%s", name);
    derive_lmk(s_stats.own_mac, mac, s_my_nonce, their_nonce, s_partner.lmk);
    partner_peer_install();
    partner_save();
    s_pair_events++;
    s_their_bump.valid = false;
    /* One knock was 17 pairings on 2026-09-19: the case keeps ringing above
     * the bump threshold for a good while, and every ring re-paired with a
     * fresh key. Three seconds of deafness after a pairing. */
    s_pair_quiet_until = now_ms() + 3000;
    ESP_LOGI(TAG, "paired with %s (%02x:%02x:%02x:%02x:%02x:%02x)", name,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    send_confirm(LINK_T_CONFIRM);
}

static bool bumps_match(uint32_t mine, uint32_t theirs)
{
    uint32_t d = mine > theirs ? mine - theirs : theirs - mine;
    return mine && theirs && d <= LINK_BUMP_WINDOW_MS;
}

static void handle_link_frame(const link_frame_t *f)
{
    if (f->len < 6 || f->data[4] != LINK_PROTO) {
        return;
    }
    switch (f->data[5]) {
    case LINK_T_BEACON: {
        if (f->len < 6 + 2 * AOS_LINK_NAME_MAX) {
            return;
        }
        neighbour_t *n = neighbour_slot(f->mac);
        memcpy(n->name, f->data + 6, AOS_LINK_NAME_MAX);
        n->name[AOS_LINK_NAME_MAX] = '\0';
        memcpy(n->app, f->data + 6 + AOS_LINK_NAME_MAX, AOS_LINK_NAME_MAX);
        n->app[AOS_LINK_NAME_MAX] = '\0';
        n->rssi    = f->rssi;
        n->seen_ms = now_ms();
        if (s_partner.valid && memcmp(f->mac, s_partner.mac, 6) == 0) {
            if (n->name[0]) {
                snprintf(s_partner.name, sizeof s_partner.name, "%s", n->name);
            }
            /* The partner is back in range: prove the key still matches on
             * both sides, once per session, before any app needs it. */
            if (!s_partner.confirmed && now_ms() - s_last_confirm_ms > 2000) {
                s_last_confirm_ms = now_ms();
                send_confirm(LINK_T_CONFIRM);
            }
        }
        break;
    }
    case LINK_T_BUMP: {
        if (f->len < 6 + 4 + AOS_LINK_NAME_MAX || !s_pairing ||
            (int32_t)(now_ms() - s_pair_quiet_until) < 0) {
            return;
        }
        s_their_bump.valid = true;
        memcpy(s_their_bump.mac, f->mac, 6);
        memcpy(&s_their_bump.nonce, f->data + 6, 4);
        memcpy(s_their_bump.name, f->data + 10, AOS_LINK_NAME_MAX);
        s_their_bump.name[AOS_LINK_NAME_MAX] = '\0';
        s_their_bump.at_ms = now_ms();
        s_their_bump.rssi  = f->rssi;
        if (f->rssi < LINK_RSSI_NEAR) {
            ESP_LOGI(TAG, "bump from %s too far away (%d dBm)", s_their_bump.name, f->rssi);
            return;
        }
        if (bumps_match(s_my_bump_ms, s_their_bump.at_ms)) {
            pair_with(f->mac, s_their_bump.name, s_their_bump.nonce);
        }
        break;
    }
    case LINK_T_CONFIRM:
        /* It arrived on the encrypted peer: only the right key decrypts. */
        if (s_partner.valid && memcmp(f->mac, s_partner.mac, 6) == 0) {
            s_partner.confirmed = true;
            send_confirm(LINK_T_CONFIRM_ACK);
        }
        break;
    case LINK_T_CONFIRM_ACK:
        if (s_partner.valid && memcmp(f->mac, s_partner.mac, 6) == 0) {
            s_partner.confirmed = true;
        }
        break;
    default:
        break;
    }
}

static void bump_cb(uint32_t t_ms, float magnitude_g)
{
    (void)t_ms; (void)magnitude_g;
    aos_hal_link_bump();
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
        uint32_t now = now_ms();
        if (now - s_last_beacon_ms >= LINK_BEACON_MS) {
            s_last_beacon_ms = now;
            send_beacon();
            for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
                if (s_neighbours[i].used && now - s_neighbours[i].seen_ms > LINK_NEIGHBOUR_TTL) {
                    s_neighbours[i].used = false;
                }
            }
        }
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
        } else if (f.len >= 6 && memcmp(f.data, LINK_MAGIC, 4) == 0) {
            handle_link_frame(&f);
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
    esp_now_set_pmk((const uint8_t *)LINK_PMK);
    ensure_peer(BROADCAST);
    memset(s_neighbours, 0, sizeof s_neighbours);
    s_their_bump.valid = false;
    s_my_bump_ms = 0;
    s_last_beacon_ms = 0;

    memset(&s_stats, 0, sizeof s_stats);
    s_test_have_seq = false;
    s_ring_head = s_ring_tail = 0;
    esp_now_get_version(&s_stats.version);
    esp_read_mac(s_stats.own_mac, ESP_MAC_WIFI_STA);
    partner_load();
    partner_peer_install();
    aos_board_imu_set_bump_cb(bump_cb);

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
    aos_board_imu_set_bump_cb(NULL);
    s_pairing = false;
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

/* ---- phase 2 public --------------------------------------------------- */

void aos_hal_link_offer(const char *app)
{
    snprintf(s_offer, sizeof s_offer, "%s", app ? app : "");
}

int aos_hal_link_neighbours(aos_link_neighbour_t *out, int max)
{
    int n = 0;
    uint32_t now = now_ms();
    for (int i = 0; i < AOS_LINK_NEIGHBOURS && n < max; i++) {
        const neighbour_t *nb = &s_neighbours[i];
        if (!nb->used || now - nb->seen_ms > LINK_NEIGHBOUR_TTL) {
            continue;
        }
        memcpy(out[n].mac, nb->mac, 6);
        snprintf(out[n].name, sizeof out[n].name, "%s", nb->name);
        snprintf(out[n].app, sizeof out[n].app, "%s", nb->app);
        out[n].rssi   = nb->rssi;
        out[n].age_ms = now - nb->seen_ms;
        n++;
    }
    return n;
}

void aos_hal_link_pair_enable(bool on)
{
    s_pairing = on && s_started;
    if (!on) {
        s_their_bump.valid = false;
        s_my_bump_ms = 0;
    }
}

bool aos_hal_link_pairing(void)
{
    return s_pairing;
}

void aos_hal_link_bump(void)
{
    if (!s_started || !s_pairing) {
        return;
    }
    uint32_t now = now_ms();
    if ((int32_t)(now - s_pair_quiet_until) < 0) {
        return;                         /* just paired: the case is still ringing */
    }
    if (now - s_my_bump_ms < LINK_BUMP_WINDOW_MS) {
        return;                         /* the same knock ringing on */
    }
    s_my_bump_ms = now;
    s_my_nonce   = esp_random();
    uint8_t f[6 + 4 + AOS_LINK_NAME_MAX];
    memcpy(f, LINK_MAGIC, 4);
    f[4] = LINK_PROTO;
    f[5] = LINK_T_BUMP;
    memcpy(f + 6, &s_my_nonce, 4);
    memset(f + 10, 0, AOS_LINK_NAME_MAX);
    memcpy(f + 10, aos_hal_device_name(), strnlen(aos_hal_device_name(), AOS_LINK_NAME_MAX));
    aos_hal_link_send(NULL, f, sizeof f);
    if (s_their_bump.valid && s_their_bump.rssi >= LINK_RSSI_NEAR &&
        bumps_match(s_my_bump_ms, s_their_bump.at_ms)) {
        pair_with(s_their_bump.mac, s_their_bump.name, s_their_bump.nonce);
    }
}

bool aos_hal_link_partner(aos_link_partner_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof *out);
    if (!s_partner.valid && s_started == false) {
        partner_load();                 /* the app may ask before the link is up */
    }
    out->valid     = s_partner.valid;
    out->confirmed = s_partner.confirmed;
    memcpy(out->mac, s_partner.mac, 6);
    snprintf(out->name, sizeof out->name, "%s", s_partner.name);
    uint32_t now = now_ms();
    for (int i = 0; i < AOS_LINK_NEIGHBOURS; i++) {
        const neighbour_t *nb = &s_neighbours[i];
        if (nb->used && memcmp(nb->mac, s_partner.mac, 6) == 0 && now - nb->seen_ms <= LINK_NEIGHBOUR_TTL) {
            out->seen   = true;
            out->rssi   = nb->rssi;
            out->age_ms = now - nb->seen_ms;
        }
    }
    return out->valid;
}

void aos_hal_link_unpair(void)
{
    if (s_started && s_partner.valid && esp_now_is_peer_exist(s_partner.mac)) {
        esp_now_del_peer(s_partner.mac);
    }
    memset(&s_partner, 0, sizeof s_partner);
    partner_save();
    ESP_LOGI(TAG, "partner forgotten");
}

bool aos_hal_link_send_partner(const void *data, size_t len)
{
    if (!s_partner.valid) {
        return false;
    }
    return aos_hal_link_send(s_partner.mac, data, len);
}

uint32_t aos_hal_link_pair_events(void)
{
    return s_pair_events;
}
