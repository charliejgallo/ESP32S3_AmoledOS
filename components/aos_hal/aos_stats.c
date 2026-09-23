/*
 * AmoledOS - System statistics and their history (see aos_hal.h).
 *
 * Driven by aos_stats_tick(), which the housekeeping task calls on every
 * pass (every 40-100 ms); the work happens once a second:
 *
 *   - the live values: the three temperatures, memory, and the CPU load per
 *     core over the last second, from the idle tasks' run-time counters
 *     (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS);
 *   - once a minute, a sample of each into a one-hour ring;
 *   - every five minutes of wall-clock time, the battery's percent into a
 *     24-hour ring, written to the card every half hour. On boot the file is
 *     read back and the slots the watch was off for are filled as "no
 *     sample", so the graph has a gap where it really has one.
 *
 * Everything lives in PSRAM and is a few KB. Readers copy the rings from
 * another task without a lock: a torn sample at worst draws one odd point.
 */
#include "aos_hal.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/temperature_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "aos_stats";

#define BATT_SLOT_S     300             /* five minutes */
#define SAVE_EVERY_S    1800
#define FILE_MAGIC      0x31545342u     /* "BST1" */

/* --- live values ---------------------------------------------------------- */

static temperature_sensor_handle_t s_tsens;
static bool     s_tsens_ok;
static aos_sys_stats_t s_now = {
    .chip_c = NAN, .pmu_c = NAN, .board_c = NAN, .cpu_load = { -1, -1 },
};
static uint32_t s_idle_prev[2];
static int64_t  s_idle_at_us;

/* --- the minute ring ------------------------------------------------------ */

AOS_BSS_PSRAM static int16_t s_min[AOS_HIST_COUNT][AOS_MIN_HIST_LEN];
static int      s_min_head;             /* next slot to write */
static int      s_cpu_acc[2], s_cpu_n;
static int64_t  s_min_at_us;

/* --- the battery ring ----------------------------------------------------- */

typedef struct {
    uint32_t magic;
    int64_t  last_epoch;                /* the slot of the newest sample */
    uint16_t head;                      /* next slot to write */
    uint8_t  pct[AOS_BATT_HIST_LEN];
    uint8_t  flags[AOS_BATT_HIST_LEN];
} batt_file_t;

AOS_BSS_PSRAM static batt_file_t s_batt;
static bool     s_batt_loaded;
static int64_t  s_saved_at_us;
static bool     s_batt_dirty;

static int16_t tenths(float c)
{
    return isnan(c) ? AOS_HIST_NONE : (int16_t)lrintf(c * 10.0f);
}

/* ------------------------------------------------------------------------ */

static void batt_path(char *out, size_t len)
{
    const char *root = aos_hal_path_sd_root();
    if (root && aos_hal_sd_present()) {
        snprintf(out, len, "%s/data", root);
        mkdir(out, 0777);
        snprintf(out, len, "%s/data/battery24.bin", root);
    } else {
        snprintf(out, len, "%s/battery24.bin", aos_hal_path_data());
    }
}

static void batt_push(uint8_t pct, uint8_t flags)
{
    s_batt.pct[s_batt.head] = pct;
    s_batt.flags[s_batt.head] = flags;
    s_batt.head = (uint16_t)((s_batt.head + 1) % AOS_BATT_HIST_LEN);
    s_batt_dirty = true;
}

static void batt_load(void)
{
    s_batt_loaded = true;
    memset(s_batt.pct, AOS_BATT_HIST_NONE, sizeof(s_batt.pct));
    memset(s_batt.flags, 0, sizeof(s_batt.flags));
    s_batt.magic = FILE_MAGIC;
    s_batt.head = 0;
    s_batt.last_epoch = 0;

    char path[96];
    batt_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    /* Straight into the PSRAM copy, not through the stack: this runs on the
     * housekeeping task, whose stack is small. A bad file is thrown away. */
    bool ok = fread(&s_batt, 1, sizeof(s_batt), f) == sizeof(s_batt) &&
              s_batt.magic == FILE_MAGIC && s_batt.head < AOS_BATT_HIST_LEN;
    fclose(f);
    if (ok) {
        ESP_LOGI(TAG, "battery history read back from %s", path);
    } else {
        memset(s_batt.pct, AOS_BATT_HIST_NONE, sizeof(s_batt.pct));
        memset(s_batt.flags, 0, sizeof(s_batt.flags));
        s_batt.magic = FILE_MAGIC;
        s_batt.head = 0;
        s_batt.last_epoch = 0;
    }
}

