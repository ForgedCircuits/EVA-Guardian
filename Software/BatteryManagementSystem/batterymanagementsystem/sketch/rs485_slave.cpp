/**
 * @file rs485_slave.cpp
 * @brief RS485 UART Slave implementation for Battery Management System.
 * @details Parses incoming binary command packets from the IDS master using a
 *          non-blocking byte-level state machine. On a valid bat_status (0x01)
 *          command, builds and transmits a formatted response packet containing
 *          live BMS telemetry values (Voltage, Current, Temperature, OCV,
 *          Coulomb Count, SOC, SOH).
 *
 * CRC algorithm: XOR of all payload bytes only.
 */

#include "rs485_slave.h"
#include <stdio.h>
#include <string.h>

/* ── External BMS state variables (defined in sketch.ino) ───────── */
extern float systemVoltage;
extern float systemCurrent;
extern float systemTemperature;
extern float systemSOC;
extern float systemSOH;
extern float accumulatedAh;
extern float get_soc_from_ocv(float voltage);

/* ── RX State Machine ────────────────────────────────────────────── */
typedef enum {
    RX_WAIT_SOF1 = 0, /**< Waiting for 0x55 */
    RX_WAIT_SOF2,     /**< Waiting for 0xAA */
    RX_WAIT_LEN,      /**< Reading payload length byte */
    RX_PAYLOAD,       /**< Accumulating payload bytes */
    RX_WAIT_CRC,      /**< Reading CRC byte */
    RX_WAIT_EOF       /**< Waiting for end-of-frame 0xAA */
} SlaveRxState_t;

static SlaveRxState_t s_rxState   = RX_WAIT_SOF1;
static uint8_t        s_rxLen     = 0;
static uint8_t        s_rxIdx     = 0;
static uint8_t        s_rxBuf[128] = {0}; // Increased for loopback size if needed
static uint8_t        s_rxCrc     = 0; /**< Running XOR CRC over payload bytes */

/* ── Diagnostic State ────────────────────────────────────────────── */
static bool          s_diagActive = false;
static unsigned long s_diagTimer  = 0;
static bool          s_lbReceived = false;
static uint8_t       s_diagAttempts = 0;

/* ── Private Helpers ─────────────────────────────────────────────── */

/**
 * @brief Drives loopback GPIOs high.
 */
static void enable_loopback_gpio() {
    pinMode(LB_PIN_2, OUTPUT);
    pinMode(LB_PIN_3, OUTPUT);
    pinMode(LB_PIN_4, OUTPUT);
    pinMode(LB_PIN_5, OUTPUT);
    digitalWrite(LB_PIN_2, HIGH);
    digitalWrite(LB_PIN_3, HIGH);
    digitalWrite(LB_PIN_4, HIGH);
    digitalWrite(LB_PIN_5, HIGH);
}

/**
 * @brief Clears loopback GPIOs.
 */
void disable_loopback_gpio() {
    digitalWrite(LB_PIN_2, LOW);
    digitalWrite(LB_PIN_3, LOW);
    digitalWrite(LB_PIN_4, LOW);
    digitalWrite(LB_PIN_5, LOW);
    pinMode(LB_PIN_2, INPUT);
    pinMode(LB_PIN_3, INPUT);
    pinMode(LB_PIN_4, INPUT);
    pinMode(LB_PIN_5, INPUT);
}

/**
 * @brief Sends the loopback test command over RS485.
 */
static void send_loopback_cmd(void) {
    uint8_t crc = CMD_LOOPBACK_TEST ^ LOOPBACK_PAYLOAD_1 ^ LOOPBACK_PAYLOAD_2;
    RS485_SERIAL.write(RS485_SOF1);
    RS485_SERIAL.write(RS485_SOF2_EOF);
    RS485_SERIAL.write((uint8_t)3); /* Command + 2 payload bytes */
    RS485_SERIAL.write(CMD_LOOPBACK_TEST);
    RS485_SERIAL.write(LOOPBACK_PAYLOAD_1);
    RS485_SERIAL.write(LOOPBACK_PAYLOAD_2);
    RS485_SERIAL.write(crc);
    RS485_SERIAL.write(RS485_SOF2_EOF);
    RS485_SERIAL.flush();
}


/**
 * @brief Resets the RX state machine to its initial idle state.
 */
static void slave_reset_rx(void) {
    s_rxState = RX_WAIT_SOF1;
    s_rxLen   = 0;
    s_rxIdx   = 0;
    s_rxCrc   = 0;
}

/**
 * @brief Computes XOR checksum over a byte array.
 * @param data Pointer to data buffer.
 * @param len  Number of bytes to XOR.
 * @return XOR checksum byte.
 */
static uint8_t compute_crc(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}

/**
 * @brief Builds and transmits a BMS telemetry response packet.
 * @details Packet structure:
 *   [0x55][0xAA][LEN][ASCII CSV payload][CRC][0xAA]
 *
 *   Payload CSV order: Voltage,Current,Temperature,OCV,CoulombCount,SOC,SOH
 *
 *   OCV is approximated by the current filtered voltage reading.
 *   True OCV requires a rest period (handled by calculateSOC() in sketch.ino).
 */
