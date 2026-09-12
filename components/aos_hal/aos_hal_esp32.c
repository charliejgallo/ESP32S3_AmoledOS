/*
 * AmoledOS - HAL on the Waveshare ESP32-S3-Touch-AMOLED-1.8 board.
 *
 * Joins the official BSP (display, touch, I2C, audio, SD, SPIFFS) with
 * aos_board's own drivers (PMU, RTC, IMU) and exposes it all through the
 * single interface the UI and the apps consume.
 */
#include "aos_hal.h"
#include "aos_ble.h"
#include "aos_board.h"
#include "axp2101.h"

#include "bsp/esp-bsp.h"
#include "esp_vfs_fat.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "soc/rtc.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_touch.h"
#include "bsp/display.h"
#include "bsp/touch.h"

#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/stat.h>

#define FIRMWARE_VERSION        "0.1.0-dev"
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define BUTTON_LONG_MS          800
#define NVS_NAMESPACE           "amoledos"
/* Idle timeouts. With always-on, the screen never switches itself off unless
 * the battery is very low. */
#define AOD_TIMEOUT_MS          60000       /* active -> dimmed           */
#define OFF_TIMEOUT_MS          300000      /* dimmed -> off              */
#define OFF_NO_AOD_MS           30000       /* active -> off, without AOD */
#define AOD_LOW_BATTERY_PCT     15

/* --- Battery policy -----------------------------------------------------------
 * The cell Waveshare ships is a 3.7 V 300 mAh pouch. The charger's factory
 * programme (300 mA, 4.2 V, terminate at 125 mA) is 1 C into it and stops
 * early; what is below is 0.5 C and a proper termination, which is what the
 * cell's own datasheet asks for. See docs/POWER.md.
 * -------------------------------------------------------------------------- */
#define AOS_BATTERY_MAH             300
#define AOS_CHARGE_MA_CARE          150     /* 0.5 C                              */
#define AOS_CHARGE_MA_FULL          300     /* the chip's own default, 1 C        */
#define AOS_CHARGE_MV_CARE          4100    /* ~10% less capacity, ~2x the cycles */
#define AOS_CHARGE_MV_FULL          4200
#define AOS_PRECHARGE_MA            50
#define AOS_TERMINATION_MA          25      /* C/12                               */
#define AOS_LOW_BATTERY_WARN_PCT    10      /* the PMU raises an IRQ here         */
#define AOS_LOW_BATTERY_OFF_PCT     3       /* and here; we power off cleanly     */
#define AOS_POWEROFF_MV             2900    /* VOFF: the PMU's own cut, was 2.6 V */
#define AOS_CRITICAL_VBAT           3.30f   /* software backstop, sustained 15 s  */
#define AOS_LOW_BATTERY_SAVING_PCT  20      /* power saving switches itself on    */
#define AOS_PANEL_WAKE_MS           120     /* sleep-out to display-on            */
#define AOS_DFS_MIN_MHZ             80
#define AOS_DFS_MAX_MHZ             240

/* Stack of the housekeeping task. Measured on the board: while running it has
 * ~2.0 KB of the 4 to spare, so its peak is about 2 KB. Left at 3 KB, which
 * keeps 1 KB of headroom over what was measured. Shrinking a stack is a better
 * deal than sending it to PSRAM: it frees internal RAM and pays no cache. */
#define HK_STACK            3072


/* --------------------------------------------------------------------------
 * QSPI clock of the panel
 *
 * Measured: pushing one screen to the panel is 16.5 ms, which is half of a
 * 33 ms frame. It follows from CO5300_PANEL_IO_QSPI_CONFIG setting pclk_hz to
 * 40 MHz: over four lines that is 20 MB/s, and a screen is 322 KB.
 *
 * The macro lives in a managed component and the BSP uses it inside
 * bsp_display_new(), so there is no parameter to touch. Rather than forking
 * the BSP, panel creation is intercepted with -Wl,--wrap, the same device
 * aos_dynapp already uses for esp_elf_malloc.
 *
 * Only the configuration carrying exactly the macro's 40 MHz is overridden:
 * that is the panel's signature and it avoids touching any other SPI device
 * that may turn up later.
 *
 * 2026-09-04: LOWERED FROM 80 TO 40 MHz, and this is what the comment that
 * used to be here said: "if the image comes out with garbage at 80 MHz, lower
 * AOS_LCD_PCLK_HZ". It did come out with garbage, but that took a while to
 * see because until now every app had a black background, and a corrupt pixel
 * on black is indistinguishable from a pixel that is off. With Truco's green
 * baize they showed up at once: isolated dark dots ALONG THE PATH of the
 * moving cards, that is, exactly where the bus is pushing the most bytes, and
 * there they stay because LVGL considers that area drawn and does not touch
 * it again.
 *
 * What pointed at the bus and not at the drawing:
 *   - It NEVER happens in the simulator, not even in partial-flush mode, which
 *     is the only one where a missing repaint stays stuck. Measured by counting
 *     pixels of the capture, not by looking at it: zero anomalies on the baize.
 *   - More of them appear the more cards move at once.
 *   - They do not depend on what is drawn, but on how much is transferred.
 *
 * The cost of going back to 40 MHz is measured and acceptable: pushing a whole
 * screen goes from 8 to 16.5 ms, and the startup benchmark had already
 * concluded that the QSPI clock was NOT the frame's bottleneck (see
 * DECISIONES.md, "El reloj del QSPI no era el cuello"). In other words, image
 * integrity was being paid for time that was not being used.
 *
 * If speed is ever to be recovered, it has to be RAISED IN SMALL STEPS while
 * watching a light-coloured background -not a black one-, which is where it
 * shows. */
#define AOS_LCD_PCLK_DEFAULT_HZ     (40 * 1000 * 1000)
#define AOS_LCD_PCLK_HZ             (40 * 1000 * 1000)

esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                          const esp_lcd_panel_io_spi_config_t *io_config,
                                          esp_lcd_panel_io_handle_t *ret_io);

esp_err_t __wrap_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                          const esp_lcd_panel_io_spi_config_t *io_config,
                                          esp_lcd_panel_io_handle_t *ret_io)
{
    if (io_config && io_config->pclk_hz == AOS_LCD_PCLK_DEFAULT_HZ) {
        esp_lcd_panel_io_spi_config_t cfg = *io_config;
        cfg.pclk_hz = AOS_LCD_PCLK_HZ;
        ESP_EARLY_LOGW("aos_hal", "panel qspi: %d -> %d MHz",
                       (int)(io_config->pclk_hz / 1000000),
                       (int)(cfg.pclk_hz / 1000000));
        return __real_esp_lcd_new_panel_io_spi(bus, &cfg, ret_io);
    }
    ESP_EARLY_LOGW("aos_hal", "qspi panel left alone: %d MHz",
                   io_config ? (int)(io_config->pclk_hz / 1000000) : -1);
    return __real_esp_lcd_new_panel_io_spi(bus, io_config, ret_io);
}

static const char *TAG = "aos_hal";

/* The I2S channels, caught on their way out of the BSP. esp_codec_dev enables
 * a channel on open and NEVER disables it on close (close only switches the
 * codec chip off), and an enabled channel holds an APB_FREQ_MAX pm lock,
 * which forbids light sleep for good. With the handles in hand the policy
 * can disable both whenever no audio is running; the codec enables them
 * again on its next open. */
static i2s_chan_handle_t s_i2s_tx, s_i2s_rx;
static bool              s_i2s_idle;

esp_err_t __real_i2s_new_channel(const i2s_chan_config_t *chan_cfg,
                                 i2s_chan_handle_t *tx, i2s_chan_handle_t *rx);
esp_err_t __wrap_i2s_new_channel(const i2s_chan_config_t *chan_cfg,
                                 i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    esp_err_t ret = __real_i2s_new_channel(chan_cfg, tx, rx);
    if (ret == ESP_OK) {
        if (tx) s_i2s_tx = *tx;
        if (rx) s_i2s_rx = *rx;
    }
    return ret;
}

static lv_display_t *s_display;
static int           s_brightness = 80;
static int           s_volume     = 60;
static int64_t       s_last_activity_us;

static aos_display_state_t s_display_state = AOS_DISPLAY_ACTIVE;
static bool          s_aod_enabled = true;
static int           s_aod_brightness = 10;
static void        (*s_display_cb)(aos_display_state_t state);
static char          s_board_name[48] = "desconocida";

/* Panel handles: display_start() gets them from the BSP and keeps them so the
 * driver IC can be put to sleep and woken. */
static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static bool                      s_panel_asleep;
static bool                      s_panel_sleep_enabled = false;  /* off: SLPOUT flashes, see POWER.md 6b */
static esp_err_t                 s_panel_last_err;   /* of the last wake sequence */

/* Power policy */
static bool                 s_power_saving = true;      /* preference          */
static bool                 s_battery_care = true;      /* preference          */
static bool                 s_low_battery_saving;       /* forced under 20%    */
static bool                 s_light_sleep_enabled = true;   /* preference       */
static volatile bool        s_speaker_open;             /* held by tone_task   */
static volatile bool        s_mic_holds_codec;          /* the capture took the codec */
static bool                 s_light_sleep_on;           /* what esp_pm has now */
static esp_pm_lock_handle_t s_pm_max_lock;
static bool                 s_pm_max_held;
static int                  s_wifi_ps = -1;
static void               (*s_power_cb)(aos_power_event_t event, int percent);
static int                  s_gyro_users;

/* Battery bookkeeping */
static bool     s_usb_last = true;
static int64_t  s_unplug_us;
static int      s_unplug_pct = -1;
static float    s_drain_pct_h = NAN;
static float    s_hours_left  = NAN;
static uint32_t s_battery_minutes;      /* lifetime, NVS "bat_min"   */
static uint32_t s_charge_cycles;        /* lifetime, NVS "chg_cyc"   */
static uint32_t s_unsaved_minutes;
static bool     s_low_warned;
static bool     s_charge_counted;
static int      s_critical_strikes;
static bool     s_shutting_down;

static void pm_policy_apply(void);
static bool panel_sleep(bool sleep);
static void lvgl_timers_idle(bool idle);
static bool power_saving_active(void);
static int  cpu_mhz_now(void);
static TaskHandle_t s_player_task;
static TaskHandle_t s_mic_task;

static esp_codec_dev_handle_t s_speaker;
static esp_codec_dev_handle_t s_mic;

static void (*s_button_cb)(aos_button_t button, aos_button_action_t action);
static int64_t s_button_down_us;

static aos_net_state_t s_net_state = AOS_NET_OFF;
static char            s_net_ssid[33];
static esp_netif_t    *s_netif_ap;
static bool            s_ap_active;
static char            s_ap_ssid[33];
static char            s_ap_pass[65];
static char            s_ap_ip[16] = "192.168.4.1";
static char            s_net_ip[16] = "0.0.0.0";

/* -------------------------------------------------------------------------- */
/* Preferences (NVS)                                                           */
/* -------------------------------------------------------------------------- */

bool aos_hal_pref_get_i32(const char *key, int32_t *out)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    bool ok = nvs_get_i32(handle, key, out) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool aos_hal_pref_set_i32(const char *key, int32_t value)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_i32(handle, key, value) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool aos_hal_pref_get_str(const char *key, char *out, size_t out_len)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t len = out_len;
    bool ok = nvs_get_str(handle, key, out, &len) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool aos_hal_pref_set_str(const char *key, const char *value)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_str(handle, key, value) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool aos_hal_pref_erase(const char *key)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    bool ok = nvs_erase_key(handle, key) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

/* -------------------------------------------------------------------------- */
/* Display and power                                                           */
/* -------------------------------------------------------------------------- */

bool aos_hal_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void aos_hal_unlock(void)
{
    bsp_display_unlock();
}

int aos_hal_brightness_get(void)
{
    return s_brightness;
}

void aos_hal_brightness_set(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_brightness = percent;
    bsp_display_brightness_set(percent);
    aos_hal_pref_set_i32("bright", percent);
}




aos_display_state_t aos_hal_display_state(void)
{
    return s_display_state;
}

void aos_hal_display_set_state(aos_display_state_t state)
{
    if (state == s_display_state) {
        return;
    }
    s_display_state = state;

    switch (state) {
    case AOS_DISPLAY_ACTIVE:
        panel_sleep(false);
        bsp_display_brightness_set(s_brightness);
        break;
    case AOS_DISPLAY_AOD:
        panel_sleep(false);
        bsp_display_brightness_set(s_aod_brightness);
        break;
    case AOS_DISPLAY_OFF:
        bsp_display_brightness_set(0);
        panel_sleep(true);
        break;
    }
    lvgl_timers_idle(state == AOS_DISPLAY_OFF);
    pm_policy_apply();

    ESP_LOGI(TAG, "display -> %s",
             state == AOS_DISPLAY_ACTIVE ? "active" :
             state == AOS_DISPLAY_AOD    ? "dimmed" : "off");

    if (s_display_cb) {
        s_display_cb(state);
    }
}

void aos_hal_set_display_state_cb(void (*cb)(aos_display_state_t state))
{
    s_display_cb = cb;
}

void aos_hal_aod_enable(bool enable)
{
    s_aod_enabled = enable;
    aos_hal_pref_set_i32("aod", enable ? 1 : 0);
    if (!enable && s_display_state == AOS_DISPLAY_AOD) {
        aos_hal_display_set_state(AOS_DISPLAY_OFF);
    }
}

bool aos_hal_aod_enabled(void)
{
    return s_aod_enabled;
}

void aos_hal_aod_brightness_set(int percent)
{
    if (percent < 1)  percent = 1;
    if (percent > 50) percent = 50;
    s_aod_brightness = percent;
    aos_hal_pref_set_i32("aod_bright", percent);
    if (s_display_state == AOS_DISPLAY_AOD) {
        bsp_display_brightness_set(s_aod_brightness);
    }
}

int aos_hal_aod_brightness_get(void)
{
    return s_aod_brightness;
}

void aos_hal_display_on(bool on)
{
    aos_hal_display_set_state(on ? AOS_DISPLAY_ACTIVE : AOS_DISPLAY_OFF);
}

bool aos_hal_display_is_on(void)
{
    return s_display_state != AOS_DISPLAY_OFF;
}

void aos_hal_activity(void)
{
    s_last_activity_us = esp_timer_get_time();
    aos_hal_display_set_state(AOS_DISPLAY_ACTIVE);
}

void aos_hal_sleep(void)
{
    aos_hal_display_on(false);
}

static void battery_stats_save(void);

void aos_hal_shutdown(void)
{
    ESP_LOGI(TAG, "powering off through the PMU");
    battery_stats_save();
    aos_board_pmu_shutdown();
}

void aos_hal_reboot(void)
{
    esp_restart();
}

void aos_hal_set_button_cb(void (*cb)(aos_button_t button, aos_button_action_t action))
{
    s_button_cb = cb;
}

/* Polling of the BOOT button (GPIO0, active low). Called from the background
 * task every 40 ms, which is already enough as a debounce. */
