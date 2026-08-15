/**
 * @file bms_soc.cpp
 * @brief Implementation of SOC and SOH logic.
 */

#include "bms_soc.h"
#include "bms_storage.h"
#include "bms_sensors.h"

// Battery Config
const float BATTERY_NOMINAL_CAPACITY_AH = 18.650f;
const float CHARGE_EFFICIENCY           = 0.98f;
const float REST_CURRENT_THRESHOLD_A    = 0.05f;
const unsigned long REST_DURATION_MS    = 60000;
const unsigned long READ_INTERVAL_MS    = 100;

// OCV LUT
typedef struct { float voltage; float soc; } OCV_Point_t;

const OCV_Point_t OCV_LUT[] = {
    {6.00f,   0.0f},
    {7.20f,  10.0f},
    {7.40f,  20.0f},
    {7.50f,  30.0f},
    {7.60f,  45.0f},
    {7.80f,  60.0f},
    {8.00f,  75.0f},
    {8.20f,  90.0f},
    {8.40f, 100.0f}
};
const uint8_t OCV_LUT_SIZE = sizeof(OCV_LUT) / sizeof(OCV_LUT[0]);

// Live State
float systemSOC = 50.0f;
float systemSOH = 100.0f;
float accumulatedAh = 0.0f;
static unsigned long rest_timer_ms = 0;

float get_soc_from_ocv(float voltage) {
    if (voltage <= OCV_LUT[0].voltage)                  return OCV_LUT[0].soc;
    if (voltage >= OCV_LUT[OCV_LUT_SIZE - 1].voltage)   return OCV_LUT[OCV_LUT_SIZE - 1].soc;
    for (uint8_t i = 0; i < OCV_LUT_SIZE - 1; i++) {
        if (voltage >= OCV_LUT[i].voltage && voltage <= OCV_LUT[i+1].voltage) {
            float ratio = (voltage - OCV_LUT[i].voltage) / (OCV_LUT[i+1].voltage - OCV_LUT[i].voltage);
            return OCV_LUT[i].soc + ratio * (OCV_LUT[i+1].soc - OCV_LUT[i].soc);
        }
    }
    return 50.0f;
}

void initSOC(float bootVoltage) {
    systemSOC = get_soc_from_ocv(bootVoltage);
    accumulatedAh = (systemSOC / 100.0f) * persistentState.actualMaxCapacityAh;
}

void integrateCoulombs(unsigned long timeDeltaMs) {
    float dt_hours = (float)timeDeltaMs / 3600000.0f;

    if (systemCurrent > 0.0f) {
        accumulatedAh += systemCurrent * CHARGE_EFFICIENCY * dt_hours;
    } else {
        accumulatedAh += systemCurrent * dt_hours;
    }

    if (accumulatedAh > persistentState.actualMaxCapacityAh) {
        accumulatedAh = persistentState.actualMaxCapacityAh;
    }
    if (accumulatedAh < 0.0f) {
        accumulatedAh = 0.0f;
    }
}

void calculateSOC() {
    bool isAtRest = (fabs(systemCurrent) < REST_CURRENT_THRESHOLD_A);

    if (isAtRest) {
        rest_timer_ms += READ_INTERVAL_MS;
    } else {
        rest_timer_ms = 0;
    }

    if (rest_timer_ms >= REST_DURATION_MS) {
        float ocvSOC = get_soc_from_ocv(systemVoltage);
        accumulatedAh = (ocvSOC / 100.0f) * persistentState.actualMaxCapacityAh;
        rest_timer_ms = 0;
        Serial.print("[OCV Recal] Voltage="); Serial.print(systemVoltage, 2);
        Serial.print("V OCV_SOC="); Serial.print(ocvSOC, 1);
        Serial.println("%");
    }

    systemSOC = (accumulatedAh / persistentState.actualMaxCapacityAh) * 100.0f;

    if (systemSOC > 100.0f) systemSOC = 100.0f;
    if (systemSOC <   0.0f) systemSOC =   0.0f;
}

void calculateSOH() {
    static float cycleAhTracker  = 0.0f;
    static float lastAh          = -1.0f;

    if (lastAh < 0.0f) { lastAh = accumulatedAh; return; }

    cycleAhTracker += fabs(accumulatedAh - lastAh);
    lastAh = accumulatedAh;

    if (cycleAhTracker >= (BATTERY_NOMINAL_CAPACITY_AH * 2.0f)) {
        persistentState.cycleCount++;
        cycleAhTracker = 0.0f;
        persistentState.actualMaxCapacityAh -= BATTERY_NOMINAL_CAPACITY_AH * 0.0002f;
        if (persistentState.actualMaxCapacityAh < 0.0f) {
            persistentState.actualMaxCapacityAh = 0.0f;
        }
        saveBMSState();
    }

    systemSOH = (persistentState.actualMaxCapacityAh / BATTERY_NOMINAL_CAPACITY_AH) * 100.0f;
}
