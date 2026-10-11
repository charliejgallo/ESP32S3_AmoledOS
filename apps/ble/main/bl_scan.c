/*
 * BLE - the table of devices heard, and what is kept of it.
 *
 * The HAL's ring is drained from the app's timer (ble.c) a few times a
 * second; each report lands on its device's record: the RSSI (last,
 * smoothed, min, max, and the strongest of each second for two minutes),
 * the gap between packets, the raw packets, and when the bytes change, the
 * merged AD structures with what they say (class, sensor readings, beacon).
 *
 * Kept on the card, under <card>/ble/:
 *   nombres.txt          favourites and aliases ("AA:BB:..|*|Heladera"), which
 *                        can be edited from the portal's file browser too;
 *   claves.txt           the keys of the sensors that encrypt;
 *   sensores-<day>.csv   one line a minute per sensor, when asked.
 */
#include "bl.h"
#include "aos_text_safe.h"
#include "bl_crypt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
#include "esp_heap_caps.h"
#endif

#define RX_MAX 256

static void *ps_alloc(size_t n)
{
#if !defined(AOS_SIM) && !defined(AOS_SIM_BUILTIN)
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(n);
#else
    void *p = malloc(n);
#endif
    if (p) memset(p, 0, n);
    return p;
}

static uint32_t now_ms(void) { return (uint32_t)aos_hal_uptime_ms(); }

static const char *card_root(void)
{
    const char *r = aos_hal_path_sd_root();
    return r ? r : aos_hal_path_data();
}

/* -------------------------------------------------------------------------- */
/* Formatting                                                                  */
/* -------------------------------------------------------------------------- */

void bl_fmt_num(char *out, size_t n, float v, int dec)
{
    snprintf(out, n, "%.*f", dec, (double)v);
    for (char *p = out; *p; p++) if (*p == '.') *p = ',';
}

void bl_fmt_age(char *out, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    if (s < 2) snprintf(out, n, "%s", _("ahora"));
    else if (s < 60) snprintf(out, n, "%u s", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%u min", (unsigned)(s / 60));
    else snprintf(out, n, "%u h", (unsigned)(s / 3600));
}

void bl_fmt_addr(const uint8_t a[6], char *out, size_t n)
{
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

bool bl_parse_addr(const char *s, uint8_t a[6])
{
    unsigned v[6];
    if (!s || sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (v[i] > 255) return false;
        a[i] = (uint8_t)v[i];
    }
    return true;
}

/* -------------------------------------------------------------------------- */
/* Looks                                                                       */
/* -------------------------------------------------------------------------- */

const char *bl_class_glyph(bl_class_t c)
{
    switch (c) {
    case BL_CLS_PHONE:    return AOS_SYM_PHONE;
    case BL_CLS_COMPUTER: return AOS_SYM_MONITOR;
    case BL_CLS_TABLET:   return AOS_SYM_MONITOR;
    case BL_CLS_WATCH:    return AOS_SYM_CLOCK_OUTLINE;
    case BL_CLS_AUDIO:    return AOS_SYM_MUSIC;
    case BL_CLS_TRACKER:  return AOS_SYM_PIN;
    case BL_CLS_SENSOR:   return AOS_SYM_THERMOMETER;
    case BL_CLS_BEACON:   return AOS_SYM_RADAR;
    case BL_CLS_HID:      return AOS_SYM_KEYBOARD;
    case BL_CLS_GAMEPAD:  return AOS_SYM_GAMEPAD_VARIANT;
    case BL_CLS_TV:       return AOS_SYM_TELEVISION;
    case BL_CLS_HEALTH:   return AOS_SYM_PULSE;
    case BL_CLS_LIGHT:    return AOS_SYM_LIGHTBULB;
    case BL_CLS_FITNESS:  return AOS_SYM_SPEEDOMETER;
    case BL_CLS_DEVBOARD: return AOS_SYM_DEVELOPER_BOARD;
    default:              return AOS_SYM_BLUETOOTH;
    }
}

lv_color_t bl_class_color(bl_class_t c)
{
    switch (c) {
    case BL_CLS_PHONE:    return lv_color_hex(0x0A84FF);
    case BL_CLS_COMPUTER: return lv_color_hex(0x5E5CE6);
    case BL_CLS_TABLET:   return lv_color_hex(0x5E5CE6);
    case BL_CLS_WATCH:    return lv_color_hex(0xFF9F0A);
    case BL_CLS_AUDIO:    return lv_color_hex(0xBF5AF2);
    case BL_CLS_TRACKER:  return lv_color_hex(0xFF375F);
    case BL_CLS_SENSOR:   return lv_color_hex(0x30D158);
    case BL_CLS_BEACON:   return lv_color_hex(0x40C8E0);
    case BL_CLS_HID:      return lv_color_hex(0x64D2FF);
    case BL_CLS_GAMEPAD:  return lv_color_hex(0xFF6482);
    case BL_CLS_TV:       return lv_color_hex(0xAC8E68);
    case BL_CLS_HEALTH:   return lv_color_hex(0xFF453A);
    case BL_CLS_LIGHT:    return lv_color_hex(0xFFD60A);
    case BL_CLS_FITNESS:  return lv_color_hex(0x66D4CF);
    case BL_CLS_DEVBOARD: return lv_color_hex(0xE5484D);
    default:              return lv_color_hex(0x636366);
    }
}

/* path loss exponents: open air, a house, an office full of things */
static const float ENV_N[] = { 2.0f, 2.7f, 3.3f };

float bl_env_n(void)
{
    int e = BL.env < 0 || BL.env > 2 ? 1 : BL.env;
    return ENV_N[e];
}

/* -------------------------------------------------------------------------- */
/* The table                                                                   */
/* -------------------------------------------------------------------------- */

#define BL_HASH 4096                /* twice BL_DEV_MAX, a power of two */

bool bl_scan_init(void)
{
    if (!BL.dev) {
        BL.dev = ps_alloc(sizeof(bl_dev_t) * BL_DEV_START);
        BL.cap = BL.dev ? BL_DEV_START : 0;
    }
    if (!BL.rx) BL.rx = ps_alloc(sizeof(aos_ble_adv_t) * RX_MAX);
    if (!BL.hash) {
        BL.hash = ps_alloc(sizeof(int16_t) * BL_HASH);
        if (BL.hash) for (int i = 0; i < BL_HASH; i++) BL.hash[i] = -1;
    }
    if (!BL.sh) {
        BL.sh = ps_alloc(sizeof(bl_senhist_t) * BL_SH_MAX);
        if (BL.sh) for (int i = 0; i < BL_SH_MAX; i++) BL.sh[i].dev = -1;
    }
    return BL.dev && BL.rx && BL.hash && BL.sh;
}

void bl_scan_free(void)
{
    free(BL.dev);
    free(BL.rx);
    free(BL.hash);
    free(BL.sh);
    BL.dev = NULL;
    BL.rx = NULL;
    BL.hash = NULL;
    BL.sh = NULL;
    BL.ndev = BL.cap = 0;
}

void bl_scan_apply(void)
{
    /* only while the app is on screen (the user's choice for a watch: the
     * radio and the battery are shared with everything else) */
    bool want = !BL.paused && !BL.hidden;
    if (!want) {
        if (aos_hal_ble_scanning()) aos_hal_ble_scan_stop();
        return;
    }
    if (!aos_hal_bt_enabled()) {
        BL.bt_off = true;
        return;
    }
    BL.bt_off = !aos_hal_ble_scan_start(BL.active, BL.duty);
}

/* ---- the address index: every packet looks its device up, and at a fair
 * a list walk over two thousand records was the costly part ---- */

static uint32_t addr_hash(const uint8_t a[6])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ a[i]) * 16777619u;
    return h;
}

