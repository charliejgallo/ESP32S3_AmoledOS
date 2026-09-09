/*
 * GEMAS - sound (see gm_snd.h)
 */
#include "gm_snd.h"
#include "aos_hal.h"

#include <string.h>

#define GM_SND_QUEUE    12

typedef struct {
    uint16_t freq;
    uint16_t ms;
    uint16_t gap;       /* silence after the note */
} gm_note_t;

static struct {
    gm_note_t q[GM_SND_QUEUE];
    int       head, count;
    int       priority;
    uint32_t  next_at;      /* uptime in ms, narrowed to 32 bits */
    bool      on;
    bool      started;
} s;

/* Pentatonic scale: any order of these notes sounds good, which is exactly
 * what is needed when the order is decided by a cascade. */
static const uint16_t s_scale[10] = {
    523, 587, 659, 784, 880, 1047, 1175, 1319, 1568, 1760
};

void gm_snd_init(void)
{
    memset(&s, 0, sizeof(s));
    s.on = true;
}

void gm_snd_enable(bool on)
{
    s.on = on;
    if (!on) {
        gm_snd_stop();
    }
}

bool gm_snd_enabled(void)
{
    return s.on;
}

void gm_snd_stop(void)
{
    s.count = 0;
    s.head = 0;
    s.priority = 0;
}

static void push(uint16_t freq, uint16_t ms, uint16_t gap)
{
    if (s.count >= GM_SND_QUEUE) {
        return;
    }
    int idx = (s.head + s.count) % GM_SND_QUEUE;
    s.q[idx].freq = freq;
    s.q[idx].ms   = ms;
    s.q[idx].gap  = gap;
    s.count++;
}

/* Priority: the important announcements (level, game over) override whatever
 * is playing; the per-line beeps do not override each other, they queue up. */
static bool take_over(int priority)
{
    if (!s.on) {
        return false;
    }
    if (priority > s.priority || s.count == 0) {
        s.count = 0;
        s.head = 0;
        s.priority = priority;
        return true;
    }
    return priority >= s.priority;
}

void gm_snd_play(gm_sfx_t sfx, int arg)
{
    if (!s.on) {
        return;
    }

    switch (sfx) {
    case GM_SFX_SELECT:
        if (!take_over(1)) return;
        push(880, 18, 0);
        break;

    case GM_SFX_SWAP:
        if (!take_over(1)) return;
        push(660, 16, 0);
        break;

    case GM_SFX_DENY:
        if (!take_over(2)) return;
        push(220, 45, 10);
        push(165, 60, 0);
        break;

    case GM_SFX_MATCH: {
        /* each link of the cascade, one note higher */
        if (!take_over(2)) return;
        int step = arg - 1;
        if (step < 0) step = 0;
        if (step > 9) step = 9;
        push(s_scale[step], 40, 6);
        break;
    }

    case GM_SFX_MATCH4:
        if (!take_over(3)) return;
        push(784, 40, 4);
        push(1047, 55, 0);
        break;

    case GM_SFX_MATCH5:
        if (!take_over(4)) return;
        push(659, 40, 3);
        push(880, 40, 3);
        push(1175, 40, 3);
        push(1568, 70, 0);
        break;

    case GM_SFX_FLAME:
        if (!take_over(3)) return;
        push(160, 60, 4);
        push(110, 90, 0);
        break;

    case GM_SFX_STAR:
        if (!take_over(3)) return;
        push(1568, 35, 2);
        push(1175, 35, 2);
        push(1760, 60, 0);
        break;

    case GM_SFX_HYPER:
        if (!take_over(4)) return;
        push(1319, 45, 3);
        push(1568, 45, 3);
        push(1760, 45, 3);
        push(2093, 90, 0);
        break;

    case GM_SFX_LEVELUP:
        if (!take_over(5)) return;
        push(523, 70, 8);
        push(659, 70, 8);
        push(784, 70, 8);
        push(1047, 140, 0);
        break;

    case GM_SFX_GAMEOVER:
        if (!take_over(6)) return;
        push(659, 120, 20);
        push(523, 120, 20);
        push(440, 150, 20);
        push(330, 320, 0);
        break;

    case GM_SFX_SHUFFLE:
        if (!take_over(3)) return;
        push(392, 50, 5);
        push(523, 50, 5);
        push(659, 50, 0);
        break;

    case GM_SFX_TICK:
        if (!take_over(2)) return;
        push(1200, 22, 0);
        break;

    case GM_SFX_START:
        if (!take_over(5)) return;
        push(784, 60, 6);
        push(1047, 60, 6);
        push(1319, 110, 0);
        break;
    }
}

void gm_snd_tick(void)
{
    if (!s.on || s.count == 0) {
        if (s.count == 0) {
            s.priority = 0;
        }
        return;
    }

    /* the subtraction is done in 32 bits on purpose: dividing a uint64_t drags
     * in __udivdi3, which the firmware does not lend to dynamic apps */
    uint32_t now = (uint32_t)aos_hal_uptime_ms();
    if (s.started && (int32_t)(now - s.next_at) < 0) {
        return;
    }

    gm_note_t n = s.q[s.head];
    s.head = (s.head + 1) % GM_SND_QUEUE;
    s.count--;

    aos_hal_beep(n.freq, n.ms);

    s.started = true;
    s.next_at = now + n.ms + n.gap;
    if (s.count == 0) {
        s.priority = 0;
    }
}
