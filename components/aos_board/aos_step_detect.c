/* AmoledOS - Step detector. See aos_step_detect.h; tuned in tools/steps/. */
#include "aos_step_detect.h"

#include <string.h>

void aos_step_detect_init(aos_step_detect_t *d)
{
    memset(d, 0, sizeof *d);
    d->last_peak = (uint32_t)-1000000;   /* far in the past */
}

static float lerp_step(float dt_ms, float tau_ms)
{
    float k = dt_ms / tau_ms;
    return k > 1.0f ? 1.0f : k;
}

int aos_step_detect_feed(aos_step_detect_t *d, uint32_t t_ms, float mag_g)
{
    if (!d->started) {
        d->started = true;
        d->prev_t  = t_ms;
        d->g       = mag_g;
        return 0;
    }
    uint32_t dt = t_ms - d->prev_t;
    if (dt == 0) {
        dt = 1;
    }
    d->prev_t = t_ms;

    d->g  += (mag_g - d->g) * lerp_step((float)dt, AOS_STEP_TAU_G_MS);
    float dyn = mag_g - d->g;
    d->lp += (dyn - d->lp) * lerp_step((float)dt, AOS_STEP_TAU_LP_MS);

    int added = 0;
    if (d->lp > d->prev_lp) {
        d->rising = true;
    } else if (d->rising && d->lp < d->prev_lp) {
        d->rising = false;
        float h   = d->prev_lp;                    /* the hump's top */
        float thr = AOS_STEP_THR_FRAC * d->peak_avg;
        if (thr < AOS_STEP_THR_MIN) {
            thr = AOS_STEP_THR_MIN;
        }
        if (h > thr) {
            uint32_t gap = t_ms - d->last_peak;
            if (gap >= AOS_STEP_MIN_MS) {
                if (gap > AOS_STEP_MAX_MS) {
                    d->run = 0;
                }
                d->run++;
                d->last_peak = t_ms;
                d->peak_avg = d->peak_avg == 0.0f ? h : d->peak_avg + 0.2f * (h - d->peak_avg);
                if (d->run == AOS_STEP_NEED) {
                    added = AOS_STEP_NEED;
                } else if (d->run > AOS_STEP_NEED) {
                    added = 1;
                }
            }
        }
    }
    d->prev_lp = d->lp;
    return added;
}