static void hash_put(int idx)
{
    uint32_t h = addr_hash(BL.dev[idx].addr) & (BL_HASH - 1);
    while (BL.hash[h] >= 0) h = (h + 1) & (BL_HASH - 1);
    BL.hash[h] = (int16_t)idx;
}

/* After a record changed its address or the table was compacted: linear
 * probing has no cheap removal, and this is rare. */
static void hash_rebuild(void)
{
    for (int i = 0; i < BL_HASH; i++) BL.hash[i] = -1;
    for (int i = 0; i < BL.ndev; i++) hash_put(i);
}

int bl_find(const uint8_t addr[6])
{
    if (!BL.hash) return -1;
    uint32_t h = addr_hash(addr) & (BL_HASH - 1);
    for (int n = 0; n < BL_HASH && BL.hash[h] >= 0; n++, h = (h + 1) & (BL_HASH - 1)) {
        int i = BL.hash[h];
        if (i < BL.ndev && !memcmp(BL.dev[i].addr, addr, 6)) return i;
    }
    return -1;
}

bool bl_alive(const bl_dev_t *d)
{
    return now_ms() - d->last_ms < BL_GONE_MS;
}

static void sh_release(bl_dev_t *d)
{
    if (d->sh >= 0 && d->sh < BL_SH_MAX && BL.sh[d->sh].dev == (int16_t)(d - BL.dev)) BL.sh[d->sh].dev = -1;
    d->sh = -1;
}

/* Room for one more: double the table while it may grow, else the record
 * not heard for longest that is neither a favourite nor open on the screen
 * makes room. Indexes stay what they were either way. */
static int dev_slot(bool *evicted)
{
    *evicted = false;
    if (BL.ndev < BL.cap) return BL.ndev++;
    if (BL.cap < BL_DEV_MAX) {
        int cap = BL.cap * 2 > BL_DEV_MAX ? BL_DEV_MAX : BL.cap * 2;
        bl_dev_t *n = ps_alloc(sizeof(bl_dev_t) * cap);
        if (n) {
            memcpy(n, BL.dev, sizeof(bl_dev_t) * BL.ndev);
            free(BL.dev);
            BL.dev = n;
            BL.cap = cap;
            return BL.ndev++;
        }
    }
    int old = -1;
    for (int k = 0; k < BL.ndev; k++) {
        if (BL.dev[k].fav || k == BL.sel) continue;
        if (old < 0 || (int32_t)(BL.dev[k].last_ms - BL.dev[old].last_ms) < 0) old = k;
    }
    if (old >= 0) {
        sh_release(&BL.dev[old]);
        BL.forgotten++;
        *evicted = true;
    }
    return old;
}