static void button_poll(void)
{
    bool down = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
    int64_t now = esp_timer_get_time();

    if (down && s_button_down_us == 0) {
        s_button_down_us = now;
        aos_hal_activity();
        if (s_button_cb) {
            s_button_cb(AOS_BUTTON_BOOT, AOS_BUTTON_PRESS);
        }
    } else if (!down && s_button_down_us != 0) {
        int64_t held_ms = (now - s_button_down_us) / 1000;
        s_button_down_us = 0;
        /* The release event is always emitted, even if the pulse was very
         * short: if we announced a press, the listener needs the complete pair
         * or it is left with the button held down forever. */
        if (s_button_cb) {
            s_button_cb(AOS_BUTTON_BOOT,
                        held_ms >= BUTTON_LONG_MS ? AOS_BUTTON_LONG : AOS_BUTTON_CLICK);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Battery and IMU                                                             */
/* -------------------------------------------------------------------------- */

bool aos_hal_battery_read(aos_battery_t *out)
{
    if (!out) {
        return false;
    }
    aos_pmu_state_t pmu;
    if (!aos_board_pmu_read(&pmu) || !pmu.valid) {
        out->percent = -1;
        return false;
    }
    out->percent     = pmu.percent;
    out->voltage     = pmu.vbat;
    /* The AXP2101 does not measure battery current; we leave it at NAN rather
     * than invent a number. */
    out->current     = NAN;
    out->temperature = pmu.temperature;
    out->charging    = pmu.charging;
    out->usb_present = pmu.usb_present;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Power API                                                                   */
/* -------------------------------------------------------------------------- */

bool aos_hal_power_info(aos_power_info_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    aos_pmu_state_t pmu;
    if (!aos_board_pmu_read(&pmu) || !pmu.valid) {
        return false;
    }
    out->charge_state      = (aos_charge_state_t)pmu.charge_state;
    out->vbus              = pmu.vbus;
    out->vsys              = pmu.vsys;
    out->board_temperature = pmu.board_temperature;
    out->battery_present   = pmu.battery_present;

    aos_pmu_charger_t chg;
    if (aos_board_pmu_charger_get(&chg)) {
        out->charge_ma        = chg.charge_ma;
        out->charge_target_mv = chg.target_mv;
        out->warn_pct         = chg.warn_pct;
        out->shutdown_pct     = chg.shutdown_pct;
        out->poweroff_mv      = chg.poweroff_mv;
    }
    out->drain_pct_per_hour    = s_drain_pct_h;
    out->hours_left            = s_hours_left;
    out->on_battery_s          = s_unplug_us ? (uint32_t)((esp_timer_get_time() - s_unplug_us) / 1000000) : 0;
    out->battery_minutes_total = s_battery_minutes;
    out->charge_cycles         = s_charge_cycles;
    out->power_on_reason       = aos_board_pmu_power_on_reason();
    out->power_off_reason      = aos_board_pmu_power_off_reason();
    out->cpu_mhz               = cpu_mhz_now();
    out->panel_asleep          = s_panel_asleep;
    out->power_saving_active   = power_saving_active();
    out->light_sleep           = s_light_sleep_on;
    return true;
}

void aos_hal_set_power_event_cb(void (*cb)(aos_power_event_t event, int percent))
{
    s_power_cb = cb;
}

void aos_hal_power_saving_enable(bool on)
{
    s_power_saving = on;
    aos_hal_pref_set_i32("pwr_save", on ? 1 : 0);
    pm_policy_apply();
}

bool aos_hal_power_saving_enabled(void)
{
    return s_power_saving;
}

void aos_hal_battery_care_enable(bool on)
{
    s_battery_care = on;
    aos_hal_pref_set_i32("batt_care", on ? 1 : 0);
    aos_board_pmu_charge_target_set(on ? AOS_CHARGE_MV_CARE : AOS_CHARGE_MV_FULL);
    aos_board_pmu_charge_current_set(on ? AOS_CHARGE_MA_CARE : AOS_CHARGE_MA_FULL);
    ESP_LOGI(TAG, "battery care %s: charging to %d mV at %d mA", on ? "on" : "off",
             on ? AOS_CHARGE_MV_CARE : AOS_CHARGE_MV_FULL,
             on ? AOS_CHARGE_MA_CARE : AOS_CHARGE_MA_FULL);
}

bool aos_hal_battery_care_enabled(void)
{
    return s_battery_care;
}

void aos_hal_panel_sleep_enable(bool on)
{
    s_panel_sleep_enabled = on;
    aos_hal_pref_set_i32("panel_slp", on ? 1 : 0);
    if (!on) {
        panel_sleep(false);
    } else if (s_display_state == AOS_DISPLAY_OFF) {
        panel_sleep(true);
    }
}

bool aos_hal_panel_sleep_enabled(void)
{
    return s_panel_sleep_enabled;
}

void aos_hal_light_sleep_enable(bool on)
{
    s_light_sleep_enabled = on;
    aos_hal_pref_set_i32("light_slp", on ? 1 : 0);
    pm_policy_apply();
}

bool aos_hal_light_sleep_enabled(void)
{
    return s_light_sleep_enabled;
}

void aos_hal_imu_gyro_request(bool on)
{
    s_gyro_users += on ? 1 : -1;
    if (s_gyro_users < 0) {
        s_gyro_users = 0;
    }
    aos_board_imu_gyro_enable(s_gyro_users > 0);
}

int aos_hal_pmu_rail_count(void)                       { return aos_board_pmu_rail_count(); }
bool aos_hal_pmu_rail_get(int idx, const char **name, bool *on, int *mv)
{
    return aos_board_pmu_rail_get(idx, name, on, mv);
}
int aos_hal_pmu_rail_find(const char *name)             { return aos_board_pmu_rail_find(name); }
bool aos_hal_pmu_rail_set(int idx, bool on)             { return aos_board_pmu_rail_set(idx, on) == ESP_OK; }
int aos_hal_pmu_register_read(int reg)                  { return aos_board_pmu_register_read((uint8_t)reg); }
bool aos_hal_pmu_register_write(int reg, int value)
{
    return aos_board_pmu_register_write((uint8_t)reg, (uint8_t)value) == ESP_OK;
}
float aos_hal_pmu_ts_voltage(void)                      { return aos_board_pmu_ts_voltage(); }
int aos_hal_probe_devices(char *out, size_t len)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    static const struct { const char *name; uint8_t addr; } devs[] = {
        { "pmu", 0x34 }, { "expander", 0x20 }, { "touch", 0x15 }, { "touch_v1", 0x38 },
        { "rtc", 0x51 }, { "imu", 0x6B }, { "imu_low", 0x6A }, { "codec", 0x18 }, { "codec_alt", 0x30 },
    };
    int n = snprintf(out, len, "{");
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]) && n < (int)len - 32; i++) {
        bool ok = bus && i2c_master_probe(bus, devs[i].addr, 50) == ESP_OK;
        n += snprintf(out + n, len - n, "%s\"%s\":%s", i ? "," : "", devs[i].name, ok ? "true" : "false");
    }
    aos_imu_t imu;
    bool have = aos_hal_imu_read(&imu);
    n += snprintf(out + n, len - n, ",\"imu_read\":%s,\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f",
                  have ? "true" : "false", have ? imu.ax : 0.0f, have ? imu.ay : 0.0f, have ? imu.az : 0.0f);

    /* The panel: its tearing-effect line (GPIO13, enabled by the BSP's init
     * with 0x35) toggles at the refresh rate only while the driver IC is
     * powered and awake. Woken first, because sleep-in stops TE too. */
    aos_hal_activity();
    vTaskDelay(pdMS_TO_TICKS(250));
    gpio_config_t te = {
        .pin_bit_mask = 1ULL << GPIO_NUM_13, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&te);
    gpio_sleep_sel_dis(GPIO_NUM_13);
    int edges = 0, last = gpio_get_level(GPIO_NUM_13), highs = 0, samples = 0;
    int64_t until = esp_timer_get_time() + 100000;
    while (esp_timer_get_time() < until) {
        int now = gpio_get_level(GPIO_NUM_13);
        highs += now; samples++;
        if (now != last) { edges++; last = now; }
    }
    n += snprintf(out + n, len - n, ",\"te_edges_100ms\":%d,\"te_high_pct\":%d,"
                  "\"panel_asleep\":%s,\"panel_wake_err\":\"%s\",\"display\":%d",
                  edges, samples ? 100 * highs / samples : -1,
                  s_panel_asleep ? "true" : "false", esp_err_to_name(s_panel_last_err),
                  (int)s_display_state);

    /* The microphone: open, let the capture task fill, take the RMS. A dead
     * analogue side gives a flat zero; a live one gives room noise. */
    float rms = -1.0f;
    if (aos_hal_mic_open(16000)) {
        vTaskDelay(pdMS_TO_TICKS(400));
        static int16_t pcm[1024];
        int got = aos_hal_mic_read(pcm, 1024);
        if (got > 0) {
            double acc = 0;
            for (int i = 0; i < got; i++) acc += (double)pcm[i] * pcm[i];
            rms = (float)sqrt(acc / got);
        }
        aos_hal_mic_close();
    }
    n += snprintf(out + n, len - n, ",\"mic_rms\":%.1f}", rms);
    return n;
}

/* The same dump into a buffer, for the web portal: with light sleep on the
 * USB console drops, so the log is not where the numbers can be read. */
int aos_hal_pm_dump_text(char *out, size_t len)
{
    if (!out || len < 64) {
        return 0;
    }
    int n = snprintf(out, len, "cpu %d MHz, max lock %s, saving %s, light sleep %s, display %d, audio %s\n",
                     cpu_mhz_now(), s_pm_max_held ? "held" : "released",
                     power_saving_active() ? "on" : "off", s_light_sleep_on ? "on" : "off",
                     (int)s_display_state,
                     (s_player_task || s_mic_task || s_speaker_open || s_mic_holds_codec) ? "yes" : "no");
    FILE *f = fmemopen(out + n, len - n - 1, "w");
    if (f) {
        esp_pm_dump_locks(f);
        fclose(f);
        out[len - 1] = '\0';
        n += (int)strlen(out + n);
    }
    return n;
}

const char *aos_hal_boot_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB:      return "usb";
    default:               return "other";
    }
}

void aos_hal_panel_hw_reset(void)
{
    aos_board_panel_hw_reset();
}

void aos_hal_pm_dump_locks(void)
{
    ESP_LOGI(TAG, "pm: cpu %d MHz, our max lock %s, saving %s, light sleep %s, display %d, player %p mic %p",
             cpu_mhz_now(), s_pm_max_held ? "HELD" : "released",
             power_saving_active() ? "on" : "off", s_light_sleep_on ? "on" : "off",
             (int)s_display_state, (void *)s_player_task, (void *)s_mic_task);
    esp_pm_dump_locks(stdout);
}

bool aos_hal_imu_read(aos_imu_t *out)
{
    if (!out) {
        return false;
    }
    aos_imu_sample_t sample;
    if (!aos_board_imu_read(&sample) || !sample.valid) {
        return false;
    }
    out->ax = sample.ax; out->ay = sample.ay; out->az = sample.az;
    out->gx = sample.gx; out->gy = sample.gy; out->gz = sample.gz;
    out->temperature = sample.temperature;
    return true;
}

aos_orientation_t aos_hal_imu_orientation(void)
{
    return (aos_orientation_t)aos_board_imu_orientation();
}

uint32_t aos_hal_imu_steps(void)
{
    return aos_board_imu_steps();
}

void aos_hal_imu_steps_reset(void)
{
    aos_board_imu_steps_reset();
}

/* -------------------------------------------------------------------------- */
/* Time                                                                        */
/* -------------------------------------------------------------------------- */

void aos_hal_time_now(struct tm *out)
{
    time_t now = time(NULL);
    localtime_r(&now, out);
}

bool aos_hal_time_set(const struct tm *value)
{
    if (!value) {
        return false;
    }
    struct tm copy = *value;
    time_t epoch = mktime(&copy);
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    aos_board_rtc_set(value);
    aos_hal_pref_set_i32("time_ok", 1);
    return true;
}

bool aos_hal_time_is_valid(void)
{
    int32_t flag = 0;
    return aos_hal_pref_get_i32("time_ok", &flag) && flag == 1;
}

void aos_hal_timezone_set(const char *tz)
{
    if (!tz) {
        return;
    }
    setenv("TZ", tz, 1);
    tzset();
    aos_hal_pref_set_str("tz", tz);
}

const char *aos_hal_timezone_get(void)
{
    static char tz[40] = "ART3";
    if (!aos_hal_pref_get_str("tz", tz, sizeof(tz))) {
        strcpy(tz, "ART3");
    }
    return tz;
}

bool aos_hal_rtc_alarm_set(const struct tm *when)
{
    return when ? aos_board_rtc_alarm_set(when->tm_hour, when->tm_min) : false;
}

void aos_hal_rtc_alarm_clear(void)
{
    aos_board_rtc_alarm_clear();
}

/* -------------------------------------------------------------------------- */
/* Storage                                                                     */
/* -------------------------------------------------------------------------- */

static bool s_sd_mounted;

const char *aos_hal_path_apps(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT "/apps" : BSP_SPIFFS_MOUNT_POINT "/apps";
}

const char *aos_hal_path_photos(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT "/photos" : BSP_SPIFFS_MOUNT_POINT "/photos";
}

const char *aos_hal_path_music(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT "/music" : BSP_SPIFFS_MOUNT_POINT "/music";
}

const char *aos_hal_path_recordings(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT "/recordings"
                        : BSP_SPIFFS_MOUNT_POINT "/recordings";
}

const char *aos_hal_path_data(void)
{
    return BSP_SPIFFS_MOUNT_POINT "/data";
}

/* No SPIFFS fallback on purpose: see the comment in aos_hal.h. With the card
 * out, this points at a directory that does not exist, opendir() fails and the
 * system stays in Spanish, which is what the source says. */
const char *aos_hal_path_lang(void)
{
    return BSP_SD_MOUNT_POINT "/lang";
}

/* Network surveys DO go to the card and not to SPIFFS: they are files that
 * grow, that pile up and that you want to be able to take away with you. */
const char *aos_hal_path_scans(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT "/redes"
                        : BSP_SPIFFS_MOUNT_POINT "/redes";
}

bool aos_hal_sd_present(void)
{
    return s_sd_mounted;
}

bool aos_hal_sd_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    /* FatFs walks the FAT to count free clusters: on a 32 GB card that is a
     * few hundred ms the first time and cached by FatFs afterwards (FSINFO).
     * Called from the portal's status handler, never from the UI task. */
    if (!s_sd_mounted) {
        return false;
    }
    return esp_vfs_fat_info(BSP_SD_MOUNT_POINT, total_bytes, free_bytes) == ESP_OK;
}

const char *aos_hal_path_sd_root(void)
{
    return s_sd_mounted ? BSP_SD_MOUNT_POINT : NULL;
}

/* -------------------------------------------------------------------------- */
/* Audio                                                                       */
/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Tones
 *
 * aos_hal_beep() enqueues and returns straight away. It used to write the tone
 * to the codec directly, so a 60 ms beep froze its caller for 60 ms, and the
 * caller is always an LVGL timer: two notes in a row and the game stutters.
 * Now this task plays them.
 *
 * The codec is left open between notes and closed after half a second of
 * silence: opening and closing it on every note adds a click and makes it
 * impossible to play a melody.
 * -------------------------------------------------------------------------- */

#define TONE_RATE       16000
#define TONE_QUEUE_LEN  16

/* --------------------------------------------------------------------------
 * Arbitration of the shared codec
 *
 * The speaker and the microphone are the SAME ES8311 hanging off the SAME pair
 * of I2S channels, and the BSP creates the speaker as
 * ESP_CODEC_DEV_TYPE_IN_OUT (esp32_s3_touch_amoled_1_8.c:
 * bsp_audio_codec_speaker_init). With that type, opening or closing the
 * speaker goes through this branch of esp_codec_dev:
 *
 *     if (dev_type == ESP_CODEC_DEV_TYPE_IN_OUT) {
 *         _i2s_drv_enable(i2s_data, true,  enable);   // TX
 *         _i2s_drv_enable(i2s_data, false, enable);   // RX  <-- the microphone
 *     }
 *
 * that is, it enables and disables BOTH channels, without the protection the
 * one-way path does have ("when RX is working TX disable should be blocked",
 * audio_codec_data_i2s.c). Translated: a beep in the middle of a recording
 * tears the input channel away from the microphone and the read fails.
 *
 * So, while the microphone is capturing, the speaker is left alone: the tone
 * task releases the codec and drops the notes. While recording nothing is lost
 * —a beep would end up inside the file—, and with the raw microphone open it
 * is the price of listening: a tuner cannot give the reference tone while it
 * listens.
 *
 * The arbitration is deliberately binary: microphone against speaker. It works
 * for both consumers of the capture (the recorder and aos_hal_mic_open), so
 * there is no need for an owner with more states than the hardware has.
 * -------------------------------------------------------------------------- */


typedef struct {
    uint16_t freq;
    uint16_t ms;
} tone_note_t;

static QueueHandle_t s_tone_queue;

/* RAM audit (E1b): with AOS_AUDIT_PSRAM_STACKS the player, mic and tone tasks
 * get their stacks in PSRAM (they never touch flash: SD card only). A task
 * created with xTaskCreateWithCaps must exit through vTaskDeleteWithCaps. */
#ifdef AOS_AUDIT_PSRAM_STACKS
#define AOS_XTASKCREATE(fn, name, stack, arg, prio, handle) \
    xTaskCreateWithCaps(fn, name, stack, arg, prio, handle, MALLOC_CAP_SPIRAM)
#define AOS_VTASKDELETE_SELF() vTaskDeleteWithCaps(NULL)
#else
#define AOS_XTASKCREATE(fn, name, stack, arg, prio, handle) \
    xTaskCreate(fn, name, stack, arg, prio, handle)
#define AOS_VTASKDELETE_SELF() vTaskDelete(NULL)
#endif

static void tone_task(void *arg)
{
    (void)arg;
    static int16_t buffer[256];
    bool open = false;
    int  idle_rounds = 0;
    int  phase = 0;             /* samples played of the note in flight */

    while (1) {
        /* If the recorder asked for the codec, release it before anything
         * else. We get here promptly because rec_start pushes an empty note to
         * wake the queue. */
        if (s_mic_holds_codec && open) {
            esp_codec_dev_close(s_speaker);
            open = false;
            s_speaker_open = false;
            idle_rounds = 0;
        }

        tone_note_t note;
        if (xQueueReceive(s_tone_queue, &note, pdMS_TO_TICKS(500)) != pdTRUE) {
            /* Closing the codec after half a second turned out to be very
             * expensive: every isolated note switched the amplifier back on,
             * which takes tens of ms to start, and a 40 ms note was consumed
             * entirely by that ramp. Measured: the notes were played
             * (open=ESP_OK, none dropped) but could not be heard except when
             * they came in a burst. Now it is kept open for 10 rounds =
             * 5 seconds. */
            if (open && ++idle_rounds >= 10) {
                esp_codec_dev_close(s_speaker);
                open = false;
                s_speaker_open = false;
                idle_rounds = 0;
            }
            continue;
        }
        idle_rounds = 0;

        /* while music is playing the speaker belongs to the player, and while
         * recording it belongs to nobody: a 40 ms note is not worth breaking
         * the capture for */
        if (!s_speaker || aos_hal_audio_is_playing() || s_mic_holds_codec) {
            continue;
        }
        if (note.freq == 0) {
            continue;       /* empty note: it only served to wake the queue */
        }

        if (!open) {
            esp_codec_dev_sample_info_t fs = {
                .bits_per_sample = 16,
                .channel         = 1,
                .sample_rate     = TONE_RATE,
            };
            s_speaker_open = true;      /* before the open: see s_i2s_idle */
            if (esp_codec_dev_open(s_speaker, &fs) != ESP_OK) {
                continue;
            }
            esp_codec_dev_set_out_vol(s_speaker, s_volume);
            open = true;
            s_speaker_open = true;

            /* A breath of silence so the amplifier settles before the note;
             * without it, the first one after a while is lost. */
            memset(buffer, 0, sizeof(buffer));
            for (int i = 0; i < 3; i++) {      /* ~48 ms at 16 kHz */
                esp_codec_dev_write(s_speaker, buffer, sizeof(buffer));
            }
        }

        int samples = TONE_RATE * note.ms / 1000;
        int chunk = (int)(sizeof(buffer) / sizeof(buffer[0]));
        phase = 0;

        for (int written = 0; written < samples; written += chunk) {
            int count = (samples - written) < chunk ? (samples - written) : chunk;
            for (int i = 0; i < count; i++) {
                float t = (float)(written + i) / (float)samples;
                /* short attack and exponential decay: it sounds like a bell
                 * and not like a buzzer, and it also avoids the click of a
                 * hard cut */
                float env = (t < 0.04f) ? (t / 0.04f) : expf(-3.5f * (t - 0.04f));
                float ph = 2.0f * (float)M_PI * (float)note.freq *
                           (float)(phase + i) / (float)TONE_RATE;
                buffer[i] = (int16_t)((sinf(ph) + 0.22f * sinf(3.0f * ph)) *
                                      env * 5500.0f);
            }
            esp_codec_dev_write(s_speaker, buffer,
                                count * (int)sizeof(int16_t));
            phase += count;
        }
    }
}

