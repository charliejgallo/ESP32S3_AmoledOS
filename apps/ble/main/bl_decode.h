/*
 * BLE - what the bytes of an advertisement mean (bl_decode.c, bl_names.c).
 *
 * Pure C with no LVGL and no HAL, so apps/ble/test builds it on the Mac and
 * checks every format against packets written by hand. Texts meant for the
 * screen are marked N_() and stay in Spanish: the caller passes them through
 * aos_tr() (_()) when it shows them. Company and product names are proper
 * names and are not marked.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef N_
#define N_(s) (s)
#endif

/* ---- the AD structures of one device, adv and scan response merged ---- */

#define BL_MAX_U16   12
#define BL_MAX_U128  2
#define BL_MAX_SD    4
#define BL_MAX_MFG   2

typedef struct {
    uint16_t uuid;              /* 16-bit; a 32-bit one keeps its low 16 bits, a 128-bit one its
                                   bytes 12-13 (the 16 bits of a UUID on the SIG's base)... */
    uint8_t  uuid_len;          /* ...and says which it was: 2, 4 or 16 */
    uint8_t  len;
    uint8_t  data[29];          /* after the UUID */
} bl_sd_t;

typedef struct {
    uint16_t company;           /* the first two bytes, little endian */
    uint8_t  len;
    uint8_t  data[29];          /* after the company id */
} bl_mfg_t;

typedef struct {
    char     name[32];          /* UTF-8, cut at 31 bytes */
    bool     name_complete;     /* 0x09 rather than 0x08 */
    bool     has_flags, has_tx, has_appearance, has_interval;
    uint8_t  flags;
    int8_t   tx_power;          /* dBm, 0x0A */
    uint16_t appearance;        /* 0x19 */
    uint16_t interval;          /* 0x1A, in 0.625 ms units */
    uint16_t u16[BL_MAX_U16];   /* 0x02/0x03 and the 16-bit solicitations 0x14 */
    int      n16;
    uint8_t  u128[BL_MAX_U128][16];   /* 0x06/0x07, little endian as on the air */
    int      n128;
    bl_sd_t  sd[BL_MAX_SD];     /* 0x16, 0x20, 0x21 */
    int      nsd;
    bl_mfg_t mfg[BL_MAX_MFG];   /* 0xFF */
    int      nmfg;
    bool     malformed;         /* a length ran past the end */
} bl_ad_t;

/* Clears *out. */
void bl_ad_clear(bl_ad_t *out);
/* Parses one packet's AD structures INTO *out: a name, a UUID or a service
 * data already there is replaced or added to, so the advertisement and its
 * scan response end up in one place. Repeated UUIDs are not added twice;
 * service data and manufacturer data with the same key are replaced, and a
 * new key when the table is full pushes out the oldest one (the newest is
 * always last). */
void bl_ad_merge(bl_ad_t *out, const uint8_t *data, int len);

/* ---- one line of explanation, for the detail screen and the portal ---- */

typedef struct {
    char key[28];               /* "Flags", "Fabricante", "Servicio 0x181A"... (Spanish) */
    char val[100];              /* "LE General Discoverable, sin BR/EDR" */
} bl_line_t;

/* Every AD structure of one raw packet, explained in plain words: type, and
 * what it says (flags spelled out, UUIDs with their names, the company,
 * Apple's Continuity messages, beacons, sensor formats...). Returns how many
 * lines were written. The keys and values are ready to show (Spanish words
 * already passed through the translation function given, or kept if NULL). */
typedef const char *(*bl_tr_fn)(const char *es);
int bl_explain(const uint8_t *data, int len, bl_line_t *out, int max, bl_tr_fn tr);

/* ---- sensors that broadcast their readings ---- */

