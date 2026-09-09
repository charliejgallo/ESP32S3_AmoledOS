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
    bool  charging;
    bool  usb_present;
    bool  valid;
} aos_pmu_state_t;

bool aos_board_pmu_read(aos_pmu_state_t *out);
void aos_board_pmu_shutdown(void);

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

#ifdef __cplusplus
}
#endif