void aos_hal_beep(int freq_hz, int ms)
{
    if (freq_hz <= 0 || ms <= 0 || ms > 2000) {
        return;
    }
    if (!s_tone_queue) {
        return;             /* the HAL has not started yet */
    }

    tone_note_t note = { (uint16_t)freq_hz, (uint16_t)ms };
    /* if the queue is full the note is lost: better that than stalling the
     * caller, which is nearly always a frame of a game */
    xQueueSend(s_tone_queue, &note, 0);
}

/* --------------------------------------------------------------------------
 * Player
 *
 * A task reads the file and feeds PCM to the codec. For now it understands
 * 16-bit PCM WAV, which is what comes out without decoding. For MP3 a decoder
 * has to be added (esp_audio_codec or libhelix) and hooked into player_task,
 * between the read and the esp_codec_dev_write.
 * -------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t size;
    char     wave[4];
} wav_riff_t;

typedef struct __attribute__((packed)) {
    char     id[4];
    uint32_t size;
} wav_chunk_t;

typedef struct __attribute__((packed)) {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits;
} wav_fmt_t;

static aos_player_state_t s_player_state;
static char       s_player_path[160];
static char       s_player_title[64];
static uint32_t   s_player_duration;
static uint32_t   s_player_position;
static uint32_t   s_player_rate = 44100;
static uint8_t    s_player_channels = 2;
static volatile bool s_player_abort;

static void player_title_from_path(const char *path)
{
    const char *slash = strrchr(path, '/');
    snprintf(s_player_title, sizeof(s_player_title), "%s", slash ? slash + 1 : path);
    char *dot = strrchr(s_player_title, '.');
    if (dot) {
        *dot = '\0';
    }
}

/* Finds the WAV's fmt and data. Returns false if it is not a WAV we know how
 * to play; leaves the file positioned at the start of the audio. */
static bool wav_open(FILE *file, wav_fmt_t *fmt, uint32_t *data_bytes)
{
    wav_riff_t riff;
    if (fread(&riff, sizeof(riff), 1, file) != 1 ||
        memcmp(riff.riff, "RIFF", 4) != 0 || memcmp(riff.wave, "WAVE", 4) != 0) {
        return false;
    }

    bool have_fmt = false;
    wav_chunk_t chunk;
    while (fread(&chunk, sizeof(chunk), 1, file) == 1) {
        if (memcmp(chunk.id, "fmt ", 4) == 0) {
            if (fread(fmt, sizeof(*fmt), 1, file) != 1) {
                return false;
            }
            /* the fmt chunk may carry extra fields after the base format */
            if (chunk.size > sizeof(*fmt)) {
                fseek(file, (long)(chunk.size - sizeof(*fmt)), SEEK_CUR);
            }
            have_fmt = true;
        } else if (memcmp(chunk.id, "data", 4) == 0) {
            *data_bytes = chunk.size;
            return have_fmt && fmt->format == 1 && fmt->bits == 16;
        } else {
            fseek(file, (long)chunk.size, SEEK_CUR);
        }
    }
    return false;
}

static void player_task(void *arg)
{
    (void)arg;

    FILE *file = fopen(s_player_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "could not open %s", s_player_path);
        s_player_state = AOS_PLAYER_STOPPED;
        s_player_task = NULL;
        AOS_VTASKDELETE_SELF();
        return;
    }

    wav_fmt_t fmt = {0};
    uint32_t data_bytes = 0;
    if (!wav_open(file, &fmt, &data_bytes)) {
        ESP_LOGW(TAG, "%s is not 16-bit PCM WAV", s_player_path);
        fclose(file);
        s_player_state = AOS_PLAYER_STOPPED;
        s_player_task = NULL;
        AOS_VTASKDELETE_SELF();
        return;
    }

    s_player_rate     = fmt.sample_rate;
    s_player_channels = (uint8_t)fmt.channels;
    s_player_duration = fmt.byte_rate ? data_bytes / fmt.byte_rate : 0;

    esp_codec_dev_sample_info_t info = {
        .bits_per_sample = 16,
        .channel         = (uint8_t)fmt.channels,
        .sample_rate     = fmt.sample_rate,
    };

    if (!s_speaker || esp_codec_dev_open(s_speaker, &info) != ESP_OK) {
        ESP_LOGE(TAG, "the codec did not accept %lu Hz", (unsigned long)fmt.sample_rate);
        fclose(file);
        s_player_state = AOS_PLAYER_STOPPED;
        s_player_task = NULL;
        AOS_VTASKDELETE_SELF();
        return;
    }
    esp_codec_dev_set_out_vol(s_speaker, s_volume);

    const size_t buffer_size = 4096;
    uint8_t *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_DEFAULT);
    uint32_t played = 0;

    while (buffer && !s_player_abort && played < data_bytes) {
        if (s_player_state == AOS_PLAYER_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(60));
            continue;
        }
        size_t want = data_bytes - played < buffer_size ? data_bytes - played
                                                        : buffer_size;
        size_t got = fread(buffer, 1, want, file);
        if (got == 0) {
            break;
        }
        if (esp_codec_dev_write(s_speaker, buffer, (int)got) != ESP_OK) {
            break;
        }
        played += got;
        s_player_position = fmt.byte_rate ? played / fmt.byte_rate : 0;
    }

    free(buffer);
    esp_codec_dev_close(s_speaker);
    fclose(file);

    s_player_position = 0;
    s_player_state = AOS_PLAYER_STOPPED;
    s_player_task = NULL;
    AOS_VTASKDELETE_SELF();
}

bool aos_hal_player_play(const char *path)
{
    /* Same reason as in tone_task: opening the speaker while recording tears
     * down the microphone's input channel. */
    if (s_mic_holds_codec) {
        ESP_LOGW(TAG, "no playback while recording");
        return false;
    }

    if (!path || !s_speaker) {
        return false;
    }
    aos_hal_player_stop();

    snprintf(s_player_path, sizeof(s_player_path), "%s", path);
    player_title_from_path(path);
    s_player_position = 0;
    s_player_abort = false;
    s_player_state = AOS_PLAYER_PLAYING;

    if (AOS_XTASKCREATE(player_task, "aos_player", 4096, NULL, 5, &s_player_task) != pdPASS) {
        s_player_state = AOS_PLAYER_STOPPED;
        return false;
    }
    return true;
}

void aos_hal_player_pause(void)
{
    if (s_player_state == AOS_PLAYER_PLAYING) {
        s_player_state = AOS_PLAYER_PAUSED;
    }
}

void aos_hal_player_resume(void)
{
    if (s_player_state == AOS_PLAYER_PAUSED) {
        s_player_state = AOS_PLAYER_PLAYING;
    }
}

