/*
 * AmoledOS - BLE stack on top of NimBLE. See aos_ble.h.
 *
 * Division of labour: aos_ancs.c assembles and takes apart the protocol's
 * bytes -and is therefore tested on the Mac with tools/ancs_harness.c-; this
 * file is everything a real phone on the other side needs: advertising,
 * pairing, GATT discovery and the routing of notifications.
 *
 * EVERYTHING here runs in NimBLE's host task. Not one line may touch LVGL:
 * what goes out to the interface goes out through aos_notif_push(), which
 * leaves the news in a queue the LVGL task empties on its tick.
 */
#include "aos_ble.h"
#include "aos_ancs.h"
#include "aos_ams.h"
#include "aos_notif_internal.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "aos_ble";

/* Provided by the NimBLE component when BT_NIMBLE_NVS_PERSIST is set: it is
 * what makes the pairing survive a restart. */
void ble_store_config_init(void);

/* -------------------------------------------------------------------------- */
/* ANCS UUIDs                                                                  */
/* -------------------------------------------------------------------------- */

/* 7905F431-B5CE-4E99-A40F-4B1E122D00D0 and its three characteristics. They go
 * backwards because BLE_UUID128_INIT takes the bytes little endian. */
static const ble_uuid128_t UUID_ANCS = BLE_UUID128_INIT(
    0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4,
    0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79);

/* 9FBF120D-6301-42D9-8C58-25E699A21DBD - notifies */
static const ble_uuid128_t UUID_NOTIFICATION_SOURCE = BLE_UUID128_INIT(
    0xBD, 0x1D, 0xA2, 0x99, 0xE6, 0x25, 0x58, 0x8C,
    0xD9, 0x42, 0x01, 0x63, 0x0D, 0x12, 0xBF, 0x9F);

/* 69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9 - is written */
static const ble_uuid128_t UUID_CONTROL_POINT = BLE_UUID128_INIT(
    0xD9, 0xD9, 0xAA, 0xFD, 0xBD, 0x9B, 0x21, 0x98,
    0xA8, 0x49, 0xE1, 0x45, 0xF3, 0xD8, 0xD1, 0x69);

/* 22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB - notifies */
static const ble_uuid128_t UUID_DATA_SOURCE = BLE_UUID128_INIT(
    0xFB, 0x7B, 0x7C, 0xCE, 0x6A, 0xB3, 0x44, 0xBE,
    0xB5, 0x4B, 0xD6, 0x24, 0xE9, 0xC6, 0xEA, 0x22);

/* --------------------------------------------------------------------------
 * Apple Media Service: 89D3502B-0F36-433A-8EF4-C502AD55F8DC
 *
 * The usual, with one rule on top: **notifications are worth more than music
 * control**. See the long comment in aos_ams.h.
 * -------------------------------------------------------------------------- */
static const ble_uuid128_t UUID_AMS = BLE_UUID128_INIT(
    0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E,
    0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89);

/* 9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2 - is written */
static const ble_uuid128_t UUID_AMS_REMOTE = BLE_UUID128_INIT(
    0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8,
    0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B);

/* 2F7CABCE-808D-411F-9A0C-BB92BA96C102 - notifies and is written */
static const ble_uuid128_t UUID_AMS_ENTITY_UPDATE = BLE_UUID128_INIT(
    0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A,
    0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F);

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */

#define NOMBRE      "AmoledOS"
#define SIN_CONN    0xFFFF

static volatile bool     s_running;
static volatile bool     s_conectado;
static volatile bool     s_cifrado;
static volatile bool     s_pair_wanted;
static volatile uint32_t s_pair_code;
static volatile uint16_t s_conn = SIN_CONN;

static uint8_t  s_addr_type;
static uint16_t s_h_notification_source;
static uint16_t s_h_data_source;
static uint16_t s_h_control_point;
static uint16_t s_h_svc_end;

/* The phone's name, read from its 0x2A00 characteristic. Written by the host
 * task and read by the LVGL one through aos_hal_bt_peer(). It is written ONCE
 * per connection and always ends in a zero, so the worst that can happen is
 * that a Settings refresh catches it half copied and shows a truncated name
 * for the two seconds until the next one. A mutex here would mean being able
 * to stall the drawing from a radio callback, which is worse. */
static char s_peer[40];

/* --------------------------------------------------------------------------
 * What the phone publishes besides notifications
 *
 * Walking its GATT tree (section 25 of the handoff) showed that an iPhone also
 * exposes the Current Time Service and the Battery Service. With the first the
 * watch sets itself WITHOUT WIFI —the phone is always next to it and always on
 * time—, and with the second you can see how much battery the phone has left.
 *
 * The time is NOT written from here. `aos_hal_time_set()` touches the RTC over
 * I2C and also writes NVS, and this runs in NimBLE's host task, which owns
 * neither that bus nor the stack for it. It is noted down and applied by
 * aos_ble_tick(), which runs in the main task: the same deferral the whole
 * rest of the project uses.
 * -------------------------------------------------------------------------- */

static volatile bool   s_hora_lista;
static struct tm       s_hora;              /* written by the radio, read by the tick */
static volatile int    s_bateria = -1;      /* 0..100, -1 = unknown */
static uint16_t        s_h_bateria;         /* value handle, for the notifications */

/* --- Apple Media Service ------------------------------------------------- */
static uint16_t        s_h_ams_entity;      /* Entity Update: notifies          */
static uint16_t        s_h_ams_cccd;        /* its CCCD, so it can be SILENCED  */
static uint16_t        s_h_ams_remote;      /* Remote Command: is written       */
static uint16_t        s_h_ams_end;
static aos_ams_state_t s_ams;
static int64_t         s_ams_pos_us;        /* when the last position arrived   */
static volatile bool   s_ams_listo;

/* Data Source reassembly. It has to be INTERNAL RAM -and so carries no
 * AOS_BSS_PSRAM- because NimBLE's callback writes it. It is the only exception
 * to the rule of sending everything outside; the rest of the notification
 * store does live in PSRAM. */
#define DS_MAX  640
static uint8_t  s_ds[DS_MAX];
static uint16_t s_ds_len;

/* -------------------------------------------------------------------------- */
/* The F0 measurement (see aos_ble.h)                                          */
/* -------------------------------------------------------------------------- */

void aos_ble_measure_report(const char *etiqueta)
{
    ESP_LOGW(TAG, "[MEASURE %-14s] internal=%7u  exec=%7u  exec_largest=%7u  "
                  "psram=%8u  internal_low=%7u",
             etiqueta ? etiqueta : "?",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_EXEC),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_EXEC),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
}

/* -------------------------------------------------------------------------- */
/* Advertising                                                                 */
/* -------------------------------------------------------------------------- */

