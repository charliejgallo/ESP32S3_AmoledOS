/*
 * BLE - what the bytes of an advertisement mean.
 *
 * Everything here works on the bytes as they came off the air and never
 * trusts a length: each reader checks that what it is about to read is
 * inside what it was given, and a format that does not add up is simply not
 * that format. No heap, no double (the P4's FPU is single precision and the
 * .so's symbol table has no soft-double helpers), no 64-bit integers; the
 * numbers are printed in fixed point by hand because "%f" would promote to
 * double.
 *
 * The formats were written from their specifications, not from captures,
 * and apps/ble/test checks each one against packets built by hand from
 * those same documents.
 */
#include "bl_decode.h"
#include "bl_crypt.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define COUNTOF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- bytes ---- */

static inline uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint16_t u16be(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline int16_t s16le(const uint8_t *p) { return (int16_t)u16le(p); }
static inline int16_t s16be(const uint8_t *p) { return (int16_t)u16be(p); }
static inline uint32_t u24le(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
static inline uint32_t u32le(const uint8_t *p) { return u24le(p) | (uint32_t)p[3] << 24; }
/* Built without optimisation on the watch: gcc takes these four shifts for a
 * byte swap and calls __bswapsi2, which AmoledOS's firmware does not lend to
 * the apps (docs/APP-GUIDE.md). */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, optimize("O0")))
