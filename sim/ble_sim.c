/*
 * AmoledOS simulator - the Bluetooth LE the apps see (aos_hal_ble_*).
 *
 * A made-up neighbourhood instead of a radio: thermometers in the formats
 * people flash or buy (pvvx, BTHome, Govee, Ruuvi, Xiaomi's MiBeacon,
 * SwitchBot, Qingping, Inkbird), beacons (iBeacon, Eddystone), the phones,
 * earbuds, watches and trackers that fill any room, someone walking about
 * and someone passing by, and two devices that take a connection: a heart
 * rate strap and an ESP32 with environmental sensing and a UART that
 * answers. Nothing is anybody's real device: names, addresses and values
 * are invented.
 *
 * No threads: the packets a device would have sent since the last call are
 * made when the app reads, and the GATT client answers on the next poll.
 * Everything runs on LVGL's thread, as the app's calls do.
 */
#include "aos_hal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADV_RING 1024
#define EV_RING  64

typedef struct sdev sdev_t;
typedef void (*adv_fn)(sdev_t *d, uint32_t now, uint8_t *out, uint8_t *len, bool rsp);

enum { MOVE_STILL, MOVE_WALK, MOVE_PASS, MOVE_FAR };
enum { PROF_NONE, PROF_HR, PROF_ESP };

struct sdev {
    uint8_t addr[6];
    uint8_t type;           /* 0 public, 1 random */
    uint8_t kind;           /* AOS_BLE_ADV_* of its advertising */
    uint16_t itvl;          /* ms */
    float rssi;             /* at rest */
    int move;
    int prof;
    adv_fn adv;
    /* run time */
    uint32_t next;
    float walk;
    uint32_t seq;
    uint32_t phase;         /* ms, so the movers are not in step */
};

static uint32_t rnd(void)
{
    static uint32_t s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
static float frnd(void) { return (rnd() & 0xFFFF) / 65535.0f; }

static void ad(uint8_t *out, uint8_t *len, uint8_t type, const void *data, int n)
{
    if (*len + 2 + n > 31) return;
    out[(*len)++] = (uint8_t)(n + 1);
    out[(*len)++] = type;
    memcpy(out + *len, data, n);
    *len += (uint8_t)n;
}
static void ad_flags(uint8_t *out, uint8_t *len) { uint8_t f = 0x06; ad(out, len, 0x01, &f, 1); }
static void ad_name(uint8_t *out, uint8_t *len, const char *n) { ad(out, len, 0x09, n, (int)strlen(n)); }
static void put16(uint8_t *p, int v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
static void put16be(uint8_t *p, int v) { p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF; }

/* slow weather: a value drifting around a centre */
static float drift(uint32_t now, float centre, float span, float period_s, float off)
{
    return centre + span * sinf((now / 1000.0f) / period_s * 6.2831853f + off);
}

/* ---- the sensors ---- */

/* pvvx's custom format on a Xiaomi LYWSD03MMC: service data 0x181A */
static void adv_pvvx(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) { ad_name(o, n, "ATC_5C1A2B"); return; }
    ad_flags(o, n);
    uint8_t s[17];
    put16(s, 0x181A);
    for (int i = 0; i < 6; i++) s[2 + i] = d->addr[5 - i];
    put16(s + 8, (int)(drift(now, 23.4f, 0.8f, 600, 0) * 100));
    put16(s + 10, (int)(drift(now, 48.0f, 3.0f, 900, 1) * 100));
    put16(s + 12, 2950);
    s[14] = 87;
    s[15] = (uint8_t)(d->seq++);
    s[16] = 0x04;
    ad(o, n, 0x16, s, 17);
}

/* BTHome v2, not encrypted: temperature, humidity, battery */
static void adv_bthome(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d;
    if (rsp) return;
    ad_flags(o, n);
    uint8_t s[16];
    int k = 0;
    put16(s, 0xFCD2);
    k = 2;
    s[k++] = 0x40;                          /* v2, unencrypted, regular */
    s[k++] = 0x00; s[k++] = (uint8_t)(d->seq++);  /* packet id */
    s[k++] = 0x01; s[k++] = 64;            /* battery % */
    s[k++] = 0x02; put16(s + k, (int)(drift(now, 19.8f, 1.2f, 500, 2) * 100)); k += 2;
    s[k++] = 0x03; put16(s + k, (int)(drift(now, 61.0f, 4.0f, 700, 0.5f) * 100)); k += 2;
    ad(o, n, 0x16, s, k);
    ad_name(o, n, "THB2");
}

/* BTHome v2: a door sensor with a button, the window opening now and then */
static void adv_bthome_door(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) return;
    ad_flags(o, n);
    uint8_t s[12];
    int k = 0;
    put16(s, 0xFCD2);
    k = 2;
    s[k++] = 0x44;                          /* v2, unencrypted, trigger based */
    s[k++] = 0x00; s[k++] = (uint8_t)(d->seq++);
    s[k++] = 0x01; s[k++] = 92;
    s[k++] = 0x2D; s[k++] = (now / 15000) % 3 == 0;   /* window open */
    s[k++] = 0x3A; s[k++] = (now / 4000) % 9 == 0 ? 1 : 0; /* button: press */
    ad(o, n, 0x16, s, k);
    ad_name(o, n, "Puerta");
}

