/*
 * AmoledOS - gesture recogniser (see aos_gesture.h and docs/GESTURES.md).
 *
 * It does not listen to LVGL's pointer, which is one finger: it reads the
 * HAL's two-finger frames (aos_hal_touch_frame) from a 10 ms timer that only
 * runs while a touch it cares about is in progress. LVGL is used for one
 * thing: LV_EVENT_PRESSED on the object says a touch started THERE.
 *
 * Every rule below comes from a measurement on the board (2026-09-24); the
 * document has the logs. In short, the chip:
 *
 *  - refreshes ~every 70 ms; we read every ~36, and the HAL only bumps seq on
 *    a real change, so each new sample is processed exactly once;
 *  - sometimes reports a bogus second point for the first sample after it
 *    lands (1,447 with the finger at the top): a second finger counts only
 *    after TWO samples in a row;
 *  - drops the second finger for up to 0.9 s on the ↗↙ diagonal: during a
 *    pinch, a sample with one finger is simply ignored, and when the second
 *    one comes back the pinch re-anchors instead of jumping;
 *  - swaps the two X on that diagonal, or collapses them: distance and
 *    centre do not care about the swap; a centre that jumps more than
 *    CENTRE_JUMP in one sample is a collapse, and re-anchors;
 *  - puts point 1 on garbage for the sample where a finger lifts: after two
 *    fingers, nothing is emitted until every finger is up.
 */
#include "aos_gesture.h"
#include "aos_hal.h"
#include "aos_ui.h"

#include <math.h>
#include <string.h>

#define TICK_MS         10
#define SLOP_PX         10.0f   /* movement that turns a press into a drag   */
#define TAP_MAX_MS      300     /* longer than this is not a tap             */
#define DOUBLE_GAP_MS   300     /* from the first release to the second one  */
#define DOUBLE_NEAR_PX  48.0f
#define LONG_MS         550
#define PINCH_CONFIRM   2       /* samples in a row with two fingers         */
#define PINCH_MIN_DIST  16.0f   /* floor for the ratio: fingers touching     */
#define CENTRE_JUMP     90.0f   /* px in one sample: not a real movement     */
#define FLING_WINDOW_MS 160     /* speed measured over the last ~2 samples   */
#define FLING_STALE_MS  150     /* last move older than this = it had stopped */
#define HIST            6
#define ARMED_MS        300     /* how long a press waits for its first sample */

typedef enum { ST_IDLE, ST_PENDING, ST_DRAG, ST_PINCH } state_t;

struct aos_gesture {
    lv_obj_t        *obj;
    aos_gesture_cb_t cb;
    void            *user;
    uint32_t         flags;
    lv_timer_t      *timer;

    bool     in_cb, dead;       /* the callback deleted the object          */
    uint32_t seq;               /* last frame processed                     */
    state_t  state;
    bool     armed;             /* a press landed ON the object: the next
                                 * touch is ours. Frames are global, and the
                                 * timer may still be running for a tap when
                                 * a finger lands somewhere else.            */
    uint32_t armed_t;

    /* one finger */
    float    sx, sy, lx, ly;    /* where it went down / last emitted        */
    uint32_t t_down;
    bool     long_fired;
    struct { float x, y; uint32_t t; } hist[HIST];
    int      hist_n;

    /* two fingers */
    int      streak;            /* consecutive samples with two             */
    bool     pinch_begun, anchored;
    float    pd, pcx, pcy;      /* last distance and centre                 */

    /* taps */
    bool     tap_pending;       /* a TAP waiting out the double-tap window  */
    bool     last_tap_valid;
    uint32_t last_tap_t;
    float    last_tap_x, last_tap_y;
};

static uint32_t now_ms(void)
{
    return (uint32_t)aos_hal_uptime_ms();
}