#endif
static uint32_t u32be(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static const char *T(bl_tr_fn tr, const char *s) { return tr ? tr(s) : s; }

static const char HX[] = "0123456789ABCDEF";

/* ---- a string that fills up without ever splitting a character ---- */

typedef struct {
    char *p;
    size_t sz, len;
    bool full;
} sb_t;

static void sb_init(sb_t *b, char *p, size_t sz)
{
    b->p = p;
    b->sz = sz;
    b->len = 0;
    b->full = sz == 0;
    if (sz) p[0] = 0;
}

/* The largest cut at or below n that does not land inside a UTF-8 sequence
 * (s[n] must exist). */
static size_t utf8_cut(const char *s, size_t n)
{
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

static void sb_cat(sb_t *b, const char *s)
{
    if (b->full || !s) return;
    size_t n = strlen(s), room = b->sz - 1 - b->len;
    if (n <= room) {
        memcpy(b->p + b->len, s, n);
        b->len += n;
        b->p[b->len] = 0;
        return;
    }
    /* does not fit: what fits, cut on a character, and an ellipsis */
    b->full = true;
    size_t keep = utf8_cut(s, room >= 3 ? room - 3 : 0);
    memcpy(b->p + b->len, s, keep);
    b->len += keep;
    if (room >= 3) {
        memcpy(b->p + b->len, "\xE2\x80\xA6", 3);
        b->len += 3;
    }
    b->p[b->len] = 0;
}

/* Only for numbers and ASCII: a %s with UTF-8 goes through sb_cat. */
static void sb_catf(sb_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb_catf(sb_t *b, const char *fmt, ...)
{
    char t[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    sb_cat(b, t);
}

static void sb_sep(sb_t *b, const char *sep)
{
    if (b->len) sb_cat(b, sep);
}

static const int32_t P10[] = { 1, 10, 100, 1000, 10000, 100000, 1000000 };

/* v / 10^dec with dec decimals and a decimal comma: 2730, 2 -> "27,30". */
static void sb_fix(sb_t *b, int32_t v, int dec)
{
    uint32_t a = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    if (!dec) sb_catf(b, "%s%u", v < 0 ? "-" : "", (unsigned)a);
    else sb_catf(b, "%s%u,%0*u", v < 0 ? "-" : "", (unsigned)(a / (uint32_t)P10[dec]), dec,
                 (unsigned)(a % (uint32_t)P10[dec]));
}

static void sb_flt(sb_t *b, float x, int dec)
{
    if (x != x) { sb_cat(b, "?"); return; }
    if (dec < 0) dec = 0;
    if (dec > 6) dec = 6;
    while (dec > 0 && fabsf(x * (float)P10[dec]) > 2.0e9f) dec--;
    float s = x * (float)P10[dec];
    if (s > 2.0e9f || s < -2.0e9f) { sb_cat(b, x < 0 ? "-\xE2\x88\x9E" : "\xE2\x88\x9E"); return; }
    sb_fix(b, (int32_t)(s + (s < 0 ? -0.5f : 0.5f)), dec);
}

static void sb_hex(sb_t *b, const uint8_t *v, int n)
{
    for (int i = 0; i < n && !b->full; i++) {
        size_t room = b->sz - 1 - b->len;
        bool last = i == n - 1;
        /* a byte goes in whole, and only if an ellipsis still fits after it */
        if (room < (last ? 2u : 6u)) {
            if (room >= 3) {
                memcpy(b->p + b->len, "\xE2\x80\xA6", 3);
                b->len += 3;
                b->p[b->len] = 0;
            }
            b->full = true;
            return;
        }
        b->p[b->len++] = HX[v[i] >> 4];
        b->p[b->len++] = HX[v[i] & 15];
        if (!last) b->p[b->len++] = ' ';
        b->p[b->len] = 0;
    }
}

/* An address as it travels (little endian) in its written form. */
static void sb_addr_le(sb_t *b, const uint8_t *p)
{
    sb_catf(b, "%02X:%02X:%02X:%02X:%02X:%02X", p[5], p[4], p[3], p[2], p[1], p[0]);
}

/* ---- UTF-8 ---- */

/* The length of the valid UTF-8 sequence at s (n bytes left), 0 if it is not one. */
static int u8seq(const uint8_t *s, int n)
{
    uint8_t c = s[0], lo = 0x80, hi = 0xBF;
    int k;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) k = 2;
    else if (c >= 0xE0 && c <= 0xEF) {
        k = 3;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        k = 4;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else return 0;
    if (n < k || s[1] < lo || s[1] > hi) return 0;
    for (int i = 2; i < k; i++)
        if ((s[i] & 0xC0) != 0x80) return 0;
    return k;
}

/* A name off the air into something a label can show: stops at a NUL, a
 * broken sequence or a control character becomes '?', and it is cut on a
 * whole character. */
static void name_copy(char *d, size_t sz, const uint8_t *s, int n)
{
    size_t o = 0;
    for (int i = 0; i < n && s[i];) {
        int k = u8seq(s + i, n - i);
        const uint8_t *src = s + i;
        int w = k;
        static const uint8_t q = '?';
        if (k == 0) { src = &q; w = 1; k = 1; }
        else if (k == 1 && (s[i] < 0x20 || s[i] == 0x7F)) src = &q;
        if (o + (size_t)w > sz - 1) break;
        memcpy(d + o, src, (size_t)w);
        o += (size_t)w;
        i += k;
    }
    d[o] = 0;
}

/* ---- merging the AD structures ---- */

void bl_ad_clear(bl_ad_t *out)
{
    if (out) memset(out, 0, sizeof *out);
}

static void add_u16(bl_ad_t *a, uint16_t u)
{
    if (a->n16 < 0 || a->n16 > BL_MAX_U16) a->n16 = 0;
    for (int i = 0; i < a->n16; i++)
        if (a->u16[i] == u) return;
    if (a->n16 < BL_MAX_U16) a->u16[a->n16++] = u;
}

static void add_u128(bl_ad_t *a, const uint8_t *u)
{
    if (a->n128 < 0 || a->n128 > BL_MAX_U128) a->n128 = 0;
    for (int i = 0; i < a->n128; i++)
        if (!memcmp(a->u128[i], u, 16)) return;
    if (a->n128 < BL_MAX_U128) memcpy(a->u128[a->n128++], u, 16);
}

/* The key of a service data: 16-bit as is, 32-bit by its low half (the
 * part that is a 16-bit UUID when the top is zero), 128-bit by bytes 12-13
 * (where a UUID on the SIG's base keeps its 16 bits). */
static uint16_t sd_key(const uint8_t *p, int ul)
{
    return ul == 16 ? u16le(p + 12) : u16le(p);
}

static void add_sd(bl_ad_t *a, const uint8_t *p, int n, int ul)
{
    if (n < ul) return;
    if (a->nsd < 0 || a->nsd > BL_MAX_SD) a->nsd = 0;
    uint16_t key = sd_key(p, ul);
    bl_sd_t *s = NULL;
    for (int i = 0; i < a->nsd; i++)
        if (a->sd[i].uuid == key && a->sd[i].uuid_len == ul) s = &a->sd[i];
    if (!s) {
        /* full: the oldest goes, so what keeps changing its key is not frozen */
        if (a->nsd >= BL_MAX_SD) {
            memmove(&a->sd[0], &a->sd[1], sizeof a->sd[0] * (BL_MAX_SD - 1));
            a->nsd = BL_MAX_SD - 1;
        }
        s = &a->sd[a->nsd++];
    }
    int l = n - ul;
    if (l > (int)sizeof s->data) l = (int)sizeof s->data;
    memset(s, 0, sizeof *s);
    s->uuid = key;
    s->uuid_len = (uint8_t)ul;
    s->len = (uint8_t)l;
    memcpy(s->data, p + ul, (size_t)l);
}

static bool inkbird_name(const char *n) { return !strcmp(n, "sps") || !strcmp(n, "tps"); }

static void add_mfg(bl_ad_t *a, const uint8_t *p, int n)
{
    if (n < 2) return;
    if (a->nmfg < 0 || a->nmfg > BL_MAX_MFG) a->nmfg = 0;
    /* Inkbird's "company" is its temperature: keyed by it, an old reading
     * could come back to life when the temperature returns to its value.
     * Once its name is known it gets one slot, the newest. */
    if (inkbird_name(a->name)) a->nmfg = 0;
    uint16_t c = u16le(p);
    bl_mfg_t *m = NULL;
    for (int i = 0; i < a->nmfg; i++)
        if (a->mfg[i].company == c) m = &a->mfg[i];
    if (!m) {
        /* full: the oldest goes. Inkbird writes its temperature where the
         * company goes, so its "company" is new in nearly every packet. */
        if (a->nmfg >= BL_MAX_MFG) {
            memmove(&a->mfg[0], &a->mfg[1], sizeof a->mfg[0] * (BL_MAX_MFG - 1));
            a->nmfg = BL_MAX_MFG - 1;
        }
        m = &a->mfg[a->nmfg++];
    }
    int l = n - 2;
    if (l > (int)sizeof m->data) l = (int)sizeof m->data;
    memset(m, 0, sizeof *m);
    m->company = c;
    m->len = (uint8_t)l;
    memcpy(m->data, p + 2, (size_t)l);
}

void bl_ad_merge(bl_ad_t *a, const uint8_t *d, int len)
{
    if (!a || !d || len <= 0) return;
    int i = 0;
    while (i < len) {
        int l = d[i];
        if (l == 0) break;                  /* the rest is padding */
        if (i + 1 + l > len) { a->malformed = true; break; }
        uint8_t t = d[i + 1];
        const uint8_t *p = d + i + 2;
        int n = l - 1;
        switch (t) {
        case 0x01:
            if (n >= 1) { a->has_flags = true; a->flags = p[0]; }
            break;
        case 0x02: case 0x03: case 0x14:
            for (int k = 0; k + 1 < n; k += 2) add_u16(a, u16le(p + k));
            break;
        case 0x04: case 0x05: case 0x1F:
            /* a 32-bit UUID on the base is a 16-bit one written long */
            for (int k = 0; k + 3 < n; k += 4)
                if (!p[k + 2] && !p[k + 3]) add_u16(a, u16le(p + k));
            break;
        case 0x06: case 0x07:
            for (int k = 0; k + 15 < n; k += 16) add_u128(a, p + k);
            break;
        case 0x08:
            if (!a->name_complete || !a->name[0]) {
                name_copy(a->name, sizeof a->name, p, n);
                a->name_complete = false;
            }
            break;
        case 0x09:
            name_copy(a->name, sizeof a->name, p, n);
            a->name_complete = true;
            break;
        case 0x0A:
            if (n >= 1) { a->has_tx = true; a->tx_power = (int8_t)p[0]; }
            break;
        case 0x16: add_sd(a, p, n, 2); break;
        case 0x20: add_sd(a, p, n, 4); break;
        case 0x21: add_sd(a, p, n, 16); break;
        case 0x19:
            if (n >= 2) { a->has_appearance = true; a->appearance = u16le(p); }
            break;
        case 0x1A:
            if (n >= 2) { a->has_interval = true; a->interval = u16le(p); }
            break;
        case 0xFF: add_mfg(a, p, n); break;
        default: break;
        }
        i += 1 + l;
    }
}

/* ---- UUIDs and addresses ---- */

void bl_uuid_str(const uint8_t *u, int len, char *out, size_t n)
{
    if (!out || !n) return;
    out[0] = 0;
    if (!u) return;
    char t[40];
    if (len == 2) snprintf(t, sizeof t, "0x%04X", (unsigned)u16le(u));
    else if (len == 4) snprintf(t, sizeof t, "0x%08X", (unsigned)u32le(u));
    else if (len == 16) {
        int k = 0;
        for (int i = 15; i >= 0; i--) {
            t[k++] = HX[u[i] >> 4];
            t[k++] = HX[u[i] & 15];
            if (i == 12 || i == 10 || i == 8 || i == 6) t[k++] = '-';
        }
        t[k] = 0;
    } else {
        sb_t b;
        sb_init(&b, t, sizeof t);
        sb_hex(&b, u, len > 12 ? 12 : len);
    }
    sb_t b;
    sb_init(&b, out, n);
    sb_cat(&b, t);
}

bl_addr_kind_t bl_addr_kind(const uint8_t addr[6], uint8_t addr_type)
{
    /* NimBLE's 2 and 3 are the identity addresses behind a resolved RPA */
    if (addr_type == 0 || addr_type == 2) return BL_ADDR_PUBLIC;
    if (addr_type == 3) return BL_ADDR_STATIC;
    if (!addr) return BL_ADDR_NRPA;
    switch (addr[0] >> 6) {
    case 3: return BL_ADDR_STATIC;
    case 1: return BL_ADDR_RPA;
    default: return BL_ADDR_NRPA;          /* 00, and the reserved 10 */
    }
}

const char *bl_addr_kind_name(bl_addr_kind_t k)
{
    switch (k) {
    case BL_ADDR_PUBLIC: return N_("Pública");
    case BL_ADDR_STATIC: return N_("Aleatoria estática");
    case BL_ADDR_RPA: return N_("Privada resoluble");
    case BL_ADDR_NRPA: return N_("Privada no resoluble");
    }
    return N_("Desconocido");
}

/* ---- text and hex ---- */

bool bl_value_text(const uint8_t *v, int n, char *out, size_t sz)
{
    if (!out || !sz) return false;
    out[0] = 0;
    if (!v || n <= 0) return false;
    while (n > 0 && v[n - 1] == 0) n--;     /* a C string sent with its NUL */
    if (n <= 0) return false;
    for (int i = 0; i < n;) {
        int k = u8seq(v + i, n - i);
        if (!k) return false;
        if (k == 1 && ((v[i] < 0x20 && v[i] != '\t' && v[i] != '\n' && v[i] != '\r') || v[i] == 0x7F)) return false;
        i += k;
    }
    size_t c = (size_t)n;
    if (c > sz - 1) c = utf8_cut((const char *)v, sz - 1);
    memcpy(out, v, c);
    out[c] = 0;
    return true;
}

void bl_hex(const uint8_t *v, int n, char *out, size_t sz)
{
    if (!out || !sz) return;
    sb_t b;
    sb_init(&b, out, sz);
    if (v && n > 0) sb_hex(&b, v, n);
}

/* ---- sensors ---- */

static void sen_set(bl_sensor_t *o, const char *fmt)
{
    if (!o->format) o->format = fmt;
}

/* BTHome objects: id, size, signedness, factor and where the value goes. */
enum {
    W_SKIP, W_PKT, W_BATT, W_TEMP, W_HUM, W_PRESS, W_LUX, W_KG, W_LB, W_DEW, W_COUNT, W_ENERGY,
    W_POWER, W_VOLT, W_PM25, W_CO2, W_TVOC, W_MOIST, W_OPEN, W_MOTION, W_BUTTON, W_ROT, W_MM,
    W_M, W_COND,
};

typedef struct {
    uint8_t id, size;                       /* size 0: the next byte says it */
    bool sign;
    uint8_t what;
    float f;
} bth_obj_t;

static const bth_obj_t BTH[] = {
    { 0x00, 1, false, W_PKT, 1 },
    { 0x01, 1, false, W_BATT, 1 },
    { 0x02, 2, true, W_TEMP, 0.01f },
    { 0x03, 2, false, W_HUM, 0.01f },
    { 0x04, 3, false, W_PRESS, 0.01f },
    { 0x05, 3, false, W_LUX, 0.01f },
    { 0x06, 2, false, W_KG, 0.01f },
    { 0x07, 2, false, W_LB, 0.01f },
    { 0x08, 2, true, W_DEW, 0.01f },
    { 0x09, 1, false, W_COUNT, 1 },
    { 0x0A, 3, false, W_ENERGY, 0.001f },
    { 0x0B, 3, false, W_POWER, 0.01f },
    { 0x0C, 2, false, W_VOLT, 0.001f },
    { 0x0D, 2, false, W_PM25, 1 },
    { 0x0E, 2, false, W_SKIP, 1 },          /* PM10 */
    { 0x0F, 1, false, W_SKIP, 1 },          /* generic boolean */
    { 0x10, 1, false, W_SKIP, 1 },          /* power on/off */
    { 0x11, 1, false, W_OPEN, 1 },          /* opening */
    { 0x12, 2, false, W_CO2, 1 },
    { 0x13, 2, false, W_TVOC, 1 },
    { 0x14, 2, false, W_MOIST, 0.01f },
    { 0x15, 1, false, W_SKIP, 1 },          /* battery low */
    { 0x16, 1, false, W_SKIP, 1 },          /* battery charging */
    { 0x17, 1, false, W_SKIP, 1 },          /* carbon monoxide */
    { 0x18, 1, false, W_SKIP, 1 },          /* cold */
    { 0x19, 1, false, W_SKIP, 1 },          /* connectivity */
    { 0x1A, 1, false, W_OPEN, 1 },          /* door */
    { 0x1B, 1, false, W_OPEN, 1 },          /* garage door */
    { 0x1C, 1, false, W_SKIP, 1 },          /* gas */
    { 0x1D, 1, false, W_SKIP, 1 },          /* heat */
    { 0x1E, 1, false, W_SKIP, 1 },          /* light */
    { 0x1F, 1, false, W_SKIP, 1 },          /* lock */
    { 0x20, 1, false, W_SKIP, 1 },          /* moisture */
    { 0x21, 1, false, W_MOTION, 1 },        /* motion */
    { 0x22, 1, false, W_MOTION, 1 },        /* moving */
    { 0x23, 1, false, W_MOTION, 1 },        /* occupancy */
    { 0x24, 1, false, W_SKIP, 1 },          /* plug */
    { 0x25, 1, false, W_MOTION, 1 },        /* presence */
    { 0x26, 1, false, W_SKIP, 1 },          /* problem */
    { 0x27, 1, false, W_SKIP, 1 },          /* running */
    { 0x28, 1, false, W_SKIP, 1 },          /* safety */
    { 0x29, 1, false, W_SKIP, 1 },          /* smoke */
    { 0x2A, 1, false, W_SKIP, 1 },          /* sound */
    { 0x2B, 1, false, W_SKIP, 1 },          /* tamper */
    { 0x2C, 1, false, W_SKIP, 1 },          /* vibration */
    { 0x2D, 1, false, W_OPEN, 1 },          /* window */
    { 0x2E, 1, false, W_HUM, 1 },
    { 0x2F, 1, false, W_MOIST, 1 },
    { 0x3A, 1, false, W_BUTTON, 1 },
    { 0x3C, 2, false, W_SKIP, 1 },          /* dimmer: event and steps */
    { 0x3D, 2, false, W_COUNT, 1 },
    { 0x3E, 4, false, W_COUNT, 1 },
    { 0x3F, 2, true, W_ROT, 0.1f },
    { 0x40, 2, false, W_MM, 1 },
    { 0x41, 2, false, W_M, 0.1f },
    { 0x42, 3, false, W_SKIP, 0.001f },     /* duration, s */
    { 0x43, 2, false, W_SKIP, 0.001f },     /* current, A */
    { 0x44, 2, false, W_SKIP, 0.01f },      /* speed, m/s */
    { 0x45, 2, true, W_TEMP, 0.1f },
    { 0x46, 1, false, W_SKIP, 0.1f },       /* UV index */
    { 0x47, 2, false, W_SKIP, 0.1f },       /* volume, L */
    { 0x48, 2, false, W_SKIP, 1 },          /* volume, mL */
    { 0x49, 2, false, W_SKIP, 0.001f },     /* volume flow, m3/h */
    { 0x4A, 2, false, W_VOLT, 0.1f },
    { 0x4B, 3, false, W_SKIP, 0.001f },     /* gas, m3 */
    { 0x4C, 4, false, W_SKIP, 0.001f },     /* gas, m3 */
    { 0x4D, 4, false, W_ENERGY, 0.001f },
    { 0x4E, 4, false, W_SKIP, 0.001f },     /* volume, L */
    { 0x4F, 4, false, W_SKIP, 0.001f },     /* water, L */
    { 0x50, 4, false, W_SKIP, 1 },          /* timestamp */
    { 0x51, 2, false, W_SKIP, 0.001f },     /* acceleration, m/s2 */
    { 0x52, 2, false, W_SKIP, 0.001f },     /* gyroscope, deg/s */
    { 0x53, 0, false, W_SKIP, 1 },          /* text */
    { 0x54, 0, false, W_SKIP, 1 },          /* raw */
    { 0x55, 4, false, W_SKIP, 0.001f },     /* volume storage, L */
    { 0x56, 2, false, W_COND, 1 },
    { 0x57, 1, true, W_TEMP, 1 },
    { 0x58, 1, true, W_TEMP, 0.35f },
    { 0x59, 1, true, W_COUNT, 1 },
    { 0x5A, 2, true, W_COUNT, 1 },
    { 0x5B, 4, true, W_COUNT, 1 },
    { 0x5C, 4, true, W_POWER, 0.01f },
    { 0x5D, 2, true, W_SKIP, 0.001f },      /* current, A */
    { 0x5E, 2, false, W_SKIP, 0.01f },      /* direction, deg */
    { 0x5F, 2, false, W_SKIP, 0.1f },       /* precipitation, mm */
    { 0x60, 1, false, W_SKIP, 1 },          /* channel */
    { 0x61, 2, false, W_SKIP, 1 },          /* rotational speed, rpm */
    { 0xF0, 2, false, W_SKIP, 1 },          /* device type id */
    { 0xF1, 4, false, W_SKIP, 1 },          /* firmware version */
    { 0xF2, 3, false, W_SKIP, 1 },          /* firmware version */
};

static const bth_obj_t *bth_find(uint8_t id)
{
    int lo = 0, hi = COUNTOF(BTH) - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (BTH[mid].id == id) return &BTH[mid];
        if (BTH[mid].id < id) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

/* An integer of 1-4 bytes, little endian, into an int32 of its sign. */
static int32_t le_int(const uint8_t *p, int size, bool sign)
{
    uint32_t v = 0;
    for (int i = 0; i < size; i++) v |= (uint32_t)p[i] << (8 * i);
    if (sign && size < 4 && (v & (1u << (8 * size - 1)))) v |= ~0u << (8 * size);
    return (int32_t)v;
}

static void bth_put(bl_sensor_t *o, const bth_obj_t *ob, int32_t raw, bool sign)
{
    float x = sign ? (float)raw * ob->f : (float)(uint32_t)raw * ob->f;
    switch (ob->what) {
    case W_PKT:
        if (!(o->mask & BL_V_COUNT)) { o->count = raw; o->mask |= BL_V_COUNT; }
        break;
    case W_BATT: o->batt = raw; o->mask |= BL_V_BATT; break;
    case W_TEMP: o->temp = x; o->mask |= BL_V_TEMP; break;
    case W_HUM: o->hum = x; o->mask |= BL_V_HUM; break;
    case W_PRESS: o->press = x; o->mask |= BL_V_PRESS; break;
    case W_LUX: o->lux = x; o->mask |= BL_V_LUX; break;
    case W_KG: o->weight = x; o->mask |= BL_V_WEIGHT; break;
    case W_LB: o->weight = x * 0.45359237f; o->mask |= BL_V_WEIGHT; break;
    case W_DEW: o->dew = x; o->mask |= BL_V_DEW; break;
    case W_COUNT: o->count = raw; o->mask |= BL_V_COUNT; break;
    case W_ENERGY: o->energy = x; o->mask |= BL_V_ENERGY; break;
    case W_POWER: o->power = x; o->mask |= BL_V_POWER; break;
    case W_VOLT: o->volt = x; o->mask |= BL_V_VOLT; break;
    case W_PM25: o->pm25 = x; o->mask |= BL_V_PM25; break;
    case W_CO2: o->co2 = x; o->mask |= BL_V_CO2; break;
    case W_TVOC: o->tvoc = x; o->mask |= BL_V_TVOC; break;
    case W_MOIST: o->moist = x; o->mask |= BL_V_MOIST; break;
    case W_OPEN: o->open = raw != 0; o->mask |= BL_V_OPEN; break;
    case W_MOTION: o->motion = raw != 0; o->mask |= BL_V_MOTION; break;
    case W_BUTTON: o->button = raw; o->mask |= BL_V_BUTTON; break;
    case W_ROT: o->rotation = x; o->mask |= BL_V_ROTATION; break;
    case W_MM: o->dist = x; o->mask |= BL_V_DIST; break;
    case W_M: o->dist = x * 1000.0f; o->mask |= BL_V_DIST; break;
    case W_COND: o->cond = x; o->mask |= BL_V_COND; break;
    default: break;
    }
}

/* BTHome v2: a device info byte, then objects of known sizes. An unknown
 * id ends the parse there: its size is unknown, and guessing would read
 * the rest out of step. */
static bool sen_bthome2(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n < 1 || (d[0] >> 5) != 2) return false;
    sen_set(o, "BTHome v2");
    if (d[0] & 0x01) { o->encrypted = true; return true; }
    int i = 1;
    while (i < n) {
        const bth_obj_t *ob = bth_find(d[i]);
        if (!ob) break;
        int size = ob->size;
        if (!size) {
            if (i + 1 >= n) break;
            size = 1 + d[i + 1];
        }
        if (i + 1 + size > n) break;
        if (ob->size) bth_put(o, ob, le_int(d + i + 1, size, ob->sign), ob->sign);
        i += 1 + size;
    }
    return true;
}

/* BTHome v1: each object says its length (with the id) in the low five bits
 * and its format in the top three: 0 unsigned, 1 signed, 2 float, 3 text. */
static bool sen_bthome1(const uint8_t *d, int n, bool encrypted, bl_sensor_t *o)
{
    if (n < 2) return false;
    sen_set(o, "BTHome v1");
    if (encrypted) { o->encrypted = true; return true; }
    int i = 0;
    bool any = false;
    while (i + 1 < n) {
        int len = d[i] & 0x1F, fmt = d[i] >> 5;
        if (len < 1 || i + 1 + len > n) break;
        int size = len - 1;
        const bth_obj_t *ob = bth_find(d[i + 1]);
        if (ob && ob->size && size >= 1 && size <= 4 && fmt <= 1) {
            bth_put(o, ob, le_int(d + i + 2, size, fmt == 1), fmt == 1);
            any = true;
        }
        i += 1 + len;
    }
    return any || i == n;
}

/* pvvx's custom format: MAC (little endian), temp s16 0.01, hum u16 0.01,
 * battery mV, battery %, counter, flags. */
static bool sen_pvvx(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n != 15) return false;
    sen_set(o, "pvvx");
    o->temp = s16le(d + 6) * 0.01f;
    o->hum = u16le(d + 8) * 0.01f;
    o->volt = u16le(d + 10) * 0.001f;
    o->batt = d[12];
    o->count = d[13];
    o->mask |= BL_V_TEMP | BL_V_HUM | BL_V_VOLT | BL_V_BATT | BL_V_COUNT;
    return true;
}

/* ATC1441's: MAC (big endian), temp s16 BE 0.1, hum %, battery %, battery
 * mV BE, counter. */
static bool sen_atc(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n != 13) return false;
    sen_set(o, "ATC");
    o->temp = s16be(d + 6) * 0.1f;
    o->hum = d[8];
    o->batt = d[9];
    o->volt = u16be(d + 10) * 0.001f;
    o->count = d[12];
    o->mask |= BL_V_TEMP | BL_V_HUM | BL_V_VOLT | BL_V_BATT | BL_V_COUNT;
    return true;
}

/* Xiaomi's MiBeacon (0xFE95): frame control, product id, counter, then what
 * the frame control says is there. */
static bool sen_mibeacon(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n < 5) return false;
    uint16_t fc = u16le(d);
    sen_set(o, "MiBeacon");
    if (fc & 0x0008) { o->encrypted = true; return true; }
    int i = 5;
    if (fc & 0x0010) i += 6;                /* MAC */
    if (fc & 0x0020) {                      /* capability, and maybe its I/O word */
        if (i >= n) return true;
        if (d[i] & 0x20) i += 2;
        i += 1;
    }
    if (!(fc & 0x0040)) return true;        /* nothing measured in this one */
    while (i + 3 <= n) {
        uint16_t id = u16le(d + i);
        int l = d[i + 2];
        const uint8_t *v = d + i + 3;
        if (i + 3 + l > n) break;
        switch (id) {
        case 0x1004: if (l >= 2) { o->temp = s16le(v) * 0.1f; o->mask |= BL_V_TEMP; } break;
        case 0x1006: if (l >= 2) { o->hum = u16le(v) * 0.1f; o->mask |= BL_V_HUM; } break;
        case 0x100A: if (l >= 1) { o->batt = v[0]; o->mask |= BL_V_BATT; } break;
        case 0x100D:
            if (l >= 4) {
                o->temp = s16le(v) * 0.1f;
                o->hum = u16le(v + 2) * 0.1f;
                o->mask |= BL_V_TEMP | BL_V_HUM;
            }
            break;
        case 0x1007: if (l >= 3) { o->lux = (float)u24le(v); o->mask |= BL_V_LUX; } break;
        case 0x1008: if (l >= 1) { o->moist = v[0]; o->mask |= BL_V_MOIST; } break;
        case 0x1009: if (l >= 2) { o->cond = u16le(v); o->mask |= BL_V_COND; } break;
        case 0x000F:                        /* someone moving, with the light */
            if (l >= 3) {
                o->motion = 1;
                o->lux = (float)u24le(v);
                o->mask |= BL_V_MOTION | BL_V_LUX;
            }
            break;
        case 0x1019:                        /* door: 0 open, 1 closed, 2 left open */
            if (l >= 1) { o->open = v[0] != 1; o->mask |= BL_V_OPEN; }
            break;
        /* the newer ids, v5 firmwares: a float temperature, one-byte
         * humidity and battery, a float humidity */
        case 0x4C01: if (l >= 4) { float f; memcpy(&f, v, 4); o->temp = f; o->mask |= BL_V_TEMP; } break;
        case 0x4C02: if (l >= 1) { o->hum = v[0]; o->mask |= BL_V_HUM; } break;
        case 0x4C03: if (l >= 1) { o->batt = v[0]; o->mask |= BL_V_BATT; } break;
        case 0x4C08: if (l >= 4) { float f; memcpy(&f, v, 4); o->hum = f; o->mask |= BL_V_HUM; } break;
        default: break;
        }
        i += 3 + l;
    }
    return true;
}

/* Govee's three packed bytes: temperature x 10000 + humidity x 10, the top
 * bit the sign. */
static void govee_packed(const uint8_t *p, int batt, bl_sensor_t *o)
{
    uint32_t v = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    bool neg = v & 0x800000;
    v &= 0x7FFFFF;
    o->temp = (float)(v / 1000) / 10.0f * (neg ? -1.0f : 1.0f);
    o->hum = (float)(v % 1000) / 10.0f;
    o->batt = batt & 0x7F;
    o->mask |= BL_V_TEMP | BL_V_HUM | BL_V_BATT;
}

static bool has_ci(const char *hay, const char *needle);

static bool sen_govee(const bl_mfg_t *m, const bl_ad_t *ad, bl_sensor_t *o)
{
    const uint8_t *d = m->data;
    if (m->company == 0xEC88 && m->len == 6) {
        /* H5072, H5075: 00, the packed bytes, battery, 00 */
        sen_set(o, "Govee");
        govee_packed(d + 1, d[4], o);
        return true;
    }
    if (m->company == 0xEC88 && (m->len == 7 || m->len == 9) &&
        (has_ci(ad->name, "5074") || has_ci(ad->name, "5051") || has_ci(ad->name, "5052") ||
         has_ci(ad->name, "5071"))) {
        /* H5074 and kin: 00, temp s16 0.01, hum u16 0.01, battery */
        sen_set(o, "Govee");
        o->temp = s16le(d + 1) * 0.01f;
        o->hum = u16le(d + 3) * 0.01f;
        o->batt = d[5];
        o->mask |= BL_V_TEMP | BL_V_HUM | BL_V_BATT;
        return true;
    }
    if (m->company == 0x0001 && m->len == 6 && (has_ci(ad->name, "GVH51") || has_ci(ad->name, "Govee_H51"))) {
        /* H5101, H5102, H5174, H5177 borrow Nokia's id: 01 01, packed, battery */
        sen_set(o, "Govee");
        govee_packed(d + 2, d[5], o);
        return true;
    }
    return false;
}

/* Ruuvi: RAWv2 (format 5) and RAWv1 (format 3), both big endian. */
static bool sen_ruuvi(const bl_mfg_t *m, bl_sensor_t *o)
{
    const uint8_t *d = m->data;
    if (m->len >= 24 && d[0] == 5) {
        sen_set(o, "Ruuvi RAWv2");
        int16_t t = s16be(d + 1);
        uint16_t h = u16be(d + 3), p = u16be(d + 5), pw = u16be(d + 13), seq = u16be(d + 16);
        if (t != (int16_t)0x8000) { o->temp = t * 0.005f; o->mask |= BL_V_TEMP; }
        if (h != 0xFFFF) { o->hum = h * 0.0025f; o->mask |= BL_V_HUM; }
        if (p != 0xFFFF) { o->press = ((float)p + 50000.0f) / 100.0f; o->mask |= BL_V_PRESS; }
        int16_t ax = s16be(d + 7), ay = s16be(d + 9), az = s16be(d + 11);
        if (ax != (int16_t)0x8000 && ay != (int16_t)0x8000 && az != (int16_t)0x8000) {
            o->acc_x = ax * 0.001f;
            o->acc_y = ay * 0.001f;
            o->acc_z = az * 0.001f;
            o->mask |= BL_V_ACC;
        }
        if ((pw >> 5) != 0x7FF) { o->volt = ((pw >> 5) + 1600) * 0.001f; o->mask |= BL_V_VOLT; }
        if (seq != 0xFFFF) { o->count = seq; o->mask |= BL_V_COUNT; }
        return true;
    }
    if (m->len >= 14 && d[0] == 3) {
        sen_set(o, "Ruuvi RAWv1");
        o->hum = d[1] * 0.5f;
        o->temp = ((d[2] & 0x7F) + d[3] * 0.01f) * ((d[2] & 0x80) ? -1.0f : 1.0f);
        o->press = ((float)u16be(d + 4) + 50000.0f) / 100.0f;
        o->acc_x = s16be(d + 6) * 0.001f;
        o->acc_y = s16be(d + 8) * 0.001f;
        o->acc_z = s16be(d + 10) * 0.001f;
        o->volt = u16be(d + 12) * 0.001f;
        o->mask |= BL_V_HUM | BL_V_TEMP | BL_V_PRESS | BL_V_ACC | BL_V_VOLT;
        return true;
    }
    return false;
}

static const bl_mfg_t *find_mfg(const bl_ad_t *ad, uint16_t company)
{
    for (int i = 0; i < ad->nmfg && i < BL_MAX_MFG; i++)
        if (ad->mfg[i].company == company) return &ad->mfg[i];
    return NULL;
}

/* SwitchBot's three bytes of temperature and humidity: tenths in the low
 * nibble, whole degrees in seven bits with the sign on top (1 positive),
 * humidity in seven bits. */
static void switchbot_th(const uint8_t *p, bl_sensor_t *o)
{
    float t = (float)(p[1] & 0x7F) + (float)(p[0] & 0x0F) / 10.0f;
    o->temp = (p[1] & 0x80) ? t : -t;
    o->hum = p[2] & 0x7F;
    o->mask |= BL_V_TEMP | BL_V_HUM;
}

static bool sen_switchbot(const uint8_t *d, int n, const bl_ad_t *ad, bl_sensor_t *o)
{
    if (n < 3) return false;
    int type = d[0] & 0x7F;
    if (type != 'T' && type != 'i' && type != 'w') return false;
    sen_set(o, "SwitchBot");
    o->batt = d[2] & 0x7F;
    o->mask |= BL_V_BATT;
    /* newer firmware (and the outdoor 'w' always) put the reading in the
     * manufacturer data, after the MAC and two more bytes */
    const bl_mfg_t *m = find_mfg(ad, 0x0969);
    if (m && m->len >= 11) switchbot_th(m->data + 8, o);
    else if (type != 'w' && n >= 6) switchbot_th(d + 3, o);
    return true;
}

/* Qingping: flags, device type, MAC (little endian), then id-length-value. */
static bool sen_qingping(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n < 8) return false;
    sen_set(o, "Qingping");
    int i = 8;
    while (i + 2 <= n) {
        int id = d[i], l = d[i + 1];
        const uint8_t *v = d + i + 2;
        if (i + 2 + l > n) break;
        if (id == 0x01 && l == 4) {
            o->temp = s16le(v) * 0.1f;
            o->hum = u16le(v + 2) * 0.1f;
            o->mask |= BL_V_TEMP | BL_V_HUM;
        } else if (id == 0x02 && l == 1) {
            o->batt = v[0];
            o->mask |= BL_V_BATT;
        } else if (id == 0x07 && l == 2) {
            o->press = u16le(v) * 0.1f;
            o->mask |= BL_V_PRESS;
        } else if (id == 0x12 && l == 4) {
            o->pm25 = u16le(v);
            o->mask |= BL_V_PM25;
        } else if (id == 0x13 && l == 2) {
            o->co2 = u16le(v);
            o->mask |= BL_V_CO2;
        }
        i += 2 + l;
    }
    return true;
}

