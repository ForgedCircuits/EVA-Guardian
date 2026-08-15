/**
 * @file bms_storage.h
 * @brief EEPROM persistent storage management for the BMS.
 * @details Handles initializing the EEPROM and loading/saving the BMS state
 *          (such as cycle counts and max capacity) to persist across reboots.
 */

#ifndef BMS_STORAGE_H
#define BMS_STORAGE_H

#include <Arduino.h>

/**
 * @struct BMSState
 * @brief Represents the persistent state of the battery.
 */
struct BMSState {
    float cycleCount;           /**< @brief Total equivalent full charge/discharge cycles. */
    float actualMaxCapacityAh;  /**< @brief Actual maximum capacity in Amp-hours, accounting for degradation. */
};

extern BMSState persistentState;
extern bool emmcInitialized;

/**
 * @brief Initializes the EEPROM adapter.
 * @details Call this during setup() to prepare the EEPROM adapter and load the saved state.
 */
void initEMMC();

/**
 * @brief Loads the BMS state from EEPROM.
 * @details Reads the state and handles corruption (e.g., first boot or bad data)
 *          by reverting to factory defaults.
 */
void loadBMSState();

/**
 * @brief Saves the current BMS state to EEPROM.
 * @details Writes the `persistentState` struct to the EEPROM. Should be called periodically.
 */
void saveBMSState();

#endif // BMS_STORAGE_H