/* false = the object died inside the callback: stop touching 'g'. */
static bool emit(aos_gesture_t *g, aos_gesture_type_t type, float x, float y,
                 uint32_t t)
{
    aos_gesture_event_t ev = {
        .type = type, .x = x, .y = y, .scale = 1.0f, .t_ms = t,
    };
    g->in_cb = true;
    g->cb(&ev, g->user);
    g->in_cb = false;
    return !g->dead;
}

static bool emit_ev(aos_gesture_t *g, aos_gesture_event_t *ev)
{
    g->in_cb = true;
    g->cb(ev, g->user);
    g->in_cb = false;
    return !g->dead;
}

/* The single tap still waiting, delivered now because something else is
 * about to happen (a drag, a pinch) and order matters. */
static bool flush_tap(aos_gesture_t *g)
{
    if (!g->tap_pending) {
        return true;
    }
    g->tap_pending = false;
    return emit(g, AOS_GESTURE_TAP, g->last_tap_x, g->last_tap_y, g->last_tap_t);
}

static void hist_push(aos_gesture_t *g, float x, float y, uint32_t t)
{
    if (g->hist_n == HIST) {
        memmove(&g->hist[0], &g->hist[1], sizeof(g->hist[0]) * (HIST - 1));
        g->hist_n--;
    }
    g->hist[g->hist_n].x = x;
    g->hist[g->hist_n].y = y;
    g->hist[g->hist_n].t = t;
    g->hist_n++;
}

static void fling_speed(const aos_gesture_t *g, uint32_t t_up, float *vx, float *vy)
{
    *vx = *vy = 0.0f;
    if (g->hist_n < 2) {
        return;
    }
    const int last = g->hist_n - 1;
    if (t_up - g->hist[last].t > FLING_STALE_MS) {
        return;                     /* the finger rested before lifting */
    }
    int first = last - 1;
    while (first > 0 && g->hist[last].t - g->hist[first - 1].t <= FLING_WINDOW_MS) {
        first--;
    }
    uint32_t dt = g->hist[last].t - g->hist[first].t;
    if (dt == 0) {
        return;
    }
    *vx = (g->hist[last].x - g->hist[first].x) * 1000.0f / (float)dt;
    *vy = (g->hist[last].y - g->hist[first].y) * 1000.0f / (float)dt;
}

static void on_release(aos_gesture_t *g, uint32_t t)
{
    state_t st = g->state;
    g->state = ST_IDLE;

    if (st == ST_PINCH) {
        if (g->pinch_begun) {
            emit(g, AOS_GESTURE_PINCH_END, g->pcx, g->pcy, t);
        }
        return;
    }
    if (st == ST_DRAG) {
        aos_gesture_event_t ev = {
            .type = AOS_GESTURE_DRAG_END, .x = g->lx, .y = g->ly,
            .scale = 1.0f, .after_long = g->long_fired, .t_ms = t,
        };
        fling_speed(g, t, &ev.vx, &ev.vy);
        emit_ev(g, &ev);
        return;
    }
    if (st != ST_PENDING || g->long_fired || t - g->t_down > TAP_MAX_MS) {
        return;
    }

    /* A tap. The second of a pair? */
    if (g->last_tap_valid && t - g->last_tap_t <= DOUBLE_GAP_MS + TAP_MAX_MS &&
        fabsf(g->sx - g->last_tap_x) <= DOUBLE_NEAR_PX &&
        fabsf(g->sy - g->last_tap_y) <= DOUBLE_NEAR_PX) {
        g->last_tap_valid = false;
        g->tap_pending = false;
        emit(g, AOS_GESTURE_DOUBLE_TAP, g->sx, g->sy, t);
        return;
    }
    if (!flush_tap(g)) {
        return;
    }
    g->last_tap_valid = true;
    g->last_tap_t = t;
    g->last_tap_x = g->sx;
    g->last_tap_y = g->sy;
    if (g->flags & AOS_GESTURE_FLAG_FAST_TAP) {
        emit(g, AOS_GESTURE_TAP, g->sx, g->sy, t);
    } else {
        g->tap_pending = true;
    }
}

