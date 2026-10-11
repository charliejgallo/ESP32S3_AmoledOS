/*
 * AmoledOS - Bluetooth LE for apps: the scanner and a GATT client
 * (aos_hal_ble_* in aos_hal.h). Ported from P4OS, where it was written and
 * tested first; the API is the same so the app moves between the two.
 *
 * The phone is aos_ble.c's and stays so: this file only listens to the air
 * and opens one connection of its own, as a central, with a GAP callback of
 * its own. NimBLE delivers a connection's events to the callback that made
 * it, so none of these reach aos_ble.c's gap_event, which takes every
 * connection it sees for the phone. That needs a second place:
 * CONFIG_BT_NIMBLE_MAX_CONNECTIONS is 2 since this file.
 *
 * Two sides:
 *   - the caller (an app, from LVGL's thread) never talks to NimBLE: it
 *     queues a command and posts an event to NimBLE's queue, because some of
 *     these calls (starting a scan, a connection) are HCI commands that wait
 *     for the controller, and LVGL must not wait for the radio;
 *   - NimBLE's host task runs the commands, one GATT procedure at a time,
 *     and leaves what comes back in two rings (reports, GATT events) and a
 *     table (the attributes), all in PSRAM, under one mutex. The host task is
 *     an ordinary task (not an ISR, not DMA), so PSRAM is fine for what it
 *     writes; its stack is internal and small (5 KB), so nothing big lives on
 *     it.
 */
#include "aos_ble.h"
#include "aos_hal.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"

#include <string.h>

static const char *TAG = "ble_scan";

#define ADV_RING   1024
#define EV_RING    64
#define CMD_RING   24
#define SVC_MAX    40
#define CHR_MAX    200
#define DSC_MAX    240
#define ATTR_MAX   (SVC_MAX + CHR_MAX + DSC_MAX)
#define NO_CONN    0xFFFF
#define SCAN_ITVL  160          /* 100 ms in 0.625 ms units */

/* ---- shared with the caller, under s_mx ---- */

static SemaphoreHandle_t s_mx;
static aos_ble_adv_t *s_adv;            /* ADV_RING, PSRAM */
static uint32_t s_adv_w, s_adv_r, s_adv_lost;
static aos_ble_gatt_ev_t *s_ev;         /* EV_RING, PSRAM */
static uint32_t s_ev_w, s_ev_r;
static aos_ble_attr_t *s_attrs;         /* ATTR_MAX, PSRAM: the table handed out */
static int s_nattrs;

enum { CMD_SCAN, CMD_CONNECT, CMD_DISCONNECT, CMD_READ, CMD_WRITE, CMD_WRITE_NR, CMD_SUB };
typedef struct {
    uint8_t op;
    uint8_t arg;                        /* scan: active; connect: address type; sub: mode */
    uint16_t handle;
    uint16_t len;
    uint8_t addr[6];
    uint8_t data[512];                  /* a write's */
} cmd_t;
static cmd_t *s_cmd;                    /* CMD_RING, PSRAM */
static uint32_t s_cmd_w, s_cmd_r;

/* written by the host task, read by anyone */
static volatile bool s_scan_want, s_scan_active_mode, s_scanning;
static volatile int s_scan_duty = 30;
static volatile aos_ble_gatt_state_t s_state;
static volatile int s_reason;
static volatile uint16_t s_mtu;
static volatile int8_t s_rssi;
static volatile bool s_rssi_ok;

/* ---- the host task's own ---- */

static bool s_ready;                    /* the event and the callout exist */
static struct ble_npl_event s_kick;
static struct ble_npl_callout s_rssi_co;
static uint16_t s_conn = NO_CONN;
static bool s_busy;                     /* a GATT procedure or a connection is under way */
static bool s_connecting;

typedef struct { uint16_t start, end; ble_uuid_any_t uuid; } svc_t;
typedef struct { uint16_t def, val; uint8_t props; uint8_t svc; ble_uuid_any_t uuid; } chr_t;
typedef struct { uint16_t handle; uint16_t chr; ble_uuid_any_t uuid; } dsc_t;
static svc_t *s_svc;                    /* PSRAM, discovery's scratch */
static chr_t *s_chr;
static dsc_t *s_dsc;
static int s_nsvc, s_nchr, s_ndsc, s_disc_i;

