/*
 * AmoledOS - Landscape: a valley, a city or a beach under today's weather.
 *
 * The sky is the hour's: night, dawn, day and dusk from the real sunrise and
 * sunset of the place, with the sun or the moon on its arc. What falls from
 * it is the weather's: clouds by how overcast it is, moved by the wind, rain,
 * snow, fog, lightning. Cold whitens the hills. A tap shows tomorrow's for a
 * few seconds.
 *
 * Three places to look at it from (v0.17.0), chosen on its page of the portal
 * (/face_paisaje) or with a tap on the time, and kept in face_paisaje.cfg:
 * the valley; a city, whose windows and street lamps light as the day goes,
 * whose street shines when it rains and whose chimney smokes when it is
 * cold; and a beach, whose sea takes the sky's mood, with a swash that grows
 * with the wind, a parasol that is only open on a fine day and a sailing boat
 * that stays in port when it rains.
 *
 * The place is the Clima app's (clima_lat, clima_lon); the forecast is asked
 * of Open-Meteo, one small request every half hour while the screen is lit
 * (never dimmed), and kept in a file so the face comes up with the last one
 * known. Without a place it is a clear day by the watch's clock.
 *
 * The sky and what stands still are drawn once a minute into 'base'; a frame
 * adds what moves (clouds, the tree, the cars, the sea's edge, what falls)
 * and only the rows that changed go to the screen.
 */
#include "aos_watchface.h"
#include "aos_theme.h"
#include "aos_i18n.h"
#include "aos_hal.h"
#include "../../common/face_draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FETCH_EVERY_S   1800
#define RETRY_MS        90000
#define RESP_MAX        2048
#define PREVIEW_S       6.0f
#define CLOUDS          6
#define DROPS           70
#define FACE_ID         "face.paisaje"
#define CFG_FILE        "face_paisaje.cfg"
#define CITY_GY         188             /* where the city's street begins */
#define BEACH_HZ        132             /* the sea's horizon */

enum { S_VALLEY = 0, S_CITY, S_BEACH, SCENES };

enum { W_CLEAR = 0, W_FEW, W_CLOUDY, W_FOG, W_DRIZZLE, W_RAIN, W_SNOW, W_STORM };

typedef struct {
    bool    valid;
    int     code, temp10, wind;         /* now */
    int     d_code[2], d_max10[2], d_min10[2];
    int     sunrise, sunset;            /* minutes of the day, local */
    int32_t fetched;                    /* unix time */
} wx_t;

typedef struct { float x, y, s, v; } cloud_t;
typedef struct { float x, y, v; } drop_t;

typedef struct {
    fc_t       fc;
    lv_obj_t  *root;
    uint16_t  *base, *work;
    bool       aod;

    wx_t       wx;
    bool       have_place;
    int        demo_minute;             /* PAISAJE_DEMO: the hour to show, -1 the clock's */
    int32_t    lat, lon;
    int        req;
    uint32_t   next_try_ms;

    /* the scene being shown: now, or tomorrow while the preview lasts */
    int        kind, temp10, wind, minute;
    float      preview;
    float      t, flash, flash_in;
    float      since;                   /* seconds since the last frame drawn */
    cloud_t    cloud[CLOUDS];
    drop_t     drop[DROPS];
    uint32_t   seed;
    int        base_minute, base_kind, base_scene;
    bool       base_preview, base_aod;
    int        scene;                   /* S_VALLEY, S_CITY, S_BEACH */
    float      mast_x, mast_y, chim_x, chim_y;     /* the city's, found by its base */

    char       last[3][48];
} paisaje_t;

/* -------------------------------------------------------------------------- */
/* The forecast                                                               */
/* -------------------------------------------------------------------------- */

static int kind_of(int code)
{
    if (code == 0)                  return W_CLEAR;
    if (code <= 2)                  return W_FEW;
    if (code == 3)                  return W_CLOUDY;
    if (code == 45 || code == 48)   return W_FOG;
    if (code >= 51 && code <= 57)   return W_DRIZZLE;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return W_SNOW;
    if (code >= 95)                 return W_STORM;
    return W_RAIN;                  /* 61..67, 80..82 */
}

static const char *kind_text(int kind)
{
    static const char *const T[] = {
        N_("Despejado"), N_("Algo nublado"), N_("Nublado"), N_("Niebla"),
        N_("Llovizna"), N_("Lluvia"), N_("Nieve"), N_("Tormenta"),
    };
    return _(T[kind]);
}

/* The value after "key": inside the object that starts at 'obj'. */
static const char *after(const char *obj, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = obj ? strstr(obj, pat) : NULL;
    return p ? p + strlen(pat) : NULL;
}

static int num10(const char *p)
{
    return p ? (int)(strtof(p, NULL) * 10.0f + (*p == '-' ? -0.5f : 0.5f)) : 0;
}

/* The i-th element of the array at p ("[a,b]"), NULL if there is none. */
static const char *elem(const char *p, int i)
{
    if (!p || *p != '[') {
        return NULL;
    }
    p++;
    while (i-- > 0) {
        p = strchr(p, ',');
        if (!p) {
            return NULL;
        }
        p++;
    }
    return p;
}

/* "2026-11-10T05:43" -> 343 */
static int minute_of(const char *p, int fallback)
{
    const char *t = p ? strchr(p, 'T') : NULL;
    return (t && t - p < 14) ? atoi(t + 1) * 60 + atoi(t + 4) : fallback;
}

static bool wx_parse(const char *json, wx_t *w)
{
    const char *cur = strstr(json, "\"current\":{");
    const char *day = strstr(json, "\"daily\":{");
    if (!cur || !day || !after(cur, "weather_code")) {
        return false;
    }
    memset(w, 0, sizeof(*w));
    w->code = atoi(after(cur, "weather_code"));
    w->temp10 = num10(after(cur, "temperature_2m"));
    w->wind = num10(after(cur, "wind_speed_10m")) / 10;
    for (int i = 0; i < 2; i++) {
        const char *e;
        if ((e = elem(after(day, "weather_code"), i)) != NULL)       w->d_code[i] = atoi(e);
        if ((e = elem(after(day, "temperature_2m_max"), i)) != NULL) w->d_max10[i] = num10(e);
        if ((e = elem(after(day, "temperature_2m_min"), i)) != NULL) w->d_min10[i] = num10(e);
    }
    w->sunrise = minute_of(elem(after(day, "sunrise"), 0), 6 * 60 + 30);
    w->sunset = minute_of(elem(after(day, "sunset"), 0), 19 * 60 + 30);
    w->fetched = (int32_t)time(NULL);
    w->valid = true;
    return true;
}

