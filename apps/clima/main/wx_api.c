/*
 * CLIMA - data: network, JSON and weather codes.
 *
 * The JSON reader is deliberately homespun: it builds no tree, it looks for
 * "key": inside a byte range and reads what follows. That is enough because
 * the shape of the response is fixed by us in the query, and it avoids putting
 * cJSON into the firmware's symbol table.
 *
 * The one subtlety is that there are repeated keys: "temperature_2m" appears
 * in current_units, in current and in hourly. So the block is bounded first
 * ("current":{...}) and the search happens inside it.
 *
 * Both queries have gone over https:// since 2026-09-03, when the HAL learned
 * TLS. For the app the change was one letter in each URL: the handshake, the
 * certificates and session resumption live inside aos_hal_http_get(). What DID
 * change is that a failure mode appeared that did not exist before —the
 * freshly powered board has no time yet, and without time a certificate cannot
 * be verified—, and that is handled in clima.c.
 */
#include "wx_api.h"
#include "aos_hal.h"
#include "aos_i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* JSON reader                                                                 */
/* -------------------------------------------------------------------------- */

/* First character of "key"'s value within [from, end). */
static const char *j_find(const char *from, const char *end, const char *key)
{
    char pat[40];
    int n = snprintf(pat, sizeof(pat), "\"%s\":", key);
    if (n <= 0 || n >= (int)sizeof(pat) || !from) {
        return NULL;
    }
    const char *p = strstr(from, pat);
    if (!p || p + n > end) {
        return NULL;
    }
    return p + n;
}

/* End of the block starting at '{' or at '['. */
static const char *j_block_end(const char *start)
{
    if (!start || (*start != '{' && *start != '[')) {
        return start;
    }
    char open  = *start;
    char close = (open == '{') ? '}' : ']';
    int depth = 0, in_str = 0;
    for (const char *p = start; *p; p++) {
        if (in_str) {
            if (*p == '\\') { p++; continue; }
            if (*p == '"')  { in_str = 0; }
            continue;
        }
        if (*p == '"')   { in_str = 1; continue; }
        if (*p == open)  { depth++; continue; }
        if (*p == close) { depth--; if (depth == 0) return p + 1; }
    }
    return start + strlen(start);
}

/* An ordinary integer. Used for the unixtimes, which multiplied by ten would
 * run out of an int32. */
static int32_t j_int(const char *p, bool *ok)
{
    if (ok) *ok = false;
    if (!p) return 0;
    while (*p == ' ') p++;
    int32_t sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (*p < '0' || *p > '9') return 0;
    int32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    if (ok) *ok = true;
    return sign * v;
}

/* Number in tenths: "11.4" -> 114, "-3" -> -30. */
static int j_dec10(const char *p, bool *ok)
{
    if (ok) *ok = false;
    if (!p) return 0;
    while (*p == ' ') p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (*p < '0' || *p > '9') return 0;

    int whole = 0;
    while (*p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        p++;
    }
    int frac = 0;
    if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
        frac = p[1] - '0';
        if (p[2] >= '5' && p[2] <= '9') {          /* rounded to the tenth */
            frac++;
            if (frac == 10) { frac = 0; whole++; }
        }
    }
    if (ok) *ok = true;
    return sign * (whole * 10 + frac);
}

/* Number with four decimals: "-34.62143" -> -346214. It is how the coordinates
 * are stored: 1/10000 of a degree is about 11 metres, plenty. */
static int32_t j_dec10k(const char *p)
{
    if (!p) return 0;
    while (*p == ' ') p++;
    int32_t sign = 1;
    if (*p == '-') { sign = -1; p++; }
    int32_t whole = 0;
    while (*p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        p++;
    }
    int32_t frac = 0, mul = 1000;
    if (*p == '.') {
        p++;
        while (mul > 0 && *p >= '0' && *p <= '9') {
            frac += (*p - '0') * mul;
            mul /= 10;
            p++;
        }
    }
    return sign * (whole * 10000 + frac);
}

