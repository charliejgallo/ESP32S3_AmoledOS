/*
 * AmoledOS - The battery's state of charge, estimated by the firmware.
 *
 * Why this exists: the AXP2101's own gauge uses a generic battery model and
 * does not know this cell. On 2026-09-25 it read 49 % and 65 % the two times
 * the watch ran flat (the recharge afterwards took as long as one from
 * empty), and it jumps to 100 % the moment the charger finishes. The chip
 * cannot measure current, so the estimate is built from what it can:
 *
 *   - At rest (screen not lit for 30 s, no audio) the voltage is close to
 *     the open-circuit voltage, and a lithium cell's OCV maps to its charge.
 *   - With the screen lit the voltage sags under the load. How much is
 *     learned on the watch: every time the screen is lit from rest, the
 *     voltage before and 20 s after are compared.
 *   - While charging, the current IS known in the constant-current phase
 *     (it is what the firmware programmed), so charge is counted in; the
 *     constant-voltage tail is approached smoothly and "done" is 100 %.
 *   - A full charge that started low measures the capacity: charge counted
 *     in, over the fraction of the cell it filled.
 *
 * Pure C, no IDF: it builds on the Mac too (tools/soc/soc_bench.c).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The charger's stage, as axp2101_chg_state_t / aos_charge_state_t. */
enum {
    AOS_SOC_CHG_TRICKLE = 0,
    AOS_SOC_CHG_PRECHARGE,
    AOS_SOC_CHG_CC,
    AOS_SOC_CHG_CV,
    AOS_SOC_CHG_DONE,
    AOS_SOC_CHG_IDLE,
};

typedef struct {
    int64_t now_us;
    int     vbat_mv;
    bool    usb;            /* VBUS present                              */
    int     chg_state;      /* AOS_SOC_CHG_*                             */
    bool    screen_lit;     /* the display is ACTIVE                     */
    bool    audio;          /* playing or recording                      */
    bool    busy;           /* something else loads the cell: the radio
                             * connecting, the link, a worker at full tilt.
                             * Never taken as rest.                         */
    int     icc_ma;         /* constant-current limit as programmed      */
    int     ipre_ma;        /* precharge current as programmed           */
    int     iterm_ma;       /* termination current as programmed         */
    int     target_mv;      /* charge target: 4100 with battery care     */
} aos_soc_input_t;

typedef struct {
    /* the estimate */
    float   soc;            /* 0..100                                     */
    bool    valid;
    /* learned, persisted by the caller */
    float   sag_mv;         /* voltage lost with the screen lit           */
    float   cap_mah;        /* usable capacity up to the charge target    */
    int     sag_samples;
    int     cap_samples;
    /* internal */
    int64_t last_us;
    int64_t quiet_since_us; /* 0 = not quiet                              */
    int     quiet_mv;       /* the latest reading at rest                 */
    int64_t quiet_mv_us;
    int64_t lit_since_us;   /* 0 = not lit                                */
    bool    sag_armed;
    int     sag_from_mv;
    bool    was_usb;
    /* the charge session in progress */
    bool    sess_on;
    bool    sess_trusted;   /* it started from a reading at rest          */
    float   sess_start_soc;
    float   sess_mah;
    bool    sess_saw_cc;
} aos_soc_t;

#define AOS_SOC_DEFAULT_SAG_MV      80.0f
#define AOS_SOC_DEFAULT_CAP_MAH     130.0f

/* Fresh state. sag_mv / cap_mah <= 0 mean "the defaults above". */
void  aos_soc_init(aos_soc_t *st, float sag_mv, float cap_mah);

/* The open-circuit voltage of a lithium polymer cell mapped to its charge,
 * with 100 % at a cell charged to target_mv and left to rest. A generic
 * curve until this cell's own has been recorded (the battery history keeps
 * the voltage for that). */
float aos_soc_from_ocv(int ocv_mv, int target_mv);

/* One step, every few seconds. Returns true when a learned value changed and
 * should be persisted. */
bool  aos_soc_step(aos_soc_t *st, const aos_soc_input_t *in);

/* Rounded percent, or -1 when there is no estimate yet. */
int   aos_soc_percent(const aos_soc_t *st);

#ifdef __cplusplus
}
#endif