static uint8_t *s_rd;                   /* a long read being put together, 512 */
static uint8_t *s_nb;                   /* a notification's copy, 512: not on NimBLE's 5 KB stack */
static bool s_scan_restart;             /* new parameters: cancel and start again */
static uint16_t s_rd_len, s_rd_handle;
static uint8_t s_sub_mode;
static uint16_t s_sub_handle;

static void kick(void);
static int gap_cb(struct ble_gap_event *event, void *arg);
static void disc_chrs_next(void);
static void disc_dscs_next(void);

static void lock(void) { xSemaphoreTake(s_mx, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_mx); }

static void *ps_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memset(p, 0, n);
    return p;
}

/* Everything once, the first time an app asks; ~170 KB of PSRAM, kept. */
static bool mem_ready(void)
{
    if (!s_mx) {
        s_mx = xSemaphoreCreateMutex();
        if (!s_mx) return false;
    }
    if (s_adv && s_ev && s_cmd && s_attrs && s_svc && s_chr && s_dsc && s_rd && s_nb) return true;
    lock();
    if (!s_adv) s_adv = ps_alloc(sizeof(aos_ble_adv_t) * ADV_RING);
    if (!s_ev) s_ev = ps_alloc(sizeof(aos_ble_gatt_ev_t) * EV_RING);
    if (!s_cmd) s_cmd = ps_alloc(sizeof(cmd_t) * CMD_RING);
    if (!s_attrs) s_attrs = ps_alloc(sizeof(aos_ble_attr_t) * ATTR_MAX);
    if (!s_svc) s_svc = ps_alloc(sizeof(svc_t) * SVC_MAX);
    if (!s_chr) s_chr = ps_alloc(sizeof(chr_t) * CHR_MAX);
    if (!s_dsc) s_dsc = ps_alloc(sizeof(dsc_t) * DSC_MAX);
    if (!s_rd) s_rd = ps_alloc(512);
    if (!s_nb) s_nb = ps_alloc(512);
    unlock();
    return s_adv && s_ev && s_cmd && s_attrs && s_svc && s_chr && s_dsc && s_rd && s_nb;
}

/* ---- the rings ---- */

static void ev_push(uint8_t type, uint16_t handle, int status, const uint8_t *data, size_t len)
{
    lock();
    aos_ble_gatt_ev_t *e = &s_ev[s_ev_w % EV_RING];
    e->t_ms = aos_hal_uptime_ms();
    e->type = type;
    e->handle = handle;
    e->status = (int16_t)status;
    if (len > sizeof e->data) len = sizeof e->data;
    e->len = (uint16_t)len;
    if (len) memcpy(e->data, data, len);
    s_ev_w++;
    if (s_ev_w - s_ev_r > EV_RING) s_ev_r = s_ev_w - EV_RING;
    unlock();
}

static bool cmd_push(const cmd_t *c)
{
    if (!mem_ready()) return false;
    lock();
    bool ok = s_cmd_w - s_cmd_r < CMD_RING;
    if (ok) s_cmd[s_cmd_w++ % CMD_RING] = *c;
    unlock();
    if (ok) kick();
    return ok;
}

static bool cmd_pop(cmd_t *c)
{
    lock();
    bool ok = s_cmd_r != s_cmd_w;
    if (ok) *c = s_cmd[s_cmd_r++ % CMD_RING];
    unlock();
    return ok;
}

/* ---- scanning ---- */

static int disc_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &event->disc;
        lock();
        if (s_adv_w - s_adv_r >= ADV_RING) {
            s_adv_r++;
            s_adv_lost++;
        }
        aos_ble_adv_t *a = &s_adv[s_adv_w++ % ADV_RING];
        a->t_ms = aos_hal_uptime_ms();
        for (int i = 0; i < 6; i++) a->addr[i] = d->addr.val[5 - i];
        a->addr_type = d->addr.type & 1;
        a->kind = d->event_type <= AOS_BLE_ADV_SCAN_RSP ? d->event_type : AOS_BLE_ADV_NONCONN_IND;
        a->rssi = d->rssi;
        a->len = d->length_data > 31 ? 31 : d->length_data;
        memcpy(a->data, d->data, a->len);
        unlock();
    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        s_scanning = false;
        ESP_LOGI(TAG, "scan ended (%d)", event->disc_complete.reason);
        kick();                 /* back on if it is still wanted */
    }
    return 0;
}