/* Inkbird IBS-TH1/TH2: nine bytes of manufacturer data where the company id
 * is really the temperature; humidity, the probe, a CRC and the battery
 * follow. */
static bool sen_inkbird(const bl_mfg_t *m, const bl_ad_t *ad, bl_sensor_t *o)
{
    bool sps = !strcmp(ad->name, "sps");
    if (!inkbird_name(ad->name) || m->len != 7) return false;
    sen_set(o, "Inkbird");
    o->temp = (int16_t)m->company * 0.01f;
    o->batt = m->data[5];
    o->mask |= BL_V_TEMP | BL_V_BATT;
    if (sps) { o->hum = u16le(m->data) * 0.01f; o->mask |= BL_V_HUM; }
    return true;
}

/* Eddystone TLM: version, battery mV, temperature 8.8, counters (big endian). */
static bool sen_tlm(const uint8_t *d, int n, bl_sensor_t *o)
{
    if (n < 2 || d[0] != 0x20) return false;
    sen_set(o, "Eddystone TLM");
    if (d[1] != 0) { o->encrypted = d[1] == 1; return true; }
    if (n < 14) return true;
    uint16_t mv = u16be(d + 2);
    int16_t t = s16be(d + 4);
    if (mv) { o->volt = mv * 0.001f; o->mask |= BL_V_VOLT; }
    if (t != (int16_t)0x8000) { o->temp = t / 256.0f; o->mask |= BL_V_TEMP; }
    o->count = (int)u32be(d + 6);
    o->mask |= BL_V_COUNT;
    return true;
}

/* One service data: which format, and its readings. label is what the
 * device is (NULL when the format does not make it a sensor). */
static bool sen_sd(const bl_sd_t *s, const bl_ad_t *ad, bl_sensor_t *o, const char **label)
{
    const uint8_t *d = s->data;
    int n = s->len > (int)sizeof s->data ? (int)sizeof s->data : s->len;
    const char *lb = NULL;
    bool ok = false;
    if (s->uuid_len != 2) return false;
    switch (s->uuid) {
    case 0xFCD2: ok = sen_bthome2(d, n, o); lb = N_("Sensor BTHome"); break;
    case 0x181C: ok = sen_bthome1(d, n, false, o); lb = N_("Sensor BTHome"); break;
    case 0x181E: ok = sen_bthome1(d, n, true, o); lb = N_("Sensor BTHome"); break;
    case 0x181A:
        if ((ok = sen_pvvx(d, n, o))) lb = N_("Termómetro (pvvx)");
        else if ((ok = sen_atc(d, n, o))) lb = N_("Termómetro (ATC)");
        break;
    case 0xFE95: ok = sen_mibeacon(d, n, o); lb = N_("Sensor Xiaomi"); break;
    case 0xFD3D: case 0x0D00: ok = sen_switchbot(d, n, ad, o); lb = N_("Termómetro SwitchBot"); break;
    case 0xFDCD: ok = sen_qingping(d, n, o); lb = N_("Sensor Qingping"); break;
    case 0xFEAA: ok = sen_tlm(d, n, o); break;
    default: break;
    }
    if (ok && label && !*label) *label = lb;
    return ok;
}

static bool sen_mfg(const bl_mfg_t *m, const bl_ad_t *ad, bl_sensor_t *o, const char **label)
{
    const char *lb = NULL;
    bool ok = false;
    if ((ok = sen_inkbird(m, ad, o))) lb = N_("Termómetro Inkbird");
    else if ((ok = sen_govee(m, ad, o))) lb = N_("Termómetro Govee");
    else if (m->company == 0x0499 && (ok = sen_ruuvi(m, o))) lb = "RuuviTag";
    else if (m->company == 0x02E1 && m->len >= 1 && m->data[0] == 0x10) {
        /* Victron's instant readout: AES with the device's own key */
        sen_set(o, "Victron");
        o->encrypted = true;
        ok = true;
        lb = "Victron";
    }
    if (ok && label && !*label) *label = lb;
    return ok;
}

static bool sensors(const bl_ad_t *ad, bl_sensor_t *o, const char **label)
{
    bool ok = false;
    for (int i = 0; i < ad->nsd && i < BL_MAX_SD; i++) ok |= sen_sd(&ad->sd[i], ad, o, label);
    for (int i = 0; i < ad->nmfg && i < BL_MAX_MFG; i++) ok |= sen_mfg(&ad->mfg[i], ad, o, label);
    return ok;
}

/* ---- with the device's key ---- */

/* MiBeacon v4/v5, encrypted: after the header (and the MAC and capability
 * the frame control announces), the objects encrypted, then three bytes of
 * the extended counter and a 4-byte tag. AES-CCM, nonce = the MAC as on the
 * air (little endian) + product id + frame counter + extended counter,
 * associated data 0x11. The plain frame keeps the header, with the
 * encrypted bit cleared, and the objects in place. */
static int mi_decrypt(const bl_sd_t *s, const uint8_t addr[6], const uint8_t key[16], bl_sd_t *plain)
{
    const uint8_t *d = s->data;
    int n = s->len > (int)sizeof s->data ? (int)sizeof s->data : s->len;
    if (n < 5) return BL_KEY_NOT_ENCRYPTED;
    uint16_t fc = u16le(d);
    if (!(fc & 0x0008)) return BL_KEY_NOT_ENCRYPTED;
    if ((fc >> 12) < 4) return BL_KEY_UNSUPPORTED;      /* v2/v3: another scheme, 12-byte keys */
    int i = 5;
    if (fc & 0x0010) i += 6;
    if (fc & 0x0020) {
        if (i >= n) return BL_KEY_UNSUPPORTED;
        if (d[i] & 0x20) i += 2;
        i += 1;
    }
    int clen = n - i - 7;
    if (clen <= 0) return BL_KEY_NOT_ENCRYPTED;         /* nothing measured in this one */
    uint8_t nonce[12];
    for (int k = 0; k < 6; k++) nonce[k] = addr[5 - k];
    memcpy(nonce + 6, d + 2, 3);
    memcpy(nonce + 9, d + n - 7, 3);
    static const uint8_t AAD = 0x11;
    uint8_t out[29];
    if (!bl_ccm_decrypt(key, nonce, 12, &AAD, 1, d + i, clen, d + n - 4, 4, out)) return BL_KEY_WRONG;
    *plain = *s;
    memcpy(plain->data, d, i);
    plain->data[0] = (uint8_t)(fc & ~0x0008);
    memcpy(plain->data + i, out, clen);
    plain->len = (uint8_t)(i + clen);
    return BL_KEY_OK;
}

/* BTHome v2, encrypted: the device info byte, the objects encrypted, a
 * 4-byte counter and a 4-byte tag. Nonce = the MAC as written (big endian)
 * + the UUID as on the air (D2 FC) + the device info + the counter. */
