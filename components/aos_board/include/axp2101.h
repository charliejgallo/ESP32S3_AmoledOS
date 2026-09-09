/*
 * AmoledOS - Driver for the AXP2101 PMU, written against the datasheet
 * (AXP2101_Datasheet_V1.0_en, section 6.13) and cross-checked with XPowersLib
 * (lewisxhe, MIT). Plain C on the new i2c_master driver so as not to drag in
 * the C++ library.
 *
 * What the chip can and cannot tell us (measured and read, not assumed):
 *
 *   - It has NO coulomb counter and NO current sense: it never reports the
 *     battery current. The percentage comes from its own fuel gauge (a
 *     voltage-model "E-Gauge") in register 0xA4.
 *   - It measures VBAT, VBUS, VSYS, its own die temperature and the TS pin.
 *     On this board TS has a 10K NTC to ground (RP2 in the schematic), so it
 *     is a board temperature next to the PMU, not the cell's.
 *   - Its IRQ pin does NOT reach the ESP32: it goes to EXIO5 of the TCA9554
 *     expander, whose INT pin is only pulled up. Interrupts are therefore a
 *     matter of polling the expander (aos_board does it).
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

/* --- measurements ------------------------------------------------------- */

int   axp2101_battery_percent(axp2101_t *pmu);   /* 0..100, -1 if there is no reading */
float axp2101_battery_voltage(axp2101_t *pmu);   /* V */
float axp2101_vbus_voltage(axp2101_t *pmu);      /* V */
float axp2101_system_voltage(axp2101_t *pmu);    /* V */
float axp2101_die_temperature(axp2101_t *pmu);   /* degrees C */
float axp2101_ts_voltage(axp2101_t *pmu);        /* V on the TS pin, 0 if the channel is off */
float axp2101_ts_temperature(axp2101_t *pmu);    /* degrees C from the NTC, NAN if TS is open */
bool  axp2101_is_charging(axp2101_t *pmu);
bool  axp2101_is_vbus_present(axp2101_t *pmu);
bool  axp2101_battery_present(axp2101_t *pmu);

/* reg 0x01 bits 2:0 */
typedef enum {
    AXP2101_CHG_TRICKLE = 0,    /* very flat cell, tiny current       */
    AXP2101_CHG_PRECHARGE,      /* below 3 V, precharge current       */
    AXP2101_CHG_CC,             /* constant current                   */
    AXP2101_CHG_CV,             /* constant voltage, current tapering */
    AXP2101_CHG_DONE,           /* terminated                         */
    AXP2101_CHG_IDLE,           /* not charging                       */
} axp2101_chg_state_t;

axp2101_chg_state_t axp2101_charge_state(axp2101_t *pmu);
const char         *axp2101_charge_state_name(axp2101_chg_state_t state);

/* --- charger --------------------------------------------------------------
 * Every setter rounds DOWN to the nearest step the chip has and returns what
 * was actually programmed through the matching getter. Steps:
 *   constant current   0..200 mA by 25, 300..1000 mA by 100   (reg 0x62)
 *   precharge          0..200 mA by 25                        (reg 0x61)
 *   termination        0..200 mA by 25                        (reg 0x63)
 *   target voltage     4000 / 4100 / 4200 / 4350 / 4400 mV    (reg 0x64)
 * ------------------------------------------------------------------------ */
esp_err_t axp2101_charge_current_set(axp2101_t *pmu, int ma);
int       axp2101_charge_current_ma(axp2101_t *pmu);
esp_err_t axp2101_precharge_current_set(axp2101_t *pmu, int ma);
int       axp2101_precharge_current_ma(axp2101_t *pmu);
esp_err_t axp2101_termination_current_set(axp2101_t *pmu, int ma);
int       axp2101_termination_current_ma(axp2101_t *pmu);
esp_err_t axp2101_charge_target_set(axp2101_t *pmu, int mv);
int       axp2101_charge_target_mv(axp2101_t *pmu);
esp_err_t axp2101_vbus_current_limit_set(axp2101_t *pmu, int ma);   /* 100/500/900/1000/1500/2000 */
int       axp2101_vbus_current_limit_ma(axp2101_t *pmu);
esp_err_t axp2101_charging_enable(axp2101_t *pmu, bool on);         /* reg 0x18 bit 1 */

/* --- thresholds -----------------------------------------------------------
 * Low battery, from the fuel gauge's percentage (reg 0x1A): the "warning"
 * level raises an IRQ at warn_pct (5..20) and the "shutdown" level another at
 * shutdown_pct (0..15). Both are only IRQs: the chip does not power off by
 * itself on them. What does power it off is VSYS falling under VOFF
 * (reg 0x24, 2.6..3.3 V), and the factory value of 2.6 V is far deeper than a
 * lithium cell likes to go.
 * ------------------------------------------------------------------------ */
esp_err_t axp2101_low_battery_levels_set(axp2101_t *pmu, int warn_pct, int shutdown_pct);
void      axp2101_low_battery_levels_get(axp2101_t *pmu, int *warn_pct, int *shutdown_pct);
esp_err_t axp2101_poweroff_voltage_set(axp2101_t *pmu, int mv);
int       axp2101_poweroff_voltage_mv(axp2101_t *pmu);

/* --- interrupts -----------------------------------------------------------
 * One 24-bit mask: reg 0x40 in bits 23..16, 0x41 in 15..8, 0x42 in 7..0,
 * the same layout as the status registers 0x48/0x49/0x4A.
 * ------------------------------------------------------------------------ */