static void scan_apply(void)
{
    if (!ble_hs_synced()) return;
    bool want = s_scan_want && !s_connecting;
    if (!want) {
        if (ble_gap_disc_active()) ble_gap_disc_cancel();
        s_scanning = false;
        return;
    }
    if (ble_gap_disc_active()) {
        if (!s_scan_restart) return;
        ble_gap_disc_cancel();
    }
    s_scan_restart = false;
    struct ble_gap_disc_params p = { 0 };
    int duty = s_scan_duty < 5 ? 5 : s_scan_duty > 100 ? 100 : s_scan_duty;
    p.itvl = SCAN_ITVL;
    p.window = (uint16_t)(SCAN_ITVL * duty / 100);
    if (p.window < 4) p.window = 4;
    p.passive = !s_scan_active_mode;
    p.filter_duplicates = 0;            /* every packet: the RSSI over time is the point */
    int rc = ble_gap_disc(aos_ble_own_addr_type(), BLE_HS_FOREVER, &p, disc_cb, NULL);
    s_scanning = rc == 0;
    if (rc) ESP_LOGW(TAG, "ble_gap_disc: %d", rc);
    else ESP_LOGI(TAG, "scanning (%s, %d %%)", p.passive ? "passive" : "active", duty);
}

/* ---- the GATT client: discovery ---- */

static void uuid_out(const ble_uuid_any_t *u, aos_ble_attr_t *a)
{
    memset(a->uuid, 0, sizeof a->uuid);
    if (u->u.type == BLE_UUID_TYPE_16) {
        a->uuid_len = 2;
        a->uuid[0] = u->u16.value & 0xFF;
        a->uuid[1] = u->u16.value >> 8;
    } else if (u->u.type == BLE_UUID_TYPE_32) {
        a->uuid_len = 4;
        for (int i = 0; i < 4; i++) a->uuid[i] = (u->u32.value >> (8 * i)) & 0xFF;
    } else {
        a->uuid_len = 16;
        memcpy(a->uuid, u->u128.value, 16);
    }
}

/* The flat table: a service, its characteristics, each one's descriptors. */
static void table_build(void)
{
    lock();
    int n = 0;
    for (int s = 0; s < s_nsvc && n < ATTR_MAX; s++) {
        aos_ble_attr_t *a = &s_attrs[n++];
        memset(a, 0, sizeof *a);
        a->kind = AOS_BLE_ATTR_SERVICE;
        a->handle = s_svc[s].start;
        a->end = s_svc[s].end;
        uuid_out(&s_svc[s].uuid, a);
        for (int c = 0; c < s_nchr && n < ATTR_MAX; c++) {
            if (s_chr[c].svc != s) continue;
            a = &s_attrs[n++];
            memset(a, 0, sizeof *a);
            a->kind = AOS_BLE_ATTR_CHAR;
            a->props = s_chr[c].props;
            a->handle = s_chr[c].val;
            a->end = s_chr[c].def;
            uuid_out(&s_chr[c].uuid, a);
            for (int d = 0; d < s_ndsc && n < ATTR_MAX; d++) {
                if (s_dsc[d].chr != c) continue;
                a = &s_attrs[n++];
                memset(a, 0, sizeof *a);
                a->kind = AOS_BLE_ATTR_DESC;
                a->handle = s_dsc[d].handle;
                uuid_out(&s_dsc[d].uuid, a);
            }
        }
    }
    s_nattrs = n;
    unlock();
}

static void disc_done(void)
{
    if (s_conn == NO_CONN || s_state != AOS_BLE_GATT_DISCOVERING) return;
    table_build();
    s_busy = false;
    s_state = AOS_BLE_GATT_READY;
    ESP_LOGI(TAG, "discovered %d services, %d characteristics, %d descriptors", s_nsvc, s_nchr, s_ndsc);
    kick();
}