static int bth_decrypt(const bl_sd_t *s, const uint8_t addr[6], const uint8_t key[16], bl_sd_t *plain)
{
    const uint8_t *d = s->data;
    int n = s->len > (int)sizeof s->data ? (int)sizeof s->data : s->len;
    if (n < 1 || (d[0] >> 5) != 2 || !(d[0] & 0x01)) return BL_KEY_NOT_ENCRYPTED;
    int clen = n - 1 - 8;
    if (clen <= 0) return BL_KEY_NOT_ENCRYPTED;
    uint8_t nonce[13];
    memcpy(nonce, addr, 6);
    nonce[6] = 0xD2;
    nonce[7] = 0xFC;
    nonce[8] = d[0];
    memcpy(nonce + 9, d + n - 8, 4);
    uint8_t out[29];
    if (!bl_ccm_decrypt(key, nonce, 13, NULL, 0, d + 1, clen, d + n - 4, 4, out)) return BL_KEY_WRONG;
    *plain = *s;
    plain->data[0] = (uint8_t)(d[0] & ~0x01);
    memcpy(plain->data + 1, out, clen);
    plain->len = (uint8_t)(1 + clen);
    return BL_KEY_OK;
}

int bl_sensor_decode_key(const bl_ad_t *ad, const uint8_t addr[6], const uint8_t key[16], bl_sensor_t *out)
{
    if (!ad || !addr || !key || !out) return BL_KEY_NOT_ENCRYPTED;
    static bl_ad_t tmp;                 /* too big for a small task's stack */
    tmp = *ad;
    int best = BL_KEY_NOT_ENCRYPTED;
    for (int i = 0; i < tmp.nsd && i < BL_MAX_SD; i++) {
        bl_sd_t *s = &tmp.sd[i];
        if (s->uuid_len != 2) continue;
        bl_sd_t plain;
        int r = s->uuid == 0xFE95 ? mi_decrypt(s, addr, key, &plain)
              : s->uuid == 0xFCD2 ? bth_decrypt(s, addr, key, &plain) : BL_KEY_NOT_ENCRYPTED;
        if (r == BL_KEY_OK) *s = plain;
        /* the most telling outcome: ok, else a wrong key, else unsupported */
        if (r == BL_KEY_OK || (r == BL_KEY_WRONG && best != BL_KEY_OK) ||
            (r == BL_KEY_UNSUPPORTED && best == BL_KEY_NOT_ENCRYPTED))
            best = r;
    }
    if (best != BL_KEY_OK) return best;
    if (!bl_sensor_decode(&tmp, addr, out)) return BL_KEY_WRONG;
    return BL_KEY_OK;
}

bool bl_sensor_decode(const bl_ad_t *ad, const uint8_t addr[6], bl_sensor_t *out)
{
    (void)addr;     /* every format here is told apart by its length and markers */
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!ad) return false;
    bool ok = sensors(ad, out, NULL);
    bl_beacon_t bc;
    if (bl_beacon_decode(ad, &bc) && bc.kind != BL_BEACON_EDDY_TLM) {
        bool eddy = bc.kind >= BL_BEACON_EDDY_UID;
        int p = eddy ? bc.tx1m - 41 : bc.tx1m;
        out->rssi1m = (int8_t)(p < -128 ? -128 : p);
        out->mask |= BL_V_RSSI1M;
        sen_set(out, bc.kind == BL_BEACON_IBEACON ? "iBeacon" : bc.kind == BL_BEACON_ALT ? "AltBeacon" : "Eddystone");
        ok = true;
    }
    return ok;
}

/* ---- beacons ---- */

typedef struct {
    uint8_t type;
    int len;                    /* what is really there */
    int declared;               /* what the length byte says */
    const uint8_t *p;
} atlv_t;

/* Apple's Continuity: type, length, payload, again and again. A last one
 * cut short is still listed, with what there is of it. */
static int apple_tlvs(const bl_mfg_t *m, atlv_t *out, int max)
{
    int len = m->len > (int)sizeof m->data ? (int)sizeof m->data : m->len;
    int n = 0, i = 0;
    while (i + 2 <= len && n < max) {
        int l = m->data[i + 1], avail = len - i - 2;
        out[n].type = m->data[i];
        out[n].declared = l;
        out[n].len = l < avail ? l : avail;
        out[n].p = m->data + i + 2;
        n++;
        i += 2 + l;
    }
    return n;
}

static void rev16(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0; i < 16; i++) dst[i] = src[15 - i];
}

static void eddy_url(const uint8_t *p, int n, char *out, size_t sz)
{
    static const char *const SCHEME[] = { "http://www.", "https://www.", "http://", "https://" };
    static const char *const EXP[] = { ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
                                       ".com", ".org", ".edu", ".net", ".info", ".biz", ".gov" };
    sb_t b;
    sb_init(&b, out, sz);
    if (n < 1) return;
    sb_cat(&b, p[0] < 4 ? SCHEME[p[0]] : "?");
    for (int i = 1; i < n; i++) {
        uint8_t c = p[i];
        if (c < COUNTOF(EXP)) sb_cat(&b, EXP[c]);
        else {
            char t[2] = { (char)(c > 0x20 && c < 0x7F ? c : '?'), 0 };
            sb_cat(&b, t);
        }
    }
}

bool bl_beacon_decode(const bl_ad_t *ad, bl_beacon_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof *out);
    if (!ad) return false;
    for (int i = 0; i < ad->nmfg && i < BL_MAX_MFG; i++) {
        const bl_mfg_t *m = &ad->mfg[i];
        if (m->company == 0x004C) {
            atlv_t t[8];
            int n = apple_tlvs(m, t, 8);
            for (int k = 0; k < n; k++) {
                if (t[k].type != 0x02 || t[k].len < 21) continue;
                out->kind = BL_BEACON_IBEACON;
                rev16(out->uuid, t[k].p);
                out->major = u16be(t[k].p + 16);
                out->minor = u16be(t[k].p + 18);
                out->tx1m = (int8_t)t[k].p[20];
                return true;
            }
        }
        if (m->len >= 24 && m->data[0] == 0xBE && m->data[1] == 0xAC) {
            out->kind = BL_BEACON_ALT;
            rev16(out->uuid, m->data + 2);
            out->major = u16be(m->data + 18);
            out->minor = u16be(m->data + 20);
            out->tx1m = (int8_t)m->data[22];
            return true;
        }
    }
    for (int i = 0; i < ad->nsd && i < BL_MAX_SD; i++) {
        const bl_sd_t *s = &ad->sd[i];
        const uint8_t *d = s->data;
        int n = s->len > (int)sizeof s->data ? (int)sizeof s->data : s->len;
        if (s->uuid_len != 2 || s->uuid != 0xFEAA || n < 1) continue;
        switch (d[0]) {
        case 0x00:
            if (n < 18) break;
            out->kind = BL_BEACON_EDDY_UID;
            out->tx1m = (int8_t)d[1];
            memcpy(out->uuid, d + 2, 16);
            return true;
        case 0x10:
            if (n < 3) break;
            out->kind = BL_BEACON_EDDY_URL;
            out->tx1m = (int8_t)d[1];
            eddy_url(d + 2, n - 2, out->url, sizeof out->url);
            return true;
        case 0x20:
            if (n < 2) break;
            out->kind = BL_BEACON_EDDY_TLM;
            return true;
        case 0x30:
            if (n < 10) break;
            out->kind = BL_BEACON_EDDY_EID;
            out->tx1m = (int8_t)d[1];
            memcpy(out->uuid, d + 2, 8);
            return true;
        default: break;
        }
    }
    return false;
}

/* ---- Apple ---- */

static const char *apple_type_name(uint8_t t)
{
    switch (t) {
    case 0x02: return "iBeacon";
    case 0x03: return "AirPrint";
    case 0x05: return "AirDrop";
    case 0x06: return "HomeKit";
    case 0x07: return "AirPods";
    case 0x08: return "Hey Siri";
    case 0x09: return "AirPlay Target";
    case 0x0A: return "AirPlay Source";
    case 0x0B: return "Magic Switch";
    case 0x0C: return "Handoff";
    case 0x0D: return "Tethering Target";
    case 0x0E: return "Tethering Source";
    case 0x0F: return "Nearby Action";
    case 0x10: return "Nearby Info";
    case 0x12: return "Find My";
    }
    return NULL;
}

int bl_apple_types(const bl_mfg_t *m, const char **out, int max)
{
    if (!m || !out || max <= 0 || m->company != 0x004C) return 0;
    atlv_t t[12];
    int n = apple_tlvs(m, t, 12), k = 0;
    for (int i = 0; i < n && k < max; i++) {
        const char *s = apple_type_name(t[i].type);
        if (!s) s = N_("Otro");
        bool dup = false;
        for (int j = 0; j < k; j++) dup |= out[j] == s;
        if (!dup) out[k++] = s;
    }
    return k;
}

/* The earbuds behind a Proximity Pairing, by the model id after the prefix. */
static const char *apple_model(uint16_t id)
{
    static const struct { uint16_t id; const char *name; } M[] = {
        { 0x0220, "AirPods" },
        { 0x0320, "Powerbeats3" },
        { 0x0520, "BeatsX" },
        { 0x0620, "Beats Solo3" },
        { 0x0920, "Beats Studio3" },
        { 0x0A20, "AirPods Max" },
        { 0x0B20, "Powerbeats Pro" },
        { 0x0C20, "Beats Solo Pro" },
        { 0x0E20, "AirPods Pro" },
        { 0x0F20, "AirPods 2" },
        { 0x1020, "Beats Flex" },
        { 0x1120, "Beats Studio Buds" },
        { 0x1220, "Beats Fit Pro" },
        { 0x1320, "AirPods 3" },
        { 0x1420, "AirPods Pro 2" },
        { 0x1620, "Beats Studio Buds+" },
        { 0x1720, "Beats Studio Pro" },
    };
    for (int i = 0; i < COUNTOF(M); i++)
        if (M[i].id == id) return M[i].name;
    return NULL;
}

static void sb_bud(sb_t *b, const char *what, int v, bool charging, bl_tr_fn tr)
{
    sb_sep(b, ", ");
    sb_cat(b, T(tr, what));
    sb_cat(b, " ");
    if (v <= 10) sb_catf(b, "%d %%", v * 10);
    else sb_cat(b, "-");
    if (charging) {
        sb_cat(b, " (");
        sb_cat(b, T(tr, N_("cargando")));
        sb_cat(b, ")");
    }
}

/* Proximity Pairing: prefix, model (2), status, the two buds' battery
 * nibbles, then the charging flags (high nibble) and the case's battery
 * (low), lid counter, color. Which nibble is the left bud depends on which
 * one is the primary (status bit 0x20). Battery 0-10 is tens of percent,
 * 15 is not there. */
static void apple_pods_text(sb_t *b, const atlv_t *t, bl_tr_fn tr)
{
    if (t->len < 3) return;
    uint16_t model = u16be(t->p + 1);
    const char *nm = apple_model(model);
    if (nm) sb_cat(b, nm);
    else sb_catf(b, "%s 0x%04X", T(tr, N_("modelo")), (unsigned)model);
    if (t->len < 6) return;
    uint8_t st = t->p[3], bat = t->p[4], cc = t->p[5];
    bool flip = !(st & 0x20);
    int left = flip ? bat >> 4 : bat & 15, right = flip ? bat & 15 : bat >> 4, chg = cc >> 4;
    sb_t parts;
    char tmp[96];
    sb_init(&parts, tmp, sizeof tmp);
    sb_bud(&parts, N_("izq."), left, (chg & (flip ? 2 : 1)) != 0, tr);
    sb_bud(&parts, N_("der."), right, (chg & (flip ? 1 : 2)) != 0, tr);
    sb_bud(&parts, N_("estuche"), cc & 15, (chg & 4) != 0, tr);
    sb_cat(b, " \xC2\xB7 ");
    sb_cat(b, tmp);
}

/* Find My: the status byte (battery in the top two bits, what it is in the
 * next two) and, separated from its owner, the whole public key. */
static void apple_findmy_text(sb_t *b, const atlv_t *t, bl_tr_fn tr)
{
    static const char *const KIND[] = { N_("Equipo Apple"), "AirTag", N_("Accesorio Find My"), "AirPods" };
    static const char *const BATT[] = { N_("batería llena"), N_("batería media"), N_("batería baja"),
                                        N_("batería muy baja") };
    if (t->len < 1) return;
    uint8_t st = t->p[0];
    sb_cat(b, T(tr, KIND[(st >> 4) & 3]));
    sb_cat(b, " \xC2\xB7 ");
    sb_cat(b, T(tr, t->declared >= 25 ? N_("lejos del dueño") : N_("cerca del dueño")));
    sb_cat(b, " \xC2\xB7 ");
    sb_cat(b, T(tr, BATT[st >> 6]));
}

/* ---- classification ---- */

static bool has_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return false;
    size_t n = strlen(needle);
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        for (; i < n && h[i]; i++) {
            char a = h[i], c = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (a != c) break;
        }
        if (i == n) return true;
    }
    return false;
}

static bool has_u16(const bl_ad_t *ad, uint16_t u)
{
    for (int i = 0; i < ad->n16 && i < BL_MAX_U16; i++)
        if (ad->u16[i] == u) return true;
    for (int i = 0; i < ad->nsd && i < BL_MAX_SD; i++)
        if (ad->sd[i].uuid_len != 16 && ad->sd[i].uuid == u) return true;
    return false;
}

static bool has_u128_name(const bl_ad_t *ad, const char *name)
{
    for (int i = 0; i < ad->n128 && i < BL_MAX_U128; i++) {
        const char *s = bl_uuid128_name(ad->u128[i]);
        if (s && !strcmp(s, name)) return true;
    }
    return false;
}

const char *bl_class_name(bl_class_t c)
{
    switch (c) {
    case BL_CLS_PHONE: return N_("Teléfono");
    case BL_CLS_COMPUTER: return N_("Computadora");
    case BL_CLS_TABLET: return N_("Tableta");
    case BL_CLS_WATCH: return N_("Reloj");
    case BL_CLS_AUDIO: return N_("Audio");
    case BL_CLS_TRACKER: return N_("Rastreador");
    case BL_CLS_SENSOR: return N_("Sensor");
    case BL_CLS_BEACON: return N_("Baliza");
    case BL_CLS_HID: return N_("Teclado o mouse");
    case BL_CLS_GAMEPAD: return N_("Joystick");
    case BL_CLS_TV: return N_("Tele");
    case BL_CLS_HEALTH: return N_("Salud");
    case BL_CLS_LIGHT: return N_("Luz");
    case BL_CLS_FITNESS: return N_("Deporte");
    case BL_CLS_DEVBOARD: return N_("Placa de desarrollo");
    default: break;
    }
    return N_("Desconocido");
}

