/*
 * AmoledOS - Network survey
 *
 * A background task sweeps the WiFi networks around, then our own LAN (which
 * addresses answer and with what MAC) and then the TCP ports of whatever it
 * found. It writes an NDJSON report to the microSD as it works.
 *
 * The app only looks at totals (aos_hal_scan_status). The detail is read from
 * the portal, which serves the file verbatim: the firmware does not interpret
 * the format, just as with 'remoto's profile.
 *
 * The four decisions inside it, with their reasons:
 *
 *  1. ONE SINGLE RAW ICMP SOCKET, not esp_ping. esp_ping creates a task per
 *     session: 254 sequential sessions with a 300 ms wait is 76 seconds.
 *     Sending the 254 echo requests back to back and listening afterwards, the
 *     whole sweep is a couple of seconds.
 *  2. THE MACs COME FROM THE ARP TABLE, and they have to be harvested WHILE
 *     sweeping: ARP_TABLE_SIZE is 10 entries (fixed in lwip/opt.h, not a
 *     Kconfig), so waiting until the end means finding it overwritten twenty
 *     times. This is also what finds the ones that do NOT answer ping but do
 *     answer ARP, which in a house is half of them.
 *  3. THE ARP TABLE IS READ FROM THE lwIP THREAD, with tcpip_callback_wait().
 *     CONFIG_LWIP_TCPIP_CORE_LOCKING is not set, so reading arp_table from
 *     this task would be a race against the thread that writes it. Since
 *     tcpip_callback_wait() blocks the caller until the callback finishes,
 *     there is no race over our own table either.
 *  4. RANGE GUARD. Only our own subnet and only if the prefix is /24 or
 *     smaller. Without that, this is not a scanner of your house: it is a
 *     scanner.
 */
#include "aos_hal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "mdns.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/icmp.h"
#include "lwip/ip.h"
#include "lwip/inet_chksum.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/opt.h"

static const char *TAG = "aos_scan";

/* -------------------------------------------------------------------------- */

#define SCAN_MAX_HOSTS      254
#define SCAN_MAX_PUERTOS    8       /* open ports stored per host */
#define SCAN_MAX_WIFI       40

/* Sockets in flight for the port sweep. CONFIG_LWIP_MAX_SOCKETS is 10 and the
 * portal already takes several, so room has to be left for it: if the web
 * server runs out of sockets while you scan, the report cannot even be looked
 * at. */
#define SCAN_CONCURRENTES   4

/* The sweep goes IN BATCHES, and the batch size is not arbitrary: the ARP
 * table has TEN entries. Asking for eight addresses at a time and harvesting
 * before the next batch, no reply is lost to eviction. With 254 addresses that
 * is 32 rounds of ~140 ms: about 4.5 seconds. */
#define LOTE_IPS            8
#define LOTE_ESPERA_MS      140
#define PASADAS             2

/* mDNS. The meta-query asks WHICH services are on the network instead of
 * trying a fixed list: in a typical house there are three or four, so it comes
 * out faster and it also finds the ones you would not have thought of. */
#define MDNS_SERVICIO_MS    500
#define MDNS_MAX_SERVICIOS  12
#define MDNS_MAX_RESULT     16

/* The services that are asked about.
 *
 * The first version did not carry this list: it did the DNS-SD meta-query
 * (_services._dns-sd._udp) so the network would say WHICH types are advertised
 * and asked only about those. That is the correct way and it is faster.
 * MEASURED TWICE ON THE BOARD: it returned ZERO types on a network with 39
 * hosts, fourteen of which advertise mDNS in abundance. The IDF's mdns
 * component is built to query a KNOWN service type: the meta-query answers
 * with PTRs pointing at *types* and not at *instances*, and the parser
 * discards them. It was removed: it was 900 ms per sweep in exchange for
 * nothing.
 *
 * The order matters little for the result and a lot for reading the code:
 * first the ones that gave the most names when measured in a real house. The
 * ones that gave zero here stay all the same, because another network is not
 * this network. */
