#include "axp2101.h"
#include "esp_log.h"
#include <math.h>

/* Register map, from the datasheet's section 6.13. */
#define REG_STATUS1             0x00
#define REG_STATUS2             0x01
#define REG_IC_TYPE             0x03
#define REG_COMMON_CONFIG       0x10
#define REG_VBUS_CUR_LIMIT      0x16
#define REG_CHG_GAUGE_WDT       0x18
#define REG_LOW_BAT_WARN        0x1A
#define REG_PWRON_STATUS        0x20
#define REG_PWROFF_STATUS       0x21
#define REG_VOFF                0x24
#define REG_KEY_LEVELS          0x27
#define REG_ADC_CHANNEL_CTRL    0x30
#define REG_ADC_VBAT_H          0x34    /* H5L8 */
#define REG_ADC_TS_H            0x36    /* H6L8, 0.5 mV per LSB */
#define REG_ADC_VBUS_H          0x38    /* H6L8 */
#define REG_ADC_VSYS_H          0x3A    /* H6L8 */
#define REG_ADC_TDIE_H          0x3C    /* H6L8 */
#define REG_IRQ_EN0             0x40
#define REG_IRQ_STATUS0         0x48
#define REG_IPRECHG             0x61
#define REG_ICC                 0x62
#define REG_ITERM               0x63
#define REG_CV                  0x64
#define REG_BAT_DET_CTRL        0x68
#define REG_DCDC_ONOFF          0x80
#define REG_DCDC1_VOL           0x82
#define REG_LDO_ONOFF0          0x90
#define REG_LDO_ONOFF1          0x91
#define REG_ALDO1_VOL           0x92
#define REG_BAT_PERCENT         0xA4

static const char *TAG = "axp2101";

/* -------------------------------------------------------------------------- */
/* Bus                                                                         */
/* -------------------------------------------------------------------------- */

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

/* Read-modify-write of a field: keeps every bit outside 'mask'. */
static esp_err_t reg_update(axp2101_t *pmu, uint8_t reg, uint8_t mask, uint8_t value)
{
    int current = reg_read8(pmu, reg);
    if (current < 0) {
        return ESP_FAIL;
    }
    uint8_t next = (uint8_t)((current & ~mask) | (value & mask));
    if (next == (uint8_t)current) {
        return ESP_OK;
    }
    return reg_write8(pmu, reg, next);
}

static esp_err_t reg_set_bit(axp2101_t *pmu, uint8_t reg, uint8_t bit)
{
    return reg_update(pmu, reg, (uint8_t)(1 << bit), (uint8_t)(1 << bit));
}

static bool reg_get_bit(axp2101_t *pmu, uint8_t reg, uint8_t bit)
{
    int value = reg_read8(pmu, reg);
    return value >= 0 && (value & (1 << bit)) != 0;
}

/* The ADC publishes 14-bit results as "H6L8"/"H5L8": the high bits in the
 * high register and 8 bits in the low one. Reading both in one transaction
 * is what the datasheet asks for ("read the high 6 bits firstly"). */
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

/* -------------------------------------------------------------------------- */
/* Init                                                                        */
/* -------------------------------------------------------------------------- */

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

    /* Battery detection on, and every ADC channel we read: bit0 VBAT, bit1 TS,
     * bit2 VBUS, bit3 VSYS, bit4 TDIE. TS stays enabled ON PURPOSE: the board
     * has an NTC on it and the charger uses it as its temperature guard. */
    reg_set_bit(pmu, REG_BAT_DET_CTRL, 0);
    reg_update(pmu, REG_ADC_CHANNEL_CTRL, 0x1F, 0x1F);

    /* Measured 2026-09-09: Waveshare's EFUSE leaves reg 0x50 at 0x12 - TS
     * as "external input, does not gate the charger" (bit4, kept) and the
     * current source OFF (bits 3:2 = 00), so the ADC read full scale and
     * the NTC looked absent. With the source on, TS read 0.429 V = 8.6 kOhm
     * = 28.7 C next to a die at 33 C. Mode 10 switches the source on only
     * while the TS channel is being sampled, which is the cheapest. */
    reg_update(pmu, 0x50, 0x0C, 0x08);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* Measurements                                                                */
/* -------------------------------------------------------------------------- */

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

