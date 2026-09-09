/*
 * AmoledOS - Orchestration of the board's own peripherals.
 */
#include "aos_board.h"
#include "axp2101.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_io_expander.h"
#include "driver/i2c_master.h"

#include <math.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "aos_board";

/* defined in pcf85063.c and qmi8658.c */
bool aos_rtc_start(i2c_master_bus_handle_t bus);
bool aos_imu_start(i2c_master_bus_handle_t bus);

static axp2101_t           s_pmu;
static bool                s_pmu_ready;
static aos_board_variant_t s_variant = AOS_BOARD_UNKNOWN;
static esp_io_expander_handle_t s_expander;
static char                s_power_on_reason[48]  = "unknown";
static char                s_power_off_reason[48] = "unknown";

/* I2C addresses of the two possible touch controllers. The rest of the board
 * is identical between revisions, so this is enough to know which one we are
 * running on. */
#define TOUCH_ADDR_CST816   0x15
#define TOUCH_ADDR_FT3168   0x38

/* The TCA9554's pins, from the schematic. Only the two the PMU uses are
 * touched here; the display's reset lines are left exactly as found. */
#define EXIO_LCD_RESET      IO_EXPANDER_PIN_NUM_0   /* LCD_RESET, active low           */
#define EXIO_POWER_KEY      IO_EXPANDER_PIN_NUM_4   /* SYS_OUT: high while PWR is held */
#define EXIO_PMU_IRQ        IO_EXPANDER_PIN_NUM_5   /* AXP_IRQ: active low             */

static void detect_variant(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, TOUCH_ADDR_CST816, 100) == ESP_OK) {
        s_variant = AOS_BOARD_V2_CO5300_CST816;
    } else if (i2c_master_probe(bus, TOUCH_ADDR_FT3168, 100) == ESP_OK) {
        s_variant = AOS_BOARD_V1_SH8601_FT3168;
    } else {
        s_variant = AOS_BOARD_UNKNOWN;
    }
    ESP_LOGI(TAG, "variant detected: %s", aos_board_variant_name());
}

esp_err_t aos_board_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "the BSP I2C bus is not initialised");
        return ESP_ERR_INVALID_STATE;
    }

    detect_variant(bus);

    s_pmu_ready = (axp2101_init(&s_pmu, bus) == ESP_OK);
    if (!s_pmu_ready) {
        ESP_LOGW(TAG, "AXP2101 does not answer: the battery is reported as unknown");
    } else {
        /* The reasons are read ONCE, before anything else clears them. 0x21
         * is what says "the battery ran out last night". */
        uint8_t on  = axp2101_power_on_source(&s_pmu);
        uint8_t off = axp2101_power_off_source(&s_pmu);
        snprintf(s_power_on_reason,  sizeof(s_power_on_reason),  "%s", axp2101_power_on_source_name(on));
        snprintf(s_power_off_reason, sizeof(s_power_off_reason), "%s", axp2101_power_off_source_name(off));
        ESP_LOGI(TAG, "PMU powered on by: %s; last power-off: %s",
                 s_power_on_reason, s_power_off_reason);
    }

    /* The expander carries the PMU's IRQ line and the power key's level. The
     * BSP creates it lazily; both pins are inputs from reset, this only
     * makes it explicit. */
    s_expander = bsp_io_expander_init();
    if (s_expander) {
        esp_io_expander_set_dir(s_expander, EXIO_POWER_KEY | EXIO_PMU_IRQ, IO_EXPANDER_INPUT);
    } else {
        ESP_LOGW(TAG, "TCA9554 does not answer: no PMU interrupts, no power key");
    }

    aos_rtc_start(bus);
    aos_imu_start(bus);
    return ESP_OK;
}

aos_board_variant_t aos_board_variant(void)
{
    return s_variant;
}

const char *aos_board_variant_name(void)
{
    switch (s_variant) {
    case AOS_BOARD_V1_SH8601_FT3168: return "v1 (SH8601 + FT3168)";
    case AOS_BOARD_V2_CO5300_CST816: return "v2 (CO5300 + CST816)";
    default:                         return "desconocida";
    }
}

/* -------------------------------------------------------------------------- */
/* PMU                                                                         */
/* -------------------------------------------------------------------------- */

