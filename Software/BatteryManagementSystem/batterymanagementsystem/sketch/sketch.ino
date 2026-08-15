/**
 * @file sketch.ino
 * @brief Main entry point for the Battery Management System (BMS).
 * @details This sketch orchestrates the BMS by calling into the sensor reading,
 *          SOC/SOH calculation, EEPROM storage, display rendering, and RS485
 *          communication modules on a cooperative multitasking loop.
 */

#include "bms_storage.h"
#include "bms_sensors.h"
#include "bms_soc.h"
#include "matrix_display.h"
#include "rs485_slave.h"

// Timing Configurations
const unsigned long READ_INTERVAL_MS  = 100;
const unsigned long PRINT_INTERVAL_MS = 1000;
const unsigned long SAVE_INTERVAL_MS  = 60000;

unsigned long lastReadTime  = 0;
unsigned long lastPrintTime = 0;
unsigned long lastSaveTime  = 0;

void setup() {
    Serial.begin(115200);
    analogReadResolution(14);

    initEMMC();
    init_matrix_display();
    initSensors();

    Serial.println("Waiting for sensors to stabilize...");
    delay(3000);

    // Boot-time OCV initialization
    float bootVoltage = readRawVoltage(10);
    initSOC(bootVoltage);

    Serial.println("======= BOOT CALIBRATION =======");
    Serial.print("  Boot Voltage   : "); Serial.print(bootVoltage, 3); Serial.println(" V");
    Serial.print("  Boot SOC (OCV) : "); Serial.print(systemSOC, 1);    Serial.println(" %");
    Serial.print("  Coulomb counter: "); Serial.print(accumulatedAh, 3); Serial.println(" Ah");
    Serial.print("  Max Capacity   : "); Serial.print(persistentState.actualMaxCapacityAh, 3); Serial.println(" Ah");
    Serial.println("================================");

    lastReadTime  = millis();
    lastPrintTime = millis();
    lastSaveTime  = millis();

    /* Initialize RS485 slave (Serial2 @ 115200) */
    rs485_slave_init();
}

void loop() {
    unsigned long currentTime = millis();

    if (currentTime - lastReadTime >= READ_INTERVAL_MS) {
        unsigned long timeDelta = currentTime - lastReadTime;
        lastReadTime = currentTime;

        readSensors();
        integrateCoulombs(timeDelta);
        calculateSOC();
        calculateSOH();
    }

    if (currentTime - lastPrintTime >= PRINT_INTERVAL_MS) {
        lastPrintTime = currentTime;
        render_soc(systemSOC);
    }

    if (currentTime - lastSaveTime >= SAVE_INTERVAL_MS) {
        lastSaveTime = currentTime;
        saveBMSState();
    }

    /* Service RS485 slave — non-blocking, handles command parsing and response TX */
    rs485_slave_poll();
}