/* Govee H5075: name and manufacturer 0xEC88 with three packed bytes */
static void adv_govee(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d;
    if (rsp) return;
    ad_flags(o, n);
    ad_name(o, n, "GVH5075_4F21");
    float t = drift(now, 4.5f, 1.5f, 400, 0);           /* a fridge */
    float h = drift(now, 72.0f, 5.0f, 800, 0);
    uint32_t v = (uint32_t)(fabsf(t) * 10) * 1000 + (uint32_t)(h * 10);
    if (t < 0) v |= 0x800000;
    uint8_t m[8] = { 0x88, 0xEC, 0x00, (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, 76, 0x00 };
    ad(o, n, 0xFF, m, 8);
}

/* Ruuvi RAWv2 (format 5), outdoors */
static void adv_ruuvi(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) { ad_name(o, n, "Ruuvi 3F8C"); return; }
    ad_flags(o, n);
    uint8_t m[26];
    put16(m, 0x0499);
    m[2] = 5;
    put16be(m + 3, (int)(drift(now, 11.5f, 3.0f, 1200, 1) / 0.005f));
    put16be(m + 5, (int)(drift(now, 80.0f, 6.0f, 1500, 0) / 0.0025f));
    put16be(m + 7, (int)(drift(now, 101325.0f, 300.0f, 3000, 0) - 50000));
    put16be(m + 9, (int)(frnd() * 20 - 10));
    put16be(m + 11, (int)(frnd() * 20 - 10));
    put16be(m + 13, 1000 + (int)(frnd() * 10));
    put16be(m + 15, ((2980 - 1600) << 5) | ((4 + 40) / 2));
    m[17] = (uint8_t)(now / 60000);
    put16be(m + 18, (int)(d->seq++ & 0xFFFF));
    memcpy(m + 20, d->addr, 6);
    ad(o, n, 0xFF, m, 26);
}

/* Xiaomi's MiBeacon, an old unencrypted LYWSDCGQ: temperature and humidity */
static void adv_mibeacon(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) { ad_name(o, n, "MJ_HT_V1"); return; }
    ad_flags(o, n);
    uint8_t s[20];
    put16(s, 0xFE95);
    put16(s + 2, 0x2050);                   /* frame control: v2, MAC and object included */
    put16(s + 4, 0x01AA);                   /* product id */
    s[6] = (uint8_t)(d->seq++);
    for (int i = 0; i < 6; i++) s[7 + i] = d->addr[5 - i];
    put16(s + 13, 0x100D);                  /* temperature and humidity */
    s[15] = 4;
    put16(s + 16, (int)(drift(now, 21.0f, 0.6f, 650, 3) * 10));
    put16(s + 18, (int)(drift(now, 52.0f, 2.0f, 800, 1) * 10));
    ad(o, n, 0x16, s, 20);
}

/* A stock Xiaomi LYWSD03MMC: MiBeacon v5 encrypted, one reading a packet
 * (temperature 22.4, humidity 58, battery 83). Made beforehand with
 * pycryptodome and the test key 00 01 02 .. 0F, which is what the app
 * needs to read it (Cargar la clave); without it, "cifrado". */
static void adv_mi_enc(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)now;
    static const uint8_t P[3][23] = {
        { 0x58, 0x58, 0x5B, 0x05, 0x40, 0xA1, 0xF0, 0xE1, 0x38, 0xC1, 0xA4, 0x05, 0xE2, 0x35, 0xCC, 0x02, 0x01, 0x00, 0x00, 0x8A, 0xED, 0xA9, 0x58 },
        { 0x58, 0x58, 0x5B, 0x05, 0x41, 0xA1, 0xF0, 0xE1, 0x38, 0xC1, 0xA4, 0xC7, 0x2A, 0xC6, 0x1B, 0x02, 0x00, 0x00, 0x72, 0x8D, 0xB8, 0x63 },
        { 0x58, 0x58, 0x5B, 0x05, 0x42, 0xA1, 0xF0, 0xE1, 0x38, 0xC1, 0xA4, 0x10, 0x4A, 0x0A, 0xA3, 0x03, 0x00, 0x00, 0xBD, 0x09, 0x0C, 0xF3 },
    };
    if (rsp) {
        uint8_t m[13] = { 0x8F, 0x03, 0x10 };
        for (int i = 0; i < 6; i++) m[3 + i] = d->addr[5 - i];
        ad(o, n, 0xFF, m, 9);
        return;
    }
    ad_flags(o, n);
    int k = (int)(d->seq++ % 3);
    uint8_t s[25] = { 0x95, 0xFE };
    int len = k == 0 ? 23 : 22;
    memcpy(s + 2, P[k], len);
    ad(o, n, 0x16, s, 2 + len);
}

/* SwitchBot Meter: service data 0xFD3D, type 'T' */
static void adv_switchbot(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d;
    if (rsp) {
        uint8_t s[8];
        put16(s, 0xFD3D);
        float t = drift(now, 24.6f, 0.5f, 450, 0);
        s[2] = 0x54;
        s[3] = 0x00;
        s[4] = 100;
        s[5] = (uint8_t)((int)(t * 10) % 10);
        s[6] = (uint8_t)((int)t | 0x80);
        s[7] = (uint8_t)drift(now, 45.0f, 2.0f, 600, 0);
        ad(o, n, 0x16, s, 8);
        return;
    }
    ad_flags(o, n);
    uint8_t u[16] = { 0x1B, 0xC5, 0xD5, 0xA5, 0x02, 0x00, 0xB8, 0x9F, 0xE6, 0x11, 0x4D, 0x22, 0x00, 0x0D, 0xA2, 0xCB };
    ad(o, n, 0x07, u, 16);
    uint8_t m[8] = { 0x69, 0x09 };
    memcpy(m + 2, d->addr, 6);
    ad(o, n, 0xFF, m, 8);
}

/* Qingping: service data 0xFDCD */
static void adv_qingping(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) return;
    ad_flags(o, n);
    uint8_t s[19];
    put16(s, 0xFDCD);
    s[2] = 0x08;
    s[3] = 0x01;                            /* temperature and humidity monitor */
    for (int i = 0; i < 6; i++) s[4 + i] = d->addr[5 - i];
    s[10] = 0x01; s[11] = 4;
    put16(s + 12, (int)(drift(now, 26.1f, 0.4f, 500, 2) * 10));
    put16(s + 14, (int)(drift(now, 40.0f, 1.5f, 700, 2) * 10));
    s[16] = 0x02; s[17] = 1; s[18] = 55;
    ad(o, n, 0x16, s, 19);
}