static bl_class_t cls_apple(const bl_mfg_t *m, const char **label)
{
    atlv_t t[12];
    int n = apple_tlvs(m, t, 12);
    const atlv_t *nearby = NULL;
    bool handoff = false;
    for (int i = 0; i < n; i++) {
        switch (t[i].type) {
        case 0x02: *label = "iBeacon"; return BL_CLS_BEACON;
        case 0x07: {
            const char *nm = t[i].len >= 3 ? apple_model(u16be(t[i].p + 1)) : NULL;
            *label = nm ? nm : "AirPods";
            return BL_CLS_AUDIO;
        }
        case 0x12: {
            int kind = t[i].len >= 1 ? (t[i].p[0] >> 4) & 3 : 2;
            bool far = t[i].declared >= 25;
            if (kind == 3) { *label = "AirPods"; return BL_CLS_AUDIO; }
            if (kind == 1) *label = far ? N_("AirTag (lejos del dueño)") : "AirTag";
            else *label = far ? N_("Find My (lejos del dueño)") : "Find My";
            return BL_CLS_TRACKER;
        }
        case 0x09: case 0x0A: *label = "AirPlay"; return BL_CLS_TV;
        case 0x0B: *label = "Apple Watch"; return BL_CLS_WATCH;
        case 0x0E: *label = N_("iPhone (punto de acceso)"); return BL_CLS_PHONE;
        case 0x0D: *label = "Mac"; return BL_CLS_COMPUTER;
        case 0x0C: handoff = true; break;
        case 0x10: nearby = &t[i]; break;
        default: break;
        }
    }
    /* A guess: an iPhone's Nearby Info is the longer one (with its
     * authentication tag); a Mac at its desk says Handoff and a short one. */
    if (nearby && nearby->declared >= 7) { *label = "iPhone"; return BL_CLS_PHONE; }
    if (handoff) { *label = "Mac"; return BL_CLS_COMPUTER; }
    if (nearby) { *label = "iPhone"; return BL_CLS_PHONE; }
    for (int i = 0; i < n; i++) {
        if (t[i].type == 0x05 || t[i].type == 0x0F || t[i].type == 0x08) { *label = N_("Equipo Apple"); return BL_CLS_PHONE; }
        if (t[i].type == 0x06) { *label = N_("Accesorio HomeKit"); return BL_CLS_UNKNOWN; }
        if (t[i].type == 0x03) { *label = N_("Impresora AirPrint"); return BL_CLS_UNKNOWN; }
    }
    return BL_CLS_UNKNOWN;
}

/* Microsoft: Swift Pair (beacon id 3) and the Connected Devices Platform
 * (scenario 1, then the device type in the low six bits). */
static bl_class_t cls_microsoft(const bl_mfg_t *m, const bl_ad_t *ad, const char **label)
{
    if (m->len >= 1 && m->data[0] == 0x03) {
        unsigned cat = ad->has_appearance ? ad->appearance >> 6 : 0;
        if (cat == 15 && (ad->appearance & 0x3F) == 1) { *label = N_("Teclado (Swift Pair)"); return BL_CLS_HID; }
        if (cat == 15 && (ad->appearance & 0x3F) == 2) { *label = N_("Mouse (Swift Pair)"); return BL_CLS_HID; }
        if (cat == 15 && ((ad->appearance & 0x3F) == 3 || (ad->appearance & 0x3F) == 4)) {
            *label = N_("Joystick (Swift Pair)");
            return BL_CLS_GAMEPAD;
        }
        if (cat == 33 || cat == 37) { *label = N_("Audio (Swift Pair)"); return BL_CLS_AUDIO; }
        *label = "Swift Pair";
        return BL_CLS_HID;
    }
    if (m->len >= 2 && m->data[0] == 0x01) {
        switch (m->data[1] & 0x3F) {
        case 1: *label = "Xbox"; return BL_CLS_TV;
        case 6: *label = "iPhone"; return BL_CLS_PHONE;
        case 7: *label = "iPad"; return BL_CLS_TABLET;
        case 8: *label = "Android"; return BL_CLS_PHONE;
        case 9: *label = N_("PC con Windows"); return BL_CLS_COMPUTER;
        case 11: *label = N_("Teléfono con Windows"); return BL_CLS_PHONE;
        case 12: *label = "Linux"; return BL_CLS_COMPUTER;
        case 14: *label = "Surface Hub"; return BL_CLS_TV;
        case 15: *label = N_("Notebook con Windows"); return BL_CLS_COMPUTER;
        case 16: *label = N_("Tableta con Windows"); return BL_CLS_TABLET;
        default: break;
        }
    }
    return BL_CLS_UNKNOWN;
}

static bl_class_t cls_appearance(uint16_t a)
{
    unsigned cat = a >> 6, sub = a & 0x3F;
    switch (cat) {
    case 1: return BL_CLS_PHONE;
    case 2: return sub == 7 ? BL_CLS_TABLET : BL_CLS_COMPUTER;
    case 3: return BL_CLS_WATCH;
    case 6: case 11: return BL_CLS_HID;
    case 8: case 9: return BL_CLS_TRACKER;
    case 10: case 33: case 34: case 37: return BL_CLS_AUDIO;
    case 12: case 13: case 14: case 16: case 41: case 49: case 50: case 52: case 53: case 54: case 55:
        return BL_CLS_HEALTH;
    case 15: return sub == 3 || sub == 4 ? BL_CLS_GAMEPAD : BL_CLS_HID;
    case 17: case 18: case 81: return BL_CLS_FITNESS;
    case 21: return BL_CLS_SENSOR;
    case 22: case 31: return BL_CLS_LIGHT;
    case 39: case 40: return BL_CLS_TV;
    case 42: return BL_CLS_GAMEPAD;
    default: break;
    }
    return BL_CLS_UNKNOWN;
}

typedef struct {
    const char *needle;
    uint8_t cls;
    bool prefix;
    const char *label;
} hint_t;

/* Names that say what they are. Earlier wins: "Galaxy Buds" is audio
 * before "Galaxy" is a phone, "JBL Charge" a speaker before anything else. */
static const hint_t HINTS[] = {
    { "[TV]", BL_CLS_TV, true, N_("Tele") },
    { "[AV]", BL_CLS_AUDIO, true, NULL },
    { "LE-", BL_CLS_AUDIO, true, NULL },
    { "AirPods", BL_CLS_AUDIO, false, NULL },
    { "Buds", BL_CLS_AUDIO, false, NULL },
    { "WH-", BL_CLS_AUDIO, true, NULL },
    { "WF-", BL_CLS_AUDIO, true, NULL },
    { "WI-", BL_CLS_AUDIO, true, NULL },
    { "JBL", BL_CLS_AUDIO, false, NULL },
    { "Beats", BL_CLS_AUDIO, false, NULL },
    { "Jabra", BL_CLS_AUDIO, false, NULL },
    { "Bose", BL_CLS_AUDIO, false, NULL },
    { "SoundLink", BL_CLS_AUDIO, false, NULL },
    { "Soundcore", BL_CLS_AUDIO, false, NULL },
    { "Headphone", BL_CLS_AUDIO, false, NULL },
    { "Headset", BL_CLS_AUDIO, false, NULL },
    { "Earbud", BL_CLS_AUDIO, false, NULL },
    { "Speaker", BL_CLS_AUDIO, false, NULL },
    { "Soundbar", BL_CLS_AUDIO, false, NULL },
    { "Auricular", BL_CLS_AUDIO, false, NULL },
    { "Parlante", BL_CLS_AUDIO, false, NULL },
    { "Controller", BL_CLS_GAMEPAD, false, NULL },
    { "Gamepad", BL_CLS_GAMEPAD, false, NULL },
    { "DualSense", BL_CLS_GAMEPAD, false, NULL },
    { "DUALSHOCK", BL_CLS_GAMEPAD, false, NULL },
    { "Joy-Con", BL_CLS_GAMEPAD, false, NULL },
    { "Watch", BL_CLS_WATCH, false, NULL },
    { "Band", BL_CLS_WATCH, false, NULL },
    { "Amazfit", BL_CLS_WATCH, false, NULL },
    { "Forerunner", BL_CLS_WATCH, false, NULL },
    { "fenix", BL_CLS_WATCH, false, NULL },
    { "Venu", BL_CLS_WATCH, false, NULL },
    { "Instinct", BL_CLS_WATCH, false, NULL },
    { "Reloj", BL_CLS_WATCH, false, NULL },
    { "Keyboard", BL_CLS_HID, false, N_("Teclado") },
    { "Teclado", BL_CLS_HID, false, N_("Teclado") },
    { "MX Keys", BL_CLS_HID, false, N_("Teclado") },
    { "Mouse", BL_CLS_HID, false, N_("Mouse") },
    { "MX Master", BL_CLS_HID, false, N_("Mouse") },
    { "MX Anywhere", BL_CLS_HID, false, N_("Mouse") },
    { "Trackpad", BL_CLS_HID, false, NULL },
    { "Remote", BL_CLS_HID, false, N_("Control remoto") },
    { "LYWSD", BL_CLS_SENSOR, true, N_("Termómetro") },
    { "ATC_", BL_CLS_SENSOR, true, N_("Termómetro") },
    { "GVH5", BL_CLS_SENSOR, true, N_("Termómetro Govee") },
    { "Govee_H5", BL_CLS_SENSOR, true, N_("Termómetro Govee") },
    { "Ruuvi", BL_CLS_SENSOR, true, "RuuviTag" },
    { "MJ_HT", BL_CLS_SENSOR, true, N_("Termómetro") },
    { "CGG", BL_CLS_SENSOR, true, N_("Sensor Qingping") },
    { "CGDK", BL_CLS_SENSOR, true, N_("Sensor Qingping") },
    { "Qingping", BL_CLS_SENSOR, false, N_("Sensor Qingping") },
    { "IBS-", BL_CLS_SENSOR, true, N_("Termómetro Inkbird") },
    { "WoSensor", BL_CLS_SENSOR, true, N_("Termómetro SwitchBot") },
    { "Thermo", BL_CLS_SENSOR, false, N_("Termómetro") },
    { "Hygro", BL_CLS_SENSOR, false, N_("Termómetro") },
    { "Tile", BL_CLS_TRACKER, true, "Tile" },
    { "Chipolo", BL_CLS_TRACKER, false, "Chipolo" },
    { "SmartTag", BL_CLS_TRACKER, false, "SmartTag" },
    { "Hue", BL_CLS_LIGHT, true, NULL },
    { "ihoment", BL_CLS_LIGHT, true, NULL },
    { "Govee_H6", BL_CLS_LIGHT, true, NULL },
    { "GBK_H6", BL_CLS_LIGHT, true, NULL },
    { "ELK-BLEDOM", BL_CLS_LIGHT, true, NULL },
    { "Triones", BL_CLS_LIGHT, true, NULL },
    { "LEDnet", BL_CLS_LIGHT, true, NULL },
    { "Yeelight", BL_CLS_LIGHT, false, NULL },
    { "Bulb", BL_CLS_LIGHT, false, NULL },
    { "Lamp", BL_CLS_LIGHT, false, NULL },
    { "BRAVIA", BL_CLS_TV, false, N_("Tele") },
    { "Chromecast", BL_CLS_TV, false, NULL },
    { "Fire TV", BL_CLS_TV, false, NULL },
    { "Roku", BL_CLS_TV, false, NULL },
    { "KICKR", BL_CLS_FITNESS, false, NULL },
    { "Wahoo", BL_CLS_FITNESS, false, NULL },
    { "Treadmill", BL_CLS_FITNESS, false, NULL },
    { "ESP32", BL_CLS_DEVBOARD, false, NULL },
    { "ESP_", BL_CLS_DEVBOARD, true, NULL },
    { "nRF", BL_CLS_DEVBOARD, true, NULL },
    { "Arduino", BL_CLS_DEVBOARD, false, NULL },
    { "micro:bit", BL_CLS_DEVBOARD, false, NULL },
    { "MacBook", BL_CLS_COMPUTER, false, "Mac" },
    { "iMac", BL_CLS_COMPUTER, false, "Mac" },
    { "Mac mini", BL_CLS_COMPUTER, false, "Mac" },
    { "ThinkPad", BL_CLS_COMPUTER, false, NULL },
    { "LAPTOP-", BL_CLS_COMPUTER, true, NULL },
    { "DESKTOP-", BL_CLS_COMPUTER, true, NULL },
    { "iPad", BL_CLS_TABLET, false, "iPad" },
    { "Tablet", BL_CLS_TABLET, false, NULL },
    { "iPhone", BL_CLS_PHONE, false, "iPhone" },
    { "Galaxy", BL_CLS_PHONE, false, NULL },
    { "Pixel", BL_CLS_PHONE, true, NULL },
    { "Redmi", BL_CLS_PHONE, true, NULL },
};

bl_class_t bl_classify(const bl_ad_t *ad, const char **label)
{
    const char *dummy;
    if (!label) label = &dummy;
    *label = NULL;
    if (!ad) return BL_CLS_UNKNOWN;

    /* 1. readings on the air: a sensor, whatever else it says */
    bl_sensor_t s;
    memset(&s, 0, sizeof s);
    const char *sl = NULL;
    if (sensors(ad, &s, &sl) && sl) { *label = sl; return BL_CLS_SENSOR; }

    /* 2. the companies that say what the device is */
    for (int i = 0; i < ad->nmfg && i < BL_MAX_MFG; i++) {
        const bl_mfg_t *m = &ad->mfg[i];
        bl_class_t c = BL_CLS_UNKNOWN;
        const char *l = NULL;
        if (m->company == 0x004C) c = cls_apple(m, &l);
        else if (m->company == 0x0006) c = cls_microsoft(m, ad, &l);
        if (c != BL_CLS_UNKNOWN || l) { *label = l; if (c != BL_CLS_UNKNOWN) return c; }
    }

    /* 3. beacons */
    bl_beacon_t bc;
    if (bl_beacon_decode(ad, &bc)) {
        *label = bc.kind == BL_BEACON_IBEACON ? "iBeacon" : bc.kind == BL_BEACON_ALT ? "AltBeacon" : "Eddystone";
        return BL_CLS_BEACON;
    }

    /* 4. what it says it looks like */
    if (ad->has_appearance && (ad->appearance >> 6)) {
        bl_class_t c = cls_appearance(ad->appearance);
        if (c != BL_CLS_UNKNOWN) {
            *label = bl_appearance_name(ad->appearance);
            return c;
        }
    }

    /* 5. development boards, before their services make them look like products */
    if (find_mfg(ad, 0x02E5)) { *label = "ESP32"; return BL_CLS_DEVBOARD; }
    if (has_u128_name(ad, "Nordic UART")) { *label = N_("UART de Nordic"); return BL_CLS_DEVBOARD; }

    /* 6. services */
    if (has_u16(ad, 0xFE2C)) { *label = N_("Auriculares (Fast Pair)"); return BL_CLS_AUDIO; }
    if (has_u16(ad, 0xFD5A)) { *label = "SmartTag"; return BL_CLS_TRACKER; }
    if (has_u16(ad, 0xFEED) || has_u16(ad, 0xFEEC)) { *label = "Tile"; return BL_CLS_TRACKER; }
    if (has_u16(ad, 0xFD6F)) { *label = N_("Teléfono (exposición)"); return BL_CLS_PHONE; }
    if (has_u16(ad, 0x180D)) { *label = N_("Sensor de pulso"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x1810)) { *label = N_("Tensiómetro"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x1808) || has_u16(ad, 0x181F)) { *label = N_("Glucómetro"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x1809)) { *label = N_("Termómetro"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x181D) || has_u16(ad, 0x181B)) { *label = N_("Balanza"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x1822)) { *label = N_("Oxímetro"); return BL_CLS_HEALTH; }
    if (has_u16(ad, 0x1812)) return BL_CLS_HID;
    if (has_u16(ad, 0x1816) || has_u16(ad, 0x1818) || has_u16(ad, 0x1826) || has_u16(ad, 0x1814))
        return BL_CLS_FITNESS;
    for (uint16_t u = 0x184E; u <= 0x1856; u++)
        if (has_u16(ad, u)) return BL_CLS_AUDIO;
    if (has_u16(ad, 0xFEBE)) { *label = "Bose"; return BL_CLS_AUDIO; }
    if (has_u16(ad, 0xFE0F)) { *label = "Philips Hue"; return BL_CLS_LIGHT; }
    if (has_u16(ad, 0xFEE0)) { *label = "Mi Band"; return BL_CLS_WATCH; }
    if (has_u16(ad, 0x181A)) return BL_CLS_SENSOR;

    /* 7. companies that make one kind of thing */
    for (int i = 0; i < ad->nmfg && i < BL_MAX_MFG; i++) {
        switch (ad->mfg[i].company) {
        case 0x0087: *label = "Garmin"; return BL_CLS_WATCH;
        case 0x009E: *label = "Bose"; return BL_CLS_AUDIO;
        case 0x009F: *label = "Suunto"; return BL_CLS_WATCH;
        case 0x0157: *label = "Amazfit"; return BL_CLS_WATCH;
        case 0x006B: *label = "Polar"; return BL_CLS_FITNESS;
        case 0x01DA: *label = "Logitech"; return BL_CLS_HID;
        case 0x0067: *label = "Jabra"; return BL_CLS_AUDIO;
        case 0x00CC: *label = "Beats"; return BL_CLS_AUDIO;
        default: break;
        }
    }

    /* 8. the name */
    if (ad->name[0]) {
        for (int i = 0; i < COUNTOF(HINTS); i++) {
            const hint_t *h = &HINTS[i];
            bool hit = h->prefix ? !strncmp(ad->name, h->needle, strlen(h->needle)) : has_ci(ad->name, h->needle);
            if (hit) {
                if (h->label) *label = h->label;
                return (bl_class_t)h->cls;
            }
        }
    }
    return BL_CLS_UNKNOWN;
}

