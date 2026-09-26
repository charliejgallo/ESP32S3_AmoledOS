/*
 * AmoledOS - bench for the state-of-charge estimator (components/aos_hal/aos_soc.c).
 *
 *   cc -O2 -I components/aos_hal/include tools/soc/soc_bench.c \
 *      components/aos_hal/aos_soc.c -lm -o /tmp/soc_bench && /tmp/soc_bench
 *
 * A simulated cell (capacity, its own OCV curve slightly off the estimator's
 * generic one, internal resistance), a charger with a constant-current and a
 * constant-voltage phase, and usage profiles. It prints, per scenario, how
 * far the estimate strays from the truth. The cell deliberately does not
 * match the estimator's assumptions: capacity, curve and resistance all
 * differ, because the real cell does too.
 */
#include "aos_soc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    double cap_mah;     /* true capacity, full at 4.2 V */
    double soc;         /* true, 0..100 */
    double r_ohm;
    double ocv_bias_mv; /* the real curve sits this far from the generic one */
} cell_t;

/* The cell's own OCV: the generic curve rescaled, shifted and bent a little. */
static double cell_ocv(const cell_t *c, int target_mv)
{
    /* invert aos_soc_from_ocv by bisection, then distort */
    int lo = 3000, hi = 4300;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (aos_soc_from_ocv(mid) < c->soc) lo = mid; else hi = mid;
    }
    double bend = 15.0 * sin(c->soc / 100.0 * 3.14159);   /* up to 15 mV */
    return hi + c->ocv_bias_mv + bend;
}

typedef struct {
    const char *name;
    double      sim_h;
} scen_t;

static int      s_target = 4100, s_icc = 100, s_ipre = 50, s_iterm = 25;

/* one run: returns worst |estimate - truth| and fills the final numbers */
typedef double (*load_fn)(double t_s, bool *lit, bool *usb);
static bool s_busy_flag;       /* the profile says the radio is busy */

static double run(const char *name, cell_t cell, double start_soc, double hours,
                  load_fn load, float cap_guess, bool verbose)
{
    cell.soc = start_soc;
    aos_soc_t st;
    aos_soc_init(&st, 0, cap_guess);
    double worst = 0, sum = 0; int n = 0;
    double t = 0, dt = 5.0;
    int chg = AOS_SOC_CHG_IDLE;
    double empty_est = -1;
    for (; t < hours * 3600; t += dt) {
        bool lit = false, usb = false;
        double load_ma = load(t, &lit, &usb);
        double i_batt;      /* + discharge, - charge */
        int vbat;
        if (usb) {
            double v_ocv = cell_ocv(&cell, s_target);
            if (chg == AOS_SOC_CHG_DONE) { i_batt = 0; }
            else if (v_ocv + s_icc / 1000.0 * cell.r_ohm * 1000 < s_target) {
                chg = cell.soc < 2 ? AOS_SOC_CHG_PRECHARGE : AOS_SOC_CHG_CC;
                i_batt = -(chg == AOS_SOC_CHG_CC ? s_icc : s_ipre);
            } else {
                chg = AOS_SOC_CHG_CV;
                double i = (s_target - v_ocv) / (cell.r_ohm * 1000.0) * 1000.0;
                if (i < s_iterm) { chg = AOS_SOC_CHG_DONE; i = 0; }
                i_batt = -i;
            }
        } else {
            chg = AOS_SOC_CHG_IDLE;
            i_batt = load_ma;
        }
        cell.soc -= i_batt * dt / 3600.0 / cell.cap_mah * 100.0;
        if (cell.soc > 100) cell.soc = 100;
        if (cell.soc < 0) cell.soc = 0;
        vbat = (int)lrint(cell_ocv(&cell, s_target) - i_batt / 1000.0 * cell.r_ohm * 1000.0);
        if (chg == AOS_SOC_CHG_DONE) vbat = (int)lrint(cell_ocv(&cell, s_target));

        aos_soc_input_t in = {
            .now_us = (int64_t)(t * 1e6), .vbat_mv = vbat, .usb = usb, .chg_state = chg,
            .screen_lit = lit, .audio = false, .busy = s_busy_flag, .icc_ma = s_icc, .ipre_ma = s_ipre,
            .iterm_ma = s_iterm, .target_mv = s_target,
        };
        aos_soc_step(&st, &in);
        double err = st.soc - cell.soc;
        if (t > 120) {          /* the first two minutes settle */
            if (fabs(err) > worst) worst = fabs(err);
            sum += fabs(err); n++;
        }
        if (verbose && fmod(t, 600) < dt)
            printf("   t=%5.0f min  true %5.1f  est %5.1f  vbat %d  %s%s\n", t / 60, cell.soc,
                   st.soc, vbat, usb ? "usb " : "", lit ? "lit" : "");
        if (!usb && cell.soc <= 3.0 && empty_est < 0) empty_est = st.soc;
        if (!usb && cell.soc <= 0.0) break;
    }
    printf("%-34s worst %5.1f  mean %4.1f  sag %3.0f mV (%d)  cap %5.1f mAh (%d)",
           name, worst, n ? sum / n : 0, st.sag_mv, st.sag_samples, st.cap_mah, st.cap_samples);
    if (empty_est >= 0) printf("  shows %2.0f%% at a true 3%%", empty_est);
    printf("\n");
    return worst;
}

