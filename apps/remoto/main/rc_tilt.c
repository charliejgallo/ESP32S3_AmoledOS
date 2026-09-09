/*
 * Remoto - accelerometer gestures and dial. See rc_tilt.h for the axes.
 */
#include "rc_tilt.h"

#include <math.h>
#include <string.h>

#define RAD2DEG     57.2957795f

/* How much the old sample weighs in the shake accumulator. With samples every
 * 100 ms, 0.55 gives a memory of half a second: just enough for two or three
 * consecutive jerks to add up and for a single knock not to be enough. */
#define JERK_DECAY  0.55f

/* Coming back within this fraction of the threshold rearms the gesture. The
 * hysteresis is what stops a sustained tilt firing in bursts. */
#define REARM       0.5f

/* How quickly the rest position follows the real posture when there is no
 * gesture. At 10 Hz, 0.06 is some two seconds to adopt a new posture: slower
 * than a deliberate tilt and faster than changing hands. */
#define REST_TRACK  0.06f

/* Face down. Measured: face up is -az, so face down is positive az.
 *
 * The value is deliberately high. Tilting forwards and turning it over are the
 * SAME movement at different depths, so the cut decides how much forward pitch
 * is possible before it starts counting as face down: 0.85 g is some 58
 * degrees, comfortable for a threshold of 40. */
#define FACE_DOWN_G 0.85f

float rc_roll_deg(float ax, float ay)
{
    return atan2f(-ay, ax) * RAD2DEG;
}

float rc_pitch_deg(float ax, float az)
{
    return atan2f(-az, ax) * RAD2DEG;
}