bool aos_board_pmu_read(aos_pmu_state_t *out)
{
    if (!out) {
        return false;
    }
    if (!s_pmu_ready) {
        out->valid = false;
        out->percent = -1;
        return false;
    }

    out->percent           = axp2101_battery_percent(&s_pmu);
    out->vbat              = axp2101_battery_voltage(&s_pmu);
    out->vbus              = axp2101_vbus_voltage(&s_pmu);
    out->vsys              = axp2101_system_voltage(&s_pmu);
    out->temperature       = axp2101_die_temperature(&s_pmu);
    out->board_temperature = axp2101_ts_temperature(&s_pmu);
    out->charge_state      = (int)axp2101_charge_state(&s_pmu);
    out->charging          = axp2101_is_charging(&s_pmu);
    out->usb_present       = axp2101_is_vbus_present(&s_pmu);
    out->battery_present   = axp2101_battery_present(&s_pmu);
    out->valid             = true;
    return true;
}

void aos_board_pmu_shutdown(void)
{
    if (s_pmu_ready) {
        axp2101_shutdown(&s_pmu);
    }
}

esp_err_t aos_board_pmu_configure(const aos_pmu_config_t *cfg)
{
    if (!s_pmu_ready || !cfg) {
        return ESP_ERR_INVALID_STATE;
    }
    axp2101_charge_current_set(&s_pmu, cfg->charge_ma);
    axp2101_precharge_current_set(&s_pmu, cfg->precharge_ma);
    axp2101_termination_current_set(&s_pmu, cfg->termination_ma);
    axp2101_charge_target_set(&s_pmu, cfg->target_mv);
    axp2101_low_battery_levels_set(&s_pmu, cfg->warn_pct, cfg->shutdown_pct);
    axp2101_poweroff_voltage_set(&s_pmu, cfg->poweroff_mv);
    axp2101_charging_enable(&s_pmu, true);

    /* Regulators with nothing on them, measured on 2026-09-09 by switching
     * each one off, rebooting and checking the panel's TE line, the
     * accelerometer, the microphone and (by ear) the speaker. The AMOLED
     * needs ALDO1-4 and BLDO2 - its TE signal stops when any of them goes -
     * and DCDC1 is everything else. These seven feed nothing on this board
     * and stay off. The PMU keeps rail states across an ESP32 reset, so this
     * is also what protects a reboot from inheriting a stray experiment. */
    static const axp2101_rail_t unused[] = {
        AXP2101_RAIL_DCDC2, AXP2101_RAIL_DCDC3, AXP2101_RAIL_DCDC4,
        AXP2101_RAIL_BLDO1, AXP2101_RAIL_CPUSLDO,
        AXP2101_RAIL_DLDO1, AXP2101_RAIL_DLDO2,
    };
    static const axp2101_rail_t panel[] = {
        AXP2101_RAIL_ALDO1, AXP2101_RAIL_ALDO2, AXP2101_RAIL_ALDO3,
        AXP2101_RAIL_ALDO4, AXP2101_RAIL_BLDO2,
    };
    for (unsigned i = 0; i < sizeof(unused) / sizeof(unused[0]); i++) {
        axp2101_rail_enable(&s_pmu, unused[i], false);
    }
    for (unsigned i = 0; i < sizeof(panel) / sizeof(panel[0]); i++) {
        axp2101_rail_enable(&s_pmu, panel[i], true);
    }

    /* What we want to hear about. Everything else (gauge watchdog, "new SOC"
     * every percent, battery insert/remove on a soldered pack) would only be
     * noise on a polled line. */
    axp2101_irq_read_clear(&s_pmu);
    axp2101_irq_enable(&s_pmu,
                       AXP2101_IRQ_SOC_SHUTDOWN_LEVEL | AXP2101_IRQ_SOC_WARN_LEVEL |
                       AXP2101_IRQ_BAT_WORK_OVER_TEMP | AXP2101_IRQ_BAT_CHG_OVER_TEMP |
                       AXP2101_IRQ_VBUS_INSERT | AXP2101_IRQ_VBUS_REMOVE |
                       AXP2101_IRQ_PKEY_SHORT | AXP2101_IRQ_PKEY_LONG |
                       AXP2101_IRQ_PKEY_NEGATIVE | AXP2101_IRQ_PKEY_POSITIVE |
                       AXP2101_IRQ_CHG_DONE | AXP2101_IRQ_CHG_START |
                       AXP2101_IRQ_DIE_OVER_TEMP | AXP2101_IRQ_CHG_TIMEOUT |
                       AXP2101_IRQ_BAT_OVER_VOLTAGE);
    return ESP_OK;
}

esp_err_t aos_board_pmu_charge_target_set(int mv)
{
    return s_pmu_ready ? axp2101_charge_target_set(&s_pmu, mv) : ESP_ERR_INVALID_STATE;
}

