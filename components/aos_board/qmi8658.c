/*
 * AmoledOS - QMI8658 IMU.
 *
 * On top of the waveshare/qmi8658 component we add what a watch needs: step
 * counter, orientation and wrist-raise detection.
 */
#include "aos_board.h"
#include "qmi8658.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>

static const char *TAG = "aos_imu";

static qmi8658_dev_t s_imu;
static bool          s_present;

/* --- step counter --------------------------------------------------------
 * Peak detection over the magnitude of the acceleration, with hysteresis and a
 * 250 ms dead time so bounces are not counted. It is simple but behaves
 * reasonably well on the wrist; if more accuracy is needed, the QMI8658 has a
 * hardware pedometer that can be enabled later.
 * ------------------------------------------------------------------------ */
#define STEP_HIGH_G     1.18f
#define STEP_LOW_G      1.02f
#define STEP_DEAD_US    250000

static uint32_t s_steps;
static bool     s_above;
static int64_t  s_last_step_us;

static int      s_orientation;
static bool     s_wrist_raised;
static float    s_filtered_az = 1.0f;

bool aos_imu_start(i2c_master_bus_handle_t bus)
{
    uint8_t address = 0;
    const uint8_t candidates[] = { QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW };
    for (unsigned i = 0; i < sizeof(candidates); i++) {
        if (i2c_master_probe(bus, candidates[i], 100) == ESP_OK) {
            address = candidates[i];
            break;
        }
    }
    if (address == 0) {
        ESP_LOGW(TAG, "QMI8658 no encontrado");
        return false;
    }

    if (qmi8658_init(&s_imu, bus, address) != ESP_OK) {
        return false;
    }

    qmi8658_set_accel_range(&s_imu, QMI8658_ACCEL_RANGE_4G);
    qmi8658_set_accel_odr(&s_imu, QMI8658_ACCEL_ODR_125HZ);
    qmi8658_set_gyro_range(&s_imu, QMI8658_GYRO_RANGE_256DPS);
    qmi8658_set_gyro_odr(&s_imu, QMI8658_GYRO_ODR_125HZ);
    qmi8658_set_accel_unit_mps2(&s_imu, false);   /* we want g */
    qmi8658_set_gyro_unit_dps(&s_imu, true);
    qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);

    s_present = true;
    ESP_LOGI(TAG, "QMI8658 listo en 0x%02X", address);
    return true;
}

bool aos_board_imu_read(aos_imu_sample_t *out)
{
    if (!s_present || !out) {
        return false;
    }
    qmi8658_data_t data = {0};
    if (qmi8658_read_sensor_data(&s_imu, &data) != ESP_OK) {
        out->valid = false;
        return false;
    }
    /* The QMI8658's driver returns MILLI-g: (raw * 1000) / accel_lsb_div.
     * aos_hal.h's contract says g, and everything consuming this uses
     * thresholds in g: FLAT_THRESHOLD_G 0.72, the 0.7/0.6 of the orientation
     * below, and the games that do (ax - zero) * 1000 to get back to milli-g.
     * Measured on the board: flat and face up gave az = -1022. Without this
     * division the level stayed ALWAYS in flat mode and Activity's bubble was
     * pinned against the edge. */
    out->ax = data.accelX / 1000.0f;
    out->ay = data.accelY / 1000.0f;
    out->az = data.accelZ / 1000.0f;
    out->gx = data.gyroX;
    out->gy = data.gyroY;
    out->gz = data.gyroZ;
    out->temperature = data.temperature;
    out->valid = true;
    return true;
}

void aos_board_imu_poll(void)
{
    aos_imu_sample_t sample;
    if (!aos_board_imu_read(&sample) || !sample.valid) {
        return;
    }

    /* --- steps --- */
    float magnitude = sqrtf(sample.ax * sample.ax +
                            sample.ay * sample.ay +
                            sample.az * sample.az);
    int64_t now = esp_timer_get_time();

    if (!s_above && magnitude > STEP_HIGH_G) {
        s_above = true;
        if (now - s_last_step_us > STEP_DEAD_US) {
            s_steps++;
            s_last_step_us = now;
        }
    } else if (s_above && magnitude < STEP_LOW_G) {
        s_above = false;
    }

    /* --- orientation --- */
    /* Mapping MEASURED on the board on 2026-08-28, with the four postures:
     *   flat, face up            -> az = -1.02   (that is: face up is -az)
     *   standing, upright        -> ax = +1.02
     *   lying on its right side  -> ay = -0.99   (the right of the screen is -ay)
     *   standing, upside down    -> ax = -0.98
     * az was inverted and ax/ay were crossed: ax is the screen's VERTICAL axis
     * and ay the HORIZONTAL one, not the other way round. */
    if (sample.az < -0.7f) {
        s_orientation = 4;              /* AOS_ORIENT_FACE_UP   */
    } else if (sample.az > 0.7f) {
        s_orientation = 5;              /* AOS_ORIENT_FACE_DOWN */
    } else if (sample.ax > 0.6f) {
        s_orientation = 0;              /* AOS_ORIENT_UP        */
    } else if (sample.ax < -0.6f) {
        s_orientation = 1;              /* AOS_ORIENT_DOWN      */
    } else if (sample.ay < -0.6f) {
        s_orientation = 3;              /* AOS_ORIENT_RIGHT     */
    } else if (sample.ay > 0.6f) {
        s_orientation = 2;              /* AOS_ORIENT_LEFT      */
    }

    /* --- wrist raise ---
     * The screen goes from facing the floor to facing the user: az falls and
     * ay rises. A low-pass filter so it is not triggered by any old shake. */
    s_filtered_az = s_filtered_az * 0.8f + sample.az * 0.2f;
    s_wrist_raised = (s_filtered_az < 0.45f && sample.ay > 0.35f);
}

uint32_t aos_board_imu_steps(void)
{
    return s_steps;
}

void aos_board_imu_steps_reset(void)
{
    s_steps = 0;
}

int aos_board_imu_orientation(void)
{
    return s_orientation;
}

bool aos_board_imu_wrist_raised(void)
{
    return s_wrist_raised;
}
