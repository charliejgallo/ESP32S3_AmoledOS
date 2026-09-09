/*
 * AmoledOS - Board peripherals the Waveshare BSP does not cover:
 * AXP2101 PMU, PCF85063A RTC and QMI8658 IMU.
 *
 * The I2C bus is created by the BSP (bsp_i2c_init); here we only hang devices
 * off it.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AOS_BOARD_UNKNOWN = 0,
    AOS_BOARD_V1_SH8601_FT3168,
    AOS_BOARD_V2_CO5300_CST816,
} aos_board_variant_t;

esp_err_t           aos_board_init(void);
aos_board_variant_t aos_board_variant(void);
const char         *aos_board_variant_name(void);

/* --- PMU ---------------------------------------------------------------- */
typedef struct {
    int   percent;      /* -1 if there is no battery or it could not be read */
    float vbat;         /* V */
    float vbus;         /* V */
    float vsys;         /* V */
    float temperature;  /* degrees C of the PMU's die */
    float board_temperature;    /* degrees C from the NTC on the TS pin, NAN if none */
    int   charge_state; /* axp2101_chg_state_t */
    bool  charging;
    bool  usb_present;
    bool  battery_present;
    bool  valid;
} aos_pmu_state_t;

bool aos_board_pmu_read(aos_pmu_state_t *out);
void aos_board_pmu_shutdown(void);

/* What the firmware programs into the charger and the gauge at boot. The
 * chip's own defaults are 300 mA / 4.2 V / warn 15% / VOFF 2.6 V; see
 * docs/POWER.md for why each of these is different. */
typedef struct {
    int charge_ma;          /* constant-current limit                       */
    int precharge_ma;
    int termination_ma;
    int target_mv;          /* 4100 = "battery care", 4200 = full capacity  */
    int warn_pct;           /* gauge percent that raises the warning IRQ    */
    int shutdown_pct;       /* gauge percent that raises the shutdown IRQ   */
    int poweroff_mv;        /* VOFF: the PMU cuts everything below this     */
} aos_pmu_config_t;

esp_err_t aos_board_pmu_configure(const aos_pmu_config_t *cfg);
esp_err_t aos_board_pmu_charge_target_set(int mv);
esp_err_t aos_board_pmu_charge_current_set(int ma);

/* The charger as it is programmed right now. */
typedef struct {
    int charge_ma, precharge_ma, termination_ma, target_mv;
    int warn_pct, shutdown_pct, poweroff_mv;
    int key_long_ms, key_off_ms;
} aos_pmu_charger_t;

bool aos_board_pmu_charger_get(aos_pmu_charger_t *out);

/* Why the PMU came up, and why it went down the last time. Static strings. */
const char *aos_board_pmu_power_on_reason(void);
const char *aos_board_pmu_power_off_reason(void);

/* Pending PMU interrupts, as AXP2101_IRQ_* bits, and cleared on the way out.
 * The IRQ line reaches the TCA9554 expander and not the ESP32, so this is a
 * poll: one I2C read of the expander, and the PMU's status registers only
 * when the line is actually low. 0 when nothing is pending. */
uint32_t aos_board_pmu_poll_irq(void);

/* The power button's level (EXIO4 through a transistor): true while held. */
bool aos_board_power_key_down(void);

void aos_board_pmu_dump(void);

/* Hardware reset of the AMOLED through LCD_RESET on EXIO0 of the expander.
 * The BSP never pulls it: it relies on the software reset command, which a
 * panel whose interface mode got corrupted no longer understands. */
void aos_board_panel_hw_reset(void);

/* Regulators and raw registers, for the experiments behind /api/pmu. */
int       aos_board_pmu_rail_count(void);
bool      aos_board_pmu_rail_get(int idx, const char **name, bool *on, int *mv);
int       aos_board_pmu_rail_find(const char *name);       /* -1 if unknown */
esp_err_t aos_board_pmu_rail_set(int idx, bool on);
int       aos_board_pmu_register_read(uint8_t reg);
esp_err_t aos_board_pmu_register_write(uint8_t reg, uint8_t value);
float     aos_board_pmu_ts_voltage(void);

/* --- RTC ---------------------------------------------------------------- */
bool aos_board_rtc_get(struct tm *out);
bool aos_board_rtc_set(const struct tm *value);
bool aos_board_rtc_alarm_set(int hour, int minute);
void aos_board_rtc_alarm_clear(void);
bool aos_board_rtc_present(void);

/* --- IMU ---------------------------------------------------------------- */
typedef struct {
    float ax, ay, az;       /* g   */
    float gx, gy, gz;       /* dps */
    float temperature;
    bool  valid;
} aos_imu_sample_t;

bool     aos_board_imu_read(aos_imu_sample_t *out);
void     aos_board_imu_poll(void);      /* step counter and orientation */
uint32_t aos_board_imu_steps(void);
void     aos_board_imu_steps_reset(void);
int      aos_board_imu_orientation(void);   /* maps to aos_orientation_t */
bool     aos_board_imu_wrist_raised(void);  /* wrist-raise gesture */

/* The gyroscope is the expensive half of the QMI8658 (about 1 mA on its own
 * against tens of uA for the accelerometer) and nothing the watch does all day
 * needs it. Switching it off was tried and broke the accelerometer (see the
 * note in qmi8658.c), so today this only records the request. */
void     aos_board_imu_gyro_enable(bool on);
bool     aos_board_imu_gyro_enabled(void);

#ifdef __cplusplus
}
#endif