/* ---- explaining one packet ---- */

typedef struct {
    bl_line_t *out;
    int max, n;
    bl_tr_fn tr;
    sb_t v;                     /* the value of the line being written */
} ex_t;

/* A new line: key (Spanish, translated) plus an ASCII suffix. false when
 * there is no room for more lines. */
static bool ex_line(ex_t *e, const char *key, const char *suffix)
{
    if (e->n >= e->max) return false;
    bl_line_t *l = &e->out[e->n++];
    sb_t k;
    sb_init(&k, l->key, sizeof l->key);
    sb_cat(&k, T(e->tr, key));
    if (suffix) sb_cat(&k, suffix);
    sb_init(&e->v, l->val, sizeof l->val);
    return true;
}

static void sensor_text(sb_t *b, const bl_sensor_t *s, bl_tr_fn tr)
{
    static const char *const BTN[] = { N_("nada"), N_("pulsado"), N_("doble"), N_("triple"), N_("largo"),
                                       N_("largo doble"), N_("largo triple") };
    const char *dot = " \xC2\xB7 ";
    uint32_t m = s->mask;
    if (s->encrypted) { sb_sep(b, dot); sb_cat(b, T(tr, N_("cifrado, sin lecturas"))); return; }
    if (m & BL_V_TEMP) { sb_sep(b, dot); sb_flt(b, s->temp, 2); sb_cat(b, " \xC2\xB0" "C"); }
    if (m & BL_V_HUM) { sb_sep(b, dot); sb_flt(b, s->hum, 1); sb_cat(b, " %"); }
    if (m & BL_V_PRESS) { sb_sep(b, dot); sb_flt(b, s->press, 1); sb_cat(b, " hPa"); }
    if (m & BL_V_DEW) { sb_sep(b, dot); sb_cat(b, T(tr, N_("rocío"))); sb_cat(b, " "); sb_flt(b, s->dew, 1); sb_cat(b, " \xC2\xB0" "C"); }
    if (m & BL_V_CO2) { sb_sep(b, dot); sb_cat(b, "CO2 "); sb_flt(b, s->co2, 0); sb_cat(b, " ppm"); }
    if (m & BL_V_PM25) { sb_sep(b, dot); sb_cat(b, "PM2,5 "); sb_flt(b, s->pm25, 0); sb_cat(b, " \xC2\xB5g/m\xC2\xB3"); }
    if (m & BL_V_TVOC) { sb_sep(b, dot); sb_cat(b, "TVOC "); sb_flt(b, s->tvoc, 0); }
    if (m & BL_V_LUX) { sb_sep(b, dot); sb_flt(b, s->lux, 0); sb_cat(b, " lx"); }
    if (m & BL_V_MOIST) { sb_sep(b, dot); sb_cat(b, T(tr, N_("suelo"))); sb_cat(b, " "); sb_flt(b, s->moist, 0); sb_cat(b, " %"); }
    if (m & BL_V_COND) { sb_sep(b, dot); sb_flt(b, s->cond, 0); sb_cat(b, " \xC2\xB5S/cm"); }
    if (m & BL_V_WEIGHT) { sb_sep(b, dot); sb_flt(b, s->weight, 2); sb_cat(b, " kg"); }
    if (m & BL_V_POWER) { sb_sep(b, dot); sb_flt(b, s->power, 1); sb_cat(b, " W"); }
    if (m & BL_V_ENERGY) { sb_sep(b, dot); sb_flt(b, s->energy, 3); sb_cat(b, " kWh"); }
    if (m & BL_V_DIST) { sb_sep(b, dot); sb_flt(b, s->dist, 0); sb_cat(b, " mm"); }
    if (m & BL_V_ROTATION) { sb_sep(b, dot); sb_flt(b, s->rotation, 1); sb_cat(b, "\xC2\xB0"); }
    if (m & BL_V_OPEN) { sb_sep(b, dot); sb_cat(b, T(tr, s->open ? N_("abierto") : N_("cerrado"))); }
    if (m & BL_V_MOTION) { sb_sep(b, dot); sb_cat(b, T(tr, s->motion ? N_("movimiento") : N_("quieto"))); }
    if (m & BL_V_BUTTON) {
        sb_sep(b, dot);
        sb_cat(b, T(tr, N_("botón")));
        sb_cat(b, " ");
        if (s->button >= 0 && s->button < COUNTOF(BTN)) sb_cat(b, T(tr, BTN[s->button]));
        else if (s->button == 0x80) sb_cat(b, T(tr, N_("mantenido")));
        else sb_catf(b, "%d", s->button);
    }
    if (m & BL_V_ACC) {
        sb_sep(b, dot);
        sb_cat(b, "acc ");
        sb_flt(b, s->acc_x, 2);
        sb_cat(b, " / ");
        sb_flt(b, s->acc_y, 2);
        sb_cat(b, " / ");
        sb_flt(b, s->acc_z, 2);
        sb_cat(b, " g");
    }
    if (m & BL_V_BATT) { sb_sep(b, dot); sb_cat(b, T(tr, N_("batería"))); sb_catf(b, " %d %%", s->batt); }
    if (m & BL_V_VOLT) { sb_sep(b, dot); sb_flt(b, s->volt, 3); sb_cat(b, " V"); }
    if (m & BL_V_COUNT) { sb_sep(b, dot); sb_cat(b, T(tr, N_("contador"))); sb_catf(b, " %d", s->count); }
    if (m & BL_V_HR) { sb_sep(b, dot); sb_catf(b, "%d ", s->hr); sb_cat(b, T(tr, N_("lpm"))); }
}

static void ex_uuid16_list(ex_t *e, const char *key, const uint8_t *p, int n, int step)
{
    if (!ex_line(e, key, NULL)) return;
    for (int k = 0; k + step <= n; k += step) {
        sb_sep(&e->v, ", ");
        uint16_t u = u16le(p + k);
        if (step == 4) sb_catf(&e->v, "0x%08X", (unsigned)u32le(p + k));
        else sb_catf(&e->v, "0x%04X", (unsigned)u);
        const char *nm = step == 2 || !u16le(p + k + 2) ? bl_uuid16_name(u) : NULL;
        if (nm) { sb_cat(&e->v, " "); sb_cat(&e->v, nm); }
    }
    if (n % step) { sb_sep(&e->v, " "); sb_cat(&e->v, T(e->tr, N_("(sobran bytes)"))); }
}

static void ex_uuid128(ex_t *e, const char *key, const uint8_t *u)
{
    if (!ex_line(e, key, NULL)) return;
    char s[40];
    bl_uuid_str(u, 16, s, sizeof s);
    sb_cat(&e->v, s);
    const char *nm = bl_uuid128_name(u);
    if (nm) { sb_cat(&e->v, " ("); sb_cat(&e->v, nm); sb_cat(&e->v, ")"); }
}

static void ex_data(ex_t *e, const uint8_t *p, int n)
{
    if (n > 0 && ex_line(e, N_("Datos"), NULL)) sb_hex(&e->v, p, n);
}

static void ex_apple(ex_t *e, const bl_mfg_t *m)
{
    const char *ty[12];
    int nt = bl_apple_types(m, ty, 12);
    if (nt && ex_line(e, "Apple", NULL))
        for (int i = 0; i < nt; i++) { sb_sep(&e->v, ", "); sb_cat(&e->v, T(e->tr, ty[i])); }
    atlv_t t[12];
    int n = apple_tlvs(m, t, 12);
    for (int i = 0; i < n; i++) {
        if (t[i].type == 0x07 && t[i].len >= 3 && ex_line(e, "AirPods", NULL)) apple_pods_text(&e->v, &t[i], e->tr);
        if (t[i].type == 0x12 && t[i].len >= 1 && ex_line(e, "Find My", NULL)) apple_findmy_text(&e->v, &t[i], e->tr);
    }
}

static void ex_beacon(ex_t *e, const bl_beacon_t *b)
{
    char u[40];
    switch (b->kind) {
    case BL_BEACON_IBEACON:
    case BL_BEACON_ALT:
        if (!ex_line(e, b->kind == BL_BEACON_IBEACON ? "iBeacon" : "AltBeacon", NULL)) return;
        bl_uuid_str(b->uuid, 16, u, sizeof u);
        sb_cat(&e->v, u);
        sb_cat(&e->v, " ");
        sb_cat(&e->v, T(e->tr, N_("mayor")));
        sb_catf(&e->v, " %u ", (unsigned)b->major);
        sb_cat(&e->v, T(e->tr, N_("menor")));
        sb_catf(&e->v, " %u \xC2\xB7 %d dBm ", (unsigned)b->minor, b->tx1m);
        sb_cat(&e->v, T(e->tr, N_("a 1 m")));
        break;
    case BL_BEACON_EDDY_UID:
        if (!ex_line(e, "Eddystone UID", NULL)) return;
        sb_hex(&e->v, b->uuid, 10);
        sb_cat(&e->v, " / ");
        sb_hex(&e->v, b->uuid + 10, 6);
        sb_catf(&e->v, " \xC2\xB7 %d dBm ", b->tx1m);
        sb_cat(&e->v, T(e->tr, N_("a 0 m")));
        break;
    case BL_BEACON_EDDY_URL:
        if (!ex_line(e, "Eddystone URL", NULL)) return;
        sb_cat(&e->v, b->url);
        sb_catf(&e->v, " \xC2\xB7 %d dBm ", b->tx1m);
        sb_cat(&e->v, T(e->tr, N_("a 0 m")));
        break;
    case BL_BEACON_EDDY_EID:
        if (!ex_line(e, "Eddystone EID", NULL)) return;
        sb_hex(&e->v, b->uuid, 8);
        break;
    default: break;
    }
}

static void ex_mfg(ex_t *e, const bl_ad_t *ad, const uint8_t *p, int n)
{
    if (n < 2) {
        if (ex_line(e, N_("Fabricante"), NULL)) { sb_cat(&e->v, T(e->tr, N_("demasiado corto"))); sb_cat(&e->v, ": "); sb_hex(&e->v, p, n); }
        return;
    }
    bl_mfg_t m;
    memset(&m, 0, sizeof m);
    m.company = u16le(p);
    m.len = (uint8_t)(n - 2 > 29 ? 29 : n - 2);
    memcpy(m.data, p + 2, m.len);
    if (ex_line(e, N_("Fabricante"), NULL)) {
        const char *c = bl_company_name(m.company);
        if (c) { sb_cat(&e->v, c); sb_catf(&e->v, " (0x%04X)", (unsigned)m.company); }
        else { sb_catf(&e->v, "0x%04X (", (unsigned)m.company); sb_cat(&e->v, T(e->tr, N_("desconocido"))); sb_cat(&e->v, ")"); }
    }
    if (m.company == 0x004C) ex_apple(e, &m);
    if (m.company == 0x0006 && m.len >= 1) {
        const char *l = NULL;
        bl_class_t c = cls_microsoft(&m, ad, &l);
        if (ex_line(e, "Microsoft", NULL)) {
            if (m.data[0] == 0x03) sb_cat(&e->v, "Swift Pair");
            else if (m.data[0] == 0x01) {
                sb_cat(&e->v, "Connected Devices Platform");
                if (c != BL_CLS_UNKNOWN && l) { sb_cat(&e->v, " \xC2\xB7 "); sb_cat(&e->v, T(e->tr, l)); }
            } else sb_catf(&e->v, "%s 0x%02X", T(e->tr, N_("tipo")), m.data[0]);
        }
    }
    /* a beacon or a sensor in this very structure */
    bl_ad_t one;
    bl_ad_clear(&one);
    memcpy(one.name, ad->name, sizeof one.name);
    one.mfg[0] = m;
    one.nmfg = 1;
    bl_beacon_t bc;
    if (bl_beacon_decode(&one, &bc)) ex_beacon(e, &bc);
    bl_sensor_t s;
    memset(&s, 0, sizeof s);
    if (sen_mfg(&m, &one, &s, NULL) && ex_line(e, s.format, NULL)) sensor_text(&e->v, &s, e->tr);
    ex_data(e, p + 2, n - 2);                   /* all of it, not the 29 bytes kept */
}

static void ex_sd(ex_t *e, const bl_ad_t *ad, const uint8_t *p, int n, int ul)
{
    char k[16];
    if (n < ul) {
        if (ex_line(e, N_("Datos de servicio"), NULL)) { sb_cat(&e->v, T(e->tr, N_("demasiado corto"))); sb_cat(&e->v, ": "); sb_hex(&e->v, p, n); }
        return;
    }
    if (ul == 2) snprintf(k, sizeof k, " 0x%04X", (unsigned)u16le(p));
    else if (ul == 4) snprintf(k, sizeof k, " 0x%08X", (unsigned)u32le(p));
    else k[0] = 0;
    if (!ex_line(e, N_("Datos de servicio"), k)) return;
    const char *nm = NULL;
    if (ul == 16) {
        char s[40];
        bl_uuid_str(p, 16, s, sizeof s);
        sb_cat(&e->v, s);
        nm = bl_uuid128_name(p);
        if (nm) { sb_cat(&e->v, " ("); sb_cat(&e->v, nm); sb_cat(&e->v, ")"); }
    } else if (ul == 2 || !u16le(p + 2)) {
        nm = bl_uuid16_name(u16le(p));
        if (nm) sb_cat(&e->v, nm);
    }
    bl_sd_t sd;
    memset(&sd, 0, sizeof sd);
    sd.uuid = sd_key(p, ul);
    sd.uuid_len = (uint8_t)ul;
    sd.len = (uint8_t)(n - ul > 29 ? 29 : n - ul);
    memcpy(sd.data, p + ul, sd.len);
    bl_ad_t one;
    bl_ad_clear(&one);
    memcpy(one.name, ad->name, sizeof one.name);
    one.sd[0] = sd;
    one.nsd = 1;
    /* the manufacturer data that a format reads along (SwitchBot's) */
    one.nmfg = ad->nmfg < 0 ? 0 : ad->nmfg > BL_MAX_MFG ? BL_MAX_MFG : ad->nmfg;
    memcpy(one.mfg, ad->mfg, sizeof one.mfg);
    bl_sensor_t s;
    memset(&s, 0, sizeof s);
    if (sen_sd(&sd, &one, &s, NULL)) {
        if (s.format && (!nm || strcmp(s.format, nm))) { sb_sep(&e->v, " \xC2\xB7 "); sb_cat(&e->v, s.format); }
        sensor_text(&e->v, &s, e->tr);
    }
    if (ul == 2 && sd.uuid == 0xFE2C) {
        sb_sep(&e->v, " \xC2\xB7 ");
        if (sd.len == 3) sb_catf(&e->v, "%s 0x%06X", T(e->tr, N_("modelo")), (unsigned)(sd.data[0] << 16 | sd.data[1] << 8 | sd.data[2]));
        else sb_cat(&e->v, T(e->tr, N_("emparejado, no visible")));
    }
    bl_beacon_t bc;
    if (bl_beacon_decode(&one, &bc)) ex_beacon(e, &bc);
    ex_data(e, p + ul, n - ul);
}