static const struct { const char *serv, *proto; } MDNS_COMUNES[] = {
    { "_http",        "_tcp" },   /* routers, NAS, cameras, Home Assistant */
    { "_device-info", "_tcp" },   /* Apple and company: gives the pretty name */
    { "_googlecast",  "_tcp" },   /* Chromecast, Android TV, Nest           */
    { "_airplay",     "_tcp" },   /* Apple TV, HomePod, televisions         */
    { "_raop",        "_tcp" },   /* audio over AirPlay                     */
    { "_esphomelib",  "_tcp" },   /* ESPHome gadgets                        */
    { "_workstation", "_tcp" },   /* machines running avahi                 */
    { "_ssh",         "_tcp" },
    { "_ipp",         "_tcp" },   /* printers                               */
    { "_printer",     "_tcp" },
    { "_smb",         "_tcp" },   /* NAS and shared folders                 */
    { "_hap",         "_tcp" },   /* HomeKit accessories                    */
};
#define N_MDNS_COMUNES ((int)(sizeof(MDNS_COMUNES) / sizeof(MDNS_COMUNES[0])))
#define ICMP_ESCUCHA_MS     1200
#define PUERTO_TIMEOUT_MS   300
#define ICMP_ID             0xA05

/* The default list. These are the ports that say something in a house:
 * routers, cameras, printers, NAS, Home Assistant, a couple of web admin
 * pages. */
static const uint16_t PUERTOS[] = {
    21, 22, 23, 53, 80, 139, 443, 445, 554, 631,
    1883, 3000, 3389, 5000, 8080, 8123, 8443, 9000,
};
#define N_PUERTOS  ((int)(sizeof(PUERTOS) / sizeof(PUERTOS[0])))

typedef struct {
    uint32_t ip;                    /* host order */
    uint8_t  mac[6];
    bool     mac_ok;
    bool     ping_ok;
    uint16_t puertos[SCAN_MAX_PUERTOS];
    uint8_t  n_puertos;
    bool     escrito;               /* already went out in the report */
} scan_host_t;

static scan_host_t      *s_hosts;
static int               s_n_hosts;

static TaskHandle_t      s_task;
static volatile bool     s_abort;
static uint32_t          s_flags;
static volatile aos_scan_phase_t s_phase;
static volatile int      s_wifi_found, s_hosts_found, s_ports_found;
static volatile int      s_done, s_total;
static uint64_t          s_inicio_ms;
static char              s_path[160];

/* our own network, in host order */
static uint32_t s_mi_ip, s_mi_mask, s_red, s_primero, s_ultimo;

/* Diagnostic counters. Without these, "it found nothing" could be the sending,
 * the receiving or the ARP harvest, and there is no way to tell which. */
static int s_enviados, s_envios_fallidos, s_respuestas, s_arp_vistos;

/* -------------------------------------------------------------------------- */
/* Writing the report                                                          */
/* -------------------------------------------------------------------------- */

/* A valid JSON string: quotes, backslashes and control characters are escaped.
 * An SSID can carry anything, and a report the browser cannot parse because of
 * one quote is of no use at all. */
static void json_str(char *out, size_t cap, const uint8_t *in, size_t len)
{
    size_t o = 0;
    for (size_t i = 0; i < len && in[i] && o + 7 < cap; i++) {
        uint8_t c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20 || c == 0x7F) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

static void linea(FILE *f, const char *fmt, ...)
{
    if (!f) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    /* Flushed on every line on purpose: the report has to be usable even if
     * the power goes or you hit stop halfway. It is a few tens of lines in
     * total, not an audio stream. */
    fflush(f);
}

static void ip_txt(uint32_t ip_host, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u",
             (unsigned)((ip_host >> 24) & 0xFF), (unsigned)((ip_host >> 16) & 0xFF),
             (unsigned)((ip_host >> 8) & 0xFF), (unsigned)(ip_host & 0xFF));
}