void aos_hal_player_stop(void)
{
    if (!s_player_task) {
        s_player_state = AOS_PLAYER_STOPPED;
        return;
    }
    s_player_abort = true;
    for (int i = 0; i < 50 && s_player_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_player_state = AOS_PLAYER_STOPPED;
}

bool aos_hal_player_status(aos_player_status_t *out)
{
    if (!out) {
        return false;
    }
    out->state       = s_player_state;
    out->duration_s  = s_player_duration;
    out->position_s  = s_player_position;
    out->sample_rate = s_player_rate;
    out->channels    = s_player_channels;
    snprintf(out->path, sizeof(out->path), "%s", s_player_path);
    snprintf(out->title, sizeof(out->title), "%s", s_player_title);
    return true;
}

bool aos_hal_play_file(const char *path)
{
    return aos_hal_player_play(path);
}

void aos_hal_audio_stop(void)
{
    aos_hal_player_stop();
}

bool aos_hal_audio_is_playing(void)
{
    return s_player_state == AOS_PLAYER_PLAYING;
}

/* --------------------------------------------------------------------------
 * Recorder
 *
 * A task reads blocks from the microphone and writes them to the WAV as they
 * are: 16-bit mono PCM, uncompressed. At 16 kHz that is 32 KB per second,
 * ~2 MB per minute, which is nothing for a microSD and saves having to bring
 * in an encoder.
 *
 * The header is written twice: on open with the sizes at zero, and on close
 * with the real ones. If the power goes in between, what is left is a WAV
 * claiming 0 bytes of audio; the file is intact, but nobody will open it. So
 * every two seconds the header is rewritten with the figures so far: ending a
 * recording badly costs at most the last two seconds.
 * -------------------------------------------------------------------------- */

#define REC_RING_LEN    256     /* ~12 s of envelope at 20 Hz */
/* ES8311 PGA, in 6 dB steps (0..42).
 *
 * Measured on the board, and not the obvious way: the peak of a recording hit
 * the top of the scale and looked like clipping, but it was the finger hitting
 * the glass when pressing REC. Discarding those 150 ms (REC_SKIP_BLOCKS), the
 * voice alone peaks at 3503 of 32768 with 24 dB, that is -19 dBFS: 20 dB of
 * unused headroom and a file that sounds quiet. With 30 dB a normal voice
 * lands near -13 dBFS and an emphatic one, some 10 dB above that, still does
 * not reach the ceiling. More than that (36) would clip as soon as anybody
 * raised their voice, and clipping cannot be fixed afterwards; something being
 * a bit quiet can. */
#define REC_MIC_GAIN_DB 30      /* the PGA moves in 6 dB steps */
/* The first few milliseconds are the finger hitting the glass a centimetre
 * from the microphone: an impulse that slams into the top of the scale and
 * stays in the file as a click. Measured: with 18 dB, the peak of a normal
 * voice recording read 99% with sustained saturation at 0%, so the only
 * clipping was that knock. Three blocks are discarded; nobody starts talking
 * 150 ms after pressing, and it gives the ADC a moment to settle as well. */
#define REC_SKIP_BLOCKS 3

/* --------------------------------------------------------------------------
 * One capture, two consumers
 *
 * The recorder and the raw microphone are NOT two tasks: they are two clients
 * of the same one. Two tasks reading the same codec is exactly the class of
 * bug that already cost one round (see the codec arbitration, further up), and
 * besides, with a single one the microphone level is valid whenever there is a
 * capture and not only while recording, which was the ceiling of the noise
 * app.
 *
 * The task lives for as long as some user remains and shuts itself down on
 * seeing s_mic_users at zero. Users come and go from the LVGL thread, which is
 * a single one, so it is enough for the counters to be volatile.
 * -------------------------------------------------------------------------- */

#define MIC_USER_REC    0x01    /* the recorder */
#define MIC_USER_RAW    0x02    /* aos_hal_mic_open() */

static volatile uint32_t s_mic_users;
static uint32_t          s_mic_rate = AOS_MIC_RATE_HZ;  /* set by whoever is first */
static volatile bool     s_mic_running;  /* the codec really is open */
static volatile int      s_mic_level;    /* 0..100 of the last block */
static volatile int      s_mic_peak;     /* raw peak of the last block */
static int               s_mic_gain_db = REC_MIC_GAIN_DB;
static volatile bool     s_mic_gain_dirty;

/* Ring of raw PCM: one second, in PSRAM.
 *
 * It is allocated the first time somebody opens the raw microphone and is
 * NEVER freed. It is 32 KB of the 8 MB of PSRAM, and in exchange the task can
 * never find the ring freed from under a memcpy while the recorder keeps it
 * alive. Freeing it would require an orderly shutdown between two clients in
 * order to save 0.4% of the PSRAM. */
static int16_t          *s_pcm_ring;
static uint32_t          s_pcm_len;      /* in samples */
static volatile uint32_t s_pcm_w;        /* monotonic counters, not indices */
static volatile uint32_t s_pcm_r;
static volatile uint32_t s_pcm_dropped;

static aos_rec_state_t   s_rec_state;
static char              s_rec_path[160];
static uint32_t          s_rec_rate = AOS_REC_RATE_HZ;
static volatile uint32_t s_rec_bytes;
static volatile bool     s_rec_abort;

static uint8_t           s_rec_ring[REC_RING_LEN];
static volatile uint32_t s_rec_ring_w;   /* monotonic counters, not indices */
static volatile uint32_t s_rec_ring_r;

static void rec_header_write(FILE *file, uint32_t rate, uint32_t data_bytes)
{
    const uint16_t channels = 1;
    const uint16_t bits     = 16;

    wav_riff_t riff = { .riff = {'R','I','F','F'}, .wave = {'W','A','V','E'} };
    riff.size = 36 + data_bytes;

    wav_chunk_t fmt_chunk  = { .id = {'f','m','t',' '}, .size = 16 };
    wav_fmt_t   fmt = {
        .format      = 1,                       /* PCM */
        .channels    = channels,
        .sample_rate = rate,
        .byte_rate   = rate * channels * bits / 8,
        .block_align = channels * bits / 8,
        .bits        = bits,
    };
    wav_chunk_t data_chunk = { .id = {'d','a','t','a'}, .size = data_bytes };

    fseek(file, 0, SEEK_SET);
    fwrite(&riff, sizeof(riff), 1, file);
    fwrite(&fmt_chunk, sizeof(fmt_chunk), 1, file);
    fwrite(&fmt, sizeof(fmt), 1, file);
    fwrite(&data_chunk, sizeof(data_chunk), 1, file);
}

/* A VU level goes in dB, not in linear.
 *
 * A normal voice a hand's width from the microphone peaks at about 4000 of
 * 32768: on a linear scale that is 12 out of 100 and the waveform never leaves
 * the baseline. In dB it falls in the middle of the scale, which is where the
 * eye expects to see it. -48 dBFS..0 dBFS is mapped to 0..100. */
static uint8_t rec_level_from_peak(int32_t peak)
{
    if (peak < 16) {
        return 0;                   /* noise floor of the ES8311 */
    }
    float db = 20.0f * log10f((float)peak / 32768.0f);
    if (db < -48.0f) {
        return 0;
    }
    int level = (int)((db + 48.0f) * (100.0f / 48.0f) + 0.5f);
    return level > 100 ? 100 : (uint8_t)level;
}

static void rec_push_peak(uint8_t peak)
{
    s_rec_ring[s_rec_ring_w % REC_RING_LEN] = peak;
    s_rec_ring_w++;
}

/* Pushes the block into the raw PCM ring. Only if somebody is listening: while
 * merely recording, copying 1.6 KB every 50 ms is of use to nobody. */
static void pcm_push(const int16_t *samples, int count)
{
    if (!s_pcm_ring || !s_pcm_len) {
        return;
    }
    for (int i = 0; i < count; i++) {
        s_pcm_ring[(s_pcm_w + (uint32_t)i) % s_pcm_len] = samples[i];
    }
    s_pcm_w += (uint32_t)count;
}

/* Statistics of one recording. Reset when each file is opened, not when the
 * capture starts: with the shared task, one capture may see several recordings
 * go by. */
typedef struct {
    int32_t  max_raw;
    uint32_t blocks;
    uint32_t clipped;
    uint32_t level_sum;
    uint32_t since_flush;
    int      warmup;
} rec_stats_t;

/* Closes the WAV: final header, the log line that makes it possible to tune
 * the gain with data, and deleting the file if nothing made it in. */
static void rec_finish(FILE **file, rec_stats_t *st)
{
    if (!*file) {
        return;
    }
    rec_header_write(*file, s_rec_rate, s_rec_bytes);
    fclose(*file);
    *file = NULL;

    /* The peak alone is not enough: a sharp knock also takes it to the
     * ceiling. The proportion of saturated blocks tells "a loud noise" apart
     * from "the gain is wrong", and the mean level says how much headroom went
     * unused. */
    ESP_LOGI(TAG, "recording finished: %u ms, peak %ld of 32768 (%d%%), "
                  "%u%% of %u blocks clipped, average level %u/100",
             (unsigned)(s_rec_rate ? (uint32_t)((uint64_t)s_rec_bytes * 500 / s_rec_rate) : 0),
             (long)st->max_raw, (int)(st->max_raw * 100 / 32768),
             (unsigned)(st->blocks ? st->clipped * 100 / st->blocks : 0),
             (unsigned)st->blocks,
             (unsigned)(st->blocks ? st->level_sum / st->blocks : 0));

    /* If not even a fifth of a second made it in, the capture failed: delete
     * the file instead of leaving a zero-second WAV on the card. */
    if (s_rec_bytes < s_rec_rate / 5 * 2) {
        ESP_LOGW(TAG, "empty recording, deleting %s", s_rec_path);
        remove(s_rec_path);
        s_rec_bytes = 0;
    }
    s_rec_state = AOS_REC_IDLE;
}

static void mic_task(void *arg)
{
    (void)arg;

    FILE       *file = NULL;
    rec_stats_t st   = { 0 };

    /* Wait for the tone task to close the speaker: opening or closing it
     * enables both I2S channels and would knock our capture over. */
    for (int i = 0; i < 40 && s_speaker_open; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 1,
        .sample_rate     = s_mic_rate,
    };
    int open_ret = s_mic ? esp_codec_dev_open(s_mic, &fs) : ESP_CODEC_DEV_NOT_FOUND;
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "the microphone did not accept %lu Hz (%d)",
                 (unsigned long)s_mic_rate, open_ret);
        s_rec_state       = AOS_REC_IDLE;
        s_mic_users       = 0;
        s_mic_holds_codec = false;
        s_mic_task        = NULL;
        AOS_VTASKDELETE_SELF();
        return;
    }
    esp_codec_dev_set_in_gain(s_mic, (float)s_mic_gain_db);
    s_mic_running = true;

    /* One block = one envelope sample, so the peak comes for free. */
    const int block = (int)(s_mic_rate / AOS_REC_PEAK_HZ);
    int16_t *buffer = heap_caps_malloc((size_t)block * sizeof(int16_t),
                                       MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    int errors = 0;

    while (buffer && s_mic_users) {
        if (s_mic_gain_dirty) {
            s_mic_gain_dirty = false;
            esp_codec_dev_set_in_gain(s_mic, (float)s_mic_gain_db);
        }

        /* Recording starts and stops: the file is opened and closed by the
         * task, which is the only one that writes it. aos_hal_rec_start() only
         * declares the intent. */
        if (!file && (s_mic_users & MIC_USER_REC) &&
            !s_rec_abort && s_rec_state != AOS_REC_IDLE) {
            file = fopen(s_rec_path, "wb");
            if (!file) {
                ESP_LOGE(TAG, "could not create %s", s_rec_path);
                s_rec_state  = AOS_REC_IDLE;
                s_mic_users &= ~(uint32_t)MIC_USER_REC;
                continue;
            }
            rec_header_write(file, s_rec_rate, 0);
            st = (rec_stats_t){ .warmup = REC_SKIP_BLOCKS };
        }
        if (file && (s_rec_abort || !(s_mic_users & MIC_USER_REC))) {
            rec_finish(&file, &st);
            s_mic_users &= ~(uint32_t)MIC_USER_REC;
            continue;
        }

        int read_ret = esp_codec_dev_read(s_mic, buffer,
                                          block * (int)sizeof(int16_t));
        if (read_ret != ESP_CODEC_DEV_OK) {
            /* A single error must not cost the whole capture: it is retried a
             * fair few times before giving up. */
            ESP_LOGE(TAG, "microphone read: %d (failure %d)", read_ret, errors + 1);
            if (++errors >= 8) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        errors = 0;

        int32_t peak = 0;
        for (int i = 0; i < block; i++) {
            int32_t value = buffer[i] < 0 ? -buffer[i] : buffer[i];
            if (value > peak) {
                peak = value;
            }
        }
        s_mic_peak  = (int)peak;
        s_mic_level = rec_level_from_peak(peak);
        rec_push_peak((uint8_t)s_mic_level);

        if (s_mic_users & MIC_USER_RAW) {
            pcm_push(buffer, block);
        }

        if (!file || s_rec_state != AOS_REC_RECORDING) {
            continue;       /* not recording or paused: the capture stays alive */
        }
        if (st.warmup > 0) {
            st.warmup--;            /* the finger's knock does not make it into the file */
            continue;
        }

        if (peak > st.max_raw) {
            st.max_raw = peak;
        }
        st.blocks++;
        if (peak >= 32000) {
            st.clipped++;      /* pinned to the ceiling: that is clipping */
        }
        st.level_sum += (uint32_t)s_mic_level;

        if (fwrite(buffer, 1, (size_t)block * sizeof(int16_t), file) == 0) {
            ESP_LOGE(TAG, "no more audio fits on the card");
            s_rec_abort = true;
            continue;
        }
        s_rec_bytes += (uint32_t)block * sizeof(int16_t);

        st.since_flush += (uint32_t)block * sizeof(int16_t);
        if (st.since_flush >= s_rec_rate * 2 * 2) {     /* every ~2 s */
            st.since_flush = 0;
            rec_header_write(file, s_rec_rate, s_rec_bytes);
            fseek(file, 0, SEEK_END);
            fflush(file);
        }
    }

    rec_finish(&file, &st);
    free(buffer);
    esp_codec_dev_close(s_mic);

    s_mic_running     = false;
    s_mic_level       = 0;
    s_mic_peak        = 0;
    s_rec_state       = AOS_REC_IDLE;
    s_mic_users       = 0;
    s_mic_holds_codec = false;      /* the speaker is available again */
    s_mic_task        = NULL;
    AOS_VTASKDELETE_SELF();
}

/* Adds one user to the capture and starts it if it was needed. */
static bool mic_acquire(uint32_t user)
{
    s_mic_users |= user;
    if (s_mic_task) {
        return true;
    }

    /* Reserve the codec BEFORE creating the task, and push an empty note so
     * the tone task wakes up and releases the speaker right now rather than at
     * its next queue timeout (half a second). */
    s_mic_holds_codec = true;
    if (s_tone_queue) {
        tone_note_t wake = { 0, 0 };
        xQueueSend(s_tone_queue, &wake, 0);
    }

    if (AOS_XTASKCREATE(mic_task, "aos_mic", 4096, NULL, 6, &s_mic_task) != pdPASS) {
        s_mic_users &= ~user;
        if (!s_mic_users) {
            s_mic_holds_codec = false;
        }
        return false;
    }
    return true;
}

/* Removes a user. The task shuts down on its own once none are left. */
static void mic_release(uint32_t user)
{
    s_mic_users &= ~user;
}

bool aos_hal_rec_start(const char *path, uint32_t sample_rate)
{
    if (!path || s_rec_state != AOS_REC_IDLE || (s_mic_users & MIC_USER_REC)) {
        return false;
    }

    snprintf(s_rec_path, sizeof(s_rec_path), "%s", path);
    /* If a capture is already running (the tuner, say), the rate is the one
     * already in force: changing it would mean closing the codec from under
     * the other one. */
    if (!s_mic_task) {
        s_mic_rate = sample_rate ? sample_rate : AOS_REC_RATE_HZ;
    }
    s_rec_rate     = s_mic_rate;
    s_rec_bytes    = 0;
    s_rec_abort    = false;
    s_rec_ring_w   = 0;
    s_rec_ring_r   = 0;
    s_rec_state    = AOS_REC_RECORDING;

    if (!mic_acquire(MIC_USER_REC)) {
        s_rec_state = AOS_REC_IDLE;
        return false;
    }
    return true;
}

void aos_hal_rec_pause(void)
{
    if (s_rec_state == AOS_REC_RECORDING) {
        s_rec_state = AOS_REC_PAUSED;
    }
}

void aos_hal_rec_resume(void)
{
    if (s_rec_state == AOS_REC_PAUSED) {
        s_rec_state = AOS_REC_RECORDING;
    }
}

bool aos_hal_rec_stop(void)
{
    if (!(s_mic_users & MIC_USER_REC)) {
        s_rec_state = AOS_REC_IDLE;
        return s_rec_bytes > 0;
    }

    /* We wait for the task to close the file, not to die: with the raw
     * microphone open the capture stays alive after the recording. */
    s_rec_abort = true;
    for (int i = 0; i < 100 && (s_mic_users & MIC_USER_REC); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    mic_release(MIC_USER_REC);
    s_rec_state = AOS_REC_IDLE;
    return s_rec_bytes > 0;
}

bool aos_hal_rec_status(aos_rec_status_t *out)
{
    if (!out) {
        return false;
    }
    out->state       = s_rec_state;
    out->bytes       = s_rec_bytes;
    out->sample_rate = s_rec_rate;
    out->channels    = 1;
    out->level       = s_mic_level;
    /* The time comes from the bytes written, not from the clock: that way what
     * the screen says is exactly how long the file will be. */
    out->elapsed_ms  = s_rec_rate
                     ? (uint32_t)((uint64_t)s_rec_bytes * 500 / s_rec_rate)
                     : 0;
    snprintf(out->path, sizeof(out->path), "%s", s_rec_path);
    return true;
}

int aos_hal_rec_peaks(uint8_t *out, int max)
{
    if (!out || max <= 0) {
        return 0;
    }

    uint32_t write = s_rec_ring_w;
    uint32_t pending = write - s_rec_ring_r;
    if (pending > REC_RING_LEN) {       /* the app fell asleep: we lost the old data */
        s_rec_ring_r = write - REC_RING_LEN;
        pending = REC_RING_LEN;
    }
    if (pending > (uint32_t)max) {
        s_rec_ring_r = write - (uint32_t)max;
        pending = (uint32_t)max;
    }

    for (uint32_t i = 0; i < pending; i++) {
        out[i] = s_rec_ring[(s_rec_ring_r + i) % REC_RING_LEN];
    }
    s_rec_ring_r += pending;
    return (int)pending;
}

/* -------------------------------------------------------------------------- */
/* Raw microphone                                                              */
/* -------------------------------------------------------------------------- */

bool aos_hal_mic_open(uint32_t sample_rate)
{
    if (s_mic_users & MIC_USER_RAW) {
        return true;                /* it was already open */
    }

    /* The real rate: if there is already a capture, whichever one is running. */
    uint32_t rate = s_mic_task ? s_mic_rate
                               : (sample_rate ? sample_rate : AOS_MIC_RATE_HZ);

    /* The ring only grows while the capture is stopped: nobody is writing it
     * then. If it turned out small with the capture running it is left as it
     * is — that is less than a second of history, which is not an error. */
    if (!s_pcm_ring || (s_pcm_len < rate && !s_mic_task)) {
        int16_t *ring = heap_caps_malloc((size_t)rate * sizeof(int16_t),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ring) {
            ESP_LOGE(TAG, "no PSRAM for the microphone ring");
            return false;
        }
        free(s_pcm_ring);
        s_pcm_ring = ring;
        s_pcm_len  = rate;
    }

    s_pcm_w       = 0;
    s_pcm_r       = 0;
    s_pcm_dropped = 0;

    if (!s_mic_task) {
        s_mic_rate = rate;
    }
    return mic_acquire(MIC_USER_RAW);
}

void aos_hal_mic_close(void)
{
    mic_release(MIC_USER_RAW);
}

int aos_hal_mic_read(int16_t *out, int max)
{
    if (!out || max <= 0 || !s_pcm_ring || !s_pcm_len) {
        return 0;
    }

    uint32_t write   = s_pcm_w;
    uint32_t pending = write - s_pcm_r;
    if (pending > s_pcm_len) {          /* the app fell asleep: we lost the old data */
        s_pcm_dropped += pending - s_pcm_len;
        s_pcm_r = write - s_pcm_len;
        pending = s_pcm_len;
    }
    if (pending > (uint32_t)max) {
        pending = (uint32_t)max;
    }

    for (uint32_t i = 0; i < pending; i++) {
        out[i] = s_pcm_ring[(s_pcm_r + i) % s_pcm_len];
    }
    s_pcm_r += pending;
    return (int)pending;
}

int aos_hal_mic_available(void)
{
    if (!s_pcm_ring || !s_pcm_len) {
        return 0;
    }
    uint32_t pending = s_pcm_w - s_pcm_r;
    return (int)(pending > s_pcm_len ? s_pcm_len : pending);
}

bool aos_hal_mic_status(aos_mic_status_t *out)
{
    if (!out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->open        = s_mic_running;
    out->sample_rate = s_mic_rate;
    out->gain_db     = s_mic_gain_db;
    out->level       = s_mic_level;
    out->peak        = s_mic_peak;
    out->dropped     = s_pcm_dropped;
    return true;
}

void aos_hal_mic_gain_set(int db)
{
    if (db < 0) {
        db = 0;
    } else if (db > 42) {
        db = 42;
    }
    db = (db + 3) / 6 * 6;          /* the PGA only has 6 dB steps */
    if (db == s_mic_gain_db) {
        return;
    }
    s_mic_gain_db    = db;
    s_mic_gain_dirty = true;        /* applied by the task, owner of the codec */
}

int aos_hal_mic_gain_get(void)
{
    return s_mic_gain_db;
}

/* --------------------------------------------------------------------------
 * Remote control of the phone's music
 *
 * WAITING ON HARDWARE. The plan is to present as a BLE HID device and send the
 * consumer usages (0x0C): 0xCD play/pause, 0xB5 next, 0xB6 previous, 0xE9/0xEA
 * volume. iOS and Android both understand that. Metadata, iPhone only, via
 * AMS.
 *
 * It is deliberately not implemented yet: a BLE stack written blind, with no
 * phone to test it against, is code that looks like it works and does not. The
 * API is already settled and the simulator implements the whole of it, so when
 * the board arrives only this block has to be filled in.
 * -------------------------------------------------------------------------- */

static bool s_media_enabled;

/* --------------------------------------------------------------------------
 * Control of the phone's music, over AMS
 *
 * This block sat empty from the start waiting for "the BLE stack". It turned
 * out not to be BLE HID -which sends keys blind- but AMS, which also returns
 * the title, the artist and the state. The comment in aos_hal.h said metadata
 * only arrives from an iPhone: true, and this is what it arrives through.
 *
 * Born switched off. See the rule in aos_ams.h.
 * -------------------------------------------------------------------------- */

void aos_hal_media_enable(bool enable)
{
    aos_ble_media_enable(enable);
}

bool aos_hal_media_enabled(void)
{
    return aos_ble_media_enabled();
}

aos_media_link_t aos_hal_media_link(void)
{
    if (!aos_ble_media_enabled()) {
        return AOS_MEDIA_OFF;
    }
    /* CONNECTED means "AMS subscribed and answering", not "there is
     * bluetooth": with the phone connected but no AMS there is nothing to show
     * and no command to send. */
    return aos_ble_media_ready() ? AOS_MEDIA_CONNECTED : AOS_MEDIA_ADVERTISING;
}

const char *aos_hal_media_peer(void)
{
    return aos_ble_peer();
}

const char *aos_hal_media_player(void)
{
    return aos_ble_media_player();
}

bool aos_hal_media_info(aos_media_info_t *out)
{
    aos_ams_state_t st;
    uint32_t pos = 0;
    if (!out || !aos_ble_media_info(&st, &pos)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->title,  sizeof(out->title),  "%s", st.title);
    snprintf(out->artist, sizeof(out->artist), "%s", st.artist);
    snprintf(out->album,  sizeof(out->album),  "%s", st.album);
    out->playing      = st.playing;
    out->has_metadata = st.hay_datos;
    out->duration_s   = st.duration_s;
    out->position_s   = pos;
    return true;
}

bool aos_hal_media_command(aos_media_cmd_t cmd)
{
    return aos_ble_media_command((int)cmd);
}

/* --------------------------------------------------------------------------
 * Bluetooth link
 *
 * The HAL only translates: the stack lives in components/aos_ble/ and does not
 * show its face around here. What comes in over ANCS goes out through
 * aos_notif_push(), which is the same store and the same policy that runs in
 * the simulator.
 *
 * Turning bluetooth on costs ~30 KB of executable RAM, which is where the code
 * of dynamic apps comes from: this switch is also an app switch, like the WiFi
 * one, and turning it off gives all the memory back. The figures are measured
 * in docs/HANDOFF-BLE-ANCS.md section 2.4.
 * -------------------------------------------------------------------------- */

void aos_hal_bt_enable(bool on)
{
    aos_hal_pref_set_i32("bt_on", on ? 1 : 0);
    if (on) {
        if (!aos_ble_start()) {
            ESP_LOGE(TAG, "could not bring up the BLE stack");
        }
    } else {
        aos_ble_stop();
    }
}

bool aos_hal_bt_enabled(void)
{
    /* The preference, not the state of the stack: it is what decides whether
     * the watch brings bluetooth up at startup, and it is what the Settings
     * switch has to show even while the stack is still coming up. */
    int32_t v = 0;
    aos_hal_pref_get_i32("bt_on", &v);
    return v != 0;
}

aos_bt_state_t aos_hal_bt_state(void)  { return aos_ble_state(); }
const char    *aos_hal_bt_peer(void)   { return aos_ble_peer(); }
bool           aos_hal_bt_bonded(void) { return aos_ble_bonded(); }
bool           aos_hal_bt_phone_battery(int *p) { return aos_ble_phone_battery(p); }
void           aos_hal_bt_forget(void) { aos_ble_forget(); }

void     aos_hal_bt_pair_begin(void)          { aos_ble_pair_begin(); }
uint32_t aos_hal_bt_pair_code(void)           { return aos_ble_pair_code(); }
void     aos_hal_bt_pair_confirm(bool accept) { aos_ble_pair_confirm(accept); }
void     aos_hal_bt_pair_cancel(void)         { aos_ble_pair_confirm(false); }

bool aos_hal_notif_action(uint32_t uid, bool positive)
{
    return aos_ble_notif_action(uid, positive);
}

int aos_hal_volume_get(void)
{
    return s_volume;
}

void aos_hal_volume_set(int percent)
{
    s_volume = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    aos_hal_pref_set_i32("volume", s_volume);
}

int aos_hal_mic_level(void)
{
    /* Valid whenever there is a capture, whether from the recorder or from
     * aos_hal_mic_open(). What still is not done is keeping the microphone
     * open just in case: on a 300 mAh battery, a permanent VU meter is
     * expensive. */
    return s_mic_level;
}

/* -------------------------------------------------------------------------- */
/* Network                                                                     */
/* -------------------------------------------------------------------------- */


/* The portal by name rather than by IP: the IP is handed out by the router and
 * changes. With this, http://amoledos.local/ is enough from any device on the
 * network (macOS and iOS resolve .local out of the box; on Android an app is
 * usually needed). */
static void mdns_up(void)
{
    static bool arrancado = false;
    if (arrancado) {
        return;
    }
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns did not start");
        return;
    }
    mdns_hostname_set("amoledos");
    mdns_instance_name_set("AmoledOS");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    arrancado = true;
    ESP_LOGI(TAG, "portal also at http://amoledos.local/");
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        s_net_state = AOS_NET_CONNECTING;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_net_state = AOS_NET_FAILED;
        strcpy(s_net_ip, "0.0.0.0");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_net_ip, sizeof(s_net_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_net_state = AOS_NET_CONNECTED;
        ESP_LOGI(TAG, "wifi connected, ip %s", s_net_ip);
        mdns_up();
    }
}

/* The WiFi stack is only brought up once there is something to connect to.
 *
 * esp_wifi_init() eats several tens of KB of internal RAM, which is precisely
 * what the I2S DMA descriptors and the internal copy the panel's SPI driver
 * builds need. Starting it always, even with no stored credentials, meant
 * audio could not initialise and the firmware aborted. */
static bool s_wifi_started;

/* The network interfaces and the event handlers are created ONCE: creating
 * them again after a deinit leaves rubbish behind. What does come and go is
 * esp_wifi_init/deinit, which is the only thing that really frees the
 * buffers. */
static bool s_netif_ready;

static bool wifi_stack_start(void)
{
    if (s_wifi_started) {
        return true;
    }

    if (!s_netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init());
        esp_netif_create_default_wifi_sta();
        s_netif_ap = esp_netif_create_default_wifi_ap();
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, NULL, NULL);
        s_netif_ready = true;
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "could not initialise the wifi");
        return false;
    }

    s_wifi_started = true;
    return true;
}