static void pinch_sample(aos_gesture_t *g, const float x[2], const float y[2],
                         uint32_t t)
{
    float dx = x[1] - x[0], dy = y[1] - y[0];
    float d  = sqrtf(dx * dx + dy * dy);
    float cx = (x[0] + x[1]) * 0.5f, cy = (y[0] + y[1]) * 0.5f;

    if (!g->anchored) {
        g->anchored = true;
        g->pd = d; g->pcx = cx; g->pcy = cy;
        if (!g->pinch_begun) {
            g->pinch_begun = true;
            aos_gesture_event_t ev = {
                .type = AOS_GESTURE_PINCH_BEGIN, .x = cx, .y = cy,
                .scale = 1.0f, .dist = d, .t_ms = t,
            };
            emit_ev(g, &ev);
        }
        return;
    }
    float jx = cx - g->pcx, jy = cy - g->pcy;
    if (jx * jx + jy * jy > CENTRE_JUMP * CENTRE_JUMP) {
        g->pd = d; g->pcx = cx; g->pcy = cy;    /* collapsed X: re-anchor */
        return;
    }
    aos_gesture_event_t ev = {
        .type  = AOS_GESTURE_PINCH, .x = cx, .y = cy, .dx = jx, .dy = jy,
        .scale = fmaxf(d, PINCH_MIN_DIST) / fmaxf(g->pd, PINCH_MIN_DIST),
        .dist  = d, .t_ms = t,
    };
    g->pd = d; g->pcx = cx; g->pcy = cy;
    if (ev.scale != 1.0f || jx != 0.0f || jy != 0.0f) {
        emit_ev(g, &ev);
    }
}

static void process(aos_gesture_t *g, const aos_touch_frame_t *f)
{
    float x[2], y[2];
    for (int i = 0; i < f->count && i < 2; i++) {
        int32_t sx, sy;
        aos_ui_touch_map(f->x[i], f->y[i], &sx, &sy);
        x[i] = (float)sx;
        y[i] = (float)sy;
    }

    if (f->count == 0) {
        if (g->state != ST_IDLE) {
            on_release(g, f->t_ms);
        }
        return;
    }

    if (g->state == ST_IDLE) {
        if (!g->armed) {
            return;                 /* a touch that started elsewhere */
        }
        g->armed = false;
        g->state = ST_PENDING;
        g->sx = g->lx = x[0];
        g->sy = g->ly = y[0];
        g->t_down = f->t_ms;
        g->long_fired = false;
        g->hist_n = 0;
        g->streak = 0;
        g->pinch_begun = g->anchored = false;
        hist_push(g, x[0], y[0], f->t_ms);
    }

    if (f->count == 2) {
        if (++g->streak < PINCH_CONFIRM) {
            return;                 /* maybe a bogus point: wait for another */
        }
        if (g->state == ST_DRAG) {
            aos_gesture_event_t ev = {
                .type = AOS_GESTURE_DRAG_END, .x = g->lx, .y = g->ly,
                .scale = 1.0f, .after_long = g->long_fired, .t_ms = f->t_ms,
            };
            if (!emit_ev(g, &ev)) return;
        }
        if (g->state != ST_PINCH) {
            if (!flush_tap(g)) return;
            g->state = ST_PINCH;
        }
        pinch_sample(g, x, y, f->t_ms);
        return;
    }

    /* one finger */
    g->streak = 0;
    if (g->state == ST_PINCH) {
        g->anchored = false;        /* hold; re-anchor when the second returns */
        return;
    }
    if (g->state == ST_PENDING) {
        float mx = x[0] - g->sx, my = y[0] - g->sy;
        if (mx * mx + my * my < SLOP_PX * SLOP_PX) {
            return;
        }
        if (!flush_tap(g)) return;
        g->state = ST_DRAG;
        aos_gesture_event_t ev = {
            .type = AOS_GESTURE_DRAG_BEGIN, .x = g->sx, .y = g->sy,
            .scale = 1.0f, .after_long = g->long_fired, .t_ms = f->t_ms,
        };
        if (!emit_ev(g, &ev)) return;
    }
    /* ST_DRAG */
    aos_gesture_event_t ev = {
        .type = AOS_GESTURE_DRAG, .x = x[0], .y = y[0],
        .dx = x[0] - g->lx, .dy = y[0] - g->ly,
        .scale = 1.0f, .after_long = g->long_fired, .t_ms = f->t_ms,
    };
    g->lx = x[0];
    g->ly = y[0];
    hist_push(g, x[0], y[0], f->t_ms);
    emit_ev(g, &ev);
}