float axp2101_ts_voltage(axp2101_t *pmu)
{
    return read_h6l8(pmu, REG_ADC_TS_H) * 0.5f / 1000.0f;
}

/* The chip pushes a current through the NTC (reg 0x50 bits 1:0, 50 uA from
 * the factory) and measures the voltage, so R = V / I. The board's part is a
 * 10K NTC; its beta is not on the schematic, and 3950 is what those parts
 * almost always are. Treat the last degree as decoration. */
float axp2101_ts_temperature(axp2101_t *pmu)
{
    static const float source_ua[4] = { 20.0f, 40.0f, 50.0f, 60.0f };
    int ctrl = reg_read8(pmu, 0x50);
    float ua = ctrl < 0 ? 50.0f : source_ua[ctrl & 0x03];

    float v = axp2101_ts_voltage(pmu);
    if (v <= 0.02f || v >= 3.5f) {
        return NAN;                     /* channel off, shorted or open */
    }
    float r = v / (ua * 1e-6f);
    const float beta = 3950.0f, r25 = 10000.0f, t25 = 298.15f;
    float kelvin = 1.0f / (1.0f / t25 + logf(r / r25) / beta);
    return kelvin - 273.15f;
}

bool axp2101_is_charging(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_STATUS2);
    return value >= 0 && ((value >> 5) & 0x03) == 0x01;
}

bool axp2101_is_vbus_present(axp2101_t *pmu)
{
    return reg_get_bit(pmu, REG_STATUS1, 5);
}

bool axp2101_battery_present(axp2101_t *pmu)
{
    return reg_get_bit(pmu, REG_STATUS1, 3);
}

axp2101_chg_state_t axp2101_charge_state(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_STATUS2);
    if (value < 0) {
        return AXP2101_CHG_IDLE;
    }
    value &= 0x07;
    return value > AXP2101_CHG_IDLE ? AXP2101_CHG_IDLE : (axp2101_chg_state_t)value;
}

const char *axp2101_charge_state_name(axp2101_chg_state_t state)
{
    switch (state) {
    case AXP2101_CHG_TRICKLE:   return "trickle";
    case AXP2101_CHG_PRECHARGE: return "precharge";
    case AXP2101_CHG_CC:        return "constant current";
    case AXP2101_CHG_CV:        return "constant voltage";
    case AXP2101_CHG_DONE:      return "done";
    default:                    return "idle";
    }
}

/* -------------------------------------------------------------------------- */
/* Charger                                                                     */
/* -------------------------------------------------------------------------- */

/* reg 0x62 bits 4:0: 25*N mA up to N=8, then 200+100*(N-8) up to N=16. */
esp_err_t axp2101_charge_current_set(axp2101_t *pmu, int ma)
{
    if (ma < 0) ma = 0;
    int n = ma <= 200 ? ma / 25 : 8 + (ma - 200) / 100;
    if (n > 16) n = 16;
    return reg_update(pmu, REG_ICC, 0x1F, (uint8_t)n);
}

int axp2101_charge_current_ma(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_ICC);
    if (value < 0) return -1;
    int n = value & 0x1F;
    return n <= 8 ? 25 * n : 200 + 100 * (n - 8);
}

static esp_err_t set_25ma_field(axp2101_t *pmu, uint8_t reg, int ma)
{
    if (ma < 0) ma = 0;
    int n = ma / 25;
    if (n > 8) n = 8;
    return reg_update(pmu, reg, 0x0F, (uint8_t)n);
}

static int get_25ma_field(axp2101_t *pmu, uint8_t reg)
{
    int value = reg_read8(pmu, reg);
    return value < 0 ? -1 : 25 * (value & 0x0F);
}

esp_err_t axp2101_precharge_current_set(axp2101_t *pmu, int ma)
{
    return set_25ma_field(pmu, REG_IPRECHG, ma);
}

int axp2101_precharge_current_ma(axp2101_t *pmu)
{
    return get_25ma_field(pmu, REG_IPRECHG);
}

esp_err_t axp2101_termination_current_set(axp2101_t *pmu, int ma)
{
    return set_25ma_field(pmu, REG_ITERM, ma);
}

int axp2101_termination_current_ma(axp2101_t *pmu)
{
    return get_25ma_field(pmu, REG_ITERM);
}