static void fail(int reason)
{
    s_reason = reason;
    s_state = AOS_BLE_GATT_FAILED;
    if (s_conn != NO_CONN) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn;
    (void)chr_val;
    int ci = (int)(intptr_t)arg;
    if (err->status == 0) {
        if (s_ndsc < DSC_MAX && dsc) {
            s_dsc[s_ndsc].handle = dsc->handle;
            s_dsc[s_ndsc].chr = (uint16_t)ci;
            s_dsc[s_ndsc].uuid = dsc->uuid;
            s_ndsc++;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE) {
        fail(err->status);
        return 0;
    }
    /* done, or this characteristic had none: on to the next one */
    s_disc_i = ci + 1;
    disc_dscs_next();
    return 0;
}

/* Where a characteristic ends: before the next one's declaration, or at its
 * service's end. */
static uint16_t chr_end(int c)
{
    uint16_t end = s_svc[s_chr[c].svc].end;
    for (int k = 0; k < s_nchr; k++)
        if (s_chr[k].svc == s_chr[c].svc && s_chr[k].def > s_chr[c].def && s_chr[k].def - 1 < end) end = s_chr[k].def - 1;
    return end;
}

static void disc_dscs_next(void)
{
    if (s_conn == NO_CONN || s_state != AOS_BLE_GATT_DISCOVERING) return;
    while (s_disc_i < s_nchr) {
        int c = s_disc_i;
        uint16_t end = chr_end(c);
        if (end > s_chr[c].val) {
            int rc = ble_gattc_disc_all_dscs(s_conn, s_chr[c].val, end, on_dsc, (void *)(intptr_t)c);
            if (rc == 0) return;
            ESP_LOGW(TAG, "disc_all_dscs: %d", rc);
        }
        s_disc_i++;
    }
    disc_done();
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn;
    int si = (int)(intptr_t)arg;
    if (err->status == 0) {
        if (s_nchr < CHR_MAX && chr) {
            chr_t *c = &s_chr[s_nchr++];
            c->def = chr->def_handle;
            c->val = chr->val_handle;
            c->props = chr->properties;
            c->svc = (uint8_t)si;
            c->uuid = chr->uuid;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE) {
        fail(err->status);
        return 0;
    }
    s_disc_i = si + 1;
    disc_chrs_next();
    return 0;
}

static void disc_chrs_next(void)
{
    if (s_conn == NO_CONN || s_state != AOS_BLE_GATT_DISCOVERING) return;
    if (s_disc_i < s_nsvc) {
        int si = s_disc_i;
        int rc = ble_gattc_disc_all_chrs(s_conn, s_svc[si].start, s_svc[si].end, on_chr, (void *)(intptr_t)si);
        if (rc == 0) return;
        ESP_LOGW(TAG, "disc_all_chrs: %d", rc);
        s_disc_i = si + 1;
        disc_chrs_next();
        return;
    }
    s_disc_i = 0;
    disc_dscs_next();
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn;
    (void)arg;
    if (err->status == 0) {
        if (s_nsvc < SVC_MAX && svc) {
            s_svc[s_nsvc].start = svc->start_handle;
            s_svc[s_nsvc].end = svc->end_handle;
            s_svc[s_nsvc].uuid = svc->uuid;
            s_nsvc++;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE) {
        ESP_LOGW(TAG, "service discovery: %d", err->status);
        fail(err->status);
        return 0;
    }
    s_disc_i = 0;
    disc_chrs_next();
    return 0;
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    (void)arg;
    if (err->status == 0) s_mtu = mtu;
    /* with or without a bigger MTU, the services */
    int rc = ble_gattc_disc_all_svcs(conn, on_svc, NULL);
    if (rc) fail(rc);
    return 0;
}

/* ---- the GATT client: reads, writes, subscriptions ---- */

static int on_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)arg;
    if (err->status == 0 && attr) {
        uint16_t n = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset + n > 512) n = attr->offset < 512 ? 512 - attr->offset : 0;
        if (n) os_mbuf_copydata(attr->om, 0, n, s_rd + attr->offset);
        if (attr->offset + n > s_rd_len) s_rd_len = attr->offset + n;
        return 0;
    }
    int st = err->status == BLE_HS_EDONE ? 0 : err->status;
    ev_push(AOS_BLE_EV_READ, s_rd_handle, st, s_rd, st ? 0 : s_rd_len);
    s_busy = false;
    kick();
    return 0;
}

