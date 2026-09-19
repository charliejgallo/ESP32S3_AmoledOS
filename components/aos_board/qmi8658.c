/*
 * AmoledOS - QMI8658 IMU.
 *
 * On top of the waveshare/qmi8658 component we add what a watch needs: step
 * counter, orientation and wrist-raise detection.
 */
#include "aos_board.h"
#include "qmi8658.h"
#include "aos_step_detect.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include <math.h>

static const char *TAG = "aos_imu";

static qmi8658_dev_t s_imu;
static bool          s_present;

/* --- step counter --------------------------------------------------------
 * The detector is aos_step_detect.c: pure C, tuned on the desktop against
 * /api/imu dumps of counted walks (tools/steps/). It replaced a fixed
 * 1.18 g / 1.02 g threshold on the magnitude that counted 124 for 100
 * steps with the watch in a pocket (the impact and the toe-off of a stride
 * both crossed it) and needed the screen on to see anything.
 * ------------------------------------------------------------------------ */
static aos_step_detect_t s_detector;
static uint32_t s_steps;
static aos_bump_cb_t s_bump_cb;

void aos_board_imu_set_bump_cb(aos_bump_cb_t cb)
{
    s_bump_cb = cb;
}

static int      s_orientation;
static bool     s_wrist_raised;
static float    s_filtered_az = 1.0f;
static bool     s_gyro_on;

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
        ESP_LOGW(TAG, "QMI8658 not found");
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
    /* Both sensors: see the note above aos_board_imu_gyro_enable(). */
    qmi8658_enable_sensors(&s_imu, QMI8658_ENABLE_ACCEL | QMI8658_ENABLE_GYRO);
    s_gyro_on = true;

    s_present = true;
    aos_step_detect_init(&s_detector);
    ESP_LOGI(TAG, "QMI8658 ready at 0x%02X", address);
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

/* --- sample ring ----------------------------------------------------------
 * The last AOS_IMU_RING samples, as the poll saw them (every 40 ms, so a
 * minute), in PSRAM. It is what /api/imu serves: the raw material for
 * tuning the step detector against a walk of counted steps, instead of
 * against a feeling. Milli-g, and the poll's timestamp in ms.
 * ------------------------------------------------------------------------ */
static aos_imu_ring_sample_t *s_ring;
static uint32_t s_ring_head;       /* next slot to write */
static uint32_t s_ring_count;

void aos_board_imu_ring_get(aos_imu_ring_sample_t *out, uint32_t max, uint32_t *count)
{
    uint32_t n = s_ring_count < max ? s_ring_count : max;
    uint32_t start = (s_ring_head + AOS_IMU_RING - n) % AOS_IMU_RING;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = s_ring[(start + i) % AOS_IMU_RING];
    }
    *count = s_ring ? n : 0;
}

void aos_board_imu_poll(void)
{
    aos_imu_sample_t sample;
    if (!aos_board_imu_read(&sample) || !sample.valid) {
        return;
    }
    if (!s_ring) {
        s_ring = heap_caps_calloc(AOS_IMU_RING, sizeof *s_ring, MALLOC_CAP_SPIRAM);
    }
    if (s_ring) {
        aos_imu_ring_sample_t *slot = &s_ring[s_ring_head];
        slot->t_ms = (uint32_t)(esp_timer_get_time() / 1000);
        slot->ax = (int16_t)(sample.ax * 1000.0f);
        slot->ay = (int16_t)(sample.ay * 1000.0f);
        slot->az = (int16_t)(sample.az * 1000.0f);
        slot->steps = s_steps;
        s_ring_head = (s_ring_head + 1) % AOS_IMU_RING;
        if (s_ring_count < AOS_IMU_RING) {
            s_ring_count++;
        }
    }

    /* --- steps --- */
    float magnitude = sqrtf(sample.ax * sample.ax +
                            sample.ay * sample.ay +
                            sample.az * sample.az);
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_steps += (uint32_t)aos_step_detect_feed(&s_detector, now_ms, magnitude);
    if (s_bump_cb && fabsf(magnitude - 1.0f) > AOS_BUMP_G) {
        s_bump_cb(now_ms, magnitude);
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

/* Measured on the board on 2026-09-09: with CTRL7 = accelerometer only, the
 * accelerometer itself reads 0x7FFF/0x8000 garbage on every axis, and it
 * only recovers with the gyro enabled again. Whatever this driver's init
 * leaves in the other control registers, accel-only mode is not usable with
 * it, so the gyro stays on and the request is only remembered. The saving
 * (about 1 mA) is still there to be had, through the chip's gyro snooze or
 * a proper accel-only ODR, once somebody reads the QMI8658C's CTRL2/CTRL7
 * pages with the board in hand. */
void aos_board_imu_gyro_enable(bool on)
{
    static bool warned;
    if (!on && !warned) {
        warned = true;
        ESP_LOGW(TAG, "gyro off requested and ignored: accel-only mode breaks the accelerometer");
    }
}

bool aos_board_imu_gyro_enabled(void)
{
    return s_gyro_on;
}