/* esp_wifi_stop() on its own does NOT give the static buffers back: the deinit
 * is needed. That is some 60 KB of executable memory, which is where the .text
 * of dynamic apps comes from. */
static void wifi_stack_stop(void)
{
    if (!s_wifi_started) {
        return;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    s_wifi_started = false;
    s_ap_active   = false;
    strcpy(s_net_ip, "0.0.0.0");
    ESP_LOGI(TAG, "wifi off, memory returned");
}

void aos_hal_net_enable(bool on)
{
    aos_hal_pref_set_i32("wifi_on", on ? 1 : 0);

    if (!on) {
        wifi_stack_stop();
        s_net_state = AOS_NET_OFF;
        return;
    }

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!aos_hal_pref_get_str("wifi_ssid", ssid, sizeof(ssid))) {
        ESP_LOGW(TAG, "no wifi credentials stored");
        s_net_state = AOS_NET_OFF;
        return;
    }
    aos_hal_pref_get_str("wifi_pass", pass, sizeof(pass));
    snprintf(s_net_ssid, sizeof(s_net_ssid), "%s", ssid);

    /* only here is the stack brought up: there are credentials to use */
    if (!wifi_stack_start()) {
        s_net_state = AOS_NET_FAILED;
        return;
    }

    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", pass);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_start();
    s_net_state = AOS_NET_CONNECTING;
}


/* -------------------------------------------------------------------------- */
/* Onboarding through the board's own access point                             */
/* -------------------------------------------------------------------------- */

#define AOS_AP_PASS     "amoledos"      /* WPA2 asks for 8 characters minimum */

bool aos_hal_net_has_credentials(void)
{
    char ssid[33] = {0};
    return aos_hal_pref_get_str("wifi_ssid", ssid, sizeof(ssid)) && ssid[0];
}

bool aos_hal_net_set_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) {
        return false;
    }
    aos_hal_pref_set_str("wifi_ssid", ssid);
    aos_hal_pref_set_str("wifi_pass", pass ? pass : "");
    ESP_LOGI(TAG, "network saved: %s", ssid);

    /* Reconnect with the new network. The AP, if it was up, is switched off by
     * the caller: the page had better manage to answer before it is cut. Saving
     * a network implies wanting to use it, so this switches it on as well. */
    aos_hal_net_enable(true);
    return true;
}

void aos_hal_net_forget(void)
{
    aos_hal_pref_set_str("wifi_ssid", "");
    aos_hal_pref_set_str("wifi_pass", "");
    s_net_ssid[0] = 0;
    aos_hal_net_enable(false);
}

/* --------------------------------------------------------------------------
 * AP name and password
 *
 * The three getters answer even with the AP switched off, and that is
 * deliberate: the screen shows the password and the QR BEFORE bringing it up.
 * The default name comes from esp_read_mac(), which reads the factory MAC out
 * of the eFuse without turning the radio on; esp_wifi_get_mac() -which is what
 * was there- needs the stack initialised and returned rubbish until the first
 * ap_start.
 * -------------------------------------------------------------------------- */

#define AOS_AP_KEY_SSID  "ap_ssid"
#define AOS_AP_KEY_PASS  "ap_pass"
#define AOS_AP_KEY_MODE  "ap_pmode"

const char *aos_hal_net_ap_default_ssid(void)
{
    static char automatico[33];
    if (!automatico[0]) {
        /* The last two bytes of the softAP's MAC are enough to tell boards on
         * the same table apart. */
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
        snprintf(automatico, sizeof(automatico), "AmoledOS-%02X%02X",
                 mac[4], mac[5]);
    }
    return automatico;
}

aos_ap_pass_mode_t aos_hal_net_ap_pass_mode(void)
{
    int32_t modo = AOS_AP_PASS_FIXED;
    aos_hal_pref_get_i32(AOS_AP_KEY_MODE, &modo);
    return modo == AOS_AP_PASS_ROTATING ? AOS_AP_PASS_ROTATING
                                        : AOS_AP_PASS_FIXED;
}

/* Alphabet without characters that get confused when read off the screen (0/O,
 * 1/l/I) and without the ones that have to be escaped in the QR's text
 * (\ ; , : "). Whoever scans the QR does not type it, but whoever is standing
 * next to them reading the screen does. */
static const char AP_PASS_ALFABETO[] =
    "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
#define AP_PASS_LARGO   10

static void ap_pass_generar(char *out, size_t len)
{
    /* esp_random() uses the hardware generator; with the radio up it has real
     * entropy, and here it runs right before esp_wifi_start(). */
    for (size_t i = 0; i + 1 < len && i < AP_PASS_LARGO; i++) {
        out[i] = AP_PASS_ALFABETO[esp_random() % (sizeof(AP_PASS_ALFABETO) - 1)];
        out[i + 1] = 0;
    }
}

/* Leaves in s_ap_ssid / s_ap_pass whatever should be used right now. With
 * 'rotar' the password is renewed if the mode is rotating; the getters call
 * with false so as not to change it merely by asking. */
static void ap_config_resolver(bool rotar)
{
    char guardado[33] = {0};
    if (aos_hal_pref_get_str(AOS_AP_KEY_SSID, guardado, sizeof(guardado)) &&
        guardado[0]) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", guardado);
    } else {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", aos_hal_net_ap_default_ssid());
    }

    char clave[65] = {0};
    bool hay = aos_hal_pref_get_str(AOS_AP_KEY_PASS, clave, sizeof(clave)) &&
               clave[0];

    if (aos_hal_net_ap_pass_mode() == AOS_AP_PASS_ROTATING) {
        /* The rotating password IS stored. If it only lived in RAM, the screen
         * would show one after a restart and the AP would come up with
         * another; besides, the portal reads it from a different task. It is
         * renewed when the AP comes up -not per client-, so the QR keeps
         * working for as long as it lasts. */
        if (rotar || !hay) {
            char nueva[AP_PASS_LARGO + 1] = {0};
            ap_pass_generar(nueva, sizeof(nueva));
            snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", nueva);
            aos_hal_pref_set_str(AOS_AP_KEY_PASS, s_ap_pass);
        } else {
            snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", clave);
        }
        return;
    }

    snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", hay ? clave : AOS_AP_PASS);
}

const char *aos_hal_net_ap_ssid(void)
{
    if (!s_ap_active) {
        ap_config_resolver(false);
    }
    return s_ap_ssid;
}

const char *aos_hal_net_ap_pass(void)
{
    if (!s_ap_active) {
        ap_config_resolver(false);
    }
    return s_ap_pass;
}

const char *aos_hal_net_ap_ip(void)   { return s_ap_ip; }
bool        aos_hal_net_ap_active(void) { return s_ap_active; }

bool aos_hal_net_ap_set_config(const char *ssid, const char *pass,
                               aos_ap_pass_mode_t mode)
{
    if (ssid && ssid[0] && strlen(ssid) > 32) {
        return false;
    }
    /* WPA2 asks for between 8 and 63 characters. Empty is legitimate: it means
     * "go back to the factory one", not "open network" - a setup AP with no
     * password leaves the home WiFi form within anybody's reach. */
    if (mode == AOS_AP_PASS_FIXED && pass && pass[0] &&
        (strlen(pass) < 8 || strlen(pass) > 63)) {
        return false;
    }

    aos_hal_pref_set_str(AOS_AP_KEY_SSID, ssid ? ssid : "");
    aos_hal_pref_set_i32(AOS_AP_KEY_MODE, (int32_t)mode);
    /* In rotating mode the stored one is wiped: if the old one stayed, the
     * screen would show the previous fixed password until the next ap_start.
     * Emptying it makes the first getter generate one on the spot. */
    aos_hal_pref_set_str(AOS_AP_KEY_PASS,
                         (mode == AOS_AP_PASS_FIXED && pass) ? pass : "");
    s_ap_pass[0] = 0;       /* so ap_config_resolver() re-reads it from NVS */

    ESP_LOGI(TAG, "AP configured: %s / key %s",
             (ssid && ssid[0]) ? ssid : "(automatic)",
             mode == AOS_AP_PASS_ROTATING ? "rotating" : "fixed");

    /* With the AP up it is bounced so the change takes effect. That drops
     * whoever is connected, which is unavoidable if the password has just been
     * changed: the portal answers BEFORE calling here, just like /api/wifi. */
    if (s_ap_active) {
        aos_hal_net_ap_stop();
        return aos_hal_net_ap_start();
    }
    return true;
}

bool aos_hal_net_ap_start(void)
{
    if (s_ap_active) {
        return true;
    }
    if (!wifi_stack_start()) {
        return false;
    }

    ap_config_resolver(true);

    wifi_config_t ap = {0};
    /* memcpy with an explicit length and not snprintf: ap.ap.ssid is 32 bytes
     * WITHOUT a terminator -the length travels in ssid_len- so a 32-character
     * name, which is the legal maximum, fits exactly. With snprintf the
     * compiler warns about truncation and it is right: it would eat the last
     * one. */
    size_t n_ssid = strlen(s_ap_ssid);
    if (n_ssid > sizeof(ap.ap.ssid)) {
        n_ssid = sizeof(ap.ap.ssid);
    }
    memcpy(ap.ap.ssid, s_ap_ssid, n_ssid);
    ap.ap.ssid_len = (uint8_t)n_ssid;

    /* The password is terminated: 63 characters plus the zero, which is the
     * WPA2 maximum and what aos_hal_net_ap_set_config() validates. */
    size_t n_pass = strlen(s_ap_pass);
    if (n_pass >= sizeof(ap.ap.password)) {
        n_pass = sizeof(ap.ap.password) - 1;
    }
    memcpy(ap.ap.password, s_ap_pass, n_pass);
    ap.ap.password[n_pass] = 0;
    ap.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;

    /* The channel: the STA's if there is a connection, and 1 if there is
     * none.
     *
     * There is ONE radio. In APSTA the AP and the STA have to share a channel,
     * and the STA's is the one that rules. With channel 1 hard-coded -which is
     * what was there- bringing the AP up while associated to a network on
     * another channel knocked the board off the home network: it was seen on
     * the board, with the STA on channel 6, and the symptom is that the portal
     * stops answering on the LAN exactly when you tap "Configurar red".
     *
     * Asking for the STA's channel lets the two coexist: the watch stays on
     * the home network AND serves its form. When there is no connection there
     * is nothing to clash with and 1 is fine. */
    ap.ap.channel = 1;
    wifi_ap_record_t asociado;
    if (esp_wifi_sta_get_ap_info(&asociado) == ESP_OK && asociado.primary) {
        ap.ap.channel = asociado.primary;
        ESP_LOGI(TAG, "the AP goes to channel %d, which is where the STA is",
                 asociado.primary);
    }

    /* APSTA and not plain AP: STA mode is what makes it possible to scan the
     * networks around while the AP goes on serving the form. */
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }

    esp_netif_ip_info_t ip;
    if (s_netif_ap && esp_netif_get_ip_info(s_netif_ap, &ip) == ESP_OK) {
        snprintf(s_ap_ip, sizeof(s_ap_ip), IPSTR, IP2STR(&ip.ip));
    }

    s_ap_active = true;
    ESP_LOGI(TAG, "access point up: %s / %s -> http://%s/",
             s_ap_ssid, s_ap_pass, s_ap_ip);
    return true;
}

void aos_hal_net_ap_stop(void)
{
    if (!s_ap_active) {
        return;
    }
    s_ap_active = false;
    /* If there are credentials it goes back to STA alone; if not, the radio is
     * switched off. */
    if (aos_hal_net_has_credentials()) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_stop();
        s_net_state = AOS_NET_OFF;
    }
    ESP_LOGI(TAG, "access point down");
}

int aos_hal_net_scan(aos_wifi_ap_t *out, int max)
{
    if (!out || max <= 0 || !wifi_stack_start()) {
        return -1;
    }
    /* Scanning needs the STA interface up. If nothing is running it is brought
     * up in plain STA just to look. */
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
    }

    wifi_scan_config_t cfg = { .show_hidden = false };
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) {
        return -1;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found == 0) {
        return 0;
    }
    if (found > (uint16_t)max) {
        found = (uint16_t)max;
    }

    wifi_ap_record_t *recs = calloc(found, sizeof(wifi_ap_record_t));
    if (!recs) {
        return -1;
    }
    esp_wifi_scan_get_ap_records(&found, recs);

    int n = 0;
    for (uint16_t i = 0; i < found; i++) {
        if (!recs[i].ssid[0]) {
            continue;               /* hidden network: no name, no use */
        }
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", (char *)recs[i].ssid);
        out[n].rssi   = recs[i].rssi;
        out[n].secure = (recs[i].authmode != WIFI_AUTH_OPEN);
        n++;
    }
    free(recs);
    return n;
}


bool aos_hal_net_enabled(void)
{
    int32_t on = 1;                 /* on by default */
    aos_hal_pref_get_i32("wifi_on", &on);
    return on != 0;
}

aos_net_state_t aos_hal_net_state(void) { return s_net_state; }
const char     *aos_hal_net_ssid(void)  { return s_net_ssid; }
const char     *aos_hal_net_ip(void)    { return s_net_ip; }

int aos_hal_net_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

bool aos_hal_net_sync_time(void)
{
    if (s_net_state != AOS_NET_CONNECTED) {
        return false;
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000)) != ESP_OK) {
        return false;
    }
    struct tm now;
    aos_hal_time_now(&now);
    aos_board_rtc_set(&now);
    aos_hal_pref_set_i32("time_ok", 1);
    return true;
}

/* -------------------------------------------------------------------------- */
/* Miscellaneous                                                               */
/* -------------------------------------------------------------------------- */

uint64_t aos_hal_uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

void aos_hal_heap_info(uint32_t *free_internal, uint32_t *free_psram)
{
    if (free_internal) {
        *free_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    }
    if (free_psram) {
        *free_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
}

const char *aos_hal_board_name(void)       { return s_board_name; }
/* From the app descriptor, which CMakeLists fills with 'git describe --tags'.
 * FIRMWARE_VERSION is only the floor for a build with no git repo behind it. */
const char *aos_hal_firmware_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return (d && d->version[0]) ? d->version : FIRMWARE_VERSION;
}

/* -------------------------------------------------------------------------- */
/* OTA                                                                         */
/*                                                                             */
/* Deliberately thin: opening, writing and closing. No downloading, no          */
/* progress bar, no policy about when to update. The bytes come from whoever    */
/* calls -today the portal's POST /api/ota- and this puts them down.            */
/*                                                                             */
/* esp_ota_write() validates the header of the first chunk, so an image for     */
/* another chip, or a file that is not an image at all, fails on the first      */
/* write and never touches the running slot. The idle slot is the only thing    */
/* that gets erased, so a failure halfway leaves the watch exactly as it was.   */
/* -------------------------------------------------------------------------- */

static esp_ota_handle_t     s_ota;
static const esp_partition_t *s_ota_part;
static char                 s_ota_err[96];

static void ota_fail(const char *what, esp_err_t err)
{
    snprintf(s_ota_err, sizeof(s_ota_err), "%s: %s", what, esp_err_to_name(err));
    ESP_LOGE(TAG, "ota: %s", s_ota_err);
}

bool aos_hal_ota_begin(size_t total_bytes)
{
    if (s_ota) {
        aos_hal_ota_abort();     /* an interrupted one left the slot open */
    }
    s_ota_err[0] = '\0';

    s_ota_part = esp_ota_get_next_update_partition(NULL);
    if (!s_ota_part) {
        snprintf(s_ota_err, sizeof(s_ota_err), "no hay particion OTA libre");
        ESP_LOGE(TAG, "ota: %s", s_ota_err);
        return false;
    }
    if (total_bytes > s_ota_part->size) {
        snprintf(s_ota_err, sizeof(s_ota_err), "la imagen no entra: %u B en %u B",
                 (unsigned)total_bytes, (unsigned)s_ota_part->size);
        ESP_LOGE(TAG, "ota: %s", s_ota_err);
        return false;
    }

    /* OTA_SIZE_UNKNOWN erases the whole partition, which is several seconds.
     * With the size known it only erases what it needs. */
    esp_err_t err = esp_ota_begin(s_ota_part,
                                  total_bytes ? total_bytes : OTA_SIZE_UNKNOWN,
                                  &s_ota);
    if (err != ESP_OK) {
        s_ota = 0;
        ota_fail("esp_ota_begin", err);
        return false;
    }
    ESP_LOGI(TAG, "ota: writing into %s (%u B free), image of %u B",
             s_ota_part->label, (unsigned)s_ota_part->size, (unsigned)total_bytes);
    return true;
}

bool aos_hal_ota_write(const void *data, size_t len)
{
    if (!s_ota) {
        return false;
    }
    esp_err_t err = esp_ota_write(s_ota, data, len);
    if (err != ESP_OK) {
        ota_fail("esp_ota_write", err);
        esp_ota_abort(s_ota);
        s_ota = 0;
        return false;
    }
    return true;
}