/* reg 0x64 bits 2:0: 1=4.0 2=4.1 3=4.2 4=4.35 5=4.4 */
esp_err_t axp2101_charge_target_set(axp2101_t *pmu, int mv)
{
    int code;
    if (mv >= 4400)      code = 5;
    else if (mv >= 4350) code = 4;
    else if (mv >= 4200) code = 3;
    else if (mv >= 4100) code = 2;
    else                 code = 1;
    return reg_update(pmu, REG_CV, 0x07, (uint8_t)code);
}

int axp2101_charge_target_mv(axp2101_t *pmu)
{
    static const int table[8] = { -1, 4000, 4100, 4200, 4350, 4400, -1, -1 };
    int value = reg_read8(pmu, REG_CV);
    return value < 0 ? -1 : table[value & 0x07];
}

static const int s_vbus_limits[6] = { 100, 500, 900, 1000, 1500, 2000 };

esp_err_t axp2101_vbus_current_limit_set(axp2101_t *pmu, int ma)
{
    int code = 0;
    for (int i = 0; i < 6; i++) {
        if (s_vbus_limits[i] <= ma) code = i;
    }
    return reg_update(pmu, REG_VBUS_CUR_LIMIT, 0x07, (uint8_t)code);
}

int axp2101_vbus_current_limit_ma(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_VBUS_CUR_LIMIT);
    if (value < 0) return -1;
    value &= 0x07;
    return value < 6 ? s_vbus_limits[value] : -1;
}

esp_err_t axp2101_charging_enable(axp2101_t *pmu, bool on)
{
    return reg_update(pmu, REG_CHG_GAUGE_WDT, 0x02, on ? 0x02 : 0x00);
}

/* -------------------------------------------------------------------------- */
/* Thresholds                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t axp2101_low_battery_levels_set(axp2101_t *pmu, int warn_pct, int shutdown_pct)
{
    if (warn_pct < 5) warn_pct = 5;
    if (warn_pct > 20) warn_pct = 20;
    if (shutdown_pct < 0) shutdown_pct = 0;
    if (shutdown_pct > 15) shutdown_pct = 15;
    return reg_write8(pmu, REG_LOW_BAT_WARN,
                      (uint8_t)(((warn_pct - 5) << 4) | shutdown_pct));
}

void axp2101_low_battery_levels_get(axp2101_t *pmu, int *warn_pct, int *shutdown_pct)
{
    int value = reg_read8(pmu, REG_LOW_BAT_WARN);
    if (warn_pct)     *warn_pct     = value < 0 ? -1 : 5 + (value >> 4);
    if (shutdown_pct) *shutdown_pct = value < 0 ? -1 : (value & 0x0F);
}

esp_err_t axp2101_poweroff_voltage_set(axp2101_t *pmu, int mv)
{
    if (mv < 2600) mv = 2600;
    if (mv > 3300) mv = 3300;
    return reg_update(pmu, REG_VOFF, 0x07, (uint8_t)((mv - 2600) / 100));
}

int axp2101_poweroff_voltage_mv(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_VOFF);
    return value < 0 ? -1 : 2600 + 100 * (value & 0x07);
}

/* -------------------------------------------------------------------------- */
/* Interrupts                                                                  */
/* -------------------------------------------------------------------------- */

esp_err_t axp2101_irq_enable(axp2101_t *pmu, uint32_t mask)
{
    esp_err_t ret = reg_write8(pmu, REG_IRQ_EN0,     (uint8_t)(mask >> 16));
    if (ret == ESP_OK) ret = reg_write8(pmu, REG_IRQ_EN0 + 1, (uint8_t)(mask >> 8));
    if (ret == ESP_OK) ret = reg_write8(pmu, REG_IRQ_EN0 + 2, (uint8_t)(mask));
    return ret;
}

uint32_t axp2101_irq_read_clear(axp2101_t *pmu)
{
    uint8_t status[3] = {0};
    uint32_t pending = 0;
    for (int i = 0; i < 3; i++) {
        int value = reg_read8(pmu, (uint8_t)(REG_IRQ_STATUS0 + i));
        if (value < 0) {
            return 0;
        }
        status[i] = (uint8_t)value;
        pending = (pending << 8) | status[i];
    }
    /* Write-1-to-clear, only what was seen, so nothing that arrives in
     * between is lost. */
    for (int i = 0; i < 3; i++) {
        if (status[i]) {
            reg_write8(pmu, (uint8_t)(REG_IRQ_STATUS0 + i), status[i]);
        }
    }
    return pending;
}