static bl_dev_t *dev_new(const aos_ble_adv_t *a)
{
    bool reused;
    int i = dev_slot(&reused);
    if (i < 0) return NULL;
    bl_dev_t *d = &BL.dev[i];
    memset(d, 0, sizeof *d);
    memcpy(d->addr, a->addr, 6);
    d->addr_type = a->addr_type;
    d->first_ms = a->t_ms;
    d->rssi_min = 127;
    d->rssi_max = -127;
    d->rssi_avg = a->rssi;
    for (int k = 0; k < BL_HIST; k++) d->hist[k] = BL_NO_RSSI;
    d->sh = -1;
    d->hist_sec = a->t_ms / 1000;
    d->key_state = -1;
    bl_ad_clear(&d->ad);
    if (reused) hash_rebuild();
    else hash_put(i);
    BL.gen++;
    return d;
}

/* ---- sensors' history ---- */

#define SH_NONE INT16_MIN

float bl_sen_at(const bl_dev_t *d, bool hum, uint32_t slot)
{
    if (!BL.sh || d->sh < 0) return NAN;
    const bl_senhist_t *h = &BL.sh[d->sh];
    if (slot > h->slot || h->slot - slot >= BL_SEN_HIST) return NAN;
    int16_t v = (hum ? h->h : h->t)[slot % BL_SEN_HIST];
    return v == SH_NONE ? NAN : v / 10.0f;
}

uint32_t bl_sen_last(const bl_dev_t *d)
{
    return BL.sh && d->sh >= 0 ? BL.sh[d->sh].slot : 0;
}

static int16_t tenths(float v)
{
    float r = v * 10.0f;
    if (r > 32000) r = 32000;
    if (r < -32000) r = -32000;
    return (int16_t)(r < 0 ? r - 0.5f : r + 0.5f);
}

static void sensor_keep(bl_dev_t *d, uint32_t t)
{
    if (!BL.sh || !(d->sen.mask & (BL_V_TEMP | BL_V_HUM))) return;
    uint32_t slot = t / 60000;
    int idx = (int)(d - BL.dev);
    if (d->sh < 0 || BL.sh[d->sh].dev != idx) {
        /* a free history, else the one read longest ago */
        int pick = -1;
        for (int k = 0; k < BL_SH_MAX; k++) {
            if (BL.sh[k].dev < 0) { pick = k; break; }
            if (pick < 0 || BL.sh[k].slot < BL.sh[pick].slot) pick = k;
        }
        bl_senhist_t *h = &BL.sh[pick];
        if (h->dev >= 0 && h->dev < BL.ndev) BL.dev[h->dev].sh = -1;
        for (int k = 0; k < BL_SEN_HIST; k++) h->t[k] = h->h[k] = SH_NONE;
        h->slot = slot;
        h->dev = (int16_t)idx;
        d->sh = (int16_t)pick;
    }
    bl_senhist_t *h = &BL.sh[d->sh];
    if (slot > h->slot) {
        uint32_t gap = slot - h->slot;
        if (gap > BL_SEN_HIST) gap = BL_SEN_HIST;
        for (uint32_t k = 1; k <= gap; k++) h->t[(h->slot + k) % BL_SEN_HIST] = h->h[(h->slot + k) % BL_SEN_HIST] = SH_NONE;
        h->slot = slot;
    }
    int i = slot % BL_SEN_HIST;
    if (d->sen.mask & BL_V_TEMP) h->t[i] = tenths(d->sen.temp);
    if (d->sen.mask & BL_V_HUM) h->h[i] = tenths(d->sen.hum);
}

static void hist_roll(int8_t *hist, uint32_t *last, uint32_t sec)
{
    if (sec <= *last) return;
    uint32_t gap = sec - *last;
    if (gap > BL_HIST) gap = BL_HIST;
    for (uint32_t k = 1; k <= gap; k++) hist[(*last + k) % BL_HIST] = BL_NO_RSSI;
    *last = sec;
}

static void air_roll(uint32_t sec)
{
    bl_air_t *A = &BL.air;
    if (!A->sec) A->sec = sec;
    if (sec <= A->sec) return;
    uint32_t gap = sec - A->sec;
    if (gap > BL_AIR_HIST) gap = BL_AIR_HIST;
    for (uint32_t k = 1; k <= gap; k++) {
        A->pkts[(A->sec + k) % BL_AIR_HIST] = 0;
        A->devs[(A->sec + k) % BL_AIR_HIST] = 0;
    }
    A->sec = sec;
}

/* What the bytes say, again: after a change in the advertisement or the
 * scan response. */
