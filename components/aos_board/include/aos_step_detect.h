/*
 * AmoledOS - Step detector: pure C, no dependencies, so the same file runs
 * on the board (fed by the IMU poll) and on the desktop (tools/steps/bench.c,
 * fed by /api/imu dumps of counted walks). Tune it there, not by feel.
 *
 * Time-based, because the poll runs at 25 Hz with the screen on and at
 * 10 Hz in light sleep. Orientation-free, because the watch is worn on the
 * wrist or carried in a pocket: it works on the magnitude of the
 * acceleration. The shape (docs/STEPS.md has the numbers):
 *
 *   gravity  = slow low-pass of the magnitude (2 s)
 *   dynamic  = magnitude - gravity, smoothed (0.16 s) so one stride is one
 *              hump and the impact and the toe-off of a pocket signal do
 *              not count twice
 *   a step   = a hump above max(THR_MIN, THR_FRAC * recent hump height),
 *              at least MIN_MS after the previous one
 *   a run    = steps at most MAX_MS apart; it counts once it has NEED of
 *              them, all at once, so a jolt or a grab of the watch does not
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define AOS_STEP_TAU_G_MS    2000
#define AOS_STEP_TAU_LP_MS   160
#define AOS_STEP_THR_MIN     0.03f
#define AOS_STEP_THR_FRAC    0.30f
#define AOS_STEP_MIN_MS      330
#define AOS_STEP_MAX_MS      1500
#define AOS_STEP_NEED        4

typedef struct {
    bool     started;
    uint32_t prev_t;
    float    g;             /* gravity estimate, g */
    float    lp;            /* smoothed dynamic part */
    float    prev_lp;
    bool     rising;
    float    peak_avg;      /* recent hump height */
    uint32_t last_peak;     /* ms */
    int      run;           /* steps in the current rhythmic run */
} aos_step_detect_t;

void aos_step_detect_init(aos_step_detect_t *d);

/* One sample: the poll's time in ms and the acceleration magnitude in g.
 * Returns how many steps to add: 0, 1, or AOS_STEP_NEED when a run has
 * just proven itself. */
int aos_step_detect_feed(aos_step_detect_t *d, uint32_t t_ms, float mag_g);
