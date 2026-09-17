/*
 * AmoledOS - Steps: the day, the history and the goal, on top of the raw
 * counter the board (or the simulator) keeps since boot.
 *
 * The raw counter is a number that only grows while the watch is on. This
 * turns it into what a watch shows: today's steps, kept across restarts,
 * cut at midnight by the clock, with the last seven days behind it and a
 * goal to measure against. Shared by both HALs: it only needs the raw
 * counter, the time, and the preferences.
 *
 * Persistence is NVS, written every five minutes when the count changed
 * and at every day change, so a restart costs at most five minutes of
 * walking. Before the clock has ever been set (no phone, no wifi) the day
 * is unknown: the steps still count, and the first valid time adopts the
 * day without discarding anything.
 */
#include "aos_hal.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define STEPS_DAYS          7
#define STEPS_SAVE_EVERY_MS (5u * 60u * 1000u)
#define STEPS_GOAL_DEFAULT  8000

static struct {
    bool     loaded;
    uint32_t raw_last;          /* the raw counter as last seen */
    uint32_t today;
    int32_t  day;               /* year * 1000 + yday of 'today'; 0 = unknown */
    uint32_t goal;
    uint32_t hist[STEPS_DAYS];  /* [0] = yesterday */
    uint64_t saved_at_ms;
    uint32_t saved_today;
    int32_t  saved_day;
} s;

static int32_t day_of(const struct tm *t)
{
    return (int32_t)(t->tm_year + 1900) * 1000 + t->tm_yday;
}

/* Whole days between two day keys, through mktime so year ends count. */
static int days_between(int32_t from, int32_t to)
{
    struct tm a = { .tm_year = from / 1000 - 1900, .tm_mday = 1 + from % 1000, .tm_hour = 12 };
    struct tm b = { .tm_year = to / 1000 - 1900,   .tm_mday = 1 + to % 1000,   .tm_hour = 12 };
    time_t ta = mktime(&a), tb = mktime(&b);
    if (ta == (time_t)-1 || tb == (time_t)-1) {
        return 1;
    }
    return (int)((tb - ta + 43200) / 86400);
}

static void save(void)
{
    char key[16];
    aos_hal_pref_set_i32("st_day", s.day);
    aos_hal_pref_set_i32("st_today", (int32_t)s.today);
    aos_hal_pref_set_i32("st_goal", (int32_t)s.goal);
    for (int i = 0; i < STEPS_DAYS; i++) {
        snprintf(key, sizeof(key), "st_h%d", i);
        aos_hal_pref_set_i32(key, (int32_t)s.hist[i]);
    }
    s.saved_at_ms  = aos_hal_uptime_ms();
    s.saved_today  = s.today;
    s.saved_day    = s.day;
}

static void load(void)
{
    int32_t v;
    s.goal = aos_hal_pref_get_i32("st_goal", &v) && v > 0 ? (uint32_t)v : STEPS_GOAL_DEFAULT;
    s.day  = aos_hal_pref_get_i32("st_day", &v) ? v : 0;
    s.today = aos_hal_pref_get_i32("st_today", &v) && v >= 0 ? (uint32_t)v : 0;
    for (int i = 0; i < STEPS_DAYS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "st_h%d", i);
        s.hist[i] = aos_hal_pref_get_i32(key, &v) && v >= 0 ? (uint32_t)v : 0;
    }
    s.raw_last    = aos_hal_imu_steps();
    s.saved_today = s.today;
    s.saved_day   = s.day;
    s.saved_at_ms = aos_hal_uptime_ms();
    s.loaded      = true;
}

/* A new day: today goes into the history, missed days are zeros. */
static void roll_to(int32_t new_day)
{
    int gap = s.day ? days_between(s.day, new_day) : 1;
    if (gap < 1) {
        gap = 1;
    }
    for (int d = 0; d < gap && d < STEPS_DAYS; d++) {
        memmove(&s.hist[1], &s.hist[0], (STEPS_DAYS - 1) * sizeof s.hist[0]);
        s.hist[0] = d == 0 ? s.today : 0;
    }
    s.today = 0;
    s.day   = new_day;
}

void aos_steps_tick(void)
{
    if (!s.loaded) {
        load();
    }
    /* Fold the raw counter's growth into today; a reset of the raw counter
     * (the Activity app's button used to do that) just moves the base. */
    uint32_t raw = aos_hal_imu_steps();
    if (raw > s.raw_last) {
        s.today += raw - s.raw_last;
    }
    s.raw_last = raw;

    if (aos_hal_time_is_valid()) {
        struct tm now;
        aos_hal_time_now(&now);
        int32_t today_key = day_of(&now);
        if (s.day == 0) {
            s.day = today_key;          /* first valid clock: adopt the day */
        } else if (today_key != s.day) {
            if (today_key > s.day) {
                roll_to(today_key);
                save();
            } else {
                s.day = today_key;      /* the clock went backwards: keep counting */
            }
        }
    }

    bool changed = s.today != s.saved_today || s.day != s.saved_day;
    if (changed && aos_hal_uptime_ms() - s.saved_at_ms >= STEPS_SAVE_EVERY_MS) {
        save();
    }
}

bool aos_hal_steps_get(aos_steps_info_t *out)
{
    if (!out) {
        return false;
    }
    if (!s.loaded) {
        aos_steps_tick();
    }
    out->today     = s.today + (aos_hal_imu_steps() - s.raw_last);
    out->goal      = s.goal;
    out->day_known = s.day != 0;
    memcpy(out->history, s.hist, sizeof out->history);
    return true;
}

void aos_hal_steps_set_goal(uint32_t goal)
{
    if (goal < 1000) {
        goal = 1000;
    }
    if (goal > 50000) {
        goal = 50000;
    }
    s.goal = goal;
    aos_hal_pref_set_i32("st_goal", (int32_t)goal);
}

void aos_hal_steps_reset_today(void)
{
    aos_steps_tick();
    s.today = 0;
    save();
}