/* Inkbird IBS-TH2: name "sps" and the temperature where a company id goes */
static void adv_inkbird(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d;
    if (rsp) { ad_name(o, n, "sps"); return; }
    ad_flags(o, n);
    uint8_t m[9];
    put16(m, (int)(drift(now, -18.2f, 0.7f, 300, 0) * 100) & 0xFFFF);
    put16(m + 2, (int)(drift(now, 41.0f, 3.0f, 500, 1) * 100));
    m[4] = 0x00;
    put16(m + 5, 0x1234);
    m[7] = 71;
    m[8] = 0x08;
    ad(o, n, 0xFF, m, 9);
}

/* ---- beacons ---- */

static void adv_ibeacon(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    ad_flags(o, n);
    uint8_t m[25] = { 0x4C, 0x00, 0x02, 0x15,
                      0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2, 0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0,
                      0x00, 0x01, 0x00, 0x2A, (uint8_t)-59 };
    ad(o, n, 0xFF, m, 25);
}

/* Eddystone: URL, TLM and UID in turns */
static void adv_eddystone(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    if (rsp) return;
    ad_flags(o, n);
    uint8_t u[2] = { 0xAA, 0xFE };
    ad(o, n, 0x03, u, 2);
    uint8_t s[24];
    int k = 0;
    put16(s, 0xFEAA);
    k = 2;
    int f = d->seq++ % 3;
    if (f == 0) {
        s[k++] = 0x10; s[k++] = (uint8_t)-20; s[k++] = 0x03;     /* https:// */
        memcpy(s + k, "example", 7); k += 7;
        s[k++] = 0x07;                                          /* .com */
    } else if (f == 1) {
        s[k++] = 0x20; s[k++] = 0x00;
        put16be(s + k, 2870); k += 2;
        int t = (int)(drift(now, 17.0f, 1.0f, 900, 0) * 256);
        put16be(s + k, t); k += 2;
        uint32_t cnt = now / 500, sec = now / 100;
        s[k++] = cnt >> 24; s[k++] = cnt >> 16; s[k++] = cnt >> 8; s[k++] = cnt;
        s[k++] = sec >> 24; s[k++] = sec >> 16; s[k++] = sec >> 8; s[k++] = sec;
    } else {
        s[k++] = 0x00; s[k++] = (uint8_t)-20;
        static const uint8_t ns[10] = { 0xED, 0xD1, 0xEB, 0xEA, 0xC0, 0x4E, 0x5D, 0xEF, 0xA0, 0x17 };
        memcpy(s + k, ns, 10); k += 10;
        static const uint8_t in[6] = { 0x00, 0x00, 0x00, 0x00, 0x04, 0xD2 };
        memcpy(s + k, in, 6); k += 6;
        s[k++] = 0; s[k++] = 0;                                 /* RFU */
    }
    ad(o, n, 0x16, s, k);
}

/* ---- what fills any room ---- */

/* an iPhone: Nearby Info, and an address that changes every 15 minutes */
static void adv_iphone(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)now;
    if (rsp) return;
    ad_flags(o, n);
    uint8_t m[11] = { 0x4C, 0x00, 0x10, 0x07, 0x3B, 0x1C, 0x9A, 0x51, 0x2E, 0x77, 0x18 };
    m[10] = (uint8_t)(0x10 | (d->seq++ & 0x0F));      /* the nibble that changes */
    ad(o, n, 0xFF, m, 11);
    uint8_t tx = 12;
    ad(o, n, 0x0A, &tx, 1);
}

/* earbuds out of their case: Proximity Pairing with the batteries */
static void adv_airpods(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    uint8_t m[29] = { 0x4C, 0x00, 0x07, 0x19, 0x01, 0x14, 0x20, 0x55, 0x98, 0x8F, 0x01, 0x00 };
    for (int i = 12; i < 29; i++) m[i] = (uint8_t)rnd();
    ad(o, n, 0xFF, m, 29);
}

/* a tracker away from its owner: Find My's offline finding */
static void adv_findmy(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    uint8_t m[29] = { 0x4C, 0x00, 0x12, 0x19, 0x10 };
    for (int i = 5; i < 27; i++) m[i] = (uint8_t)(0x31 * i + 7);
    m[27] = 0x01; m[28] = 0x00;
    ad(o, n, 0xFF, m, 29);
}

/* a Mac nearby: Handoff */
static void adv_mac(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    ad_flags(o, n);
    /* Handoff and Nearby Info in one manufacturer structure, as a Mac does */
    uint8_t m[25] = { 0x4C, 0x00, 0x0C, 0x0E, 0x00 };
    for (int i = 5; i < 18; i++) m[i] = (uint8_t)rnd();
    const uint8_t ni[7] = { 0x10, 0x05, 0x01, 0x18, 0x44, 0x9B, 0x2C };
    memcpy(m + 18, ni, 7);
    ad(o, n, 0xFF, m, 25);
}

/* a mouse waiting to be paired: Microsoft's Swift Pair */
static void adv_swiftpair(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) { ad_name(o, n, "Mouse BT 4"); return; }
    ad_flags(o, n);
    uint8_t m[6] = { 0x06, 0x00, 0x03, 0x00, 0x80, 0x00 };
    ad(o, n, 0xFF, m, 5);
    uint8_t ap[2];
    put16(ap, 0x03C2);                      /* mouse */
    ad(o, n, 0x19, ap, 2);
    uint8_t u[2] = { 0x12, 0x18 };
    ad(o, n, 0x03, u, 2);
}

