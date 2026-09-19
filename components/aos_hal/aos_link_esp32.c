/*
 * AmoledOS - Link, the raw layer on the board: ESP-NOW.
 *
 * What the common layer (aos_link.c) gets from here: esp_now on the
 * station interface with a broadcast peer and one encrypted peer for the
 * partner; a queue between the driver's callbacks (which run in the WiFi
 * task and must return at once) and a task of the link's own, which
 * delivers the frames and runs the common layer's clockwork every 10 ms;
 * the lock; and the bump hook from the IMU poll. Parking on a channel is
 * in aos_hal_esp32.c, next to the WiFi event handler it has to talk to.
 */
#include "aos_hal.h"
#include "aos_link_internal.h"
#include "aos_board.h"

#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "aos_link";

#define LINK_QUEUE_LEN  16
#define LINK_PMK_BYTES  "AmoledOS-link-01"

typedef struct {
    uint8_t  mac[6];
    int8_t   rssi;
    uint16_t len;
    uint8_t  data[AOS_LINK_MAX_FRAME];
} raw_frame_t;

static bool          s_up;
static QueueHandle_t s_queue;
static TaskHandle_t  s_task;
static volatile bool s_stop;
static SemaphoreHandle_t s_mutex;   /* recursive: the common layer sends, hashes and writes NVS under it */
static uint8_t       s_partner_mac[6];
static bool          s_partner_set;

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ---- driver callbacks: copy and leave ------------------------------------ */

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!s_queue || len <= 0 || len > AOS_LINK_MAX_FRAME) {
        return;
    }
    raw_frame_t f;
    memcpy(f.mac, info->src_addr, 6);
    f.rssi = info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0;
    f.len  = (uint16_t)len;
    memcpy(f.data, data, (size_t)len);
    xQueueSend(s_queue, &f, 0);         /* full: the frame is lost, as on the air */
}

static void on_sent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    aos_link_on_sent(status == ESP_NOW_SEND_SUCCESS);
}

static bool ensure_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = false;
    esp_err_t e = esp_now_add_peer(&peer);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "add peer: %s", esp_err_to_name(e));
    }
    return e == ESP_OK;
}

static void link_task(void *arg)
{
    (void)arg;
    raw_frame_t f;
    while (!s_stop) {
        if (xQueueReceive(s_queue, &f, pdMS_TO_TICKS(10)) == pdTRUE) {
            aos_link_on_frame(f.mac, f.rssi, f.data, f.len);
        }
        aos_link_poll();
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

static void bump_cb(uint32_t t_ms, float magnitude_g)
{
    (void)t_ms; (void)magnitude_g;
    aos_link_bump_hint();
}

/* ---- the raw interface ---------------------------------------------------- */

bool aos_link_raw_start(uint8_t own_mac[6], uint32_t *version)
{
    if (s_up) {
        return true;
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "wifi is not started: the link needs the radio on");
        return false;
    }
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateRecursiveMutex();
    }
    if (!s_queue) {
        s_queue = xQueueCreate(LINK_QUEUE_LEN, sizeof(raw_frame_t));
    }
    if (!s_mutex || !s_queue) {
        return false;
    }
    esp_err_t e = esp_now_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(e));
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    esp_now_set_pmk((const uint8_t *)LINK_PMK_BYTES);
    ensure_peer(BROADCAST);
    s_partner_set = false;
    esp_now_get_version(version);
    esp_read_mac(own_mac, ESP_MAC_WIFI_STA);
    uint8_t ch = 0; wifi_second_chan_t sc;
    esp_wifi_get_channel(&ch, &sc);
    aos_hal_link_set_channel_info(ch);

    s_stop = false;
    if (xTaskCreate(link_task, "aos_link", 4096, NULL, 5, &s_task) != pdPASS) {
        esp_now_deinit();
        return false;
    }
    aos_board_imu_set_bump_cb(bump_cb);
    s_up = true;
    ESP_LOGI(TAG, "link up: esp-now v%lu, channel %u, mac %02x:%02x:%02x:%02x:%02x:%02x",
             (unsigned long)*version, ch,
             own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4], own_mac[5]);
    return true;
}

void aos_link_raw_stop(void)
{
    if (!s_up) {
        return;
    }
    aos_board_imu_set_bump_cb(NULL);
    s_stop = true;
    for (int i = 0; i < 30 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    s_up = false;
    ESP_LOGI(TAG, "link down");
}

bool aos_link_raw_send(const uint8_t mac[6], const void *data, size_t len)
{
    if (!s_up) {
        return false;
    }
    if (!ensure_peer(mac)) {
        return false;
    }
    return esp_now_send(mac, data, len) == ESP_OK;
}

bool aos_link_raw_set_partner(const uint8_t mac[6], const uint8_t lmk[16])
{
    if (!s_up) {
        return false;
    }
    if (!lmk) {
        if (esp_now_is_peer_exist(mac)) {
            esp_now_del_peer(mac);
        }
        s_partner_set = false;
        return true;
    }
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.ifidx   = WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = true;
    memcpy(peer.lmk, lmk, 16);
    esp_err_t e = esp_now_is_peer_exist(mac) ? esp_now_mod_peer(&peer) : esp_now_add_peer(&peer);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "partner peer: %s", esp_err_to_name(e));
        return false;
    }
    memcpy(s_partner_mac, mac, 6);
    s_partner_set = true;
    ESP_LOGI(TAG, "partner %02x:%02x:%02x:%02x:%02x:%02x, encrypted",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

uint32_t aos_link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void aos_link_lock(void)
{
    if (s_mutex) {
        xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY);
    }
}

void aos_link_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGiveRecursive(s_mutex);
    }
}