static void cache_path(char *out, size_t n)
{
    snprintf(out, n, "%s/face_paisaje.txt", aos_hal_path_data());
}

static void cache_save(const paisaje_t *p)
{
    char path[96];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    const wx_t *w = &p->wx;
    fprintf(f, "%ld %ld %ld %d %d %d %d %d %d %d %d %d %d %d\n", (long)p->lat, (long)p->lon,
            (long)w->fetched, w->code, w->temp10, w->wind, w->d_code[0], w->d_max10[0],
            w->d_min10[0], w->d_code[1], w->d_max10[1], w->d_min10[1], w->sunrise, w->sunset);
    fclose(f);
}

static void cache_load(paisaje_t *p)
{
    char path[96];
    cache_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        return;
    }
    /* read whole and then sscanf: fscanf is not in the firmware's table */
    char txt[160];
    size_t got = fread(txt, 1, sizeof(txt) - 1, f);
    txt[got] = '\0';
    fclose(f);
    long lat = 0, lon = 0, at = 0;
    wx_t w = { 0 };
    int n = sscanf(txt, "%ld %ld %ld %d %d %d %d %d %d %d %d %d %d %d", &lat, &lon, &at, &w.code,
                   &w.temp10, &w.wind, &w.d_code[0], &w.d_max10[0], &w.d_min10[0], &w.d_code[1],
                   &w.d_max10[1], &w.d_min10[1], &w.sunrise, &w.sunset);
    /* what was kept for another place is no use here */
    if (n == 14 && lat == p->lat && lon == p->lon) {
        w.fetched = (int32_t)at;
        w.valid = true;
        p->wx = w;
    }
}

static void deg(char *out, size_t n, int32_t v)
{
    snprintf(out, n, "%s%ld.%04ld", (v < 0 && v > -10000) ? "-" : "", (long)(v / 10000),
             (long)labs(v % 10000));
}

static void fetch(paisaje_t *p)
{
    char lat[16], lon[16], url[320];
    deg(lat, sizeof(lat), p->lat);
    deg(lon, sizeof(lon), p->lon);
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,weather_code,wind_speed_10m"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset"
             "&forecast_days=2&timezone=auto", lat, lon);
    int id = aos_hal_http_get(url, RESP_MAX);
    if (id > 0) {
        p->req = id;
    }
    p->next_try_ms = (uint32_t)aos_hal_uptime_ms() + RETRY_MS;
}

/* -------------------------------------------------------------------------- */
/* Its file: the scene's number, written by its page and by the tap           */
/* -------------------------------------------------------------------------- */

static void cfg_path(char *out, size_t n)
{
    for (int i = 0; i < aos_watchface_count(); i++) {
        const aos_watchface_t *f = aos_watchface_at(i);
        const char *path = aos_watchface_path(i);
        if (f && path && strcmp(f->id, FACE_ID) == 0) {
            const char *slash = strrchr(path, '/');
            snprintf(out, n, "%.*s/" CFG_FILE, (int)(slash - path), path);
            return;
        }
    }
    snprintf(out, n, "%s/" CFG_FILE, aos_hal_path_data());
}

static int cfg_load(void)
{
    char path[96], txt[16] = "";
    cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        size_t got = fread(txt, 1, sizeof(txt) - 1, f);
        txt[got] = '\0';
        fclose(f);
    }
    int scene = atoi(txt);
    return (scene >= 0 && scene < SCENES) ? scene : S_VALLEY;
}

static void cfg_save(int scene)
{
    char path[96];
    cfg_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d\n", scene);
        fclose(f);
    }
}

/* -------------------------------------------------------------------------- */
/* The scene                                                                  */
/* -------------------------------------------------------------------------- */

static float smooth(float a, float b, float x)
{
    float t = LV_CLAMP(0.0f, (x - a) / (b - a), 1.0f);
    return t * t * (3 - 2 * t);
}

static float overcast(int kind)
{
    static const float O[] = { 0.0f, 0.35f, 0.85f, 0.7f, 0.8f, 0.95f, 0.9f, 1.0f };
    return O[kind];
}

/* What the hour and the weather make of the light, worked out once for the
 * base and once for each frame. */
typedef struct {
    int      sr, ss;                /* sunrise, sunset: minutes of the day */
    float    day, glow, oc, light, windy;
    bool     snowy, wet;
    uint16_t top, bot;              /* the sky, above and at the horizon */
    float    sx, sy;                /* the sun's centre, or the moon's */
    uint32_t disc;                  /* how much of it the clouds let through */
    bool     sun;
} env_t;

static void env_of(const paisaje_t *p, env_t *e)
{
    e->sr = p->wx.valid ? p->wx.sunrise : 6 * 60 + 30;
    e->ss = p->wx.valid ? p->wx.sunset : 19 * 60 + 30;
    float m = (float)p->minute;
    e->day = smooth(e->sr - 35, e->sr + 35, m) * (1.0f - smooth(e->ss - 35, e->ss + 35, m));
    e->glow = LV_MAX(1.0f - fabsf(m - e->sr) / 70.0f, 1.0f - fabsf(m - e->ss) / 70.0f);
    e->glow = LV_CLAMP(0.0f, e->glow, 1.0f);
    e->oc = overcast(p->kind);
    e->light = (0.20f + 0.80f * e->day) * (1.0f - e->oc * 0.30f);
    e->windy = LV_MIN(1.0f, p->wind / 35.0f);
    e->snowy = p->kind == W_SNOW || p->temp10 < 10;
    e->wet = p->kind == W_DRIZZLE || p->kind == W_RAIN || p->kind == W_STORM;

    e->top = fd_lerp(fc_hex(0x04071A), fc_hex(0x2476D8), e->day);
    e->bot = fd_lerp(fc_hex(0x101A3C), fc_hex(0x9CD2F8), e->day);
    e->top = fd_lerp(e->top, fc_hex(0x4A2E78), e->glow * 0.45f);
    e->bot = fd_lerp(e->bot, fc_hex(0xFF8A4A), e->glow * 0.85f);
    uint16_t grey = fd_lerp(fc_hex(0x14181F), fc_hex(0x8A949E), e->day);
    e->top = fd_lerp(e->top, grey, e->oc * 0.8f);
    e->bot = fd_lerp(e->bot, fd_light(grey, 0.2f), e->oc * 0.7f);

    /* the sun by day, the moon by night, each on its arc */
    e->disc = (uint32_t)(255 * (1.0f - e->oc * 0.85f));
    e->sun = m > e->sr - 20 && m < e->ss + 20;
    if (e->sun) {
        float f = (m - e->sr) / (float)(e->ss - e->sr);
        /* high enough at noon to clear the time's digits */
        e->sx = 18 + f * (FC_SW - 36);
        e->sy = 128 - sinf(LV_CLAMP(0.0f, f, 1.0f) * FC_PI) * 114;
    } else {
        float span = 1440 - (e->ss - e->sr);
        float f = (m >= e->ss ? m - e->ss : m + 1440 - e->ss) / span;
        e->sx = 18 + f * (FC_SW - 36);
        e->sy = 124 - sinf(f * FC_PI) * 108;
    }
}