static void ex_addr(ex_t *e, const char *key, const uint8_t *p, int n, int type)
{
    if (!ex_line(e, key, NULL)) return;
    for (int k = 0; k + 6 <= n; k += 6) {
        sb_sep(&e->v, ", ");
        sb_addr_le(&e->v, p + k);
    }
    if (type >= 0) {
        sb_cat(&e->v, " (");
        sb_cat(&e->v, T(e->tr, type ? N_("aleatoria") : N_("pública")));
        sb_cat(&e->v, ")");
    }
}

static void ex_text(ex_t *e, const char *key, const uint8_t *p, int n)
{
    if (!ex_line(e, key, NULL)) return;
    char t[100];
    name_copy(t, sizeof t, p, n);
    sb_cat(&e->v, t);
}

static void ex_one(ex_t *e, const bl_ad_t *ad, uint8_t t, const uint8_t *p, int n)
{
    bl_tr_fn tr = e->tr;
    switch (t) {
    case 0x01: {
        static const char *const F[] = { N_("LE Limited Discoverable"), N_("LE General Discoverable"),
                                         N_("sin BR/EDR"), N_("LE y BR/EDR a la vez (controlador)"),
                                         N_("LE y BR/EDR a la vez (host)") };
        if (!ex_line(e, N_("Flags"), NULL)) return;
        if (n < 1) { sb_cat(&e->v, T(tr, N_("vacío"))); return; }
        for (int i = 0; i < 5; i++)
            if (p[0] & (1 << i)) { sb_sep(&e->v, ", "); sb_cat(&e->v, T(tr, F[i])); }
        if (!(p[0] & 0x1F)) sb_cat(&e->v, T(tr, N_("ninguno")));
        sb_catf(&e->v, " (0x%02X)", p[0]);
        return;
    }
    case 0x02: case 0x03: ex_uuid16_list(e, N_("Servicios (16 bits)"), p, n, 2); return;
    case 0x04: case 0x05: ex_uuid16_list(e, N_("Servicios (32 bits)"), p, n, 4); return;
    case 0x06: case 0x07:
        for (int k = 0; k + 16 <= n; k += 16) ex_uuid128(e, N_("Servicio 128 bits"), p + k);
        if (n % 16 && ex_line(e, N_("Servicio 128 bits"), NULL)) { sb_cat(&e->v, T(tr, N_("incompleto"))); sb_cat(&e->v, ": "); sb_hex(&e->v, p, n); }
        return;
    case 0x08: ex_text(e, N_("Nombre corto"), p, n); return;
    case 0x09: ex_text(e, N_("Nombre"), p, n); return;
    case 0x0A:
        if (ex_line(e, N_("Potencia TX"), NULL)) { if (n >= 1) sb_catf(&e->v, "%d dBm", (int8_t)p[0]); }
        return;
    case 0x0D:
        if (!ex_line(e, N_("Clase de dispositivo"), NULL)) return;
        if (n >= 3) {
            static const char *const MAJ[] = { N_("Varios"), N_("Computadora"), N_("Teléfono"), N_("Red"),
                                               N_("Audio y video"), N_("Periférico"), N_("Imagen"), N_("Vestible"),
                                               N_("Juguete"), N_("Salud") };
            uint32_t cod = u24le(p);
            int maj = (int)((cod >> 8) & 0x1F);
            sb_catf(&e->v, "0x%06X \xC2\xB7 ", (unsigned)cod);
            sb_cat(&e->v, T(tr, maj < COUNTOF(MAJ) ? MAJ[maj] : N_("Sin categoría")));
        } else sb_hex(&e->v, p, n);
        return;
    case 0x0E: if (ex_line(e, N_("Hash C (SSP)"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x0F: if (ex_line(e, N_("Aleatorio R (SSP)"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x10:
        if (n == 8) {
            if (!ex_line(e, N_("ID de dispositivo"), NULL)) return;
            uint16_t src = u16le(p), vid = u16le(p + 2);
            const char *c = src == 1 ? bl_company_name(vid) : NULL;
            sb_cat(&e->v, src == 1 ? "Bluetooth SIG" : src == 2 ? "USB" : "?");
            sb_catf(&e->v, " 0x%04X", (unsigned)vid);
            if (c) { sb_cat(&e->v, " ("); sb_cat(&e->v, c); sb_cat(&e->v, ")"); }
            sb_cat(&e->v, ", ");
            sb_cat(&e->v, T(tr, N_("producto")));
            sb_catf(&e->v, " 0x%04X, ", (unsigned)u16le(p + 4));
            sb_cat(&e->v, T(tr, N_("versión")));
            sb_catf(&e->v, " 0x%04X", (unsigned)u16le(p + 6));
        } else if (ex_line(e, N_("Clave TK"), NULL)) sb_hex(&e->v, p, n);
        return;
    case 0x11: if (ex_line(e, N_("Flags OOB"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x12:
        if (!ex_line(e, N_("Intervalo de conexión"), NULL)) return;
        if (n >= 4) {
            uint16_t lo = u16le(p), hi = u16le(p + 2);
            if (lo == 0xFFFF) sb_cat(&e->v, "?");
            else sb_fix(&e->v, (int32_t)lo * 125, 2);
            sb_cat(&e->v, " \xE2\x80\x93 ");
            if (hi == 0xFFFF) sb_cat(&e->v, "?");
            else sb_fix(&e->v, (int32_t)hi * 125, 2);
            sb_cat(&e->v, " ms");
        } else sb_hex(&e->v, p, n);
        return;
    case 0x14: ex_uuid16_list(e, N_("Pide servicios (16 bits)"), p, n, 2); return;
    case 0x1F: ex_uuid16_list(e, N_("Pide servicios (32 bits)"), p, n, 4); return;
    case 0x15:
        for (int k = 0; k + 16 <= n; k += 16) ex_uuid128(e, N_("Pide servicio 128 bits"), p + k);
        return;
    case 0x16: ex_sd(e, ad, p, n, 2); return;
    case 0x20: ex_sd(e, ad, p, n, 4); return;
    case 0x21: ex_sd(e, ad, p, n, 16); return;
    case 0x17: ex_addr(e, N_("Destino"), p, n, 0); return;
    case 0x18: ex_addr(e, N_("Destino"), p, n, 1); return;
    case 0x19:
        if (!ex_line(e, N_("Apariencia"), NULL)) return;
        if (n >= 2) {
            uint16_t a = u16le(p);
            const char *nm = bl_appearance_name(a);
            if (nm) { sb_cat(&e->v, T(tr, nm)); sb_cat(&e->v, " "); }
            sb_catf(&e->v, "(0x%04X)", (unsigned)a);
        } else sb_hex(&e->v, p, n);
        return;
    case 0x1A:
        if (!ex_line(e, N_("Intervalo de anuncios"), NULL)) return;
        if (n >= 2) { sb_fix(&e->v, (int32_t)u16le(p) * 625, 3); sb_cat(&e->v, " ms"); }
        else sb_hex(&e->v, p, n);
        return;
    case 0x1B:
        if (n >= 7) ex_addr(e, N_("Dirección LE"), p, 6, p[6] & 1);
        else if (ex_line(e, N_("Dirección LE"), NULL)) sb_hex(&e->v, p, n);
        return;
    case 0x1C: {
        static const char *const R[] = { N_("sólo periférico"), N_("sólo central"),
                                         N_("periférico y central, prefiere periférico"),
                                         N_("periférico y central, prefiere central") };
        if (!ex_line(e, N_("Rol LE"), NULL)) return;
        if (n >= 1 && p[0] < 4) sb_cat(&e->v, T(tr, R[p[0]]));
        else sb_hex(&e->v, p, n);
        return;
    }
    case 0x1D: if (ex_line(e, N_("Hash C-256"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x1E: if (ex_line(e, N_("Aleatorio R-256"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x22: if (ex_line(e, N_("Confirmación LE SC"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x23: if (ex_line(e, N_("Aleatorio LE SC"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x24: {
        /* the scheme is a code point of the URI scheme table, sent as UTF-8 */
        if (!ex_line(e, "URI", NULL)) return;
        if (n < 1) return;
        int skip = 1;
        if (p[0] == 0x16) sb_cat(&e->v, "http:");
        else if (p[0] == 0x17) sb_cat(&e->v, "https:");
        else if (p[0] != 0x01) { sb_catf(&e->v, "[0x%02X]", p[0]); }
        char t2[100];
        name_copy(t2, sizeof t2, p + skip, n - skip);
        sb_cat(&e->v, t2);
        return;
    }
    case 0x25: if (ex_line(e, N_("Posicionamiento en interiores"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x26: if (ex_line(e, N_("Descubrimiento de transporte"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x27: if (ex_line(e, N_("Funciones LE"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x28: if (ex_line(e, N_("Mapa de canales"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x29: if (ex_line(e, "Mesh PB-ADV", NULL)) sb_hex(&e->v, p, n); return;
    case 0x2A: if (ex_line(e, N_("Mensaje mesh"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x2B: if (ex_line(e, N_("Baliza mesh"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x2C: if (ex_line(e, "BIGInfo", NULL)) sb_hex(&e->v, p, n); return;
    case 0x2D: if (ex_line(e, "Broadcast Code", NULL)) sb_hex(&e->v, p, n); return;
    case 0x2E: if (ex_line(e, N_("Identificador de conjunto"), NULL)) sb_hex(&e->v, p, n); return;
    case 0x2F:
        if (!ex_line(e, N_("Intervalo de anuncios"), NULL)) return;
        if (n >= 3 && n <= 4) {
            uint32_t v = n == 3 ? u24le(p) : u32le(p);
            /* 0.625 ms units: in whole milliseconds above what fits in fixed point */
            if (v < 3000000u) { sb_fix(&e->v, (int32_t)(v * 625u), 3); sb_cat(&e->v, " ms"); }
            else { sb_catf(&e->v, "%u", (unsigned)(v / 1600u)); sb_cat(&e->v, " s"); }
        } else sb_hex(&e->v, p, n);
        return;
    case 0x30: ex_text(e, N_("Nombre de difusión"), p, n); return;
    case 0x31: if (ex_line(e, N_("Datos cifrados"), NULL)) sb_hex(&e->v, p, n); return;
    case 0xFF: ex_mfg(e, ad, p, n); return;
    default: {
        char k[8];
        snprintf(k, sizeof k, " 0x%02X", t);
        if (ex_line(e, N_("Tipo"), k)) sb_hex(&e->v, p, n);
        return;
    }
    }
}

int bl_explain(const uint8_t *data, int len, bl_line_t *out, int max, bl_tr_fn tr)
{
    if (!data || !out || max <= 0 || len <= 0) return 0;
    bl_ad_t ad;
    bl_ad_clear(&ad);
    bl_ad_merge(&ad, data, len);
    ex_t e = { out, max, 0, tr, { NULL, 0, 0, true } };
    int i = 0;
    while (i < len && e.n < e.max) {
        int l = data[i];
        if (l == 0) break;
        if (i + 1 + l > len) {
            if (ex_line(&e, N_("Error"), NULL)) {
                sb_cat(&e.v, T(tr, N_("la longitud se pasa del paquete")));
                sb_cat(&e.v, ": ");
                sb_hex(&e.v, data + i, len - i);
            }
            break;
        }
        ex_one(&e, &ad, data[i + 1], data + i + 2, l - 1);
        i += 1 + l;
    }
    return e.n;
}

/* ---- GATT values ---- */

/* IEEE-11073 32-bit FLOAT: a 24-bit signed mantissa and an 8-bit signed
 * exponent of ten. false for its NaN, NRes and infinities. */
static bool ieee_float(const uint8_t *p, sb_t *b)
{
    int32_t m = le_int(p, 3, true);
    int e = (int8_t)p[3];
    if (m == 0x7FFFFF || m == -0x800000 || m == 0x7FFFFE || m == -0x7FFFFE || m == -0x7FFFFF) return false;
    if (e <= 0 && e >= -6) sb_fix(b, m, -e);
    else sb_flt(b, (float)m * powf(10.0f, (float)e), e > 0 ? 0 : 2);
    return true;
}

/* IEEE-11073 16-bit SFLOAT: 12-bit mantissa, 4-bit exponent. */
static bool ieee_sfloat(const uint8_t *p, sb_t *b)
{
    uint16_t raw = u16le(p);
    int32_t m = raw & 0x0FFF;
    int e = (raw >> 12) & 0x0F;
    if (m == 0x07FF || m == 0x0800 || m == 0x07FE || m == 0x0802 || m == 0x0801) return false;
    if (m & 0x0800) m -= 0x1000;
    if (e & 0x08) e -= 16;
    if (e <= 0) sb_fix(b, m, -e);
    else sb_flt(b, (float)m * powf(10.0f, (float)e), 0);
    return true;
}

static void date_time(sb_t *b, const uint8_t *p)
{
    sb_catf(b, "%02u/%02u/%04u %02u:%02u:%02u", p[3], p[2], (unsigned)u16le(p), p[4], p[5], p[6]);
}

static const char *unit_name(uint16_t u)
{
    switch (u) {
    case 0x2700: return "";
    case 0x2701: return "m";
    case 0x2702: return "kg";
    case 0x2703: return "s";
    case 0x2704: return "A";
    case 0x2705: return "K";
    case 0x2722: return "Hz";
    case 0x2723: return "N";
    case 0x2724: return "Pa";
    case 0x2725: return "J";
    case 0x2726: return "W";
    case 0x2728: return "V";
    case 0x272F: return "\xC2\xB0" "C";
    case 0x2731: return "lx";
    case 0x2760: return "min";
    case 0x2761: return "h";
    case 0x2762: return "d";
    case 0x2763: return "\xC2\xB0";
    case 0x27AC: return "\xC2\xB0" "F";
    case 0x27AD: return "%";
    case 0x27AF: return "lpm";
    case 0x27C4: return "ppm";
    case 0x27C5: return "ppb";
    }
    return NULL;
}

static const char *const FORMATS[] = {
    "?", "boolean", "2bit", "nibble", "uint8", "uint12", "uint16", "uint24", "uint32", "uint48",
    "uint64", "uint128", "sint8", "sint12", "sint16", "sint24", "sint32", "sint48", "sint64",
    "sint128", "float32", "float64", "SFLOAT", "FLOAT", "duint16", "utf8s", "utf16s", "struct",
};

static bool fmt_value(uint16_t uuid, const uint8_t *v, int n, sb_t *b, bl_tr_fn tr)
{
    const char *deg = " \xC2\xB0" "C";
    switch (uuid) {
    /* strings */
    case 0x2A00: case 0x2A24: case 0x2A25: case 0x2A26: case 0x2A27: case 0x2A28: case 0x2A29:
    case 0x2901: {
        char t[256];
        if (!bl_value_text(v, n, t, sizeof t)) {
            if (n == 0) { sb_cat(b, T(tr, N_("vacío"))); return true; }
            return false;
        }
        sb_cat(b, t);
        return true;
    }
    case 0x2A19:
        if (n < 1) return false;
        sb_catf(b, "%u %%", v[0]);
        return true;
    case 0x2A6E:
        if (n < 2) return false;
        if (u16le(v) == 0x8000) { sb_cat(b, T(tr, N_("sin dato"))); return true; }
        sb_fix(b, s16le(v), 2);
        sb_cat(b, deg);
        return true;
    case 0x2A6F:
        if (n < 2) return false;
        if (u16le(v) == 0xFFFF) { sb_cat(b, T(tr, N_("sin dato"))); return true; }
        sb_fix(b, u16le(v), 2);
        sb_cat(b, " %");
        return true;
    case 0x2A6D: {
        if (n < 4) return false;
        uint32_t p = u32le(v);                  /* tenths of a pascal */
        sb_fix(b, (int32_t)((p + 50u) / 100u), 1);
        sb_cat(b, " hPa");
        return true;
    }
    case 0x2A1F:
        if (n < 2) return false;
        sb_fix(b, s16le(v), 1);
        sb_cat(b, deg);
        return true;
    case 0x2A20:
        if (n < 2) return false;
        sb_fix(b, s16le(v), 1);
        sb_cat(b, " \xC2\xB0" "F");
        return true;
    case 0x2A7B:
        if (n < 1) return false;
        sb_catf(b, "%d", (int8_t)v[0]);
        sb_cat(b, deg);
        return true;
    case 0x2A76:
        if (n < 1) return false;
        sb_catf(b, "UV %u", v[0]);
        return true;
    case 0x2A6C:
        if (n < 3) return false;
        sb_fix(b, le_int(v, 3, true), 2);
        sb_cat(b, " m");
        return true;
    case 0x2A1C: case 0x2A1E: {
        static const char *const TT[] = { NULL, N_("axila"), N_("cuerpo"), N_("oído"), N_("dedo"),
                                          N_("tracto digestivo"), N_("boca"), N_("recto"), N_("dedo del pie"),
                                          N_("tímpano") };
        if (n < 5) return false;
        uint8_t f = v[0];
        if (!ieee_float(v + 1, b)) sb_cat(b, T(tr, N_("sin dato")));
        else sb_cat(b, f & 1 ? " \xC2\xB0" "F" : deg);
        int i = 5;
        if (f & 0x02) {
            if (i + 7 > n) return true;
            sb_cat(b, " \xC2\xB7 ");
            date_time(b, v + i);
            i += 7;
        }
        if ((f & 0x04) && i < n && v[i] >= 1 && v[i] < COUNTOF(TT)) {
            sb_cat(b, " \xC2\xB7 ");
            sb_cat(b, T(tr, TT[v[i]]));
        }
        return true;
    }
    case 0x2A1D: {
        static const char *const TT[] = { NULL, N_("axila"), N_("cuerpo"), N_("oído"), N_("dedo"),
                                          N_("tracto digestivo"), N_("boca"), N_("recto"), N_("dedo del pie"),
                                          N_("tímpano") };
        if (n < 1 || v[0] < 1 || v[0] >= COUNTOF(TT)) return false;
        sb_cat(b, T(tr, TT[v[0]]));
        return true;
    }
    case 0x2A35: case 0x2A36: {
        if (n < 7) return false;
        uint8_t f = v[0];
        const char *unit = f & 1 ? " kPa" : " mmHg";
        if (!ieee_sfloat(v + 1, b)) sb_cat(b, "?");
        sb_cat(b, "/");
        if (!ieee_sfloat(v + 3, b)) sb_cat(b, "?");
        sb_cat(b, unit);
        int i = 7;
        if (f & 0x02) i += 7;
        if ((f & 0x04) && i + 2 <= n) {
            sb_cat(b, " \xC2\xB7 ");
            if (!ieee_sfloat(v + i, b)) sb_cat(b, "?");
            sb_cat(b, " ");
            sb_cat(b, T(tr, N_("lpm")));
        }
        return true;
    }
    case 0x2A37: {
        if (n < 2) return false;
        uint8_t f = v[0];
        int i = 1, bpm;
        if (f & 0x01) {
            if (n < 3) return false;
            bpm = u16le(v + 1);
            i = 3;
        } else {
            bpm = v[1];
            i = 2;
        }
        sb_catf(b, "%d ", bpm);
        sb_cat(b, T(tr, N_("lpm")));
        sb_t extra;
        char t[80];
        sb_init(&extra, t, sizeof t);
        if (f & 0x04) sb_cat(&extra, T(tr, f & 0x02 ? N_("contacto") : N_("sin contacto")));
        if (f & 0x08) {
            if (i + 2 > n) return true;
            sb_sep(&extra, ", ");
            sb_catf(&extra, "%u kJ", (unsigned)u16le(v + i));
            i += 2;
        }
        if (f & 0x10) {
            bool first = true;
            for (; i + 2 <= n; i += 2) {
                unsigned ms = ((unsigned)u16le(v + i) * 1000u + 512u) / 1024u;
                if (first) { sb_sep(&extra, ", "); sb_catf(&extra, "RR %u", ms); first = false; }
                else sb_catf(&extra, "/%u", ms);
            }
            if (!first) sb_cat(&extra, " ms");
        }
        if (extra.len) { sb_cat(b, " ("); sb_cat(b, t); sb_cat(b, ")"); }
        return true;
    }
    case 0x2A38: {
        static const char *const L[] = { N_("Otro"), N_("Pecho"), N_("Muñeca"), N_("Dedo"), N_("Mano"),
                                         N_("Lóbulo de la oreja"), N_("Pie") };
        if (n < 1 || v[0] >= COUNTOF(L)) return false;
        sb_cat(b, T(tr, L[v[0]]));
        return true;
    }
    case 0x2A01: {
        if (n < 2) return false;
        uint16_t a = u16le(v);
        const char *nm = bl_appearance_name(a);
        if (nm) { sb_cat(b, T(tr, nm)); sb_cat(b, " "); }
        sb_catf(b, "(0x%04X)", (unsigned)a);
        return true;
    }
    case 0x2A50: {
        if (n < 7) return false;
        uint16_t vid = u16le(v + 1);
        const char *c = v[0] == 1 ? bl_company_name(vid) : NULL;
        sb_cat(b, v[0] == 1 ? "Bluetooth SIG" : v[0] == 2 ? "USB" : "?");
        sb_cat(b, ": ");
        if (c) { sb_cat(b, c); sb_cat(b, " "); }
        sb_catf(b, "0x%04X, ", (unsigned)vid);
        sb_cat(b, T(tr, N_("producto")));
        sb_catf(b, " 0x%04X, ", (unsigned)u16le(v + 3));
        sb_cat(b, T(tr, N_("versión")));
        sb_catf(b, " 0x%04X", (unsigned)u16le(v + 5));
        return true;
    }
    case 0x2A23:
        if (n != 8) return false;
        sb_hex(b, v, n);
        return true;
    case 0x2A04: {
        if (n < 8) return false;
        uint16_t lo = u16le(v), hi = u16le(v + 2), lat = u16le(v + 4), to = u16le(v + 6);
        if (lo == 0xFFFF) sb_cat(b, "?");
        else sb_fix(b, (int32_t)lo * 125, 2);
        sb_cat(b, " \xE2\x80\x93 ");
        if (hi == 0xFFFF) sb_cat(b, "?");
        else sb_fix(b, (int32_t)hi * 125, 2);
        sb_cat(b, " ms, ");
        sb_cat(b, T(tr, N_("latencia")));
        sb_catf(b, " %u, ", (unsigned)lat);
        sb_cat(b, T(tr, N_("supervisión")));
        if (to == 0xFFFF) sb_cat(b, " ?");
        else sb_catf(b, " %u ms", (unsigned)to * 10u);
        return true;
    }
    case 0x2A05:
        if (n < 4) return false;
        sb_catf(b, "0x%04X \xE2\x80\x93 0x%04X", (unsigned)u16le(v), (unsigned)u16le(v + 2));
        return true;
    case 0x2A08: case 0x2A2B:
        if (n < 7) return false;
        date_time(b, v);
        return true;
    case 0x2A9D: {
        if (n < 3) return false;
        uint8_t f = v[0];
        uint16_t w = u16le(v + 1);
        if (w == 0xFFFF) { sb_cat(b, T(tr, N_("medición fallida"))); return true; }
        if (f & 1) { sb_fix(b, w, 2); sb_cat(b, " lb"); }
        else { sb_fix(b, (int32_t)(((uint32_t)w * 5u + 5u) / 10u), 2); sb_cat(b, " kg"); }
        return true;
    }
    case 0x2A5B: {
        if (n < 1) return false;
        uint8_t f = v[0];
        int i = 1;
        bool any = false;
        if (f & 1) {
            if (i + 6 > n) return false;
            sb_cat(b, T(tr, N_("rueda")));
            sb_catf(b, " %u ", (unsigned)u32le(v + i));
            sb_cat(b, T(tr, N_("vueltas")));
            i += 6;
            any = true;
        }
        if (f & 2) {
            if (i + 4 > n) return any;
            if (any) sb_cat(b, " \xC2\xB7 ");
            sb_cat(b, T(tr, N_("pedal")));
            sb_catf(b, " %u ", (unsigned)u16le(v + i));
            sb_cat(b, T(tr, N_("vueltas")));
            any = true;
        }
        return any;
    }
    case 0x2A63: {
        if (n < 4) return false;
        uint16_t f = u16le(v);
        sb_catf(b, "%d W", (int)s16le(v + 2));
        if ((f & 1) && n >= 5) {
            sb_cat(b, " \xC2\xB7 ");
            sb_cat(b, T(tr, N_("balance")));
            sb_cat(b, " ");
            sb_fix(b, v[4] * 5, 1);
            sb_cat(b, " %");
        }
        return true;
    }
    case 0x2A53: {
        if (n < 4) return false;
        uint8_t f = v[0];
        uint16_t sp = u16le(v + 1);     /* 1/256 m/s */
        sb_fix(b, (int32_t)(((uint32_t)sp * 100u + 128u) / 256u), 2);
        sb_cat(b, " m/s (");
        sb_fix(b, (int32_t)(((uint32_t)sp * 360u + 128u) / 256u), 2);
        sb_catf(b, " km/h), %u ", v[3]);
        sb_cat(b, T(tr, N_("pasos/min")));
        sb_cat(b, ", ");
        sb_cat(b, T(tr, f & 0x04 ? N_("corriendo") : N_("caminando")));
        return true;
    }
    /* descriptors */
    case 0x2902: {
        if (n < 2) return false;
        uint16_t c = u16le(v);
        if ((c & 3) == 3) sb_cat(b, T(tr, N_("notificaciones e indicaciones")));
        else if (c & 1) sb_cat(b, T(tr, N_("notificaciones")));
        else if (c & 2) sb_cat(b, T(tr, N_("indicaciones")));
        else sb_cat(b, T(tr, N_("apagado")));
        return true;
    }
    case 0x2903:
        if (n < 2) return false;
        sb_cat(b, T(tr, u16le(v) & 1 ? N_("difusión") : N_("apagado")));
        return true;
    case 0x2900: {
        if (n < 2) return false;
        uint16_t c = u16le(v);
        if (c & 1) sb_cat(b, T(tr, N_("escritura confiable")));
        if (c & 2) { sb_sep(b, ", "); sb_cat(b, T(tr, N_("descripción escribible"))); }
        if (!(c & 3)) sb_cat(b, T(tr, N_("ninguna")));
        return true;
    }
    case 0x2904: {
        if (n < 7) return false;
        sb_cat(b, v[0] < COUNTOF(FORMATS) ? FORMATS[v[0]] : "?");
        if ((int8_t)v[1]) sb_catf(b, " \xC3\x97" "10^%d", (int8_t)v[1]);
        uint16_t u = u16le(v + 2);
        const char *un = unit_name(u);
        if (un && *un) { sb_cat(b, " "); sb_cat(b, un); }
        else if (!un) sb_catf(b, " (%s 0x%04X)", T(tr, N_("unidad")), (unsigned)u);
        return true;
    }
    case 0x2907:
        if (n < 2) return false;
        sb_catf(b, "0x%04X", (unsigned)u16le(v));
        if (bl_uuid16_name(u16le(v))) { sb_cat(b, " "); sb_cat(b, bl_uuid16_name(u16le(v))); }
        return true;
    case 0x2908: {
        static const char *const RT[] = { NULL, N_("entrada"), N_("salida"), N_("característica") };
        if (n < 2) return false;
        sb_cat(b, T(tr, N_("reporte")));
        sb_catf(b, " %u", v[0]);
        if (v[1] >= 1 && v[1] <= 3) { sb_cat(b, ", "); sb_cat(b, T(tr, RT[v[1]])); }
        return true;
    }
    case 0x2905:
        if (n < 2 || n % 2) return false;
        for (int i = 0; i + 2 <= n; i += 2) {
            sb_sep(b, ", ");
            sb_catf(b, "0x%04X", (unsigned)u16le(v + i));
        }
        return true;
    default: break;
    }
    return false;
}

bool bl_value_format(uint16_t uuid16, const uint8_t *v, int n, char *out, size_t sz, bl_tr_fn tr)
{
    if (!out || !sz) return false;
    out[0] = 0;
    if (n < 0 || (!v && n > 0)) return false;
    static const uint8_t none = 0;
    sb_t b;
    sb_init(&b, out, sz);
    if (!fmt_value(uuid16, v ? v : &none, n, &b, tr)) {
        out[0] = 0;
        return false;
    }
    return true;
}

/* ---- distance ---- */

float bl_distance_m(int rssi, int p1m, float n)
{
    if (!(n > 0.5f)) n = 2.0f;
    return powf(10.0f, (float)(p1m - rssi) / (10.0f * n));
}

int bl_p1m_guess(const bl_ad_t *ad, const bl_beacon_t *bc)
{
    if (bc) {
        if ((bc->kind == BL_BEACON_IBEACON || bc->kind == BL_BEACON_ALT) && bc->tx1m) return bc->tx1m;
        if ((bc->kind == BL_BEACON_EDDY_UID || bc->kind == BL_BEACON_EDDY_URL || bc->kind == BL_BEACON_EDDY_EID) &&
            bc->tx1m)
            return bc->tx1m - 41;
    }
    if (ad && ad->has_tx) return ad->tx_power - 41;
    return -59;
}