/* earbuds announcing Google's Fast Pair */
static void adv_fastpair(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    ad_flags(o, n);
    uint8_t s[5] = { 0x2C, 0xFE, 0x0A, 0x5C, 0x7D };
    ad(o, n, 0x16, s, 5);
    int8_t tx = -10;
    ad(o, n, 0x0A, &tx, 1);
}

/* a watch: Samsung's company id */
static void adv_samsung(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) { ad_name(o, n, "Reloj (A1B2) LE"); return; }
    ad_flags(o, n);
    uint8_t m[12] = { 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x00 };
    ad(o, n, 0xFF, m, 12);
    uint8_t ap[2];
    put16(ap, 0x00C2);                      /* smartwatch */
    ad(o, n, 0x19, ap, 2);
}

/* a Tile */
static void adv_tile(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) return;
    ad_flags(o, n);
    uint8_t u[2] = { 0xED, 0xFE };
    ad(o, n, 0x03, u, 2);
    uint8_t s[10] = { 0xED, 0xFE, 0x02, 0x00, 0x91, 0x7C, 0x2E, 0x01, 0x55, 0xA0 };
    ad(o, n, 0x16, s, 10);
}

/* a car's or a speaker's, only its company: Garmin, Bose, Sony... */
static void adv_vendor(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)now;
    if (rsp) return;
    ad_flags(o, n);
    static const uint16_t cid[] = { 0x0087, 0x009E, 0x012D, 0x0157, 0x0059 };
    uint8_t m[8];
    put16(m, cid[d->addr[5] % 5]);
    for (int i = 2; i < 8; i++) m[i] = (uint8_t)(d->addr[i - 2] ^ (d->seq >> 3));
    d->seq++;
    ad(o, n, 0xFF, m, 8);
}

/* ---- the two that take a connection ---- */

static void adv_hr(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) { ad_name(o, n, "Banda HR 7C21"); return; }
    ad_flags(o, n);
    uint8_t u[4] = { 0x0D, 0x18, 0x0F, 0x18 };
    ad(o, n, 0x03, u, 4);
    uint8_t ap[2];
    put16(ap, 0x0341);                      /* heart rate belt */
    ad(o, n, 0x19, ap, 2);
    int8_t tx = 0;
    ad(o, n, 0x0A, &tx, 1);
}

