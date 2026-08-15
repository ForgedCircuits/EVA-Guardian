/**
 * @file bms_storage.cpp
 * @brief Implementation of EEPROM storage for the BMS.
 */

#include "bms_storage.h"
#include <EEPROMAdapter.h>

const uint16_t EEPROM_START_ADDRESS = 0;
const float BATTERY_NOMINAL_CAPACITY_AH = 18.650f;

BMSState persistentState;
bool emmcInitialized = false;
EEPROM::Adapter eepromAdapter = EEPROM::Adapter();

void initEMMC() {
    Serial.println("Initializing EEPROM...");
    eepromAdapter.init();
    emmcInitialized = true;
    loadBMSState();
}

void loadBMSState() {
    uint8_t* ptr = (uint8_t*)&persistentState;
    for (size_t i = 0; i < sizeof(persistentState); i++) {
        *ptr++ = eepromAdapter.readChip(EEPROM_START_ADDRESS + i);
    }

    bool corrupt = isnan(persistentState.actualMaxCapacityAh)
                || (persistentState.actualMaxCapacityAh < 1.0f)
                || (persistentState.actualMaxCapacityAh > 100.0f)
                || isnan(persistentState.cycleCount)
                || (persistentState.cycleCount < 0.0f);

    if (corrupt) {
        persistentState.cycleCount        = 0.0f;
        persistentState.actualMaxCapacityAh = BATTERY_NOMINAL_CAPACITY_AH;
        saveBMSState();
        Serial.println("Fresh EEPROM - default values written.");
    } else {
        Serial.print("EEPROM loaded: MaxCap=");
        Serial.print(persistentState.actualMaxCapacityAh, 3);
        Serial.print(" Ah, Cycles=");
        Serial.println(persistentState.cycleCount, 1);
    }
}

void saveBMSState() {
    uint8_t* ptr = (uint8_t*)&persistentState;
    for (size_t i = 0; i < sizeof(persistentState); i++) {
        eepromAdapter.writeChip(EEPROM_START_ADDRESS + i, *ptr++);
    }
    Serial.println("State saved to EEPROM.");
}
