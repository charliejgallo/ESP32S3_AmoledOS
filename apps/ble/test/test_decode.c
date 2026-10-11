/*
 * BLE - the decoder against packets written by hand from each format's
 * specification (not from the decoder), against the simulator's own
 * neighbourhood (sim/ble_sim.c, run for real through shim/aos_hal.h), and
 * against a few hundred thousand packets of noise under ASan and UBSan.
 *
 * run.sh builds and runs it. Exit status 0 when everything held.
 */
#include "bl_decode.h"
#include "bl_crypt.h"
#include "aos_hal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;

#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, e) do { float _a = (float)(a), _b = (float)(b); \
    if (fabsf(_a - _b) <= (e)) g_pass++; \
    else { g_fail++; printf("FAIL %s:%d: %s = %g, want %g\n", __FILE__, __LINE__, #a, (double)_a, (double)_b); } } while (0)
#define STREQ(a, b) do { const char *_a = (a), *_b = (b); \
    if (_a && _b && !strcmp(_a, _b)) g_pass++; \
    else { g_fail++; printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); } } while (0)
#define HAS(hay, needle) do { const char *_h = (hay), *_n = (needle); \
    if (_h && strstr(_h, _n)) g_pass++; \
    else { g_fail++; printf("FAIL %s:%d: \"%s\" lacks \"%s\"\n", __FILE__, __LINE__, _h ? _h : "(null)", _n); } } while (0)

/* ---- packets by hand ---- */

typedef struct {
    uint8_t b[300];
    int n;
} pkt_t;

#define BYTES(...) (const uint8_t[]){ __VA_ARGS__ }, (int)sizeof((const uint8_t[]){ __VA_ARGS__ })

static void ad(pkt_t *p, uint8_t type, const uint8_t *d, int n)
{
    p->b[p->n++] = (uint8_t)(n + 1);
    p->b[p->n++] = type;
    memcpy(p->b + p->n, d, (size_t)n);
    p->n += n;
}

static void ad_str(pkt_t *p, uint8_t type, const char *s) { ad(p, type, (const uint8_t *)s, (int)strlen(s)); }

static bl_ad_t merged(const pkt_t *p)
{
    bl_ad_t a;
    bl_ad_clear(&a);
    bl_ad_merge(&a, p->b, p->n);
    return a;
}

static bl_sensor_t sensor(const pkt_t *p, bool *ok)
{
    bl_ad_t a = merged(p);
    bl_sensor_t s;
    *ok = bl_sensor_decode(&a, NULL, &s);
    return s;
}

static bool utf8_ok(const char *s, size_t cap)
{
    size_t n = strnlen(s, cap);
    if (n >= cap) return false;
    const unsigned char *u = (const unsigned char *)s;
    for (size_t i = 0; i < n;) {
        unsigned char c = u[i];
        int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!k || i + (size_t)k > n) return false;
        for (int j = 1; j < k; j++)
            if ((u[i + j] & 0xC0) != 0x80) return false;
        i += (size_t)k;
    }
    return true;
}

static int explain(const pkt_t *p, bl_line_t *l, int max, bl_tr_fn tr)
{
    int n = bl_explain(p->b, p->n, l, max, tr);
    for (int i = 0; i < n; i++) CHECK(utf8_ok(l[i].key, sizeof l[i].key) && utf8_ok(l[i].val, sizeof l[i].val));
    return n;
}

static const char *line(const bl_line_t *l, int n, const char *key)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(l[i].key, key)) return l[i].val;
    return NULL;
}

/* ---- the AD structures ---- */

static void test_merge(void)
{
    pkt_t p = { .n = 0 };
    ad(&p, 0x01, BYTES(0x06));
    ad_str(&p, 0x08, "Corto");
    ad(&p, 0x0A, BYTES(0xF4));
    ad(&p, 0x03, BYTES(0x0D, 0x18, 0x0F, 0x18, 0x0D, 0x18));
    ad(&p, 0x19, BYTES(0xC1, 0x03));
    ad(&p, 0x1A, BYTES(0x40, 0x06));
    bl_ad_t a = merged(&p);
    CHECK(a.has_flags && a.flags == 0x06);
    STREQ(a.name, "Corto");
    CHECK(!a.name_complete);
    CHECK(a.has_tx && a.tx_power == -12);
    CHECK(a.n16 == 2 && a.u16[0] == 0x180D && a.u16[1] == 0x180F);
    CHECK(a.has_appearance && a.appearance == 0x03C1);
    CHECK(a.has_interval && a.interval == 0x0640);
    CHECK(!a.malformed);

    /* the scan response: the complete name wins and stays, UUIDs add up */
    pkt_t r = { .n = 0 };
    ad_str(&r, 0x09, "Nombre completo");
    ad(&r, 0x02, BYTES(0x0F, 0x18, 0x12, 0x18));
    ad(&r, 0x05, BYTES(0x16, 0x18, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44));
    bl_ad_merge(&a, r.b, r.n);
    STREQ(a.name, "Nombre completo");
    CHECK(a.name_complete);
    CHECK(a.n16 == 4 && a.u16[2] == 0x1812 && a.u16[3] == 0x1816);
    bl_ad_merge(&a, p.b, p.n);                  /* the short name again: ignored */
    STREQ(a.name, "Nombre completo");

    /* service data and manufacturer data: replaced by key, added otherwise */
    pkt_t s1 = { .n = 0 }, s2 = { .n = 0 };
    ad(&s1, 0x16, BYTES(0xD2, 0xFC, 0x40, 0x01, 0x10));
    ad(&s1, 0xFF, BYTES(0x4C, 0x00, 0x10, 0x01, 0x00));
    ad(&s1, 0x20, BYTES(0x34, 0x12, 0x00, 0x00, 0xAA));
    ad(&s2, 0x16, BYTES(0xD2, 0xFC, 0x40, 0x01, 0x20, 0x09));
    ad(&s2, 0xFF, BYTES(0x75, 0x00, 0x01));
    ad(&s2, 0x21, BYTES(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E, 0x55));
    bl_ad_clear(&a);
    bl_ad_merge(&a, s1.b, s1.n);
    bl_ad_merge(&a, s2.b, s2.n);
    CHECK(a.nsd == 3);
    CHECK(a.sd[0].uuid == 0xFCD2 && a.sd[0].uuid_len == 2 && a.sd[0].len == 4 && a.sd[0].data[2] == 0x20);
    CHECK(a.sd[1].uuid == 0x1234 && a.sd[1].uuid_len == 4 && a.sd[1].len == 1 && a.sd[1].data[0] == 0xAA);
    CHECK(a.sd[2].uuid == 0x0001 && a.sd[2].uuid_len == 16 && a.sd[2].len == 1 && a.sd[2].data[0] == 0x55);
    CHECK(a.nmfg == 2 && a.mfg[0].company == 0x004C && a.mfg[1].company == 0x0075 && a.mfg[1].len == 1);

    /* 128-bit lists, deduplicated and capped */
    pkt_t u = { .n = 0 };
    static const uint8_t NUS[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E };
    ad(&u, 0x07, NUS, 16);
    ad(&u, 0x06, NUS, 16);
    a = merged(&u);
    CHECK(a.n128 == 1 && !memcmp(a.u128[0], NUS, 16));

    /* a length past the end, and padding */
    uint8_t bad[] = { 0x02, 0x01, 0x06, 0x09, 0x09, 'a', 'b' };
    bl_ad_clear(&a);
    bl_ad_merge(&a, bad, sizeof bad);
    CHECK(a.malformed && a.has_flags && !a.name[0]);
    uint8_t pad[] = { 0x02, 0x01, 0x06, 0x00, 0x00, 0x00, 0x05 };
    bl_ad_clear(&a);
    bl_ad_merge(&a, pad, sizeof pad);
    CHECK(!a.malformed && a.has_flags);

    /* a name cut on a whole character, and one with junk */
    pkt_t nm = { .n = 0 };
    ad_str(&nm, 0x09, "ab\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1");
    a = merged(&nm);
    CHECK(strlen(a.name) == 30 && utf8_ok(a.name, sizeof a.name));
    pkt_t junk = { .n = 0 };
    ad(&junk, 0x09, BYTES('o', 'k', 0xFF, 0x01, 0xC3));
    a = merged(&junk);
    STREQ(a.name, "ok???");
}

/* ---- explanations ---- */

static const char *tr_upper(const char *s)
{
    if (!strcmp(s, "Flags")) return "FLAGS";
    if (!strcmp(s, "sin BR/EDR")) return "no BR/EDR";
    return s;
}

static const char *tr_long(const char *s)
{
    (void)s;
    return "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1"
           "\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1\xC3\xB1"
           "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC"
           "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC";
}

static void test_explain(void)
{
    bl_line_t l[24];
    pkt_t p = { .n = 0 };
    ad(&p, 0x01, BYTES(0x06));
    ad_str(&p, 0x09, "Termo");
    ad(&p, 0x0A, BYTES(0x0C));
    ad(&p, 0x03, BYTES(0x0D, 0x18, 0x0F, 0x18));
    ad(&p, 0x19, BYTES(0x41, 0x03));
    ad(&p, 0x99, BYTES(0x01, 0x02));
    int n = explain(&p, l, 24, NULL);
    CHECK(n == 6);
    STREQ(line(l, n, "Flags"), "LE General Discoverable, sin BR/EDR (0x06)");
    STREQ(line(l, n, "Nombre"), "Termo");
    STREQ(line(l, n, "Potencia TX"), "12 dBm");
    STREQ(line(l, n, "Servicios (16 bits)"), "0x180D Heart Rate, 0x180F Battery");
    STREQ(line(l, n, "Apariencia"), "Banda de pulso (0x0341)");
    STREQ(line(l, n, "Tipo 0x99"), "01 02");
    n = explain(&p, l, 2, NULL);
    CHECK(n == 2);
    n = explain(&p, l, 24, tr_upper);
    STREQ(line(l, n, "FLAGS"), "LE General Discoverable, no BR/EDR (0x06)");

    /* translations far too long: cut, never split */
    n = explain(&p, l, 24, tr_long);
    CHECK(n == 6);
    for (int i = 0; i < n; i++) CHECK(strlen(l[i].key) < sizeof l[i].key && strlen(l[i].val) < sizeof l[i].val);

    /* flags spelled out */
    pkt_t f = { .n = 0 };
    ad(&f, 0x01, BYTES(0x1A));
    n = explain(&f, l, 24, NULL);
    STREQ(line(l, n, "Flags"), "LE General Discoverable, LE y BR/EDR a la vez (controlador), LE y BR/EDR a la vez (host) (0x1A)");
    pkt_t f2 = { .n = 0 };
    ad(&f2, 0x01, BYTES(0x05));
    n = explain(&f2, l, 24, NULL);
    STREQ(line(l, n, "Flags"), "LE Limited Discoverable, sin BR/EDR (0x05)");

    /* the rest of the types */
    pkt_t q = { .n = 0 };
    ad(&q, 0x12, BYTES(0x06, 0x00, 0x0C, 0x00));
    ad(&q, 0x1A, BYTES(0x40, 0x06));
    ad(&q, 0x1C, BYTES(0x02));
    ad(&q, 0x24, BYTES(0x17, '/', '/', 'e', '.', 'a', 'r'));
    ad(&q, 0x1B, BYTES(0x2B, 0x1A, 0x5C, 0x38, 0xC1, 0xA4, 0x00));
    n = explain(&q, l, 24, NULL);
    STREQ(line(l, n, "Intervalo de conexión"), "7,50 \xE2\x80\x93 15,00 ms");
    STREQ(line(l, n, "Intervalo de anuncios"), "1000,000 ms");
    STREQ(line(l, n, "Rol LE"), "periférico y central, prefiere periférico");
    STREQ(line(l, n, "URI"), "https://e.ar");
    STREQ(line(l, n, "Dirección LE"), "A4:C1:38:5C:1A:2B (pública)");
    pkt_t q2 = { .n = 0 };
    ad(&q2, 0x0D, BYTES(0x0C, 0x02, 0x5A));
    ad(&q2, 0x10, BYTES(0x01, 0x00, 0x4C, 0x00, 0x34, 0x12, 0x00, 0x01));
    ad_str(&q2, 0x30, "Radio");
    ad(&q2, 0x18, BYTES(0x11, 0x22, 0x33, 0x44, 0x55, 0xC6));
    n = explain(&q2, l, 24, NULL);
    STREQ(line(l, n, "Clase de dispositivo"), "0x5A020C \xC2\xB7 Teléfono");
    STREQ(line(l, n, "ID de dispositivo"), "Bluetooth SIG 0x004C (Apple, Inc.), producto 0x1234, versión 0x0100");
    STREQ(line(l, n, "Nombre de difusión"), "Radio");
    STREQ(line(l, n, "Destino"), "C6:55:44:33:22:11 (aleatoria)");

    /* a length past the end */
    uint8_t bad[] = { 0x02, 0x01, 0x06, 0x09, 0x09, 'a', 'b' };
    n = bl_explain(bad, sizeof bad, l, 24, NULL);
    CHECK(n == 2);
    HAS(line(l, n, "Error"), "09 09 61 62");

    /* manufacturer data, and a long one cut with an ellipsis */
    pkt_t m = { .n = 0 };
    uint8_t big[120];
    for (int i = 0; i < 120; i++) big[i] = (uint8_t)i;
    big[0] = 0x34;
    big[1] = 0x12;
    ad(&m, 0xFF, big, 120);
    n = explain(&m, l, 24, NULL);
    STREQ(line(l, n, "Fabricante"), "0x1234 (desconocido)");
    HAS(line(l, n, "Datos"), "\xE2\x80\xA6");
    CHECK(strlen(line(l, n, "Datos")) <= 99);
    pkt_t ap = { .n = 0 };
    ad(&ap, 0xFF, BYTES(0x4C, 0x00, 0x10, 0x05, 0x01, 0x18, 0x44, 0x9B, 0x2C, 0x0C, 0x02, 0x00, 0x01));
    n = explain(&ap, l, 24, NULL);
    STREQ(line(l, n, "Fabricante"), "Apple, Inc. (0x004C)");
    STREQ(line(l, n, "Apple"), "Nearby Info, Handoff");
}

