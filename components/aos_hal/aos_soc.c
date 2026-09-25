/*
 * AmoledOS - State of charge (see aos_soc.h).
 */
#include "aos_soc.h"

#include <math.h>
#include <stddef.h>

/* A generic LiPo open-circuit curve at rest, charge in percent of a cell
 * charged to 4.20 V. Standard figures for small pouch cells; to be replaced
 * by this cell's own once a discharge has been recorded with the voltage. */
static const struct { int mv; float pct; } s_ocv[] = {
    { 3270,   0 }, { 3610,   5 }, { 3690,  10 }, { 3710,  15 }, { 3730,  20 },
    { 3750,  25 }, { 3770,  30 }, { 3790,  35 }, { 3800,  40 }, { 3820,  45 },
    { 3840,  50 }, { 3850,  55 }, { 3870,  60 }, { 3910,  65 }, { 3950,  70 },
    { 3980,  75 }, { 4020,  80 }, { 4080,  85 }, { 4110,  90 }, { 4150,  95 },
    { 4200, 100 },
};
#define OCV_N   (sizeof(s_ocv) / sizeof(s_ocv[0]))

/* A cell charged to the target and left to rest settles this far below it. */
#define REST_DROP_MV        20
/* At rest the watch still draws a few mA: a few mV of sag. */
#define QUIET_SAG_MV        5
#define QUIET_AFTER_US      (30LL * 1000000)
#define SAG_SAMPLE_AFTER_US (20LL * 1000000)
#define SAG_QUIET_MAX_AGE   (60LL * 1000000)
/* How far each reading moves the estimate: damps the ADC's noise and the
 * load's ups and downs. */
#define FOLLOW              0.25f
/* The constant-voltage tail is approached with this time constant. */
#define CV_TAU_S            600.0f

static float raw_pct(int mv)
{
    if (mv <= s_ocv[0].mv) {
        return 0.0f;
    }
    if (mv >= s_ocv[OCV_N - 1].mv) {
        return 100.0f;
    }
    for (size_t i = 1; i < OCV_N; i++) {
        if (mv <= s_ocv[i].mv) {
            float t = (float)(mv - s_ocv[i - 1].mv) / (float)(s_ocv[i].mv - s_ocv[i - 1].mv);
            return s_ocv[i - 1].pct + t * (s_ocv[i].pct - s_ocv[i - 1].pct);
        }
    }
    return 100.0f;
}