static int on_write(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)attr;
    uint16_t h = (uint16_t)(uintptr_t)arg;
    ev_push(AOS_BLE_EV_WRITE, h, err->status, NULL, 0);
    s_busy = false;
    kick();
    return 0;
}

static int on_sub(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    ev_push(AOS_BLE_EV_SUBSCRIBE, s_sub_handle, err->status, &s_sub_mode, 1);
    s_busy = false;
    kick();
    return 0;
}

/* The CCCD of a characteristic: the 0x2902 among its descriptors, or the
 * handle after its value when discovery left it out. */
static uint16_t cccd_of(uint16_t val)
{
    for (int c = 0; c < s_nchr; c++) {
        if (s_chr[c].val != val) continue;
        for (int d = 0; d < s_ndsc; d++)
            if (s_dsc[d].chr == c && s_dsc[d].uuid.u.type == BLE_UUID_TYPE_16 && s_dsc[d].uuid.u16.value == 0x2902)
                return s_dsc[d].handle;
    }
    return val + 1;
}

/* ---- connections ---- */

static void rssi_tick(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_conn == NO_CONN) return;
    /* static: GCC 14 takes the HCI call for keeping the pointer
     * (-Werror=dangling-pointer) */
    static int8_t r;
    if (ble_gap_conn_rssi(s_conn, &r) == 0) {
        s_rssi = r;
        s_rssi_ok = true;
    }
    ble_npl_callout_reset(&s_rssi_co, ble_npl_time_ms_to_ticks32(1000));
}

static void gone(void)
{
    s_conn = NO_CONN;
    s_busy = false;
    s_connecting = false;
    s_rssi_ok = false;
    if (s_ready) ble_npl_callout_stop(&s_rssi_co);
}

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!s_connecting) {
            /* cancelled while it was being made: not ours any more */
            if (event->connect.status == 0) ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        s_connecting = false;
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "connection failed (%d)", event->connect.status);
            s_reason = event->connect.status;
            s_state = AOS_BLE_GATT_FAILED;
            gone();
            kick();
            return 0;
        }
        s_conn = event->connect.conn_handle;
        s_state = AOS_BLE_GATT_DISCOVERING;
        s_mtu = 23;
        ESP_LOGI(TAG, "connected (%u)", (unsigned)s_conn);
        ble_npl_callout_reset(&s_rssi_co, ble_npl_time_ms_to_ticks32(300));
        if (ble_gattc_exchange_mtu(s_conn, on_mtu, NULL) != 0) {
            int rc = ble_gattc_disc_all_svcs(s_conn, on_svc, NULL);
            if (rc) fail(rc);
        }
        kick();                 /* the scan, back on */
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (%d)", event->disconnect.reason);
        /* a previous connection going late, after another was asked for */
        if (event->disconnect.conn.conn_handle != s_conn) return 0;
        if (s_state != AOS_BLE_GATT_IDLE && s_state != AOS_BLE_GATT_FAILED) {
            s_reason = event->disconnect.reason;
            s_state = AOS_BLE_GATT_FAILED;
        }
        gone();
        kick();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        if (event->notify_rx.conn_handle != s_conn) return 0;
        uint16_t n = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (n > 512) n = 512;
        os_mbuf_copydata(event->notify_rx.om, 0, n, s_nb);
        ev_push(event->notify_rx.indication ? AOS_BLE_EV_INDICATE : AOS_BLE_EV_NOTIFY,
                event->notify_rx.attr_handle, 0, s_nb, n);
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        s_mtu = event->mtu.value;
        return 0;
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: {
        const struct ble_gap_upd_params *p = event->conn_update_req.peer_params;
        /* accepted; said, because a peer that asks in the middle of a
         * procedure is what hung discovery (connect_to) */
        ESP_LOGI(TAG, "peer asks itvl %u-%u latency %u timeout %u", p->itvl_min, p->itvl_max, p->latency,
                 p->supervision_timeout);
        return 0;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc d;
        if (ble_gap_conn_find(event->conn_update.conn_handle, &d) == 0)
            ESP_LOGI(TAG, "connection updated (%d): itvl %u latency %u timeout %u", event->conn_update.status,
                     d.conn_itvl, d.conn_latency, d.supervision_timeout);
        return 0;
    }
    default:
        return 0;
    }
}