static int gap_event(struct ble_gap_event *event, void *arg);
static void ams_start(void);
static void ams_stop(void);

/* The advertising packet carries three things, and all three have to be there:
 *
 *   flags                3 bytes   general discoverable + no BR/EDR
 *   complete name       10 bytes   "AmoledOS"
 *   ANCS solicitation   18 bytes   the 128-bit UUID in field 0x15
 *   ------------------------------
 *                       31 bytes   which is exactly what fits
 *
 * The service solicitation is how an accessory tells iOS "I want your
 * notifications". The name is what makes it show up in the phone's
 * Settings -> Bluetooth list.
 *
 * > The first version left the name OUT, in the scan response, on the grounds
 * > that it did not fit. **It did fit**: the arithmetic was added up wrong (21
 * > of the packet + 10 of the name is 31, that is, exactly, and it was read as
 * > overflowing). The result was that the watch advertised perfectly well and
 * > **did not appear in the iPhone's list**, because iOS builds that list from
 * > the advertising packet's name and not from the scan response's. Without a
 * > phone to test against, an arithmetic mistake looks identical to a stack
 * > that works.
 *
 * The scan response is left with the transmit power, which is informative and
 * needed by nobody, but it leaves the second packet built in case something
 * ever has to go in there. */
static void advertise(void)
{
    if (!s_running) {
        return;
    }

    uint8_t adv[31];
    int i = 0;

    adv[i++] = 2;
    adv[i++] = BLE_HS_ADV_TYPE_FLAGS;
    adv[i++] = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    size_t largo_nombre = strlen(NOMBRE);
    adv[i++] = (uint8_t)(1 + largo_nombre);
    adv[i++] = BLE_HS_ADV_TYPE_COMP_NAME;
    memcpy(&adv[i], NOMBRE, largo_nombre);
    i += (int)largo_nombre;

    adv[i++] = 17;
    adv[i++] = 0x15;                    /* 128-bit UUID solicitation */
    memcpy(&adv[i], UUID_ANCS.value, 16);
    i += 16;

    int rc = ble_gap_adv_set_data(adv, i);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data: %d (%d bytes)", rc, i);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.tx_pwr_lvl_is_present = 1;
    rsp.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields: %d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start: %d", rc);
        return;
    }

    /* The raw bytes to the log. A malformed advertising packet is accepted all
     * the same -the controller only checks that the lengths add up- and then
     * appears in no list, without saying why. With this it can be compared
     * against the paper. */
    char hex[3 * sizeof(adv) + 1];
    for (int k = 0; k < i; k++) {
        snprintf(&hex[k * 3], 4, "%02X ", adv[k]);
    }
    ESP_LOGI(TAG, "advertising as '%s' (%d bytes): %s", NOMBRE, i, hex);
}

/* -------------------------------------------------------------------------- */
/* ANCS discovery                                                              */
/* -------------------------------------------------------------------------- */

/* Discovery goes IN A CHAIN, one procedure at a time.
 *
 * The first version launched the descriptor search from inside the
 * characteristics callback, and asked for the phone's name in parallel with
 * the service search into the bargain: three GATT procedures stacked on top of
 * one another on the same connection. The result was that **discovery stopped
 * at the first characteristic**: the Notification Source turned up and neither
 * the Data Source nor the Control Point did. And that does not look like an
 * error, it looks like a phone that sends no notifications — without a Control
 * Point the attributes cannot be requested, and without the Data Source's
 * handle the reply is not recognised when it arrives.
 *
 * So now each step starts when the previous one has really finished, that is,
 * when NimBLE says so with BLE_HS_EDONE:
 *
 *   service -> characteristics -> Notification Source CCCD
 *           -> Data Source CCCD -> phone name
 */

#define CHRS_MAX  8

typedef struct {
    uint16_t def;
    uint16_t val;
} chr_t;

static chr_t    s_chrs[CHRS_MAX];
static int      s_chr_count;
static uint16_t s_svc_start;

static void suscribir(int paso);

static int on_subscribe(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (error->status != 0) {
        ESP_LOGE(TAG, "could not subscribe: %d", error->status);
    }
    return 0;
}

/* How far a characteristic reaches: to just before the next one, or to the end
 * of the service if it is the last. Without this, searching for descriptors
 * "from here to the end" also returns those of the ones that come after. */
static uint16_t fin_de(uint16_t val)
{
    uint16_t fin = s_h_svc_end;
    for (int i = 0; i < s_chr_count; i++) {
        if (s_chrs[i].def > val && s_chrs[i].def - 1 < fin) {
            fin = (uint16_t)(s_chrs[i].def - 1);
        }
    }
    return fin < val ? val : fin;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                  void *arg)
{
    int paso = (int)(intptr_t)arg;
    (void)chr_val_handle;

    if (error->status == BLE_HS_EDONE) {
        suscribir(paso + 1);            /* only now the next one */
        return 0;
    }
    if (error->status != 0 || !dsc) {
        ESP_LOGW(TAG, "descriptores: %d", error->status);
        suscribir(paso + 1);
        return 0;
    }
    /* The client characteristic configuration descriptor: writing 0x0001 to it
     * is what switches that characteristic's notifications on. */
    if (ble_uuid_cmp(&dsc->uuid.u,
                     BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
        uint8_t cccd[2] = { 0x01, 0x00 };
        ble_gattc_write_flat(conn, dsc->handle, cccd, sizeof(cccd),
                             on_subscribe, NULL);
    }
    return 0;
}

static int on_name(uint16_t conn, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg);
static int on_hora(uint16_t conn, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg);
static int on_bateria(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg);

/* The whole chain, one procedure at a time:
 *
 *   0  Notification Source CCCD     3  the phone's time
 *   1  Data Source CCCD             4  its battery, and subscribing
 *   2  the phone's name
 *
 * What is needed for notifications comes first. The time, the battery and the
 * name are a bonus: if any of them fails, the watch goes on doing its job. */
static void suscribir(int paso)
{
    uint16_t conn = s_conn;
    if (conn == SIN_CONN) {
        return;
    }

    if (paso == 0 && s_h_notification_source) {
        ble_gattc_disc_all_dscs(conn, s_h_notification_source,
                                fin_de(s_h_notification_source), on_dsc,
                                (void *)(intptr_t)0);
        return;
    }
    if (paso <= 1 && s_h_data_source) {
        ble_gattc_disc_all_dscs(conn, s_h_data_source,
                                fin_de(s_h_data_source), on_dsc,
                                (void *)(intptr_t)1);
        return;
    }
    if (paso <= 2) {
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A00),
                               on_name, NULL);
        return;
    }
    if (paso <= 3) {
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A2B),
                               on_hora, NULL);
        return;
    }
    ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A19),
                           on_bateria, NULL);
}

