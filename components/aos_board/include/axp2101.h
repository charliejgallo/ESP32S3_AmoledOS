/*
 * AmoledOS - Minimal driver for the AXP2101 PMU (only what the watch uses).
 *
 * Registers and formulas taken from XPowersLib (lewisxhe, MIT), rewritten in C
 * so as not to drag in the whole C++ library.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXP2101_I2C_ADDRESS     0x34
#define AXP2101_CHIP_ID         0x4A

typedef struct {
    i2c_master_dev_handle_t dev;
    bool                    ready;
} axp2101_t;

esp_err_t axp2101_init(axp2101_t *pmu, i2c_master_bus_handle_t bus);

int   axp2101_battery_percent(axp2101_t *pmu);   /* 0..100, -1 if there is no reading */
float axp2101_battery_voltage(axp2101_t *pmu);   /* V */
float axp2101_vbus_voltage(axp2101_t *pmu);      /* V */
float axp2101_system_voltage(axp2101_t *pmu);    /* V */
float axp2101_die_temperature(axp2101_t *pmu);   /* degrees C */
bool  axp2101_is_charging(axp2101_t *pmu);
bool  axp2101_is_vbus_present(axp2101_t *pmu);
void  axp2101_shutdown(axp2101_t *pmu);

#ifdef __cplusplus
}
#endif