static void connect_to(const uint8_t addr[6], uint8_t type)
{
    if (s_connecting) ble_gap_conn_cancel();
    if (s_conn != NO_CONN) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    gone();
    s_nsvc = s_nchr = s_ndsc = 0;
    lock();
    s_nattrs = 0;
    unlock();
    s_reason = 0;
    /* NimBLE will not start a connection while discovering */
    if (ble_gap_disc_active()) ble_gap_disc_cancel();
    s_scanning = false;
    ble_addr_t peer = { .type = type ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC };
    for (int i = 0; i < 6; i++) peer.val[i] = addr[5 - i];
    s_state = AOS_BLE_GATT_CONNECTING;
    /* 15-30 ms from the start, what small peripherals ask for anyway. With
     * NimBLE's default (30-50 ms) a stock Xiaomi thermometer asked for these
     * a few seconds in, and the ATT request in flight at that moment never
     * got its answer: discovery hung until the 30 s ATT timeout, 7 times in
     * 13 on the watch, whether the update was accepted or refused. Asked
     * for from the start, it has nothing to ask: 14 in 14, in 7-11 s, with
     * the scan at 100 % or without it (2026-10-10). */
    static const struct ble_gap_conn_params cp = {
        .scan_itvl = 16, .scan_window = 16,
        .itvl_min = 12, .itvl_max = 24,         /* 15-30 ms */
        .latency = 0, .supervision_timeout = 200,   /* 2 s */
    };
    int rc = ble_gap_connect(aos_ble_own_addr_type(), &peer, 8000, &cp, gap_cb, NULL);
    if (rc) {
        ESP_LOGW(TAG, "ble_gap_connect: %d", rc);
        s_reason = rc;
        s_state = AOS_BLE_GATT_FAILED;
        return;
    }
    s_connecting = true;
    s_busy = true;              /* until it is connected and discovered */
}

/* ---- the host task's loop: the commands, in order ---- */