static void send_bms_response(void) {
    char    payload[128];
    uint8_t payloadLen;
    uint8_t crc;

    /* Build ASCII CSV response payload */
    int n = snprintf(payload, sizeof(payload),
        "%.3f,%.4f,%.1f,%.3f,%.4f,%.1f,%.1f",
        systemVoltage,       /* Voltage (V)          */
        systemCurrent,       /* Current (A)          */
        systemTemperature,   /* Temperature (°C)     */
        systemVoltage,       /* OCV proxy (V)        */
        accumulatedAh,       /* Coulomb Count (Ah)   */
        systemSOC,           /* SOC (%)              */
        systemSOH            /* SOH (%)              */
    );

    if (n <= 0 || n >= (int)sizeof(payload)) {
        return; /* snprintf overflowed — abort */
    }

    payloadLen = (uint8_t)n;
    crc        = compute_crc((const uint8_t*)payload, payloadLen);

    /* Transmit framed packet */
    RS485_SERIAL.write(RS485_SOF1);          /* SOF byte 1    */
    RS485_SERIAL.write(RS485_SOF2_EOF);      /* SOF byte 2    */
    RS485_SERIAL.write(payloadLen);          /* Payload length */
    RS485_SERIAL.write((const uint8_t*)payload, payloadLen); /* ASCII CSV */
    RS485_SERIAL.write(crc);                 /* CRC           */
    RS485_SERIAL.write(RS485_SOF2_EOF);      /* EOF           */
    RS485_SERIAL.flush();                    /* Wait for TX FIFO drain */
}

/* ── Public API ─────────────────────────────────────────────────── */

void trigger_rs485_loopback(void) {
    if (!s_diagActive) {
        Serial.println("[DIAG] Network trigger received. Starting Loopback.");
        s_diagActive = true;
        s_lbReceived = false;
        s_diagAttempts = 0;
        s_diagTimer = millis();
        enable_loopback_gpio();
        slave_reset_rx();
        send_loopback_cmd();
    }
}

void rs485_slave_init(void) {
    RS485_SERIAL.begin(RS485_BAUD);

    slave_reset_rx();
    Bridge.provide("run_loopback_test", trigger_rs485_loopback);
    Bridge.provide("force_disable_gpio", disable_loopback_gpio);
    
    Serial.println("[RS485-Slave] Initialized on Serial1 @ 115200");
}

void rs485_slave_poll(void) {
    unsigned long now = millis();

    if (s_diagActive) {
        if (now - s_diagTimer > LOOPBACK_TEST_WAIT_MS) {
            s_diagAttempts++;
            if (s_diagAttempts < 3) {
                Serial.println("[DIAG] Loopback failed. Retrying...");
                s_diagTimer = now;
                send_loopback_cmd();
            } else {
                disable_loopback_gpio();
                s_diagActive = false;
                Serial.println("[DIAG] Loopback failed 3 times. Reporting BMS_FAULT via Network.");
                Bridge.call("report_diag_status", "BMS_FAULT");
            }
        }
    }

    /* ── RS485 RX Processing ── */
    while (RS485_SERIAL.available() > 0) {
        uint8_t b = (uint8_t)RS485_SERIAL.read();

        switch (s_rxState) {

            case RX_WAIT_SOF1:
                if (b == RS485_SOF1) {
                    s_rxState = RX_WAIT_SOF2;
                }
                break;

            case RX_WAIT_SOF2:
                if (b == RS485_SOF2_EOF) {
                    s_rxState = RX_WAIT_LEN;
                } else {
                    slave_reset_rx(); /* Spurious byte — restart */
                }
                break;

            case RX_WAIT_LEN:
                if (b == 0 || b > sizeof(s_rxBuf)) {
                    slave_reset_rx(); /* Invalid length */
                } else {
                    s_rxLen   = b;
                    s_rxIdx   = 0;
                    s_rxCrc   = 0;
                    s_rxState = RX_PAYLOAD;
                }
                break;

            case RX_PAYLOAD:
                s_rxBuf[s_rxIdx++] = b;
                s_rxCrc ^= b;                   /* Accumulate XOR CRC */
                if (s_rxIdx >= s_rxLen) {
                    s_rxState = RX_WAIT_CRC;
                }
                break;

            case RX_WAIT_CRC:
                if (b == s_rxCrc) {
                    s_rxState = RX_WAIT_EOF;    /* CRC OK */
                } else {
                    Serial.println("[RS485-Slave] CRC mismatch — packet dropped");
                    slave_reset_rx();
                }
                break;

            case RX_WAIT_EOF:
                if (b == RS485_SOF2_EOF) {
                    /* ── Valid complete packet received ── */
                    if (s_diagActive && s_rxLen == 3 && s_rxBuf[0] == CMD_LOOPBACK_TEST) {
                        s_lbReceived = true;
                        disable_loopback_gpio(); // Reset GPIOs immediately on success
                        s_diagActive = false;
                        Serial.println("[DIAG] Loopback OK. Reporting BMS_OK via Network.");
                        Bridge.call("report_diag_status", "BMS_OK");
                    } else if (!s_diagActive && s_rxLen == 1 && s_rxBuf[0] == CMD_BAT_STATUS) {
                        send_bms_response();
                    } else {
                        Serial.print("[RS485-Slave] Unknown command: 0x");
                        Serial.println(s_rxBuf[0], HEX);
                    }
                } else {
                    Serial.println("[RS485-Slave] EOF mismatch — packet dropped");
                }
                slave_reset_rx();
                break;

            default:
                slave_reset_rx();
                break;
        }
    }
}