static void tick_cb(lv_timer_t *timer)
{
    aos_gesture_t *g = lv_timer_get_user_data(timer);
    aos_touch_frame_t f;

    if (aos_hal_touch_frame(&f) && f.seq != g->seq) {
        g->seq = f.seq;
        process(g, &f);
        if (g->dead) return;
    }

    uint32_t now = now_ms();
    if (g->state == ST_PENDING && !g->long_fired && now - g->t_down >= LONG_MS) {
        g->long_fired = true;
        if (!flush_tap(g)) return;
        if (!emit(g, AOS_GESTURE_LONG_PRESS, g->sx, g->sy, now)) return;
    }
    if (g->tap_pending && g->state == ST_IDLE &&
        now - g->last_tap_t > DOUBLE_GAP_MS) {
        if (!flush_tap(g)) return;
    }
    if (g->last_tap_valid && now - g->last_tap_t > DOUBLE_GAP_MS + TAP_MAX_MS) {
        g->last_tap_valid = false;
    }

    /* A press whose first sample never showed a finger (it was lifted
     * before the chip reported it) stops waiting. */
    if (g->armed && g->state == ST_IDLE && now - g->armed_t > ARMED_MS) {
        g->armed = false;
    }

    /* Nothing in flight: sleep until the next press on the object. Armed
     * counts as in flight: LVGL's press can arrive BEFORE the sample that
     * carries it -the latest one may still be the previous touch's release,
     * zero fingers-, and a recogniser that went to sleep on that stale
     * sample never saw the touch. Seen in the simulator, where samples are
     * rationed to 14 Hz; possible on the watch for the same reason. */
    if (g->state == ST_IDLE && !g->armed && !g->tap_pending && !g->last_tap_valid) {
        lv_timer_pause(g->timer);
    }
}

static void pressed_cb(lv_event_t *e)
{
    aos_gesture_t *g = lv_event_get_user_data(e);
    aos_touch_frame_t f;
    if (g->state != ST_IDLE) {
        return;
    }
    /* The frame that carried this press is the first sample to process:
     * step the counter back so the next tick takes it. */
    if (aos_hal_touch_frame(&f)) {
        g->seq = f.seq - 1;
    }
    g->armed = true;
    g->armed_t = now_ms();
    lv_timer_resume(g->timer);
    lv_timer_ready(g->timer);
}

static void free_gesture(aos_gesture_t *g)
{
    if (g->timer) {
        lv_timer_delete(g->timer);
        g->timer = NULL;
    }
    lv_free(g);
}

static void delete_cb(lv_event_t *e)
{
    aos_gesture_t *g = lv_event_get_user_data(e);
    g->obj = NULL;
    if (g->in_cb) {
        /* Deleted from inside our own callback: the tick that called it is
         * still running and returns as soon as it sees 'dead'. The timer
         * goes now; the struct, on the next LVGL timer pass. */
        g->dead = true;
        if (g->timer) {
            lv_timer_delete(g->timer);
            g->timer = NULL;
        }
        lv_async_call((lv_async_cb_t)lv_free, g);
        return;
    }
    free_gesture(g);
}