int32_t wx_deg_parse(const char *text)
{
    return j_dec10k(text);
}

/* Array of numbers into tenths. Returns how many it read. */
static int j_arr10(const char *p, int *out, int max)
{
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        bool ok = false;
        int v = j_dec10(p, &ok);
        out[n++] = ok ? v : 0;
        while (*p && *p != ',' && *p != ']') p++;
    }
    return n;
}

/* Array of integers (unixtime). */
static int j_arrint(const char *p, int32_t *out, int max)
{
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        bool ok = false;
        int32_t v = j_int(p, &ok);
        out[n++] = ok ? v : 0;
        while (*p && *p != ',' && *p != ']') p++;
    }
    return n;
}

static int j_str(const char *p, char *out, int max)
{
    if (!p || *p != '"') { if (max > 0) out[0] = 0; return 0; }
    p++;
    int n = 0;
    while (*p && *p != '"' && n < max - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return n;
}

/* -------------------------------------------------------------------------- */
/* Requests                                                                    */
/* -------------------------------------------------------------------------- */

/* Prints degrees x10000 as "-34.6214". */
static void deg_str(int32_t v, char *out, int max)
{
    int32_t whole = v / 10000;
    int32_t frac  = v % 10000;
    if (frac < 0) frac = -frac;
    /* the sign is lost if the integer part is zero */
    const char *sign = (v < 0 && whole == 0) ? "-" : "";
    snprintf(out, max, "%s%ld.%04ld", sign, (long)whole, (long)frac);
}

int wx_fetch_start(int32_t lat10k, int32_t lon10k)
{
    char lat[16], lon[16], url[512];
    deg_str(lat10k, lat, sizeof(lat));
    deg_str(lon10k, lon, sizeof(lon));

    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "is_day,weather_code,wind_speed_10m,wind_gusts_10m"
             "&hourly=temperature_2m,weather_code,precipitation_probability"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max,sunrise,sunset"
             "&forecast_hours=%d&forecast_days=%d"
             "&timezone=auto&timeformat=unixtime",
             lat, lon, WX_HOURS, WX_DAYS);

    return aos_hal_http_get(url, WX_BUF_BYTES);
}

int wx_search_start(const char *query)
{
    /* URL escaping. Only the safe characters are let through; the rest go
     * percent-encoded, byte by byte, which is the right thing for the UTF-8
     * the user may type. */
    char esc[128];
    int e = 0;
    for (const unsigned char *s = (const unsigned char *)query;
         *s && e < (int)sizeof(esc) - 4; s++) {
        if (*s == ' ') {
            esc[e++] = '+';
        } else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
                   (*s >= '0' && *s <= '9') || *s == '-' || *s == '_') {
            esc[e++] = (char)*s;
        } else {
            e += snprintf(esc + e, sizeof(esc) - e, "%%%02X", *s);
        }
    }
    esc[e] = 0;
    if (e == 0) {
        return -1;
    }

    /* The search's language follows the system's: open-meteo returns city
     * names in that language, so with the UI in English searching for
     * "Londres" has to find "London". It is not a translation of a string of
     * ours but of the data we ask for, and that is why it comes from
     * aos_i18n_current() and not from a catalogue. */
    char url[256];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search"
             "?name=%s&count=%d&language=%s&format=json",
             esc, WX_PLACES, aos_i18n_current());
    return aos_hal_http_get(url, WX_BUF_BYTES);
}

/* -------------------------------------------------------------------------- */
/* Reading the responses                                                       */
/* -------------------------------------------------------------------------- */