static void batt_save(void)
{
    char path[96];
    batt_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fwrite(&s_batt, 1, sizeof(s_batt), f);
    fclose(f);
    s_batt_dirty = false;
}

/* A clean restart (an OTA, Settings' restart button) saves what the last
 * half hour added, or the graph would lose it. A crash skips shutdown
 * handlers and loses up to those thirty minutes. */
static void batt_save_on_restart(void)
{
    if (s_batt_loaded && s_batt_dirty) {
        batt_save();
    }
}

/* One sample per five-minute slot of wall-clock time; the slots the watch
 * spent off become gaps. Without a valid clock there is no slot to put it
 * in, so nothing is recorded until the time is set. */
static void batt_tick(void)
{
    if (!aos_hal_time_is_valid()) {
        return;
    }
    time_t now = time(NULL);
    int64_t slot = (int64_t)now / BATT_SLOT_S * BATT_SLOT_S;
    if (s_batt.last_epoch == slot) {
        return;
    }
    if (s_batt.last_epoch > 0 && slot > s_batt.last_epoch) {
        int64_t missed = (slot - s_batt.last_epoch) / BATT_SLOT_S - 1;
        if (missed > AOS_BATT_HIST_LEN) missed = AOS_BATT_HIST_LEN;
        for (int64_t i = 0; i < missed; i++) {
            batt_push(AOS_BATT_HIST_NONE, 0);
        }
    }
    aos_battery_t b;
    if (aos_hal_battery_read(&b) && b.percent >= 0) {
        batt_push((uint8_t)b.percent, b.charging ? AOS_BATT_HIST_CHARGING : 0);
    } else {
        batt_push(AOS_BATT_HIST_NONE, 0);
    }
    s_batt.last_epoch = slot;
}

/* ------------------------------------------------------------------------ */

static void cpu_sample(int64_t now_us)
{
    uint32_t idle[2];
    for (int c = 0; c < 2; c++) {
        idle[c] = (uint32_t)ulTaskGetIdleRunTimeCounterForCore(c);
    }
    if (s_idle_at_us) {
        uint32_t span = (uint32_t)(now_us - s_idle_at_us);
        for (int c = 0; c < 2; c++) {
            uint32_t d = idle[c] - s_idle_prev[c];        /* wraps are fine */
            int load = span ? 100 - (int)((uint64_t)d * 100 / span) : -1;
            if (load < 0) load = 0;
            if (load > 100) load = 100;
            s_now.cpu_load[c] = load;
            s_cpu_acc[c] += load;
        }
        s_cpu_n++;
    }
    memcpy(s_idle_prev, idle, sizeof(idle));
    s_idle_at_us = now_us;
}

static void live_sample(bool pmu)
{
    if (s_tsens_ok) {
        float c;
        if (temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK) {
            s_now.chip_c = c;
        }
    }
    /* The PMU is an I2C read: every fifth second is plenty for temperatures
     * that move by tenths of a degree a minute. */
    if (pmu) {
        aos_battery_t b;
        if (aos_hal_battery_read(&b)) {
            s_now.pmu_c = b.temperature;
        }
        aos_power_info_t pi;
        if (aos_hal_power_info(&pi)) {
            s_now.board_c = pi.board_temperature;
        }
    }
    s_now.int_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_now.int_total   = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s_now.int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    s_now.psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s_now.psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    s_now.exec_free   = heap_caps_get_free_size(MALLOC_CAP_EXEC);
    s_now.exec_total  = heap_caps_get_total_size(MALLOC_CAP_EXEC);
    s_now.exec_largest = heap_caps_get_largest_free_block(MALLOC_CAP_EXEC);
}

