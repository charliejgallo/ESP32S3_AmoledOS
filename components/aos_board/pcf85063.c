/*
 * AmoledOS - PCF85063A RTC.
 *
 * Wraps the waveshare/pcf85063a component and translates to struct tm. The RTC
 * has a battery of its own, so the time survives a complete power-down.
 */
#include "aos_board.h"
#include "pcf85063a.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "aos_rtc";

static pcf85063a_dev_t s_rtc;
static bool            s_present;

bool aos_rtc_start(i2c_master_bus_handle_t bus)
{
    if (pcf85063a_init(&s_rtc, bus, PCF85063A_ADDRESS) != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063A does not answer at 0x%02X", PCF85063A_ADDRESS);
        s_present = false;
        return false;
    }
    s_present = true;
    ESP_LOGI(TAG, "PCF85063A listo");
    return true;
}

bool aos_board_rtc_present(void)
{
    return s_present;
}

bool aos_board_rtc_get(struct tm *out)
{
    if (!s_present || !out) {
        return false;
    }
    pcf85063a_datetime_t value = {0};
    if (pcf85063a_get_time_date(&s_rtc, &value) != ESP_OK) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->tm_year = (int)value.year - 1900;
    out->tm_mon  = (int)value.month - 1;
    out->tm_mday = value.day;
    out->tm_wday = value.dotw;
    out->tm_hour = value.hour;
    out->tm_min  = value.min;
    out->tm_sec  = value.sec;
    return true;
}

bool aos_board_rtc_set(const struct tm *value)
{
    if (!s_present || !value) {
        return false;
    }
    pcf85063a_datetime_t dt = {
        .year  = (uint16_t)(value->tm_year + 1900),
        .month = (uint8_t)(value->tm_mon + 1),
        .day   = (uint8_t)value->tm_mday,
        .dotw  = (uint8_t)value->tm_wday,
        .hour  = (uint8_t)value->tm_hour,
        .min   = (uint8_t)value->tm_min,
        .sec   = (uint8_t)value->tm_sec,
    };
    return pcf85063a_set_time_date(&s_rtc, dt) == ESP_OK;
}

bool aos_board_rtc_alarm_set(int hour, int minute)
{
    if (!s_present) {
        return false;
    }
    /* The PCF85063A's alarm compares field by field; we leave day and date
     * disabled (bit 7 set) by writing out-of-range values. */
    pcf85063a_datetime_t alarm = {
        .year  = 0,
        .month = 0,
        .day   = 0x80,
        .dotw  = 0x80,
        .hour  = (uint8_t)hour,
        .min   = (uint8_t)minute,
        .sec   = 0x80,
    };
    if (pcf85063a_set_alarm(&s_rtc, alarm) != ESP_OK) {
        return false;
    }
    return pcf85063a_enable_alarm(&s_rtc) == ESP_OK;
}

void aos_board_rtc_alarm_clear(void)
{
    if (!s_present) {
        return;
    }
    uint8_t flag = 0;
    pcf85063a_get_alarm_flag(&s_rtc, &flag);
}
