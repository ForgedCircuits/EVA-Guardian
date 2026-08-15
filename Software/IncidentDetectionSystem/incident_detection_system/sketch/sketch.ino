/**
 * @file sketch.ino
 * @brief Incident Detection & Telematics System — Arduino Main Firmware Entry.
 * @details Initializes IMU sensor module and LED Matrix display module,
 *          then runs periodic 42 Hz sensor sampling loop.
 *
 * @author Incident Telematics System Team
 * @version 2.0.0
 */

#include "imu_sensor.h"
#include "matrix_display.h"
#include "rs485_master.h"

/**
 * @brief Arduino hardware setup function.
 * @details Initializes Serial, RouterBridge communication link, LED matrix peripheral,
 *          and MPU6500 accelerometer hardware.
 * @return void
 */
void setup() {
  Serial.begin(115200);

  /* Initialize Router Bridge subsystem */
  Bridge.begin();

  /* Initialize LED Matrix display module */
  init_matrix_display();

  /* Initialize MPU6500 IMU sensor module */
  init_imu();

  /* Set ADC resolution to 14-bit (matches BMS board config) */
  analogReadResolution(14);

  /* Initialize RS485 master (Serial2 @ 115200) — Serial1 is reserved by Bridge */
  rs485_master_init();
}

/**
 * @brief Arduino main execution loop.
 * @details Periodically invokes update_imu_sample() to acquire acceleration data
 *          at 42 Hz and dispatch data across the Bridge.
 * @return void
 */
void loop() {
  update_imu_sample();

  /* Poll RS485 master — transmits bat_status every 2s and parses BMS responses */
  rs485_master_poll();
}