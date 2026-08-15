/**
 * @file bms_sensors.h
 * @brief Analog sensor data acquisition module.
 * @details Handles sampling and filtering of the battery pack's voltage, current,
 *          and temperature sensors via the Arduino ADC.
 */

#ifndef BMS_SENSORS_H
#define BMS_SENSORS_H

#include <Arduino.h>

extern float systemVoltage;
extern float systemCurrent;
extern float systemTemperature;
extern bool isFirstRead;

/**
 * @brief Initializes the sensor pins and state.
 * @details Should be called once in setup().
 */
void initSensors();

/**
 * @brief Reads all analog sensors.
 * @details Reads the current (A0), voltage (A1), and temperature (A2) pins,
 *          applies calibration constants, and filters the result using an
 *          Exponential Moving Average (EMA). Updates systemVoltage, 
 *          systemCurrent, and systemTemperature globals.
 */
void readSensors();

/**
 * @brief Reads raw voltage for boot calibration without filtering.
 * @param samples The number of samples to average.
 * @return The raw averaged voltage in volts.
 */
float readRawVoltage(int samples = 5);

#endif // BMS_SENSORS_H