/* -------------------------------------------------------------------------- */
/* Power key                                                                   */
/* -------------------------------------------------------------------------- */

static const int s_on_ms[4]  = { 128, 512, 1000, 2000 };
static const int s_irq_ms[4] = { 1000, 1500, 2000, 2500 };
static const int s_off_ms[4] = { 4000, 6000, 8000, 10000 };

static int nearest_code(const int *table, int ms)
{
    int code = 0;
    for (int i = 0; i < 4; i++) {
        if (table[i] <= ms) code = i;
    }
    return code;
}

esp_err_t axp2101_power_key_timing_set(axp2101_t *pmu, int on_ms, int irq_ms, int off_ms)
{
    uint8_t value = (uint8_t)((nearest_code(s_irq_ms, irq_ms) << 4) |
                              (nearest_code(s_off_ms, off_ms) << 2) |
                               nearest_code(s_on_ms,  on_ms));
    return reg_update(pmu, REG_KEY_LEVELS, 0x3F, value);
}

void axp2101_power_key_timing_get(axp2101_t *pmu, int *on_ms, int *irq_ms, int *off_ms)
{
    int value = reg_read8(pmu, REG_KEY_LEVELS);
    if (value < 0) value = 0;
    if (on_ms)  *on_ms  = s_on_ms[value & 0x03];
    if (irq_ms) *irq_ms = s_irq_ms[(value >> 4) & 0x03];
    if (off_ms) *off_ms = s_off_ms[(value >> 2) & 0x03];
}

/* -------------------------------------------------------------------------- */
/* Sources                                                                     */
/* -------------------------------------------------------------------------- */

uint8_t axp2101_power_on_source(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_PWRON_STATUS);
    return value < 0 ? 0 : (uint8_t)value;
}

uint8_t axp2101_power_off_source(axp2101_t *pmu)
{
    int value = reg_read8(pmu, REG_PWROFF_STATUS);
    return value < 0 ? 0 : (uint8_t)value;
}

const char *axp2101_power_on_source_name(uint8_t bits)
{
    if (bits & (1 << 0)) return "power key";
    if (bits & (1 << 2)) return "USB plugged in";
    if (bits & (1 << 3)) return "battery charged past 3.3 V";
    if (bits & (1 << 4)) return "battery inserted";
    if (bits & (1 << 1)) return "IRQ pin pulled low";
    if (bits & (1 << 5)) return "EN pin high";
    return "unknown";
}

const char *axp2101_power_off_source_name(uint8_t bits)
{
    if (bits & (1 << 0)) return "power key held";
    if (bits & (1 << 1)) return "software";
    if (bits & (1 << 3)) return "battery ran out (VSYS under VOFF)";
    if (bits & (1 << 7)) return "PMU overheated";
    if (bits & (1 << 5)) return "a DCDC rail sagged";
    if (bits & (1 << 6)) return "a DCDC rail overshot";
    if (bits & (1 << 4)) return "USB over-voltage";
    if (bits & (1 << 2)) return "EN pin low";
    return "none recorded";
}

/* -------------------------------------------------------------------------- */
/* Regulators                                                                  */
/* -------------------------------------------------------------------------- */

static const char *s_rail_names[AXP2101_RAIL_COUNT] = {
    "DCDC1", "DCDC2", "DCDC3", "DCDC4", "DCDC5",
    "ALDO1", "ALDO2", "ALDO3", "ALDO4", "BLDO1", "BLDO2", "CPUSLDO",
    "DLDO1", "DLDO2",
};

const char *axp2101_rail_name(axp2101_rail_t rail)
{
    return rail < AXP2101_RAIL_COUNT ? s_rail_names[rail] : "?";
}