bool aos_hal_ota_end(void)
{
    if (!s_ota) {
        return false;
    }
    esp_err_t err = esp_ota_end(s_ota);
    s_ota = 0;
    if (err != ESP_OK) {
        /* ESP_ERR_OTA_VALIDATE_FAILED is the interesting one: the image
         * arrived whole but its checksum does not match. */
        ota_fail("esp_ota_end", err);
        return false;
    }
    err = esp_ota_set_boot_partition(s_ota_part);
    if (err != ESP_OK) {
        ota_fail("esp_ota_set_boot_partition", err);
        return false;
    }
    ESP_LOGW(TAG, "ota: %s is now the boot partition; it starts on trial",
             s_ota_part->label);
    return true;
}

void aos_hal_ota_abort(void)
{
    if (s_ota) {
        esp_ota_abort(s_ota);
        s_ota = 0;
        ESP_LOGW(TAG, "ota: aborted, the running image is untouched");
    }
}

const char *aos_hal_ota_error(void)
{
    return s_ota_err;
}

bool aos_hal_ota_pending_verify(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!run || esp_ota_get_state_partition(run, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

void aos_hal_ota_mark_valid(void)
{
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        ESP_LOGI(TAG, "ota: this image is confirmed, the rollback is cancelled");
    }
}

const char *aos_hal_ota_running_slot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run ? run->label : "?";
}

/* The format is built separately and only then passed through ESP_LOGI.
 *
 * This used to call esp_log_writev() directly, which adds NEITHER the
 * "I (ms) tag:" prefix NOR the trailing newline: the macro puts those in. None
 * of the ~50 sites calling aos_hal_log() writes the \n by hand -and the
 * simulator's HAL adds it itself-, so every system line came out glued to the
 * next:
 *
 *     idioma: de, 357 cadenas (tarjeta)I (2995) wifi:<ba-add>idx:0 ...
 *
 * It is always annoying and it misleads exactly when it matters: a line that
 * starts where another ends is found by no grep of the log. */
void aos_hal_log(const char *tag, const char *fmt, ...)
{
    /* 256 is enough with room to spare: the system's longest line runs to
     * about 100. It truncates silently if one day it is not enough, which is
     * preferable to spending stack in the caller's task. */
    char linea[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(linea, sizeof(linea), fmt, args);
    va_end(args);
    ESP_LOGI(tag, "%s", linea);
}

/*
 * The v2 board carries a CST820 (ID 0xB7), not the CST816S the BSP claims. The
 * difference that matters: **it falls asleep on its own**. Touching it wakes
 * it, it latches the gesture and the coordinates, and it goes back to sleep
 * before the driver manages to read the finger-count register (0x02), which is
 * the one it looks at to decide whether there is a touch. Result: the chip
 * answers over I2C, reports correct coordinates, and the screen still looks
 * dead.
 *
 * Writing any non-zero value to 0xFE disables auto-sleep. Measured: without
 * this LVGL sees not a single touch; with it, it sees them all.
 */
static i2c_master_dev_handle_t s_touch_dev;

static void touch_disable_autosleep(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus || i2c_master_probe(bus, 0x15, 100) != ESP_OK) {
        return;         /* not the v2's touch panel */
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = 0x15,
        .scl_speed_hz    = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_touch_dev) != ESP_OK) {
        return;
    }

    const uint8_t dis_auto_sleep[2] = { 0xFE, 0xFF };
    esp_err_t ret = i2c_master_transmit(s_touch_dev, dis_auto_sleep,
                                        sizeof(dis_auto_sleep), 100);
    ESP_LOGI(TAG, "CST820 touch: auto-sleep disabled (%s)", esp_err_to_name(ret));
}

/* The CST820 puts itself back to sleep (after its own internal reset it
 * returns to the factory values), and asleep it stops asserting the interrupt.
 * A 2-byte write every 5 s is unnoticeable next to the reads the touch driver
 * already does. Note: this is separate from the gesture polling we tried
 * earlier which set off watchdogs; that one read every 40 ms. */
static void touch_keep_awake(void)
{
    if (!s_touch_dev) {
        return;
    }
    const uint8_t dis_auto_sleep[2] = { 0xFE, 0xFF };
    i2c_master_transmit(s_touch_dev, dis_auto_sleep, sizeof(dis_auto_sleep), 50);
}

/* CST820 gesture codes, register 0x01 */
#define CST_GESTURE_SLIDE_DOWN  0x01
#define CST_GESTURE_SLIDE_UP    0x02
#define CST_GESTURE_SLIDE_LEFT  0x03
#define CST_GESTURE_SLIDE_RIGHT 0x04

static volatile aos_touch_gesture_t s_pending_gesture;

/* UNUSED. Polling the gesture register from the background task, in parallel
 * with the touch driver's reads on the same bus, saturated CPU 0 and set off
 * the watchdog ("task_wdt ... CPU 0: aos_hk"). And it is not needed: LVGL
 * detects swipes properly once the screen is awake. Kept in case the chip's
 * gesture ever becomes necessary, but not called. */
__attribute__((unused))
static void touch_poll_gesture(void)
{
    static uint8_t prev_fingers;

    if (!s_touch_dev) {
        return;
    }

    uint8_t reg = 0x01, buf[2] = {0};
    if (i2c_master_transmit_receive(s_touch_dev, &reg, 1, buf, sizeof(buf), 50) != ESP_OK) {
        return;
    }

    uint8_t gesture = buf[0];
    uint8_t fingers = buf[1] & 0x0F;

    if (prev_fingers && !fingers) {          /* released */
        switch (gesture) {
        case CST_GESTURE_SLIDE_DOWN:  s_pending_gesture = AOS_TOUCH_GESTURE_DOWN;  break;
        case CST_GESTURE_SLIDE_UP:    s_pending_gesture = AOS_TOUCH_GESTURE_UP;    break;
        case CST_GESTURE_SLIDE_LEFT:  s_pending_gesture = AOS_TOUCH_GESTURE_LEFT;  break;
        case CST_GESTURE_SLIDE_RIGHT: s_pending_gesture = AOS_TOUCH_GESTURE_RIGHT; break;
        default: break;
        }
        if (s_pending_gesture != AOS_TOUCH_GESTURE_NONE) {
            ESP_LOGD(TAG, "touch gesture: 0x%02X", gesture);
        }
    }
    prev_fingers = fingers;
}

aos_touch_gesture_t aos_hal_touch_gesture(void)
{
    aos_touch_gesture_t g = s_pending_gesture;
    s_pending_gesture = AOS_TOUCH_GESTURE_NONE;
    return g;
}

/* --------------------------------------------------------------------------
 * Panel sleep
 *
 * "Screen off" used to mean brightness 0: every pixel dark, but the driver IC
 * still scanning, its charge pumps still running and the QSPI link still
 * live. Sleep-in (0x10) stops all of that and keeps the frame memory, so the
 * wake is sleep-out (0x11), a wait the panel's datasheet asks for, and
 * display-on (0x29). Both revisions' controllers speak the same MIPI DCS
 * commands and both go through the co5300 driver with the QSPI opcode wrap,
 * which is what the (0x02 << 24) below is.
 *
 * Taken under the LVGL lock so a flush cannot be mid-flight on the same bus.
 * The lock is recursive, so this works from the LVGL task and from the
 * housekeeping task alike.
 * -------------------------------------------------------------------------- */
static esp_err_t panel_cmd(uint8_t cmd)
{
    return esp_lcd_panel_io_tx_param(s_panel_io, (0x02 << 24) | ((int)cmd << 8), NULL, 0);
}

/* With the screen off LVGL has nothing to draw and nobody to listen to, but
 * its refresh timer (33 ms) and the touch read timer (33 ms) go on waking
 * the chip. The refresh timer is paused; the touch is read every 200 ms,
 * because a finger also wakes the chip through GPIO21 and the read is what
 * turns that into aos_hal_activity(). Both back to normal on wake. */
static void lvgl_timers_idle(bool idle)
{
    if (!s_display || !aos_hal_lock(2000)) {
        return;
    }
    lv_timer_t *refr = lv_display_get_refr_timer(s_display);
    if (refr) {
        if (idle) lv_timer_pause(refr); else lv_timer_resume(refr);
    }
    for (lv_indev_t *indev = lv_indev_get_next(NULL); indev; indev = lv_indev_get_next(indev)) {
        lv_timer_t *read = lv_indev_get_read_timer(indev);
        if (read) {
            lv_timer_set_period(read, idle ? 200 : LV_DEF_REFR_PERIOD);
        }
    }
    aos_hal_unlock();
}

/* true when the panel was actually put to sleep or woken by this call */
static bool panel_sleep(bool sleep)
{
    if (!s_panel || !s_panel_io || sleep == s_panel_asleep) {
        return false;
    }
    if (sleep && !s_panel_sleep_enabled) {
        return false;
    }
    if (!aos_hal_lock(2000)) {
        ESP_LOGW(TAG, "panel %s: could not take the LVGL lock", sleep ? "sleep" : "wake");
        return false;
    }
    int64_t t0 = esp_timer_get_time();
    if (sleep) {
        /* No display-off here: the panel's own sleep-in blanks it, and a
         * display-on at wake was measured to cost a light-blue then white
         * flash (see docs/POWER.md 6b). Brightness is already 0. */
        panel_cmd(LCD_CMD_SLPIN);
        vTaskDelay(pdMS_TO_TICKS(5));
    } else {
        s_panel_last_err = panel_cmd(LCD_CMD_SLPOUT);
        /* Sleep-out brings the brightness register back to the factory 0xFF
         * on this panel, and with it a flash of whatever the pixels do while
         * their supply comes up (seen: light blue, then white). Brightness is
         * pinned at 0 for the whole sequence; the caller raises it after the
         * panel is on and showing a finished frame. */
        bsp_display_brightness_set(0);
        vTaskDelay(pdMS_TO_TICKS(AOS_PANEL_WAKE_MS));
        /* The panel's memory still holds whatever was on screen when it went
         * to sleep, and the UI has moved on since (back to the face, into
         * always-on): with the refresh timer paused none of that was drawn.
         * Displaying the old memory and letting LVGL catch up on top of it
         * showed the menu bleeding through the watchface. So: the whole
         * screen is rendered into the panel while it is still display-off,
         * and only then is it switched on. Costs about 60 ms of the wake. */
        if (s_display) {
            lv_timer_t *refr = lv_display_get_refr_timer(s_display);
            if (refr) lv_timer_resume(refr);
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(s_display);
        }
        vTaskDelay(pdMS_TO_TICKS(20));      /* one frame before the brightness comes up */
    }
    s_panel_asleep = sleep;
    aos_hal_unlock();
    ESP_LOGI(TAG, "panel %s in %lld ms", sleep ? "asleep" : "awake",
             (esp_timer_get_time() - t0) / 1000);
    return true;
}

/* --------------------------------------------------------------------------
 * Power policy
 *
 * Two knobs, both decided here and nowhere else:
 *
 *   CPU  240 MHz while the screen is active or audio runs; 80 MHz otherwise.
 *        It is a pm lock on ESP_PM_CPU_FREQ_MAX, so anything else that needs
 *        the full clock can take its own. Without power saving the lock is
 *        simply always held, which is the behaviour the firmware always had.
 *   WiFi deepest modem sleep with the screen off, the light one otherwise.
 *        The web portal answers a few hundred ms later with the screen off,
 *        which nobody is looking at anyway.
 *
 * "Saving" is the preference OR the battery under 20% and unplugged.
 * -------------------------------------------------------------------------- */
static bool power_saving_active(void)
{
    return s_power_saving || s_low_battery_saving;
}

static void pm_policy_apply(void)
{
    bool saving   = power_saving_active();
    bool audio    = s_player_task != NULL || s_mic_task != NULL ||
                    s_speaker_open || s_mic_holds_codec;
    bool want_max = !saving || s_display_state == AOS_DISPLAY_ACTIVE || audio;

    if (audio) {
        s_i2s_idle = false;
    } else if (!s_i2s_idle) {
        if (s_i2s_tx) i2s_channel_disable(s_i2s_tx);
        if (s_i2s_rx) i2s_channel_disable(s_i2s_rx);
        s_i2s_idle = true;
    }

    if (s_pm_max_lock) {
        if (want_max && !s_pm_max_held) {
            if (esp_pm_lock_acquire(s_pm_max_lock) == ESP_OK) {
                s_pm_max_held = true;
            }
        } else if (!want_max && s_pm_max_held) {
            if (esp_pm_lock_release(s_pm_max_lock) == ESP_OK) {
                s_pm_max_held = false;
            }
        }
    }

    /* Light sleep only with the screen off and no audio: the QSPI panel and
     * the I2S codec are not asked to survive it, and neither is anyone who is
     * looking at the watch. It is an esp_pm reconfiguration, so it can be
     * switched at run time. */
    /* And only on battery: on USB there is nothing to save, and the
     * USB-Serial-JTAG console does not survive light sleep - the port
     * vanishes from the host until it is replugged, which is no way to
     * develop. s_usb_last is what the PMU said, refreshed every 5 s and on
     * every insert/remove interrupt. */
    bool want_ls = s_light_sleep_enabled && saving && s_pm_max_lock &&
                   s_display_state == AOS_DISPLAY_OFF && !audio && !s_usb_last;
    if (want_ls != s_light_sleep_on) {
        esp_pm_config_t pm = {
            .max_freq_mhz = AOS_DFS_MAX_MHZ,
            .min_freq_mhz = AOS_DFS_MIN_MHZ,
            .light_sleep_enable = want_ls,
        };
        esp_err_t ret = esp_pm_configure(&pm);
        if (ret == ESP_OK) {
            s_light_sleep_on = want_ls;
            ESP_LOGI(TAG, "light sleep %s", want_ls ? "on" : "off");
        } else {
            ESP_LOGW(TAG, "light sleep %s refused: %s", want_ls ? "on" : "off", esp_err_to_name(ret));
        }
    }

    if (s_net_state != AOS_NET_OFF) {
        int ps = (saving && s_display_state == AOS_DISPLAY_OFF) ? WIFI_PS_MAX_MODEM
                                                                 : WIFI_PS_MIN_MODEM;
        if (ps != s_wifi_ps && esp_wifi_set_ps((wifi_ps_type_t)ps) == ESP_OK) {
            s_wifi_ps = ps;
        }
    }
}

static int cpu_mhz_now(void)
{
    rtc_cpu_freq_config_t cfg;
    rtc_clk_cpu_freq_get_config(&cfg);
    return (int)cfg.freq_mhz;
}

/* --------------------------------------------------------------------------
 * Battery bookkeeping
 *
 * The AXP2101 has no coulomb counter, so anything about rates is arithmetic
 * on its percentage over time. It is honest arithmetic: nothing is shown
 * until there has been a quarter of an hour and two percent of drop to
 * divide, and it resets every time USB is plugged in.
 * -------------------------------------------------------------------------- */
static void battery_stats_save(void)
{
    if (s_unsaved_minutes) {
        aos_hal_pref_set_i32("bat_min", (int32_t)s_battery_minutes);
        s_unsaved_minutes = 0;
    }
}

static void power_event(aos_power_event_t event, int percent)
{
    if (s_power_cb) {
        s_power_cb(event, percent);
    }
}

static void usb_changed(bool present, int percent)
{
    if (present == s_usb_last) {
        return;
    }
    s_usb_last = present;
    pm_policy_apply();
    if (present) {
        ESP_LOGI(TAG, "usb in at %d%%", percent);
        s_unplug_us   = 0;
        s_drain_pct_h = NAN;
        s_hours_left  = NAN;
        s_charge_counted = false;
        s_critical_strikes = 0;
        battery_stats_save();
        power_event(AOS_POWER_USB_IN, percent);
    } else {
        ESP_LOGI(TAG, "usb out at %d%%", percent);
        s_unplug_us  = esp_timer_get_time();
        s_unplug_pct = percent;
        s_low_warned = false;
        power_event(AOS_POWER_USB_OUT, percent);
    }
}

/* The clean way down: tell the UI, give it a moment to say so, save what has
 * to be saved, and let the PMU cut the rails. Only ever on battery. */
static void power_critical(int percent)
{
    if (s_shutting_down) {
        return;
    }
    s_shutting_down = true;
    ESP_LOGW(TAG, "battery critical at %d%%: powering off", percent);
    power_event(AOS_POWER_CRITICAL, percent);
    vTaskDelay(pdMS_TO_TICKS(3000));

    aos_pmu_state_t pmu;
    if (aos_board_pmu_read(&pmu) && pmu.valid && pmu.usb_present) {
        ESP_LOGI(TAG, "usb arrived in time: staying up");
        s_shutting_down = false;
        s_critical_strikes = 0;
        return;
    }
    aos_hal_shutdown();
}

