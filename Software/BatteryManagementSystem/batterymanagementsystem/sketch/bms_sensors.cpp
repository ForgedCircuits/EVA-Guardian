/**
 * @file bms_sensors.cpp
 * @brief Implementation of the BMS sensor reading logic.
 */

#include "bms_sensors.h"

// Hardware Config
const int CURRENT_PIN = A0;
const int VOLTAGE_PIN = A1;
const int TEMP_PIN    = A2;

// ADC
const float ADC_REF_VOLTAGE = 3.3f;
const float ADC_MAX_VALUE   = 16383.0f;

// Current Sensor (ACS723)
const float CURRENT_ZERO_POINT_V       = 1.70f;
const float CURRENT_SENSITIVITY_V_PER_A = 1.336f;

// Voltage Divider
const float R1_VOLTAGE_DIVIDER = 6800.0f;
const float R2_VOLTAGE_DIVIDER = 3000.0f;
const float VOLTAGE_MULTIPLIER = (R1_VOLTAGE_DIVIDER + R2_VOLTAGE_DIVIDER) / R2_VOLTAGE_DIVIDER;

// Temperature Sensor
const float NTC_VCC               = 5.0f;
const float NTC_R_FIXED           = 3300.0f;
const float NTC_R_SERIES          = 3300.0f;
const float NTC_NOMINAL_RESISTANCE = 10000.0f;
const float NTC_NOMINAL_TEMP      = 298.15f;
const float NTC_BETA              = 3950.0f;
const float FILTER_ALPHA          = 0.1f;

// Live State
float systemVoltage     = 0.0f;
float systemCurrent     = 0.0f;
float systemTemperature = 0.0f;
bool isFirstRead        = true;

void initSensors() {
    isFirstRead = true;
}

float readRawVoltage(int samples) {
    float sum = 0.0f;
    for (int i = 0; i < samples; i++) {
        int adc = analogRead(VOLTAGE_PIN);
        float pinV = ((float)adc / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
        sum += pinV * VOLTAGE_MULTIPLIER;
        delay(10);
    }
    return sum / (float)samples;
}

void readSensors() {
    // Current (A0)
    int   currentADC      = analogRead(CURRENT_PIN);
    float currentPinV     = ((float)currentADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    // NEGATIVE = discharge/load, POSITIVE = charging
    float rawCurrent      = (currentPinV - CURRENT_ZERO_POINT_V) / CURRENT_SENSITIVITY_V_PER_A;

    // Voltage (A1)
    int   voltageADC      = analogRead(VOLTAGE_PIN);
    float voltagePinV     = ((float)voltageADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float rawVoltage      = voltagePinV * VOLTAGE_MULTIPLIER;

    // Temperature (A2) - Steinhart-Hart NTC
    int   tempADC         = analogRead(TEMP_PIN);
    float tempPinV        = ((float)tempADC / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
    float rawTemp         = -999.0f;
    if (tempPinV > 0.01f) {
        float rNTC    = (NTC_VCC * NTC_R_FIXED / tempPinV) - (NTC_R_FIXED + NTC_R_SERIES);
        if (rNTC > 0.0f) {
            float s = log(rNTC / NTC_NOMINAL_RESISTANCE);
            s = (s / NTC_BETA) + (1.0f / NTC_NOMINAL_TEMP);
            rawTemp = (1.0f / s) - 273.15f;
        }
    }

    // EMA filter
    if (isFirstRead) {
        systemCurrent     = rawCurrent;
        systemVoltage     = rawVoltage;
        systemTemperature = rawTemp;
        isFirstRead       = false;
    } else {
        systemCurrent     = FILTER_ALPHA * rawCurrent + (1.0f - FILTER_ALPHA) * systemCurrent;
        systemVoltage     = FILTER_ALPHA * rawVoltage + (1.0f - FILTER_ALPHA) * systemVoltage;
        if (rawTemp > -900.0f) {
            systemTemperature = FILTER_ALPHA * rawTemp + (1.0f - FILTER_ALPHA) * systemTemperature;
        }
    }
}