/* --------------------------------------------------------------------------
 * The phone's time (Current Time Service, 0x2A2B)
 *
 * Ten bytes: year little endian, month, day, hour, minute, second, day of the
 * week, fractions of a second and the adjustment reason. It is the phone's
 * LOCAL time with no time zone, which is exactly what we want to show.
 * -------------------------------------------------------------------------- */
static int on_hora(uint16_t conn, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)arg;
    if (error->status == BLE_HS_EDONE) {
        suscribir(4);
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om || s_hora_lista) {
        return 0;
    }

    uint8_t b[10] = { 0 };
    uint16_t n = OS_MBUF_PKTLEN(attr->om);
    if (n > sizeof(b)) {
        n = sizeof(b);
    }
    if (n < 7 || ble_hs_mbuf_to_flat(attr->om, b, n, NULL) != 0) {
        return 0;
    }

    unsigned ano = (unsigned)(b[0] | (b[1] << 8));
    /* A phone claiming to be in 1970 or in 2200 is talking nonsense, and
     * setting the clock from that is worse than not setting it. */
    if (ano < 2020 || ano > 2100 || b[2] < 1 || b[2] > 12 ||
        b[3] < 1 || b[3] > 31 || b[4] > 23 || b[5] > 59 || b[6] > 60) {
        ESP_LOGW(TAG, "the phone's time makes no sense, discarding it");
        return 0;
    }

    memset(&s_hora, 0, sizeof(s_hora));
    s_hora.tm_year  = (int)ano - 1900;
    s_hora.tm_mon   = b[2] - 1;
    s_hora.tm_mday  = b[3];
    s_hora.tm_hour  = b[4];
    s_hora.tm_min   = b[5];
    s_hora.tm_sec   = b[6];
    s_hora.tm_isdst = -1;
    s_hora_lista = true;            /* applied by the tick, not by this task */
    return 0;
}

/* --------------------------------------------------------------------------
 * The phone's battery (Battery Service, 0x2A19)
 *
 * One byte with the percentage. Besides reading it, the notification is
 * subscribed to, so the number keeps itself up to date instead of freezing at
 * the one from connection time.
 * -------------------------------------------------------------------------- */
static int on_bat_dsc(uint16_t conn, const struct ble_gatt_error *error,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                      void *arg)
{
    (void)chr_val_handle; (void)arg;
    if (error->status == BLE_HS_EDONE) {
        /* Last link: only now, with ANCS subscribed a good while ago and the
         * time and battery already read, is the music asked of the phone. If
         * this fails, it takes none of the above with it. */
        ams_start();
        return 0;
    }
    if (error->status != 0 || !dsc) {
        return 0;
    }
    if (ble_uuid_cmp(&dsc->uuid.u,
                     BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
        uint8_t cccd[2] = { 0x01, 0x00 };
        ble_gattc_write_flat(conn, dsc->handle, cccd, sizeof(cccd),
                             on_subscribe, NULL);
    }
    return 0;
}

static int on_bateria(uint16_t conn, const struct ble_gatt_error *error,
                      struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        /* The battery service is four handles, so its configuration descriptor
         * falls within the two following the value. Bounded and not "to the
         * end": searching too far brings in descriptors of other
         * characteristics, which is what broke ANCS discovery. */
        if (s_h_bateria) {
            ble_gattc_disc_all_dscs(conn, s_h_bateria, (uint16_t)(s_h_bateria + 2),
                                    on_bat_dsc, NULL);
        }
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om) {
        return 0;
    }
    uint8_t pct = 0;
    if (ble_hs_mbuf_to_flat(attr->om, &pct, 1, NULL) == 0 && pct <= 100) {
        s_h_bateria = attr->handle;
        s_bateria   = pct;
        ESP_LOGI(TAG, "phone battery: %u%%", pct);
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn; (void)arg;

    if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "ANCS: notif=%u data=%u control=%u",
                 s_h_notification_source, s_h_data_source, s_h_control_point);
        if (!s_h_control_point || !s_h_data_source || !s_h_notification_source) {
            ESP_LOGE(TAG, "ANCS incompleto: faltan caracteristicas");
        }
        suscribir(0);
        return 0;
    }
    if (error->status != 0 || !chr) {
        return 0;
    }

    if (s_chr_count < CHRS_MAX) {
        s_chrs[s_chr_count].def = chr->def_handle;
        s_chrs[s_chr_count].val = chr->val_handle;
        s_chr_count++;
    }

    if (ble_uuid_cmp(&chr->uuid.u, &UUID_NOTIFICATION_SOURCE.u) == 0) {
        s_h_notification_source = chr->val_handle;
    } else if (ble_uuid_cmp(&chr->uuid.u, &UUID_DATA_SOURCE.u) == 0) {
        s_h_data_source = chr->val_handle;
    } else if (ble_uuid_cmp(&chr->uuid.u, &UUID_CONTROL_POINT.u) == 0) {
        s_h_control_point = chr->val_handle;
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_h_svc_end) {
            ESP_LOGW(TAG, "the ANCS service did not show up");
        }
        return 0;
    }
    if (error->status != 0 || !svc) {
        ESP_LOGW(TAG, "looking for ANCS: %d", error->status);
        return 0;
    }
    s_svc_start = svc->start_handle;
    s_h_svc_end = svc->end_handle;
    s_chr_count = 0;
    ESP_LOGI(TAG, "ANCS at %u..%u", svc->start_handle, svc->end_handle);
    ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle,
                            on_chr, NULL);
    return 0;
}

/* The phone's name: Device Name characteristic (0x2A00) of the Generic Access
 * service, which iOS always publishes. It is the only thing that lets Settings
 * say "iPhone de Charlie" instead of an address in hexadecimal.
 *
 * It keeps the FIRST reply: reading by UUID across the whole tree returns
 * several, and the later ones carried rubbish that overwrote the good name. */
/* --------------------------------------------------------------------------
 * DIAGNOSTIC: what else the phone exposes
 *
 * ANCS delivers notifications and nothing else. But the iPhone publishes other
 * GATT services to a device it has keys with, and knowing WHICH is the
 * difference between guessing and knowing. This walks its whole tree and lists
 * them.
 *
 * Runs at the END of the discovery chain, never in parallel: the lesson of
 * section 19 of the handoff.
 * -------------------------------------------------------------------------- */
#if AOS_BLE_DIAG_GATT

static const char *servicio_conocido(const ble_uuid_t *u)
{
    if (u->type == BLE_UUID_TYPE_16) {
        switch (((const ble_uuid16_t *)u)->value) {
        case 0x1800: return "Generic Access";
        case 0x1801: return "Generic Attribute";
        case 0x1805: return "Current Time  <-- the phone's clock";
        case 0x180A: return "Device Information";
        case 0x180F: return "Battery  <-- battery level";
        case 0x1811: return "Alert Notification";
        case 0x1812: return "HID";
        case 0x181C: return "User Data";
        default:     return "";
        }
    }
    return "(128 bits)";
}

