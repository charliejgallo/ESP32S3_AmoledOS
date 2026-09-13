/* D6: the watch as a network interface of the computer it is plugged into
 * (CDC-NCM), so the portal answers at http://192.168.7.1 over the cable with
 * no WiFi at all. docs/USB.md.
 *
 * Two halves glued together: TinyUSB's NCM class (frames in and out of the
 * cable) and an esp_netif of our own on lwIP's Ethernet stack, with the DHCP
 * server the AP uses, so the computer gets 192.168.7.2 by itself. The server
 * offers no router and no DNS: the computer must not think the watch is its
 * way to the internet.
 *
 * Frames from the cable arrive in TinyUSB's own buffer, which it reuses the
 * moment the callback returns, while lwIP keeps the frame until it is done
 * with it (ethernetif_input wraps the buffer in a pbuf and frees it through
 * driver_free_rx_buffer): so every frame is copied, into PSRAM. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "dhcpserver/dhcpserver.h"
#include "tinyusb_net.h"
#include "aos_usb_net.h"
#include "aos_hal.h"

static const char *TAG = "aos_usb_net";

static esp_netif_t             *s_netif;
static esp_netif_driver_base_t  s_drv;
static bool                     s_net_up;
static bool                     s_mdns;

static esp_err_t usb_tx(void *h, void *buffer, size_t len)
{
    (void)h;
    return tinyusb_net_send_sync(buffer, (uint16_t)len, NULL, pdMS_TO_TICKS(200));
}

static esp_err_t usb_tx_wrap(void *h, void *buffer, size_t len, void *netstack_buffer)
{
    (void)netstack_buffer;
    return usb_tx(h, buffer, len);
}

static void usb_free_rx(void *h, void *buffer)
{
    (void)h;
    free(buffer);
}

static esp_err_t post_attach(esp_netif_t *netif, esp_netif_iodriver_handle h)
{
    const esp_netif_driver_ifconfig_t cfg = {
        .handle = h,
        .transmit = usb_tx,
        .transmit_wrap = usb_tx_wrap,
        .driver_free_rx_buffer = usb_free_rx,
    };
    s_drv.netif = netif;
    return esp_netif_set_driver_config(netif, &cfg);
}

/* From the TinyUSB task. */
static esp_err_t usb_rx(void *buffer, uint16_t len, void *ctx)
{
    (void)ctx;
    if (!s_netif || !s_net_up) {
        return ESP_OK;
    }
    void *copy = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(copy, buffer, len);
    esp_err_t e = esp_netif_receive(s_netif, copy, len, NULL);
    if (e != ESP_OK) {
        free(copy);
    }
    return e;
}

void aos_usb_net_mac(uint8_t mac[6])
{
    /* The S3 derives an Ethernet MAC of its own from the base one (+3): not
     * the WiFi station's, so the two interfaces never share an address. */
    esp_read_mac(mac, ESP_MAC_ETH);
}

bool aos_usb_net_start(void)
{
    if (s_netif) {
        return true;
    }
    /* lwIP and esp_netif come up with WiFi; with WiFi off at boot they may
     * not be up yet. INVALID_STATE is "already done". */
    esp_err_t e0 = esp_netif_init();
    if (e0 != ESP_OK && e0 != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(e0));
        return false;
    }
    static esp_netif_ip_info_t ip;
    IP4_ADDR(&ip.ip, 192, 168, 7, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    IP4_ADDR(&ip.gw, 192, 168, 7, 1);
    esp_netif_inherent_config_t base = {
        .flags = (esp_netif_flags_t)(ESP_NETIF_DHCP_SERVER | ESP_NETIF_FLAG_AUTOUP),
        .ip_info = &ip,
        .if_key = "USB",
        .if_desc = "usb",
        .route_prio = 5,        /* below the WiFi station: the internet stays on WiFi */
    };
    esp_netif_config_t cfg = {
        .base = &base,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_netif = esp_netif_new(&cfg);
    if (!s_netif) {
        ESP_LOGE(TAG, "esp_netif_new failed");
        return false;
    }
    /* Two addresses on this two-node link. The one in the descriptor (the
     * iMACAddress string) is the one the COMPUTER gives its own interface -
     * macOS did exactly that, en7 came up with it - so the watch's side has
     * to be a different one, or the computer is talking to itself. TinyUSB's
     * own lwIP example flips the last bit for the same reason. */
    uint8_t mac[6], ours[6];
    aos_usb_net_mac(mac);
    memcpy(ours, mac, 6);
    ours[5] ^= 0x01;
    esp_netif_set_mac(s_netif, ours);
    s_drv.post_attach = post_attach;
    esp_err_t e = esp_netif_attach(s_netif, &s_drv);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_attach: %s", esp_err_to_name(e));
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return false;
    }
    /* Before the server starts (action_start): no router, no DNS. */
    dhcps_offer_t none = 0;
    esp_netif_dhcps_option(s_netif, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &none, sizeof(none));
    esp_netif_dhcps_option(s_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &none, sizeof(none));

    tinyusb_net_config_t nc = {
        .on_recv_callback = usb_rx,
    };
    memcpy(nc.mac_addr, mac, 6);
    e = tinyusb_net_init(&nc);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_net_init: %s", esp_err_to_name(e));
        esp_netif_destroy(s_netif);
        s_netif = NULL;
        return false;
    }
    esp_netif_action_start(s_netif, NULL, 0, NULL);
    esp_netif_action_connected(s_netif, NULL, 0, NULL);
    s_net_up = true;
    s_mdns = aos_hal_mdns_add_netif(s_netif);      /* amoledos.local on this side too */
    ESP_LOGI(TAG, "usb network up: 192.168.7.1, the computer gets .2 with mac %02x:%02x:%02x:%02x:%02x:%02x, ours ends in %02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ours[5]);
    return true;
}

void aos_usb_net_stop(void)
{
    if (!s_netif) {
        return;
    }
    s_net_up = false;
    if (s_mdns) {
        aos_hal_mdns_remove_netif(s_netif);
        s_mdns = false;
    }
    esp_netif_action_disconnected(s_netif, NULL, 0, NULL);
    esp_netif_action_stop(s_netif, NULL, 0, NULL);
    tinyusb_net_deinit();
    esp_netif_destroy(s_netif);
    s_netif = NULL;
    ESP_LOGI(TAG, "usb network down");
}

bool aos_usb_net_up(void)
{
    return s_net_up;
}