/* A number in 0..1 that is always the same for the same pair: which window
 * is lit, how tall a building is. */
static float h01(int a, int b)
{
    uint32_t s = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u + 0x9E3779B9u;
    s = (s ^ (s >> 13)) * 1274126177u;
    s ^= s >> 16;
    return (float)(s & 0xFFFF) / 65536.0f;
}

static void sky(paisaje_t *p, const env_t *e)
{
    uint16_t *b = p->base;
    for (int y = 0; y < FC_SH; y++) {
        uint16_t c = fd_lerp(e->top, e->bot, (float)y / 150.0f);
        for (int x = 0; x < FC_SW; x++) {
            b[y * FC_SW + x] = c;
        }
    }
    /* stars, where the night is clear */
    if (e->day < 0.3f && e->oc < 0.6f) {
        uint32_t s = 0xC0FFEE11u;
        for (int i = 0; i < 46; i++) {
            int x = (int)(fc_randf(&s) * FC_SW), y = (int)(fc_randf(&s) * 110);
            fd_px(b, x, y, 0xFFFF,
                  (uint32_t)((1 - e->day / 0.3f) * (1 - e->oc) * (120 + fc_randf(&s) * 135)));
        }
    }
    if (e->sun) {
        fd_circle(b, e->sx, e->sy, 22, fc_hex(0xFFE9A0), e->disc / 5);
        fd_circle(b, e->sx, e->sy, 15, fc_hex(0xFFE9A0), e->disc / 3);
        fd_circle(b, e->sx, e->sy, 10, fc_hex(0xFFF4C8), e->disc);
    } else {
        fd_circle(b, e->sx, e->sy, 9, fc_hex(0xE8ECF4), e->disc);
        fd_circle(b, e->sx + 4, e->sy - 2, 8, e->top, e->disc);       /* a crescent */
    }
}

/* --- the valley: mountains, hills, a house and a tree ---------------------- */

static float hill_y(float x)
{
    return 166 - (sinf(x * 0.035f + 0.5f) * 12 + sinf(x * 0.09f + 3.0f) * 5);
}

static void valley_base(paisaje_t *p, const env_t *e)
{
    uint16_t *b = p->base;
    float light = e->light;
    bool snowy = e->snowy;
    /* the far range, then the hills, lit by the hour */
    uint16_t far = fd_lerp(e->bot, fc_scale(fc_hex(0x4A5A80), (uint32_t)(light * 255)), 0.75f);
    /* snow lights itself: it is the brightest thing under a grey sky */
    uint16_t hill = fc_scale(fc_hex(snowy ? 0xE6EEF4 : 0x4E9A44),
                             (uint32_t)(LV_MIN(1.0f, snowy ? light * 1.35f : light) * 255));
    uint16_t cap = fc_scale(fc_hex(0xF4F8FC), (uint32_t)(light * 255));
    for (int x = 0; x < FC_SW; x++) {
        float ridge = 122 - (sinf(x * 0.045f + 1.0f) * 16 + sinf(x * 0.11f) * 8 + sinf(x * 0.23f + 2.0f) * 3);
        float hy = hill_y(x);
        for (int y = (int)ridge; y < FC_SH; y++) {
            uint16_t c;
            if (y < hy) {
                /* snow on the peaks when it is cold: more of it the higher
                 * the ridge stands */
                c = (snowy && ridge < 114 && y < ridge + (114 - ridge) * 0.55f) ? cap : far;
            } else {
                c = fd_dark(hill, (y - hy) / (snowy ? 300.0f : 110.0f));
            }
            b[y * FC_SW + x] = c;
        }
    }
    /* the house, its window lit when the light goes */
    float hx = 126, hy = hill_y(hx) + 1;
    uint16_t wall = fc_scale(fc_hex(0xD8644A), (uint32_t)(light * 255));
    uint16_t roof = fc_scale(fc_hex(snowy ? 0xEEF2F6 : 0x5A3428), (uint32_t)(light * 255));
    fd_rect(b, (int)hx - 9, (int)hy - 12, (int)hx + 9, (int)hy, wall, 255);
    fd_tri(b, hx - 12, hy - 12, hx + 12, hy - 12, hx, hy - 23, roof, 255);
    if (e->day < 0.6f) {
        fd_circle(b, hx - 3, hy - 6, 6, fc_hex(0xFFD060), (uint32_t)((1 - e->day) * 70));
        fd_rect(b, (int)hx - 5, (int)hy - 9, (int)hx - 1, (int)hy - 4, fc_hex(0xFFD868), 255);
    } else {
        fd_rect(b, (int)hx - 5, (int)hy - 9, (int)hx - 1, (int)hy - 4, fd_dark(wall, 0.5f), 255);
    }
}

static void valley_frame(paisaje_t *p, uint16_t *b, const env_t *e)
{
    /* the tree, bending with the wind */
    float light = 0.25f + 0.75f * e->day, windy = e->windy;
    float tx = 44, ty = hill_y(tx) + 2;
    float bend = sinf(p->t * (1.2f + windy * 3.0f)) * (0.6f + windy * 4.0f) + windy * 3.0f;
    uint16_t trunk = fc_scale(fc_hex(0x6A4428), (uint32_t)(light * 255));
    uint16_t leaf = fc_scale(fc_hex(e->snowy ? 0xDCE8EE : 0x2F7A36), (uint32_t)(light * 255));
    fd_line(b, tx, ty, tx + bend * 0.4f, ty - 16, 3, trunk, 255);
    fd_circle(b, tx + bend * 0.8f, ty - 22, 10, leaf, 255);
    fd_circle(b, tx - 7 + bend, ty - 17, 7, leaf, 255);
    fd_circle(b, tx + 7 + bend, ty - 17, 7, fd_light(leaf, 0.12f), 255);
}

/* --- the city: two rows of buildings over a street ------------------------- */