static void adv_esp(sdev_t *d, uint32_t now, uint8_t *o, uint8_t *n, bool rsp)
{
    (void)d; (void)now;
    if (rsp) {
        uint8_t u[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
        ad(o, n, 0x07, u, 16);
        return;
    }
    ad_flags(o, n);
    ad_name(o, n, "ESP32-Taller");
    uint8_t u[2] = { 0x1A, 0x18 };
    ad(o, n, 0x03, u, 2);
    uint8_t m[4] = { 0xE5, 0x02, 0x01, 0x07 };    /* Espressif */
    ad(o, n, 0xFF, m, 4);
}

#define A(a, b, c, d, e, f) { 0x##a, 0x##b, 0x##c, 0x##d, 0x##e, 0x##f }
/* the run-time fields after 'adv' start at zero */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static sdev_t DEV[] = {
    { A(A4, C1, 38, 5C, 1A, 2B), 0, AOS_BLE_ADV_IND, 2500, -62, MOVE_STILL, 0, adv_pvvx },
    { A(D8, 3B, 7A, 11, 40, E2), 1, AOS_BLE_ADV_NONCONN_IND, 1500, -71, MOVE_STILL, 0, adv_bthome },
    { A(E4, 9A, 12, 33, 08, C5), 1, AOS_BLE_ADV_NONCONN_IND, 1000, -77, MOVE_STILL, 0, adv_bthome_door },
    { A(A4, C1, 38, 52, 4F, 21), 0, AOS_BLE_ADV_IND, 2000, -80, MOVE_STILL, 0, adv_govee },
    { A(F1, 0C, 55, 9D, 3F, 8C), 1, AOS_BLE_ADV_IND, 1285, -86, MOVE_STILL, 0, adv_ruuvi },
    { A(58, 2D, 34, 10, 77, 6A), 0, AOS_BLE_ADV_IND, 3000, -74, MOVE_STILL, 0, adv_mibeacon },
    { A(A4, C1, 38, E1, F0, A1), 0, AOS_BLE_ADV_IND, 2400, -59, MOVE_STILL, 0, adv_mi_enc },
    { A(C9, 21, 7E, 0A, 3D, 91), 1, AOS_BLE_ADV_IND, 1800, -68, MOVE_STILL, 0, adv_switchbot },
    { A(58, 2D, 34, 54, 2C, 08), 0, AOS_BLE_ADV_NONCONN_IND, 2000, -83, MOVE_STILL, 0, adv_qingping },
    { A(49, 22, 06, 12, 3A, 7B), 0, AOS_BLE_ADV_IND, 2500, -88, MOVE_STILL, 0, adv_inkbird },
    { A(DC, 0D, 30, 01, 2A, 6E), 1, AOS_BLE_ADV_NONCONN_IND, 100, -58, MOVE_STILL, 0, adv_ibeacon },
    { A(C3, 7F, 21, 98, 0B, 44), 1, AOS_BLE_ADV_NONCONN_IND, 350, -79, MOVE_STILL, 0, adv_eddystone },
    { A(5A, 21, 9C, 3E, 70, 12), 1, AOS_BLE_ADV_IND, 180, -55, MOVE_STILL, 0, adv_iphone },
    { A(6E, 03, A1, 4C, 92, 5D), 1, AOS_BLE_ADV_IND, 270, -66, MOVE_WALK, 0, adv_iphone },
    { A(4F, 88, 12, 6B, 31, 0A), 1, AOS_BLE_ADV_NONCONN_IND, 200, -60, MOVE_WALK, 0, adv_airpods },
    { A(F7, 41, 2A, 9C, 05, B3), 1, AOS_BLE_ADV_NONCONN_IND, 2000, -84, MOVE_STILL, 0, adv_findmy },
    { A(72, 1E, 4B, 0D, 88, 39), 1, AOS_BLE_ADV_IND, 160, -64, MOVE_STILL, 0, adv_mac },
    { A(E2, 7C, 90, 3B, 14, 6F), 1, AOS_BLE_ADV_IND, 100, -70, MOVE_STILL, 0, adv_swiftpair },
    { A(66, 2A, 81, 09, C4, 1D), 1, AOS_BLE_ADV_IND, 250, -78, MOVE_PASS, 0, adv_fastpair },
    { A(8C, 79, F5, 22, 61, A8), 0, AOS_BLE_ADV_IND, 500, -73, MOVE_STILL, 0, adv_samsung },
    { A(E9, 10, 4D, 77, 2B, 03), 1, AOS_BLE_ADV_NONCONN_IND, 2200, -90, MOVE_FAR, 0, adv_tile },
    { A(3C, 61, 05, 1E, 9A, 44), 0, AOS_BLE_ADV_NONCONN_IND, 800, -92, MOVE_FAR, 0, adv_vendor },
    { A(7A, 33, 0E, 51, 2C, 9B), 1, AOS_BLE_ADV_NONCONN_IND, 640, -87, MOVE_PASS, 0, adv_vendor },
    { A(D0, 5F, 64, 8A, 17, 3E), 1, AOS_BLE_ADV_IND, 100, -66, MOVE_WALK, PROF_HR, adv_hr },
    { A(24, 0A, C4, 6E, 1F, 52), 0, AOS_BLE_ADV_IND, 200, -52, MOVE_STILL, PROF_ESP, adv_esp },
};
#pragma GCC diagnostic pop
#define NDEV ((int)(sizeof DEV / sizeof DEV[0]))

static float rssi_now(sdev_t *d, uint32_t now, bool *present)
{
    *present = true;
    float t = (now + d->phase) / 1000.0f;
    float r = d->rssi + d->walk;
    if (d->move == MOVE_WALK) r += -10 + 12 * sinf(t / 25.0f * 6.2831853f);
    else if (d->move == MOVE_PASS) {
        float c = fmodf(t, 120.0f);         /* by every two minutes, for forty seconds */
        if (c > 40) *present = false;
        r += 14 * sinf(c / 40.0f * 3.14159265f) - 10;
    } else if (d->move == MOVE_FAR) {
        if (frnd() < 0.35f) *present = false;   /* at the edge: often lost */
    }
    /* multipath: a few dB either way, now and then a deep fade */
    r += (frnd() + frnd() + frnd() - 1.5f) * 4.0f;
    if (frnd() < 0.04f) r -= 8 + frnd() * 10;
    if (r > -30) r = -30;
    return r;
}

/* ---- the scanner ---- */

static aos_ble_adv_t s_ring[ADV_RING];
static uint32_t s_w, s_r, s_lost;
static bool s_scan, s_active;
static int s_duty = 30;
static uint32_t s_last;
static bool s_init;

static void push(sdev_t *d, uint32_t now, int8_t rssi, bool rsp)
{
    aos_ble_adv_t a = { 0 };
    a.t_ms = now;
    memcpy(a.addr, d->addr, 6);
    a.addr_type = d->type;
    a.kind = rsp ? AOS_BLE_ADV_SCAN_RSP : d->kind;
    a.rssi = rssi;
    d->adv(d, now, a.data, &a.len, rsp);
    if (rsp && !a.len) return;              /* nothing to answer with: no response */
    if (s_w - s_r >= ADV_RING) { s_r++; s_lost++; }
    s_ring[s_w++ % ADV_RING] = a;
}

static void crowd_init(uint32_t now);

static void world_init(uint32_t now)
{
    if (s_init) return;
    s_init = true;
    for (int i = 0; i < NDEV; i++) {
        DEV[i].next = now + rnd() % DEV[i].itvl;
        DEV[i].phase = rnd() % 60000;
    }
    crowd_init(now);
}

/* A crowd, for a fair: AOS_SIM_BLE_CROWD=N phones that come and go, each
 * with a private address it changes every minute, as phones do (every 15
 * minutes or so, in truth). Off unless asked: the tests count on the 25
 * above. */
static sdev_t *s_crowd;
static int s_ncrowd;
static uint32_t s_crowd_rot;

static void crowd_addr(sdev_t *d)
{
    for (int k = 0; k < 6; k++) d->addr[k] = (uint8_t)rnd();
    d->addr[0] = (uint8_t)((d->addr[0] & 0x3F) | 0x40);   /* resolvable private */
}

static void crowd_init(uint32_t now)
{
    const char *e = getenv("AOS_SIM_BLE_CROWD");
    int n = e ? atoi(e) : 0;
    if (n <= 0) return;
    if (n > 4000) n = 4000;
    s_crowd = calloc((size_t)n, sizeof *s_crowd);
    if (!s_crowd) return;
    s_ncrowd = n;
    for (int i = 0; i < n; i++) {
        sdev_t *d = &s_crowd[i];
        crowd_addr(d);
        d->type = 1;
        d->kind = AOS_BLE_ADV_IND;
        d->itvl = (uint16_t)(150 + rnd() % 900);
        d->rssi = -60 - (float)(rnd() % 34);
        d->move = rnd() % 3 == 0 ? MOVE_WALK : MOVE_STILL;
        d->adv = adv_iphone;
        d->next = now + rnd() % d->itvl;
        d->phase = rnd() % 60000;
    }
    s_crowd_rot = now + 60000;
    printf("[ble] a crowd of %d phones\n", n);
}

static int world_count(void) { return NDEV + s_ncrowd; }
static sdev_t *world_dev(int i) { return i < NDEV ? &DEV[i] : &s_crowd[i - NDEV]; }

static void world_run(uint32_t now)
{
    world_init(now);
    if (!s_scan) { s_last = now; return; }
    if (now - s_last > 3000) s_last = now - 3000;
    if (s_ncrowd && (int32_t)(now - s_crowd_rot) >= 0) {
        /* a twentieth of them, a new address each, every three seconds:
         * all of them in a minute */
        for (int i = 0; i < s_ncrowd; i++) if (rnd() % 20 == 0) crowd_addr(&s_crowd[i]);
        s_crowd_rot = now + 3000;
    }
    for (int i = 0; i < world_count(); i++) {
        sdev_t *d = world_dev(i);
        if ((int32_t)(d->next - s_last) < 0) d->next = s_last;
        while ((int32_t)(d->next - now) <= 0) {
            uint32_t t = d->next;
            /* the 0-10 ms of jitter every advertiser adds */
            d->next += d->itvl + rnd() % 10;
            d->walk += (frnd() - 0.5f) * 0.6f;
            if (d->walk > 6) d->walk = 6;
            if (d->walk < -6) d->walk = -6;
            /* one packet on each of the three channels; the scanner is on
             * one of them for a share of the time */
            if (frnd() * 100 >= s_duty) continue;
            bool present;
            float r = rssi_now(d, t, &present);
            if (!present || r < -97) continue;
            push(d, t, (int8_t)r, false);
            bool scannable = d->kind == AOS_BLE_ADV_IND || d->kind == AOS_BLE_ADV_SCAN_IND;
            if (s_active && scannable && frnd() < 0.8f) push(d, t + 1, (int8_t)(r + (frnd() - 0.5f) * 2), true);
        }
    }
    s_last = now;
}

bool aos_hal_ble_scan_start(bool active, int duty_pct)
{
    if (!aos_hal_bt_enabled()) return false;
    uint32_t now = aos_hal_uptime_ms();
    world_init(now);
    if (!s_scan) s_last = now;
    s_scan = true;
    s_active = active;
    s_duty = duty_pct < 5 ? 5 : duty_pct > 100 ? 100 : duty_pct;
    s_r = s_w;
    s_lost = 0;
    return true;
}

void aos_hal_ble_scan_stop(void) { s_scan = false; }

bool aos_hal_ble_scanning(void)
{
    if (s_scan && !aos_hal_bt_enabled()) s_scan = false;
    return s_scan;
}

int aos_hal_ble_scan_read(aos_ble_adv_t *out, int max)
{
    if (!aos_hal_ble_scanning()) return 0;
    world_run(aos_hal_uptime_ms());
    int n = 0;
    while (n < max && s_r != s_w) out[n++] = s_ring[s_r++ % ADV_RING];
    return n;
}

uint32_t aos_hal_ble_scan_lost(void) { return s_lost; }

/* ---- the GATT client ---- */

typedef struct {
    uint16_t handle;
    uint8_t kind, props;
    uint16_t uuid16;            /* 0: uuid128 */
    const uint8_t *uuid128;
    uint16_t end;
    const char *text;           /* a fixed string value */
} gattr_t;

static const uint8_t U_NUS[16]    = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
static const uint8_t U_NUS_RX[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E };
static const uint8_t U_NUS_TX[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E };
static const uint8_t U_SECRET[16] = { 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x10, 0x32, 0x54, 0x76, 0x01, 0x00, 0x00, 0xC0 };

#define S_(h, e, u) { h, AOS_BLE_ATTR_SERVICE, 0, u, NULL, e, NULL }
#define C_(h, p, u, t) { h, AOS_BLE_ATTR_CHAR, p, u, NULL, 0, t }
#define C128(h, p, u) { h, AOS_BLE_ATTR_CHAR, p, 0, u, 0, NULL }
#define D_(h, u) { h, AOS_BLE_ATTR_DESC, 0, u, NULL, 0, NULL }

static const gattr_t HR_TABLE[] = {
    S_(1, 7, 0x1800),
    C_(3, 0x02, 0x2A00, "Banda HR 7C21"),
    C_(5, 0x02, 0x2A01, NULL),
    C_(7, 0x02, 0x2A04, NULL),
    S_(8, 11, 0x1801),
    C_(10, 0x20, 0x2A05, NULL),
    D_(11, 0x2902),
    S_(12, 17, 0x180D),
    C_(14, 0x10, 0x2A37, NULL),
    D_(15, 0x2902),
    C_(17, 0x02, 0x2A38, NULL),
    S_(18, 21, 0x180F),
    C_(20, 0x12, 0x2A19, NULL),
    D_(21, 0x2902),
    S_(22, 34, 0x180A),
    C_(24, 0x02, 0x2A29, "Pulsos S.A."),
    C_(26, 0x02, 0x2A24, "HR-7"),
    C_(28, 0x02, 0x2A25, "7C21-0042"),
    C_(30, 0x02, 0x2A27, "B"),
    C_(32, 0x02, 0x2A26, "2.4.1"),
    C_(34, 0x02, 0x2A28, "2.4.1 (build 812)"),
};

static const gattr_t ESP_TABLE[] = {
    S_(1, 5, 0x1800),
    C_(3, 0x02, 0x2A00, "ESP32-Taller"),
    C_(5, 0x02, 0x2A01, NULL),
    S_(6, 16, 0x181A),
    C_(8, 0x12, 0x2A6E, NULL),
    D_(9, 0x2902),
    C_(11, 0x12, 0x2A6F, NULL),
    D_(12, 0x2902),
    C_(14, 0x02, 0x2A6D, NULL),
    D_(15, 0x2901),                 /* user description */
    D_(16, 0x2904),                 /* presentation format */
    { 17, AOS_BLE_ATTR_SERVICE, 0, 0, U_NUS, 22, NULL },
    C128(19, 0x0C, U_NUS_RX),
    C128(21, 0x10, U_NUS_TX),
    D_(22, 0x2902),
    { 23, AOS_BLE_ATTR_SERVICE, 0, 0, U_SECRET, 25, NULL },
    C128(25, 0x0A, U_SECRET),
    S_(26, 32, 0x180A),
    C_(28, 0x02, 0x2A29, "Espressif"),
    C_(30, 0x02, 0x2A24, "ESP32-C3-DevKitM-1"),
    C_(32, 0x02, 0x2A26, "v5.5.1"),
};

static int s_gstate = AOS_BLE_GATT_IDLE, s_greason;
static sdev_t *s_gdev;
static uint32_t s_gt;
static const gattr_t *s_tab;
static int s_ntab;
static bool s_sub[64];
static uint32_t s_sub_next;
static aos_ble_gatt_ev_t s_ev[EV_RING];
static uint32_t s_ev_w, s_ev_r;
static uint8_t s_nus_tx[244];
static int s_nus_len;
static bool s_nus_pending;

static void ev_put(uint8_t type, uint16_t h, int st, const void *data, int len)
{
    aos_ble_gatt_ev_t *e = &s_ev[s_ev_w++ % EV_RING];
    if (s_ev_w - s_ev_r > EV_RING) s_ev_r = s_ev_w - EV_RING;
    memset(e, 0, sizeof *e);
    e->t_ms = aos_hal_uptime_ms();
    e->type = type;
    e->handle = h;
    e->status = (int16_t)st;
    if (len > (int)sizeof e->data) len = sizeof e->data;
    e->len = (uint16_t)len;
    if (len > 0) memcpy(e->data, data, len);
}

static const gattr_t *find(uint16_t h)
{
    for (int i = 0; i < s_ntab; i++) if (s_tab[i].handle == h) return &s_tab[i];
    return NULL;
}

/* A characteristic's value, made now: returns its length. */
static int value_of(const gattr_t *a, uint8_t *v)
{
    uint32_t now = aos_hal_uptime_ms();
    if (a->text) { int n = (int)strlen(a->text); memcpy(v, a->text, n); return n; }
    switch (a->uuid16) {
    case 0x2A01: put16(v, s_tab == HR_TABLE ? 0x0341 : 0x0540); return 2;
    case 0x2A04: put16(v, 24); put16(v + 2, 40); put16(v + 4, 0); put16(v + 6, 400); return 8;
    case 0x2A05: put16(v, 1); put16(v + 2, 0xFFFF); return 4;
    case 0x2A37: v[0] = 0x16; v[1] = (uint8_t)drift(now, 92, 25, 60, 0);   /* flags: contact, RR */
                 put16(v + 2, (int)(60000.0f / v[1] * 1.024f)); return 4;
    case 0x2A38: v[0] = 1; return 1;                                         /* chest */
    case 0x2A19: v[0] = 78; return 1;
    case 0x2A6E: put16(v, (int)(drift(now, 27.3f, 0.6f, 120, 0) * 100)); return 2;
    case 0x2A6F: put16(v, (int)(drift(now, 38.0f, 2.0f, 150, 0) * 100)); return 2;
    case 0x2A6D: { uint32_t p = (uint32_t)(drift(now, 101180, 80, 300, 0) * 10);
                   v[0] = p; v[1] = p >> 8; v[2] = p >> 16; v[3] = p >> 24; return 4; }
    case 0x2901: memcpy(v, "Presion (Pa x10)", 16); return 16;
    case 0x2904: v[0] = 0x08; v[1] = 0xFF; put16(v + 2, 0x2724); v[4] = 1; put16(v + 5, 0); return 7;
    case 0x2902: v[0] = 0; v[1] = 0; return 2;
    }
    return 0;
}

static bool gatt_ready(void) { return s_gstate == AOS_BLE_GATT_READY; }

bool aos_hal_ble_gatt_connect(const uint8_t addr[6], uint8_t addr_type)
{
    (void)addr_type;
    if (!aos_hal_bt_enabled()) return false;
    s_gdev = NULL;
    for (int i = 0; i < NDEV; i++) if (!memcmp(DEV[i].addr, addr, 6)) s_gdev = &DEV[i];
    s_gstate = AOS_BLE_GATT_CONNECTING;
    s_greason = 0;
    s_gt = aos_hal_uptime_ms();
    memset(s_sub, 0, sizeof s_sub);
    s_ev_r = s_ev_w;
    s_nus_pending = false;
    return true;
}

void aos_hal_ble_gatt_disconnect(void) { s_gstate = AOS_BLE_GATT_IDLE; s_gdev = NULL; }

static void gatt_run(void)
{
    uint32_t now = aos_hal_uptime_ms();
    if (s_gstate == AOS_BLE_GATT_CONNECTING && now - s_gt > 600) {
        /* only the two with a profile take the connection; the rest do
         * not answer, as a beacon would, and it times out like NimBLE's */
        if (!s_gdev || s_gdev->prof == PROF_NONE || s_gdev->kind != AOS_BLE_ADV_IND) {
            if (now - s_gt > 8000 || !s_gdev || s_gdev->kind != AOS_BLE_ADV_IND) {
                s_gstate = AOS_BLE_GATT_FAILED;
                s_greason = s_gdev && s_gdev->kind == AOS_BLE_ADV_IND ? 0x208 : 13;
            }
            return;
        }
        s_gstate = AOS_BLE_GATT_DISCOVERING;
        s_gt = now;
        s_tab = s_gdev->prof == PROF_HR ? HR_TABLE : ESP_TABLE;
        s_ntab = s_gdev->prof == PROF_HR ? (int)(sizeof HR_TABLE / sizeof HR_TABLE[0])
                                          : (int)(sizeof ESP_TABLE / sizeof ESP_TABLE[0]);
    }
    if (s_gstate == AOS_BLE_GATT_DISCOVERING && now - s_gt > 900) {
        s_gstate = AOS_BLE_GATT_READY;
        s_sub_next = now;
    }
    if (!gatt_ready()) return;
    /* the strap walks about: out of reach now and then */
    bool present;
    if (rssi_now(s_gdev, now, &present) < -94) {
        s_gstate = AOS_BLE_GATT_FAILED;
        s_greason = 0x208;
        return;
    }
    if ((int32_t)(now - s_sub_next) >= 0) {
        s_sub_next = now + 1000;
        for (int i = 0; i < s_ntab; i++) {
            const gattr_t *a = &s_tab[i];
            if (a->kind != AOS_BLE_ATTR_CHAR || a->handle >= 64 || !s_sub[a->handle] || a->uuid128) continue;
            uint8_t v[32];
            int n = value_of(a, v);
            ev_put(a->props & 0x10 ? AOS_BLE_EV_NOTIFY : AOS_BLE_EV_INDICATE, a->handle, 0, v, n);
        }
    }
    if (s_nus_pending && s_sub[21]) {
        ev_put(AOS_BLE_EV_NOTIFY, 21, 0, s_nus_tx, s_nus_len);
        s_nus_pending = false;
    }
}

aos_ble_gatt_state_t aos_hal_ble_gatt_state(int *reason)
{
    gatt_run();
    if (reason) *reason = s_greason;
    return (aos_ble_gatt_state_t)s_gstate;
}

uint16_t aos_hal_ble_gatt_mtu(void) { return gatt_ready() ? 247 : 0; }

bool aos_hal_ble_gatt_rssi(int8_t *rssi)
{
    if (!gatt_ready()) return false;
    bool present;
    if (rssi) *rssi = (int8_t)rssi_now(s_gdev, aos_hal_uptime_ms(), &present);
    return true;
}

int aos_hal_ble_gatt_attrs(aos_ble_attr_t *out, int max)
{
    if (!gatt_ready()) return 0;
    if (!out) return s_ntab;
    int n = 0;
    for (int i = 0; i < s_ntab && n < max; i++, n++) {
        const gattr_t *a = &s_tab[i];
        aos_ble_attr_t *o = &out[n];
        memset(o, 0, sizeof *o);
        o->kind = a->kind;
        o->props = a->props;
        o->handle = a->handle;
        o->end = a->kind == AOS_BLE_ATTR_SERVICE ? a->end : a->kind == AOS_BLE_ATTR_CHAR ? a->handle - 1 : 0;
        if (a->uuid128) { o->uuid_len = 16; memcpy(o->uuid, a->uuid128, 16); }
        else { o->uuid_len = 2; put16(o->uuid, a->uuid16); }
    }
    return n;
}

bool aos_hal_ble_gatt_read(uint16_t handle)
{
    if (!gatt_ready()) return false;
    const gattr_t *a = find(handle);
    uint8_t v[64];
    if (!a) { ev_put(AOS_BLE_EV_READ, handle, 0x101, NULL, 0); return true; }
    if (a->uuid128 == U_SECRET) { ev_put(AOS_BLE_EV_READ, handle, 0x105, NULL, 0); return true; }
    if (a->kind == AOS_BLE_ATTR_CHAR && !(a->props & 0x02)) { ev_put(AOS_BLE_EV_READ, handle, 0x102, NULL, 0); return true; }
    if (a->kind == AOS_BLE_ATTR_DESC && a->uuid16 == 0x2902 && handle < 64) {
        v[0] = s_sub[handle - 1] ? 1 : 0;
        v[1] = 0;
        ev_put(AOS_BLE_EV_READ, handle, 0, v, 2);
        return true;
    }
    int n = value_of(a, v);
    ev_put(AOS_BLE_EV_READ, handle, 0, v, n);
    return true;
}

bool aos_hal_ble_gatt_write(uint16_t handle, const void *data, size_t len, bool response)
{
    if (!gatt_ready() || len > 512) return false;
    const gattr_t *a = find(handle);
    int st = 0;
    if (!a) st = 0x101;
    else if (a->uuid128 == U_SECRET) st = 0x105;
    else if (a->kind == AOS_BLE_ATTR_CHAR && !(a->props & 0x0C)) st = 0x103;
    if (!st && a->uuid128 == U_NUS_RX) {
        /* the UART answers what it is sent, shouting */
        const char *p = data;
        int n = len > 200 ? 200 : (int)len;
        s_nus_len = snprintf((char *)s_nus_tx, sizeof s_nus_tx, "eco: ");
        for (int i = 0; i < n && s_nus_len < (int)sizeof s_nus_tx - 1; i++)
            s_nus_tx[s_nus_len++] = (p[i] >= 'a' && p[i] <= 'z') ? p[i] - 32 : p[i];
        s_nus_pending = true;
    }
    if (response || st) ev_put(AOS_BLE_EV_WRITE, handle, st, NULL, 0);
    return true;
}

bool aos_hal_ble_gatt_subscribe(uint16_t value_handle, int mode)
{
    if (!gatt_ready() || value_handle >= 64) return false;
    const gattr_t *a = find(value_handle);
    uint8_t m = (uint8_t)mode;
    if (!a || !(a->props & 0x30)) { ev_put(AOS_BLE_EV_SUBSCRIBE, value_handle, 0x103, &m, 1); return true; }
    s_sub[value_handle] = mode != 0;
    ev_put(AOS_BLE_EV_SUBSCRIBE, value_handle, 0, &m, 1);
    return true;
}

int aos_hal_ble_gatt_events(aos_ble_gatt_ev_t *out, int max)
{
    gatt_run();
    int n = 0;
    while (n < max && s_ev_r != s_ev_w) out[n++] = s_ev[s_ev_r++ % EV_RING];
    return n;
}