/* ---- sensors, from their specifications ---- */

static void test_bthome(void)
{
    bool ok;
    /* v2: packet id, battery, temperature, humidity, pressure, temperature
     * 0.1, voltage, window, button, a text, CO2, then an unknown id and
     * garbage that must not be read */
    pkt_t p = { .n = 0 };
    ad(&p, 0x16, BYTES(0xD2, 0xFC, 0x40,
                       0x00, 0x05,
                       0x01, 0x61,
                       0x02, 0xCA, 0x09,
                       0x03, 0xBF, 0x13,
                       0x04, 0x13, 0x8A, 0x01,
                       0x0C, 0x02, 0x0C,
                       0x2D, 0x01,
                       0x3A, 0x04,
                       0x53, 0x03, 'a', 'b', 'c'));
    bl_sensor_t s = sensor(&p, &ok);
    CHECK(ok);
    STREQ(s.format, "BTHome v2");
    CHECK(!s.encrypted);
    CHECK(s.count == 5 && (s.mask & BL_V_COUNT));
    CHECK(s.batt == 97 && (s.mask & BL_V_BATT));
    NEAR(s.temp, 25.06f, 0.001f);
    NEAR(s.hum, 50.55f, 0.001f);
    NEAR(s.press, 1008.83f, 0.01f);
    NEAR(s.volt, 3.074f, 0.0001f);
    CHECK(s.open == 1 && (s.mask & BL_V_OPEN));
    CHECK(s.button == 4 && (s.mask & BL_V_BUTTON));
    CHECK((s.mask & (BL_V_TEMP | BL_V_HUM | BL_V_PRESS | BL_V_VOLT)) == (BL_V_TEMP | BL_V_HUM | BL_V_PRESS | BL_V_VOLT));
    /* a text, then CO2 after it, then an unknown id and garbage that must not be read */
    pkt_t p2 = { .n = 0 };
    ad(&p2, 0x16, BYTES(0xD2, 0xFC, 0x40, 0x53, 0x03, 'a', 'b', 'c', 0x12, 0xE2, 0x04, 0x9F, 0x01, 0x02, 0x13, 0x44, 0x44));
    s = sensor(&p2, &ok);
    CHECK(ok);
    NEAR(s.co2, 1250, 0);
    CHECK((s.mask & BL_V_CO2) && !(s.mask & BL_V_TVOC));
    /* the AD cut short in the middle of an object: the object is not read */
    pkt_t p3 = { .n = 0 };
    ad(&p3, 0x16, BYTES(0xD2, 0xFC, 0x40, 0x01, 0x50, 0x02, 0xCA));
    s = sensor(&p3, &ok);
    CHECK(ok && s.batt == 80 && !(s.mask & BL_V_TEMP));

    /* negative temperature at 0.1, dew point, weight in lb, count u32, dimmer */
    pkt_t q = { .n = 0 };
    ad(&q, 0x16, BYTES(0xD2, 0xFC, 0x44,
                       0x08, 0x18, 0xFC,
                       0x07, 0x10, 0x27,
                       0x3C, 0x01, 0x03,
                       0x3E, 0x01, 0x02, 0x03, 0x04,
                       0x45, 0x9C, 0xFF));
    s = sensor(&q, &ok);
    CHECK(ok);
    NEAR(s.dew, -10.0f, 0.001f);
    NEAR(s.weight, 100.0f * 0.45359237f, 0.01f);
    CHECK(s.count == 0x04030201);
    NEAR(s.temp, -10.0f, 0.001f);

    /* encrypted */
    pkt_t e = { .n = 0 };
    ad(&e, 0x16, BYTES(0xD2, 0xFC, 0x41, 0xA4, 0x72, 0x66, 0xC9, 0x5F, 0x73, 0x00, 0x11, 0x22, 0x33, 0x78, 0x23, 0x72, 0x14));
    s = sensor(&e, &ok);
    CHECK(ok && s.encrypted && !(s.mask & BL_V_TEMP));
    STREQ(s.format, "BTHome v2");

    /* not v2: the version bits say 1 */
    pkt_t w = { .n = 0 };
    ad(&w, 0x16, BYTES(0xD2, 0xFC, 0x20, 0x02, 0xCA, 0x09));
    s = sensor(&w, &ok);
    CHECK(!ok);

    /* v1, unencrypted: length+format then id, as in its documentation */
    pkt_t v1 = { .n = 0 };
    ad(&v1, 0x16, BYTES(0x1C, 0x18, 0x23, 0x02, 0xC4, 0x09, 0x03, 0x03, 0xBF, 0x13, 0x02, 0x01, 0x61));
    s = sensor(&v1, &ok);
    CHECK(ok);
    STREQ(s.format, "BTHome v1");
    NEAR(s.temp, 25.0f, 0.001f);
    NEAR(s.hum, 50.55f, 0.001f);
    CHECK(s.batt == 97);
    pkt_t v1n = { .n = 0 };
    ad(&v1n, 0x16, BYTES(0x1C, 0x18, 0x23, 0x02, 0x18, 0xFC));
    s = sensor(&v1n, &ok);
    NEAR(s.temp, -10.0f, 0.001f);
    pkt_t v1e = { .n = 0 };
    ad(&v1e, 0x16, BYTES(0x1E, 0x18, 0xFB, 0xA4, 0x5E, 0x4D, 0x00, 0x11, 0x22, 0x33));
    s = sensor(&v1e, &ok);
    CHECK(ok && s.encrypted);

    bl_line_t l[24];
    int n = explain(&p, l, 24, NULL);
    const char *v = line(l, n, "Datos de servicio 0xFCD2");
    HAS(v, "BTHome");
    HAS(v, "25,06 \xC2\xB0" "C");
    HAS(v, "50,");
    HAS(v, "abierto");
    HAS(v, "bot\xC3\xB3n largo");
}

static void test_thermometers(void)
{
    bool ok;
    bl_sensor_t s;
    /* pvvx: MAC 2B 1A 5C 38 C1 A4, 23.45 C, 51.20 %, 2950 mV, 87 %, 12, flags 4 */
    pkt_t p = { .n = 0 };
    ad(&p, 0x16, BYTES(0x1A, 0x18, 0x2B, 0x1A, 0x5C, 0x38, 0xC1, 0xA4, 0x29, 0x09, 0x00, 0x14, 0x86, 0x0B, 87, 12, 0x04));
    s = sensor(&p, &ok);
    CHECK(ok);
    STREQ(s.format, "pvvx");
    NEAR(s.temp, 23.45f, 0.001f);
    NEAR(s.hum, 51.20f, 0.001f);
    NEAR(s.volt, 2.950f, 0.0001f);
    CHECK(s.batt == 87 && s.count == 12);

    /* ATC1441: MAC A4 C1 38 5C 1A 2B, -5.3 C (big endian), 47 %, 91 %, 3010 mV, 200 */
    pkt_t a = { .n = 0 };
    ad(&a, 0x16, BYTES(0x1A, 0x18, 0xA4, 0xC1, 0x38, 0x5C, 0x1A, 0x2B, 0xFF, 0xCB, 47, 91, 0x0B, 0xC2, 200));
    s = sensor(&a, &ok);
    CHECK(ok);
    STREQ(s.format, "ATC");
    NEAR(s.temp, -5.3f, 0.001f);
    NEAR(s.hum, 47, 0);
    CHECK(s.batt == 91 && s.count == 200);
    NEAR(s.volt, 3.010f, 0.0001f);

    /* MiBeacon: frame control 0x2050 (MAC, object, v2), product 0x01AA,
     * counter, MAC reversed, then one object each time */
#define MI(...) do { pkt_t m = { .n = 0 }; ad(&m, 0x16, BYTES(0x95, 0xFE, 0x50, 0x20, 0xAA, 0x01, 0x07, \
                     0x6A, 0x77, 0x10, 0x34, 0x2D, 0x58, __VA_ARGS__)); s = sensor(&m, &ok); CHECK(ok); } while (0)
    MI(0x04, 0x10, 0x02, 0xD4, 0x00);
    NEAR(s.temp, 21.2f, 0.001f);
    MI(0x06, 0x10, 0x02, 0x08, 0x02);
    NEAR(s.hum, 52.0f, 0.001f);
    MI(0x0A, 0x10, 0x01, 0x5D);
    CHECK(s.batt == 93);
    MI(0x0D, 0x10, 0x04, 0xFE, 0xFF, 0xE8, 0x01);
    NEAR(s.temp, -0.2f, 0.001f);
    NEAR(s.hum, 48.8f, 0.001f);
    MI(0x07, 0x10, 0x03, 0x10, 0x27, 0x00);
    NEAR(s.lux, 10000, 0);
    MI(0x08, 0x10, 0x01, 0x21);
    NEAR(s.moist, 33, 0);
    MI(0x09, 0x10, 0x02, 0x5E, 0x01);
    NEAR(s.cond, 350, 0);
    MI(0x19, 0x10, 0x01, 0x00);
    CHECK((s.mask & BL_V_OPEN) && s.open == 1);
    MI(0x19, 0x10, 0x01, 0x01);
    CHECK((s.mask & BL_V_OPEN) && s.open == 0);
    STREQ(s.format, "MiBeacon");