static void city_base(paisaje_t *p, const env_t *e)
{
    uint16_t *b = p->base;
    float night = 1.0f - e->day;
    uint32_t lv = (uint32_t)(e->light * 255);

    /* the far skyline: a silhouette the colour of the haze, a window or two */
    uint16_t far = fd_lerp(e->bot, fc_scale(fc_hex(0x3A4664), lv), 0.7f);
    for (int i = 0, x = -3; x < FC_SW; i++) {
        int w = 8 + (int)(h01(i, 1) * 9), top = 108 + (int)(h01(i, 2) * 40);
        fd_rect(b, x, top, x + w, CITY_GY, far, 255);
        for (int k = 0; k < 4 && night > 0.4f; k++) {
            if (h01(i, 10 + k) < night * 0.6f) {
                fd_px(b, x + 1 + (int)(h01(i, 20 + k) * (w - 2)),
                      top + 3 + (int)(h01(i, 30 + k) * (CITY_GY - top - 8)), fc_hex(0xFFD868), 170);
            }
        }
        x += w + (int)(h01(i, 3) * 3);
    }

    /* the near ones: walls of five colours, windows that light as the day
     * goes (and a few under a heavy sky), snow on the roofs when it is cold */
    static const uint32_t WALL[] = { 0x566078, 0x74605A, 0x485470, 0x646C78, 0x5A5068 };
    uint16_t white = fc_scale(fc_hex(0xF2F6FA), (uint32_t)(LV_MIN(1.0f, e->light * 1.35f) * 255));
    for (int i = 0, x = -6; x < FC_SW; i++) {
        int w = 17 + (int)(h01(i, 4) * 12), top = 134 + (int)(h01(i, 5) * 34);
        if (i == 1) {                           /* the tower, with its mast */
            w = 21;
            top = 102;
        }
        uint16_t wall = fc_scale(fc_hex(WALL[i % 5]), lv);
        uint16_t off = fd_lerp(fd_dark(wall, 0.45f), fd_lerp(wall, e->bot, 0.35f), e->day);
        if (i == 1) {
            p->mast_x = x + w / 2 + 0.5f;
            p->mast_y = top - 13;
            fd_line(b, p->mast_x, top, p->mast_x, p->mast_y + 1, 1.2f, wall, 255);
        } else if (i == 4) {                    /* a chimney on the fifth */
            p->chim_x = x + 6;
            p->chim_y = top - 7;
            fd_rect(b, x + 4, top - 7, x + 9, top, fd_dark(wall, 0.25f), 255);
        }
        fd_rect(b, x, top, x + w, CITY_GY, wall, 255);
        fd_rect(b, x + w - 3, top, x + w, CITY_GY, 0x0000, 55);     /* its side, in shade */
        if (e->snowy) {
            fd_rect(b, x - 1, top - 2, x + w + 1, top + 1, white, 255);
        }
        for (int wy = top + 5; wy + 3 < CITY_GY - 3; wy += 6) {
            for (int wx = x + 3, c = 0; wx + 2 <= x + w - 4; wx += 5, c++) {
                float r = h01(i * 31 + c, wy);
                bool lit = r < night * 0.62f + e->oc * e->day * 0.12f;
                fd_rect(b, wx, wy, wx + 2, wy + 3,
                        lit ? fc_hex(r < 0.07f ? 0xCFE6FF : 0xFFD868) : off, 255);
            }
        }
        x += w + 1 + (int)(h01(i, 6) * 3);
    }

    /* the street: dark, white under snow, and a mirror when it rains */
    float lit = 0.40f + 0.60f * e->light;
    uint16_t road = fc_scale(fc_hex(e->snowy ? 0xD4DCE4 : 0x2B2D35), (uint32_t)(lit * 255));
    for (int y = CITY_GY; y < FC_SH; y++) {
        uint16_t c = y < CITY_GY + 3 ? fd_light(road, e->snowy ? 0.3f : 0.22f)
                                     : fd_dark(road, (y - CITY_GY) / 160.0f);
        for (int x = 0; x < FC_SW; x++) {
            b[y * FC_SW + x] = c;
        }
    }
    if (e->wet) {
        fd_rect(b, 0, CITY_GY + 3, FC_SW, FC_SH, e->bot, 46);
    }
    if (!e->snowy) {
        for (int x = 4; x < FC_SW; x += 18) {
            fd_rect(b, x, CITY_GY + 19, x + 9, CITY_GY + 20, fd_light(road, 0.5f), 255);
        }
    }
    /* the lamps, lit when the light goes or the sky closes */
    float lamp = LV_CLAMP(0.0f, (night - 0.35f) * 3.0f + (e->oc > 0.8f ? 0.35f : 0.0f), 1.0f);
    uint16_t pole = fc_scale(fc_hex(0x9098A4), (uint32_t)(lit * 255));
    for (int k = 0; k < 3; k++) {
        float lx = 24 + k * 66;
        fd_line(b, lx, CITY_GY + 2, lx, CITY_GY - 19, 1.3f, pole, 255);
        fd_line(b, lx, CITY_GY - 19, lx + 5, CITY_GY - 20, 1.3f, pole, 255);
        if (lamp > 0.02f) {
            fd_circle(b, lx + 5, CITY_GY - 18, 10, fc_hex(0xFFD890), (uint32_t)(lamp * 46));
            fd_circle(b, lx + 5, CITY_GY - 18, 2.2f, fc_hex(0xFFF0C0), (uint32_t)(lamp * 255));
            fd_ellipse(b, lx + 5, CITY_GY + 9, 18, 5, 0, fc_hex(0xFFD890), (uint32_t)(lamp * 50));
            if (e->wet) {                       /* its light, down the wet street */
                fd_rect(b, (int)lx + 4, CITY_GY + 4, (int)lx + 7, CITY_GY + 30, fc_hex(0xFFE0A0),
                        (uint32_t)(lamp * 80));
            }
        }
    }
}

