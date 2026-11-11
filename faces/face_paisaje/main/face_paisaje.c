/*
 * AmoledOS - Landscape: a valley under today's weather.
 *
 * The sky is the hour's: night, dawn, day and dusk from the real sunrise and
 * sunset of the place, with the sun or the moon on its arc. What falls from
 * it is the weather's: clouds by how overcast it is, moved by the wind, rain,
 * snow, fog, lightning. Cold whitens the hills. A tap shows tomorrow's for a
 * few seconds.
 *
 * The place is the Clima app's (clima_lat, clima_lon); the forecast is asked
 * of Open-Meteo, one small request every half hour while the screen is lit
 * (never dimmed), and kept in a file so the face comes up with the last one
 * known. Without a place it is a clear day by the watch's clock.
 *
 * The sky, the mountains and the house are drawn once a minute into 'base';
 * a frame adds what moves (clouds, the tree, what falls) and only the rows
 * that changed go to the screen. A clear, calm day draws nothing at all.
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
    int        base_minute, base_kind;
    bool       base_preview, base_aod;

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

/* The sky, the sun or the moon, the mountains, the hills and the house. */
static void build_base(paisaje_t *p)
{
    uint16_t *b = p->base;
    int sr = p->wx.valid ? p->wx.sunrise : 6 * 60 + 30, ss = p->wx.valid ? p->wx.sunset : 19 * 60 + 30;
    float m = (float)p->minute;
    float day = smooth(sr - 35, sr + 35, m) * (1.0f - smooth(ss - 35, ss + 35, m));
    float glow = LV_MAX(1.0f - fabsf(m - sr) / 70.0f, 1.0f - fabsf(m - ss) / 70.0f);
    glow = LV_CLAMP(0.0f, glow, 1.0f);
    float oc = overcast(p->kind);
    float light = (0.20f + 0.80f * day) * (1.0f - oc * 0.30f);
    bool snowy = p->kind == W_SNOW || p->temp10 < 10;

    uint16_t top = fd_lerp(fc_hex(0x04071A), fc_hex(0x2476D8), day);
    uint16_t bot = fd_lerp(fc_hex(0x101A3C), fc_hex(0x9CD2F8), day);
    top = fd_lerp(top, fc_hex(0x4A2E78), glow * 0.45f);
    bot = fd_lerp(bot, fc_hex(0xFF8A4A), glow * 0.85f);
    uint16_t grey = fd_lerp(fc_hex(0x14181F), fc_hex(0x8A949E), day);
    top = fd_lerp(top, grey, oc * 0.8f);
    bot = fd_lerp(bot, fd_light(grey, 0.2f), oc * 0.7f);
    for (int y = 0; y < FC_SH; y++) {
        uint16_t c = fd_lerp(top, bot, (float)y / 150.0f);
        for (int x = 0; x < FC_SW; x++) {
            b[y * FC_SW + x] = c;
        }
    }

    /* stars, where the night is clear */
    if (day < 0.3f && oc < 0.6f) {
        uint32_t s = 0xC0FFEE11u;
        for (int i = 0; i < 46; i++) {
            int x = (int)(fc_randf(&s) * FC_SW), y = (int)(fc_randf(&s) * 110);
            fd_px(b, x, y, 0xFFFF, (uint32_t)((1 - day / 0.3f) * (1 - oc) * (120 + fc_randf(&s) * 135)));
        }
    }
    /* the sun by day, the moon by night, each on its arc */
    uint32_t disc = (uint32_t)(255 * (1.0f - oc * 0.85f));
    if (m > sr - 20 && m < ss + 20) {
        float f = (m - sr) / (float)(ss - sr);
        /* high enough at noon to clear the time's digits */
        float x = 18 + f * (FC_SW - 36), y = 128 - sinf(LV_CLAMP(0.0f, f, 1.0f) * FC_PI) * 114;
        fd_circle(b, x, y, 22, fc_hex(0xFFE9A0), disc / 5);
        fd_circle(b, x, y, 15, fc_hex(0xFFE9A0), disc / 3);
        fd_circle(b, x, y, 10, fc_hex(0xFFF4C8), disc);
    } else {
        float span = 1440 - (ss - sr);
        float f = (m >= ss ? m - ss : m + 1440 - ss) / span;
        float x = 18 + f * (FC_SW - 36), y = 124 - sinf(f * FC_PI) * 108;
        fd_circle(b, x, y, 9, fc_hex(0xE8ECF4), disc);
        fd_circle(b, x + 4, y - 2, 8, top, disc);       /* a crescent */
    }

    /* the far range, then the hills, lit by the hour */
    uint16_t far = fd_lerp(bot, fc_scale(fc_hex(0x4A5A80), (uint32_t)(light * 255)), 0.75f);
    /* snow lights itself: it is the brightest thing under a grey sky */
    uint16_t hill = fc_scale(fc_hex(snowy ? 0xE6EEF4 : 0x4E9A44),
                             (uint32_t)(LV_MIN(1.0f, snowy ? light * 1.35f : light) * 255));
    uint16_t cap = fc_scale(fc_hex(0xF4F8FC), (uint32_t)(light * 255));
    for (int x = 0; x < FC_SW; x++) {
        float ridge = 122 - (sinf(x * 0.045f + 1.0f) * 16 + sinf(x * 0.11f) * 8 + sinf(x * 0.23f + 2.0f) * 3);
        float hy = 166 - (sinf(x * 0.035f + 0.5f) * 12 + sinf(x * 0.09f + 3.0f) * 5);
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
    float hx = 126, hy = 166 - (sinf(hx * 0.035f + 0.5f) * 12 + sinf(hx * 0.09f + 3.0f) * 5) + 1;
    uint16_t wall = fc_scale(fc_hex(0xD8644A), (uint32_t)(light * 255));
    uint16_t roof = fc_scale(fc_hex(snowy ? 0xEEF2F6 : 0x5A3428), (uint32_t)(light * 255));
    fd_rect(b, (int)hx - 9, (int)hy - 12, (int)hx + 9, (int)hy, wall, 255);
    fd_tri(b, hx - 12, hy - 12, hx + 12, hy - 12, hx, hy - 23, roof, 255);
    if (day < 0.6f) {
        fd_circle(b, hx - 3, hy - 6, 6, fc_hex(0xFFD060), (uint32_t)((1 - day) * 70));
        fd_rect(b, (int)hx - 5, (int)hy - 9, (int)hx - 1, (int)hy - 4, fc_hex(0xFFD868), 255);
    } else {
        fd_rect(b, (int)hx - 5, (int)hy - 9, (int)hx - 1, (int)hy - 4, fd_dark(wall, 0.5f), 255);
    }
    p->base_minute = p->minute;
    p->base_kind = p->kind;
    p->base_preview = p->preview > 0;
    p->base_aod = p->aod;
}

static void render(paisaje_t *p)
{
    uint16_t *b = p->work;
    memcpy(b, p->base, FC_SW * FC_SH * sizeof(uint16_t));
    int sr = p->wx.valid ? p->wx.sunrise : 390, ss = p->wx.valid ? p->wx.sunset : 1170;
    float day = smooth(sr - 35, sr + 35, p->minute) * (1.0f - smooth(ss - 35, ss + 35, p->minute));
    float light = 0.25f + 0.75f * day;
    bool snowy = p->kind == W_SNOW || p->temp10 < 10;
    float windy = LV_MIN(1.0f, p->wind / 35.0f);

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

    /* the tree, bending with the wind */
    float tx = 44, ty = 166 - (sinf(tx * 0.035f + 0.5f) * 12 + sinf(tx * 0.09f + 3.0f) * 5) + 2;
    float bend = sinf(p->t * (1.2f + windy * 3.0f)) * (0.6f + windy * 4.0f) + windy * 3.0f;
    uint16_t trunk = fc_scale(fc_hex(0x6A4428), (uint32_t)(light * 255));
    uint16_t leaf = fc_scale(fc_hex(snowy ? 0xDCE8EE : 0x2F7A36), (uint32_t)(light * 255));
    fd_line(b, tx, ty, tx + bend * 0.4f, ty - 16, 3, trunk, 255);
    fd_circle(b, tx + bend * 0.8f, ty - 22, 10, leaf, 255);
    fd_circle(b, tx - 7 + bend, ty - 17, 7, leaf, 255);
    fd_circle(b, tx + 7 + bend, ty - 17, 7, fd_light(leaf, 0.12f), 255);

    /* what falls */
    if (p->kind == W_DRIZZLE || p->kind == W_RAIN || p->kind == W_STORM) {
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
    if (p->minute != p->base_minute || p->kind != p->base_kind ||
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
    update_all(p, true);
}

static bool touch(void *ctx, aos_face_touch_t what, int x, int y)
{
    (void)x;
    (void)y;
    paisaje_t *p = ctx;
    if (!p || what != AOS_FACE_TOUCH_TAP || p->aod || !p->wx.valid) {
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
