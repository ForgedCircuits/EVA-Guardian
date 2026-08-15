/**
 * @file rs485_slave.h
 * @brief RS485 UART Slave communication module for Battery Management System.
 * @details Implements a half-duplex RS485 slave that listens for bat_status (0x01)
 *          command packets from the IncidentDetectionSystem master and responds
 *          with a structured telemetry response packet containing live BMS data.
 *
 * Packet Format (Command — Master → Slave):
 *   [0x55][0xAA][LEN=0x01][CMD=0x01][CRC][0xAA]
 *
 * Packet Format (Response — Slave → Master):
 *   [0x55][0xAA][LEN][Voltage,Current,Temp,OCV,CoulombCount,SOC,SOH][CRC][0xAA]
 *
 * Hardware: Serial1 (Pin 0 = RX, Pin 1 = TX) @ 115200 baud.
 *           Auto-direction RS485 transceiver (no DE/RE GPIO required).
 */

#ifndef RS485_SLAVE_H
#define RS485_SLAVE_H

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
/** @brief Hardware serial port used for RS485 (Pin 0 = RX, Pin 1 = TX). */
#define RS485_SERIAL        Serial1

/** @brief RS485 communication baud rate. */
#define RS485_BAUD          115200UL

/* ── Diagnostic Constants ───────────────────────────────────────── */
#define LOOPBACK_TEST_WAIT_MS  1000UL  // Wait 1 second for loopback to return

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

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Initializes RS485 slave hardware (Serial1 @ 115200).
 * @details Must be called once in setup() after sensor initialization.
 * @return void
 */
void rs485_slave_init(void);

/**
 * @brief Non-blocking RS485 receive handler. Call in every loop() iteration.
 * @details Runs a byte-level state machine to parse incoming command packets.
 *          On receiving a valid bat_status (0x01) command, immediately
 *          transmits a BMS telemetry response packet.
 * @return void
 */
void rs485_slave_poll(void);

#endif /* RS485_SLAVE_H */