/* Where each rail's enable bit lives. */
static bool rail_enable_bit(axp2101_rail_t rail, uint8_t *reg, uint8_t *bit)
{
    switch (rail) {
    case AXP2101_RAIL_DCDC1: case AXP2101_RAIL_DCDC2: case AXP2101_RAIL_DCDC3:
    case AXP2101_RAIL_DCDC4: case AXP2101_RAIL_DCDC5:
        *reg = REG_DCDC_ONOFF; *bit = (uint8_t)(rail - AXP2101_RAIL_DCDC1); return true;
    case AXP2101_RAIL_ALDO1:   *reg = REG_LDO_ONOFF0; *bit = 0; return true;
    case AXP2101_RAIL_ALDO2:   *reg = REG_LDO_ONOFF0; *bit = 1; return true;
    case AXP2101_RAIL_ALDO3:   *reg = REG_LDO_ONOFF0; *bit = 2; return true;
    case AXP2101_RAIL_ALDO4:   *reg = REG_LDO_ONOFF0; *bit = 3; return true;
    case AXP2101_RAIL_BLDO1:   *reg = REG_LDO_ONOFF0; *bit = 4; return true;
    case AXP2101_RAIL_BLDO2:   *reg = REG_LDO_ONOFF0; *bit = 5; return true;
    case AXP2101_RAIL_CPUSLDO: *reg = REG_LDO_ONOFF0; *bit = 6; return true;
    case AXP2101_RAIL_DLDO1:   *reg = REG_LDO_ONOFF0; *bit = 7; return true;
    case AXP2101_RAIL_DLDO2:   *reg = REG_LDO_ONOFF1; *bit = 0; return true;
    default: return false;
    }
}

bool axp2101_rail_is_enabled(axp2101_t *pmu, axp2101_rail_t rail)
{
    uint8_t reg, bit;
    return rail_enable_bit(rail, &reg, &bit) && reg_get_bit(pmu, reg, bit);
}

