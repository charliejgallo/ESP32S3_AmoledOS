/*
 * HA SENSORS (#46) - the part that does not draw: configuration, template,
 * reading values and the sample ring. Without LVGL, so it can be tested on the
 * Mac with tools/sn_harness.c.
 *
 * WHY THE BOARD SAMPLES INSTEAD OF ASKING HOME ASSISTANT FOR THE HISTORY. It
 * was measured against a real house on 2026-09-03, and the figure settles the
 * argument: the history from /api/history/period, ALREADY with
 * minimal_response and no_attributes, costs **67 bytes per point**, and one
 * power sensor returned **901 points in ONE hour** = 60 KB. To draw sixty
 * points on a 368 px screen sixty thousand bytes would have to be downloaded,
 * and for 24 h of that sensor, a megabyte and a half. A slow sensor (the
 * pressure) gives 97 points in 24 h and works perfectly, but a panel cannot
 * behave differently depending on which sensor it gets.
 *
 * So the chart is built from what the app measures itself: one small query
 * every SN_PERIODO_MS with ALL the sensors inside it (Remoto's lesson: one
 * request for every state, not one per entity) and a ring per sensor. It costs
 * ~200 bytes per round instead of 60 KB, it works the same with a sensor that
 * changes every 4 seconds and with one that changes once a day, and the only
 * thing lost is whatever happened before the app was first opened.
 *
 * And that is why the app runs in the BACKGROUND. Testing it in the simulator
 * made it obvious that without that it is useless: the screen dims and goes
 * back to the clock after thirty seconds, the app is destroyed, the ring is
 * lost and the chart NEVER fills up. With AOS_APP_FLAG_BACKGROUND —the same
 * mechanism as the timer and the pomodoro— it goes on measuring with the
 * screen off and on reopening it the chart is there.
 *
 * From that come the sixty seconds: a minute between samples is one query a
 * minute to a server on your own LAN, which is nothing, and 180 samples give
 * three hours of chart. With the first version's twenty seconds the window was
 * one hour and the traffic three times as much, for a panel looked at now and
 * then.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SN_MAX          4       /* sensors at once: four cards in 448 px */
#define SN_POINTS       180     /* 180 x 60 s = three hours of chart */
#define SN_PERIODO_MS   60000

/* Ceiling of the values response. It is four numbers separated by bars: 512 is
 * plenty, but the template may return 'unavailable' for all of them. */
#define SN_BUF_BYTES    512

typedef struct {
    char entity[64];
    char nombre[26];        /* ASCII; the portal transliterates it on saving */
    char unidad[10];

    /* Ring of samples, in HUNDREDTHS of the unit. Integers, as in cotiz and in
     * clima: the S3 has a single-precision FPU but a double means calls into
     * the software emulation, and strtod is not in the symbol table. */
    int32_t v[SN_POINTS];
    int     n;              /* how many valid samples there are (capped at SN_POINTS) */
    int     head;           /* next position to write */

    bool    ok;             /* the last reading was good */
    int32_t ultimo;
} sn_sensor_t;

/* Reads the "entity|name|unit;entity|name|unit;..." preference and fills the
 * array. Returns how many made it in (capped at SN_MAX). */
int sn_parse_config(const char *txt, sn_sensor_t *out, int max);

/* Builds the JSON body for /api/template with every sensor. It is called ONCE
 * when the configuration is loaded, not on every refresh. */
bool sn_build_template(const sn_sensor_t *s, int n, char *out, int max);

/* Distributes the response ("21.8|83.8|unavailable|50") into the rings.
 * Returns how many sensors brought back a number. */
int sn_push_values(const char *resp, int len, sn_sensor_t *s, int n);

/* Minimum and maximum of the ring. false if there is no sample. */
bool sn_range(const sn_sensor_t *s, int32_t *min, int32_t *max);

/* The i-th sample in chronological order (0 = the oldest of those left). */
int32_t sn_at(const sn_sensor_t *s, int i);

/* "21,8" / "1010" / "-575,1". Without a decimal when it is not needed. */
char *sn_valor(int32_t centi, char *out, int max);
