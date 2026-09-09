/*
 * CLIMA - data: network, JSON and weather codes.
 *
 * The service is Open-Meteo: free, no key, no registration and no declared
 * limit for personal use. It is queried over plain HTTP (see aos_hal_http_get)
 * and with timeformat=unixtime, so every date arrives as an integer and there
 * is no need to parse ISO strings or to drag in time zone handling: the JSON
 * itself carries utc_offset_seconds and local time is an addition.
 *
 * All in integers. Temperatures go in tenths of a degree (114 = 11.4 C).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WX_DAYS       7
#define WX_HOURS     12
#define WX_PLACES     6         /* results of a search */

/* Download buffer. Measured: the full forecast is ~1.5 KB and the city search
 * ~2 KB; 6 KB leaves room for long names and new fields. */
#define WX_BUF_BYTES 6144

typedef struct {
    bool     ok;
    int32_t  fetched;           /* UTC unixtime of when it was fetched */
    int32_t  utc_offset;        /* seconds, of the location queried */

    /* now */
    int32_t  now_t;             /* UTC unixtime of the measurement */
    int      temp10, feels10, hum, wind10, gust10, code, is_day;

    /* per day */
    int      days;
    int32_t  d_t[WX_DAYS];      /* local midnight, in UTC unixtime */
    int      d_code[WX_DAYS], d_max10[WX_DAYS], d_min10[WX_DAYS], d_pop[WX_DAYS];
    int32_t  d_sunrise[WX_DAYS], d_sunset[WX_DAYS];

    /* per hour, the next WX_HOURS */
    int      hours;
    int32_t  h_t[WX_HOURS];
    int      h_temp10[WX_HOURS], h_code[WX_HOURS], h_pop[WX_HOURS];
} wx_data_t;

typedef struct {
    char    name[40];
    char    region[40];         /* province or state */
    char    country[32];
    int32_t lat10k, lon10k;     /* degrees x 10000 */
} wx_place_t;

/* --- network: the two requests the app makes ------------------------------ */

/* They fire a GET and return the HAL's id (> 0), or < 0. They do not block. */
int wx_fetch_start(int32_t lat10k, int32_t lon10k);
int wx_search_start(const char *query);

/* --- reading the JSON ----------------------------------------------------- */

bool wx_parse(const char *json, int len, wx_data_t *out);
int  wx_places_parse(const char *json, int len, wx_place_t *out, int max);

/* --- WMO codes ------------------------------------------------------------ */

const char *wx_text(int code);          /* "Parcialmente nublado" */

typedef enum {
    WX_ICON_CLEAR = 0,
    WX_ICON_FEWCLOUDS,
    WX_ICON_CLOUDY,
    WX_ICON_FOG,
    WX_ICON_DRIZZLE,
    WX_ICON_RAIN,
    WX_ICON_SNOW,
    WX_ICON_STORM,
    WX_ICON_COUNT,
} wx_icon_t;

wx_icon_t wx_icon_of(int code);

/* --- utilities ------------------------------------------------------------ */

/* Montserrat only brings ASCII, so "Córdoba" would come out with little boxes.
 * This turns the response's UTF-8 into ASCII (á->a, ñ->n, ü->u...). */
void wx_ascii(const char *utf8, char *out, int max);

/* Day of the week (0 = Sunday) and local time from a UTC unixtime plus the
 * location's offset. */
int  wx_wday(int32_t utc, int32_t offset);
int  wx_hour(int32_t utc, int32_t offset);
int  wx_minute(int32_t utc, int32_t offset);
int  wx_mday(int32_t utc, int32_t offset);
int  wx_month(int32_t utc, int32_t offset);

/* "-34.6131" -> -346131, that is, degrees x 10000. */
int32_t wx_deg_parse(const char *text);

/* Writes a temperature in tenths as "11" or "-3" (rounded). */
void wx_temp_str(int t10, char *out, int max);

/* --- local copy ----------------------------------------------------------- */

/* Stores and retrieves the last good JSON, so the app shows something the
 * moment it opens even with no network yet. It lives in the data directory.
 *
 * It has the coordinates it came from baked in: if on opening the app the
 * location is a different one, what is stored is useless and wx_cache_load()
 * returns 0. Without that, changing city with no network showed the old
 * forecast under the new name. */
bool wx_cache_save(const char *json, int len, int32_t lat10k, int32_t lon10k);
int  wx_cache_load(char *out, int max, int32_t lat10k, int32_t lon10k);