aos_gesture_t *aos_gesture_attach(lv_obj_t *obj, uint32_t flags,
                                  aos_gesture_cb_t cb, void *user)
{
    if (!obj || !cb) {
        return NULL;
    }
    aos_gesture_t *g = lv_malloc_zeroed(sizeof(*g));
    if (!g) {
        return NULL;
    }
    g->obj   = obj;
    g->cb    = cb;
    g->user  = user;
    g->flags = flags;
    g->timer = lv_timer_create(tick_cb, TICK_MS, g);
    lv_timer_pause(g->timer);

    /* Clickable, so the press lands here; and neither scrollable nor
     * chained to a scrollable parent, or LVGL would scroll the page behind
     * with the same finger we are reading as a pinch. */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(obj, pressed_cb, LV_EVENT_PRESSED, g);
    lv_obj_add_event_cb(obj, delete_cb, LV_EVENT_DELETE, g);
    return g;
}

void aos_gesture_detach(aos_gesture_t *g)
{
    if (!g) {
        return;
    }
    if (g->obj) {
        lv_obj_remove_event_cb_with_user_data(g->obj, pressed_cb, g);
        lv_obj_remove_event_cb_with_user_data(g->obj, delete_cb, g);
    }
    if (g->in_cb) {
        g->dead = true;
        if (g->timer) {
            lv_timer_delete(g->timer);
            g->timer = NULL;
        }
        lv_async_call((lv_async_cb_t)lv_free, g);
        return;
    }
    free_gesture(g);
}

/* --------------------------------------------------------------------------
 * Fingers one by one (aos_touch_points)
 * -------------------------------------------------------------------------- */

#define TRACK_HOLD_MS   120     /* a finger missing this long is up          */
#define TRACK_JUMP      110.0f  /* px in one sample: garbage, held once      */

typedef struct {
    aos_touch_point_t p;
    uint32_t seen_ms;           /* last sample that had it                   */
    bool     jumped;            /* held one sample on a jump                 */
    float    jx, jy;            /* where it jumped to                        */
} track_t;

static track_t  s_tracks[2];
static uint32_t s_track_seq;
static uint8_t  s_next_id;
static int      s_new_streak;   /* samples in a row with an unmatched point */

static float dist2(float ax, float ay, float bx, float by)
{
    return (ax - bx) * (ax - bx) + (ay - by) * (ay - by);
}

static void track_start(track_t *t, float x, float y, uint32_t now)
{
    if (++s_next_id == 0) s_next_id = 1;
    t->p.down = true;
    t->p.id = s_next_id;
    t->p.x = x;
    t->p.y = y;
    t->p.t_down = now;
    t->seen_ms = now;
    t->jumped = false;
}

/* One point for one track: move it, unless it jumped. */
static void track_feed(track_t *t, float x, float y, uint32_t now)
{
    t->seen_ms = now;
    if (dist2(x, y, t->p.x, t->p.y) > TRACK_JUMP * TRACK_JUMP) {
        if (!t->jumped || dist2(x, y, t->jx, t->jy) > TRACK_JUMP * TRACK_JUMP) {
            t->jumped = true;           /* hold it here for this sample */
            t->jx = x;
            t->jy = y;
            return;
        }
        track_start(t, x, y, now);      /* confirmed: another finger */
        return;
    }
    t->jumped = false;
    t->p.x = x;
    t->p.y = y;
}