/* Really read the two that are worth it: a service existing does not mean it
 * delivers anything. */
static int diag_bateria(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg);

static int diag_hora(uint16_t conn, const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    /* Reading by UUID calls once per result AND once on finishing. Chaining
     * the next one on every call -and not only on finishing- fires one read
     * per result and the log fills up with "read by uuid". */
    if (error->status == BLE_HS_EDONE) {
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A19),
                               diag_bateria, NULL);
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om) {
        ESP_LOGW(TAG, "[GATT] the time could not be read (%d)", error->status);
    } else {
        uint8_t b[10] = {0};
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (n > sizeof(b)) n = sizeof(b);
        if (ble_hs_mbuf_to_flat(attr->om, b, n, NULL) == 0 && n >= 7) {
            ESP_LOGW(TAG, "[GATT] phone time: %04u-%02u-%02u %02u:%02u:%02u",
                     (unsigned)(b[0] | (b[1] << 8)), b[2], b[3], b[4], b[5], b[6]);
        }
    }
    return 0;
}

static int diag_bateria(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)arg;
    if (error->status == BLE_HS_EDONE) {
        ESP_LOGW(TAG, "[GATT] end of the reads");
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om) {
        ESP_LOGW(TAG, "[GATT] the battery could not be read (0x%X)", error->status);
        return 0;
    }
    uint8_t pct = 0;
    if (ble_hs_mbuf_to_flat(attr->om, &pct, 1, NULL) == 0) {
        ESP_LOGW(TAG, "[GATT] phone battery: %u%%", pct);
    }
    return 0;
}

static int diag_svc(uint16_t conn, const struct ble_gatt_error *error,
                    const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn; (void)arg;
    if (error->status == BLE_HS_EDONE) {
        ESP_LOGW(TAG, "[GATT] end of the walk, now to read");
        ble_gattc_read_by_uuid(conn, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A2B),
                               diag_hora, NULL);
        return 0;
    }
    if (error->status != 0 || !svc) {
        return 0;
    }
    char txt[BLE_UUID_STR_LEN];
    ble_uuid_to_str(&svc->uuid.u, txt);
    ESP_LOGW(TAG, "[GATT] %s  %u..%u  %s", txt, svc->start_handle,
             svc->end_handle, servicio_conocido(&svc->uuid.u));
    return 0;
}

static void diag_gatt(uint16_t conn)
{
    static bool hecho;
    if (hecho) {
        return;
    }
    hecho = true;
    ESP_LOGW(TAG, "[GATT] walking what the phone exposes...");
    ble_gattc_disc_all_svcs(conn, diag_svc, NULL);
}

#else
static void diag_gatt(uint16_t conn) { (void)conn; }
#endif

static int on_name(uint16_t conn, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        suscribir(3);
        return 0;
    }
    if (error->status != 0 || !attr || !attr->om || s_peer[0]) {
        return 0;
    }
    uint16_t n = OS_MBUF_PKTLEN(attr->om);
    if (n == 0 || n >= sizeof(s_peer)) {
        n = sizeof(s_peer) - 1;
    }
    char tmp[sizeof(s_peer)];
    if (ble_hs_mbuf_to_flat(attr->om, tmp, n, NULL) == 0) {
        tmp[n] = '\0';
        /* That it is text and not loose bytes: otherwise the phone's name in
         * Settings comes out with holes in it. */
        for (uint16_t k = 0; k < n; k++) {
            if ((unsigned char)tmp[k] < 0x20) {
                return 0;
            }
        }
        snprintf(s_peer, sizeof(s_peer), "%s", tmp);
        ESP_LOGI(TAG, "phone: %s", s_peer);
        diag_gatt(conn);
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Apple Media Service
 *
 * Starts AFTER everything else, and only if the user asked for it: the
 * preference is born off and is switched on from the BT Control app. If any of
 * this fails, ANCS has been subscribed and working for several steps already.
 * -------------------------------------------------------------------------- */

static bool media_pedido(void)
{
    int32_t v = 0;
    aos_hal_pref_get_i32("ms_on", &v);
    return v != 0;
}

static int on_ams_sub2(uint16_t conn, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (error->status != 0) {
        ESP_LOGW(TAG, "AMS: could not ask for the player state (%d)",
                 error->status);
        return 0;
    }
    s_ams_listo = true;
    ESP_LOGI(TAG, "AMS ready: title, artist, album, duration and state");
    return 0;
}

/* The two subscriptions go one after the other, not both together: it is the
 * same lesson that broke ANCS discovery. */
static int on_ams_sub1(uint16_t conn, const struct ble_gatt_error *error,
                       struct ble_gatt_attr *attr, void *arg)
{
    (void)attr; (void)arg;
    if (error->status != 0) {
        ESP_LOGW(TAG, "AMS: could not ask for the track (%d)", error->status);
        return 0;
    }
    uint8_t cmd[8];
    int n = aos_ams_cmd_subscribe(cmd, sizeof(cmd), AOS_AMS_PLAYER);
    if (n > 0) {
        ble_gattc_write_flat(conn, s_h_ams_entity, cmd, (uint16_t)n,
                             on_ams_sub2, NULL);
    }
    return 0;
}

static int on_ams_dsc(uint16_t conn, const struct ble_gatt_error *error,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                      void *arg)
{
    (void)chr_val_handle; (void)arg;

    if (error->status == BLE_HS_EDONE) {
        if (!s_h_ams_cccd) {
            ESP_LOGW(TAG, "AMS: no CCCD, cannot listen");
            return 0;
        }
        uint8_t cmd[8];
        int n = aos_ams_cmd_subscribe(cmd, sizeof(cmd), AOS_AMS_TRACK);
        if (n > 0) {
            ble_gattc_write_flat(conn, s_h_ams_entity, cmd, (uint16_t)n,
                                 on_ams_sub1, NULL);
        }
        return 0;
    }
    if (error->status != 0 || !dsc) {
        return 0;
    }
    if (ble_uuid_cmp(&dsc->uuid.u,
                     BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
        s_h_ams_cccd = dsc->handle;
        uint8_t cccd[2] = { 0x01, 0x00 };
        ble_gattc_write_flat(conn, dsc->handle, cccd, sizeof(cccd),
                             on_subscribe, NULL);
    }
    return 0;
}

static int on_ams_chr(uint16_t conn, const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_h_ams_entity || !s_h_ams_remote) {
            ESP_LOGW(TAG, "AMS incompleto: entidad=%u remoto=%u",
                     s_h_ams_entity, s_h_ams_remote);
            return 0;
        }
        /* Bounded to this characteristic, not to the end of the service. */
        ble_gattc_disc_all_dscs(conn, s_h_ams_entity,
                                (uint16_t)(s_h_ams_entity + 2), on_ams_dsc, NULL);
        return 0;
    }
    if (error->status != 0 || !chr) {
        return 0;
    }
    if (ble_uuid_cmp(&chr->uuid.u, &UUID_AMS_ENTITY_UPDATE.u) == 0) {
        s_h_ams_entity = chr->val_handle;
    } else if (ble_uuid_cmp(&chr->uuid.u, &UUID_AMS_REMOTE.u) == 0) {
        s_h_ams_remote = chr->val_handle;
    }
    return 0;
}