static void dev_decode(bl_dev_t *d, const uint8_t *data, int len, uint32_t t)
{
    bl_ad_merge(&d->ad, data, len);
    d->cls = bl_classify(&d->ad, &d->label);
    bl_sensor_t s;
    bool got = bl_sensor_decode(&d->ad, d->addr, &s);
    if (got && s.encrypted && d->has_key) {
        /* encrypted, and its key is known: what it says, decrypted; a wrong
         * key leaves the "encrypted" in place, and says so */
        bl_sensor_t plain;
        int r = bl_sensor_decode_key(&d->ad, d->addr, d->key, &plain);
        if (r != BL_KEY_NOT_ENCRYPTED) d->key_state = (int8_t)r;
        if (r == BL_KEY_OK) s = plain;
        else if (r == BL_KEY_NOT_ENCRYPTED) got = false;     /* this frame carried no reading */
    }
    /* a beacon's calibrated power alone is not a reading */
    if (got && ((s.mask & ~(uint32_t)BL_V_RSSI1M) || s.encrypted)) {
        /* encrypted with no readings after decrypted ones: keep the
         * readings (the key went wrong is told in the detail) */
        if (s.encrypted && d->has_sen && !d->sen.encrypted) goto beacon;
        /* some formats send their values a few at a time (BTHome objects,
         * Eddystone's frames): what did not come this time is kept */
        uint32_t had = d->has_sen ? d->sen.mask : 0;
        bl_sensor_t old = d->sen;
        d->sen = s;
        if (had & ~s.mask & BL_V_TEMP) { d->sen.temp = old.temp; d->sen.mask |= BL_V_TEMP; }
        if (had & ~s.mask & BL_V_HUM) { d->sen.hum = old.hum; d->sen.mask |= BL_V_HUM; }
        if (had & ~s.mask & BL_V_BATT) { d->sen.batt = old.batt; d->sen.mask |= BL_V_BATT; }
        if (had & ~s.mask & BL_V_VOLT) { d->sen.volt = old.volt; d->sen.mask |= BL_V_VOLT; }
        if (had & ~s.mask & BL_V_PRESS) { d->sen.press = old.press; d->sen.mask |= BL_V_PRESS; }
        d->has_sen = true;
        d->sen_ms = t;
        sensor_keep(d, t);
    }
beacon:;
    bl_beacon_t b;
    if (bl_beacon_decode(&d->ad, &b)) {
        d->bc = b;
        d->has_bc = true;
    }
}

static void ingest(const aos_ble_adv_t *a)
{
    int i = bl_find(a->addr);
    bl_dev_t *d = i >= 0 ? &BL.dev[i] : dev_new(a);
    if (!d) return;
    /* its first packet (a favourite waiting in the table has none yet) */
    if (!d->n_adv && !d->n_rsp) BL.seen_total++;
    uint32_t sec = a->t_ms / 1000;

    air_roll(sec);
    BL.air.pkts[sec % BL_AIR_HIST]++;
    BL.air.total++;

    hist_roll(d->hist, &d->hist_sec, sec);
    int8_t *bin = &d->hist[sec % BL_HIST];
    if (*bin == BL_NO_RSSI) BL.air.devs[sec % BL_AIR_HIST]++;
    if (a->rssi > *bin) *bin = a->rssi;

    d->rssi = a->rssi;
    if (a->rssi < d->rssi_min) d->rssi_min = a->rssi;
    if (a->rssi > d->rssi_max) d->rssi_max = a->rssi;
    d->rssi_avg += (a->rssi - d->rssi_avg) * 0.2f;
    d->last_ms = a->t_ms;
    d->addr_type = a->addr_type;
    d->kinds |= (uint8_t)(1u << (a->kind & 7));

    if (a->kind == AOS_BLE_ADV_SCAN_RSP) {
        d->n_rsp++;
        if (a->len != d->rsp_len || memcmp(a->data, d->rsp, a->len)) {
            memcpy(d->rsp, a->data, a->len);
            d->rsp_len = a->len;
            d->n_changes++;
            dev_decode(d, a->data, a->len, a->t_ms);
        }
        return;
    }
    d->n_adv++;
    /* The gap: a device sends each advertisement on three channels within a
     * few ms and the scanner hears one at a time, missing some while it
     * looks elsewhere. The interval is the low envelope of the gaps: down
     * fast, up slowly. */
    if (d->prev_adv_ms) {
        uint32_t gap = a->t_ms - d->prev_adv_ms;
        if (gap >= 15 && gap < 20000) {
            if (d->itvl_ms <= 0) d->itvl_ms = gap;
            else if (gap < d->itvl_ms) d->itvl_ms += (gap - d->itvl_ms) * 0.35f;
            else d->itvl_ms += (gap - d->itvl_ms) * 0.01f;
        }
    }
    d->prev_adv_ms = a->t_ms;
    if (a->len != d->adv_len || memcmp(a->data, d->adv, a->len)) {
        memcpy(d->adv, a->data, a->len);
        d->adv_len = a->len;
        d->n_changes++;
        dev_decode(d, a->data, a->len, a->t_ms);
    }
}