static void run(struct ble_npl_event *ev)
{
    (void)ev;
    /* static and in PSRAM: 524 bytes would weigh on NimBLE's 5 KB stack
     * (internal), and only its task runs this */
    AOS_BSS_PSRAM static cmd_t c;
    /* kicked at every sync, also before any app asked for anything: then
     * there is nothing allocated, no mutex, and nothing to do */
    if (!s_mx || !s_cmd) return;
    for (;;) {
        /* a connection under way or a GATT procedure holds the GATT
         * commands back; a scan or a disconnection never waits */
        bool gatt_free = !s_busy && s_state != AOS_BLE_GATT_DISCOVERING;
        lock();
        bool any = s_cmd_r != s_cmd_w;
        bool next_is_gatt = any && s_cmd[s_cmd_r % CMD_RING].op >= CMD_READ;
        unlock();
        if (!any || (next_is_gatt && !gatt_free)) break;
        if (!cmd_pop(&c)) break;
        switch (c.op) {
        case CMD_SCAN:
            break;              /* the wanted state is already set */
        case CMD_CONNECT:
            connect_to(c.addr, c.arg);
            break;
        case CMD_DISCONNECT:
            if (s_connecting) ble_gap_conn_cancel();
            if (s_conn != NO_CONN) ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
            gone();
            s_state = AOS_BLE_GATT_IDLE;
            break;
        case CMD_READ:
            if (s_conn == NO_CONN) { ev_push(AOS_BLE_EV_READ, c.handle, BLE_HS_ENOTCONN, NULL, 0); break; }
            s_rd_len = 0;
            s_rd_handle = c.handle;
            if (ble_gattc_read_long(s_conn, c.handle, 0, on_read, NULL) == 0) s_busy = true;
            else ev_push(AOS_BLE_EV_READ, c.handle, BLE_HS_EBUSY, NULL, 0);
            break;
        case CMD_WRITE: {
            if (s_conn == NO_CONN) { ev_push(AOS_BLE_EV_WRITE, c.handle, BLE_HS_ENOTCONN, NULL, 0); break; }
            int rc;
            if (c.len + 3 <= s_mtu) {
                rc = ble_gattc_write_flat(s_conn, c.handle, c.data, c.len, on_write, (void *)(uintptr_t)c.handle);
            } else {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(c.data, c.len);
                rc = om ? ble_gattc_write_long(s_conn, c.handle, 0, om, on_write, (void *)(uintptr_t)c.handle) : BLE_HS_ENOMEM;
            }
            if (rc == 0) s_busy = true;
            else ev_push(AOS_BLE_EV_WRITE, c.handle, rc, NULL, 0);
            break;
        }
        case CMD_WRITE_NR: {
            if (s_conn == NO_CONN) break;
            uint16_t n = c.len + 3 <= s_mtu ? c.len : s_mtu - 3;
            int rc = ble_gattc_write_no_rsp_flat(s_conn, c.handle, c.data, n);
            ev_push(AOS_BLE_EV_WRITE, c.handle, rc, NULL, 0);
            break;
        }
        case CMD_SUB: {
            if (s_conn == NO_CONN) { ev_push(AOS_BLE_EV_SUBSCRIBE, c.handle, BLE_HS_ENOTCONN, NULL, 0); break; }
            uint8_t v[2] = { c.arg == 1 ? 1 : 0, c.arg == 2 ? 1 : 0 };
            s_sub_mode = c.arg;
            s_sub_handle = c.handle;
            int rc = ble_gattc_write_flat(s_conn, cccd_of(c.handle), v, 2, on_sub, NULL);
            if (rc == 0) s_busy = true;
            else ev_push(AOS_BLE_EV_SUBSCRIBE, c.handle, rc, &c.arg, 1);
            break;
        }
        }
    }
    scan_apply();
}

static void kick(void)
{
    if (s_ready) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_kick);
}

/* ---- from aos_ble.c ---- */

void aos_ble_scan_init(void)
{
    if (s_ready) return;
    ble_npl_event_init(&s_kick, run, NULL);
    ble_npl_callout_init(&s_rssi_co, nimble_port_get_dflt_eventq(), rssi_tick, NULL);
    s_ready = true;
}

void aos_ble_scan_deinit(void)
{
    if (!s_ready) return;
    ble_npl_callout_stop(&s_rssi_co);
    ble_npl_callout_deinit(&s_rssi_co);
    ble_npl_event_deinit(&s_kick);
    s_ready = false;
    gone();
    s_scanning = false;
    if (s_state != AOS_BLE_GATT_IDLE) {
        s_state = AOS_BLE_GATT_FAILED;
        s_reason = BLE_HS_ENOTSYNCED;
    }
}

/* The host (re)synced: what was wanted comes back. */
void aos_ble_scan_synced(void)
{
    kick();
}

/* The host reset: every connection is gone, ours too. */
void aos_ble_scan_reset(void)
{
    s_scanning = false;
    if (s_conn != NO_CONN || s_connecting) {
        s_state = AOS_BLE_GATT_FAILED;
        s_reason = BLE_HS_ENOTSYNCED;
    }
    gone();
}

/* ---- the HAL ---- */

bool aos_hal_ble_scan_start(bool active, int duty_pct)
{
    if (!aos_ble_running() || !mem_ready()) return false;
    /* a fresh start begins with an empty ring; asking again while it is
     * wanted (an app retrying while a connection pauses the scan) keeps it */
    if (!s_scan_want) {
        lock();
        s_adv_r = s_adv_w;
        s_adv_lost = 0;
        unlock();
    }
    /* new parameters: the scan is cancelled and comes back with them */
    if (s_scan_want && (s_scan_active_mode != active || s_scan_duty != duty_pct)) s_scan_restart = true;
    s_scan_active_mode = active;
    s_scan_duty = duty_pct;
    s_scan_want = true;
    cmd_t c = { .op = CMD_SCAN };
    return cmd_push(&c);
}