enum {
    BL_V_TEMP     = 1u << 0,    /* °C */
    BL_V_HUM      = 1u << 1,    /* % */
    BL_V_PRESS    = 1u << 2,    /* hPa */
    BL_V_BATT     = 1u << 3,    /* % */
    BL_V_VOLT     = 1u << 4,    /* V */
    BL_V_CO2      = 1u << 5,    /* ppm */
    BL_V_PM25     = 1u << 6,    /* µg/m³ */
    BL_V_LUX      = 1u << 7,
    BL_V_MOIST    = 1u << 8,    /* soil moisture % */
    BL_V_COND     = 1u << 9,    /* µS/cm */
    BL_V_WEIGHT   = 1u << 10,   /* kg */
    BL_V_OPEN     = 1u << 11,   /* window/door: 1 open */
    BL_V_MOTION   = 1u << 12,   /* 1 moving / detected */
    BL_V_BUTTON   = 1u << 13,   /* an event: 1 press, 2 double, 3 triple, 4 long... */
    BL_V_ACC      = 1u << 14,   /* acc_x/y/z in g */
    BL_V_COUNT    = 1u << 15,   /* a packet or movement counter */
    BL_V_RSSI1M   = 1u << 16,   /* calibrated power at 1 m, from a beacon */
    BL_V_DEW      = 1u << 17,   /* dew point °C */
    BL_V_TVOC     = 1u << 18,   /* ppb */
    BL_V_POWER    = 1u << 19,   /* W */
    BL_V_ENERGY   = 1u << 20,   /* kWh */
    BL_V_DIST     = 1u << 21,   /* mm */
    BL_V_ROTATION = 1u << 22,   /* ° */
    BL_V_HR       = 1u << 23,   /* heart rate, bpm */
};

typedef struct {
    uint32_t mask;              /* BL_V_* that came */
    const char *format;         /* "BTHome v2", "pvvx", "ATC", "MiBeacon", "Govee", "Ruuvi RAWv2",
                                   "SwitchBot", "Qingping", "Inkbird", "Eddystone TLM"... */
    bool encrypted;             /* a known format whose payload is encrypted: no values */
    float temp, hum, press, volt, dew, co2, pm25, lux, moist, cond, weight, tvoc, power, energy, dist, rotation;
    float acc_x, acc_y, acc_z;
    int batt, open, motion, button, count, hr;
    int8_t rssi1m;
} bl_sensor_t;

/* What a device's merged AD says it measures. addr is the advertiser's (some
 * formats repeat it inside and that is how they are told apart from noise).
 * false if no known format is found. */
bool bl_sensor_decode(const bl_ad_t *ad, const uint8_t addr[6], bl_sensor_t *out);

/* The same, for an encrypted MiBeacon (v4/v5) or BTHome v2 with the
 * device's 16-byte key: decrypts and decodes. Returns one of BL_KEY_*. */
enum { BL_KEY_OK = 0, BL_KEY_NOT_ENCRYPTED, BL_KEY_WRONG, BL_KEY_UNSUPPORTED };
int bl_sensor_decode_key(const bl_ad_t *ad, const uint8_t addr[6], const uint8_t key[16], bl_sensor_t *out);

/* ---- beacons ---- */

enum { BL_BEACON_NONE, BL_BEACON_IBEACON, BL_BEACON_ALT, BL_BEACON_EDDY_UID, BL_BEACON_EDDY_URL,
       BL_BEACON_EDDY_TLM, BL_BEACON_EDDY_EID };

typedef struct {
    int kind;                   /* BL_BEACON_* (an Eddystone that sends several frames: the last seen) */
    uint8_t uuid[16];           /* iBeacon/AltBeacon proximity UUID little endian (bl_uuid_str() prints it),
                                   or Eddystone namespace(10)+instance(6) as sent, or EID(8) */
    uint16_t major, minor;
    int8_t tx1m;                /* calibrated RSSI at 1 m (iBeacon), or Eddystone's at 0 m */
    char url[48];               /* Eddystone-URL, expanded */
} bl_beacon_t;

bool bl_beacon_decode(const bl_ad_t *ad, bl_beacon_t *out);

/* ---- what kind of thing it is ---- */