void bl_scan_drain(void)
{
    if (!BL.dev || !BL.rx) return;
    uint32_t t = now_ms();
    air_roll(t / 1000);
    int got = 0;
    for (int round = 0; round < 8; round++) {
        int n = aos_hal_ble_scan_read(BL.rx, RX_MAX);
        for (int k = 0; k < n; k++) ingest(&BL.rx[k]);
        got += n;
        if (n < RX_MAX) break;
    }
    BL.air.lost = aos_hal_ble_scan_lost();
    /* packets a second over the last two whole seconds, smoothed */
    uint32_t s = t / 1000;
    float pps = (BL.air.pkts[(s + BL_AIR_HIST - 1) % BL_AIR_HIST] + BL.air.pkts[(s + BL_AIR_HIST - 2) % BL_AIR_HIST]) * 0.5f;
    BL.air.pps += (pps - BL.air.pps) * 0.3f;
    (void)got;
    /* the open device's history rolls even when it is silent */
    for (int i = 0; i < BL.ndev; i++) hist_roll(BL.dev[i].hist, &BL.dev[i].hist_sec, s);
}

int bl_rssi_recent(const bl_dev_t *d, int secs)
{
    uint32_t s = now_ms() / 1000;
    int best = BL_NO_RSSI;
    if (s - d->hist_sec > (uint32_t)secs) return BL_NO_RSSI;
    for (int k = 0; k < secs && k < BL_HIST; k++) {
        uint32_t sec = s - k;
        if (sec > d->hist_sec) continue;
        int v = d->hist[sec % BL_HIST];
        if (v > best) best = v;
    }
    return best;
}

void bl_forget_all(void)
{
    /* the favourites stay, emptied of what they had */
    int n = 0;
    for (int i = 0; i < BL.ndev; i++) {
        if (!BL.dev[i].fav) continue;
        bl_dev_t keep = BL.dev[i];
        bl_dev_t *d = &BL.dev[n++];
        memset(d, 0, sizeof *d);
        memcpy(d->addr, keep.addr, 6);
        d->addr_type = keep.addr_type;
        d->fav = true;
        memcpy(d->alias, keep.alias, sizeof d->alias);
        d->has_key = keep.has_key;
        memcpy(d->key, keep.key, sizeof d->key);
        d->key_state = -1;
        d->ad = keep.ad;
        d->cls = keep.cls;
        d->label = keep.label;
        d->rssi_min = 127;
        d->rssi_max = -127;
        for (int k = 0; k < BL_HIST; k++) d->hist[k] = BL_NO_RSSI;
        d->sh = -1;
    }
    BL.ndev = n;
    BL.sel = -1;
    BL.seen_total = BL.forgotten = 0;
    for (int k = 0; k < BL_SH_MAX && BL.sh; k++) BL.sh[k].dev = -1;
    hash_rebuild();
    memset(&BL.air, 0, sizeof BL.air);
    BL.gen++;
}

/* -------------------------------------------------------------------------- */
/* Names and lines                                                             */
/* -------------------------------------------------------------------------- */

/* A name from the air (or typed in the portal) can carry anything: emoji,
 * typographic quotes, bytes cut in half. Drawn through aos_text_safe, into
 * one of a few buffers so two names can be in use at once (a row's name
 * and its subtitle's comparison). */
static const char *safe_name(const char *raw)
{
    static char buf[4][64];
    static int k;
    char *b = buf[k++ & 3];
    aos_text_safe(b, sizeof buf[0], raw);
    return b;
}

const char *bl_dev_name(const bl_dev_t *d)
{
    if (d->alias[0]) return safe_name(d->alias);
    if (d->ad.name[0]) return safe_name(d->ad.name);
    if (d->label) return _(d->label);
    if (d->ad.nmfg) {
        const char *c = bl_company_name(d->ad.mfg[0].company);
        if (c) return c;
    }
    if (d->has_bc) return _("Baliza");
    return _("Sin nombre");
}

void bl_sensor_line(const bl_sensor_t *s, char *out, size_t n)
{
    size_t k = 0;
    out[0] = 0;
    char v[24];
#define ADD(...) do { if (k < n) { if (k) k += snprintf(out + k, n - k, " · "); if (k < n) k += snprintf(out + k, n - k, __VA_ARGS__); } } while (0)
    if (s->encrypted) { ADD("%s", _("cifrado")); return; }
    if (s->mask & BL_V_TEMP) { bl_fmt_num(v, sizeof v, s->temp, 1); ADD("%s °C", v); }
    if (s->mask & BL_V_HUM) { bl_fmt_num(v, sizeof v, s->hum, 0); ADD("%s %%", v); }
    if (s->mask & BL_V_PRESS) { bl_fmt_num(v, sizeof v, s->press, 0); ADD("%s hPa", v); }
    if (s->mask & BL_V_CO2) ADD("%d ppm", (int)s->co2);
    if (s->mask & BL_V_PM25) ADD("PM2,5 %d", (int)s->pm25);
    if (s->mask & BL_V_LUX) ADD("%d lx", (int)s->lux);
    if (s->mask & BL_V_MOIST) ADD("%s %d %%", _("suelo"), (int)s->moist);
    if (s->mask & BL_V_WEIGHT) { bl_fmt_num(v, sizeof v, s->weight, 2); ADD("%s kg", v); }
    if (s->mask & BL_V_POWER) { bl_fmt_num(v, sizeof v, s->power, 1); ADD("%s W", v); }
    if (s->mask & BL_V_OPEN) ADD("%s", s->open ? _("abierto") : _("cerrado"));
    if (s->mask & BL_V_MOTION) ADD("%s", s->motion ? _("movimiento") : _("quieto"));
    if (s->mask & BL_V_HR) ADD("%d lpm", s->hr);
    if (s->mask & BL_V_BATT) ADD("%s %d %%", _("bat."), s->batt);
    else if (s->mask & BL_V_VOLT) { bl_fmt_num(v, sizeof v, s->volt, 2); ADD("%s V", v); }
#undef ADD
}