#undef MI
    /* with a capability byte (0x0070) before the object */
    pkt_t mc = { .n = 0 };
    ad(&mc, 0x16, BYTES(0x95, 0xFE, 0x70, 0x20, 0xAA, 0x01, 0x07, 0x6A, 0x77, 0x10, 0x34, 0x2D, 0x58, 0x08,
                        0x0A, 0x10, 0x01, 0x40));
    s = sensor(&mc, &ok);
    CHECK(ok && s.batt == 64);
    /* encrypted (0x5858, a v5 frame) */
    pkt_t me = { .n = 0 };
    ad(&me, 0x16, BYTES(0x95, 0xFE, 0x58, 0x58, 0x5B, 0x05, 0x3A, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77));
    s = sensor(&me, &ok);
    CHECK(ok && s.encrypted && !(s.mask & BL_V_TEMP));

    /* Govee H5075: 00, packed, battery, 00. -5.3 C 45.6 %: 53456 | sign */
    pkt_t g = { .n = 0 };
    ad_str(&g, 0x09, "GVH5075_4F21");
    ad(&g, 0xFF, BYTES(0x88, 0xEC, 0x00, 0x80, 0xD0, 0xD0, 76, 0x00));
    s = sensor(&g, &ok);
    CHECK(ok);
    STREQ(s.format, "Govee");
    NEAR(s.temp, -5.3f, 0.001f);
    NEAR(s.hum, 45.6f, 0.001f);
    CHECK(s.batt == 76);
    /* 23.4 C 56.7 %: 234567 = 0x039447 */
    pkt_t g2 = { .n = 0 };
    ad(&g2, 0xFF, BYTES(0x88, 0xEC, 0x00, 0x03, 0x94, 0x47, 100, 0x00));
    s = sensor(&g2, &ok);
    NEAR(s.temp, 23.4f, 0.001f);
    NEAR(s.hum, 56.7f, 0.001f);
    /* H5074: 00, temp LE 0.01, hum LE 0.01, battery, 02 */
    pkt_t g3 = { .n = 0 };
    ad_str(&g3, 0x09, "Govee_H5074_ABCD");
    ad(&g3, 0xFF, BYTES(0x88, 0xEC, 0x00, 0xE6, 0x09, 0xA9, 0x11, 88, 0x02));
    s = sensor(&g3, &ok);
    CHECK(ok);
    NEAR(s.temp, 25.34f, 0.001f);
    NEAR(s.hum, 45.21f, 0.001f);
    CHECK(s.batt == 88);
    /* H5102 under Nokia's id: 01 01, packed, battery */
    pkt_t g4 = { .n = 0 };
    ad_str(&g4, 0x09, "GVH5102_1A2B");
    ad(&g4, 0xFF, BYTES(0x01, 0x00, 0x01, 0x01, 0x03, 0x94, 0x47, 64));
    s = sensor(&g4, &ok);
    CHECK(ok);
    NEAR(s.temp, 23.4f, 0.001f);
    CHECK(s.batt == 64);
    pkt_t g5 = { .n = 0 };                      /* the same bytes from someone else: nothing */
    ad(&g5, 0xFF, BYTES(0x01, 0x00, 0x01, 0x01, 0x03, 0x94, 0x47, 64));
    s = sensor(&g5, &ok);
    CHECK(!ok);

    /* Ruuvi RAWv2: the test vector of Ruuvi's own documentation */
    pkt_t r = { .n = 0 };
    ad(&r, 0xFF, BYTES(0x99, 0x04, 0x05, 0x12, 0xFC, 0x53, 0x94, 0xC3, 0x7C, 0x00, 0x04, 0xFF, 0xFC, 0x04, 0x0C, 0xAC,
                       0x36, 0x42, 0x00, 0xCD, 0xCB, 0xB8, 0x33, 0x4C, 0x88, 0x4F));
    s = sensor(&r, &ok);
    CHECK(ok);
    STREQ(s.format, "Ruuvi RAWv2");
    NEAR(s.temp, 24.3f, 0.001f);
    NEAR(s.hum, 53.49f, 0.001f);
    NEAR(s.press, 1000.44f, 0.01f);
    NEAR(s.acc_x, 0.004f, 0.0001f);
    NEAR(s.acc_y, -0.004f, 0.0001f);
    NEAR(s.acc_z, 1.036f, 0.0001f);
    NEAR(s.volt, 2.977f, 0.0001f);
    CHECK(s.count == 205);
    /* its "invalid values" vector: nothing but the format */
    pkt_t ri = { .n = 0 };
    ad(&ri, 0xFF, BYTES(0x99, 0x04, 0x05, 0x80, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0xFF,
                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF));
    s = sensor(&ri, &ok);
    CHECK(ok && !(s.mask & (BL_V_TEMP | BL_V_HUM | BL_V_PRESS | BL_V_ACC | BL_V_VOLT | BL_V_COUNT)));
    /* RAWv1, the documentation's vector */
    pkt_t r1 = { .n = 0 };
    ad(&r1, 0xFF, BYTES(0x99, 0x04, 0x03, 0x29, 0x1A, 0x1E, 0xCE, 0x1E, 0xFC, 0x18, 0xF9, 0x42, 0x02, 0xCA, 0x0B, 0x53));
    s = sensor(&r1, &ok);
    CHECK(ok);
    STREQ(s.format, "Ruuvi RAWv1");
    NEAR(s.hum, 20.5f, 0.001f);
    NEAR(s.temp, 26.3f, 0.001f);
    NEAR(s.press, 1027.66f, 0.01f);
    NEAR(s.acc_x, -1.0f, 0.0001f);
    NEAR(s.acc_y, -1.726f, 0.0001f);
    NEAR(s.acc_z, 0.714f, 0.0001f);
    NEAR(s.volt, 2.899f, 0.0001f);

    /* SwitchBot Meter: 'T', status, battery, tenths, degrees with the sign bit, humidity */
    pkt_t sb = { .n = 0 };
    ad(&sb, 0x16, BYTES(0x3D, 0xFD, 0x54, 0x00, 0x64, 0x05, 0x99, 0x2D));
    s = sensor(&sb, &ok);
    CHECK(ok);
    STREQ(s.format, "SwitchBot");
    NEAR(s.temp, 25.5f, 0.001f);
    NEAR(s.hum, 45, 0);
    CHECK(s.batt == 100);
    pkt_t sbn = { .n = 0 };
    ad(&sbn, 0x16, BYTES(0x00, 0x0D, 0x69, 0x00, 0x32, 0x03, 0x04, 0xC1));
    s = sensor(&sbn, &ok);
    CHECK(ok);
    NEAR(s.temp, -4.3f, 0.001f);
    NEAR(s.hum, 65, 0);
    CHECK(s.batt == 50);
    /* outdoor 'w': battery in the service data, the reading after the MAC in 0x0969's */
    pkt_t sbo = { .n = 0 };
    ad(&sbo, 0x16, BYTES(0x3D, 0xFD, 0x77, 0x00, 0x5A));
    ad(&sbo, 0xFF, BYTES(0x69, 0x09, 0xC9, 0x21, 0x7E, 0x0A, 0x3D, 0x91, 0x0E, 0x00, 0x03, 0x8A, 0x3C));
    s = sensor(&sbo, &ok);
    CHECK(ok);
    NEAR(s.temp, 10.3f, 0.001f);
    NEAR(s.hum, 60, 0);
    CHECK(s.batt == 90);

    /* Qingping: flags, type, MAC, then 01 temp+hum, 02 battery, 07 pressure, 12 PM, 13 CO2 */
    pkt_t qp = { .n = 0 };
    ad(&qp, 0x16, BYTES(0xCD, 0xFD, 0x88, 0x01, 0x08, 0x2C, 0x54, 0x34, 0x2D, 0x58,
                        0x01, 0x04, 0xF5, 0x00, 0x90, 0x01,
                        0x02, 0x01, 0x64,
                        0x07, 0x02, 0xA6, 0x27));
    s = sensor(&qp, &ok);
    CHECK(ok);
    STREQ(s.format, "Qingping");
    NEAR(s.temp, 24.5f, 0.001f);
    NEAR(s.hum, 40.0f, 0.001f);
    CHECK(s.batt == 100);
    NEAR(s.press, 1015.0f, 0.01f);
    pkt_t qa = { .n = 0 };                      /* an air monitor's: PM2.5/PM10 and CO2 */
    ad(&qa, 0x16, BYTES(0xCD, 0xFD, 0x88, 0x0E, 0x08, 0x2C, 0x54, 0x34, 0x2D, 0x58,
                        0x12, 0x04, 0x0C, 0x00, 0x0F, 0x00,
                        0x13, 0x02, 0x20, 0x03));
    s = sensor(&qa, &ok);
    CHECK(ok);
    NEAR(s.pm25, 12, 0);
    NEAR(s.co2, 800, 0);

    /* Inkbird IBS-TH2: name "sps"; the temperature where the company goes */
    pkt_t ib = { .n = 0 };
    ad_str(&ib, 0x09, "sps");
    ad(&ib, 0xFF, BYTES(0x10, 0x09, 0x88, 0x13, 0x00, 0xAB, 0xCD, 87, 0x08));
    s = sensor(&ib, &ok);
    CHECK(ok);
    STREQ(s.format, "Inkbird");
    NEAR(s.temp, 23.20f, 0.001f);
    NEAR(s.hum, 50.00f, 0.001f);
    CHECK(s.batt == 87);
    pkt_t ib2 = { .n = 0 };
    ad_str(&ib2, 0x09, "tps");
    ad(&ib2, 0xFF, BYTES(0xE4, 0xF8, 0x00, 0x00, 0x01, 0xAB, 0xCD, 60, 0x08));
    s = sensor(&ib2, &ok);
    CHECK(ok);
    NEAR(s.temp, -18.20f, 0.001f);
    CHECK(!(s.mask & BL_V_HUM) && s.batt == 60);

    /* Eddystone TLM: version 0, 3000 mV, 25.5 C, 1024 adverts, 1000 s */
    pkt_t t = { .n = 0 };
    ad(&t, 0x16, BYTES(0xAA, 0xFE, 0x20, 0x00, 0x0B, 0xB8, 0x19, 0x80, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x27, 0x10));
    s = sensor(&t, &ok);
    CHECK(ok);
    STREQ(s.format, "Eddystone TLM");
    NEAR(s.volt, 3.0f, 0.0001f);
    NEAR(s.temp, 25.5f, 0.001f);
    CHECK(s.count == 1024);
    pkt_t t2 = { .n = 0 };
    ad(&t2, 0x16, BYTES(0xAA, 0xFE, 0x20, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01));
    s = sensor(&t2, &ok);
    CHECK(ok && !(s.mask & (BL_V_VOLT | BL_V_TEMP)));

    /* Victron: only that it is there, encrypted */
    pkt_t vi = { .n = 0 };
    ad(&vi, 0xFF, BYTES(0xE1, 0x02, 0x10, 0x00, 0xA0, 0x01, 0x02, 0x11, 0x22, 0x33));
    s = sensor(&vi, &ok);
    CHECK(ok && s.encrypted);
    STREQ(s.format, "Victron");

    /* the explanation names the format and its readings */
    bl_line_t l[24];
    int n = explain(&r, l, 24, NULL);
    STREQ(line(l, n, "Fabricante"), "Ruuvi Innovations Ltd. (0x0499)");
    HAS(line(l, n, "Ruuvi RAWv2"), "24,30 \xC2\xB0" "C");
    HAS(line(l, n, "Ruuvi RAWv2"), "1000,4 hPa");
    n = explain(&qp, l, 24, NULL);
    HAS(line(l, n, "Datos de servicio 0xFDCD"), "Qingping");
    HAS(line(l, n, "Datos de servicio 0xFDCD"), "1015,0 hPa");
    n = explain(&qa, l, 24, NULL);
    HAS(line(l, n, "Datos de servicio 0xFDCD"), "CO2 800 ppm");

    /* Inkbird again, as the simulator sends it: name in the response, and a
     * new "company" in every advertisement; the newest must win */
    bl_ad_t ia;
    bl_ad_clear(&ia);
    pkt_t rsp = { .n = 0 };
    ad_str(&rsp, 0x09, "sps");
    bl_ad_merge(&ia, rsp.b, rsp.n);
    for (int k = 0; k < 5; k++) {
        pkt_t adv = { .n = 0 };
        int16_t tc = (int16_t)(-1820 + k * 7);
        uint8_t m[9] = { (uint8_t)tc, (uint8_t)((uint16_t)tc >> 8), 0, 0, 0, 0x34, 0x12, 71, 0x08 };
        ad(&adv, 0x01, BYTES(0x06));
        ad(&adv, 0xFF, m, 9);
        bl_ad_merge(&ia, adv.b, adv.n);
        CHECK(bl_sensor_decode(&ia, NULL, &s));
        NEAR(s.temp, tc * 0.01f, 0.0001f);
    }
    CHECK(ia.nmfg == 1);
    /* and back to an earlier temperature */
    pkt_t back = { .n = 0 };
    ad(&back, 0xFF, BYTES(0xE4, 0xF8, 0, 0, 0, 0x34, 0x12, 71, 0x08));
    bl_ad_merge(&ia, back.b, back.n);
    CHECK(bl_sensor_decode(&ia, NULL, &s));
    NEAR(s.temp, -18.20f, 0.0001f);
}

