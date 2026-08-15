/**
 * @file bms_soc.h
 * @brief State of Charge and Health calculation module.
 * @details Implements Coulomb counting (Ah integration) and OCV (Open Circuit Voltage)
 *          recalibration for calculating the battery's SOC and SOH.
 */

#ifndef BMS_SOC_H
#define BMS_SOC_H

#include <Arduino.h>

extern float systemSOC;
extern float systemSOH;
extern float accumulatedAh;

/**
 * @brief Boot-time initialization for SOC.
 * @details Uses raw OCV voltage to set the initial SOC and Ah.
 * @param bootVoltage The raw voltage at boot.
 */
void initSOC(float bootVoltage);

/**
 * @brief Converts resting voltage to SOC percentage using LUT.
 * @param voltage The resting voltage in volts.
 * @return SOC percentage (0.0 to 100.0).
 */
float get_soc_from_ocv(float voltage);

/**
 * @brief Integrates current to track accumulated charge.
 * @param timeDeltaMs The time since the last integration step in milliseconds.
 */
void integrateCoulombs(unsigned long timeDeltaMs);

/**
 * @brief Calculates current State of Charge (SOC).
 * @details Blends Coulomb counting and OCV based on resting state.
 */
void calculateSOC();

/**
 * @brief Calculates current State of Health (SOH).
 * @details Monitors capacity fade via Ah throughput and cycle count.
 */
void calculateSOH();

#endif // BMS_SOC_H