bool wx_parse(const char *json, int len, wx_data_t *out)
{
    if (!json || len < 32 || !out) {
        return false;
    }
    const char *end = json + len;
    memset(out, 0, sizeof(*out));

    out->utc_offset = j_int(j_find(json, end, "utc_offset_seconds"), NULL);

    /* --- now --- */
    const char *cur = j_find(json, end, "current");
    if (!cur || *cur != '{') {
        return false;
    }
    const char *cur_end = j_block_end(cur);

    out->now_t   = j_int  (j_find(cur, cur_end, "time"), NULL);
    out->temp10  = j_dec10(j_find(cur, cur_end, "temperature_2m"), NULL);
    out->feels10 = j_dec10(j_find(cur, cur_end, "apparent_temperature"), NULL);
    out->hum     = j_dec10(j_find(cur, cur_end, "relative_humidity_2m"), NULL) / 10;
    out->wind10  = j_dec10(j_find(cur, cur_end, "wind_speed_10m"), NULL);
    out->gust10  = j_dec10(j_find(cur, cur_end, "wind_gusts_10m"), NULL);
    out->code    = j_dec10(j_find(cur, cur_end, "weather_code"), NULL) / 10;
    out->is_day  = j_dec10(j_find(cur, cur_end, "is_day"), NULL) / 10;

    /* --- per day --- */
    const char *day = j_find(json, end, "daily");
    if (day && *day == '{') {
        const char *day_end = j_block_end(day);
        out->days = j_arrint(j_find(day, day_end, "time"), out->d_t, WX_DAYS);

        int tmp[WX_DAYS];
        int n = j_arr10(j_find(day, day_end, "weather_code"), tmp, WX_DAYS);
        for (int i = 0; i < n; i++) out->d_code[i] = tmp[i] / 10;

        j_arr10(j_find(day, day_end, "temperature_2m_max"), out->d_max10, WX_DAYS);
        j_arr10(j_find(day, day_end, "temperature_2m_min"), out->d_min10, WX_DAYS);

        n = j_arr10(j_find(day, day_end, "precipitation_probability_max"), tmp, WX_DAYS);
        for (int i = 0; i < n; i++) out->d_pop[i] = tmp[i] / 10;

        j_arrint(j_find(day, day_end, "sunrise"), out->d_sunrise, WX_DAYS);
        j_arrint(j_find(day, day_end, "sunset"),  out->d_sunset,  WX_DAYS);
    }

    /* --- per hour --- */
    const char *hr = j_find(json, end, "hourly");
    if (hr && *hr == '{') {
        const char *hr_end = j_block_end(hr);
        out->hours = j_arrint(j_find(hr, hr_end, "time"), out->h_t, WX_HOURS);
        j_arr10(j_find(hr, hr_end, "temperature_2m"), out->h_temp10, WX_HOURS);

        int tmp[WX_HOURS];
        int n = j_arr10(j_find(hr, hr_end, "weather_code"), tmp, WX_HOURS);
        for (int i = 0; i < n; i++) out->h_code[i] = tmp[i] / 10;

        n = j_arr10(j_find(hr, hr_end, "precipitation_probability"), tmp, WX_HOURS);
        for (int i = 0; i < n; i++) out->h_pop[i] = tmp[i] / 10;
    }

    out->ok = (out->days > 0);
    return out->ok;
}

int wx_places_parse(const char *json, int len, wx_place_t *out, int max)
{
    if (!json || len < 16 || !out) {
        return 0;
    }
    const char *results = strstr(json, "\"results\":");
    if (!results) {
        return 0;                       /* the API omits the key if there is nothing */
    }

    const char *p = strchr(results, '[');
    if (!p) {
        return 0;
    }
    p++;

    int n = 0;
    while (n < max) {
        while (*p == ' ' || *p == ',' || *p == '\n') p++;
        if (*p != '{') break;

        const char *e = j_block_end(p);
        wx_place_t *pl = &out[n];
        memset(pl, 0, sizeof(*pl));

        char raw[64];
        j_str(j_find(p, e, "name"), raw, sizeof(raw));
        wx_ascii(raw, pl->name, sizeof(pl->name));
        j_str(j_find(p, e, "admin1"), raw, sizeof(raw));
        wx_ascii(raw, pl->region, sizeof(pl->region));
        j_str(j_find(p, e, "country"), raw, sizeof(raw));
        wx_ascii(raw, pl->country, sizeof(pl->country));

        pl->lat10k = j_dec10k(j_find(p, e, "latitude"));
        pl->lon10k = j_dec10k(j_find(p, e, "longitude"));

        if (pl->name[0]) {
            n++;
        }
        p = e;
        if (*p != ',') break;
    }
    return n;
}