esp_err_t axp2101_rail_enable(axp2101_t *pmu, axp2101_rail_t rail, bool on)
{
    uint8_t reg, bit;
    if (!rail_enable_bit(rail, &reg, &bit)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (rail == AXP2101_RAIL_DCDC1 && !on) {
        /* That is the 3.3 V everything runs from, us included. */
        return ESP_ERR_NOT_ALLOWED;
    }
    return reg_update(pmu, reg, (uint8_t)(1 << bit), on ? (uint8_t)(1 << bit) : 0);
}

/* The voltage codes, from 6.13.2.70 onwards. The buck converters have piecewise
 * scales; the LDOs are 100 mV steps from 0.5 V except CPUSLDO (50 mV). */
int axp2101_rail_voltage_mv(axp2101_t *pmu, axp2101_rail_t rail)
{
    int n;
    switch (rail) {
    case AXP2101_RAIL_DCDC1:
        n = reg_read8(pmu, REG_DCDC1_VOL);
        if (n < 0) return -1;
        n &= 0x1F;
        return n <= 19 ? 1500 + 100 * n : -1;
    case AXP2101_RAIL_DCDC2:
    case AXP2101_RAIL_DCDC3:
        n = reg_read8(pmu, (uint8_t)(REG_DCDC1_VOL + rail - AXP2101_RAIL_DCDC1));
        if (n < 0) return -1;
        n &= 0x7F;
        if (n <= 70) return 500 + 10 * n;
        if (n <= 87) return 1220 + 20 * (n - 71);
        if (rail == AXP2101_RAIL_DCDC3 && n <= 106) return 1600 + 100 * (n - 88);
        return -1;
    case AXP2101_RAIL_DCDC4:
        n = reg_read8(pmu, REG_DCDC1_VOL + 3);
        if (n < 0) return -1;
        n &= 0x7F;
        if (n <= 70) return 500 + 10 * n;
        if (n <= 102) return 1220 + 20 * (n - 71);
        return -1;
    case AXP2101_RAIL_DCDC5:
        n = reg_read8(pmu, REG_DCDC1_VOL + 4);
        if (n < 0) return -1;
        n &= 0x1F;
        if (n == 25) return 1200;
        return n <= 23 ? 1400 + 100 * n : -1;
    case AXP2101_RAIL_CPUSLDO:
        n = reg_read8(pmu, REG_ALDO1_VOL + 6);
        if (n < 0) return -1;
        n &= 0x1F;
        return n <= 19 ? 500 + 50 * n : -1;
    default:
        if (rail < AXP2101_RAIL_ALDO1 || rail >= AXP2101_RAIL_COUNT) return -1;
        n = reg_read8(pmu, (uint8_t)(REG_ALDO1_VOL + rail - AXP2101_RAIL_ALDO1));
        if (n < 0) return -1;
        n &= 0x1F;
        return n <= 30 ? 500 + 100 * n : -1;
    }
}

/* -------------------------------------------------------------------------- */

void axp2101_dump(axp2101_t *pmu)
{
    if (!pmu || !pmu->ready) {
        ESP_LOGW(TAG, "dump: PMU not ready");
        return;
    }

    ESP_LOGI(TAG, "status: vbat %.3f V  vbus %.2f V  vsys %.3f V  die %.1f C  "
                  "ts %.3f V (%.1f C)  soc %d%%  %s%s%s",
             axp2101_battery_voltage(pmu), axp2101_vbus_voltage(pmu),
             axp2101_system_voltage(pmu), axp2101_die_temperature(pmu),
             axp2101_ts_voltage(pmu), axp2101_ts_temperature(pmu),
             axp2101_battery_percent(pmu),
             axp2101_battery_present(pmu) ? "battery" : "NO battery",
             axp2101_is_vbus_present(pmu) ? ", usb" : "",
             axp2101_is_charging(pmu) ? ", charging" : "");

    int warn, off;
    axp2101_low_battery_levels_get(pmu, &warn, &off);
    int on_ms, irq_ms, off_ms;
    axp2101_power_key_timing_get(pmu, &on_ms, &irq_ms, &off_ms);
    ESP_LOGI(TAG, "charger: %s, cc %d mA, pre %d mA, term %d mA, target %d mV, "
                  "usb limit %d mA, charging %s",
             axp2101_charge_state_name(axp2101_charge_state(pmu)),
             axp2101_charge_current_ma(pmu), axp2101_precharge_current_ma(pmu),
             axp2101_termination_current_ma(pmu), axp2101_charge_target_mv(pmu),
             axp2101_vbus_current_limit_ma(pmu),
             reg_get_bit(pmu, REG_CHG_GAUGE_WDT, 1) ? "enabled" : "DISABLED");
    ESP_LOGI(TAG, "thresholds: warn %d%%, shutdown %d%%, VOFF %d mV; "
                  "key on %d ms, long %d ms, off %d ms",
             warn, off, axp2101_poweroff_voltage_mv(pmu), on_ms, irq_ms, off_ms);

    ESP_LOGI(TAG, "adc ctrl 0x%02X, ts ctrl 0x%02X (bit4=1: TS does not gate the charger)",
             reg_read8(pmu, REG_ADC_CHANNEL_CTRL), reg_read8(pmu, 0x50));

    uint8_t on_src = axp2101_power_on_source(pmu), off_src = axp2101_power_off_source(pmu);
    ESP_LOGI(TAG, "powered on by: %s (0x%02X); last power-off: %s (0x%02X)",
             axp2101_power_on_source_name(on_src), on_src,
             axp2101_power_off_source_name(off_src), off_src);

    char line[160];
    int pos = 0;
    for (int r = 0; r < AXP2101_RAIL_COUNT; r++) {
        bool on = axp2101_rail_is_enabled(pmu, (axp2101_rail_t)r);
        int mv  = axp2101_rail_voltage_mv(pmu, (axp2101_rail_t)r);
        pos += snprintf(line + pos, sizeof(line) - pos, "%s%s=%d ",
                        on ? "" : "-", axp2101_rail_name((axp2101_rail_t)r), mv);
        if (pos > 120 || r == AXP2101_RAIL_COUNT - 1) {
            ESP_LOGI(TAG, "rails (- means off): %s", line);
            pos = 0;
            line[0] = '\0';
        }
    }
}

void axp2101_shutdown(axp2101_t *pmu)
{
    reg_set_bit(pmu, REG_COMMON_CONFIG, 0);
}

int axp2101_register_read(axp2101_t *pmu, uint8_t reg)
{
    return reg_read8(pmu, reg);
}

esp_err_t axp2101_register_write(axp2101_t *pmu, uint8_t reg, uint8_t value)
{
    return reg_write8(pmu, reg, value);
}