static int on_ams_svc(uint16_t conn, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == BLE_HS_EDONE) {
        if (!s_h_ams_end) {
            ESP_LOGW(TAG, "the phone does not publish AMS");
        }
        return 0;
    }
    if (error->status != 0 || !svc) {
        return 0;
    }
    s_h_ams_end = svc->end_handle;
    ESP_LOGI(TAG, "AMS at %u..%u", svc->start_handle, svc->end_handle);
    ble_gattc_disc_all_chrs(conn, svc->start_handle, svc->end_handle,
                            on_ams_chr, NULL);
    return 0;
}

static void ams_start(void)
{
    if (s_conn == SIN_CONN || s_ams_listo || !media_pedido()) {
        return;
    }
    memset(&s_ams, 0, sizeof(s_ams));
    ble_gattc_disc_svc_by_uuid(s_conn, &UUID_AMS.u, on_ams_svc, NULL);
}

/* Silencing it completely: 0 is written to the CCCD and the phone stops
 * sending. This is what makes "removing music control" a tap on the screen and
 * not a recompilation. */
static void ams_stop(void)
{
    if (s_conn != SIN_CONN && s_h_ams_cccd) {
        uint8_t cccd[2] = { 0x00, 0x00 };
        ble_gattc_write_flat(s_conn, s_h_ams_cccd, cccd, sizeof(cccd),
                             NULL, NULL);
        ESP_LOGI(TAG, "AMS off: the phone stops sending music");
    }
    s_ams_listo = false;
    memset(&s_ams, 0, sizeof(s_ams));
}

/* -------------------------------------------------------------------------- */
/* Notifications                                                               */
/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Attribute requests go ONE AT A TIME, queued
 *
 * The first version wrote the request to the Control Point as soon as the
 * notice arrived, without waiting for the previous one. With that, on
 * connecting -which is when iOS dumps everything it had pending all at once-
 * the log filled up with
 *
 *     GATTC proc alloc failed; op=write attr=0x0025
 *
 * forty times and not a single notification got in: NimBLE has four GATT
 * client procedures and it ran out of all of them.
 *
 * Raising that limit would have covered the symptom without fixing anything,
 * because the underlying problem is a different one: **there is only one
 * reassembly buffer**. Two requests in flight return two replies that get
 * tangled in the same buffer and the parsing breaks. Serialising is not an
 * optimisation, it is the only way this can work.
 * -------------------------------------------------------------------------- */

#define PEDIDOS_MAX     16
#define PEDIDO_TOPE_US  (3 * 1000 * 1000)   /* if it does not answer, move on to the next */

/* Every request carries its category and its flags with it. Storing them
 * separately, in a table indexed by UID, was the defect that made eight full
 * screens of old notifications pop up on connecting: see the comment in
 * aos_ancs_notification_source(). */
typedef struct {
    uint32_t uid;
    uint8_t  cat;
    uint8_t  flags;
} pedido_t;

static pedido_t s_cola[PEDIDOS_MAX];
static uint8_t  s_cola_w, s_cola_r;
static pedido_t s_en_vuelo;
static bool     s_pidiendo;
static int64_t  s_pidiendo_us;

static void encolar(uint32_t uid, uint8_t cat, uint8_t flags)
{
    if ((uint8_t)(s_cola_w - s_cola_r) >= PEDIDOS_MAX) {
        s_cola_r++;                 /* the oldest is lost, not the newest */
    }
    s_cola[s_cola_w % PEDIDOS_MAX] = (pedido_t){ uid, cat, flags };
    s_cola_w++;
}

static void pedir_siguiente(void)
{
    if (s_pidiendo || s_cola_r == s_cola_w) {
        return;
    }
    if (s_conn == SIN_CONN || !s_h_control_point) {
        return;
    }

    pedido_t p = s_cola[s_cola_r % PEDIDOS_MAX];

    uint8_t cmd[32];
    int n = aos_ancs_cmd_atributos(cmd, sizeof(cmd), p.uid);
    if (n <= 0) {
        s_cola_r++;
        return;
    }
    int rc = ble_gattc_write_flat(s_conn, s_h_control_point, cmd, (uint16_t)n,
                                  NULL, NULL);
    if (rc != 0) {
        /* No slot right now: it is left in the queue and the heartbeat retries
         * it. Losing the notification by not retrying would be worse. */
        ESP_LOGW(TAG, "the request did not go out (%d), retrying", rc);
        return;
    }
    s_cola_r++;
    s_en_vuelo    = p;
    s_ds_len      = 0;
    s_pidiendo    = true;
    s_pidiendo_us = esp_timer_get_time();
}

static void pedido_listo(void)
{
    s_pidiendo = false;
    s_ds_len   = 0;
    pedir_siguiente();
}

