/**
 * @file imu_sensor.h
 * @brief IMU sensor acquisition and filtering module.
 * @details Handles MPU6500 hardware initialization, 42 Hz sampling timer,
 *          and Exponential Moving Average (EMA) acceleration filtering.
 */

#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include "FastIMU.h"
#include <Wire.h>
#include "Arduino_RouterBridge.h"

/**
 * @brief IMU I2C bus address definition (0x68 default for MPU6500).
 */
#define IMU_ADDRESS 0x68

/**
 * @brief Calibration flag macro.
 */
#define PERFORM_CALIBRATION

/**
 * @brief Target IMU sampling frequency in Hertz.
 */
#define SAMPLING_FREQ_HZ 42

/**
 * @brief Sampling interval period in microseconds.
 */
#define SAMPLING_INTERVAL_US (1000000UL / SAMPLING_FREQ_HZ)

/**
 * @brief Initializes the MPU6500 IMU sensor and performs offset calibration.
 * @return void
 */
void init_imu(void);

/**
 * @brief Periodic IMU update function called in loop().
 * @details Checks micros() timestamp, updates IMU measurements at 42 Hz,
 *          applies EMA filtering, and calls Bridge "record_sensor_movement".
 * @return void
 */
void update_imu_sample(void);

#endif /* IMU_SENSOR_H */