static void city_frame(paisaje_t *p, uint16_t *b, const env_t *e)
{
    float night = 1.0f - e->day;
    /* the chimney smokes when it is cold, and the wind takes the smoke */
    if (p->temp10 < 120) {
        uint16_t smoke = fc_scale(fc_hex(0xC8CED6), (uint32_t)((0.35f + 0.65f * e->day) * 255));
        for (int k = 0; k < 4; k++) {
            float a = fmodf(p->t * 0.30f + k * 0.25f, 1.0f);
            fd_circle(b, p->chim_x + a * (3 + e->windy * 26), p->chim_y - 2 - a * 20, 2 + a * 4, smoke,
                      (uint32_t)((1 - a) * 150));
        }
    }
    /* the mast's light, for the planes */
    if (night > 0.3f && fmodf(p->t, 2.0f) < 0.7f) {
        fd_circle(b, p->mast_x, p->mast_y, 4, fc_hex(0xFF4030), 70);
        fd_circle(b, p->mast_x, p->mast_y, 1.6f, fc_hex(0xFF5040), 255);
    }
    /* three cars, two one way and one the other; headlights when they are
     * needed, and slower on snow */
    static const uint32_t BODY[] = { 0xD04A3A, 0x3A78C8, 0xE8C040 };
    float lit = 0.40f + 0.60f * e->light;
    bool lights = night > 0.4f || e->wet || p->kind == W_FOG;
    for (int k = 0; k < 3; k++) {
        float dir = (k & 1) ? -1.0f : 1.0f, speed = (24 + k * 9) * (e->snowy ? 0.55f : 1.0f);
        float pos = fmodf(p->t * speed + k * 83, FC_SW + 60) - 30;
        float x = dir > 0 ? pos : FC_SW - pos, y = CITY_GY + (dir > 0 ? 14 : 29);
        uint16_t body = fc_scale(fc_hex(BODY[k]), (uint32_t)(lit * 255));
        fd_rect(b, (int)x - 7, (int)y - 5, (int)x + 7, (int)y, body, 255);
        fd_rect(b, (int)x - 4, (int)y - 8, (int)x + 4, (int)y - 5, fd_dark(body, 0.35f), 255);
        fd_circle(b, x - 4, y, 1.6f, fc_hex(0x101014), 255);
        fd_circle(b, x + 4, y, 1.6f, fc_hex(0x101014), 255);
        if (lights) {
            fd_ellipse(b, x + dir * 16, y - 1, 9, 3, 0, fc_hex(0xFFF0B0), 60);
            fd_circle(b, x + dir * 7, y - 3, 1.5f, fc_hex(0xFFF6D0), 255);
            fd_circle(b, x - dir * 7, y - 3, 1.2f, fc_hex(0xFF3020), 255);
        }
    }
}

/* --- the beach: a sea to the horizon, sand, a palm and a parasol ------------ */

static float shore_y(float x)
{
    return 180 + sinf(x * 0.028f + 0.8f) * 5 + x * 0.035f;
}

/* The sea's colour: the day's blue, the sky's grey when it closes; darker
 * out at the horizon (depth 0), green by the sand (depth 1). */
static uint16_t sea_colour(const env_t *e, float depth)
{
    depth = LV_CLAMP(0.0f, depth, 1.0f);
    uint16_t deep = fd_lerp(fc_hex(0x14609E), fc_hex(0x46535E), e->oc * 0.85f);
    uint16_t near = fd_lerp(fc_hex(0x35B8B4), fc_hex(0x66757C), e->oc * 0.85f);
    uint16_t c = fc_scale(fd_lerp(deep, near, depth * depth), (uint32_t)((0.12f + 0.88f * e->light) * 255));
    return fd_lerp(c, e->bot, (1.0f - depth) * 0.28f);
}

static void beach_base(paisaje_t *p, const env_t *e)
{
    uint16_t *b = p->base;
    uint32_t lv = (uint32_t)(e->light * 255);
    /* a headland across the bay */
    uint16_t far = fd_lerp(e->bot, fc_scale(fc_hex(0x3E5A50), lv), 0.7f);
    fd_ellipse(b, 150, BEACH_HZ + 3, 34, 9, 0, far, 255);
    fd_ellipse(b, 174, BEACH_HZ + 3, 24, 16, 0, far, 255);
    for (int y = BEACH_HZ; y < FC_SH; y++) {
        uint16_t c = sea_colour(e, (y - BEACH_HZ) / 50.0f);
        for (int x = 0; x < FC_SW; x++) {
            b[y * FC_SW + x] = c;
        }
    }
    /* the sun's road on the water, or the moon's */
    if (e->disc > 60 && e->sy < BEACH_HZ - 4) {
        for (int y = BEACH_HZ + 1; y < 180; y += 2) {
            float d = (y - BEACH_HZ) / 48.0f, half = (2 + (y - BEACH_HZ) * 0.26f) * (0.45f + h01(y, 8) * 0.75f);
            int cx = (int)(e->sx + h01(y, 7) * 10 - 5);
            fd_rect(b, cx - (int)half, y, cx + (int)half, y + 1, fc_hex(e->sun ? 0xFFE9A0 : 0xE8ECF4),
                    (uint32_t)(e->disc * (e->sun ? 0.50f : 0.34f) * (1.0f - d * 0.6f)));
        }
    }
    /* the sand, wet and dark where the sea reaches */
    uint16_t sand = fc_scale(fc_hex(e->snowy ? 0xE6EEF4 : 0xD8BC80),
                             (uint32_t)(LV_MIN(1.0f, e->snowy ? e->light * 1.35f : e->light) * 255));
    for (int x = 0; x < FC_SW; x++) {
        int sy = (int)shore_y(x) - 9;
        for (int y = sy; y < FC_SH; y++) {
            b[y * FC_SW + x] = y < sy + 11 ? fd_dark(sand, 0.30f) : fd_dark(sand, (y - sy) / 170.0f);
        }
    }
}