float aos_soc_from_ocv(int ocv_mv, int target_mv)
{
    if (target_mv < 3900 || target_mv > 4400) {
        target_mv = 4200;
    }
    float full = raw_pct(target_mv - REST_DROP_MV);
    if (full < 10.0f) {
        full = 100.0f;
    }
    float pct = raw_pct(ocv_mv) * 100.0f / full;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

void aos_soc_init(aos_soc_t *st, float sag_mv, float cap_mah)
{
    *st = (aos_soc_t){0};
    st->sag_mv  = sag_mv  > 0 ? sag_mv  : AOS_SOC_DEFAULT_SAG_MV;
    st->cap_mah = cap_mah > 0 ? cap_mah : AOS_SOC_DEFAULT_CAP_MAH;
}

int aos_soc_percent(const aos_soc_t *st)
{
    if (!st->valid) {
        return -1;
    }
    int p = (int)lrintf(st->soc);
    return p < 0 ? 0 : p > 100 ? 100 : p;
}

static void clamp_soc(aos_soc_t *st)
{
    if (st->soc < 0.0f)   st->soc = 0.0f;
    if (st->soc > 100.0f) st->soc = 100.0f;
}

/* The average current of the constant-voltage tail, if it decays
 * exponentially from the CC limit to the termination current. */
static float cv_mean_ma(int icc, int iterm)
{
    if (icc <= 0) return 0.0f;
    if (iterm <= 0 || iterm >= icc) return (float)icc / 2.0f;
    return (float)(icc - iterm) / logf((float)icc / (float)iterm);
}

bool aos_soc_step(aos_soc_t *st, const aos_soc_input_t *in)
{
    bool learned = false;
    float dt_s = st->last_us ? (float)(in->now_us - st->last_us) / 1e6f : 0.0f;
    if (dt_s < 0.0f || dt_s > 3600.0f) {
        dt_s = 0.0f;        /* a clock jump: do not integrate across it */
    }
    st->last_us = in->now_us;

    /* --- rest and lit, tracked for the sag ------------------------------- */
    bool rest_now = !in->screen_lit && !in->audio && !in->busy;
    if (rest_now) {
        if (!st->quiet_since_us) st->quiet_since_us = in->now_us;
    } else {
        st->quiet_since_us = 0;
    }
    bool quiet = rest_now && in->now_us - st->quiet_since_us >= QUIET_AFTER_US;
    if (quiet && !in->usb) {
        st->quiet_mv = in->vbat_mv;
        st->quiet_mv_us = in->now_us;
    }

    if (in->screen_lit && !in->audio) {
        if (!st->lit_since_us) {
            st->lit_since_us = in->now_us;
            /* lit from rest, on battery: a sag sample can be taken */
            st->sag_armed = !in->usb && st->quiet_mv_us &&
                            in->now_us - st->quiet_mv_us <= SAG_QUIET_MAX_AGE;
            st->sag_from_mv = st->quiet_mv;
        } else if (st->sag_armed && !in->usb &&
                   in->now_us - st->lit_since_us >= SAG_SAMPLE_AFTER_US) {
            int sample = st->sag_from_mv - in->vbat_mv;
            st->sag_armed = false;
            if (sample > 0 && sample < 400) {
                st->sag_mv = st->sag_samples ? 0.7f * st->sag_mv + 0.3f * (float)sample
                                             : (float)sample;
                st->sag_samples++;
                learned = true;
            }
        }
    } else {
        st->lit_since_us = 0;
        st->sag_armed = false;
    }

    /* --- first estimate --------------------------------------------------- */
    if (!st->valid) {
        if (in->usb && in->chg_state == AOS_SOC_CHG_DONE) {
            st->soc = 100.0f;
        } else if (in->usb) {
            /* charging lifts the voltage above the OCV; a rough guess that
             * the counting below refines */
            st->soc = aos_soc_from_ocv(in->vbat_mv - 60, in->target_mv);
        } else {
            int comp = in->screen_lit || in->audio || in->busy ? (int)st->sag_mv : QUIET_SAG_MV;
            st->soc = aos_soc_from_ocv(in->vbat_mv + comp, in->target_mv);
        }
        st->valid = true;
        st->was_usb = in->usb;
        clamp_soc(st);
        return learned;
    }

    /* --- USB in or out ------------------------------------------------------ */
    if (in->usb && !st->was_usb) {
        st->sess_on = true;
        st->sess_start_soc = st->soc;
        st->sess_trusted = st->quiet_mv_us && in->now_us - st->quiet_mv_us <= 600LL * 1000000;
        st->sess_mah = 0.0f;
        st->sess_saw_cc = false;
    } else if (!in->usb && st->was_usb) {
        st->sess_on = false;        /* unplugged before the end: no measure */
    }
    st->was_usb = in->usb;

    /* --- on the cable ------------------------------------------------------- */
    if (in->usb) {
        float cap = st->cap_mah > 1.0f ? st->cap_mah : AOS_SOC_DEFAULT_CAP_MAH;
        switch (in->chg_state) {
        case AOS_SOC_CHG_CC:
        case AOS_SOC_CHG_PRECHARGE: {
            float ma = in->chg_state == AOS_SOC_CHG_CC ? (float)in->icc_ma : (float)in->ipre_ma;
            float mah = ma * dt_s / 3600.0f;
            st->soc += mah / cap * 100.0f;
            if (st->soc > 99.0f) st->soc = 99.0f;
            st->sess_mah += mah;
            st->sess_saw_cc = true;
            break;
        }
        case AOS_SOC_CHG_CV:
            if (dt_s > 0.0f) {
                st->soc += (99.5f - st->soc) * (1.0f - expf(-dt_s / CV_TAU_S));
            }
            st->sess_mah += cv_mean_ma(in->icc_ma, in->iterm_ma) * dt_s / 3600.0f;
            break;
        case AOS_SOC_CHG_DONE:
            if (st->sess_on && st->sess_trusted && st->sess_saw_cc &&
                st->sess_start_soc <= 40.0f && st->sess_mah > 10.0f) {
                float filled = (100.0f - st->sess_start_soc) / 100.0f;
                float measured = st->sess_mah / filled;
                if (measured > 40.0f && measured < 600.0f) {
                    st->cap_mah = st->cap_samples ? 0.5f * st->cap_mah + 0.5f * measured
                                                  : measured;
                    st->cap_samples++;
                    learned = true;
                }
            }
            st->sess_on = false;
            st->soc = 100.0f;
            break;
        default:
            /* plugged and not charging: full if it is at the target */
            if (in->vbat_mv >= in->target_mv - 50) {
                st->soc = 100.0f;
            }
            break;
        }
        clamp_soc(st);
        return learned;
    }

    /* --- on battery ----------------------------------------------------------- */
    if (quiet) {
        float at_rest = aos_soc_from_ocv(in->vbat_mv + QUIET_SAG_MV, in->target_mv);
        st->soc += FOLLOW * (at_rest - st->soc);
    } else {
        int comp = (int)st->sag_mv;
        if (!in->screen_lit && !in->audio && !in->busy) {
            comp = QUIET_SAG_MV + comp / 2;     /* just switched off: easing */
        }
        float loaded = aos_soc_from_ocv(in->vbat_mv + comp, in->target_mv);
        if (loaded < st->soc) {
            st->soc += FOLLOW * (loaded - st->soc);   /* under load it only goes down */
        }
    }
    clamp_soc(st);
    return learned;
}
