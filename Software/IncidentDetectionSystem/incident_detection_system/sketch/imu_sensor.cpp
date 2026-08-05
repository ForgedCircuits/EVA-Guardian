/**
 * @file imu_sensor.cpp
 * @brief Implementation of IMU sensor sampling and filtering logic.
 * @details Implements MPU6500 driver functions and EMA filtering.
 */

#include "imu_sensor.h"
#include "matrix_display.h"
#include <math.h>

static MPU6500 IMU;
static calData calib = { 0 };
static AccelData accelData;

static const float DT = 1.0f / SAMPLING_FREQ_HZ;
static const float ALPHA = 0.25f;

static unsigned long lastSampleTime = 0;
static float filtAccX = 0.0f;
static float filtAccY = 0.0f;
static float filtAccZ = 0.0f;
static bool isFirstSample = true;

/**
 * @brief Initializes the MPU6500 IMU hardware and performs calibration.
 * @return void
 */
void init_imu(void) {
  Wire.begin();
  Wire.setClock(400000);  /* 400 kHz I2C Fast Mode */

  IMU.init(calib, IMU_ADDRESS);

#ifdef PERFORM_CALIBRATION
  delay(1000);
  IMU.calibrateAccelGyro(&calib);
  IMU.init(calib, IMU_ADDRESS);
#endif
}

/**
 * @brief Periodic IMU sampling worker function.
 * @details Reads raw accelerometer values, applies EMA filter, and dispatches data via Bridge.
 * @return void
 */
void update_imu_sample(void) {
  if (micros() - lastSampleTime >= SAMPLING_INTERVAL_US) {
    lastSampleTime += SAMPLING_INTERVAL_US;

    IMU.update();
    IMU.getAccel(&accelData);

    float rawAccX = accelData.accelX;
    float rawAccY = accelData.accelY;
    float rawAccZ = accelData.accelZ;

    /* Exponential Moving Average (EMA) filtering */
    if (isFirstSample) {
      filtAccX = rawAccX;
      filtAccY = rawAccY;
      filtAccZ = rawAccZ;
      isFirstSample = false;
    } else {
      filtAccX = (ALPHA * rawAccX) + ((1.0f - ALPHA) * filtAccX);
      filtAccY = (ALPHA * rawAccY) + ((1.0f - ALPHA) * filtAccY);
      filtAccZ = (ALPHA * rawAccZ) + ((1.0f - ALPHA) * filtAccZ);
    }

    /* Calculate total acceleration (normalized vector in m/s^2) neglecting gravity on Z */
    float accX_ms2 = filtAccX * 9.8f;
    float accY_ms2 = filtAccY * 9.8f;
    float accZ_ms2 = filtAccZ * 9.8f;
    float accZ_ms2_no_g = accZ_ms2 - 9.8f;
    float totalAcc_ms2 = sqrt(accX_ms2 * accX_ms2 + accY_ms2 * accY_ms2 + accZ_ms2_no_g * accZ_ms2_no_g);

    /* Calculate tilt (roll) angle and animate water level indicator */
    float roll_angle = atan2(-filtAccY, filtAccZ);
    render_water_level(roll_angle);

    /* Dispatch filtered telemetry to Python backend */
    Bridge.call("record_sensor_movement",
                filtAccX,
                filtAccY,
                filtAccZ,
                totalAcc_ms2);
  }
}