/* -------------------------------------------------------------------------- */
/* WMO codes                                                                   */
/* -------------------------------------------------------------------------- */

const char *wx_text(int code)
{
    switch (code) {
    case 0:               return _("Despejado");
    case 1:               return _("Mayormente despejado");
    case 2:               return _("Parcialmente nublado");
    case 3:               return _("Nublado");
    case 45: case 48:     return _("Niebla");
    case 51: case 53:
    case 55:              return _("Llovizna");
    case 56: case 57:     return _("Llovizna helada");
    case 61:              return _("Lluvia debil");
    case 63:              return _("Lluvia");
    case 65:              return _("Lluvia fuerte");
    case 66: case 67:     return _("Lluvia helada");
    case 71:              return _("Nieve debil");
    case 73:              return _("Nieve");
    case 75:              return _("Nieve fuerte");
    case 77:              return _("Granizo fino");
    case 80: case 81:     return _("Chaparrones");
    case 82:              return _("Chaparrones fuertes");
    case 85: case 86:     return _("Chaparrones de nieve");
    case 95:              return _("Tormenta");
    case 96: case 99:     return _("Tormenta con granizo");
    default:              return _("Sin datos");
    }
}

wx_icon_t wx_icon_of(int code)
{
    if (code == 0)                   return WX_ICON_CLEAR;
    if (code == 1 || code == 2)      return WX_ICON_FEWCLOUDS;
    if (code == 3)                   return WX_ICON_CLOUDY;
    if (code == 45 || code == 48)    return WX_ICON_FOG;
    if (code >= 51 && code <= 57)    return WX_ICON_DRIZZLE;
    if (code >= 61 && code <= 67)    return WX_ICON_RAIN;
    if (code >= 71 && code <= 77)    return WX_ICON_SNOW;
    if (code >= 80 && code <= 82)    return WX_ICON_RAIN;
    if (code == 85 || code == 86)    return WX_ICON_SNOW;
    if (code >= 95)                  return WX_ICON_STORM;
    return WX_ICON_CLOUDY;
}

/* -------------------------------------------------------------------------- */
/* Utilities                                                                   */
/* -------------------------------------------------------------------------- */

void wx_ascii(const char *utf8, char *out, int max)
{
    /* Only latin-1 and a little latin extended are of interest: they are the
     * accents of the city names the API returns in Spanish. Anything not
     * recognised is replaced with a space, which is better than a little
     * box. */
    static const char *map_c3 =
        /* 0xC3 0x80..0xBF: AAAAAA_CEEEEIIIIDNOOOOO_OUUUUY__aaaaaa_ceeeeiiii */
        "AAAAAA CEEEEIIIIDNOOOOO OUUUUY  aaaaaa ceeeeiiiidnooooo ouuuuy y";

    int o = 0;
    for (const unsigned char *s = (const unsigned char *)utf8; *s && o < max - 1; s++) {
        if (*s < 0x80) {
            out[o++] = (char)*s;
        } else if (*s == 0xC3 && s[1]) {
            s++;
            int idx = *s - 0x80;
            char c = (idx >= 0 && idx < 64) ? map_c3[idx] : ' ';
            out[o++] = c;
        } else if (*s == 0xC2 && s[1]) {
            s++;                        /* symbols: discarded */
        } else if ((*s & 0xE0) == 0xC0) {
            s++;                        /* another two-byte one */
            out[o++] = ' ';
        } else if ((*s & 0xF0) == 0xE0) {
            s += 2;
            out[o++] = ' ';
        } else if ((*s & 0xF8) == 0xF0) {
            s += 3;
            out[o++] = ' ';
        }
    }
    /* no trailing spaces */
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = 0;
}

