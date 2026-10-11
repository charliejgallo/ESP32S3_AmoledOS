/*
 * The few lines of aos_hal.h that sim/ble_sim.c needs, so test_decode.c can
 * run the simulator's made-up neighbourhood on the Mac without the rest of
 * the HAL. The types are copied from components/aos_hal/include/aos_hal.h:
 * if those change, these must follow. The clock and the Bluetooth switch
 * are the test's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint64_t aos_hal_uptime_ms(void);
bool     aos_hal_bt_enabled(void);

enum {
    AOS_BLE_ADV_IND = 0,
    AOS_BLE_ADV_DIRECT_IND,
    AOS_BLE_ADV_SCAN_IND,
    AOS_BLE_ADV_NONCONN_IND,
    AOS_BLE_ADV_SCAN_RSP,
};

typedef struct {
    uint32_t t_ms;
    uint8_t  addr[6];
    uint8_t  addr_type;
    uint8_t  kind;
    int8_t   rssi;
    uint8_t  len;
    uint8_t  data[31];
} aos_ble_adv_t;

bool     aos_hal_ble_scan_start(bool active, int duty_pct);
void     aos_hal_ble_scan_stop(void);
bool     aos_hal_ble_scanning(void);
int      aos_hal_ble_scan_read(aos_ble_adv_t *out, int max);
uint32_t aos_hal_ble_scan_lost(void);

typedef enum {
    AOS_BLE_GATT_IDLE = 0,
    AOS_BLE_GATT_CONNECTING,
    AOS_BLE_GATT_DISCOVERING,
    AOS_BLE_GATT_READY,
    AOS_BLE_GATT_FAILED,
} aos_ble_gatt_state_t;

enum { AOS_BLE_ATTR_SERVICE = 0, AOS_BLE_ATTR_CHAR, AOS_BLE_ATTR_DESC };

typedef struct {
    uint8_t  kind;
    uint8_t  props;
    uint16_t handle;
    uint16_t end;
    uint8_t  uuid_len;
    uint8_t  uuid[16];
} aos_ble_attr_t;

enum {
    AOS_BLE_EV_READ = 0,
    AOS_BLE_EV_NOTIFY,
    AOS_BLE_EV_INDICATE,
    AOS_BLE_EV_WRITE,
    AOS_BLE_EV_SUBSCRIBE,
};

typedef struct {
    uint32_t t_ms;
    uint8_t  type;
    uint8_t  _pad;
    uint16_t handle;
    int16_t  status;
    uint16_t len;
    uint8_t  data[512];
} aos_ble_gatt_ev_t;

bool aos_hal_ble_gatt_connect(const uint8_t addr[6], uint8_t addr_type);
void aos_hal_ble_gatt_disconnect(void);
aos_ble_gatt_state_t aos_hal_ble_gatt_state(int *reason);
uint16_t aos_hal_ble_gatt_mtu(void);
bool     aos_hal_ble_gatt_rssi(int8_t *rssi);
int  aos_hal_ble_gatt_attrs(aos_ble_attr_t *out, int max);
bool aos_hal_ble_gatt_read(uint16_t handle);
bool aos_hal_ble_gatt_write(uint16_t handle, const void *data, size_t len, bool response);
bool aos_hal_ble_gatt_subscribe(uint16_t value_handle, int mode);
int  aos_hal_ble_gatt_events(aos_ble_gatt_ev_t *out, int max);