static void notify_rx(struct ble_gap_event *event)
{
    struct os_mbuf *om = event->notify_rx.om;
    if (!om) {
        return;
    }
    uint16_t len = OS_MBUF_PKTLEN(om);

    if (event->notify_rx.attr_handle == s_h_notification_source) {
        uint8_t p[16];
        if (len > sizeof(p)) {
            len = sizeof(p);
        }
        if (ble_hs_mbuf_to_flat(om, p, len, NULL) != 0) {
            return;
        }
        uint32_t uid = 0;
        uint8_t  cat = 0, flags = 0;
        if (aos_ancs_notification_source(p, len, &uid, &cat, &flags) ==
            AOS_ANCS_PEDIR_ATRIBUTOS) {
            encolar(uid, cat, flags);
            pedir_siguiente();
        }
        return;
    }

    if (s_h_ams_entity && event->notify_rx.attr_handle == s_h_ams_entity) {
        uint8_t p[128];
        if (len > sizeof(p)) {
            len = sizeof(p);
        }
        if (ble_hs_mbuf_to_flat(om, p, len, NULL) == 0 &&
            aos_ams_entity_update(p, len, &s_ams) && s_ams.elapsed_nuevo) {
            /* The position arrives inside the state and only when something
             * changes: the moment is noted and from here on the clock counts
             * it. */
            s_ams_pos_us = esp_timer_get_time();
        }
        return;
    }

    if (s_h_bateria && event->notify_rx.attr_handle == s_h_bateria) {
        uint8_t pct = 0;
        if (ble_hs_mbuf_to_flat(om, &pct, 1, NULL) == 0 && pct <= 100) {
            s_bateria = pct;
        }
        return;
    }

    if (event->notify_rx.attr_handle != s_h_data_source) {
        return;
    }

    /* It accumulates and assembly is attempted on each chunk.
     * aos_ancs_data_source() knows when it is complete because it counts the
     * attributes it asked for, so there is no need here for the timer with
     * which Espressif's example guesses the end. */
    if ((uint32_t)s_ds_len + len > DS_MAX) {
        ESP_LOGW(TAG, "the Data Source does not fit in %d bytes, discarding it", DS_MAX);
        s_ds_len = 0;
        return;
    }
    if (ble_hs_mbuf_to_flat(om, &s_ds[s_ds_len], len, NULL) != 0) {
        s_ds_len = 0;
        return;
    }
    s_ds_len = (uint16_t)(s_ds_len + len);

    aos_notif_t n;
    if (aos_ancs_data_source(s_ds, s_ds_len, s_en_vuelo.uid, s_en_vuelo.cat,
                             s_en_vuelo.flags, &n)) {
        bool paso = aos_notif_push(&n);
        ESP_LOGI(TAG, "notification #%u %s: %s / %s  [cat=%d flags=0x%02X "
                      "pos=%d neg=%d prev=%d sil=%d]",
                 (unsigned)n.uid, paso ? "accepted" : "dropped by the filter",
                 n.app, n.title, (int)n.category, s_en_vuelo.flags,
                 (int)n.can_positive, (int)n.can_negative,
                 (int)n.pre_existing, (int)n.silent);
        pedido_listo();
    }
}

/* -------------------------------------------------------------------------- */
/* GAP events                                                                  */
/* -------------------------------------------------------------------------- */

static void limpiar_conexion(void)
{
    s_conn      = SIN_CONN;
    s_cola_w    = 0;
    s_cola_r    = 0;
    s_pidiendo  = false;
    s_conectado = false;
    s_cifrado   = false;
    s_ds_len    = 0;
    s_h_notification_source = 0;
    s_h_data_source = 0;
    s_h_control_point = 0;
    s_h_svc_end = 0;
    s_h_bateria = 0;
    s_bateria   = -1;
    s_h_ams_entity = 0;
    s_h_ams_cccd   = 0;
    s_h_ams_remote = 0;
    s_h_ams_end    = 0;
    s_ams_listo    = false;
    memset(&s_ams, 0, sizeof(s_ams));
    s_hora_lista = false;
    s_svc_start = 0;
    s_chr_count = 0;
    s_peer[0]   = '\0';
    aos_notif_reset_pending();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "conexion fallida (%d)", event->connect.status);
            advertise();
            return 0;
        }
        s_conn      = event->connect.conn_handle;
        s_conectado = true;
        ESP_LOGI(TAG, "phone connected");
        /* We ask for encryption ourselves. Without an encrypted and
         * authenticated connection the iPhone does not expose ANCS: its three
         * characteristics require authorisation. If there are stored keys
         * already this asks the user nothing. */
        if (ble_gap_security_initiate(s_conn) != 0) {
            ESP_LOGW(TAG, "could not start encryption");
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "phone disconnected (reason %d)",
                 event->disconnect.reason);
        limpiar_conexion();
        advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGW(TAG, "advertising ended (reason %d), resuming",
                 event->adv_complete.reason);
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0) {
            ESP_LOGW(TAG, "encryption failed (%d)", event->enc_change.status);
            return 0;
        }
        s_cifrado     = true;
        s_pair_wanted = false;
        s_pair_code   = 0;
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "link encrypted (authenticated=%d, bonded=%d)",
                     desc.sec_state.authenticated, desc.sec_state.bonded);
        }
        /* Only now does looking for ANCS make sense. */
        ble_gattc_disc_svc_by_uuid(event->enc_change.conn_handle,
                                   &UUID_ANCS.u, on_svc, NULL);
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            /* Numeric comparison: the phone and the watch show the same number
             * and each one confirms. Here it is only stored; who answers is
             * the user, tapping on the Settings screen, and that arrives
             * through aos_ble_pair_confirm(). */
            s_pair_code   = event->passkey.params.numcmp;
            s_pair_wanted = true;
            ESP_LOGI(TAG, "pairing, code %06u", (unsigned)s_pair_code);
        } else {
            /* With io_cap DISPLAY_YESNO nothing else should arrive. If it
             * does, the phone does not support secure connections and
             * negotiated legacy pairing: better to cut than to accept
             * blindly. */
            ESP_LOGW(TAG, "the phone asked for method %d, we do not do it",
                     event->passkey.params.action);
            ble_gap_terminate(event->passkey.conn_handle,
                              BLE_ERR_AUTH_FAIL);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The phone wants to pair again while we already have its keys: this
         * happens on restoring it or on deleting the device from the phone.
         * The old ones are thrown away and it is accepted, which is what the
         * user expects when they do it on purpose. */
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_RX:
        notify_rx(event);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU = %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* -------------------------------------------------------------------------- */
/* Startup and shutdown                                                        */
/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * TEMPORARY DIAGNOSTIC: listening to the air
 *
 * The host says it is advertising and nobody sees it. That leaves two causes
 * which from outside look identical: the radio is not transmitting, or it is
 * transmitting something nobody recognises. Listening separates them: if the
 * BLE devices around show up here, the radio works and the problem is what we
 * send; if none show up, the problem is the radio.
 *
 * Removed as soon as it answers the question. Needs BT_NIMBLE_ROLE_OBSERVER=y.
 * -------------------------------------------------------------------------- */
#if AOS_BLE_DIAG_SCAN

static int diag_disc(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields f;
        char nombre[32] = "";
        if (ble_hs_adv_parse_fields(&f, event->disc.data,
                                    event->disc.length_data) == 0 && f.name) {
            int n = f.name_len < (int)sizeof(nombre) - 1 ? f.name_len
                                                         : (int)sizeof(nombre) - 1;
            memcpy(nombre, f.name, n);
            nombre[n] = '\0';
        }
        ESP_LOGW(TAG, "[OIGO] %02X:%02X:%02X:%02X:%02X:%02X  rssi=%d  %s",
                 event->disc.addr.val[5], event->disc.addr.val[4],
                 event->disc.addr.val[3], event->disc.addr.val[2],
                 event->disc.addr.val[1], event->disc.addr.val[0],
                 event->disc.rssi, nombre);
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGW(TAG, "[LISTEN] end of the scan");
    }
    return 0;
}