/* 1 January 1970 was a Thursday, and Thursday is day 4 counting Sunday as 0.
 * With the local time already added, integer division is enough. */
int wx_wday(int32_t utc, int32_t offset)
{
    int32_t days = (utc + offset) / 86400;
    return (int)((days + 4) % 7);
}

int wx_hour(int32_t utc, int32_t offset)
{
    int32_t s = (utc + offset) % 86400;
    if (s < 0) s += 86400;
    return (int)(s / 3600);
}

int wx_minute(int32_t utc, int32_t offset)
{
    int32_t s = (utc + offset) % 86400;
    if (s < 0) s += 86400;
    return (int)((s % 3600) / 60);
}

/* Civil from days, the integer version of Howard Hinnant's algorithm. */
static void civil(int32_t utc, int32_t offset, int *y, int *m, int *d)
{
    int32_t z = (utc + offset) / 86400 + 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    int32_t doe = z - era * 146097;
    int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy  = yoe + era * 400;
    int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int32_t mp  = (5 * doy + 2) / 153;
    int32_t dd  = doy - (153 * mp + 2) / 5 + 1;
    int32_t mm  = mp + (mp < 10 ? 3 : -9);
    if (y) *y = (int)(yy + (mm <= 2));
    if (m) *m = (int)mm;
    if (d) *d = (int)dd;
}

int wx_mday(int32_t utc, int32_t offset)  { int d; civil(utc, offset, NULL, NULL, &d); return d; }
int wx_month(int32_t utc, int32_t offset) { int m; civil(utc, offset, NULL, &m, NULL); return m; }

void wx_temp_str(int t10, char *out, int max)
{
    /* Rounded to the degree: a decimal does not fit on the screen in 96 px of
     * height, and half a degree matters to nobody. */
    int t = (t10 >= 0) ? (t10 + 5) / 10 : -((-t10 + 5) / 10);
    snprintf(out, max, "%d", t);
}

/* -------------------------------------------------------------------------- */
/* Local copy                                                                  */
/* -------------------------------------------------------------------------- */

static void cache_path(char *out, int max)
{
    snprintf(out, max, "%s/clima.json", aos_hal_path_data());
}

/* The file starts with a line of its own, "lat lon", and carries on with the
 * JSON exactly as it arrived. That way the copy can be verified without
 * parsing anything. */
bool wx_cache_save(const char *json, int len, int32_t lat10k, int32_t lon10k)
{
    char path[128];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    char head[40];
    int hn = snprintf(head, sizeof(head), "%ld %ld\n", (long)lat10k, (long)lon10k);
    bool ok = (fwrite(head, 1, hn, f) == (size_t)hn) &&
              (fwrite(json, 1, len, f) == (size_t)len);
    fclose(f);
    return ok;
}

int wx_cache_load(char *out, int max, int32_t lat10k, int32_t lon10k)
{
    char path[128];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    int n = (int)fread(out, 1, max - 1, f);
    fclose(f);
    if (n <= 0) {
        return 0;
    }
    out[n] = 0;

    /* Header: coordinates and a line break. If they do not match, what is
     * stored belongs to a different place and is useless. */
    const char *nl = strchr(out, '\n');
    if (!nl) {
        return 0;
    }
    char *rest = NULL;
    long lat = strtol(out, &rest, 10);
    long lon = (rest) ? strtol(rest, NULL, 10) : 0;
    if (lat != (long)lat10k || lon != (long)lon10k) {
        return 0;
    }

    int body = (int)(nl + 1 - out);
    int blen = n - body;
    memmove(out, nl + 1, blen);
    out[blen] = 0;
    return blen;
}
