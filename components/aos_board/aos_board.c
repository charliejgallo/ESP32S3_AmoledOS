/*
 * AmoledOS - Orchestration of the board's own peripherals.
 */
#include "aos_board.h"
#include "axp2101.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

static const char *TAG = "aos_board";

/* defined in pcf85063.c and qmi8658.c */
bool aos_rtc_start(i2c_master_bus_handle_t bus);
bool aos_imu_start(i2c_master_bus_handle_t bus);

static axp2101_t           s_pmu;
static bool                s_pmu_ready;
static aos_board_variant_t s_variant = AOS_BOARD_UNKNOWN;

/* I2C addresses of the two possible touch controllers. The rest of the board
 * is identical between revisions, so this is enough to know which one we are
 * running on. */
#define TOUCH_ADDR_CST816   0x15
#define TOUCH_ADDR_FT3168   0x38

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

    out->percent     = axp2101_battery_percent(&s_pmu);
    out->vbat        = axp2101_battery_voltage(&s_pmu);
    out->vbus        = axp2101_vbus_voltage(&s_pmu);
    out->vsys        = axp2101_system_voltage(&s_pmu);
    out->temperature = axp2101_die_temperature(&s_pmu);
    out->charging    = axp2101_is_charging(&s_pmu);
    out->usb_present = axp2101_is_vbus_present(&s_pmu);
    out->valid       = true;
    return true;
}

void aos_board_pmu_shutdown(void)
{
    if (s_pmu_ready) {
        axp2101_shutdown(&s_pmu);
    }
}
