/*
 * BLE - a Bluetooth LE scanner and analyser for AmoledOS (apps/ble/README.md),
 * ported from P4OS's.
 *
 * What is shared between the screens: the table of devices heard
 * (bl_scan.c), the counters of the air, the settings, and the UI's helpers
 * (ble.c). Each screen is a file: bl_list.c (who is around), bl_radar.c (the
 * radar and the finder), bl_sensors.c (readings broadcast), bl_air.c (the
 * statistics), bl_detail.c (one device), bl_gatt.c (its services, connected).
 *
 * Everything runs on LVGL's thread: the HAL's rings are drained from a timer.
 */
#pragma once

#include "bl_decode.h"

#include "aos_app.h"
#include "aos_hal.h"
#include "aos_i18n.h"
#include "aos_theme.h"
#include "aos_ui.h"
#include "aos_fonts.h"
#include "bl_glyphs.h"

#include <stdbool.h>
#include <stdint.h>

/* The table starts with room for BL_DEV_START and doubles when it fills, up
 * to BL_DEV_MAX (~0.8 KB each, in PSRAM): a fair or a station has hundreds
 * of phones, each changing its private address every few minutes. Past
 * the top, the one not heard for longest makes room. */
#define BL_DEV_START   256
#define BL_DEV_MAX     1024
#define BL_SH_MAX      64           /* sensors whose two hours are kept */
#define BL_HIST        120          /* seconds of RSSI kept per device */
#define BL_SEN_HIST    120          /* sensor readings kept per device, one a minute: two hours */
#define BL_AIR_HIST    120          /* seconds of the air's counters */
#define BL_GONE_MS     30000        /* not heard for this long: out of the "now" counts */
#define BL_NO_RSSI     (-128)

/* The watch's fonts under the names the screens were written with on P4OS's
 * larger screen: caption and small are the same 16 px here, tiny is 14. */
#define aos_font_caption  aos_font_small
#define aos_font_tiny     (&aos_montserrat_14)
#define aos_font_large    aos_font_huge
#define AOS_UI_PAD        14
#define AOS_UI_RADIUS     18

#define BL_C           lv_color_hex(0x3B82F6)      /* the app's blue */
#define BL_C_D         lv_color_hex(0x1D4ED8)
#define BL_C_KEYTOP    lv_color_hex(0xF2F2F7)

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    uint8_t kinds;                  /* bit per AOS_BLE_ADV_* seen */
    uint32_t first_ms, last_ms;
    uint32_t n_adv, n_rsp, n_changes;
    int8_t rssi, rssi_min, rssi_max;
    float rssi_avg;                 /* exponential, a few seconds */
    int8_t hist[BL_HIST];           /* the strongest of each second, BL_NO_RSSI none */
    uint32_t hist_sec;              /* the second the last bin belongs to */
    uint32_t prev_adv_ms;           /* for the interval */
    float itvl_ms;                  /* estimate: the low envelope of the gaps */
    uint8_t adv[31], adv_len;       /* the last packets, raw */
    uint8_t rsp[31], rsp_len;
    bl_ad_t ad;                     /* merged */
    bl_class_t cls;
    const char *label;              /* bl_classify()'s, N_() */
    bool has_sen, has_bc;
    bl_sensor_t sen;
    bl_beacon_t bc;
    uint32_t sen_ms;
    int16_t sh;                     /* its sensor history in BL.sh, -1 none */
    bool fav;
    char alias[28];
    bool has_key;                   /* its key for encrypted advertisements, from ble/claves.txt */
    uint8_t key[16];
    int8_t key_state;               /* BL_KEY_* of the last encrypted packet, -1 none yet */
} bl_dev_t;

/* A sensor's last two hours, one reading a minute, in tenths (INT16_MIN
 * none): kept apart from the devices, since few of them are sensors. */
typedef struct bl_senhist {
    int16_t t[BL_SEN_HIST], h[BL_SEN_HIST];
    uint32_t slot;                  /* the minute of the last reading */
    int16_t dev;                    /* its device, -1 free */
} bl_senhist_t;

typedef struct {
    uint32_t sec;                   /* the second of the last bin */
    uint16_t pkts[BL_AIR_HIST];     /* packets heard each second */
    uint16_t devs[BL_AIR_HIST];     /* devices heard each second */
    uint32_t total, lost;
    float pps;                      /* packets a second, smoothed */
} bl_air_t;