/* Every 200 ms: the PMU's interrupt line, through the expander. */
static void pmu_irq_service(void)
{
    uint32_t irq = aos_board_pmu_poll_irq();
    if (!irq) {
        return;
    }
    aos_pmu_state_t pmu;
    bool ok  = aos_board_pmu_read(&pmu) && pmu.valid;
    int  pct = ok ? pmu.percent : -1;
    ESP_LOGI(TAG, "pmu irq 0x%06lx at %d%%", (unsigned long)irq, pct);

    /* The power key. The chip decodes the gesture itself: a "negative edge"
     * is the press, "short" is a release before the long threshold, "long"
     * fires while still held. Holding on to the off threshold is the PMU's
     * own power-off and never reaches here. */
    if (irq & AXP2101_IRQ_PKEY_NEGATIVE) {
        if (s_button_cb) s_button_cb(AOS_BUTTON_PWR, AOS_BUTTON_PRESS);
    }
    if (irq & AXP2101_IRQ_PKEY_SHORT) {
        if (s_button_cb) s_button_cb(AOS_BUTTON_PWR, AOS_BUTTON_CLICK);
    }
    if (irq & AXP2101_IRQ_PKEY_LONG) {
        if (s_button_cb) s_button_cb(AOS_BUTTON_PWR, AOS_BUTTON_LONG);
    }

    if (irq & AXP2101_IRQ_VBUS_INSERT) usb_changed(true, pct);
    if (irq & AXP2101_IRQ_VBUS_REMOVE) usb_changed(false, pct);

    if ((irq & AXP2101_IRQ_CHG_DONE) && !s_charge_counted) {
        s_charge_counted = true;
        s_charge_cycles++;
        aos_hal_pref_set_i32("chg_cyc", (int32_t)s_charge_cycles);
        power_event(AOS_POWER_CHARGE_DONE, pct);
    }
    if ((irq & AXP2101_IRQ_SOC_WARN_LEVEL) && ok && !pmu.usb_present && !s_low_warned) {
        s_low_warned = true;
        power_event(AOS_POWER_LOW_BATTERY, pct);
    }
    if ((irq & AXP2101_IRQ_SOC_SHUTDOWN_LEVEL) && ok && !pmu.usb_present) {
        power_critical(pct);
    }
    if (irq & (AXP2101_IRQ_DIE_OVER_TEMP | AXP2101_IRQ_BAT_WORK_OVER_TEMP |
               AXP2101_IRQ_BAT_CHG_OVER_TEMP)) {
        ESP_LOGW(TAG, "PMU reports over-temperature (0x%06lx)", (unsigned long)irq);
        power_event(AOS_POWER_OVERHEAT, pct);
    }
    if (irq & AXP2101_IRQ_CHG_TIMEOUT) {
        ESP_LOGW(TAG, "charger safety timer expired: the cell is not taking charge");
    }
}

/* Every 5 s: what the IRQ line could have missed, the low-battery backstop,
 * the automatic power saving and the arithmetic above. */
static void power_watch(void)
{
    static int minute_ticks;
    aos_pmu_state_t pmu;
    if (!aos_board_pmu_read(&pmu) || !pmu.valid) {
        return;
    }
    int pct = pmu.percent;
    usb_changed(pmu.usb_present, pct);

    bool on_battery = !pmu.usb_present;
    bool was_saving = s_low_battery_saving;
    s_low_battery_saving = on_battery && pct >= 0 && pct <= AOS_LOW_BATTERY_SAVING_PCT;
    if (was_saving != s_low_battery_saving) {
        ESP_LOGI(TAG, "low-battery power saving %s", s_low_battery_saving ? "on" : "off");
    }

    /* Backstop for the PMU's shutdown IRQ: three strikes fifteen seconds
     * apart, and not during the first half minute, when the gauge is still
     * finding its feet. */
    if (on_battery && esp_timer_get_time() > 30 * 1000000LL &&
        ((pct >= 0 && pct <= AOS_LOW_BATTERY_OFF_PCT) || pmu.vbat < AOS_CRITICAL_VBAT)) {
        if (++s_critical_strikes >= 3) {
            power_critical(pct);
        }
    } else {
        s_critical_strikes = 0;
    }

    if (on_battery && ++minute_ticks >= 12) {
        minute_ticks = 0;
        s_battery_minutes++;
        if (++s_unsaved_minutes >= 10) {
            battery_stats_save();
        }
        if (s_unplug_us && s_unplug_pct >= 0 && pct >= 0) {
            float hours = (float)(esp_timer_get_time() - s_unplug_us) / 3600e6f;
            int   drop  = s_unplug_pct - pct;
            if (hours >= 0.25f && drop >= 2) {
                s_drain_pct_h = (float)drop / hours;
                s_hours_left  = (float)pct / s_drain_pct_h;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Background task: IMU, screen auto-dimming and wake on wrist raise.
 * -------------------------------------------------------------------------- */
static void housekeeping_task(void *arg)
{
    (void)arg;
    uint32_t hk_ticks = 0;

    while (1) {
        aos_board_imu_poll();
        button_poll();

        if (++hk_ticks % 125 == 0) {        /* 125 * 40 ms = 5 s */
            touch_keep_awake();
            power_watch();
        }
        if (hk_ticks % 5 == 0) {            /* 200 ms */
            pmu_irq_service();
        }
        pm_policy_apply();

        /* How much stack each of our tasks has to spare, in bytes (in ESP-IDF
         * the high water mark comes in bytes, not words). Useful for deciding
         * whether a stack can be shrunk, which is a better deal than sending
         * it to PSRAM: shrinking frees internal RAM and pays no cache. */
        if (hk_ticks % 1500 == 0) {         /* every minute */
            char extra[64] = "";
            if (s_player_task) {
                snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra),
                         " player=%u", (unsigned)uxTaskGetStackHighWaterMark(s_player_task));
            }
            if (s_mic_task) {
                snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra),
                         " mic=%u", (unsigned)uxTaskGetStackHighWaterMark(s_mic_task));
            }
            ESP_LOGI(TAG, "free stacks: hk=%u (of %d)%s",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL), HK_STACK, extra);
        }

        int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;

        if (aos_board_imu_wrist_raised()) {
            aos_hal_activity();
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        /* With the battery on its last legs, always-on is the first thing to
         * go. */
        bool aod_ok = s_aod_enabled;
        if (aod_ok) {
            aos_pmu_state_t pmu;
            if (aos_board_pmu_read(&pmu) && pmu.valid && !pmu.charging &&
                pmu.percent >= 0 && pmu.percent < AOD_LOW_BATTERY_PCT) {
                aod_ok = false;
            }
        }

        switch (s_display_state) {
        case AOS_DISPLAY_ACTIVE:
            if (aod_ok && idle_ms > AOD_TIMEOUT_MS) {
                aos_hal_display_set_state(AOS_DISPLAY_AOD);
            } else if (!aod_ok && idle_ms > OFF_NO_AOD_MS) {
                aos_hal_display_set_state(AOS_DISPLAY_OFF);
            }
            break;

        case AOS_DISPLAY_AOD:
            if (!aod_ok || idle_ms > OFF_TIMEOUT_MS) {
                aos_hal_display_set_state(AOS_DISPLAY_OFF);
            }
            break;

        case AOS_DISPLAY_OFF:
            break;
        }

        /* Screen off: nobody is waiting on the 40 ms cadence, and every wake
         * is a wake out of light sleep. */
        vTaskDelay(pdMS_TO_TICKS(s_display_state == AOS_DISPLAY_OFF ? 100 : 40));
    }
}

/* -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * Frame budget
 *
 * Without this, wrong causes get proposed: it already happened once that the
 * PSRAM buffers were blamed when the problem was the canvas stretch, and again
 * that the flush buffer was moved to internal RAM "because it made sense" and
 * the FPS dropped. The only way to know where the time goes is to measure it.
 *
 * LVGL reports the three stages of each refresh through display events:
 *
 *   REFR_START ..... RENDER_READY      drawing
 *   FLUSH_WAIT_START ... FLUSH_WAIT_FINISH   waiting for the panel
 *   ..... REFR_READY                   wrap-up; whatever is left until the
 *                                      next REFR_START is the gap, which is
 *                                      where the app's lv_timer runs
 *
 * Prints an average every 120 refreshes. To switch it off, do not call
 * perf_hook_install().
 * -------------------------------------------------------------------------- */

/* Flush buffer. It is changed one variable at a time while watching
 * bench_full_refresh(), which measures a full-screen refresh at startup
 * without depending on any app. History measured on this board:
 *
 *   20 rows, single, psram     rectangle 25.5 ms   canvas 46.2 ms
 *   20 rows, single, internal  rectangle 25.5 ms   canvas 46.2 ms
 *   60 rows, double, internal  rectangle 25.0 ms   canvas 45.1 ms
 *
 * That is: neither the size, nor the location, nor double buffering changes
 * anything. What stays is the variant that spends least of the scarce
 * resource, which is internal RAM. */
#define AOS_DRAW_ROWS       20
#define AOS_DRAW_DOUBLE      0

#define PERF_EVERY      120

static struct {
    int64_t refr_start, render_end, flush_start, flush_us, prev_end;
    int64_t acc_render, acc_flush, acc_rest, acc_gap, acc_total;
    int     n;
} s_perf;

static void perf_hook(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();

    switch (lv_event_get_code(e)) {
    case LV_EVENT_REFR_START:
        s_perf.refr_start = now;
        /* It is cleared because LVGL always sends REFR_START, but RENDER_READY
         * only when it really drew something. Without this, a clock that
         * repaints once a minute goes on adding the cost of that drawing to
         * every refresh and the budget does not add up to the total. */
        s_perf.render_end = 0;
        s_perf.flush_us = 0;
        break;

    case LV_EVENT_RENDER_READY:
        s_perf.render_end = now;
        break;

    case LV_EVENT_FLUSH_WAIT_START:
        s_perf.flush_start = now;
        break;

    case LV_EVENT_FLUSH_WAIT_FINISH:
        s_perf.flush_us += now - s_perf.flush_start;
        break;

    case LV_EVENT_REFR_READY: {
        /* if there was no drawing, the whole frame is "the rest" */
        int64_t rend = s_perf.render_end ? s_perf.render_end : s_perf.refr_start;

        if (s_perf.prev_end) {
            s_perf.acc_total  += now - s_perf.prev_end;
            s_perf.acc_gap    += s_perf.refr_start - s_perf.prev_end;
            s_perf.acc_render += rend - s_perf.refr_start;
            s_perf.acc_flush  += s_perf.flush_us;
            s_perf.acc_rest   += (now - rend) - s_perf.flush_us;
            s_perf.n++;
        }
        s_perf.prev_end = now;

        if (s_perf.n >= PERF_EVERY) {
            int n = s_perf.n;
            /* the four terms make up the total: if it does not add up, the measurement is wrong */
            ESP_LOGI(TAG,
                     "frame %.1f ms = draw %.1f + flush %.2f + rest %.1f + gap %.1f  (%.1f fps)",
                     (double)s_perf.acc_total  / n / 1000.0,
                     (double)s_perf.acc_render / n / 1000.0,
                     (double)s_perf.acc_flush  / n / 1000.0,
                     (double)s_perf.acc_rest   / n / 1000.0,
                     (double)s_perf.acc_gap    / n / 1000.0,
                     1000000.0 * n / (double)s_perf.acc_total);
            s_perf.acc_total = s_perf.acc_render = s_perf.acc_flush = 0;
            s_perf.acc_rest  = s_perf.acc_gap = 0;
            s_perf.n = 0;
        }
        break;
    }

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * What moving a screen costs
 *
 * The frame budget says drawing a full screen costs ~45 ms, and drawing a
 * game's canvas is one memcpy per row from PSRAM into the flush buffer's RAM.
 * 330 KB in 45 ms is 7 MB/s, and this chip has OCTAL PSRAM at 80 MHz: the bus
 * gives far more. So what costs is the copier, not the memory... or the model
 * is wrong.
 *
 * Measured once at startup, with the system idle, and separately:
 *
 *   - a single-shot copy, with the libc's memcpy: the bus ceiling.
 *   - a row-by-row copy (736 B), which is how LVGL really copies.
 *   - the same with lv_memcpy(), which is LVGL's own implementation in C and
 *     the one in use today (CONFIG_LV_USE_BUILTIN_STRING=y).
 *
 * With these three figures it is possible to tell whether moving LVGL to the
 * libc is worthwhile or whether something else has to be attacked.
 * -------------------------------------------------------------------------- */
static void bench_screen_copy(void)
{
    const int rows = 448, row_bytes = 368 * 2;
    const size_t total = (size_t)rows * row_bytes;

    uint8_t *src = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *dst = heap_caps_malloc((size_t)20 * row_bytes, MALLOC_CAP_INTERNAL);
    if (!src || !dst) {
        free(src);
        free(dst);
        return;
    }
    memset(src, 0x5A, total);

    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 20; i++) {
        memcpy(dst, src + (size_t)i * row_bytes * 20, (size_t)20 * row_bytes);
    }
    int64_t bulk = esp_timer_get_time() - t0;

    t0 = esp_timer_get_time();
    for (int y = 0; y < rows; y++) {
        memcpy(dst + (size_t)(y % 20) * row_bytes, src + (size_t)y * row_bytes, row_bytes);
    }
    int64_t per_row = esp_timer_get_time() - t0;

    t0 = esp_timer_get_time();
    for (int y = 0; y < rows; y++) {
        lv_memcpy(dst + (size_t)(y % 20) * row_bytes, src + (size_t)y * row_bytes, row_bytes);
    }
    int64_t per_row_lv = esp_timer_get_time() - t0;

    ESP_LOGI(TAG, "screen copy (%u KB): in one go %lld us (%.1f MB/s) | "
                  "row by row %lld us (%.1f MB/s) | lv_memcpy %lld us (%.1f MB/s)",
             (unsigned)(total / 1024),
             bulk, total / (double)bulk,
             per_row, total / (double)per_row,
             per_row_lv, total / (double)per_row_lv);

    free(src);
    free(dst);
}

/* What a full-screen refresh costs, without depending on any app.
 *
 * The same thing is drawn twice with two different objects and compared:
 *
 *   - a plain rectangle: measures the refresh machinery (splitting the screen
 *     into flush-buffer-sized chunks, building the draw tasks, flushing).
 *   - an RGB565 canvas the size of the screen, which is what a game uses.
 *
 * If the two come out similar, the machinery is what is expensive and the
 * flush buffer size is what to look at. If the canvas comes out much higher,
 * LVGL's image path is what is expensive. And we already know that copying the
 * 322 KB is 4 ms, so any figure well above that is overhead, not memory. */
static void bench_full_refresh(void)
{
    if (!s_display || !aos_hal_lock(2000)) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    const int N = 8;

    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_pos(box, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x102030), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < N; i++) {
        lv_obj_invalidate(box);
        lv_refr_now(s_display);
    }
    int64_t rect_us = (esp_timer_get_time() - t0) / N;
    lv_obj_delete(box);

    int64_t canvas_us = -1, canvas_r0_us = -1;
    int32_t radius = -1;
    size_t bytes = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    uint16_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
        memset(buf, 0x33, bytes);
        lv_obj_t *cv = lv_canvas_create(scr);
        lv_canvas_set_buffer(cv, buf, BSP_LCD_H_RES, BSP_LCD_V_RES,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(cv, BSP_LCD_H_RES, BSP_LCD_V_RES);
        lv_obj_set_pos(cv, 0, 0);
        lv_image_set_antialias(cv, false);

        t0 = esp_timer_get_time();
        for (int i = 0; i < N; i++) {
            lv_obj_invalidate(cv);
            lv_refr_now(s_display);
        }
        canvas_us = (esp_timer_get_time() - t0) / N;

        /* lv_image takes clip_radius from the object's radius style, and with
         * a non-zero radius the blit goes through per-pixel masking instead of
         * copying the whole row. A game's canvas never wants a radius. */
        radius = lv_obj_get_style_radius(cv, LV_PART_MAIN);
        lv_obj_set_style_radius(cv, 0, 0);
        t0 = esp_timer_get_time();
        for (int i = 0; i < N; i++) {
            lv_obj_invalidate(cv);
            lv_refr_now(s_display);
        }
        canvas_r0_us = (esp_timer_get_time() - t0) / N;

        lv_obj_delete(cv);
        free(buf);
    }

    /* Same canvas, same size, different memory: it separates "reading from
     * PSRAM" from "LVGL's image path". A quarter of the screen is used because
     * a whole one does not fit in internal RAM. */
    int64_t quarter_psram = -1, quarter_int = -1;
    const int qh = 112;
    size_t qbytes = (size_t)BSP_LCD_H_RES * qh * 2;

    for (int pass = 0; pass < 2; pass++) {
        uint16_t *qb = heap_caps_malloc(qbytes, pass == 0
                                        ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                        : MALLOC_CAP_INTERNAL);
        if (!qb) {
            continue;
        }
        memset(qb, 0x44, qbytes);
        lv_obj_t *qc = lv_canvas_create(scr);
        lv_canvas_set_buffer(qc, qb, BSP_LCD_H_RES, qh, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_size(qc, BSP_LCD_H_RES, qh);
        lv_obj_set_pos(qc, 0, 0);
        lv_image_set_antialias(qc, false);

        int64_t tq = esp_timer_get_time();
        for (int i = 0; i < N; i++) {
            lv_obj_invalidate(qc);
            lv_refr_now(s_display);
        }
        int64_t got = (esp_timer_get_time() - tq) / N;
        if (pass == 0) quarter_psram = got; else quarter_int = got;

        lv_obj_delete(qc);
        free(qb);
    }

    lv_refr_now(s_display);
    aos_hal_unlock();

    ESP_LOGI(TAG, "canvas of %d rows: in psram %.1f ms | in internal ram %.1f ms",
             qh, quarter_psram / 1000.0, quarter_int / 1000.0);

    ESP_LOGI(TAG, "full refresh: rectangle %.1f ms | canvas %.1f ms | "
                  "canvas radius 0: %.1f ms (the theme gives it radius %d) | "
                  "buffer %d rows %s in psram | qspi %d MHz",
             rect_us / 1000.0, canvas_us / 1000.0, canvas_r0_us / 1000.0,
             (int)radius, AOS_DRAW_ROWS,
             AOS_DRAW_DOUBLE ? "double" : "single",
             (int)(AOS_LCD_PCLK_HZ / 1000000));
}

static void perf_hook_install(lv_display_t *display)
{
    static const lv_event_code_t codes[] = {
        LV_EVENT_REFR_START, LV_EVENT_RENDER_READY,
        LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH,
        LV_EVENT_REFR_READY,
    };
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        lv_display_add_event_cb(display, perf_hook, codes[i], NULL);
    }
}