typedef enum {
    BL_CLS_UNKNOWN = 0,
    BL_CLS_PHONE,
    BL_CLS_COMPUTER,
    BL_CLS_TABLET,
    BL_CLS_WATCH,
    BL_CLS_AUDIO,               /* earbuds, headphones, speakers */
    BL_CLS_TRACKER,             /* AirTag, Find My, Tile, SmartTag, Chipolo */
    BL_CLS_SENSOR,              /* a thermometer or any broadcaster of readings */
    BL_CLS_BEACON,
    BL_CLS_HID,                 /* keyboard, mouse, remote */
    BL_CLS_GAMEPAD,
    BL_CLS_TV,
    BL_CLS_HEALTH,              /* heart rate, scales, glucose, thermometers by GATT */
    BL_CLS_LIGHT,               /* bulbs, strips */
    BL_CLS_FITNESS,             /* bikes, cadence, power */
    BL_CLS_DEVBOARD,            /* Espressif, Nordic's UART, ... */
    BL_CLS_COUNT
} bl_class_t;

/* The class and a short label for a device without a name: "AirPods",
 * "iPhone", "Mac", "AirTag (lejos del dueño)", "Mouse (Swift Pair)",
 * "Termómetro Govee"... (Spanish, N_()). label may come back NULL. */
bl_class_t bl_classify(const bl_ad_t *ad, const char **label);
const char *bl_class_name(bl_class_t c);    /* N_("Teléfono")... */

/* Apple's Continuity: the type of each message in a manufacturer data of
 * company 0x004C ("Nearby Info", "Handoff", "Find My", "AirPods"...).
 * Returns how many types were written. */
int bl_apple_types(const bl_mfg_t *m, const char **out, int max);

/* ---- names ---- */

const char *bl_company_name(uint16_t id);          /* "Apple, Inc." ; NULL if unknown */
const char *bl_uuid16_name(uint16_t uuid);         /* services, characteristics, descriptors,
                                                      member UUIDs (0xFExx); NULL if unknown */
const char *bl_uuid128_name(const uint8_t uuid[16]);   /* Nordic UART, the well known vendor ones */
const char *bl_appearance_name(uint16_t a);        /* category, or category + subcategory */
/* "0x180D" for a 16-bit, the canonical 8-4-4-4-12 form for a 128-bit
 * (little endian in, big endian text out). */
void bl_uuid_str(const uint8_t *uuid, int len, char *out, size_t n);

/* ---- addresses ---- */

typedef enum { BL_ADDR_PUBLIC, BL_ADDR_STATIC, BL_ADDR_RPA, BL_ADDR_NRPA } bl_addr_kind_t;
/* From the address type and the two top bits of a random address. */
bl_addr_kind_t bl_addr_kind(const uint8_t addr[6], uint8_t addr_type);
const char *bl_addr_kind_name(bl_addr_kind_t k);  /* N_("Pública"), N_("Aleatoria estática"),
                                                      N_("Privada resoluble"), N_("Privada no resoluble") */

/* ---- GATT values ---- */

/* A characteristic's or descriptor's value in plain words when its UUID is a
 * known one: battery "78 %", temperature 0x2A6E "27,30 °C", heart rate 0x2A37
 * "92 lpm (contacto, RR 668 ms)", body sensor location, appearance, PnP ID,
 * CCCD "notificaciones", presentation format 0x2904, the strings of Device
 * Information... false if the UUID is unknown or the value does not fit it.
 * Decimal commas, like the rest of the UI. */
bool bl_value_format(uint16_t uuid16, const uint8_t *v, int n, char *out, size_t sz, bl_tr_fn tr);
/* Any value: the text if it is all printable UTF-8, else nothing. */
bool bl_value_text(const uint8_t *v, int n, char *out, size_t sz);
/* "01 A2 FF ..." */
void bl_hex(const uint8_t *v, int n, char *out, size_t sz);

/* ---- distance ---- */

/* Log-distance path loss: d = 10 ^ ((P1m - rssi) / (10 n)), in metres.
 * P1m is the beacon's calibrated power when it says it, else the TX power
 * advertised minus 41 dB, else -59. n = 2 in the open, ~2.7 indoors. */
float bl_distance_m(int rssi, int p1m, float n);
int   bl_p1m_guess(const bl_ad_t *ad, const bl_beacon_t *bc);