void aos_hal_ble_scan_stop(void)
{
    s_scan_want = false;
    cmd_t c = { .op = CMD_SCAN };
    cmd_push(&c);
}

bool aos_hal_ble_scanning(void)
{
    return aos_ble_running() && s_scanning;
}

int aos_hal_ble_scan_read(aos_ble_adv_t *out, int max)
{
    if (!s_mx || !s_adv || !out || max <= 0) return 0;
    lock();
    int n = 0;
    while (n < max && s_adv_r != s_adv_w) out[n++] = s_adv[s_adv_r++ % ADV_RING];
    unlock();
    return n;
}

uint32_t aos_hal_ble_scan_lost(void)
{
    return s_adv_lost;
}

bool aos_hal_ble_gatt_connect(const uint8_t addr[6], uint8_t addr_type)
{
    if (!aos_ble_running() || !addr) return false;
    cmd_t c = { .op = CMD_CONNECT, .arg = addr_type };
    memcpy(c.addr, addr, 6);
    /* the state changes now, so whoever asks right after does not read the
     * previous connection's */
    s_state = AOS_BLE_GATT_CONNECTING;
    s_reason = 0;
    if (!cmd_push(&c)) {
        s_state = AOS_BLE_GATT_IDLE;
        return false;
    }
    return true;
}

void aos_hal_ble_gatt_disconnect(void)
{
    if (s_state == AOS_BLE_GATT_IDLE) return;
    cmd_t c = { .op = CMD_DISCONNECT };
    if (!cmd_push(&c)) s_state = AOS_BLE_GATT_IDLE;
}

aos_ble_gatt_state_t aos_hal_ble_gatt_state(int *reason)
{
    if (reason) *reason = s_reason;
    return s_state;
}

uint16_t aos_hal_ble_gatt_mtu(void)
{
    return s_state == AOS_BLE_GATT_READY || s_state == AOS_BLE_GATT_DISCOVERING ? s_mtu : 0;
}

bool aos_hal_ble_gatt_rssi(int8_t *rssi)
{
    if (!s_rssi_ok || s_state != AOS_BLE_GATT_READY) return false;
    if (rssi) *rssi = s_rssi;
    return true;
}

int aos_hal_ble_gatt_attrs(aos_ble_attr_t *out, int max)
{
    if (!s_mx || !s_attrs || s_state != AOS_BLE_GATT_READY) return 0;
    lock();
    int n = s_nattrs < max ? s_nattrs : max;
    if (out && n > 0) memcpy(out, s_attrs, sizeof *out * n);
    int total = s_nattrs;
    unlock();
    return out ? n : total;
}

bool aos_hal_ble_gatt_read(uint16_t handle)
{
    if (s_state != AOS_BLE_GATT_READY) return false;
    cmd_t c = { .op = CMD_READ, .handle = handle };
    return cmd_push(&c);
}

bool aos_hal_ble_gatt_write(uint16_t handle, const void *data, size_t len, bool response)
{
    if (s_state != AOS_BLE_GATT_READY || len > 512 || (len && !data)) return false;
    cmd_t c = { .op = response ? CMD_WRITE : CMD_WRITE_NR, .handle = handle, .len = (uint16_t)len };
    if (len) memcpy(c.data, data, len);
    return cmd_push(&c);
}

bool aos_hal_ble_gatt_subscribe(uint16_t value_handle, int mode)
{
    if (s_state != AOS_BLE_GATT_READY || mode < 0 || mode > 2) return false;
    cmd_t c = { .op = CMD_SUB, .handle = value_handle, .arg = (uint8_t)mode };
    return cmd_push(&c);
}

int aos_hal_ble_gatt_events(aos_ble_gatt_ev_t *out, int max)
{
    if (!s_mx || !s_ev || !out || max <= 0) return 0;
    lock();
    int n = 0;
    while (n < max && s_ev_r != s_ev_w) out[n++] = s_ev[s_ev_r++ % EV_RING];
    unlock();
    return n;
}