static void beach_frame(paisaje_t *p, uint16_t *b, const env_t *e)
{
    float light = 0.25f + 0.75f * e->day, windy = e->windy;
    bool storm = p->kind == W_STORM;
    uint16_t foam = fc_scale(0xFFFF, (uint32_t)((0.40f + 0.60f * e->light) * 255));

    /* white horses out at sea: a couple on a calm day, many in a wind */
    int caps = 2 + (int)(windy * 9) + (storm ? 4 : 0);
    for (int k = 0; k < caps; k++) {
        int y = BEACH_HZ + 6 + (int)(h01(k, 3) * 36);
        float x = FC_SW + 10 - fmodf(p->t * (7 + h01(k, 4) * 12 + windy * 16) + h01(k, 5) * 220, FC_SW + 24);
        fd_rect(b, (int)x, y, (int)x + 4 + (int)(h01(k, 6) * 6), y + 1, foam, 150);
    }
    /* the boat, out when it is not raining; it leans with the wind */
    if (!e->wet) {
        float bx = 60, by = BEACH_HZ + 7 + sinf(p->t * 1.3f) * (0.8f + windy * 1.5f), lean = windy * 4;
        uint16_t sail = fc_scale(fc_hex(0xF6F2EA), (uint32_t)(light * 255));
        fd_tri(b, bx + 0.5f, by - 1, bx + 0.5f, by - 16, bx + 9 + lean, by - 1, sail, 255);
        fd_tri(b, bx - 1, by - 1, bx - 1, by - 12, bx - 7 + lean, by - 1, fd_dark(sail, 0.15f), 255);
        fd_rect(b, (int)bx - 7, (int)by, (int)bx + 8, (int)by + 2, fc_scale(fc_hex(0x7A3A2A), (uint32_t)(light * 255)), 255);
        if (e->day < 0.4f) {
            fd_circle(b, bx, by - 17, 1.3f, fc_hex(0xFFE9A0), 255);
        }
    }
    /* the sea's edge, up the sand and back; further up in a wind */
    float amp = 2.2f + windy * 3.0f + (storm ? 2.0f : 0.0f);
    uint16_t water = sea_colour(e, 1.0f);
    for (int x = 0; x < FC_SW; x++) {
        float s = shore_y(x);
        float w = s - 3 + sinf(p->t * 0.8f + x * 0.045f) * amp + sinf(p->t * 1.7f + x * 0.11f) * amp * 0.3f;
        int y1 = LV_MIN(FC_SH, (int)w);
        for (int y = (int)s - 9; y < y1; y++) {
            b[y * FC_SW + x] = water;
        }
        fd_px(b, x, y1 - 1, foam, 150);
        fd_px(b, x, y1, foam, 255);
    }
    /* the parasol: open on a fine, calm day; furled the rest of the time.
     * It stands still, but in front of the sea's edge, so it is drawn here. */
    uint32_t lv = (uint32_t)(e->light * 255);
    float ux = 134, uy = shore_y(ux) + 20;
    bool open = e->day > 0.5f && p->kind <= W_FEW && p->wind < 30;
    uint16_t red = fc_scale(fc_hex(0xE04838), lv), cloth = fc_scale(fc_hex(0xF4F0E8), lv);
    if (open) {
        fd_ellipse(b, ux + 8, uy + 1, 15, 3, 0, 0x0000, 60);        /* its shade */
        fd_rect(b, (int)ux + 5, (int)uy - 1, (int)ux + 21, (int)uy + 3, fc_scale(fc_hex(0x3A8AD0), lv), 255);
        fd_rect(b, (int)ux + 10, (int)uy - 1, (int)ux + 13, (int)uy + 3, cloth, 255);
    }
    fd_line(b, ux, uy, ux, uy - 28, 1.4f, fc_scale(fc_hex(0xC8C8C8), lv), 255);
    if (open) {
        fd_tri(b, ux - 17, uy - 19, ux + 17, uy - 19, ux, uy - 31, red, 255);
        fd_tri(b, ux - 6, uy - 19, ux + 6, uy - 19, ux, uy - 31, cloth, 255);
    } else {
        fd_tri(b, ux - 2.5f, uy - 8, ux + 2.5f, uy - 8, ux, uy - 30, red, 255);
    }
    /* the palm, its trunk and fronds given to the wind */
    float px = 30, py = shore_y(px) + 13;
    float bend = sinf(p->t * (1.2f + windy * 3.0f)) * (0.6f + windy * 4.0f) + windy * 3.0f;
    uint16_t trunk = fc_scale(fc_hex(0x8A6A40), (uint32_t)(light * 255));
    uint16_t leaf = fc_scale(fc_hex(e->snowy ? 0xCFE0D8 : 0x2F8A3C), (uint32_t)(light * 255));
    float mx = px + 4 + bend * 0.3f, my = py - 22, cx = px + 8 + bend, cy = py - 44;
    fd_line(b, px, py, mx, my, 4, trunk, 255);
    fd_line(b, mx, my, cx, cy, 3, trunk, 255);
    for (int k = 0; k < 6; k++) {
        float a = FC_PI * (0.06f + k * 0.176f);             /* from the right round to the left */
        float ex = cx + cosf(a) * 13 + bend * 0.5f, ey = cy - sinf(a) * 9;
        fd_line(b, cx, cy, ex, ey, 2.6f, k & 1 ? leaf : fd_light(leaf, 0.10f), 255);
        fd_line(b, ex, ey, ex + cosf(a) * 8 + bend * 0.5f, ey + 8, 2.0f, leaf, 255);
    }
    fd_circle(b, cx - 2, cy + 3, 2.0f, fd_dark(trunk, 0.3f), 255);
    fd_circle(b, cx + 3, cy + 3, 2.0f, fd_dark(trunk, 0.3f), 255);
}

/* The sky and everything that stands still, for this minute. */
static void build_base(paisaje_t *p)
{
    env_t e;
    env_of(p, &e);
    sky(p, &e);
    if (p->scene == S_CITY) {
        city_base(p, &e);
    } else if (p->scene == S_BEACH) {
        beach_base(p, &e);
    } else {
        valley_base(p, &e);
    }
    p->base_minute = p->minute;
    p->base_kind = p->kind;
    p->base_scene = p->scene;
    p->base_preview = p->preview > 0;
    p->base_aod = p->aod;
}

static void render(paisaje_t *p)
{
    uint16_t *b = p->work;
    memcpy(b, p->base, FC_SW * FC_SH * sizeof(uint16_t));
    env_t e;
    env_of(p, &e);
    float day = e.day, windy = e.windy;

    /* clouds: as many as it is overcast, white, grey or dark */
    int n = (int)(overcast(p->kind) * CLOUDS + 0.5f);
    uint32_t tint = p->kind >= W_RAIN ? 0x6A7280 : (p->kind >= W_CLOUDY ? 0xC0C8D0 : 0xFFFFFF);
    uint16_t cc = fc_scale(fc_hex(tint), (uint32_t)((0.35f + 0.65f * day) * 255));
    for (int i = 0; i < n; i++) {
        const cloud_t *c = &p->cloud[i];
        float s = c->s;
        fd_ellipse(b, c->x, c->y, 17 * s, 7 * s, 0, cc, 235);
        fd_circle(b, c->x - 7 * s, c->y - 4 * s, 7 * s, cc, 235);
        fd_circle(b, c->x + 4 * s, c->y - 6 * s, 9 * s, cc, 235);
    }
    if (p->kind == W_FOG) {
        for (int i = 0; i < 4; i++) {
            float x = fmodf(p->t * (3 + i) + i * 61, FC_SW + 120) - 60;
            fd_ellipse(b, x, 112 + i * 22, 70, 9, 0, fc_hex(0xD8DEE4), 70);
        }
    }

    if (p->scene == S_CITY) {
        city_frame(p, b, &e);
    } else if (p->scene == S_BEACH) {
        beach_frame(p, b, &e);
    } else {
        valley_frame(p, b, &e);
    }

    /* what falls */
    if (e.wet) {
        int drops = p->kind == W_DRIZZLE ? DROPS / 3 : DROPS;
        float slant = 1.5f + windy * 5.0f;
        for (int i = 0; i < drops; i++) {
            const drop_t *d = &p->drop[i];
            fd_line(b, d->x, d->y, d->x - slant, d->y + 6, 1, fc_hex(0xBFD8F0), 150);
        }
    } else if (p->kind == W_SNOW) {
        for (int i = 0; i < DROPS; i++) {
            const drop_t *d = &p->drop[i];
            fd_circle(b, d->x + sinf(p->t * 2 + i) * 2, d->y, 1.2f, 0xFFFF, 230);
        }
    }
    if (p->flash > 0) {
        fd_rect(b, 0, 0, FC_SW, FC_SH, 0xFFFF, (uint32_t)(LV_MIN(1.0f, p->flash * 6) * 90));
        float x = 60 + fmodf(p->flash_in * 37.0f, 70.0f), y = 40;
        for (int k = 0; k < 5; k++) {
            float nx = x + ((k & 1) ? 9 : -7), ny = y + 16;
            fd_line(b, x, y, nx, ny, 1.6f, 0xFFFF, 255);
            x = nx;
            y = ny;
        }
    }
    if (p->aod) {
        for (int i = 0; i < FC_SW * FC_SH; i++) {
            b[i] = fc_scale(b[i], 64);
        }
    }
    fd_show(&p->fc, b);
}