float rc_angle_delta(float a, float b)
{
    float d = a - b;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

bool rc_roll_usable(float ax, float ay)
{
    return sqrtf(ax * ax + ay * ay) > RC_PLANE_MIN_G;
}

bool rc_pitch_usable(float ax, float az)
{
    /* The cut is asymmetric and has to be: pitching BACKWARDS takes the screen
     * to the ceiling (negative az) and is never confused with anything, whereas
     * pitching FORWARDS goes straight into face down (positive az). With a
     * fabsf() here, tilting backwards 50 degrees stopped being recognised. */
    return sqrtf(ax * ax + az * az) > RC_PLANE_MIN_G && az < FACE_DOWN_G;
}

float rc_dial_turn(float *acc, float *prev, float roll)
{
    *acc += rc_angle_delta(roll, *prev);
    *prev = roll;
    return *acc;
}

int rc_dial_value(int v0, float turned_deg, int span_deg,
                  int min, int max, int invert)
{
    if (max <= min) {
        return min;
    }
    if (span_deg < 1) {
        span_deg = 1;
    }
    float d = turned_deg;
    if (invert) {
        d = -d;
    }
    /* It is rounded, not truncated: with a travel of 1..100 over 140 degrees
     * each degree is 0.7 points, and truncating towards zero makes the dial
     * feel sticky at the start of the turn and then jump. */
    float step = d * (float)(max - min) / (float)span_deg;
    int   v    = v0 + (int)(step >= 0.0f ? step + 0.5f : step - 0.5f);

    if (v < min) {
        v = min;
    }
    if (v > max) {
        v = max;
    }
    return v;
}

void rc_tilt_reset(rc_tilt_t *t, const rc_gcfg_t *cfg, uint8_t want)
{
    memset(t, 0, sizeof(*t));
    if (cfg) {
        t->cfg = *cfg;
    }
    t->want = want;
}

void rc_tilt_suspend(rc_tilt_t *t, uint32_t now_ms)
{
    t->have_prev  = false;
    t->have_rest  = false;
    t->jerk       = 0.0f;
    t->out_roll   = 0;
    t->out_pitch  = 0;
    t->flip_since = 0;
    t->last_fire  = now_ms;     /* the dead time starts on release */
}

int rc_tilt_feed(rc_tilt_t *t, float ax, float ay, float az, uint32_t now_ms)
{
    t->last_ms = now_ms;

    /* --- total variation, for the shake --- */
    if (t->have_prev) {
        float d = (fabsf(ax - t->px) + fabsf(ay - t->py) + fabsf(az - t->pz))
                  * 1000.0f;
        t->jerk = t->jerk * JERK_DECAY + d;
    } else {
        t->jerk = 0.0f;
        t->have_prev = true;
    }
    t->px = ax;
    t->py = ay;
    t->pz = az;

    /* --- angles ---
     *
     * An accelerometer measures gravity PLUS its own acceleration, and only
     * the first says which way is down. While the board is really moving, the
     * magnitude departs from 1 g and the vector's direction is not a posture:
     * it is where it is being pushed. Without this check, the first jerk of a
     * shake is recognised as a tilt (measured on the bench: it gave TILT_R
     * before it had accumulated enough shake). */
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    t->stable = fabsf(mag - 1.0f) < 0.25f;

    t->roll_ok  = rc_roll_usable(ax, ay);
    t->pitch_ok = rc_pitch_usable(ax, az);
    if (t->roll_ok) {
        t->roll = rc_roll_deg(ax, ay);
    }
    if (t->pitch_ok) {
        t->pitch = rc_pitch_deg(ax, az);
    }

    if (!t->cfg.enabled) {
        return -1;
    }

    /* The dead time is checked AFTER updating everything: if it returned
     * earlier, the rest position would not move and when the silence ended the
     * board would believe the current posture was a gesture. */
    bool quiet = (uint32_t)(now_ms - t->last_fire) < t->cfg.cool_ms;

    int fired = -1;
    float thr  = (float)t->cfg.tilt_deg;

    /* --- shake ---
     *
     * It goes FIRST, and not last as it was written at the start, because a
     * real shake has so much lateral component that halfway through the board
     * "is tilted": the test bench recognised TILT_R on the first jerk. If this
     * is moving, it is being shaken, not aimed.
     *
     * And on recognising it the whole posture is thrown away: when the board
     * comes to rest it will be anywhere, and that is not a deliberate tilt. */
    if ((t->want & RC_G_BIT(RC_G_SHAKE)) &&
        t->jerk > (float)t->cfg.shake_mg) {
        t->have_rest = false;
        t->out_roll  = 0;
        t->out_pitch = 0;
        fired = RC_G_SHAKE;
        goto emitir;
    }

    /* --- face down --- */
    if (!(t->want & RC_G_BIT(RC_G_FLIP))) {
        t->flip_since = 0;
        t->flip_fired = false;
    } else if (az > FACE_DOWN_G) {
        if (!t->flip_since) {
            t->flip_since = now_ms ? now_ms : 1;
        } else if (!t->flip_fired &&
                   (uint32_t)(now_ms - t->flip_since) >= t->cfg.hold_ms) {
            t->flip_fired = true;
            fired = RC_G_FLIP;
        }
    } else {
        t->flip_since = 0;
        t->flip_fired = false;
    }

    /* --- wrist roll --- */
    if (t->roll_ok && t->stable) {
        if (!t->have_rest) {
            t->rest_roll  = t->roll;
            t->rest_pitch = t->pitch;
            t->have_rest  = true;
        }
        float d = rc_angle_delta(t->roll, t->rest_roll);
        if (t->out_roll == 0) {
            if (fired < 0 && d > thr && (t->want & RC_G_BIT(RC_G_TILT_R))) {
                t->out_roll = 1;
                fired = RC_G_TILT_R;
            } else if (fired < 0 && d < -thr && (t->want & RC_G_BIT(RC_G_TILT_L))) {
                t->out_roll = 2;
                fired = RC_G_TILT_L;
            } else if (fabsf(d) < thr) {
                t->rest_roll += d * REST_TRACK;
            }
        } else if (fabsf(d) < thr * REARM) {
            t->out_roll = 0;
        }
    } else if (!t->roll_ok) {
        t->have_rest = false;
        t->out_roll  = 0;
    }

    /* --- pitch --- */
    if (t->pitch_ok && t->stable && t->have_rest) {
        float d = rc_angle_delta(t->pitch, t->rest_pitch);
        if (t->out_pitch == 0) {
            if (fired < 0 && d > thr && (t->want & RC_G_BIT(RC_G_TILT_B))) {
                t->out_pitch = 1;
                fired = RC_G_TILT_B;
            } else if (fired < 0 && d < -thr && (t->want & RC_G_BIT(RC_G_TILT_F))) {
                t->out_pitch = 2;
                fired = RC_G_TILT_F;
            } else if (fabsf(d) < thr) {
                t->rest_pitch += d * REST_TRACK;
            }
        } else if (fabsf(d) < thr * REARM) {
            t->out_pitch = 0;
        }
    } else {
        t->out_pitch = 0;
        if (t->pitch_ok && !t->have_rest) {
            t->rest_pitch = t->pitch;
        }
    }

emitir:
    if (fired < 0) {
        return -1;
    }
    if (quiet) {
        /* It was recognised but is discarded because of the dead time. The
         * accumulator is lowered anyway, so the same shake does not fire the
         * moment it opens. */
        t->jerk = 0.0f;
        return -1;
    }
    t->last_fire = now_ms;
    t->jerk      = 0.0f;
    return fired;
}