#define AXP2101_IRQ_SOC_SHUTDOWN_LEVEL  (1u << 23)  /* SOC dropped to the shutdown level  */
#define AXP2101_IRQ_SOC_WARN_LEVEL      (1u << 22)  /* SOC dropped to the warning level   */
#define AXP2101_IRQ_GAUGE_WDT           (1u << 21)
#define AXP2101_IRQ_SOC_NEW             (1u << 20)  /* the gauge published a new percent  */
#define AXP2101_IRQ_BAT_CHG_OVER_TEMP   (1u << 19)
#define AXP2101_IRQ_BAT_CHG_UNDER_TEMP  (1u << 18)
#define AXP2101_IRQ_BAT_WORK_OVER_TEMP  (1u << 17)
#define AXP2101_IRQ_BAT_WORK_UNDER_TEMP (1u << 16)
#define AXP2101_IRQ_VBUS_INSERT         (1u << 15)
#define AXP2101_IRQ_VBUS_REMOVE         (1u << 14)
#define AXP2101_IRQ_BAT_INSERT          (1u << 13)
#define AXP2101_IRQ_BAT_REMOVE          (1u << 12)
#define AXP2101_IRQ_PKEY_SHORT          (1u << 11)  /* released before IRQLEVEL           */
#define AXP2101_IRQ_PKEY_LONG           (1u << 10)  /* still held at IRQLEVEL             */
#define AXP2101_IRQ_PKEY_NEGATIVE       (1u << 9)   /* pressed (PWRON went low)           */
#define AXP2101_IRQ_PKEY_POSITIVE       (1u << 8)   /* released                           */
#define AXP2101_IRQ_WDT_EXPIRE          (1u << 7)
#define AXP2101_IRQ_LDO_OVER_CURRENT    (1u << 6)
#define AXP2101_IRQ_BATFET_OVER_CURRENT (1u << 5)
#define AXP2101_IRQ_CHG_DONE            (1u << 4)
#define AXP2101_IRQ_CHG_START           (1u << 3)
#define AXP2101_IRQ_DIE_OVER_TEMP       (1u << 2)
#define AXP2101_IRQ_CHG_TIMEOUT         (1u << 1)
#define AXP2101_IRQ_BAT_OVER_VOLTAGE    (1u << 0)
#define AXP2101_IRQ_ALL                 (0x00FFFFFFu)

esp_err_t axp2101_irq_enable(axp2101_t *pmu, uint32_t mask);  /* replaces the three registers */
uint32_t  axp2101_irq_read_clear(axp2101_t *pmu);             /* pending bits, then cleared (W1C) */

/* --- power key timing (reg 0x27) -----------------------------------------
 * on_ms: how long PWRON must be held to power on   128 / 512 / 1000 / 2000
 * irq_ms: when the "long press" IRQ fires          1000 / 1500 / 2000 / 2500
 * off_ms: how long until the PMU powers off        4000 / 6000 / 8000 / 10000
 * ------------------------------------------------------------------------ */
esp_err_t axp2101_power_key_timing_set(axp2101_t *pmu, int on_ms, int irq_ms, int off_ms);
void      axp2101_power_key_timing_get(axp2101_t *pmu, int *on_ms, int *irq_ms, int *off_ms);

/* --- why did it start, why did it stop (regs 0x20 / 0x21) ------------------
 * The raw byte, and a short static description of the highest-priority bit.
 * 0x21 keeps the reason of the LAST power-off across the off period, which is
 * how the watch can say "the battery ran out" the next morning.
 * ------------------------------------------------------------------------ */
uint8_t     axp2101_power_on_source(axp2101_t *pmu);
uint8_t     axp2101_power_off_source(axp2101_t *pmu);
const char *axp2101_power_on_source_name(uint8_t bits);
const char *axp2101_power_off_source_name(uint8_t bits);

/* --- regulators -------------------------------------------------------------
 * On this board (schematic): DCDC1 is VCC3V3, the rail EVERYTHING hangs
 * from, including the AMOLED and the ESP32 itself. DCDC2/3/4 (0.9/1.2/1.8 V),
 * the ALDOs, BLDO2 and CPUSLDO have no consumer we could find. Read the dump
 * BEFORE switching anything off, and never touch DCDC1.
 * ------------------------------------------------------------------------ */
typedef enum {
    AXP2101_RAIL_DCDC1 = 0, AXP2101_RAIL_DCDC2, AXP2101_RAIL_DCDC3,
    AXP2101_RAIL_DCDC4, AXP2101_RAIL_DCDC5,
    AXP2101_RAIL_ALDO1, AXP2101_RAIL_ALDO2, AXP2101_RAIL_ALDO3, AXP2101_RAIL_ALDO4,
    AXP2101_RAIL_BLDO1, AXP2101_RAIL_BLDO2, AXP2101_RAIL_CPUSLDO,
    AXP2101_RAIL_DLDO1, AXP2101_RAIL_DLDO2,
    AXP2101_RAIL_COUNT
} axp2101_rail_t;

const char *axp2101_rail_name(axp2101_rail_t rail);
bool        axp2101_rail_is_enabled(axp2101_t *pmu, axp2101_rail_t rail);
int         axp2101_rail_voltage_mv(axp2101_t *pmu, axp2101_rail_t rail);   /* -1 if unknown */
esp_err_t   axp2101_rail_enable(axp2101_t *pmu, axp2101_rail_t rail, bool on);

/* Everything above, to the log, in one go. */
void axp2101_dump(axp2101_t *pmu);

void axp2101_shutdown(axp2101_t *pmu);

#ifdef __cplusplus
}
#endif