static void mac_txt(const uint8_t *m, char *out, size_t cap)
{
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

static const char *auth_txt(wifi_auth_mode_t a)
{
    switch (a) {
    case WIFI_AUTH_OPEN:            return "abierta";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    case WIFI_AUTH_ENTERPRISE:      return "enterprise";
    default:                        return "otra";
    }
}

/* -------------------------------------------------------------------------- */
/* Host table                                                                  */
/* -------------------------------------------------------------------------- */

/* Looks up without creating: needed for the second pass, which only asks again
 * of the addresses that have not answered yet. */
static scan_host_t *host_existe(uint32_t ip)
{
    for (int i = 0; i < s_n_hosts; i++) {
        if (s_hosts[i].ip == ip) {
            return &s_hosts[i];
        }
    }
    return NULL;
}

static scan_host_t *host_buscar(uint32_t ip)
{
    for (int i = 0; i < s_n_hosts; i++) {
        if (s_hosts[i].ip == ip) {
            return &s_hosts[i];
        }
    }
    if (s_n_hosts >= SCAN_MAX_HOSTS) {
        return NULL;
    }
    scan_host_t *h = &s_hosts[s_n_hosts++];
    memset(h, 0, sizeof(*h));
    h->ip = ip;
    return h;
}

/* -------------------------------------------------------------------------- */
/* Harvesting the ARP table, from the lwIP thread                              */
/* -------------------------------------------------------------------------- */

static void arp_cosechar_cb(void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t      *ip  = NULL;
        struct netif    *nif = NULL;
        struct eth_addr *mac = NULL;
        if (etharp_get_entry(i, &ip, &nif, &mac) != 1 || !ip || !mac) {
            continue;
        }
        uint32_t v = lwip_ntohl(ip->addr);
        if (v < s_primero || v > s_ultimo) {
            continue;               /* not from the subnet we are looking at */
        }
        s_arp_vistos++;
        scan_host_t *h = host_buscar(v);
        if (h && !h->mac_ok) {
            memcpy(h->mac, mac->addr, 6);
            h->mac_ok = true;
        }
    }
}

static void arp_cosechar(void)
{
    /* Blocks until the callback finishes, so while it runs nobody else touches
     * s_hosts: safety on both sides comes out of the same call. */
    tcpip_callback_wait(arp_cosechar_cb, NULL);
}

/* -------------------------------------------------------------------------- */
/* ICMP                                                                        */
/* -------------------------------------------------------------------------- */

/* The checksum is done by inet_chksum(), lwIP's own, the same one the IDF's
 * ping uses, and not by one written by hand.
 *
 * MEASURED ON THE BOARD: the hand-written version accumulated reading the
 * bytes as big-endian and returned the value in HOST order, which was then
 * stored in the field without htons(). Which means every echo request went out
 * with the two checksum bytes the wrong way round and EVERY host dropped it
 * silently: two full sweeps of a network with hosts on it returned
 * "ping":false in 100% of cases. A checksum is exactly the kind of thing where
 * writing ten lines of your own buys nothing and costs an afternoon. */