enum { BL_TAB_LIST, BL_TAB_RADAR, BL_TAB_SENS, BL_TAB_AIR, BL_TAB_COUNT };
enum { BL_SORT_RSSI, BL_SORT_NAME, BL_SORT_RECENT, BL_SORT_COUNT };
enum { BL_FILT_ALL, BL_FILT_NAMED, BL_FILT_FAV, BL_FILT_CONN, BL_FILT_SENSOR, BL_FILT_BEACON, BL_FILT_APPLE, BL_FILT_COUNT };
enum { BL_PAGE_TAB, BL_PAGE_DETAIL, BL_PAGE_FINDER, BL_PAGE_GATT };
enum { BL_LOST_PHONE = 1, BL_LOST_WIFI = 4 };

typedef struct {
    /* settings, kept in preferences */
    int tab, sort, filter, env, min_rssi;
    bool active, paused, sound, log_csv, hide_gone;
    int duty;

    /* the table */
    bl_dev_t *dev;                  /* cap records, PSRAM */
    int ndev, cap;
    uint32_t seen_total;            /* devices heard since the app opened, forgotten ones too */
    uint32_t forgotten;             /* made room for another when the table was full */
    int16_t *hash;                  /* address -> index, open addressing, BL_HASH slots */
    struct bl_senhist *sh;          /* BL_SH_MAX, PSRAM */
    uint32_t gen;                   /* bumped when a device is added or forgotten */
    bl_air_t air;
    aos_ble_adv_t *rx;              /* the drain's buffer, PSRAM */
    bool bt_off;                    /* the scan cannot start: Bluetooth is off */
    /* links that went down while scanning (BL_LOST_*), told on screen until
     * dismissed: the C6 has one radio for the phone, the computer and the
     * Wi-Fi, and listening takes air time from them */
    uint8_t lost, was_up;
    uint32_t lost_ms;
    uint32_t started_ms;

    /* the UI */
    aos_app_t *self;
    lv_obj_t *root, *content, *tabbar, *tabs[BL_TAB_COUNT + 1];   /* the pages and the settings */
    lv_obj_t *overlay;              /* a sheet or a keyboard over everything */
    int W, H, cw, ch;
    bool land, hidden;
    int page;                       /* BL_PAGE_* */
    int sel;                        /* the device open in detail/finder/gatt, -1 none */
    uint8_t sel_addr[6];            /* the same, by address: indexes move when one is forgotten */
    lv_timer_t *timer;
    uint32_t ticks;
} bl_t;

extern bl_t BL;