esp_err_t aos_board_pmu_charge_current_set(int ma)
{
    return s_pmu_ready ? axp2101_charge_current_set(&s_pmu, ma) : ESP_ERR_INVALID_STATE;
}

bool aos_board_pmu_charger_get(aos_pmu_charger_t *out)
{
    if (!out || !s_pmu_ready) {
        return false;
    }
    out->charge_ma      = axp2101_charge_current_ma(&s_pmu);
    out->precharge_ma   = axp2101_precharge_current_ma(&s_pmu);
    out->termination_ma = axp2101_termination_current_ma(&s_pmu);
    out->target_mv      = axp2101_charge_target_mv(&s_pmu);
    out->poweroff_mv    = axp2101_poweroff_voltage_mv(&s_pmu);
    axp2101_low_battery_levels_get(&s_pmu, &out->warn_pct, &out->shutdown_pct);
    int on_ms;
    axp2101_power_key_timing_get(&s_pmu, &on_ms, &out->key_long_ms, &out->key_off_ms);
    return true;
}

const char *aos_board_pmu_power_on_reason(void)
{
    return s_power_on_reason;
}

const char *aos_board_pmu_power_off_reason(void)
{
    return s_power_off_reason;
}

uint32_t aos_board_pmu_poll_irq(void)
{
    if (!s_pmu_ready || !s_expander) {
        return 0;
    }
    uint32_t levels = 0;
    if (esp_io_expander_get_level(s_expander, EXIO_PMU_IRQ, &levels) != ESP_OK) {
        return 0;
    }
    if (levels & EXIO_PMU_IRQ) {
        return 0;                       /* line high: nothing pending */
    }
    return axp2101_irq_read_clear(&s_pmu);
}

bool aos_board_power_key_down(void)
{
    if (!s_expander) {
        return false;
    }
    uint32_t levels = 0;
    if (esp_io_expander_get_level(s_expander, EXIO_POWER_KEY, &levels) != ESP_OK) {
        return false;
    }
    return (levels & EXIO_POWER_KEY) != 0;
}

void aos_board_pmu_dump(void)
{
    if (s_pmu_ready) {
        axp2101_dump(&s_pmu);
    }
}

int aos_board_pmu_rail_count(void)
{
    return AXP2101_RAIL_COUNT;
}

bool aos_board_pmu_rail_get(int idx, const char **name, bool *on, int *mv)
{
    if (!s_pmu_ready || idx < 0 || idx >= AXP2101_RAIL_COUNT) {
        return false;
    }
    if (name) *name = axp2101_rail_name((axp2101_rail_t)idx);
    if (on)   *on   = axp2101_rail_is_enabled(&s_pmu, (axp2101_rail_t)idx);
    if (mv)   *mv   = axp2101_rail_voltage_mv(&s_pmu, (axp2101_rail_t)idx);
    return true;
}

int aos_board_pmu_rail_find(const char *name)
{
    for (int i = 0; name && i < AXP2101_RAIL_COUNT; i++) {
        if (strcasecmp(name, axp2101_rail_name((axp2101_rail_t)i)) == 0) {
            return i;
        }
    }
    return -1;
}

esp_err_t aos_board_pmu_rail_set(int idx, bool on)
{
    if (!s_pmu_ready || idx < 0 || idx >= AXP2101_RAIL_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = axp2101_rail_enable(&s_pmu, (axp2101_rail_t)idx, on);
    ESP_LOGW(TAG, "rail %s -> %s (%s)", axp2101_rail_name((axp2101_rail_t)idx),
             on ? "on" : "OFF", esp_err_to_name(ret));
    return ret;
}

int aos_board_pmu_register_read(uint8_t reg)
{
    return s_pmu_ready ? axp2101_register_read(&s_pmu, reg) : -1;
}

esp_err_t aos_board_pmu_register_write(uint8_t reg, uint8_t value)
{
    if (!s_pmu_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "pmu reg 0x%02X <- 0x%02X", reg, value);
    return axp2101_register_write(&s_pmu, reg, value);
}

float aos_board_pmu_ts_voltage(void)
{
    return s_pmu_ready ? axp2101_ts_voltage(&s_pmu) : 0.0f;
}

void aos_board_panel_hw_reset(void)
{
    if (!s_expander) {
        return;
    }
    esp_io_expander_set_dir(s_expander, EXIO_LCD_RESET, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(s_expander, EXIO_LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_io_expander_set_level(s_expander, EXIO_LCD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_io_expander_set_level(s_expander, EXIO_LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "panel hardware reset through EXIO0");
}