static void minute_push(void)
{
    int h = s_min_head;
    s_min[AOS_HIST_CHIP_T][h]  = tenths(s_now.chip_c);
    s_min[AOS_HIST_PMU_T][h]   = tenths(s_now.pmu_c);
    s_min[AOS_HIST_BOARD_T][h] = tenths(s_now.board_c);
    s_min[AOS_HIST_INT_FREE_KB][h]   = (int16_t)(s_now.int_free / 1024);
    s_min[AOS_HIST_PSRAM_FREE_KB][h] = (int16_t)(s_now.psram_free / 1024 > 32767 ? 32767
                                                 : s_now.psram_free / 1024);
    for (int c = 0; c < 2; c++) {
        s_min[AOS_HIST_CPU0 + c][h] = s_cpu_n ? (int16_t)(s_cpu_acc[c] / s_cpu_n) : AOS_HIST_NONE;
        s_cpu_acc[c] = 0;
    }
    s_cpu_n = 0;
    s_min_head = (h + 1) % AOS_MIN_HIST_LEN;
}

void aos_stats_tick(void)
{
    static int64_t last_us;
    int64_t now_us = esp_timer_get_time();
    if (last_us && now_us - last_us < 1000000) {
        return;
    }
    last_us = now_us;

    if (!s_min_at_us) {
        /* First pass: the ring starts empty and the sensor comes up. */
        for (int k = 0; k < AOS_HIST_COUNT; k++) {
            for (int i = 0; i < AOS_MIN_HIST_LEN; i++) {
                s_min[k][i] = AOS_HIST_NONE;
            }
        }
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        s_tsens_ok = temperature_sensor_install(&cfg, &s_tsens) == ESP_OK &&
                     temperature_sensor_enable(s_tsens) == ESP_OK;
        s_min_at_us = now_us;
        s_saved_at_us = now_us;
        esp_register_shutdown_handler(batt_save_on_restart);
    }

    static unsigned seconds;
    cpu_sample(now_us);
    live_sample(seconds++ % 5 == 0);

    if (now_us - s_min_at_us >= 60 * 1000000LL) {
        s_min_at_us = now_us;
        minute_push();
    }

    if (!s_batt_loaded && aos_hal_time_is_valid()) {
        batt_load();
    }
    if (s_batt_loaded) {
        batt_tick();
        if (s_batt_dirty && now_us - s_saved_at_us >= SAVE_EVERY_S * 1000000LL) {
            s_saved_at_us = now_us;
            batt_save();
        }
    }
}

/* ------------------------------------------------------------------------ */

bool aos_hal_sys_stats(aos_sys_stats_t *out)
{
    if (!out) {
        return false;
    }
    *out = s_now;
    return s_min_at_us != 0;
}

int aos_hal_batt_history(uint8_t *pct, uint8_t *flags, int max)
{
    int n = max < AOS_BATT_HIST_LEN ? max : AOS_BATT_HIST_LEN;
    int start = (s_batt.head + AOS_BATT_HIST_LEN - n) % AOS_BATT_HIST_LEN;
    for (int i = 0; i < n; i++) {
        int k = (start + i) % AOS_BATT_HIST_LEN;
        if (pct)   pct[i] = s_batt_loaded ? s_batt.pct[k] : AOS_BATT_HIST_NONE;
        if (flags) flags[i] = s_batt_loaded ? s_batt.flags[k] : 0;
    }
    return n;
}

int aos_hal_minute_history(aos_hist_t which, int16_t *out, int max)
{
    if (which < 0 || which >= AOS_HIST_COUNT || !out) {
        return 0;
    }
    int n = max < AOS_MIN_HIST_LEN ? max : AOS_MIN_HIST_LEN;
    int start = (s_min_head + AOS_MIN_HIST_LEN - n) % AOS_MIN_HIST_LEN;
    for (int i = 0; i < n; i++) {
        out[i] = s_min_at_us ? s_min[which][(start + i) % AOS_MIN_HIST_LEN] : AOS_HIST_NONE;
    }
    return n;
}