/* lroundf is not in the firmware's table */
static inline int lroundf_safe(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

/* A text cut to fit by bytes may end in half a UTF-8 character: drop it. */
static inline void bl_utf8_trim(char *s)
{
    int n = 0;
    while (s[n]) n++;
    int i = n;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;   /* continuation bytes */
    if (i > 0 && ((unsigned char)s[i - 1] & 0x80)) {
        unsigned char c = (unsigned char)s[i - 1];
        int want = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (n - (i - 1) < want) s[i - 1] = 0;
    }
}

/* ---- bl_scan.c: the table ---- */
bool bl_scan_init(void);
void bl_scan_free(void);
void bl_scan_apply(void);           /* start/stop/parameters as the settings say */
void bl_scan_drain(void);           /* from the timer */
int  bl_find(const uint8_t addr[6]);
/* A sensor's reading of a minute (slot = uptime / 60 s), NAN when none;
 * and the minute of its last one, 0 when it has no history. */
float    bl_sen_at(const bl_dev_t *d, bool hum, uint32_t slot);
uint32_t bl_sen_last(const bl_dev_t *d);
bool bl_alive(const bl_dev_t *d);   /* heard in the last BL_GONE_MS */
const char *bl_dev_name(const bl_dev_t *d);   /* alias, name, label or company: ready to show */
void bl_dev_sub(const bl_dev_t *d, char *out, size_t n);   /* the second line of a row */
void bl_sensor_line(const bl_sensor_t *s, char *out, size_t n);
int  bl_rssi_recent(const bl_dev_t *d, int secs);   /* strongest in the last secs, BL_NO_RSSI */
void bl_forget_all(void);
void bl_names_save(void);           /* favourites and aliases to /sdcard/ble/nombres.txt */
void bl_names_poll(void);           /* re-read it if the portal changed it */
/* A device's key for its encrypted advertisements (MiBeacon v4/v5, BTHome
 * v2): kept in ble/claves.txt, NULL clears it. */
void bl_key_set(int idx, const uint8_t *key);
void bl_redecode(bl_dev_t *d);      /* what it said, again: after a new key */
int  bl_sorted(int *out, int max, int filter, int sort);
const char *bl_class_glyph(bl_class_t c);
lv_color_t bl_class_color(bl_class_t c);
float bl_env_n(void);               /* the path loss exponent of the chosen surroundings */
void bl_fmt_addr(const uint8_t a[6], char *out, size_t n);
bool bl_parse_addr(const char *s, uint8_t a[6]);
void bl_fmt_num(char *out, size_t n, float v, int dec);
void bl_fmt_age(char *out, size_t n, uint32_t ms);
void bl_lost_text(char *out, size_t n);   /* "el teléfono y el Wi-Fi", "" none */

/* ---- ble.c: UI helpers and navigation ---- */
lv_obj_t *bl_box(lv_obj_t *parent, int32_t w, int32_t h);
lv_obj_t *bl_card(lv_obj_t *parent, int32_t w, int32_t h);
lv_obj_t *bl_vcard(lv_obj_t *parent, int32_t w, int32_t pad, int32_t gap);
lv_obj_t *bl_column(lv_obj_t *parent, int32_t w, int32_t h);
lv_obj_t *bl_row(lv_obj_t *parent, int32_t w, int32_t h, int32_t gap);
lv_obj_t *bl_wrap(lv_obj_t *parent, int32_t w, int32_t gap);
lv_obj_t *bl_pill(lv_obj_t *parent, const char *glyph, const char *text, lv_color_t bg, lv_event_cb_t cb, void *ud);
void      bl_pill_set(lv_obj_t *b, const char *glyph, const char *text, lv_color_t bg);
lv_obj_t *bl_chip(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb, void *ud);
lv_obj_t *bl_caption(lv_obj_t *parent, const char *text, int32_t w);
lv_obj_t *bl_section(lv_obj_t *parent, const char *text);
lv_obj_t *bl_round_icon(lv_obj_t *parent, const char *glyph, lv_color_t col, int32_t size);
lv_obj_t *bl_back_bar(lv_obj_t *parent, int32_t w, const char *title, lv_event_cb_t back_cb);
lv_obj_t *bl_kv(lv_obj_t *parent, int32_t w, const char *key, const char *val);   /* a two-column line */
lv_obj_t *bl_canvas_obj(lv_obj_t *parent, int32_t w, int32_t h, lv_event_cb_t draw_cb);
void bl_draw_text(lv_layer_t *layer, const char *t, const lv_font_t *f, lv_color_t c, int32_t x, int32_t y, int32_t w, lv_text_align_t al);
void bl_draw_line(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, int32_t w, lv_opa_t opa);
void bl_fill(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, lv_opa_t opa, int32_t r);
void bl_spark(lv_layer_t *layer, const lv_area_t *a, const int8_t *hist, int n, int newest, lv_color_t c);
lv_color_t bl_rssi_color(int rssi);
void bl_text_entry(const char *title, const char *value, bool hex_kb, void (*done)(const char *));
void bl_overlay_close(void);
void bl_settings_save(void);
void bl_rebuild(void);              /* the current page, again */
void bl_open_detail(int idx);
void bl_open_finder(int idx);
void bl_open_gatt(int idx);
void bl_go_tab(void);               /* back to the tab's page */
void bl_scan_status(char *out, size_t n);   /* "24 cerca · 180 paq/s · activo 30 %" */
lv_obj_t *bl_bt_off_card(lv_obj_t *parent, int32_t w);   /* "Bluetooth apagado" with its switch */

/* ---- the screens ---- */
void bl_list_build(lv_obj_t *page);
void bl_list_refresh(void);
void bl_radar_build(lv_obj_t *page);
void bl_radar_refresh(void);
void bl_sens_build(lv_obj_t *page);
void bl_sens_refresh(void);
void bl_air_build(lv_obj_t *page);
void bl_air_refresh(void);
void bl_detail_build(lv_obj_t *page);
void bl_detail_refresh(void);
void bl_finder_build(lv_obj_t *page);
void bl_finder_refresh(void);
void bl_finder_beep(void);          /* from the timer, every tick */
void bl_gatt_build(lv_obj_t *page);
void bl_gatt_refresh(void);
void bl_gatt_close(void);           /* leaving the app or the page */
void bl_page_gone(void);            /* forget every page's object pointers */


/* ---- bl_scan.c: what is kept ---- */
void bl_log_tick(void);             /* the CSV, once a second */