/* --- profiles ----------------------------------------------------------- */
static double pocket_away(double t, bool *lit, bool *usb)
{   /* the morning of 2026-09-25: radio flat out, screen off */
    (void)t; *lit = false; *usb = false; return 110;
}
static double pocket_away_flagged(double t, bool *lit, bool *usb)
{   /* the same, with the HAL saying the radio is connecting */
    s_busy_flag = true; return pocket_away(t, lit, usb);
}
static double pocket_fixed(double t, bool *lit, bool *usb)
{   /* the same pocket with the retries paced: light sleep */
    (void)t; *lit = false; *usb = false; return 9;
}
static double daily_use(double t, bool *lit, bool *usb)
{   /* a glance of 40 s every 8 minutes, and a longer use every hour */
    *usb = false;
    double m = fmod(t, 3600);
    double g = fmod(t, 480);
    *lit = (m < 300) || (g < 40);
    return *lit ? 85 : 10;
}
static double charge_from_low(double t, bool *lit, bool *usb)
{   /* 20 min on battery at rest, then plugged in */
    *lit = false; *usb = t > 1200; return 9;
}
static double music(double t, bool *lit, bool *usb)
{   /* screen off, heavier load: the estimate only trusts the voltage at rest,
       and audio is never rest (reported as not lit here, load high) */
    (void)t; *lit = false; *usb = false; return 45;
}

/* Half a charge, then off the cable and left alone. The simulated cell has
 * no relaxation, so this checks the other half: that the estimate counted
 * during the charge is kept, and that the rest readings take over later. */
static double half_charge(double t, bool *lit, bool *usb)
{
    *lit = false; *usb = t > 600 && t < 600 + 1200; return 9;
}

int main(void)
{
    cell_t typical = { .cap_mah = 200, .r_ohm = 0.9, .ocv_bias_mv = 0 };
    cell_t skewed  = { .cap_mah = 170, .r_ohm = 1.3, .ocv_bias_mv = -25 };
    cell_t big     = { .cap_mah = 280, .r_ohm = 0.5, .ocv_bias_mv = 15 };

    printf("battery care: charge to %d mV at %d mA\n\n", s_target, s_icc);
    printf("-- the label's cell (200 mAh, 0.9 ohm) --\n");
    run("pocket, radio flat out (110 mA)", typical, 87, 3, pocket_away, 0, false);
    run("pocket, radio flat out, flagged", typical, 87, 3, pocket_away_flagged, 0, false);
    s_busy_flag = false;
    run("pocket, paced (9 mA)", typical, 87, 20, pocket_fixed, 0, false);
    run("daily use", typical, 87, 12, daily_use, 0, false);
    run("charge from low, learns capacity", typical, 12, 4, charge_from_low, 0, false);
    run("half a charge, then left alone", typical, 15, 3, half_charge, 0, false);
    printf("\n-- a worse cell (170 mAh, 1.3 ohm, curve 25 mV low) --\n");
    run("pocket, radio flat out", skewed, 87, 3, pocket_away, 0, false);
    run("pocket, radio flat out, flagged", skewed, 87, 3, pocket_away_flagged, 0, false);
    s_busy_flag = false;
    run("daily use", skewed, 87, 12, daily_use, 0, false);
    run("charge from low, learns capacity", skewed, 12, 4, charge_from_low, 0, false);
    printf("\n-- a bigger cell (280 mAh) the default does not know --\n");
    run("daily use, capacity unknown", big, 87, 20, daily_use, 0, false);
    run("charge from low, learns capacity", big, 12, 5, charge_from_low, 0, false);

    (void)music;
    return 0;
}
