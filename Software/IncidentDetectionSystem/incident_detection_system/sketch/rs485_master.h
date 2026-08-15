/**
 * @file rs485_master.h
 * @brief RS485 UART Master communication module for Incident Detection System.
 * @details Implements a half-duplex RS485 master that polls the BatteryManagementSystem
 *          slave every 2 seconds for live telemetry data and dispatches the result
 *          to the Python backend via the RouterBridge.
 *
 * Packet Format (Command — Master → Slave):
 *   [0x55][0xAA][LEN=0x01][CMD=0x01][CRC][0xAA]
 *
 * Packet Format (Response — Slave → Master):
 *   [0x55][0xAA][LEN][Voltage,Current,Temp,OCV,CoulombCount,SOC,SOH][CRC][0xAA]
 *
 * Hardware: Serial2 @ 115200 baud.
 *           (Serial1 is reserved by Arduino_RouterBridge — cannot be shared.)
 *           Auto-direction RS485 transceiver (no DE/RE GPIO required).
 *
 * @note Wire the RS485 transceiver to the Serial2 TX/RX pins on the board.
 *       Check your specific board's pinout for Serial2 pin mapping.
 */

#ifndef RS485_MASTER_H
#define RS485_MASTER_H

#include <Arduino.h>
#include "Arduino_RouterBridge.h"

/* ── Protocol Frame Constants ──────────────────────────────────── */
/** @brief Start-of-Frame byte 1. */
#define RS485_SOF1          (0x55u)

/** @brief Start-of-Frame byte 2 / End-of-Frame byte. */
#define RS485_SOF2_EOF      (0xAAu)

/** @brief BMS status request command identifier. */
#define CMD_BAT_STATUS      (0x01u)

/* ── Hardware Binding ───────────────────────────────────────────── */
/** @brief Hardware serial port used for RS485. */
#define RS485_SERIAL        Serial1

/** @brief RS485 communication baud rate. */
#define RS485_BAUD          115200UL

/* ── Diagnostic Constants ───────────────────────────────────────── */
#define DIAGNOSTIC_TIMEOUT_MS  30000UL // 30 seconds without valid response
#define LOOPBACK_TEST_WAIT_MS  1000UL  // Wait 1 second for loopback to return
#define BMS_BT_TEST_WAIT_MS    5000UL  // Wait 5 seconds for BMS BT response

// Loopback GPIO pins
#define LB_PIN_2 2
#define LB_PIN_3 3
#define LB_PIN_4 4
#define LB_PIN_5 5

/**
 * @brief Diagnostic loopback command configuration.
 */
#define CMD_LOOPBACK_TEST   0x02
#define LOOPBACK_PAYLOAD_1  0x10
#define LOOPBACK_PAYLOAD_2  0x10

/* ── Polling Configuration ──────────────────────────────────────── */
/** @brief Interval between BMS poll cycles in milliseconds (2 seconds). */
#define BMS_POLL_INTERVAL_MS  2000UL

/** @brief Maximum response payload buffer size in bytes. */
#define RS485_RX_BUF_SIZE   200u

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Initializes RS485 master hardware (Serial2 @ 115200).
 * @details Must be called once in setup() after Bridge.begin().
 * @return void
 */
void rs485_master_init(void);

/**
 * @brief Non-blocking RS485 poll handler. Call in every loop() iteration.
 * @details Every 2 seconds, transmits a bat_status command to the BMS slave,
 *          then runs a byte-level state machine to parse the incoming response.
 *          On a valid, CRC-verified response, dispatches telemetry floats to
 *          the Python backend via Bridge.call("update_bms_data", ...).
 * @return void
 */
void rs485_master_poll(void);

#endif /* RS485_MASTER_H */