static void diag_scan(void)
{
    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof(p));
    p.passive = 0;              /* active: asks for the scan response as well */
    p.filter_duplicates = 1;
    int rc = ble_gap_disc(s_addr_type, 12000, &p, diag_disc, NULL);
    ESP_LOGW(TAG, "[LISTEN] 12 s scan started, rc=%d", rc);
}

#else
static void diag_scan(void) { }
#endif

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "the host restarted (%d)", reason);
    limpiar_conexion();
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &s_addr_type) != 0) {
        ESP_LOGE(TAG, "there is no address of our own");
        return;
    }
    advertise();
    diag_scan();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();              /* only returns with nimble_port_stop() */
    nimble_port_freertos_deinit();
}

bool aos_ble_start(void)
{
    if (s_running) {
        return true;
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Numeric comparison: there is a screen to show the number and touch to
     * say yes, which is the most secure combination this board allows and also
     * the only one where the user has to type nothing. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO;
    ble_hs_cfg.sm_sc     = 1;       /* secure connections (LE Secure) */
    ble_hs_cfg.sm_mitm   = 1;       /* man-in-the-middle protection */
    ble_hs_cfg.sm_bonding = 1;      /* store the keys */
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_device_name_set(NOMBRE);

    /* A large MTU makes a notification's text arrive in two or three chunks
     * instead of fifteen. The reassembly copes with either, but each chunk is
     * one connection interval of waiting. */
    ble_att_set_preferred_mtu(256);

    ble_store_config_init();        /* keys in NVS */

    limpiar_conexion();
    s_running = true;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "BLE stack up");
    return true;
}

void aos_ble_stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;

    int rc = nimble_port_stop();
    if (rc == 0) {
        esp_err_t e = nimble_port_deinit();
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "nimble_port_deinit: %s", esp_err_to_name(e));
        }
    } else {
        ESP_LOGW(TAG, "nimble_port_stop: %d", rc);
    }
    limpiar_conexion();
    s_pair_wanted = false;
    s_pair_code   = 0;
    ESP_LOGI(TAG, "BLE stack down");
}

/* -------------------------------------------------------------------------- */
/* What the HAL sees                                                           */
/* -------------------------------------------------------------------------- */

bool aos_ble_running(void)
{
    return s_running;
}

/* Safety net. Advertising may have stopped on its own -a connection attempt
 * that came to nothing leaves it off- and without this the watch stays
 * invisible forever with nothing to say so. Called by the heartbeat. */
void aos_ble_tick(void)
{
    if (!s_running) {
        return;
    }

    /* The time the phone sent, applied from the main task. Only if it differs
     * from the one the watch has: writing the RTC and NVS every three seconds
     * to correct zero seconds is wasting flash for nothing. */
    if (s_hora_lista) {
        s_hora_lista = false;
        struct tm copia = s_hora;
        time_t del_telefono = mktime(&copia);
        time_t propia = time(NULL);
        double dif = difftime(del_telefono, propia);
        if (dif > 2.0 || dif < -2.0) {
            aos_hal_time_set(&s_hora);
            ESP_LOGI(TAG, "clock set by the phone "
                          "(%04d-%02d-%02d %02d:%02d:%02d, it was %+d s out)",
                     s_hora.tm_year + 1900, s_hora.tm_mon + 1, s_hora.tm_mday,
                     s_hora.tm_hour, s_hora.tm_min, s_hora.tm_sec, (int)-dif);
        } else {
            /* Also when NO correction is needed: otherwise the only proof the
             * reading arrived is that some day the clock runs slow. */
            ESP_LOGI(TAG, "the phone says the same time (%+d s)", (int)-dif);
        }
    }

    /* A request that does not answer must not block the queue forever: the
     * last fragment may have been lost, or the notification may have
     * disappeared from the phone between the notice and the request. */
    if (s_pidiendo && esp_timer_get_time() - s_pidiendo_us > PEDIDO_TOPE_US) {
        ESP_LOGW(TAG, "an attribute request went unanswered, carrying on");
        s_pidiendo = false;
        s_ds_len   = 0;
    }
    pedir_siguiente();

    if (s_conectado || ble_gap_adv_active()) {
        return;
    }
    ESP_LOGW(TAG, "it was not advertising: resuming");
    advertise();
}

aos_bt_state_t aos_ble_state(void)
{
    if (!s_running) {
        return AOS_BT_OFF;
    }
    if (s_pair_wanted) {
        return AOS_BT_PAIRING;
    }
    /* CONNECTED means "encrypted and usable", not merely "there is a link":
     * without encryption the iPhone does not deliver ANCS, so there is nothing
     * to be done with that connection and telling the user it is connected
     * would be lying to them. */
    if (s_conectado && s_cifrado) {
        return AOS_BT_CONNECTED;
    }

    /* And ADVERTISING is asked of NimBLE, not assumed.
     *
     * This function used to return ADVERTISING by elimination -"the stack is
     * up and nobody is connected, therefore it must be advertising"-, and that
     * turned the indicator into an assertion of ours rather than a fact: the
     * heartbeat said "advertising" while we were debugging precisely why there
     * was nothing on the air. An indicator that cannot come out false is of no
     * use at all. */
    return ble_gap_adv_active() ? AOS_BT_ADVERTISING : AOS_BT_OFF;
}

const char *aos_ble_peer(void)
{
    if (aos_ble_state() != AOS_BT_CONNECTED) {
        return "";
    }
    return s_peer[0] ? s_peer : "iPhone";
}

/* --------------------------------------------------------------------------
 * What the HAL sees for music control
 * -------------------------------------------------------------------------- */

void aos_ble_media_enable(bool on)
{
    aos_hal_pref_set_i32("ms_on", on ? 1 : 0);
    if (on) {
        ams_start();
    } else {
        ams_stop();
    }
}

bool aos_ble_media_enabled(void)
{
    return media_pedido();
}

bool aos_ble_media_ready(void)
{
    return s_ams_listo;
}

bool aos_ble_media_info(aos_ams_state_t *out, uint32_t *pos_s)
{
    if (!s_ams_listo || !out) {
        return false;
    }
    *out = s_ams;
    if (pos_s) {
        uint32_t p = s_ams.elapsed_s;
        /* Counting only continues while it is playing. Paused, the position is
         * whatever the phone said and that is that. */
        if (s_ams.playing && s_ams_pos_us) {
            p += (uint32_t)((esp_timer_get_time() - s_ams_pos_us) / 1000000);
        }
        if (s_ams.duration_s && p > s_ams.duration_s) {
            p = s_ams.duration_s;
        }
        *pos_s = p;
    }
    return true;
}

const char *aos_ble_media_player(void)
{
    return s_ams_listo ? s_ams.player : "";
}