void bl_dev_sub(const bl_dev_t *d, char *out, size_t n)
{
    if (d->has_sen && d->sen.mask) {
        bl_sensor_line(&d->sen, out, n);
        return;
    }
    const char *parts[4];
    int np = 0;
    const char *co = d->ad.nmfg ? bl_company_name(d->ad.mfg[0].company) : NULL;
    if (co && strcmp(co, bl_dev_name(d))) parts[np++] = co;
    const char *ap[2];
    if (d->ad.nmfg && d->ad.mfg[0].company == 0x004C && bl_apple_types(&d->ad.mfg[0], ap, 1) > 0) parts[np++] = _(ap[0]);
    else if (d->label && !d->ad.name[0] && !d->alias[0]) { /* already the name */ }
    else if (d->label) parts[np++] = _(d->label);
    else if (d->cls != BL_CLS_UNKNOWN) parts[np++] = _(bl_class_name(d->cls));
    parts[np++] = _(bl_addr_kind_name(bl_addr_kind(d->addr, d->addr_type)));
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < np && k < n; i++) k += snprintf(out + k, n - k, "%s%s", i ? " · " : "", parts[i]);
}

static bool passes(const bl_dev_t *d, int filter)
{
    if (BL.hide_gone && !bl_alive(d) && !d->fav) return false;
    if (BL.min_rssi > -100 && (d->rssi_avg < BL.min_rssi || !bl_alive(d)) && !d->fav) return false;
    switch (filter) {
    case BL_FILT_NAMED:  return d->ad.name[0] || d->alias[0];
    case BL_FILT_FAV:    return d->fav;
    case BL_FILT_CONN:   return (d->kinds & ((1u << AOS_BLE_ADV_IND) | (1u << AOS_BLE_ADV_DIRECT_IND))) != 0;
    case BL_FILT_SENSOR: return d->has_sen;
    case BL_FILT_BEACON: return d->has_bc;
    case BL_FILT_APPLE:  return d->ad.nmfg && d->ad.mfg[0].company == 0x004C;
    default:             return true;
    }
}

static int s_sort;
static int cmp_dev(const void *pa, const void *pb)
{
    const bl_dev_t *a = &BL.dev[*(const int *)pa], *b = &BL.dev[*(const int *)pb];
    /* favourites first, then the living, then the order asked */
    if (a->fav != b->fav) return a->fav ? -1 : 1;
    bool la = bl_alive(a), lb = bl_alive(b);
    if (la != lb) return la ? -1 : 1;
    if (s_sort == BL_SORT_NAME) {
        bool na = a->ad.name[0] || a->alias[0], nb = b->ad.name[0] || b->alias[0];
        if (na != nb) return na ? -1 : 1;
        int c = strcasecmp(bl_dev_name(a), bl_dev_name(b));
        if (c) return c;
    } else if (s_sort == BL_SORT_RECENT) {
        if (a->first_ms != b->first_ms) return (int32_t)(b->first_ms - a->first_ms) > 0 ? 1 : -1;
    }
    float ra = la ? a->rssi_avg : -200, rb = lb ? b->rssi_avg : -200;
    if (ra != rb) return ra > rb ? -1 : 1;
    return memcmp(a->addr, b->addr, 6);
}

int bl_sorted(int *out, int max, int filter, int sort)
{
    int n = 0;
    for (int i = 0; i < BL.ndev && n < max; i++)
        if (passes(&BL.dev[i], filter)) out[n++] = i;
    s_sort = sort;
    qsort(out, n, sizeof out[0], cmp_dev);
    return n;
}

/* -------------------------------------------------------------------------- */
/* nombres.txt                                                                 */
/* -------------------------------------------------------------------------- */

static uint32_t s_names_mtime;
static uint32_t s_names_check;

static void names_path(char *out, size_t n)
{
    snprintf(out, n, "%s/ble/nombres.txt", card_root());
}

static uint32_t file_mtime(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (uint32_t)st.st_mtime ^ (uint32_t)st.st_size : 0;
}