/* Drains whatever has arrived without blocking and marks those that answered. */
static void icmp_drenar(int sock, FILE *f)
{
    uint8_t buf[128];
    struct sockaddr_in de;
    socklen_t de_len = sizeof(de);

    for (int i = 0; i < 64; i++) {
        int n = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
                         (struct sockaddr *)&de, &de_len);
        if (n <= 0) {
            return;
        }
        /* A raw socket delivers the whole IP packet, header included. */
        int ihl = (buf[0] & 0x0F) * 4;
        if (n < ihl + (int)sizeof(struct icmp_echo_hdr)) {
            continue;
        }
        struct icmp_echo_hdr *ec = (struct icmp_echo_hdr *)(buf + ihl);
        if (ICMPH_TYPE(ec) != ICMP_ER || lwip_ntohs(ec->id) != ICMP_ID) {
            continue;
        }
        uint32_t ip = lwip_ntohl(de.sin_addr.s_addr);
        s_respuestas++;
        scan_host_t *h = host_buscar(ip);
        if (h && !h->ping_ok) {
            h->ping_ok = true;
            (void)f;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Phases                                                                      */
/* -------------------------------------------------------------------------- */

static void fase_wifi(FILE *f)
{
    s_phase = AOS_SCAN_PH_WIFI;
    s_done  = 0;
    s_total = 0;

    wifi_mode_t modo;
    if (esp_wifi_get_mode(&modo) != ESP_OK || modo == WIFI_MODE_NULL) {
        ESP_LOGW(TAG, "the wifi is not up, there is no network scan");
        return;
    }

    /* show_hidden: a scanner has to see the hidden ones. They come out with no
     * name but with a BSSID, channel and signal strength, which is precisely
     * the interesting part. */
    wifi_scan_config_t cfg = { .show_hidden = true };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        ESP_LOGW(TAG, "the network scan did not start");
        return;
    }

    uint16_t hay = 0;
    esp_wifi_scan_get_ap_num(&hay);
    if (hay == 0) {
        return;
    }
    if (hay > SCAN_MAX_WIFI) {
        hay = SCAN_MAX_WIFI;
    }

    wifi_ap_record_t *recs = calloc(hay, sizeof(wifi_ap_record_t));
    if (!recs) {
        return;
    }
    esp_wifi_scan_get_ap_records(&hay, recs);
    s_total = hay;

    char ssid[100], bssid[20];
    for (uint16_t i = 0; i < hay && !s_abort; i++) {
        json_str(ssid, sizeof(ssid), recs[i].ssid, sizeof(recs[i].ssid));
        mac_txt(recs[i].bssid, bssid, sizeof(bssid));
        linea(f, "{\"t\":\"wifi\",\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,"
                 "\"canal\":%d,\"cifrado\":\"%s\",\"oculta\":%s}",
              ssid, bssid, recs[i].rssi, recs[i].primary,
              auth_txt(recs[i].authmode), ssid[0] ? "false" : "true");
        s_wifi_found = i + 1;
        s_done = i + 1;
    }
    free(recs);
}

/* Asks ARP for a batch of addresses. Runs in the lwIP thread. */
typedef struct { uint32_t ips[LOTE_IPS]; int n; } arp_pedido_t;

static void arp_pedir_cb(void *ctx)
{
    arp_pedido_t *c = (arp_pedido_t *)ctx;
    struct netif *nif = netif_default;
    if (!nif) {
        return;
    }
    for (int i = 0; i < c->n; i++) {
        ip4_addr_t a;
        a.addr = lwip_htonl(c->ips[i]);
        etharp_request(nif, &a);
    }
}

/* LAN sweep: ARP first, ICMP as a bonus.
 *
 * MEASURED ON THE BOARD, and that is why it is written this way. The first
 * version sent 253 echo requests back to back, with 4 ms between them, and
 * trusted the stack to resolve each destination's ARP on its own. The log said
 * what was really happening: "253 sent (0 failed), 6 ICMP replies, 1 ARP
 * entry", and from one run to the next 6, 1, 2, 1, 5. In other words, an
 * unrepeatable disaster.
 *
 * Two things were wrong:
 *
 *  1. sendto() RETURNING SUCCESS DOES NOT MEAN THE PACKET WENT OUT. Every new
 *     destination needs an ARP resolution and the packet sits queued waiting
 *     for it. With 253 different destinations in one second, an ARP table of
 *     10 entries and a small packet queue, the stack evicts entries and drops
 *     the queued packets without saying anything. Most of those 253 never
 *     reached the wire.
 *  2. ON A LAN, ARP FINDS MORE THAN ICMP. A host may have its firewall closed
 *     to ping —Windows ships that way— but it CANNOT stop answering who has
 *     such-and-such an address: without that it could not use the network.
 *     That is why serious LAN scanners sweep by ARP.
 *
 * Now ARP is requested explicitly with etharp_request(), eight addresses at a
 * time, and the table is harvested before the next batch: eight is fewer than
 * the ten entries, so nothing is overwritten. The ICMP goes in the same batch
 * and is left as extra data: it distinguishes "answers ping" from "ARP only".
 */
static void fase_hosts(FILE *f)
{
    s_phase = AOS_SCAN_PH_HOSTS;
    s_done  = 0;
    s_total = (int)(s_ultimo - s_primero + 1) * PASADAS;

    /* The board is a host on the network too, and leaving it out of the report
     * is a small lie but a lie. It goes in with its real MAC. */
    scan_host_t *yo = host_buscar(s_mi_ip);
    if (yo) {
        yo->ping_ok = true;
        if (esp_wifi_get_mac(WIFI_IF_STA, yo->mac) == ESP_OK) {
            yo->mac_ok = true;
        }
    }

    int sock = socket(AF_INET, SOCK_RAW, IP_PROTO_ICMP);
    if (sock < 0) {
        ESP_LOGE(TAG, "could not open the ICMP socket (%d)", errno);
    } else {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 1000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    struct {
        struct icmp_echo_hdr h;
        char                 carga[16];
    } paquete;
    memset(&paquete, 0, sizeof(paquete));
    memcpy(paquete.carga, "AmoledOS scan", 13);
    uint16_t seq = 0;

    /* Two passes. The second only asks again of the addresses that did not
     * answer, and it exists because a phone with WiFi power saving may take
     * longer than a batch's window to wake up. It comes cheap: the ones that
     * already answered are not bothered again. */
    for (int pasada = 0; pasada < PASADAS && !s_abort; pasada++) {
    for (uint32_t ip = s_primero; ip <= s_ultimo && !s_abort; ip += LOTE_IPS) {
        arp_pedido_t lote = { .n = 0 };
        for (uint32_t k = ip; k < ip + LOTE_IPS && k <= s_ultimo; k++) {
            if (k == s_mi_ip) {
                continue;
            }
            if (pasada > 0 && host_existe(k)) {
                continue;               /* that one already turned up */
            }
            lote.ips[lote.n++] = k;
        }
        if (lote.n == 0) {
            s_done += LOTE_IPS;
            continue;
        }

        /* ARP first, which is the one that really finds things. */
        tcpip_callback_wait(arp_pedir_cb, &lote);

        /* And ICMP to the same ones, to know which answers ping. */
        for (int i = 0; i < lote.n && sock >= 0; i++) {
            paquete.h.chksum = 0;
            ICMPH_TYPE_SET(&paquete.h, ICMP_ECHO);
            ICMPH_CODE_SET(&paquete.h, 0);
            paquete.h.id     = lwip_htons(ICMP_ID);
            paquete.h.seqno  = lwip_htons(seq++);
            paquete.h.chksum = inet_chksum(&paquete, sizeof(paquete));

            struct sockaddr_in a = {
                .sin_family = AF_INET,
                .sin_addr   = { .s_addr = lwip_htonl(lote.ips[i]) },
            };
            if (sendto(sock, &paquete, sizeof(paquete), 0,
                       (struct sockaddr *)&a, sizeof(a)) < 0) {
                s_envios_fallidos++;
            } else {
                s_enviados++;
            }
        }

        /* Wait for the batch's replies and only then harvest the table. */
        uint64_t hasta = aos_hal_uptime_ms() + LOTE_ESPERA_MS;
        while (aos_hal_uptime_ms() < hasta && !s_abort) {
            if (sock >= 0) {
                icmp_drenar(sock, f);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        arp_cosechar();

        s_done += LOTE_IPS;
        s_hosts_found = s_n_hosts;      /* the total is seen growing on the screen */
    }
    }

    /* The last ones take a while to come back. */
    uint64_t hasta = aos_hal_uptime_ms() + ICMP_ESCUCHA_MS;
    while (aos_hal_uptime_ms() < hasta && !s_abort) {
        if (sock >= 0) {
            icmp_drenar(sock, f);
        }
        arp_cosechar();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (sock >= 0) {
        close(sock);
    }

    ESP_LOGI(TAG, "LAN sweep (%d passes): %d sent (%d failed), "
                  "%d ICMP replies, %d ARP entries seen, %d hosts",
             PASADAS,
             s_enviados, s_envios_fallidos, s_respuestas, s_arp_vistos,
             s_n_hosts);

    /* Only now are they written: that way each host comes out with its ping
     * and its MAC already resolved, on a single line, rather than on two
     * half-filled ones. */
    char ip_s[20], mac_s[20];
    s_hosts_found = 0;
    for (int i = 0; i < s_n_hosts; i++) {
        scan_host_t *h = &s_hosts[i];
        if (!h->ping_ok && !h->mac_ok) {
            continue;
        }
        ip_txt(h->ip, ip_s, sizeof(ip_s));
        if (h->mac_ok) {
            mac_txt(h->mac, mac_s, sizeof(mac_s));
        } else {
            mac_s[0] = 0;
        }
        linea(f, "{\"t\":\"host\",\"ip\":\"%s\",\"mac\":\"%s\",\"ping\":%s}",
              ip_s, mac_s, h->ping_ok ? "true" : "false");
        h->escrito = true;
        s_hosts_found++;
    }
}

/* A batch of non-blocking connect()s: they are all fired and waited on once. */
static int puertos_lote(uint32_t ip, const uint16_t *lista, int n)
{
    int  socks[SCAN_CONCURRENTES];
    int  puertos_ok = 0;
    fd_set escribibles;
    int    maxfd = -1;

    FD_ZERO(&escribibles);
    for (int i = 0; i < n; i++) {
        socks[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (socks[i] < 0) {
            continue;
        }
        int flags = fcntl(socks[i], F_GETFL, 0);
        fcntl(socks[i], F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in a = {
            .sin_family = AF_INET,
            .sin_port   = lwip_htons(lista[i]),
            .sin_addr   = { .s_addr = lwip_htonl(ip) },
        };
        connect(socks[i], (struct sockaddr *)&a, sizeof(a));
        FD_SET(socks[i], &escribibles);
        if (socks[i] > maxfd) {
            maxfd = socks[i];
        }
    }
    if (maxfd < 0) {
        return 0;
    }

    struct timeval tv = {
        .tv_sec  = PUERTO_TIMEOUT_MS / 1000,
        .tv_usec = (PUERTO_TIMEOUT_MS % 1000) * 1000,
    };
    select(maxfd + 1, NULL, &escribibles, NULL, &tv);

    for (int i = 0; i < n; i++) {
        if (socks[i] < 0) {
            continue;
        }
        if (FD_ISSET(socks[i], &escribibles)) {
            int err = 0;
            socklen_t len = sizeof(err);
            /* Writable is not enough: a refusal also wakes the select. What
             * decides is SO_ERROR. */
            if (getsockopt(socks[i], SOL_SOCKET, SO_ERROR, &err, &len) == 0 &&
                err == 0) {
                puertos_ok |= (1 << i);
            }
        }
        close(socks[i]);
    }
    return puertos_ok;
}

static void fase_puertos(FILE *f)
{
    s_phase = AOS_SCAN_PH_PORTS;
    s_done  = 0;

    int vivos = 0;
    for (int i = 0; i < s_n_hosts; i++) {
        if (s_hosts[i].escrito) {
            vivos++;
        }
    }
    s_total = vivos * N_PUERTOS;

    char ip_s[20];
    for (int i = 0; i < s_n_hosts && !s_abort; i++) {
        scan_host_t *h = &s_hosts[i];
        if (!h->escrito) {
            continue;
        }
        for (int p = 0; p < N_PUERTOS && !s_abort; p += SCAN_CONCURRENTES) {
            int n = N_PUERTOS - p;
            if (n > SCAN_CONCURRENTES) {
                n = SCAN_CONCURRENTES;
            }
            int mascara = puertos_lote(h->ip, &PUERTOS[p], n);
            for (int b = 0; b < n; b++) {
                if ((mascara & (1 << b)) && h->n_puertos < SCAN_MAX_PUERTOS) {
                    h->puertos[h->n_puertos++] = PUERTOS[p + b];
                    s_ports_found++;
                }
            }
            s_done += n;
        }
        if (h->n_puertos > 0) {
            ip_txt(h->ip, ip_s, sizeof(ip_s));
            char lista[96];
            int o = 0;
            for (int k = 0; k < h->n_puertos; k++) {
                o += snprintf(lista + o, sizeof(lista) - (size_t)o, "%s%u",
                              k ? "," : "", h->puertos[k]);
            }
            linea(f, "{\"t\":\"puertos\",\"ip\":\"%s\",\"abiertos\":[%s]}",
                  ip_s, lista);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Names over mDNS                                                             */
/* -------------------------------------------------------------------------- */

/* Puts names to what was found. With 39 addresses and half the manufacturers
 * unknown, this is what turns the report into something readable at a glance:
 * "impresora.local" says far more than 192.168.1.45.
 *
 * It does not invent a list of services: first it does the DNS-SD meta-query
 * (_services._dns-sd._udp), which returns WHICH service types are advertised
 * on this network, and then it asks about those. In a house there are three or
 * four.
 *
 * The names come out on separate lines of the report and are NOT mixed with
 * the host ones: those were already written when the sweep finished. Each
 * finding coming out on its own line is precisely what makes it possible to
 * write while working; the browser joins them by IP. */
static void fase_mdns(FILE *f)
{
    s_phase = AOS_SCAN_PH_MDNS;
    s_done  = 0;
    s_total = 1;

    char tipos[MDNS_MAX_SERVICIOS][40];
    int  n_tipos = 0;

    for (int i = 0; i < N_MDNS_COMUNES && n_tipos < MDNS_MAX_SERVICIOS; i++) {
        snprintf(tipos[n_tipos++], sizeof(tipos[0]), "%s.%s",
                 MDNS_COMUNES[i].serv, MDNS_COMUNES[i].proto);
    }

    s_total = n_tipos ? n_tipos : 1;
    ESP_LOGI(TAG, "mDNS: %d tipos a consultar", n_tipos);

    int nombres = 0;
    char ip_s[20], nombre[80], serv[48];

    for (int i = 0; i < n_tipos && !s_abort; i++) {
        /* "_http._tcp" -> service "_http", protocol "_tcp" */
        char tipo[40];
        snprintf(tipo, sizeof(tipo), "%.39s", tipos[i]);
        char *punto = strrchr(tipo, '.');
        if (!punto || punto == tipo) {
            s_done = i + 1;
            continue;
        }
        *punto = 0;
        const char *proto = punto + 1;

        mdns_result_t *res = NULL;
        if (mdns_query_ptr(tipo, proto, MDNS_SERVICIO_MS,
                           MDNS_MAX_RESULT, &res) != ESP_OK) {
            s_done = i + 1;
            continue;
        }

        for (mdns_result_t *r = res; r; r = r->next) {
            for (mdns_ip_addr_t *a = r->addr; a; a = a->next) {
                if (a->addr.type != ESP_IPADDR_TYPE_V4) {
                    continue;
                }
                uint32_t ip = lwip_ntohl(a->addr.u_addr.ip4.addr);
                if (ip < s_primero || ip > s_ultimo) {
                    continue;
                }
                ip_txt(ip, ip_s, sizeof(ip_s));
                json_str(nombre, sizeof(nombre),
                         (const uint8_t *)(r->instance_name ? r->instance_name
                                           : (r->hostname ? r->hostname : "")),
                         64);
                json_str(serv, sizeof(serv), (const uint8_t *)tipos[i], 40);
                linea(f, "{\"t\":\"nombre\",\"ip\":\"%s\",\"nombre\":\"%s\","
                         "\"host\":\"%s\",\"servicio\":\"%s\",\"puerto\":%u}",
                      ip_s, nombre, r->hostname ? r->hostname : "", serv,
                      (unsigned)r->port);
                nombres++;
            }
        }
        mdns_query_results_free(res);
        s_done = i + 1;
    }

    ESP_LOGI(TAG, "mDNS: %d announcements with a name", nombres);
}

/* -------------------------------------------------------------------------- */
/* The task                                                                    */
/* -------------------------------------------------------------------------- */

static void scan_task(void *arg)
{
    (void)arg;

    FILE *f = fopen(s_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "could not create %s", s_path);
        s_phase = AOS_SCAN_FAILED;
        s_task  = NULL;
        vTaskDeleteWithCaps(NULL);
        return;
    }

    char ip_s[20], mask_s[20], ssid[100];
    ip_txt(s_mi_ip, ip_s, sizeof(ip_s));
    ip_txt(s_mi_mask, mask_s, sizeof(mask_s));
    json_str(ssid, sizeof(ssid), (const uint8_t *)aos_hal_net_ssid(), 32);

    struct tm t;
    aos_hal_time_now(&t);
    linea(f, "{\"t\":\"inicio\",\"fecha\":\"%04d-%02d-%02d %02d:%02d\","
             "\"ssid\":\"%s\",\"ip\":\"%s\",\"mascara\":\"%s\",\"rssi\":%d}",
          t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
          ssid, ip_s, mask_s, aos_hal_net_rssi());

    if (s_flags & AOS_SCAN_WIFI) {
        fase_wifi(f);
    }
    if ((s_flags & AOS_SCAN_HOSTS) && !s_abort && s_primero <= s_ultimo) {
        fase_hosts(f);
    }
    if ((s_flags & AOS_SCAN_PORTS) && !s_abort && s_hosts_found > 0) {
        fase_puertos(f);
    }
    if ((s_flags & AOS_SCAN_MDNS) && !s_abort) {
        fase_mdns(f);
    }

    uint32_t ms = (uint32_t)(aos_hal_uptime_ms() - s_inicio_ms);
    linea(f, "{\"t\":\"fin\",\"redes\":%d,\"equipos\":%d,\"puertos\":%d,"
             "\"ms\":%u,\"cortado\":%s}",
          s_wifi_found, s_hosts_found, s_ports_found, (unsigned)ms,
          s_abort ? "true" : "false");
    fclose(f);

    ESP_LOGI(TAG, "scan finished: %d networks, %d hosts, %d ports in %u ms -> %s",
             s_wifi_found, s_hosts_found, s_ports_found, (unsigned)ms, s_path);

    s_phase = s_abort ? AOS_SCAN_FAILED : AOS_SCAN_DONE;
    s_task  = NULL;
    vTaskDeleteWithCaps(NULL);
}

/* -------------------------------------------------------------------------- */
/* API                                                                         */
/* -------------------------------------------------------------------------- */

bool aos_hal_scan_start(uint32_t flags)
{
    if (s_task) {
        return false;
    }
    if (!flags) {
        flags = AOS_SCAN_TODO;
    }
    if (aos_hal_net_state() != AOS_NET_CONNECTED) {
        /* With no connection the networks around can still be looked at, but
         * there is no LAN to sweep. */
        flags &= AOS_SCAN_WIFI;      /* no network, no LAN and no mDNS to look at */
        if (!flags) {
            return false;
        }
    }

    s_mi_ip = s_mi_mask = s_red = 0;
    s_primero = 1;
    s_ultimo  = 0;                  /* empty range = nothing is swept */

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr) {
        s_mi_ip   = lwip_ntohl(ip.ip.addr);
        s_mi_mask = lwip_ntohl(ip.netmask.addr);
        s_red     = s_mi_ip & s_mi_mask;

        uint32_t hosts = ~s_mi_mask;
        /* Range guard: only /24 or smaller. With a /16 this would stop being a
         * scanner of your house, so the surrounding /24 is what gets swept. */
        if (hosts > 255) {
            ESP_LOGW(TAG, "prefix wider than /24: only our own /24 is swept");
            s_red  = s_mi_ip & 0xFFFFFF00u;
            hosts  = 255;
        }
        s_primero = s_red + 1;
        s_ultimo  = s_red + hosts - 1;
        if (s_ultimo - s_primero + 1 > SCAN_MAX_HOSTS) {
            s_ultimo = s_primero + SCAN_MAX_HOSTS - 1;
        }
    }

    if (!s_hosts) {
        s_hosts = heap_caps_malloc(sizeof(scan_host_t) * SCAN_MAX_HOSTS,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_hosts) {
            ESP_LOGE(TAG, "no PSRAM for the host table");
            return false;
        }
    }
    memset(s_hosts, 0, sizeof(scan_host_t) * SCAN_MAX_HOSTS);
    s_n_hosts = 0;

    mkdir(aos_hal_path_scans(), 0777);
    struct tm t;
    aos_hal_time_now(&t);
    if (aos_hal_time_is_valid()) {
        snprintf(s_path, sizeof(s_path), "%s/%04d%02d%02d-%02d%02d.ndjson",
                 aos_hal_path_scans(), t.tm_year + 1900, t.tm_mon + 1,
                 t.tm_mday, t.tm_hour, t.tm_min);
    } else {
        /* With no valid time it cannot be named by date, and two files that
         * overwrite each other is worse than an ugly name. */
        snprintf(s_path, sizeof(s_path), "%s/barrido-%u.ndjson",
                 aos_hal_path_scans(), (unsigned)(aos_hal_uptime_ms() / 1000));
    }

    s_flags       = flags;
    s_abort       = false;
    s_phase       = AOS_SCAN_PH_WIFI;
    s_wifi_found  = s_hosts_found = s_ports_found = 0;
    s_enviados = s_envios_fallidos = s_respuestas = s_arp_vistos = 0;
    s_done = s_total = 0;
    s_inicio_ms   = aos_hal_uptime_ms();

    /* The stack goes in PSRAM, not in internal RAM.
     *
     * This task is an unusual case and that is why it is worth it: it does not
     * live forever -it is created when a sweep is requested and deletes itself
     * on finishing-, so sending it outside does not raise the floor of free
     * internal RAM by a single byte. What it avoids is the PEAK: its 5 KB came
     * out of internal RAM exactly when internal RAM is tightest, which is with
     * WiFi up, buffers in flight and sockets open, that is, precisely during a
     * sweep. And latency is all the same to it: it spends its life waiting on
     * network timeouts, so PSRAM's cache tax goes unnoticed.
     *
     * NOTE: a task created with xTaskCreateWithCaps MUST be deleted with
     * vTaskDeleteWithCaps, or the stack is not freed. Those are the two exits
     * of scan_task() and there is no other: nobody deletes it from outside
     * (aos_hal_scan_stop() only raises s_abort and waits). */
    if (xTaskCreateWithCaps(scan_task, "aos_scan", 5120, NULL, 4, &s_task,
                            MALLOC_CAP_SPIRAM) != pdPASS) {
        s_phase = AOS_SCAN_FAILED;
        return false;
    }
    return true;
}

void aos_hal_scan_stop(void)
{
    s_abort = true;
}

bool aos_hal_scan_status(aos_scan_status_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->phase       = s_phase;
    out->wifi_found  = s_wifi_found;
    out->hosts_found = s_hosts_found;
    out->ports_found = s_ports_found;
    out->done        = s_done;
    out->total       = s_total;
    out->elapsed_ms  = s_inicio_ms
                     ? (uint32_t)(aos_hal_uptime_ms() - s_inicio_ms) : 0;
    snprintf(out->path, sizeof(out->path), "%s", s_path);
    return true;
}