bool aos_ble_media_command(int aos_media_cmd)
{
    int c = aos_ams_comando(aos_media_cmd);
    if (!s_ams_listo || s_conn == SIN_CONN || !s_h_ams_remote || c < 0) {
        return false;
    }
    uint8_t cmd[4];
    int n = aos_ams_cmd_remote(cmd, sizeof(cmd), c);
    if (n <= 0) {
        return false;
    }
    return ble_gattc_write_flat(s_conn, s_h_ams_remote, cmd, (uint16_t)n,
                                NULL, NULL) == 0;
}

bool aos_ble_phone_battery(int *percent)
{
    int v = s_bateria;
    if (v < 0) {
        return false;
    }
    if (percent) {
        *percent = v;
    }
    return true;
}

bool aos_ble_bonded(void)
{
    if (!s_running) {
        /* Switched off, the keys cannot be asked about, but they are still in
         * NVS. What is remembered is whether there was ever a pairing, so that
         * Settings shows the forget button with bluetooth off. */
        int32_t v = 0;
        aos_hal_pref_get_i32("bt_bond", &v);
        return v != 0;
    }
    int count = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) != 0) {
        return false;
    }
    aos_hal_pref_set_i32("bt_bond", count > 0 ? 1 : 0);
    return count > 0;
}

void aos_ble_forget(void)
{
    aos_hal_pref_set_i32("bt_bond", 0);
    if (!s_running) {
        return;
    }
    if (s_conn != SIN_CONN) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_store_clear();
    limpiar_conexion();
    advertise();
    ESP_LOGI(TAG, "keys erased");
}

void aos_ble_pair_begin(void)
{
    /* There is nothing to start on this side: the one who pairs is the phone.
     * All that is needed is to be advertising -so it shows up in their list-
     * and to note that the pairing screen is open, which is what puts the bar
     * icon in the accent colour. */
    s_pair_code   = 0;
    s_pair_wanted = true;
    advertise();
}

uint32_t aos_ble_pair_code(void)
{
    return s_pair_wanted ? s_pair_code : 0;
}

void aos_ble_pair_confirm(bool accept)
{
    uint16_t conn = s_conn;
    uint32_t code = s_pair_code;

    s_pair_wanted = false;
    s_pair_code   = 0;

    if (conn == SIN_CONN || !code) {
        return;                     /* cancelled before anything arrived */
    }
    struct ble_sm_io io;
    memset(&io, 0, sizeof(io));
    io.action = BLE_SM_IOACT_NUMCMP;
    io.numcmp_accept = accept;
    int rc = ble_sm_inject_io(conn, &io);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_sm_inject_io: %d", rc);
    }
}

/* ANCS answers Control Point errors as ATT errors, with codes of its own in
 * the 0xA0-0xA3 range. Without looking at them, an action the phone rejects
 * looks the same as one that went well: the button depresses, the screen
 * closes, and on the other side nothing happens. */
static const char *ancs_error(int status)
{
    switch (status & 0xFF) {
    case 0xA0: return "el telefono no reconocio el comando";
    case 0xA1: return "comando mal formado";
    case 0xA2: return "parametro invalido (¿la notificacion ya no existe?)";
    case 0xA3: return "el telefono no pudo hacer la accion";
    default:   return "";
    }
}

static int on_accion(uint16_t conn, const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (error->status == 0) {
        ESP_LOGI(TAG, "the phone accepted the action");
    } else {
        ESP_LOGW(TAG, "the phone rejected the action: status=0x%X %s",
                 error->status, ancs_error(error->status));
        aos_notif_action_failed();
    }
    return 0;
}

bool aos_ble_notif_action(uint32_t uid, bool positive)
{
    if (s_conn == SIN_CONN || !s_h_control_point) {
        ESP_LOGW(TAG, "action with no connection or no Control Point");
        return false;
    }
    uint8_t cmd[8];
    int n = aos_ancs_cmd_accion(cmd, sizeof(cmd), uid, positive);
    if (n <= 0) {
        return false;
    }
    int rc = ble_gattc_write_flat(s_conn, s_h_control_point, cmd, (uint16_t)n,
                                  on_accion, NULL);
    ESP_LOGI(TAG, "%s action on #%u -> rc=%d",
             positive ? "positive" : "negative", (unsigned)uid, rc);
    return rc == 0;
}
/* --------------------------------------------------------------------------
 * The measurement script
 *
 * A 1 Hz timer walks the four states needed to settle the design, without
 * anybody having to touch the screen:
 *
 *   1. steady state with WiFi up
 *   2. steady state with WiFi down   (how much do the two radios tread on each
 *                                     other?)
 *   3. after switching NimBLE off    (does the switch give the memory back?)
 *   4. WiFi back up                  (so as not to leave the watch offline)
 *
 * Point 3 is the one that matters: if switching bluetooth off does not give
 * back what it took, the Settings switch is no use for the one thing it needs
 * to be of use for.
 * -------------------------------------------------------------------------- */

static int  s_paso;
static bool s_ble_arriba = true;

static void guion(void *arg)
{
    (void)arg;
    s_paso++;

    switch (s_paso) {
    case 10:
        aos_ble_measure_report("1_wifi_on");
        break;

    case 12:
        ESP_LOGW(TAG, "--- switching the wifi off ---");
        aos_hal_net_enable(false);
        break;
    case 17:
        aos_ble_measure_report("2_wifi_off");
        break;

    case 19:
        ESP_LOGW(TAG, "--- switching NimBLE off (stop + deinit) ---");
        if (s_ble_arriba) {
            aos_ble_stop();
            s_ble_arriba = false;
        }
        break;
    case 24:
        aos_ble_measure_report("3_ble_off");
        break;

    case 26:
        ESP_LOGW(TAG, "--- switching the wifi back on ---");
        aos_hal_net_enable(true);
        break;
    case 34:
        aos_ble_measure_report("4_wifi_on_sin_ble");
        break;

    /* The switch has to be able to come back: switching off and not being able
     * to switch on again without a restart would be a single-use switch. */
    case 36:
        ESP_LOGW(TAG, "--- switching NimBLE back on ---");
        if (!s_ble_arriba) {
            s_ble_arriba = aos_ble_start();
            ESP_LOGW(TAG, "aos_ble_start (2da vez) -> %d", (int)s_ble_arriba);
        }
        break;
    case 44:
        aos_ble_measure_report("5_ble_on_otra_vez");
        ESP_LOGW(TAG, "=== end of the measurement script ===");
        break;

    default:
        break;
    }
}

void aos_ble_measure_start(void)
{
    aos_ble_measure_report("antes_de_ble");
    if (!aos_ble_start()) {
        return;
    }
    aos_ble_measure_report("port_init");

    const esp_timer_create_args_t args = {
        .callback = guion,
        .name     = "ble_medicion",
    };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_periodic(t, 1000 * 1000);
    }
}