/* --------------------------------------------------------------------------
 * Display startup
 *
 * This used to be done by bsp_display_start_with_config(). It is redone here
 * because of two things that are in the BSP's code and cannot be seen from
 * outside:
 *
 * 1. THE BSP REGISTERS THIS PANEL AS IF IT WERE RGB, AND IT IS NOT.
 *    bsp_display_lcd_init() registers it with lvgl_port_add_disp_rgb(), which
 *    marks the display as LVGL_PORT_DISP_TYPE_RGB. And for that type,
 *    lvgl_port_flush_callback() calls lv_disp_flush_ready() RIGHT AFTER
 *    esp_lcd_panel_draw_bitmap(). On an RGB panel that is fine: the write goes
 *    straight into the framebuffer and that is that. Ours is QSPI, and there
 *    draw_bitmap QUEUES a DMA transfer and returns at once -which is why the
 *    driver's error says "spi transmit (QUEUE) color failed"-. With a single
 *    draw buffer, LVGL starts rendering the next chunk ON TOP of the one still
 *    being sent, and the panel receives a mixture of the two.
 *
 *    On screen that shows up as isolated pixels carrying the colour of what
 *    was there before, over the trail of whatever has just moved. And they do
 *    not correct themselves, because LVGL considers that area drawn. On a
 *    black background they are invisible; on Truco's green baize they appeared
 *    at once, as lines of dots following the path of the cards.
 *
 *    lvgl_port_add_disp() -the generic path, for panels with an io_handle-
 *    registers on_color_trans_done and considers the flush finished when the
 *    DMA really has finished. It is the same port and the same panel: only who
 *    announces that the buffer is free changes.
 *
 * 2. THE BSP IGNORES THE CONFIGURATION IT IS GIVEN.
 *    bsp_display_start_with_config() takes a bsp_display_cfg_t and keeps it to
 *    itself: bsp_display_lcd_init() does not receive it and builds its own
 *    from the Kconfig values (CONFIG_BSP_DISPLAY_LVGL_BUF_HEIGHT, 24 rows).
 *    Which means AOS_DRAW_ROWS, AOS_DRAW_DOUBLE and this file's DMA/PSRAM
 *    flags NEVER had any effect, and the benchmark that concluded "neither the
 *    size nor the location nor double buffering changes anything" was
 *    measuring the same configuration every time. They do count now, so if
 *    they are touched again they have to be measured again.
 * -------------------------------------------------------------------------- */

/* The controller wants windows of EVEN width and height. The BSP did this in
 * its own callback; since we no longer go through it, it goes here.
 *
 * The BSP's copied the coordinates into a uint16_t before rounding, so an area
 * with a negative x1 -an object poking out of the left edge- turned into a
 * huge number. With signed integers, & ~1 and | 1 round outwards in both
 * directions, which is what is needed. */
static void display_round_area_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    if (!area) {
        return;
    }
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static uint32_t lvgl_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static lv_display_t *display_start(void)
{
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
#ifdef AOS_AUDIT_LVGL_STACK16
    /* RAM audit (E1): 20 KB measured at a 7.6 KB peak after opening photos
     * (the tjpgd path), settings, the calendar and the launcher; 16 KB keeps
     * more than the whole observed peak as margin. */
    port_cfg.task_stack = 16 * 1024;
#else
    port_cfg.task_stack = 20 * 1024;
#endif
    /* The port's tick is a periodic esp_timer, 5 ms from the factory, and a
     * timer every 5 ms is a wake-up every 5 ms: with it, the chip never gets
     * the 8 idle ms tickless idle asks for before it sleeps. LVGL 9 can take
     * its tick from a callback instead, so the tick comes from esp_timer's
     * clock (which light sleep keeps right) and the port's timer is left
     * ticking once every 100 ms, where it only touches a counter nobody
     * reads. */
    port_cfg.timer_period_ms = 100;
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed");
        return NULL;
    }
    lv_tick_set_cb(lvgl_tick_ms);

    esp_lcd_panel_handle_t    panel = NULL;
    esp_lcd_panel_io_handle_t io    = NULL;
    bsp_display_config_t      hw    = {0};
    if (bsp_display_new(&hw, &panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "bsp_display_new failed");
        return NULL;
    }
    s_panel    = panel;
    s_panel_io = io;

    /* Light sleep isolates every GPIO (ESP_SLEEP_GPIO_RESET_WORKAROUND):
     * chip select and the QSPI lines float while the chip sleeps, and the
     * panel reads the noise as commands. Measured: after the first real light
     * sleep the panel came back dead (TE stopped) until the next init. These
     * six keep their normal configuration through sleep instead. */
    const gpio_num_t lcd_pins[] = { BSP_LCD_CS, BSP_LCD_PCLK, BSP_LCD_DATA0,
                                    BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3 };
    for (unsigned i = 0; i < sizeof(lcd_pins) / sizeof(lcd_pins[0]); i++) {
        gpio_sleep_sel_dis(lcd_pins[i]);
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = io,
        .panel_handle  = panel,
        .buffer_size   = BSP_LCD_H_RES * AOS_DRAW_ROWS,
        .double_buffer = AOS_DRAW_DOUBLE,
        .hres          = BSP_LCD_H_RES,
        .vres          = BSP_LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            /* The panel wants RGB565 the other way round; the BSP did this too. */
            .swap_bytes  = true,
            /* No software rotation: the screen is born portrait, and asking
             * for it allocates one more buffer that is never used. */
            .sw_rotate   = false,
            /* THE DRAW BUFFER GOES IN DMA-CAPABLE INTERNAL RAM, and it is not
             * for speed: it is so the flush cannot fail.
             *
             * With the buffer in PSRAM -which is where it landed with both
             * flags false- spi_master cannot send from there and builds a
             * bounce buffer in internal RAM ON EVERY TRANSACTION
             * (setup_dma_priv_buffer). It is the same 14 KB, but asked for
             * forty times a second and at the worst possible moment. When
             * internal RAM gets tight that allocation fails, and that is where
             * the disaster starts:
             *
             *   E spi_master: setup_dma_priv_buffer: Failed to allocate priv TX buffer
             *   E lcd_panel.io.spi: spi transmit (queue) color failed
             *   E co5300_spi: send color data failed
             *   E task_wdt: CPU 0: taskLVGL -> Aborting -> Rebooting
             *
             * The reboot comes from LVGL's port IGNORING the error from
             * esp_lcd_panel_draw_bitmap(), and for an SPI panel the
             * lv_display_flush_ready() is given by the end-of-DMA callback. If
             * the transaction was never queued, that callback never arrives
             * and LVGL spins in wait_for_flushing() forever (decoded
             * backtrace: lv_refr.c:1454). In other words, running out of
             * internal RAM by 14 KB did not degrade the image: it killed the
             * watch.
             *
             * Measured on 2026-09-08 with BLE advertising (~110 KB of internal
             * RAM) and opening Clima, which also brings up TLS: it happened
             * three times out of three. With the buffer here, there is no
             * bounce buffer to allocate and the path that failed ceases to
             * exist.
             *
             * It costs a fixed 14 KB of internal RAM, asked for at startup
             * -when there is plenty- instead of 14 KB asked for on every frame
             * when it is scarce. It is the same reasoning as aos_dynapp's code
             * reservation: reserve early what cannot be asked for later.
             *
             * Beware the old note in HANDOFF-APPS.md saying this "was already
             * tried and is not enough": that test dates from when the BSP
             * ignored the entire configuration (see point 2 above), so the
             * flag never actually got applied and the conclusion does not
             * hold. */
            .buff_dma    = true,
            .buff_spiram = false,
        },
    };

    /* Where the draw buffer lands cannot be deduced from the flags: it is
     * measured and stated, this file having already had a whole benchmark
     * measuring a configuration that was not being applied. */
    size_t int_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t ext_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return NULL;
    }

    int int_used = (int)int_before - (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int ext_used = (int)ext_before - (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGW(TAG, "drawing buffer: %d rows x %d px = %d KB asked for; "
             "spent %d KB of internal RAM and %d KB of PSRAM",
             AOS_DRAW_ROWS, BSP_LCD_H_RES,
             (int)(BSP_LCD_H_RES * AOS_DRAW_ROWS * 2 * (AOS_DRAW_DOUBLE ? 2 : 1)) / 1024,
             int_used / 1024, ext_used / 1024);
    lv_display_add_event_cb(disp, display_round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    esp_lcd_touch_handle_t tp = NULL;
    if (bsp_touch_new(NULL, &tp) == ESP_OK && tp) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = disp,
            .handle = tp,
        };
        if (!lvgl_port_add_touch(&touch_cfg)) {
            ESP_LOGE(TAG, "lvgl_port_add_touch failed");
        }
    } else {
        ESP_LOGE(TAG, "bsp_touch_new failed");
    }

    bsp_display_brightness_init();
    ESP_LOGI(TAG, "display: %d buffer rows, %s, DMA flush with a real completion signal",
             AOS_DRAW_ROWS, AOS_DRAW_DOUBLE ? "double" : "single");
    return disp;
}

bool aos_hal_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(bsp_i2c_init());
    aos_board_init();
    snprintf(s_board_name, sizeof(s_board_name), "%s", aos_board_variant_name());

    {
        int32_t saved;
        if (aos_hal_pref_get_i32("pwr_save", &saved))  s_power_saving        = (saved != 0);
        if (aos_hal_pref_get_i32("batt_care", &saved)) s_battery_care        = (saved != 0);
        if (aos_hal_pref_get_i32("panel_slp", &saved)) s_panel_sleep_enabled = (saved != 0);
        if (aos_hal_pref_get_i32("light_slp", &saved)) s_light_sleep_enabled = (saved != 0);
        if (aos_hal_pref_get_i32("bat_min", &saved))   s_battery_minutes     = (uint32_t)saved;
        if (aos_hal_pref_get_i32("chg_cyc", &saved))   s_charge_cycles       = (uint32_t)saved;

        const aos_pmu_config_t pmu_cfg = {
            .charge_ma      = s_battery_care ? AOS_CHARGE_MA_CARE : AOS_CHARGE_MA_FULL,
            .precharge_ma   = AOS_PRECHARGE_MA,
            .termination_ma = AOS_TERMINATION_MA,
            .target_mv      = s_battery_care ? AOS_CHARGE_MV_CARE : AOS_CHARGE_MV_FULL,
            .warn_pct       = AOS_LOW_BATTERY_WARN_PCT,
            .shutdown_pct   = AOS_LOW_BATTERY_OFF_PCT,
            .poweroff_mv    = AOS_POWEROFF_MV,
        };
        aos_board_pmu_configure(&pmu_cfg);
        aos_board_pmu_dump();

        aos_pmu_state_t pmu;
        if (aos_board_pmu_read(&pmu) && pmu.valid) {
            s_usb_last = pmu.usb_present;
            if (!pmu.usb_present) {
                s_unplug_us  = esp_timer_get_time();
                s_unplug_pct = pmu.percent;
            }
        }
    }

    bsp_spiffs_mount();
    s_sd_mounted = (bsp_sdcard_mount() == ESP_OK);
    ESP_LOGI(TAG, "microSD %s", s_sd_mounted ? "mounted" : "not available");

    /* Time: the RTC's rules until somebody synchronises over NTP. */
    aos_hal_timezone_set(aos_hal_timezone_get());
    struct tm rtc_time;
    if (aos_board_rtc_get(&rtc_time) && rtc_time.tm_year > 100) {
        struct tm copy = rtc_time;
        time_t epoch = mktime(&copy);
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
    }

    /* the tone queue and its task: aos_hal_beep() only enqueues */
    s_tone_queue = xQueueCreate(TONE_QUEUE_LEN, sizeof(tone_note_t));
    if (s_tone_queue) {
        /* Priority 6, above the LVGL task (4): with drawing at 100% CPU and
         * the same priority, this task got no turn and the notes came out
         * mute. Audio cannot depend on how long a frame takes to draw. */
        AOS_XTASKCREATE(tone_task, "aos_tone", 3072, NULL, 6, NULL);
    }

    int32_t saved = 0;
    if (aos_hal_pref_get_i32("bright", &saved)) {
        s_brightness = (int)saved;
    }
    if (aos_hal_pref_get_i32("volume", &saved)) {
        s_volume = (int)saved;
    }
    if (aos_hal_pref_get_i32("aod", &saved)) {
        s_aod_enabled = (saved != 0);
    }
    if (aos_hal_pref_get_i32("aod_bright", &saved)) {
        s_aod_brightness = (int)saved;
    }


    /* The LVGL task's default stack (ESP_LVGL_PORT_INIT_CONFIG) is 7168 bytes,
     * and that is not enough: decoder_info() in lv_tjpgd.c allocates its work
     * buffer ON THE STACK (uint8_t workb[4096] + JDEC jd, nearly 4.7 KB in a
     * single frame). LVGL calls it when opening any image or layer, trying
     * every registered decoder, and on an already deep drawing path the stack
     * pointer runs past the limit and lands inside the task's own TCB, which
     * sits 16 bytes below. It stomps on list pointers and the reent: the board
     * dies later, in the scheduler or in a printf, with no apparent relation
     * to the drawing.
     *
     * Confirmed with a hardware watchpoint on the TCB's _stdout:
     *   lv_draw_rect -> lv_draw_dispatch -> lv_draw_sw_layer -> lv_draw_sw_image
     *     -> lv_image_decoder_open -> decoder_info (lv_tjpgd.c:122)
     *
     * That is why only claudito, 2043 and gemas failed (canvas and images) and
     * not flappy or recorder, which use ordinary widgets. display_start()
     * raises it. */
    aos_board_panel_hw_reset();
    s_display = display_start();

    if (!s_display) {
        ESP_LOGE(TAG, "could not start the display");
        return false;
    }
    bsp_display_brightness_set(s_brightness);
    perf_hook_install(s_display);
    bench_screen_copy();
    bench_full_refresh();
    touch_disable_autosleep();

    esp_err_t audio_ret = bsp_audio_init(NULL);
    if (audio_ret == ESP_OK) {
        s_speaker = bsp_audio_codec_speaker_init();
        s_mic     = bsp_audio_codec_microphone_init();
    }
    ESP_LOGI(TAG, "audio: init=%s speaker=%p mic=%p volume=%d",
             esp_err_to_name(audio_ret), s_speaker, s_mic, s_volume);

    /* The recordings folder has to exist before anybody records: fopen() does
     * not create directories. */
    mkdir(aos_hal_path_recordings(), 0777);

    const gpio_config_t boot_button = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&boot_button);

    /* What wakes the chip from light sleep: the touch controller's INT line
     * (GPIO21, pulses low on a finger) and the BOOT button. The PMU's IRQ
     * cannot: it ends on the expander (see aos_board.c), and it is polled
     * whenever the chip is awake anyway. */
    gpio_wakeup_enable(BSP_LCD_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(BOOT_BUTTON_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    /* The BSP enables both I2S channels at init and each enabled channel
     * holds an APB_FREQ_MAX pm lock, which forbids light sleep for good. The
     * codec's data interface disables the channel on close, so one open and
     * close per device at boot puts them in the state they are in after any
     * real use: off until needed. */
    if (s_speaker) {
        esp_codec_dev_sample_info_t fs = { .sample_rate = 16000, .channel = 1, .bits_per_sample = 16 };
        if (esp_codec_dev_open(s_speaker, &fs) == ESP_OK) esp_codec_dev_close(s_speaker);
    }
    if (s_mic) {
        esp_codec_dev_sample_info_t fs = { .sample_rate = 16000, .channel = 1, .bits_per_sample = 16 };
        if (esp_codec_dev_open(s_mic, &fs) == ESP_OK) esp_codec_dev_close(s_mic);
    }

    /* Dynamic frequency scaling: the lock is taken here and released by the
     * policy when there is nothing to draw. esp_pm_configure fails harmlessly
     * when CONFIG_PM_ENABLE is off, and then the clock is what it always was. */
    {
        esp_pm_config_t pm = {
            .max_freq_mhz = AOS_DFS_MAX_MHZ,
            .min_freq_mhz = AOS_DFS_MIN_MHZ,
            .light_sleep_enable = false,
        };
        esp_err_t pm_ret = esp_pm_configure(&pm);
        if (pm_ret == ESP_OK &&
            esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "aos_ui", &s_pm_max_lock) == ESP_OK) {
            esp_pm_lock_acquire(s_pm_max_lock);
            s_pm_max_held = true;
            ESP_LOGI(TAG, "DFS %d..%d MHz, power saving %s, battery care %s, panel sleep %s",
                     AOS_DFS_MIN_MHZ, AOS_DFS_MAX_MHZ,
                     s_power_saving ? "on" : "off", s_battery_care ? "on" : "off",
                     s_panel_sleep_enabled ? "on" : "off");
        } else {
            s_pm_max_lock = NULL;
            ESP_LOGW(TAG, "DFS not available (%s): the CPU stays at %d MHz",
                     esp_err_to_name(pm_ret), cpu_mhz_now());
        }
    }

    s_last_activity_us = esp_timer_get_time();
    xTaskCreate(housekeeping_task, "aos_hk", HK_STACK, NULL, 4, NULL);

    if (aos_hal_net_enabled()) {
        aos_hal_net_enable(true);
    } else {
        ESP_LOGI(TAG, "wifi off by preference; the stack is not brought up");
        s_net_state = AOS_NET_OFF;
    }

    /* Bluetooth, like WiFi, starts as it was left. And by default it is left
     * OFF -aos_hal_bt_enabled() returns false if nothing was ever stored-,
     * which is the right thing: it costs 30 KB of the memory dynamic apps come
     * out of, and a watch with no paired phone gains nothing by having it on.
     * It is switched on from Settings, which is also where pairing happens. */
    if (aos_hal_bt_enabled()) {
        if (!aos_ble_start()) {
            ESP_LOGE(TAG, "could not bring up the BLE stack");
        }
    } else {
        ESP_LOGI(TAG, "bluetooth off by preference");
    }
    return true;
}
