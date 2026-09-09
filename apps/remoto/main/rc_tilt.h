/*
 * Remoto - accelerometer gestures and dial
 *
 * The axes are NOT the ones you would assume and are measured on the board
 * (docs/DECISIONES.md, "Ejes del acelerometro: MEDIDOS (2026-08-28)"):
 *
 *   ax  is the screen's VERTICAL axis, and the BOTTOM edge is +ax
 *   ay  is the HORIZONTAL one, and the RIGHT of the screen is -ay
 *   az  the normal, and face UP is -az
 *
 * From that come the two angles all of this uses, both in degrees and both
 * reading 0 with the board standing still facing you:
 *
 *   roll   atan2(-ay, ax)   positive = clockwise turn, the right edge drops.
 *                           It is the dial's angle.
 *   pitch  atan2(-az, ax)   positive = it tilts backwards, the screen faces
 *                           the ceiling.
 *
 * It is checked in tools/rc_harness.c against the four measured postures,
 * which is the only way a wrong sign never reaches the board.
 *
 * Without LVGL and without the HAL: an accelerometer goes in and a gesture
 * comes out.
 */
#pragma once

#include "rc_model.h"
#include <stdint.h>
#include <stdbool.h>

/* How much gravity there has to be in a plane for its angle to mean anything.
 * With the board resting, the roll is the heading of a broken compass. */
#define RC_PLANE_MIN_G  0.45f

float rc_roll_deg(float ax, float ay);
float rc_pitch_deg(float ax, float az);

/* Difference between two angles, brought into -180..180. */
float rc_angle_delta(float a, float b);

bool rc_roll_usable(float ax, float ay);
bool rc_pitch_usable(float ax, float az);

/* Which gestures have to be recognised, one per bit (1u << RC_G_*).
 *
 * It is not an optimisation: it changes the result. Tilting forwards and
 * turning the board over are the same movement at different depths, so if the
 * recogniser always looked for both, the pitch would run over the face-down of
 * anybody who had configured only the second. What is not in the profile does
 * not exist. */
#define RC_G_BIT(g)     (1u << (g))
#define RC_G_ALL        0x3Fu

typedef struct {
    rc_gcfg_t cfg;
    uint8_t   want;         /* mask of RC_G_BIT() */

    /* Last sample, for the variation */
    bool     have_prev;
    float    px, py, pz;

    /* What can be looked at from the diagnostics screen */
    float    roll, pitch;
    float    jerk;          /* milli-g accumulated with decay */
    bool     roll_ok, pitch_ok;

    /* Moving rest position: the gestures are relative to how you are holding
     * the board, not to the absolute vertical. Holding it tilted does not
     * fire. */
    bool     have_rest;
    float    rest_roll, rest_pitch;

    uint8_t  out_roll;      /* 0 at rest, 1 out to the right, 2 to the left */
    uint8_t  out_pitch;     /* 0 at rest, 1 backwards, 2 forwards           */

    uint32_t flip_since;    /* 0 = it is not face down */
    bool     flip_fired;

    bool     stable;        /* |a| near 1 g: the direction is gravity */

    uint32_t last_fire;
    uint32_t last_ms;
} rc_tilt_t;

void rc_tilt_reset(rc_tilt_t *t, const rc_gcfg_t *cfg, uint8_t want);

/* Give it one accelerometer sample (in g, as aos_hal.h declares) and it
 * returns an rc_gesture_t if it has just recognised one, or -1.
 *
 * One sample every ~100 ms is expected: the IMU hangs off the same I2C bus as
 * the touch panel and polling it more often has already saturated CPU 0 once
 * (it is in DECISIONES.md). At 10 Hz a shake is not measured by its frequency
 * -aliasing is certain- but by the accumulated total variation, which does not
 * depend on it. */
int rc_tilt_feed(rc_tilt_t *t, float ax, float ay, float az, uint32_t now_ms);

/* --------------------------------------------------------------------------
 * The dial
 *
 * The value is RELATIVE to the roll the wrist had when it was pressed, not
 * absolute: zero is where you were. That way the dial is used the same sitting
 * at the table as sprawled on the sofa, and there is no vertical to calibrate.
 *
 * The roll is ACCUMULATED sample by sample instead of being compared against
 * the latch's angle, and that is not a detail: atan2 wraps at +-180, so
 * subtracting against the initial angle makes a 200-degree turn read as -160
 * and the dial jumps to the other end exactly when you are turning most.
 * Between two samples 100 ms apart the change is a few degrees, there the wrap
 * resolves itself, and with that you can turn more than half a revolution.
 * -------------------------------------------------------------------------- */

/* Adds the stretch turned since the previous sample. Returns the accumulated
 * total. On latching: *acc = 0 and *prev = the roll at that moment. */
float rc_dial_turn(float *acc, float *prev, float roll);

/*   v0          value at the moment of pressing
 *   turned_deg  what rc_dial_turn() returned
 *   span_deg    degrees of wrist needed to go from min to max  */
int rc_dial_value(int v0, float turned_deg, int span_deg,
                  int min, int max, int invert);

/* Stops the recognition until the finger is lifted: while it is on the screen,
 * moving the board is holding it, not gesturing. */
void rc_tilt_suspend(rc_tilt_t *t, uint32_t now_ms);
