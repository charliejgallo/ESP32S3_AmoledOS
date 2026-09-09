#include "axp2101.h"
#include "esp_log.h"

#define REG_STATUS1             0x00
#define REG_STATUS2             0x01
#define REG_IC_TYPE             0x03
#define REG_COMMON_CONFIG       0x10
#define REG_ADC_CHANNEL_CTRL    0x30
#define REG_ADC_VBAT_H          0x34    /* H5L8 */
#define REG_ADC_VBUS_H          0x38    /* H6L8 */
#define REG_ADC_VSYS_H          0x3A    /* H6L8 */
#define REG_ADC_TDIE_H          0x3C    /* H6L8 */
#define REG_BAT_DET_CTRL        0x68
#define REG_BAT_PERCENT         0xA4

static const char *TAG = "axp2101";

static esp_err_t reg_read(axp2101_t *pmu, uint8_t reg, uint8_t *data, size_t len)
{
    if (!pmu || !pmu->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(pmu->dev, &reg, 1, data, len, 100);
}

static int reg_read8(axp2101_t *pmu, uint8_t reg)
{
    uint8_t value = 0;
    return reg_read(pmu, reg, &value, 1) == ESP_OK ? value : -1;
}

static esp_err_t reg_write8(axp2101_t *pmu, uint8_t reg, uint8_t value)
{
    if (!pmu || !pmu->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(pmu->dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_set_bit(axp2101_t *pmu, uint8_t reg, uint8_t bit)
{
    int value = reg_read8(pmu, reg);
    if (value < 0) {
        return ESP_FAIL;
    }
    return reg_write8(pmu, reg, (uint8_t)(value | (1 << bit)));
}

/* The AXP2101 publishes the ADC's measurements as "H6L8"/"H5L8": the high bits
 * in the high register and 8 bits in the low one. The result is in mV. */
static uint16_t read_h6l8(axp2101_t *pmu, uint8_t reg_high)
{
    uint8_t raw[2] = {0};
    if (reg_read(pmu, reg_high, raw, 2) != ESP_OK) {
        return 0;
    }
    return (uint16_t)(((raw[0] & 0x3F) << 8) | raw[1]);
}

static uint16_t read_h5l8(axp2101_t *pmu, uint8_t reg_high)
{
    uint8_t raw[2] = {0};
    if (reg_read(pmu, reg_high, raw, 2) != ESP_OK) {
        return 0;
    }
    return (uint16_t)(((raw[0] & 0x1F) << 8) | raw[1]);
}

esp_err_t axp2101_init(axp2101_t *pmu, i2c_master_bus_handle_t bus)
{
    if (!pmu || !bus) {
        return ESP_ERR_INVALID_ARG;
    }
    pmu->ready = false;

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AXP2101_I2C_ADDRESS,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &pmu->dev);
    if (ret != ESP_OK) {
        return ret;
    }
    pmu->ready = true;

    int id = reg_read8(pmu, REG_IC_TYPE);
    if (id < 0) {
        pmu->ready = false;
        i2c_master_bus_rm_device(pmu->dev);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "IC type 0x%02X", id);

    /* Enable battery detection and the measurements we care about:
     * bit0 VBAT, bit2 VBUS, bit3 VSYS, bit4 TDIE. */
    reg_set_bit(pmu, REG_BAT_DET_CTRL, 0);
    int adc = reg_read8(pmu, REG_ADC_CHANNEL_CTRL);
    if (adc >= 0) {
        reg_write8(pmu, REG_ADC_CHANNEL_CTRL, (uint8_t)(adc | 0x1D));
    }
    return ESP_OK;
}

int axp2101_battery_percent(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_BAT_PERCENT);
    if (value < 0) {
        return -1;
    }
    value &= 0x7F;
    return value > 100 ? -1 : value;
}

float axp2101_battery_voltage(axp2101_t *pmu)
{
    return read_h5l8(pmu, REG_ADC_VBAT_H) / 1000.0f;
}

float axp2101_vbus_voltage(axp2101_t *pmu)
{
    return read_h6l8(pmu, REG_ADC_VBUS_H) / 1000.0f;
}

float axp2101_system_voltage(axp2101_t *pmu)
{
    return read_h6l8(pmu, REG_ADC_VSYS_H) / 1000.0f;
}

float axp2101_die_temperature(axp2101_t *pmu)
{
    uint16_t raw = read_h6l8(pmu, REG_ADC_TDIE_H);
    return 22.0f + (7274.0f - (float)raw) / 20.0f;
}

bool axp2101_is_charging(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_STATUS2);
    return value >= 0 && ((value >> 5) & 0x07) == 0x01;
}

bool axp2101_is_vbus_present(axp2101_t *pmu)
{
    int status1 = reg_read8(pmu, REG_STATUS1);
    return status1 >= 0 && (status1 & (1 << 5)) != 0;
}

void axp2101_shutdown(axp2101_t *pmu)
{
    reg_set_bit(pmu, REG_COMMON_CONFIG, 0);
}