/* One line of a small text file, without its end; false at the end of the
 * file. (fgets is not in AmoledOS's table for the apps.) */
static bool read_line(FILE *f, char *out, size_t n)
{
    size_t k = 0;
    bool any = false;
    char c;
    while (fread(&c, 1, 1, f) == 1) {
        any = true;
        if (c == '\n') break;
        if (c != '\r' && k + 1 < n) out[k++] = c;
    }
    out[k] = 0;
    return any;
}

static void names_load(void)
{
    char path[160];
    names_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    s_names_mtime = file_mtime(path);
    if (!f) return;
    for (int i = 0; i < BL.ndev; i++) {
        BL.dev[i].fav = false;
        BL.dev[i].alias[0] = 0;
    }
    char line[128];
    while (read_line(f, line, sizeof line)) {
        if (!line[0] || line[0] == '#') continue;
        char *p1 = strchr(line, '|');
        if (!p1) continue;
        *p1++ = 0;
        char *p2 = strchr(p1, '|');
        if (p2) *p2++ = 0;
        uint8_t a[6];
        if (!bl_parse_addr(line, a)) continue;
        int i = bl_find(a);
        if (i < 0) {
            /* a favourite not heard yet this time: a record waiting for it */
            aos_ble_adv_t fake = { .t_ms = 0 };
            memcpy(fake.addr, a, 6);
            bl_dev_t *d = dev_new(&fake);
            if (!d) continue;
            d->last_ms = now_ms() - BL_GONE_MS - 1;
            d->first_ms = d->last_ms;
            i = (int)(d - BL.dev);
        }
        BL.dev[i].fav = strchr(p1, '*') != NULL;
        if (p2) {
            snprintf(BL.dev[i].alias, sizeof BL.dev[i].alias, "%.27s", p2);
            bl_utf8_trim(BL.dev[i].alias);
        }
    }
    fclose(f);
    BL.gen++;
}

void bl_names_save(void)
{
    char path[160], dir[160];
    snprintf(dir, sizeof dir, "%s/ble", card_root());
    mkdir(dir, 0777);
    names_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("# BLE de P4OS: favoritos y nombres. dirección|*|nombre\n", f);
    for (int i = 0; i < BL.ndev; i++) {
        const bl_dev_t *d = &BL.dev[i];
        if (!d->fav && !d->alias[0]) continue;
        char a[20], l[80];
        bl_fmt_addr(d->addr, a, sizeof a);
        snprintf(l, sizeof l, "%s|%s|%s\n", a, d->fav ? "*" : "", d->alias);
        fputs(l, f);
    }
    fclose(f);
    s_names_mtime = file_mtime(path);
}

/* -------------------------------------------------------------------------- */
/* claves.txt: the keys of the devices that encrypt                            */
/* -------------------------------------------------------------------------- */

static uint32_t s_keys_mtime;

static void keys_path(char *out, size_t n)
{
    snprintf(out, n, "%s/ble/claves.txt", card_root());
}

void bl_redecode(bl_dev_t *d)
{
    /* the readings start again from what the bytes say with the key */
    d->has_sen = false;
    d->key_state = -1;
    memset(&d->sen, 0, sizeof d->sen);
    uint32_t t = d->last_ms;
    if (d->adv_len) dev_decode(d, d->adv, d->adv_len, t);
    if (d->rsp_len) dev_decode(d, d->rsp, d->rsp_len, t);
    /* a key tried only on an old packet proves little: a device that was
     * just activated again (a new bindkey) still has the old key's packet
     * here. The next one decides. */
    if (d->key_state == BL_KEY_WRONG && now_ms() - t > 30000) d->key_state = -1;
}

static void keys_load(void)
{
    char path[160];
    keys_path(path, sizeof path);
    s_keys_mtime = file_mtime(path);
    FILE *f = fopen(path, "r");
    static bool had[BL_DEV_MAX];
    for (int i = 0; i < BL.ndev; i++) {
        had[i] = BL.dev[i].has_key;
        BL.dev[i].has_key = false;
    }
    char line[160];
    while (f && read_line(f, line, sizeof line)) {
        if (!line[0] || line[0] == '#') continue;
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar++ = 0;
        uint8_t a[6], k[16];
        if (!bl_parse_addr(line, a) || !bl_parse_key(bar, k)) continue;
        int i = bl_find(a);
        if (i < 0) {
            aos_ble_adv_t fake = { .t_ms = 0 };
            memcpy(fake.addr, a, 6);
            bl_dev_t *d = dev_new(&fake);
            if (!d) continue;
            d->last_ms = d->first_ms = now_ms() - BL_GONE_MS - 1;
            i = (int)(d - BL.dev);
            had[i] = false;
        }
        bool same = BL.dev[i].has_key && !memcmp(BL.dev[i].key, k, 16);
        memcpy(BL.dev[i].key, k, 16);
        BL.dev[i].has_key = true;
        if (!same) bl_redecode(&BL.dev[i]);
    }
    if (f) fclose(f);
    for (int i = 0; i < BL.ndev; i++)
        if (had[i] && !BL.dev[i].has_key) bl_redecode(&BL.dev[i]);   /* a key taken away */
}