/* -------------------------------------------------------------------------- */

/* What the scene shows: now, or tomorrow at one o'clock while previewing. */
static void scene_set(paisaje_t *p, const struct tm *now)
{
    bool tomorrow = p->preview > 0 && p->wx.valid;
    p->minute = tomorrow ? 13 * 60
              : p->demo_minute >= 0 ? p->demo_minute : now->tm_hour * 60 + now->tm_min;
    p->kind = !p->wx.valid ? W_CLEAR : kind_of(tomorrow ? p->wx.d_code[1] : p->wx.code);
    p->temp10 = !p->wx.valid ? 200 : (tomorrow ? p->wx.d_max10[1] : p->wx.temp10);
    p->wind = p->wx.valid ? p->wx.wind : 6;
    if (p->minute != p->base_minute || p->kind != p->base_kind || p->scene != p->base_scene ||
        (p->preview > 0) != p->base_preview || p->aod != p->base_aod) {
        build_base(p);
    }
}

static void text_update(paisaje_t *p, const struct tm *now, bool force)
{
    char line[3][48];
    snprintf(line[0], sizeof(line[0]), "%02d:%02d", now->tm_hour, now->tm_min);
    const wx_t *w = &p->wx;
    if (!p->have_place) {
        snprintf(line[1], sizeof(line[1]), "%s", _("Elegí tu ciudad en Clima"));
        line[2][0] = '\0';
    } else if (!w->valid) {
        snprintf(line[1], sizeof(line[1]), "%s", "...");
        line[2][0] = '\0';
    } else if (p->preview > 0) {
        snprintf(line[1], sizeof(line[1]), "%s", _("Mañana"));
        snprintf(line[2], sizeof(line[2]), "%d\xC2\xB0 / %d\xC2\xB0  %s", (w->d_min10[1] + 5) / 10,
                 (w->d_max10[1] + 5) / 10, kind_text(kind_of(w->d_code[1])));
    } else {
        snprintf(line[1], sizeof(line[1]), "%d\xC2\xB0  %s", (w->temp10 + (w->temp10 < 0 ? -5 : 5)) / 10,
                 kind_text(kind_of(w->code)));
        snprintf(line[2], sizeof(line[2]), "%d\xC2\xB0 / %d\xC2\xB0", (w->d_min10[0] + 5) / 10,
                 (w->d_max10[0] + 5) / 10);
    }
    static const struct { float scale; int y; } AT[3] = { { 1.9f, 92 }, { 1.0f, 160 }, { 1.0f, 418 } };
    for (int i = 0; i < 3; i++) {
        if (!force && strcmp(line[i], p->last[i]) == 0) {
            continue;
        }
        fc_mask_t old = p->fc.mask[i];
        p->fc.mask[i].a = NULL;
        snprintf(p->last[i], sizeof(p->last[i]), "%s", line[i]);
        fc_mask_text(&p->fc.mask[i], p->root, line[i], i == 0 ? aos_font_huge : aos_font_body,
                     AT[i].scale, FC_W / 2, AT[i].y, fc_hex(p->aod ? 0x8A8A8A : 0xFFFFFF), true);
        fc_present_mask(&p->fc, &old);
        free(old.a);
        fc_present_mask(&p->fc, &p->fc.mask[i]);
    }
}

static void update_all(paisaje_t *p, bool force_text)
{
    struct tm now;
    aos_hal_time_now(&now);
    scene_set(p, &now);
    text_update(p, &now, force_text);
    render(p);
}

static void frame(void *ctx, uint32_t dt_ms)
{
    paisaje_t *p = ctx;
    if (!p || p->aod) {
        return;
    }
    float dt = dt_ms / 1000.0f;
    p->t += dt;
    if (p->preview > 0 && (p->preview -= dt) <= 0) {
        p->preview = 0;
        update_all(p, false);               /* back to today */
        return;
    }
    float windy = LV_MIN(1.0f, p->wind / 35.0f);
    for (int i = 0; i < CLOUDS; i++) {
        cloud_t *c = &p->cloud[i];
        c->x += (1.5f + windy * 16.0f) * c->v * dt;
        if (c->x > FC_SW + 30) {
            c->x = -30;
        }
    }
    bool rain = p->kind == W_DRIZZLE || p->kind == W_RAIN || p->kind == W_STORM;
    if (rain || p->kind == W_SNOW) {
        for (int i = 0; i < DROPS; i++) {
            drop_t *d = &p->drop[i];
            d->y += d->v * (rain ? 1.0f : 0.16f) * dt;
            d->x -= (rain ? 20.0f : 6.0f) * windy * dt;
            if (d->y > FC_SH || d->x < -4) {
                d->y = -6;
                d->x = fc_randf(&p->seed) * (FC_SW + 30);
            }
        }
    }
    if (p->kind == W_STORM) {
        if (p->flash > 0) {
            p->flash -= dt;
        } else if ((p->flash_in -= dt) <= 0) {
            p->flash = 0.22f;
            p->flash_in = 3.0f + fc_randf(&p->seed) * 6.0f;
        }
    } else {
        p->flash = 0;
    }
    /* Clouds cross the sky in a minute and the tree sways once a second:
     * eight frames a second show that as well as twenty do. What falls, and
     * lightning, get every frame. (On the board, a cloudy night at twenty
     * frames a second kept the face drawing 94 % of the time.) */
    p->since += dt;
    if (rain || p->kind == W_SNOW || p->flash > 0 || p->since >= 0.125f) {
        p->since = 0;
        render(p);
    }
}

