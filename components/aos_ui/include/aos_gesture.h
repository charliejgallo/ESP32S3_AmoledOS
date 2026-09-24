/*
 * AmoledOS - gestures: tap, double tap, long press, drag with fling, and
 * two-finger pinch.
 *
 * The v2's touch chip reports two fingers (docs/GESTURES.md tells how that
 * was found), but it does it badly in ways that have to be filtered: the
 * second finger drops out for up to a second, the first one jumps to garbage
 * as a finger lifts, and on one diagonal the X of the two fingers get
 * swapped. This recogniser eats all of that and hands the app clean events,
 * so no app has to learn the chip.
 *
 *     static void on_gesture(const aos_gesture_event_t *ev, void *user)
 *     {
 *         switch (ev->type) {
 *         case AOS_GESTURE_DRAG:  rotate(ev->dx, ev->dy);          break;
 *         case AOS_GESTURE_PINCH: zoom_at(ev->scale, ev->x, ev->y); break;
 *         default: break;
 *         }
 *     }
 *     aos_gesture_attach(area, 0, on_gesture, me);
 *
 * What the app must know:
 *
 *  - Events arrive in the LVGL task, from a timer: the callback may touch
 *    LVGL, and may delete the object it is attached to (the recogniser goes
 *    with it).
 *  - The chip refreshes ~14 times a second, so DRAG and PINCH come at that
 *    rate. Something that moves under the finger looks best eased towards
 *    the target on every frame, not jumped to it on every event.
 *  - Attaching makes the object clickable, NOT scrollable and not chained to
 *    a scrollable parent, and it only listens to touches that START on that
 *    object. An app that pinches
 *    wants AOS_APP_FLAG_NO_SWIPE and AOS_APP_FLAG_LONG_DRAG too: otherwise the
 *    global back gesture fires on a drag and aborts the touch (aos_app.h).
 *  - After two fingers, a touch never turns back into a drag: the pinch lasts
 *    until every finger is up. Lifting one finger mid-pinch is where the chip
 *    reports garbage, and a model that spins by itself when you let go of a
 *    zoom is worse than one that waits.
 *  - Scale and centre are safe against the chip's swapped diagonal (they do
 *    not depend on which X goes with which Y); a two-finger ROTATION would
 *    not be, and that is why there is none.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AOS_GESTURE_TAP = 1,        /* short and still. Waits out the double-tap
                                 * window (~300 ms) unless FAST_TAP          */
    AOS_GESTURE_DOUBLE_TAP,     /* second tap close in time and place        */
    AOS_GESTURE_LONG_PRESS,     /* one finger still for 550 ms               */
    AOS_GESTURE_DRAG_BEGIN,     /* x,y = where the finger first went down    */
    AOS_GESTURE_DRAG,           /* dx,dy since the previous DRAG             */
    AOS_GESTURE_DRAG_END,       /* vx,vy = speed at release (px/s), 0 if the
                                 * finger had stopped: fling or not          */
    AOS_GESTURE_PINCH_BEGIN,    /* x,y = centre between the fingers          */
    AOS_GESTURE_PINCH,          /* scale since the previous PINCH (multiply
                                 * it in); dx,dy = motion of the centre      */
    AOS_GESTURE_PINCH_END,
} aos_gesture_type_t;

typedef struct {
    aos_gesture_type_t type;
    float    x, y;          /* screen px: the finger, or the pinch centre     */
    float    dx, dy;        /* DRAG, PINCH                                    */
    float    scale;         /* PINCH; 1 for every other event                 */
    float    dist;          /* PINCH*: distance between the fingers, px       */
    float    vx, vy;        /* DRAG_END                                       */
    bool     after_long;    /* DRAG*: the drag started with a long press      */
    uint32_t t_ms;          /* when (aos_hal_uptime_ms clock)                 */
} aos_gesture_event_t;

typedef void (*aos_gesture_cb_t)(const aos_gesture_event_t *ev, void *user);

typedef struct aos_gesture aos_gesture_t;

enum {
    /* TAP as soon as the finger lifts, without waiting to see whether a
     * second tap follows. A double tap then arrives as TAP + DOUBLE_TAP. */
    AOS_GESTURE_FLAG_FAST_TAP = 1u << 0,
};

aos_gesture_t *aos_gesture_attach(lv_obj_t *obj, uint32_t flags,
                                  aos_gesture_cb_t cb, void *user);
/* Optional: deleting the object detaches too. */
void aos_gesture_detach(aos_gesture_t *g);

/* --------------------------------------------------------------------------
 * Fingers, one by one (for on-screen game pads)
 *
 * A pinch cares about two fingers together; a pad cares about each finger
 * on its own: one on the stick, the other on FIRE, pressing and lifting
 * independently. aos_touch_points() hands out the fingers that are down with
 * an id that stays with each finger while it stays down, and the same
 * filtering of the chip as the pinch:
 *
 *  - a second finger counts after two samples (the first can be bogus);
 *  - a finger that vanishes for one sample is kept (up to ~120 ms): the
 *    chip drops the second finger now and then, and a FIRE that flickers
 *    off is worse than one that lifts a sample late;
 *  - a finger that jumps more than ~110 px in one sample is held where it
 *    was for that sample: that is the garbage sample of a finger lifting,
 *    and if the next sample agrees it is a different finger (new id).
 *
 * Pairing: with the two fingers at about the same height (a pad along the
 * bottom) the points are clean; on the ↗↙ diagonal the chip can swap their
 * X (docs/GESTURES.md), so a pad should keep its controls level.
 *
 * Call it from the LVGL task (a timer, the app's frame); it reads the
 * latest sample and only does work when there is a new one. Screen pixels.
 * -------------------------------------------------------------------------- */

typedef struct {
    bool     down;
    uint8_t  id;            /* 1..255, new for every finger that lands     */
    float    x, y;
    uint32_t t_down;        /* when it landed (aos_hal_uptime_ms clock)    */
} aos_touch_point_t;

/* Fills both slots (down = false for an empty one) and returns how many
 * fingers are down. A finger keeps its slot while it stays down. */
int aos_touch_points(aos_touch_point_t out[2]);

/* true = this hardware reports two fingers, so pinching is there to use.
 * Worth checking to decide whether to offer +/- buttons instead. */
bool aos_gesture_multitouch(void);

#ifdef __cplusplus
}
#endif