/* ---- beacons ---- */

static void test_beacons(void)
{
    bl_beacon_t b;
    char u[48];
    bool ok;
    /* iBeacon: 4C 00, 02 15, UUID, major, minor, power at 1 m */
    pkt_t ib = { .n = 0 };
    ad(&ib, 0x01, BYTES(0x06));
    ad(&ib, 0xFF, BYTES(0x4C, 0x00, 0x02, 0x15, 0xFD, 0xA5, 0x06, 0x93, 0xA4, 0xE2, 0x4F, 0xB1, 0xAF, 0xCF, 0xC6, 0xEB,
                        0x07, 0x64, 0x78, 0x25, 0x00, 0x1B, 0x01, 0x02, 0xC3));
    bl_ad_t a = merged(&ib);
    CHECK(bl_beacon_decode(&a, &b));
    CHECK(b.kind == BL_BEACON_IBEACON && b.major == 27 && b.minor == 258 && b.tx1m == -61);
    bl_uuid_str(b.uuid, 16, u, sizeof u);
    STREQ(u, "FDA50693-A4E2-4FB1-AFCF-C6EB07647825");
    bl_sensor_t s = sensor(&ib, &ok);
    CHECK(ok && (s.mask & BL_V_RSSI1M) && s.rssi1m == -61);
    STREQ(s.format, "iBeacon");
    CHECK(bl_p1m_guess(&a, &b) == -61);
    bl_line_t l[24];
    int n = explain(&ib, l, 24, NULL);
    STREQ(line(l, n, "iBeacon"), "FDA50693-A4E2-4FB1-AFCF-C6EB07647825 mayor 27 menor 258 \xC2\xB7 -61 dBm a 1 m");
    const char *t[4];
    CHECK(bl_apple_types(&a.mfg[0], t, 4) == 1);
    STREQ(t[0], "iBeacon");
    const char *lab;
    CHECK(bl_classify(&a, &lab) == BL_CLS_BEACON);

    /* AltBeacon under Radius Networks' id */
    pkt_t al = { .n = 0 };
    ad(&al, 0xFF, BYTES(0x18, 0x01, 0xBE, 0xAC, 0x2F, 0x23, 0x44, 0x54, 0xCF, 0x6D, 0x4A, 0x0F, 0xAD, 0xF2, 0xF4, 0x91,
                        0x1B, 0xA9, 0xFF, 0xA6, 0x00, 0x01, 0x00, 0x02, 0xC5, 0x00));
    a = merged(&al);
    CHECK(bl_beacon_decode(&a, &b));
    CHECK(b.kind == BL_BEACON_ALT && b.major == 1 && b.minor == 2 && b.tx1m == -59);
    bl_uuid_str(b.uuid, 16, u, sizeof u);
    STREQ(u, "2F234454-CF6D-4A0F-ADF2-F4911BA9FFA6");
    CHECK(bl_classify(&a, &lab) == BL_CLS_BEACON);
    STREQ(lab, "AltBeacon");

    /* Eddystone UID, with its two RFU bytes */
    pkt_t ui = { .n = 0 };
    ad(&ui, 0x03, BYTES(0xAA, 0xFE));
    ad(&ui, 0x16, BYTES(0xAA, 0xFE, 0x00, 0xE7, 0x8B, 0x0C, 0xA7, 0x50, 0xE5, 0xA4, 0x26, 0x8E, 0x40, 0x56, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x01, 0x00, 0x00));
    a = merged(&ui);
    CHECK(bl_beacon_decode(&a, &b));
    CHECK(b.kind == BL_BEACON_EDDY_UID && b.tx1m == -25 && b.uuid[0] == 0x8B && b.uuid[15] == 0x01);
    CHECK(bl_p1m_guess(&a, &b) == -66);
    s = sensor(&ui, &ok);
    CHECK(ok && s.rssi1m == -66);
    n = explain(&ui, l, 24, NULL);
    STREQ(line(l, n, "Eddystone UID"), "8B 0C A7 50 E5 A4 26 8E 40 56 / 00 00 00 00 00 01 \xC2\xB7 -25 dBm a 0 m");

    /* Eddystone URL: every scheme, and the expansions */
    struct { uint8_t f[20]; int n; const char *want; } U[] = {
        { { 0x10, 0xEB, 0x00, 'r', 'u', 'u', 'v', 'i', 0x07 }, 9, "http://www.ruuvi.com" },
        { { 0x10, 0xEB, 0x01, 'a', 0x08 }, 5, "https://www.a.org" },
        { { 0x10, 0xEB, 0x02, 'a', 0x00, 'b' }, 6, "http://a.com/b" },
        { { 0x10, 0xEB, 0x03, 'x', 0x04, 'y', 0x0D }, 7, "https://x.info/y.gov" },
        { { 0x10, 0xEB, 0x03, 'x', 0x0A, 0x0B, 0x0C, 0x09 }, 8, "https://x.net.info.biz.edu" },
    };
    for (int i = 0; i < (int)(sizeof U / sizeof U[0]); i++) {
        pkt_t p = { .n = 0 };
        uint8_t sd[24] = { 0xAA, 0xFE };
        memcpy(sd + 2, U[i].f, (size_t)U[i].n);
        ad(&p, 0x16, sd, U[i].n + 2);
        a = merged(&p);
        CHECK(bl_beacon_decode(&a, &b) && b.kind == BL_BEACON_EDDY_URL && b.tx1m == -21);
        STREQ(b.url, U[i].want);
    }
    pkt_t ur = { .n = 0 };
    ad(&ur, 0x16, BYTES(0xAA, 0xFE, 0x10, 0xEB, 0x03, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x07));
    n = explain(&ur, l, 24, NULL);
    STREQ(line(l, n, "Eddystone URL"), "https://example.com \xC2\xB7 -21 dBm a 0 m");
    HAS(line(l, n, "Datos de servicio 0xFEAA"), "Eddystone");

    /* EID */
    pkt_t ei = { .n = 0 };
    ad(&ei, 0x16, BYTES(0xAA, 0xFE, 0x30, 0xF0, 1, 2, 3, 4, 5, 6, 7, 8));
    a = merged(&ei);
    CHECK(bl_beacon_decode(&a, &b) && b.kind == BL_BEACON_EDDY_EID && b.tx1m == -16 && b.uuid[7] == 8);
}

/* ---- Apple, Microsoft and the rest of the classification ---- */

static void test_classify(void)
{
    const char *lab;
    bl_line_t l[24];
    int n;
    /* AirPods Pro 2: model 0x1420, status 0x55 (left primary flipped), buds 9/8, case unknown, left charging */
    pkt_t pods = { .n = 0 };
    ad(&pods, 0xFF, BYTES(0x4C, 0x00, 0x07, 0x19, 0x01, 0x14, 0x20, 0x55, 0x98, 0x2F, 0x01, 0x00, 0x05,
                          1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16));
    bl_ad_t a = merged(&pods);
    CHECK(bl_classify(&a, &lab) == BL_CLS_AUDIO);
    STREQ(lab, "AirPods Pro 2");
    n = explain(&pods, l, 24, NULL);
    STREQ(line(l, n, "AirPods"), "AirPods Pro 2 \xC2\xB7 izq. 90 % (cargando), der. 80 %, estuche -");
    /* the other bud primary (0x20 set): the nibbles swap, and so do the charge bits */
    pkt_t pods2 = { .n = 0 };
    ad(&pods2, 0xFF, BYTES(0x4C, 0x00, 0x07, 0x19, 0x01, 0x0E, 0x20, 0x75, 0x98, 0x55, 0x01, 0x00));
    n = explain(&pods2, l, 24, NULL);
    STREQ(line(l, n, "AirPods"), "AirPods Pro \xC2\xB7 izq. 80 % (cargando), der. 90 %, estuche 50 % (cargando)");

    /* Find My, separated: AirTag (status 0x10), full key */
    uint8_t fm[29] = { 0x4C, 0x00, 0x12, 0x19, 0x10 };
    pkt_t f = { .n = 0 };
    ad(&f, 0xFF, fm, 29);
    a = merged(&f);
    CHECK(bl_classify(&a, &lab) == BL_CLS_TRACKER);
    STREQ(lab, "AirTag (lejos del dueño)");
    n = explain(&f, l, 24, NULL);
    STREQ(line(l, n, "Find My"), "AirTag \xC2\xB7 lejos del dueño \xC2\xB7 batería llena");
    pkt_t f2 = { .n = 0 };
    ad(&f2, 0xFF, BYTES(0x4C, 0x00, 0x12, 0x02, 0xA0, 0x01));
    n = explain(&f2, l, 24, NULL);
    STREQ(line(l, n, "Find My"), "Accesorio Find My \xC2\xB7 cerca del dueño \xC2\xB7 batería baja");

    /* Nearby Info, the long one: an iPhone */
    pkt_t ip = { .n = 0 };
    ad(&ip, 0xFF, BYTES(0x4C, 0x00, 0x10, 0x07, 0x3B, 0x1C, 0x9A, 0x51, 0x2E, 0x77, 0x18));
    a = merged(&ip);
    CHECK(bl_classify(&a, &lab) == BL_CLS_PHONE);
    STREQ(lab, "iPhone");
    /* Handoff and a short Nearby Info: a Mac */
    pkt_t mac = { .n = 0 };
    ad(&mac, 0xFF, BYTES(0x4C, 0x00, 0x0C, 0x0E, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0x10, 0x05, 0x01, 0x18, 0x44, 0x9B, 0x2C));
    a = merged(&mac);
    CHECK(bl_classify(&a, &lab) == BL_CLS_COMPUTER);
    STREQ(lab, "Mac");
    const char *ty[8];
    CHECK(bl_apple_types(&a.mfg[0], ty, 8) == 2);
    STREQ(ty[0], "Handoff");
    STREQ(ty[1], "Nearby Info");
    pkt_t ws = { .n = 0 };
    ad(&ws, 0xFF, BYTES(0x4C, 0x00, 0x0B, 0x03, 1, 2, 3));
    a = merged(&ws);
    CHECK(bl_classify(&a, &lab) == BL_CLS_WATCH);
    pkt_t apt = { .n = 0 };
    ad(&apt, 0xFF, BYTES(0x4C, 0x00, 0x09, 0x06, 0x03, 0x10, 0xC0, 0xA8, 0x01, 0x23));
    a = merged(&apt);
    CHECK(bl_classify(&a, &lab) == BL_CLS_TV);

    /* Microsoft: Swift Pair for a mouse, CDP from a Windows desktop */
    pkt_t sp = { .n = 0 };
    ad(&sp, 0xFF, BYTES(0x06, 0x00, 0x03, 0x00, 0x80));
    ad(&sp, 0x19, BYTES(0xC2, 0x03));
    a = merged(&sp);
    CHECK(bl_classify(&a, &lab) == BL_CLS_HID);
    STREQ(lab, "Mouse (Swift Pair)");
    n = explain(&sp, l, 24, NULL);
    STREQ(line(l, n, "Microsoft"), "Swift Pair");
    pkt_t cdp = { .n = 0 };
    ad(&cdp, 0xFF, BYTES(0x06, 0x00, 0x01, 0x09, 0x20, 0x02, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20));
    a = merged(&cdp);
    CHECK(bl_classify(&a, &lab) == BL_CLS_COMPUTER);
    STREQ(lab, "PC con Windows");

    /* by appearance */
    static const struct { uint16_t ap; bl_class_t c; } AP[] = {
        { 0x0941, BL_CLS_AUDIO }, { 0x0843, BL_CLS_AUDIO }, { 0x03C1, BL_CLS_HID }, { 0x03C4, BL_CLS_GAMEPAD },
        { 0x0A01, BL_CLS_TV }, { 0x0597, BL_CLS_LIGHT }, { 0x07C2, BL_CLS_LIGHT }, { 0x0485, BL_CLS_FITNESS },
        { 0x0C80, BL_CLS_HEALTH }, { 0x0087, BL_CLS_TABLET }, { 0x0083, BL_CLS_COMPUTER }, { 0x00C2, BL_CLS_WATCH },
        { 0x0040, BL_CLS_PHONE }, { 0x0200, BL_CLS_TRACKER }, { 0x0543, BL_CLS_SENSOR }, { 0x0341, BL_CLS_HEALTH },
    };
    for (int i = 0; i < (int)(sizeof AP / sizeof AP[0]); i++) {
        pkt_t p = { .n = 0 };
        uint8_t v[2] = { (uint8_t)AP[i].ap, (uint8_t)(AP[i].ap >> 8) };
        ad(&p, 0x19, v, 2);
        a = merged(&p);
        bl_class_t c = bl_classify(&a, &lab);
        if (c != AP[i].c) printf("  appearance 0x%04X -> %d\n", AP[i].ap, c);
        CHECK(c == AP[i].c);
        CHECK(lab && !strcmp(lab, bl_appearance_name(AP[i].ap)));
    }

    /* by services */
    static const struct { uint16_t u; bl_class_t c; } SV[] = {
        { 0x180D, BL_CLS_HEALTH }, { 0x1812, BL_CLS_HID }, { 0x1816, BL_CLS_FITNESS }, { 0x1826, BL_CLS_FITNESS },
        { 0xFEED, BL_CLS_TRACKER }, { 0xFD5A, BL_CLS_TRACKER }, { 0x184E, BL_CLS_AUDIO }, { 0x181D, BL_CLS_HEALTH },
    };
    for (int i = 0; i < (int)(sizeof SV / sizeof SV[0]); i++) {
        pkt_t p = { .n = 0 };
        uint8_t v[2] = { (uint8_t)SV[i].u, (uint8_t)(SV[i].u >> 8) };
        ad(&p, 0x03, v, 2);
        a = merged(&p);
        CHECK(bl_classify(&a, &lab) == SV[i].c);
    }
    pkt_t nus = { .n = 0 };
    ad(&nus, 0x07, BYTES(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E));
    a = merged(&nus);
    CHECK(bl_classify(&a, &lab) == BL_CLS_DEVBOARD);
    pkt_t fp = { .n = 0 };
    ad(&fp, 0x16, BYTES(0x2C, 0xFE, 0x0A, 0x5C, 0x7D));
    a = merged(&fp);
    CHECK(bl_classify(&a, &lab) == BL_CLS_AUDIO);
    n = explain(&fp, l, 24, NULL);
    STREQ(line(l, n, "Datos de servicio 0xFE2C"), "Google Fast Pair \xC2\xB7 modelo 0x0A5C7D");

    /* by name alone */
    static const struct { const char *name; bl_class_t c; } NM[] = {
        { "[TV] Samsung 7 Series (55)", BL_CLS_TV }, { "LE-Bose QC35", BL_CLS_AUDIO }, { "WH-1000XM4", BL_CLS_AUDIO },
        { "Galaxy Buds2 (A1B2)", BL_CLS_AUDIO }, { "Galaxy Watch5 (ABCD)", BL_CLS_WATCH },
        { "Xbox Wireless Controller", BL_CLS_GAMEPAD }, { "MX Keys", BL_CLS_HID }, { "ATC_1A2B3C", BL_CLS_SENSOR },
        { "ESP32-C3", BL_CLS_DEVBOARD }, { "iPhone de Carlos", BL_CLS_PHONE }, { "ihoment_H6159_ABCD", BL_CLS_LIGHT },
        { "JBL Charge 5", BL_CLS_AUDIO }, { "nada en particular", BL_CLS_UNKNOWN },
    };
    for (int i = 0; i < (int)(sizeof NM / sizeof NM[0]); i++) {
        pkt_t p = { .n = 0 };
        ad_str(&p, 0x09, NM[i].name);
        a = merged(&p);
        bl_class_t c = bl_classify(&a, &lab);
        if (c != NM[i].c) printf("  name \"%s\" -> %d\n", NM[i].name, c);
        CHECK(c == NM[i].c);
    }
    bl_ad_clear(&a);
    CHECK(bl_classify(&a, NULL) == BL_CLS_UNKNOWN);
    CHECK(bl_classify(NULL, &lab) == BL_CLS_UNKNOWN && lab == NULL);
    STREQ(bl_class_name(BL_CLS_PHONE), "Teléfono");
    STREQ(bl_class_name(BL_CLS_DEVBOARD), "Placa de desarrollo");
    STREQ(bl_class_name(BL_CLS_UNKNOWN), "Desconocido");
    STREQ(bl_class_name((bl_class_t)99), "Desconocido");
}