static void *create(lv_obj_t *root)
{
    paisaje_t *p = lv_malloc_zeroed(sizeof(paisaje_t));
    if (!p) {
        return NULL;
    }
    p->base = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    p->work = malloc(FC_SW * FC_SH * sizeof(uint16_t));
    if (!p->base || !p->work || !fc_init(&p->fc, root)) {
        free(p->base);
        free(p->work);
        lv_free(p);
        return NULL;
    }
    p->root = root;
    p->seed = (uint32_t)aos_hal_uptime_ms() * 2654435761u | 1u;
    p->base_minute = -1;
    p->flash_in = 2.0f;
    for (int i = 0; i < CLOUDS; i++) {
        p->cloud[i] = (cloud_t){ fc_randf(&p->seed) * FC_SW, 14 + i * 13 + fc_randf(&p->seed) * 6,
                                 0.8f + fc_randf(&p->seed) * 0.7f, 0.6f + fc_randf(&p->seed) * 0.8f };
    }
    for (int i = 0; i < DROPS; i++) {
        p->drop[i] = (drop_t){ fc_randf(&p->seed) * (FC_SW + 30), fc_randf(&p->seed) * FC_SH,
                               150 + fc_randf(&p->seed) * 90 };
    }
    int32_t lat = 0, lon = 0;
    p->have_place = aos_hal_pref_get_i32("clima_lat", &lat) && aos_hal_pref_get_i32("clima_lon", &lon);
    p->lat = lat;
    p->lon = lon;
    if (p->have_place) {
        cache_load(p);
    }
    /* A development switch, for looking at a weather without waiting for it:
     * PAISAJE_DEMO="code,tenths of a degree,wind km/h,minute of the day",
     * with tomorrow's made the opposite. On the board getenv() is always
     * NULL (there is no environment), so this costs nothing there. */
    p->scene = cfg_load();
    const char *esc = getenv("PAISAJE_ESCENA");     /* 0 the valley, 1 the city, 2 the beach */
    if (esc && atoi(esc) >= 0 && atoi(esc) < SCENES) {
        p->scene = atoi(esc);
    }
    p->demo_minute = -1;
    const char *demo = getenv("PAISAJE_DEMO");
    if (demo) {
        int code = 0, t10 = 180, wind = 8, minute = 780;
        sscanf(demo, "%d,%d,%d,%d", &code, &t10, &wind, &minute);
        p->wx = (wx_t){ .valid = true, .code = code, .temp10 = t10, .wind = wind,
                        .d_code = { code, code >= 51 ? 1 : 63 }, .d_max10 = { t10 + 30, t10 + 10 },
                        .d_min10 = { t10 - 60, t10 - 50 }, .sunrise = 6 * 60, .sunset = 19 * 60 + 40,
                        .fetched = (int32_t)time(NULL) };
        p->have_place = true;
        p->demo_minute = minute;
    }
    update_all(p, true);
    return p;
}

static void refresh(void *ctx, const struct tm *now)
{
    paisaje_t *p = ctx;
    if (!p) {
        return;
    }
    if (p->req && aos_hal_http_state(p->req) != AOS_HTTP_BUSY) {
        const char *body = aos_hal_http_body(p->req);
        wx_t fresh;
        if (aos_hal_http_state(p->req) == AOS_HTTP_DONE && body && wx_parse(body, &fresh)) {
            p->wx = fresh;
            cache_save(p);
        }
        aos_hal_http_release(p->req);
        p->req = 0;
        update_all(p, false);
        return;
    }
    if (p->aod) {
        if (now->tm_hour * 60 + now->tm_min != p->base_minute) {
            update_all(p, false);           /* once a minute: the sun moves on */
        }
        return;
    }
    scene_set(p, now);
    text_update(p, now, false);

    /* a forecast older than half an hour is asked again, with the screen lit */
    uint32_t up = (uint32_t)aos_hal_uptime_ms();
    bool old = !p->wx.valid || (int32_t)time(NULL) - p->wx.fetched > FETCH_EVERY_S ||
               (int32_t)time(NULL) < p->wx.fetched;
    if (p->have_place && old && !p->req && aos_watchface_live() &&
        aos_hal_net_state() == AOS_NET_CONNECTED && (int32_t)(up - p->next_try_ms) >= 0) {
        fetch(p);
    }
}

static void set_aod(void *ctx, bool aod)
{
    paisaje_t *p = ctx;
    if (!p || p->aod == aod) {
        return;
    }
    p->aod = aod;
    p->preview = 0;
    p->flash = 0;
    if (!aod && !getenv("PAISAJE_ESCENA")) {
        p->scene = cfg_load();          /* its page may have chosen another meanwhile */
    }
    update_all(p, true);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    (void)x;
    paisaje_t *p = ctx;
    if (!p || what != AOS_FACE_TOUCH_TAP || p->aod) {
        return false;
    }
    if (y < 150) {
        /* on the time: the next place, kept for the next time */
        p->scene = (p->scene + 1) % SCENES;
        cfg_save(p->scene);
        p->preview = 0;
        update_all(p, false);
        return true;
    }
    if (!p->wx.valid) {
        return false;
    }
    p->preview = p->preview > 0 ? 0 : PREVIEW_S;    /* tomorrow, or back */
    update_all(p, false);
    return true;
}

static void destroy(void *ctx)
{
    paisaje_t *p = ctx;
    if (!p) {
        return;
    }
    if (p->req) {
        aos_hal_http_release(p->req);       /* the slots are the whole system's */
    }
    fc_deinit(&p->fc);
    free(p->base);
    free(p->work);
    lv_free(p);
}

static bool paisaje_get(aos_watchface_t *face)
{
    face->id       = "face.paisaje";
    face->name     = N_("Paisaje");
    face->create   = create;
    face->refresh  = refresh;
    face->set_aod  = set_aod;
    face->destroy  = destroy;
    face->flags    = AOS_FACE_FLAG_TOUCH;
    face->touch    = touch;
    face->frame    = frame;
    face->frame_ms = 50;
    return true;
}

AOS_FACE_ENTRY(paisaje_get);