static void keys_save(void)
{
    char path[160], dir[160];
    snprintf(dir, sizeof dir, "%s/ble", card_root());
    mkdir(dir, 0777);
    keys_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs("# BLE de P4OS: claves de los equipos que cifran lo que anuncian (MiBeacon v4/v5, BTHome v2).\n"
          "# direccion|32 cifras hexadecimales\n", f);
    for (int i = 0; i < BL.ndev; i++) {
        const bl_dev_t *d = &BL.dev[i];
        if (!d->has_key) continue;
        char a[20], l[80];
        bl_fmt_addr(d->addr, a, sizeof a);
        int k = snprintf(l, sizeof l, "%s|", a);
        for (int b = 0; b < 16; b++) k += snprintf(l + k, sizeof l - k, "%02x", d->key[b]);
        snprintf(l + k, sizeof l - k, "\n");
        fputs(l, f);
    }
    fclose(f);
    s_keys_mtime = file_mtime(path);
}

void bl_key_set(int idx, const uint8_t *key)
{
    if (idx < 0 || idx >= BL.ndev) return;
    bl_dev_t *d = &BL.dev[idx];
    char a[20];
    bl_fmt_addr(d->addr, a, sizeof a);
    aos_hal_log("ble", "key for %s %s", a, key ? "set" : "cleared");
    d->has_key = key != NULL;
    if (key) memcpy(d->key, key, 16);
    else memset(d->key, 0, 16);
    keys_save();
    bl_redecode(d);
}

void bl_names_poll(void)
{
    uint32_t t = now_ms();
    if (s_names_check && t - s_names_check < 2000) return;
    s_names_check = t;
    char path[160];
    names_path(path, sizeof path);
    uint32_t m = file_mtime(path);
    if (m != s_names_mtime) names_load();
    keys_path(path, sizeof path);
    if (file_mtime(path) != s_keys_mtime) keys_load();
}

/* -------------------------------------------------------------------------- */
/* CSV                                                                         */
/* -------------------------------------------------------------------------- */

void bl_log_tick(void)
{
    static uint32_t last_min;
    if (!BL.log_csv) return;
    uint32_t t = now_ms();
    uint32_t min = t / 60000;
    if (min == last_min) return;
    last_min = min;
    FILE *f = NULL;
    char stamp[32] = "";
    if (BL.log_csv) {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char dir[160], path[200];
        snprintf(dir, sizeof dir, "%s/ble", card_root());
        mkdir(dir, 0777);
        if (aos_hal_time_is_valid()) {
            snprintf(path, sizeof path, "%s/sensores-%04d-%02d-%02d.csv", dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                     tm.tm_hour, tm.tm_min, tm.tm_sec);
        } else {
            /* no clock yet: the minutes since the board started */
            snprintf(path, sizeof path, "%s/sensores-sin-hora.csv", dir);
            snprintf(stamp, sizeof stamp, "+%u min", (unsigned)min);
        }
        struct stat st;
        bool fresh = stat(path, &st) != 0;
        f = fopen(path, "a");
        if (f && fresh) fputs("hora,direccion,nombre,formato,rssi,temperatura,humedad,presion,bateria,voltaje\n", f);
    }
    for (int i = 0; i < BL.ndev; i++) {
        const bl_dev_t *d = &BL.dev[i];
        if (!d->has_sen || d->sen.encrypted || t - d->sen_ms > 120000) continue;
        if (f) {
            char a[20], line[256], tv[16] = "", hv[16] = "", pv[16] = "", bv[8] = "", vv[16] = "";
            bl_fmt_addr(d->addr, a, sizeof a);
            const bl_sensor_t *s = &d->sen;
            if (s->mask & BL_V_TEMP) snprintf(tv, sizeof tv, "%.2f", (double)s->temp);
            if (s->mask & BL_V_HUM) snprintf(hv, sizeof hv, "%.1f", (double)s->hum);
            if (s->mask & BL_V_PRESS) snprintf(pv, sizeof pv, "%.1f", (double)s->press);
            if (s->mask & BL_V_BATT) snprintf(bv, sizeof bv, "%d", s->batt);
            if (s->mask & BL_V_VOLT) snprintf(vv, sizeof vv, "%.3f", (double)s->volt);
            char nm[32];
            snprintf(nm, sizeof nm, "%s", d->alias[0] ? d->alias : d->ad.name);
            for (char *p = nm; *p; p++) if (*p == ',' || *p == '"') *p = ' ';
            snprintf(line, sizeof line, "%s,%s,%s,%s,%d,%s,%s,%s,%s,%s\n", stamp, a, nm, s->format ? s->format : "",
                     d->rssi, tv, hv, pv, bv, vv);
            fputs(line, f);
        }
    }
    if (f) fclose(f);
}

/* the first load of the names, once the table exists */
void bl_names_first(void);
void bl_names_first(void)
{
    names_load();
    keys_load();
}