/* ---- names, addresses, GATT values, text, distance ---- */

static void test_names(void)
{
    STREQ(bl_company_name(0x004C), "Apple, Inc.");
    STREQ(bl_company_name(0x0006), "Microsoft");
    STREQ(bl_company_name(0x0075), "Samsung Electronics Co. Ltd.");
    STREQ(bl_company_name(0x00E0), "Google");
    STREQ(bl_company_name(0x02E5), "Espressif Systems");
    STREQ(bl_company_name(0x0499), "Ruuvi Innovations Ltd.");
    STREQ(bl_company_name(0xEC88), "Govee");
    CHECK(bl_company_name(0x1234) == NULL);
    STREQ(bl_uuid16_name(0x180D), "Heart Rate");
    STREQ(bl_uuid16_name(0x2A37), "Heart Rate Measurement");
    STREQ(bl_uuid16_name(0x2902), "Client Characteristic Configuration");
    STREQ(bl_uuid16_name(0xFCD2), "BTHome");
    STREQ(bl_uuid16_name(0xFE95), "Xiaomi (MiBeacon)");
    CHECK(bl_uuid16_name(0x1817) == NULL);
    static const uint8_t NUS_TX[16] = { 0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E };
    STREQ(bl_uuid128_name(NUS_TX), "Nordic UART TX");
    static const uint8_t ANCS[16] = { 0xD0, 0x00, 0x2D, 0x12, 0x1E, 0x4B, 0x0F, 0xA4, 0x99, 0x4E, 0xCE, 0xB5, 0x31, 0xF4, 0x05, 0x79 };
    STREQ(bl_uuid128_name(ANCS), "Apple ANCS");
    STREQ(bl_appearance_name(0x03C2), "Mouse");
    STREQ(bl_appearance_name(0x03FF), "Dispositivo HID");
    STREQ(bl_appearance_name(0x0941), "Auriculares in-ear");
    STREQ(bl_appearance_name(0x0C41), "Oxímetro de dedo");
    STREQ(bl_appearance_name(0x1442), "Pantalla de ubicación y navegación");
    CHECK(bl_appearance_name(0xFFC0) == NULL);
    char u[40];
    bl_uuid_str(NUS_TX, 16, u, sizeof u);
    STREQ(u, "6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
    bl_uuid_str((const uint8_t[]){ 0x0D, 0x18 }, 2, u, sizeof u);
    STREQ(u, "0x180D");
    bl_uuid_str((const uint8_t[]){ 0xD2, 0xFC, 0x00, 0x00 }, 4, u, sizeof u);
    STREQ(u, "0x0000FCD2");
    bl_uuid_str(NUS_TX, 16, u, 9);
    CHECK(strlen(u) < 9);

    uint8_t pub[6] = { 0xA4, 0xC1, 0x38, 0x5C, 0x1A, 0x2B };
    CHECK(bl_addr_kind(pub, 0) == BL_ADDR_PUBLIC);
    CHECK(bl_addr_kind((const uint8_t[]){ 0xC3, 0, 0, 0, 0, 0 }, 1) == BL_ADDR_STATIC);
    CHECK(bl_addr_kind((const uint8_t[]){ 0x5A, 0, 0, 0, 0, 0 }, 1) == BL_ADDR_RPA);
    CHECK(bl_addr_kind((const uint8_t[]){ 0x1A, 0, 0, 0, 0, 0 }, 1) == BL_ADDR_NRPA);
    STREQ(bl_addr_kind_name(BL_ADDR_PUBLIC), "Pública");
    STREQ(bl_addr_kind_name(BL_ADDR_STATIC), "Aleatoria estática");
    STREQ(bl_addr_kind_name(BL_ADDR_RPA), "Privada resoluble");
    STREQ(bl_addr_kind_name(BL_ADDR_NRPA), "Privada no resoluble");
}

static const char *fmt(uint16_t uuid, const uint8_t *v, int n)
{
    static char out[160];
    if (!bl_value_format(uuid, v, n, out, sizeof out, NULL)) return "(false)";
    CHECK(utf8_ok(out, sizeof out));
    return out;
}
#define FMT(uuid, ...) fmt(uuid, BYTES(__VA_ARGS__))

static void test_values(void)
{
    STREQ(FMT(0x2A19, 78), "78 %");
    STREQ(FMT(0x2A6E, 0xAA, 0x0A), "27,30 \xC2\xB0" "C");
    STREQ(FMT(0x2A6E, 0x0C, 0xFE), "-5,00 \xC2\xB0" "C");
    STREQ(FMT(0x2A6E, 0x00, 0x80), "sin dato");
    STREQ(FMT(0x2A6E, 0x00), "(false)");
    STREQ(FMT(0x2A6F, 0x10, 0x0F), "38,56 %");
    STREQ(FMT(0x2A6D, 0x58, 0x70, 0x0F, 0x00), "1011,8 hPa");
    STREQ(FMT(0x2A1F, 0xF5, 0x00), "24,5 \xC2\xB0" "C");
    STREQ(FMT(0x2A1C, 0x00, 0x6D, 0x01, 0x00, 0xFF), "36,5 \xC2\xB0" "C");
    STREQ(FMT(0x2A1C, 0x01, 0xBB, 0x03, 0x00, 0xFF), "95,5 \xC2\xB0" "F");
    STREQ(FMT(0x2A1C, 0x04, 0x6D, 0x01, 0x00, 0xFF, 0x02), "36,5 \xC2\xB0" "C \xC2\xB7 cuerpo");
    STREQ(FMT(0x2A1C, 0x02, 0x6D, 0x01, 0x00, 0xFF, 0xEA, 0x07, 10, 7, 14, 3, 22), "36,5 \xC2\xB0" "C \xC2\xB7 07/10/2026 14:03:22");
    STREQ(FMT(0x2A1C, 0x00, 0xFF, 0xFF, 0x7F, 0x00), "sin dato");
    STREQ(FMT(0x2A1C, 0x00, 0x03, 0x00, 0x00, 0x02), "300 \xC2\xB0" "C");
    STREQ(FMT(0x2A37, 0x16, 92, 0x9C, 0x02), "92 lpm (contacto, RR 652 ms)");
    STREQ(FMT(0x2A37, 0x16, 92, 0x9C, 0x02, 0x00, 0x04), "92 lpm (contacto, RR 652/1000 ms)");
    STREQ(FMT(0x2A37, 0x01, 0x2C, 0x01), "300 lpm");
    STREQ(FMT(0x2A37, 0x08, 70, 0x10, 0x00), "70 lpm (16 kJ)");
    STREQ(FMT(0x2A37, 0x04, 60), "60 lpm (sin contacto)");
    STREQ(FMT(0x2A37, 0x01, 0x2C), "(false)");
    STREQ(FMT(0x2A38, 1), "Pecho");
    STREQ(FMT(0x2A38, 2), "Muñeca");
    STREQ(FMT(0x2A38, 9), "(false)");
    STREQ(FMT(0x2A01, 0xC2, 0x03), "Mouse (0x03C2)");
    STREQ(FMT(0x2A01, 0x41, 0x03), "Banda de pulso (0x0341)");
    STREQ(FMT(0x2A50, 0x01, 0x4C, 0x00, 0x34, 0x12, 0x00, 0x01), "Bluetooth SIG: Apple, Inc. 0x004C, producto 0x1234, versión 0x0100");
    STREQ(FMT(0x2A50, 0x02, 0x6D, 0x04, 0x2B, 0xC5, 0x10, 0x00), "USB: 0x046D, producto 0xC52B, versión 0x0010");
    STREQ(FMT(0x2A23, 1, 2, 3, 4, 5, 6, 7, 8), "01 02 03 04 05 06 07 08");
    STREQ(FMT(0x2A04, 24, 0, 40, 0, 0, 0, 0x90, 0x01), "30,00 \xE2\x80\x93 50,00 ms, latencia 0, supervisión 4000 ms");
    STREQ(FMT(0x2A05, 0x01, 0x00, 0xFF, 0xFF), "0x0001 \xE2\x80\x93 0xFFFF");
    STREQ(FMT(0x2A2B, 0xEA, 0x07, 10, 7, 14, 3, 22, 3, 0, 0), "07/10/2026 14:03:22");
    STREQ(FMT(0x2A9D, 0x00, 0x2C, 0x38), "71,90 kg");
    STREQ(FMT(0x2A9D, 0x01, 0x10, 0x27), "100,00 lb");
    STREQ(FMT(0x2A9D, 0x00, 0xFF, 0xFF), "medición fallida");
    STREQ(FMT(0x2A5B, 0x03, 0xD2, 0x04, 0x00, 0x00, 0x00, 0x04, 0xC8, 0x01, 0x00, 0x08), "rueda 1234 vueltas \xC2\xB7 pedal 456 vueltas");
    STREQ(FMT(0x2A5B, 0x02, 0xC8, 0x01, 0x00, 0x08), "pedal 456 vueltas");
    STREQ(FMT(0x2A63, 0x00, 0x00, 0xF5, 0x00), "245 W");
    STREQ(FMT(0x2A63, 0x01, 0x00, 0xF5, 0x00, 100), "245 W \xC2\xB7 balance 50,0 %");
    STREQ(FMT(0x2A53, 0x04, 0x40, 0x03, 172), "3,25 m/s (11,70 km/h), 172 pasos/min, corriendo");
    STREQ(FMT(0x2A35, 0x00, 0x78, 0x00, 0x50, 0x00, 0x5D, 0x00), "120/80 mmHg");
    STREQ(FMT(0x2A35, 0x04, 0x78, 0x00, 0x50, 0x00, 0x5D, 0x00, 0x48, 0x00), "120/80 mmHg \xC2\xB7 72 lpm");
    STREQ(FMT(0x2A29, 'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'), "Espressif");
    STREQ(FMT(0x2A00, 'B', 'a', 'n', 'd', 'a', 0), "Banda");
    STREQ(FMT(0x2A24, 0xFF, 0x01), "(false)");
    STREQ(FMT(0x2902, 0x01, 0x00), "notificaciones");
    STREQ(FMT(0x2902, 0x02, 0x00), "indicaciones");
    STREQ(FMT(0x2902, 0x03, 0x00), "notificaciones e indicaciones");
    STREQ(FMT(0x2902, 0x00, 0x00), "apagado");
    STREQ(FMT(0x2901, 'P', 'r', 'e', 's', 'i', 0xC3, 0xB3, 'n'), "Presión");
    STREQ(FMT(0x2904, 0x08, 0xFF, 0x24, 0x27, 0x01, 0x00, 0x00), "uint32 \xC3\x97" "10^-1 Pa");
    STREQ(FMT(0x2904, 0x0E, 0xFE, 0x2F, 0x27, 0x01, 0x00, 0x00), "sint16 \xC3\x97" "10^-2 \xC2\xB0" "C");
    STREQ(FMT(0x2904, 0x04, 0x00, 0xAD, 0x27, 0x01, 0x00, 0x00), "uint8 %");
    STREQ(FMT(0x2908, 1, 1), "reporte 1, entrada");
    STREQ(FMT(0x2900, 1, 0), "escritura confiable");
    STREQ(FMT(0x2907, 0x19, 0x2A), "0x2A19 Battery Level");
    STREQ(FMT(0x1234, 1, 2), "(false)");
    char small[8];
    CHECK(bl_value_format(0x2A04, (const uint8_t[]){ 24, 0, 40, 0, 0, 0, 0x90, 0x01 }, 8, small, sizeof small, NULL));
    CHECK(strlen(small) < sizeof small && utf8_ok(small, sizeof small));
    CHECK(!bl_value_format(0x2A19, NULL, 0, small, sizeof small, NULL));
}

static void test_text(void)
{
    char t[16];
    CHECK(bl_value_text((const uint8_t *)"hola\0", 5, t, sizeof t));
    STREQ(t, "hola");
    CHECK(!bl_value_text((const uint8_t[]){ 'a', 0xFF }, 2, t, sizeof t));
    CHECK(!bl_value_text((const uint8_t[]){ 'a', 0x01 }, 2, t, sizeof t));
    CHECK(!bl_value_text((const uint8_t[]){ 0, 0 }, 2, t, sizeof t));
    CHECK(!bl_value_text((const uint8_t[]){ 0xED, 0xA0, 0x80 }, 3, t, sizeof t));     /* a surrogate */
    /* cut on a character: "ñandú ñandú ñandú" in 8 bytes */
    const char *s = "\xC3\xB1" "and\xC3\xBA \xC3\xB1" "and\xC3\xBA";
    char c8[8];
    CHECK(bl_value_text((const uint8_t *)s, (int)strlen(s), c8, sizeof c8));
    STREQ(c8, "\xC3\xB1" "and\xC3\xBA");
    char h[64];
    bl_hex((const uint8_t[]){ 0x01, 0xA2, 0xFF }, 3, h, sizeof h);
    STREQ(h, "01 A2 FF");
    bl_hex((const uint8_t[]){ 1, 2, 3, 4, 5, 6 }, 6, h, 12);
    CHECK(strlen(h) < 12 && utf8_ok(h, 12));
    HAS(h, "\xE2\x80\xA6");
    bl_hex(NULL, 0, h, sizeof h);
    STREQ(h, "");
    /* the caller's buffer for a whole packet: 31 bytes fit without a cut */
    uint8_t p31[31];
    memset(p31, 0xAB, sizeof p31);
    char hx[31 * 3 + 4];
    bl_hex(p31, 31, hx, sizeof hx);
    CHECK(strlen(hx) == 92 && !strstr(hx, "\xE2\x80\xA6"));
}

static void test_distance(void)
{
    NEAR(bl_distance_m(-59, -59, 2.0f), 1.0f, 1e-5f);
    NEAR(bl_distance_m(-79, -59, 2.0f), 10.0f, 1e-3f);
    NEAR(bl_distance_m(-79, -59, 4.0f), 3.1623f, 1e-3f);
    NEAR(bl_distance_m(-49, -59, 2.0f), 0.31623f, 1e-4f);
    NEAR(bl_distance_m(-79, -59, 0.0f), 10.0f, 1e-3f);           /* a silly exponent falls back to 2 */
    bl_ad_t a;
    bl_ad_clear(&a);
    CHECK(bl_p1m_guess(&a, NULL) == -59);
    CHECK(bl_p1m_guess(NULL, NULL) == -59);
    a.has_tx = true;
    a.tx_power = 12;
    CHECK(bl_p1m_guess(&a, NULL) == -29);
    bl_beacon_t b = { .kind = BL_BEACON_EDDY_URL, .tx1m = -20 };
    CHECK(bl_p1m_guess(&a, &b) == -61);
    b.kind = BL_BEACON_EDDY_TLM;
    CHECK(bl_p1m_guess(&a, &b) == -29);
}

/* ---- the simulator's neighbourhood, for real ---- */

static uint64_t g_now;
uint64_t aos_hal_uptime_ms(void) { return g_now; }
bool aos_hal_bt_enabled(void) { return true; }

/* the simulator's drift(), to know what each packet should say */
static float drift(uint32_t now, float centre, float span, float period_s, float off)
{
    return centre + span * sinf((now / 1000.0f) / period_s * 6.2831853f + off);
}

typedef struct {
    uint8_t addr[6];
    bl_ad_t ad;
    int checked;
    bool rsp_seen;
} simdev_t;

static bool is(const uint8_t a[6], const char *hex)
{
    unsigned v[6];
    sscanf(hex, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    for (int i = 0; i < 6; i++)
        if (a[i] != v[i]) return false;
    return true;
}

static void sim_check(simdev_t *d, const aos_ble_adv_t *pk)
{
    uint32_t t = pk->t_ms;
    bool rsp = pk->kind == AOS_BLE_ADV_SCAN_RSP;
    bl_sensor_t s;
    bl_beacon_t b;
    const char *lab;
    bool sok = bl_sensor_decode(&d->ad, d->addr, &s);
    bool bok = bl_beacon_decode(&d->ad, &b);
    bl_class_t c = bl_classify(&d->ad, &lab);
    const uint8_t *a = d->addr;
    int before = g_fail;

    if (is(a, "A4:C1:38:5C:1A:2B")) {                       /* pvvx */
        if (rsp) return;
        CHECK(sok && !strcmp(s.format, "pvvx"));
        NEAR(s.temp, (int16_t)(int)(drift(t, 23.4f, 0.8f, 600, 0) * 100) * 0.01f, 0.0001f);
        NEAR(s.hum, (int)(drift(t, 48.0f, 3.0f, 900, 1) * 100) * 0.01f, 0.0001f);
        NEAR(s.volt, 2.95f, 0.0001f);
        CHECK(s.batt == 87);
        CHECK(c == BL_CLS_SENSOR);
    } else if (is(a, "D8:3B:7A:11:40:E2")) {                /* BTHome */
        CHECK(sok && !strcmp(s.format, "BTHome v2"));
        NEAR(s.temp, (int16_t)(int)(drift(t, 19.8f, 1.2f, 500, 2) * 100) * 0.01f, 0.0001f);
        NEAR(s.hum, (int)(drift(t, 61.0f, 4.0f, 700, 0.5f) * 100) * 0.01f, 0.0001f);
        CHECK(s.batt == 64);
        CHECK(c == BL_CLS_SENSOR);
    } else if (is(a, "E4:9A:12:33:08:C5")) {                /* BTHome door */
        CHECK(sok && (s.mask & BL_V_OPEN) && (s.mask & BL_V_BUTTON));
        CHECK(s.open == ((t / 15000) % 3 == 0));
        CHECK(s.button == ((t / 4000) % 9 == 0 ? 1 : 0));
        CHECK(s.batt == 92);
    } else if (is(a, "A4:C1:38:52:4F:21")) {                /* Govee */
        float tt = drift(t, 4.5f, 1.5f, 400, 0), hh = drift(t, 72.0f, 5.0f, 800, 0);
        CHECK(sok && !strcmp(s.format, "Govee"));
        NEAR(s.temp, (tt < 0 ? -1 : 1) * (float)(int)(fabsf(tt) * 10) / 10.0f, 0.0001f);
        NEAR(s.hum, (float)(uint32_t)(hh * 10) / 10.0f, 0.0001f);
        CHECK(s.batt == 76);
        CHECK(c == BL_CLS_SENSOR && lab && !strcmp(lab, "Termómetro Govee"));
    } else if (is(a, "F1:0C:55:9D:3F:8C")) {                /* Ruuvi */
        if (rsp) return;
        CHECK(sok && !strcmp(s.format, "Ruuvi RAWv2"));
        NEAR(s.temp, (int16_t)(int)(drift(t, 11.5f, 3.0f, 1200, 1) / 0.005f) * 0.005f, 0.0001f);
        NEAR(s.hum, (uint16_t)(int)(drift(t, 80.0f, 6.0f, 1500, 0) / 0.0025f) * 0.0025f, 0.0001f);
        NEAR(s.press, ((int)(drift(t, 101325.0f, 300.0f, 3000, 0) - 50000) + 50000) / 100.0f, 0.01f);
        NEAR(s.volt, 2.98f, 0.0001f);
        CHECK(fabsf(s.acc_x) <= 0.0101f && fabsf(s.acc_z - 1.005f) <= 0.006f);
    } else if (is(a, "58:2D:34:10:77:6A")) {                /* MiBeacon */
        if (rsp) return;
        CHECK(sok && !strcmp(s.format, "MiBeacon") && !s.encrypted);
        NEAR(s.temp, (int16_t)(int)(drift(t, 21.0f, 0.6f, 650, 3) * 10) * 0.1f, 0.0001f);
        NEAR(s.hum, (int)(drift(t, 52.0f, 2.0f, 800, 1) * 10) * 0.1f, 0.0001f);
    } else if (is(a, "C9:21:7E:0A:3D:91")) {                /* SwitchBot, readings in the response */
        if (!rsp) return;
        float tt = drift(t, 24.6f, 0.5f, 450, 0);
        CHECK(sok && !strcmp(s.format, "SwitchBot"));
        NEAR(s.temp, (float)(int)tt + (float)((int)(tt * 10) % 10) / 10.0f, 0.0001f);
        NEAR(s.hum, (uint8_t)drift(t, 45.0f, 2.0f, 600, 0), 0);
        CHECK(s.batt == 100);
    } else if (is(a, "58:2D:34:54:2C:08")) {                /* Qingping */
        CHECK(sok && !strcmp(s.format, "Qingping"));
        NEAR(s.temp, (int)(drift(t, 26.1f, 0.4f, 500, 2) * 10) * 0.1f, 0.0001f);
        NEAR(s.hum, (int)(drift(t, 40.0f, 1.5f, 700, 2) * 10) * 0.1f, 0.0001f);
        CHECK(s.batt == 55);
    } else if (is(a, "49:22:06:12:3A:7B")) {                /* Inkbird, once its name came */
        if (rsp || !d->rsp_seen) return;
        CHECK(sok && !strcmp(s.format, "Inkbird"));
        NEAR(s.temp, (int16_t)((int)(drift(t, -18.2f, 0.7f, 300, 0) * 100) & 0xFFFF) * 0.01f, 0.0001f);
        NEAR(s.hum, (int)(drift(t, 41.0f, 3.0f, 500, 1) * 100) * 0.01f, 0.0001f);
        CHECK(s.batt == 71);
    } else if (is(a, "DC:0D:30:01:2A:6E")) {                /* iBeacon */
        char u[48];
        CHECK(bok && b.kind == BL_BEACON_IBEACON && b.major == 1 && b.minor == 42 && b.tx1m == -59);
        bl_uuid_str(b.uuid, 16, u, sizeof u);
        STREQ(u, "E2C56DB5-DFFB-48D2-B060-D0F5A71096E0");
        CHECK(sok && s.rssi1m == -59);
        CHECK(c == BL_CLS_BEACON);
    } else if (is(a, "C3:7F:21:98:0B:44")) {                /* Eddystone, three frames in turn */
        CHECK(bok && c == BL_CLS_BEACON);
        if (b.kind == BL_BEACON_EDDY_URL) {
            STREQ(b.url, "https://example.com");
            CHECK(b.tx1m == -20);
        } else if (b.kind == BL_BEACON_EDDY_TLM) {
            CHECK(sok && !strcmp(s.format, "Eddystone TLM"));
            NEAR(s.volt, 2.87f, 0.0001f);
            NEAR(s.temp, (int)(drift(t, 17.0f, 1.0f, 900, 0) * 256) / 256.0f, 0.0001f);
            CHECK(s.count == (int)(t / 500));
        } else {
            CHECK(b.kind == BL_BEACON_EDDY_UID && b.tx1m == -20 && b.uuid[0] == 0xED && b.uuid[15] == 0xD2);
        }
    } else if (is(a, "5A:21:9C:3E:70:12") || is(a, "6E:03:A1:4C:92:5D")) {
        CHECK(c == BL_CLS_PHONE && lab && !strcmp(lab, "iPhone"));
    } else if (is(a, "4F:88:12:6B:31:0A")) {
        CHECK(c == BL_CLS_AUDIO && lab && !strcmp(lab, "AirPods Pro 2"));
    } else if (is(a, "F7:41:2A:9C:05:B3")) {
        CHECK(c == BL_CLS_TRACKER && lab && !strcmp(lab, "AirTag (lejos del dueño)"));
    } else if (is(a, "72:1E:4B:0D:88:39")) {
        CHECK(c == BL_CLS_COMPUTER && lab && !strcmp(lab, "Mac"));
    } else if (is(a, "E2:7C:90:3B:14:6F")) {
        CHECK(c == BL_CLS_HID && lab && !strcmp(lab, "Mouse (Swift Pair)"));
    } else if (is(a, "66:2A:81:09:C4:1D")) {
        CHECK(c == BL_CLS_AUDIO);
    } else if (is(a, "8C:79:F5:22:61:A8")) {
        CHECK(c == BL_CLS_WATCH);
    } else if (is(a, "E9:10:4D:77:2B:03")) {
        CHECK(c == BL_CLS_TRACKER && lab && !strcmp(lab, "Tile"));
    } else if (is(a, "3C:61:05:1E:9A:44")) {
        CHECK(c == BL_CLS_WATCH && lab && !strcmp(lab, "Amazfit"));
    } else if (is(a, "7A:33:0E:51:2C:9B")) {
        CHECK(c == BL_CLS_WATCH && lab && !strcmp(lab, "Garmin"));
    } else if (is(a, "D0:5F:64:8A:17:3E")) {
        CHECK(c == BL_CLS_HEALTH);
    } else if (is(a, "24:0A:C4:6E:1F:52")) {
        CHECK(c == BL_CLS_DEVBOARD);
    } else if (is(a, "A4:C1:38:E1:F0:A1")) {                /* a stock Xiaomi, encrypted */
        if (rsp) return;
        CHECK(sok && !strcmp(s.format, "MiBeacon") && s.encrypted);
        uint8_t k[16];
        for (int q = 0; q < 16; q++) k[q] = (uint8_t)q;
        bl_sensor_t p;
        CHECK(bl_sensor_decode_key(&d->ad, a, k, &p) == BL_KEY_OK && !p.encrypted);
        CHECK((p.mask & BL_V_TEMP) ? fabsf(p.temp - 22.4f) < 0.001f
              : (p.mask & BL_V_HUM) ? fabsf(p.hum - 58) < 0.001f : (p.mask & BL_V_BATT) && p.batt == 83);
        k[0] = 0xFF;
        CHECK(bl_sensor_decode_key(&d->ad, a, k, &p) == BL_KEY_WRONG);
    } else {
        printf("  unknown simulated device %02X:%02X:%02X:%02X:%02X:%02X\n", a[0], a[1], a[2], a[3], a[4], a[5]);
        CHECK(0);
    }
    d->checked++;
    if (g_fail != before) {
        printf("  ^ device %02X:%02X:%02X:%02X:%02X:%02X at t=%u (%s)\n", a[0], a[1], a[2], a[3], a[4], a[5], t,
               rsp ? "rsp" : "adv");
        bl_line_t l[24];
        int n = bl_explain(pk->data, pk->len, l, 24, NULL);
        for (int i = 0; i < n; i++) printf("    %s: %s\n", l[i].key, l[i].val);
    }
}

static void gatt_device(const char *addr, int min_checked)
{
    unsigned v[6];
    uint8_t a[6];
    sscanf(addr, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
    for (int i = 0; i < 6; i++) a[i] = (uint8_t)v[i];
    aos_ble_gatt_state_t st = AOS_BLE_GATT_IDLE;
    for (int tries = 0; tries < 20 && st != AOS_BLE_GATT_READY; tries++) {
        aos_hal_ble_gatt_connect(a, 0);
        for (int k = 0; k < 40; k++) {
            g_now += 100;
            st = aos_hal_ble_gatt_state(NULL);
            if (st == AOS_BLE_GATT_READY || st == AOS_BLE_GATT_FAILED) break;
        }
    }
    CHECK(st == AOS_BLE_GATT_READY);
    if (st != AOS_BLE_GATT_READY) return;
    static aos_ble_attr_t at[64];
    int n = aos_hal_ble_gatt_attrs(at, 64);
    for (int i = 0; i < n; i++) {
        if (at[i].kind == AOS_BLE_ATTR_SERVICE) continue;
        aos_hal_ble_gatt_read(at[i].handle);
        if (at[i].kind == AOS_BLE_ATTR_CHAR && (at[i].props & 0x30)) aos_hal_ble_gatt_subscribe(at[i].handle, 1);
    }
    g_now += 1500;
    aos_hal_ble_gatt_state(NULL);
    static aos_ble_gatt_ev_t ev[64];
    int ne = aos_hal_ble_gatt_events(ev, 64), formatted = 0;
    for (int e = 0; e < ne; e++) {
        if (ev[e].status || (ev[e].type != AOS_BLE_EV_READ && ev[e].type != AOS_BLE_EV_NOTIFY && ev[e].type != AOS_BLE_EV_INDICATE))
            continue;
        uint16_t u = 0;
        for (int i = 0; i < n; i++)
            if (at[i].handle == ev[e].handle && at[i].uuid_len == 2) u = (uint16_t)(at[i].uuid[0] | at[i].uuid[1] << 8);
        if (!u) continue;
        char out[128];
        bool ok = bl_value_format(u, ev[e].data, ev[e].len, out, sizeof out, NULL);
        if (!ok) printf("  GATT 0x%04X (%u bytes) did not format\n", u, ev[e].len);
        CHECK(ok);
        if (!ok) continue;
        formatted++;
        switch (u) {
        case 0x2A00: CHECK(!strcmp(out, "Banda HR 7C21") || !strcmp(out, "ESP32-Taller")); break;
        case 0x2A01: CHECK(!strcmp(out, "Banda de pulso (0x0341)") || !strcmp(out, "Sensor (0x0540)")); break;
        case 0x2A04: STREQ(out, "30,00 \xE2\x80\x93 50,00 ms, latencia 0, supervisión 4000 ms"); break;
        case 0x2A05: STREQ(out, "0x0001 \xE2\x80\x93 0xFFFF"); break;
        case 0x2A37: HAS(out, " lpm (contacto, RR "); break;
        case 0x2A38: STREQ(out, "Pecho"); break;
        case 0x2A19: STREQ(out, "78 %"); break;
        case 0x2A29: CHECK(!strcmp(out, "Pulsos S.A.") || !strcmp(out, "Espressif")); break;
        case 0x2A6E: HAS(out, " \xC2\xB0" "C"); break;
        case 0x2A6F: HAS(out, " %"); break;
        case 0x2A6D: HAS(out, " hPa"); HAS(out, "101"); break;
        case 0x2901: STREQ(out, "Presion (Pa x10)"); break;
        case 0x2904: STREQ(out, "uint32 \xC3\x97" "10^-1 Pa"); break;
        case 0x2902: CHECK(!strcmp(out, "apagado") || !strcmp(out, "notificaciones")); break;
        default: break;
        }
    }
    CHECK(formatted >= min_checked);
    aos_hal_ble_gatt_disconnect();
}

static void test_sim(void)
{
    static simdev_t dev[64];
    int nd = 0;
    g_now = 1000000;
    CHECK(aos_hal_ble_scan_start(true, 100));
    static aos_ble_adv_t buf[1024];
    for (int step = 0; step < 240; step++) {          /* four minutes of air, read every second */
        g_now += 1000;
        int n = aos_hal_ble_scan_read(buf, 1024);
        for (int i = 0; i < n; i++) {
            simdev_t *d = NULL;
            for (int k = 0; k < nd; k++)
                if (!memcmp(dev[k].addr, buf[i].addr, 6)) d = &dev[k];
            if (!d && nd < 64) {
                d = &dev[nd++];
                memcpy(d->addr, buf[i].addr, 6);
                bl_ad_clear(&d->ad);
            }
            if (!d) continue;
            bl_line_t l[24];
            int nl = bl_explain(buf[i].data, buf[i].len, l, 24, NULL);
            for (int k = 0; k < nl; k++) {
                CHECK(utf8_ok(l[k].key, sizeof l[k].key) && utf8_ok(l[k].val, sizeof l[k].val));
                if (!strcmp(l[k].key, "Error")) { printf("  sim packet with an error: %s\n", l[k].val); CHECK(0); }
            }
            bl_ad_merge(&d->ad, buf[i].data, buf[i].len);
            CHECK(!d->ad.malformed);
            if (buf[i].kind == AOS_BLE_ADV_SCAN_RSP) d->rsp_seen = true;
            sim_check(d, &buf[i]);
        }
    }
    aos_hal_ble_scan_stop();
    CHECK(nd == 25);
    for (int k = 0; k < nd; k++) {
        if (dev[k].checked < 2) printf("  device %d checked only %d times\n", k, dev[k].checked);
        CHECK(dev[k].checked >= 2);
    }
    printf("sim: %d devices heard, every one decoded as expected\n", nd);

    gatt_device("24:0A:C4:6E:1F:52", 8);
    gatt_device("D0:5F:64:8A:17:3E", 10);
}

/* ---- noise ---- */

static uint32_t rs = 0x12345678u;
static uint32_t rnd32(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

/* ---- the encrypted ones ---- */

static int unhex(const char *h, uint8_t *o)
{
    int n = 0;
    while (h[0] && h[1]) { unsigned v; sscanf(h, "%2x", &v); o[n++] = (uint8_t)v; h += 2; }
    return n;
}

static void test_crypto(void)
{
    uint8_t k[16], in[64], c[64], out[64], want[64], nonce[16], aad[32];
    /* AES-128, FIPS-197 appendix C.1 */
    unhex("000102030405060708090a0b0c0d0e0f", k);
    unhex("00112233445566778899aabbccddeeff", in);
    unhex("69c4e0d86a7b0430d8cdb78070b4c55a", want);
    bl_aes128_encrypt(k, in, out);
    CHECK(!memcmp(out, want, 16));
    /* CCM, NIST SP 800-38C examples 1 to 3 (nonces of 7, 8 and 12 bytes) */
    unhex("404142434445464748494a4b4c4d4e4f", k);
    int nn = unhex("10111213141516", nonce), na = unhex("0001020304050607", aad);
    unhex("7162015b4dac255d", c);
    unhex("20212223", want);
    CHECK(bl_ccm_decrypt(k, nonce, nn, aad, na, c, 4, c + 4, 4, out) && !memcmp(out, want, 4));
    nn = unhex("1011121314151617", nonce);
    na = unhex("000102030405060708090a0b0c0d0e0f", aad);
    unhex("d2a1f0e051ea5f62081a7792073d593d1fc64fbfaccd", c);
    unhex("202122232425262728292a2b2c2d2e2f", want);
    CHECK(bl_ccm_decrypt(k, nonce, nn, aad, na, c, 16, c + 16, 6, out) && !memcmp(out, want, 16));
    c[3] ^= 1;
    CHECK(!bl_ccm_decrypt(k, nonce, nn, aad, na, c, 16, c + 16, 6, out));
    nn = unhex("101112131415161718191a1b", nonce);
    na = unhex("000102030405060708090a0b0c0d0e0f10111213", aad);
    unhex("e3b201a9f5b71a7a9b1ceaeccd97e70b6176aad9a4428aa5484392fbc1b09951", c);
    unhex("202122232425262728292a2b2c2d2e2f3031323334353637", want);
    CHECK(bl_ccm_decrypt(k, nonce, nn, aad, na, c, 24, c + 24, 8, out) && !memcmp(out, want, 24));
    CHECK(bl_parse_key("23:1d:39:c1 d7cc1ab1-aee224cd096db932", k) && k[0] == 0x23 && k[15] == 0x32);
    CHECK(!bl_parse_key("231d39c1d7cc1ab1aee224cd096db9", k));     /* 15 bytes */
    CHECK(!bl_parse_key("b853075158487ca39a5b5ea9", k));            /* a 12-byte v2/v3 key */

    /* BTHome v2 encrypted: the example of bthome.io, temperature 25.06 and
     * humidity 50.55 */
    bl_ad_t ad;
    bl_sensor_t s;
    unhex("231d39c1d7cc1ab1aee224cd096db932", k);
    const uint8_t a1[6] = { 0x54, 0x48, 0xE6, 0x8F, 0x80, 0xA5 };
    int n = unhex("020106121" "6d2fc41a47266c95f730011223378237214", in);
    bl_ad_clear(&ad);
    bl_ad_merge(&ad, in, n);
    CHECK(bl_sensor_decode(&ad, a1, &s) && s.encrypted);
    CHECK(bl_sensor_decode_key(&ad, a1, k, &s) == BL_KEY_OK && !s.encrypted);
    NEAR(s.temp, 25.06f, 0.001f);
    NEAR(s.hum, 50.55f, 0.001f);
    k[5] ^= 0x40;
    CHECK(bl_sensor_decode_key(&ad, a1, k, &s) == BL_KEY_WRONG);

    /* MiBeacon v5 encrypted, shaped like a stock LYWSD03MMC's (frame
     * control 0x5858, MAC included), made with pycryptodome and the nonce
     * of Home Assistant's xiaomi-ble: MAC little endian + product id +
     * counter + extended counter, associated data 0x11. Objects 0x4C01
     * (temperature, float) 21.5 and 0x4C02 (humidity) 47. */
    for (int i = 0; i < 16; i++) k[i] = (uint8_t)i;
    const uint8_t a2[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };
    n = unhex("020106201695fe5858e4162a6655443322112ac52bf45b60e5f77c7ebb01000020195355", in);
    bl_ad_clear(&ad);
    bl_ad_merge(&ad, in, n);
    CHECK(bl_sensor_decode_key(&ad, a2, k, &s) == BL_KEY_OK && !strcmp(s.format, "MiBeacon"));
    NEAR(s.temp, 21.5f, 0.001f);
    NEAR(s.hum, 47.0f, 0.001f);
    CHECK(bl_sensor_decode_key(&ad, a1, k, &s) == BL_KEY_WRONG);    /* another MAC, another nonce */
    /* a plain one is not decrypted */
    n = unhex("020106121695fe5020aa01" "2a6a771034" "2d580d1004" "d500b001", in);
    bl_ad_clear(&ad);
    bl_ad_merge(&ad, in, n);
    CHECK(bl_sensor_decode_key(&ad, a2, k, &s) == BL_KEY_NOT_ENCRYPTED);
    printf("crypto: AES, CCM, BTHome and MiBeacon with keys\n");
}

static void fuzz(int iters)
{
    static const uint8_t TYPES[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0D, 0x10, 0x12, 0x14,
                                     0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1F, 0x20, 0x21, 0x24, 0x2F, 0x30, 0xFF, 0x77 };
    static const uint16_t SD[] = { 0xFCD2, 0x181C, 0x181E, 0x181A, 0xFE95, 0xFD3D, 0x0D00, 0xFDCD, 0xFEAA, 0xFE2C, 0x1234 };
    static const uint16_t CO[] = { 0x004C, 0x0006, 0xEC88, 0x0001, 0x0499, 0x0969, 0x02E1, 0x02E5, 0x0075, 0x0059 };
    static const uint16_t GV[] = { 0x2A00, 0x2A01, 0x2A04, 0x2A05, 0x2A08, 0x2A19, 0x2A1C, 0x2A1D, 0x2A1E, 0x2A1F, 0x2A20,
                                   0x2A23, 0x2A24, 0x2A2B, 0x2A35, 0x2A37, 0x2A38, 0x2A50, 0x2A53, 0x2A5B, 0x2A63, 0x2A6C,
                                   0x2A6D, 0x2A6E, 0x2A6F, 0x2A76, 0x2A7B, 0x2A9D, 0x2900, 0x2901, 0x2902, 0x2903, 0x2904,
                                   0x2905, 0x2907, 0x2908, 0xFFFF };
    static const char *const NAMES[] = { "sps", "tps", "GVH5102", "Govee_H5074", "", "ATC_1", "x" };
    bl_tr_fn trs[3] = { NULL, tr_upper, tr_long };
    bl_ad_t acc;
    bl_ad_clear(&acc);
    for (int it = 0; it < iters; it++) {
        uint8_t d[64];
        int len = (int)(rnd32() % 48);
        if (rnd32() & 1) {
            for (int i = 0; i < len; i++) d[i] = (uint8_t)rnd32();
        } else {
            /* structured: AD structures of real types, some with real keys */
            int k = 0;
            while (k < len) {
                int l = 1 + (int)(rnd32() % 30);
                if (k + 1 + l > len) l = len - k - 1;
                if (l < 1) break;
                d[k] = (uint8_t)l;
                d[k + 1] = TYPES[rnd32() % sizeof TYPES];
                for (int i = 2; i <= l; i++) d[k + i] = (uint8_t)rnd32();
                if (d[k + 1] == 0x16 && l >= 3) { uint16_t u = SD[rnd32() % 11]; d[k + 2] = (uint8_t)u; d[k + 3] = (uint8_t)(u >> 8); }
                if (d[k + 1] == 0xFF && l >= 3) { uint16_t u = CO[rnd32() % 10]; d[k + 2] = (uint8_t)u; d[k + 3] = (uint8_t)(u >> 8); }
                if (rnd32() % 8 == 0) d[k] = (uint8_t)(l + rnd32() % 4);    /* sometimes a lie */
                k += 1 + l;
            }
            len = k > len ? len : k;
        }
        if (it % 50 == 0) bl_ad_clear(&acc);
        bl_ad_merge(&acc, d, len);
        if (rnd32() % 4 == 0) {
            const char *nm = NAMES[rnd32() % 7];
            memcpy(acc.name, nm, strlen(nm) + 1);
        }
        bl_line_t l[24];
        int max = 1 + (int)(rnd32() % 24);
        int n = bl_explain(d, len, l, max, trs[it % 3]);
        if (n < 0 || n > max) { CHECK(0); break; }
        for (int i = 0; i < n; i++)
            if (!utf8_ok(l[i].key, sizeof l[i].key) || !utf8_ok(l[i].val, sizeof l[i].val)) { CHECK(0); printf("  bad utf-8 at %d\n", it); }
        bl_sensor_t s;
        bl_sensor_decode(&acc, NULL, &s);
        {
            /* and with a key: random, so nearly always the wrong one, which
             * still walks every parse and the whole CCM */
            uint8_t fk[16], fa[6];
            for (int q = 0; q < 16; q++) fk[q] = (uint8_t)rand();
            for (int q = 0; q < 6; q++) fa[q] = (uint8_t)rand();
            bl_sensor_decode_key(&acc, fa, fk, &s);
        }
        bl_beacon_t b;
        if (bl_beacon_decode(&acc, &b) && !utf8_ok(b.url, sizeof b.url)) CHECK(0);
        const char *lab;
        bl_class_t c = bl_classify(&acc, &lab);
        if ((int)c < 0 || (int)c >= (int)BL_CLS_COUNT) CHECK(0);
        (void)bl_p1m_guess(&acc, &b);
        for (int i = 0; i < acc.nmfg; i++) {
            const char *ty[6];
            int nt = bl_apple_types(&acc.mfg[i], ty, 6);
            if (nt < 0 || nt > 6) CHECK(0);
        }
        char out[100];
        size_t sz = 1 + rnd32() % sizeof out;
        uint16_t u = GV[rnd32() % (sizeof GV / sizeof GV[0])];
        if (bl_value_format(u, d, len, out, sz, trs[it % 3]) && !utf8_ok(out, sz)) { CHECK(0); printf("  bad value 0x%04X\n", u); }
        if (bl_value_text(d, len, out, sz) && !utf8_ok(out, sz)) CHECK(0);
        bl_hex(d, len, out, sz);
        if (strlen(out) >= sz) CHECK(0);
        bl_uuid_str(d, (int)(rnd32() % 20), out, sz);
        if (strlen(out) >= sz) CHECK(0);
        if (len >= 6) (void)bl_addr_kind(d, d[0] & 3);
    }
    g_pass++;
    printf("fuzz: %d packets of noise\n", iters);
}

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 200000;
    test_merge();
    test_explain();
    test_bthome();
    test_thermometers();
    test_beacons();
    test_classify();
    test_names();
    test_values();
    test_text();
    test_distance();
    test_crypto();
    test_sim();
    fuzz(iters);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