static void tracks_update(const aos_touch_frame_t *f)
{
    float x[2], y[2];
    int n = f->count > 2 ? 2 : f->count;
    for (int i = 0; i < n; i++) {
        int32_t sx, sy;
        aos_ui_touch_map(f->x[i], f->y[i], &sx, &sy);
        x[i] = (float)sx;
        y[i] = (float)sy;
    }
    uint32_t now = f->t_ms;
    bool used[2] = { false, false };

    if (n == 0) {
        s_tracks[0].p.down = s_tracks[1].p.down = false;
        s_new_streak = 0;
        return;
    }

    /* Match the fingers that are down to the nearest points. */
    int down = (s_tracks[0].p.down ? 1 : 0) + (s_tracks[1].p.down ? 1 : 0);
    if (down == 2 && n == 2) {
        float keep = dist2(x[0], y[0], s_tracks[0].p.x, s_tracks[0].p.y) +
                     dist2(x[1], y[1], s_tracks[1].p.x, s_tracks[1].p.y);
        float swap = dist2(x[1], y[1], s_tracks[0].p.x, s_tracks[0].p.y) +
                     dist2(x[0], y[0], s_tracks[1].p.x, s_tracks[1].p.y);
        int a = swap < keep ? 1 : 0;
        track_feed(&s_tracks[0], x[a], y[a], now);
        track_feed(&s_tracks[1], x[1 - a], y[1 - a], now);
        used[0] = used[1] = true;
    } else {
        for (int t = 0; t < 2; t++) {
            if (!s_tracks[t].p.down) continue;
            int best = -1;
            float bd = 0;
            for (int i = 0; i < n; i++) {
                if (used[i]) continue;
                float d = dist2(x[i], y[i], s_tracks[t].p.x, s_tracks[t].p.y);
                if (best < 0 || d < bd) { best = i; bd = d; }
            }
            if (best >= 0) {
                used[best] = true;
                track_feed(&s_tracks[t], x[best], y[best], now);
            }
        }
    }

    /* Fingers missing from this sample: kept a moment, then up. */
    for (int t = 0; t < 2; t++) {
        if (s_tracks[t].p.down && now - s_tracks[t].seen_ms > TRACK_HOLD_MS) {
            s_tracks[t].p.down = false;
        }
    }

    /* A point nobody claimed is a new finger: the first one at once, a
     * second one after two samples (the chip's first point 2 can be bogus). */
    int unmatched = -1;
    for (int i = 0; i < n; i++) {
        if (!used[i]) { unmatched = i; break; }
    }
    if (unmatched < 0) {
        s_new_streak = 0;
        return;
    }
    int free_slot = !s_tracks[0].p.down ? 0 : (!s_tracks[1].p.down ? 1 : -1);
    if (free_slot < 0) {
        return;
    }
    bool other_down = s_tracks[1 - free_slot].p.down;
    if (other_down && ++s_new_streak < 2) {
        return;
    }
    s_new_streak = 0;
    track_start(&s_tracks[free_slot], x[unmatched], y[unmatched], now);
}

int aos_touch_points(aos_touch_point_t out[2])
{
    aos_touch_frame_t f;
    if (!aos_hal_touch_frame(&f)) {
        f.count = 0;
    } else if (f.seq != s_track_seq) {
        s_track_seq = f.seq;
        tracks_update(&f);
    }
    /* A held finger expires on the clock too, not only on a new sample:
     * while the finger that stayed rests still the chip sends nothing new,
     * and the missing one would be down forever. Only when the latest
     * sample really has fewer fingers than we hold, and the oldest goes. */
    int held = (s_tracks[0].p.down ? 1 : 0) + (s_tracks[1].p.down ? 1 : 0);
    if (held > f.count) {
        uint32_t now = (uint32_t)aos_hal_uptime_ms();
        int t = -1;
        for (int i = 0; i < 2; i++) {
            if (s_tracks[i].p.down &&
                (t < 0 || s_tracks[i].seen_ms < s_tracks[t].seen_ms)) {
                t = i;
            }
        }
        if (t >= 0 && now - s_tracks[t].seen_ms > TRACK_HOLD_MS) {
            s_tracks[t].p.down = false;
        }
    }
    int n = 0;
    for (int t = 0; t < 2; t++) {
        out[t] = s_tracks[t].p;
        if (out[t].down) n++;
    }
    return n;
}

bool aos_gesture_multitouch(void)
{
    return aos_hal_touch_multi();
}
