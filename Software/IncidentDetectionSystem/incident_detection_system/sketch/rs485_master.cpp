/**
 * @file rs485_master.cpp
 * @brief RS485 UART Master implementation for Incident Detection System.
 * @details Polls the BMS slave every 2 seconds by transmitting a bat_status
 *          command packet, then parses the incoming telemetry response using a
 *          non-blocking byte-level state machine. Validated responses are
 *          dispatched to the Python backend via Bridge.call("update_bms_data").
 *
 * CRC algorithm: XOR of all payload bytes only.
 */

#include "rs485_master.h"
#include <stdlib.h>
#include <string.h>

/* ── IDS Power Measurement Constants ─────────────────────────────── */
const int   IDS_VOLTAGE_PIN        = A0;
const float ADC_MAX_VALUE          = 16383.0f;
const float ADC_REF_VOLTAGE        = 3.3f;
const float VOLTAGE_MULTIPLIER     = 3.266f;

/* ── RX State Machine ────────────────────────────────────────────── */
typedef enum {
    RX_WAIT_SOF1 = 0, /**< Waiting for 0x55 */
    RX_WAIT_SOF2,     /**< Waiting for 0xAA */
    RX_WAIT_LEN,      /**< Reading payload length byte */
    RX_PAYLOAD,       /**< Accumulating payload bytes */
    RX_WAIT_CRC,      /**< Reading CRC byte */
    RX_WAIT_EOF       /**< Waiting for end-of-frame 0xAA */
} MasterRxState_t;

static MasterRxState_t m_rxState              = RX_WAIT_SOF1;
static uint8_t         m_rxLen                = 0;
static uint8_t         m_rxIdx               = 0;
static uint8_t         m_rxBuf[RS485_RX_BUF_SIZE] = {0};
static uint8_t         m_rxCrc               = 0;

/** @brief Timestamp of last BMS poll command transmission. */
static unsigned long   m_lastPollMs          = 0;
/** @brief Timestamp of the last successful packet received from BMS. */
static unsigned long   m_lastValidResponseMs = 0;

/* ── Diagnostic State Machine ────────────────────────────────────── */
typedef enum {
    STATE_NORMAL = 0,
    STATE_DIAG_IDS_LOOPBACK,
    STATE_DIAG_DONE
} MasterDiagState_t;

static MasterDiagState_t m_diagState = STATE_NORMAL;
static unsigned long     m_diagTimer = 0;
static bool              m_lbReceived = false;
static uint8_t           m_diagAttempts = 0;

/* ── Private Helpers ─────────────────────────────────────────────── */

/**
 * @brief Resets the RX state machine to idle.
 */
static void master_reset_rx(void) {
    m_rxState = RX_WAIT_SOF1;
    m_rxLen   = 0;
    m_rxIdx   = 0;
    m_rxCrc   = 0;
}

/**
 * @brief Builds and transmits a bat_status command packet to the BMS slave.
 * @details Packet: [0x55][0xAA][0x01][0x01][CRC=0x01][0xAA]
 *          CRC = XOR of payload byte(s) only = 0x01.
 */
static void send_bat_status_cmd(void) {
    const uint8_t payload[1] = { CMD_BAT_STATUS };
    uint8_t crc = CMD_BAT_STATUS; /* XOR of one byte = the byte itself */

    RS485_SERIAL.write(RS485_SOF1);      /* SOF1           */
    RS485_SERIAL.write(RS485_SOF2_EOF);  /* SOF2           */
    RS485_SERIAL.write((uint8_t)1);      /* Payload length */
    RS485_SERIAL.write(CMD_BAT_STATUS);  /* Command byte   */
    RS485_SERIAL.write(crc);             /* CRC            */
    RS485_SERIAL.write(RS485_SOF2_EOF);  /* EOF            */
    RS485_SERIAL.flush();
}

/**
 * @brief Parses a validated ASCII CSV payload and dispatches BMS data via Bridge.
 * @details Expected CSV order: Voltage,Current,Temperature,OCV,CoulombCount,SOC,SOH
 *          All values are ASCII decimal floats separated by commas.
 *          Modifies m_rxBuf in-place (null-terminates for strtok).
 */
static void dispatch_bms_data(void) {
    /* Null-terminate the payload for string operations */
    if (m_rxIdx >= RS485_RX_BUF_SIZE) {
        return;
    }
    m_rxBuf[m_rxIdx] = '\0';

    float voltage     = 0.0f;
    float current     = 0.0f;
    float temperature = 0.0f;
    float ocv         = 0.0f;
    float coulombs    = 0.0f;
    float soc         = 0.0f;
    float soh         = 0.0f;

    char* token;
    char* buf = (char*)m_rxBuf;

    /* Parse each comma-separated field */
    token = strtok(buf, ",");  if (!token) return; voltage     = atof(token);
    token = strtok(NULL, ","); if (!token) return; current     = atof(token);
    token = strtok(NULL, ","); if (!token) return; temperature = atof(token);
    token = strtok(NULL, ","); if (!token) return; ocv         = atof(token);
    token = strtok(NULL, ","); if (!token) return; coulombs    = atof(token);
    token = strtok(NULL, ","); if (!token) return; soc         = atof(token);
    token = strtok(NULL, ","); if (!token) return; soh         = atof(token);

    m_lastValidResponseMs = millis();
    m_diagState = STATE_NORMAL; // Reset diagnostics on valid comms

    /* Dispatch to Python backend via RouterBridge */
    Bridge.call("update_bms_data",
                voltage,
                current,
                temperature,
                ocv,
                coulombs,
                soc,
                soh);
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

/* ── Public API ─────────────────────────────────────────────────── */

void rs485_master_init(void) {
    RS485_SERIAL.begin(RS485_BAUD);
    master_reset_rx();
    Bridge.provide("force_disable_gpio", disable_loopback_gpio);
    
    unsigned long now = millis();
    m_lastPollMs = now;
    m_lastValidResponseMs = now;
    Serial.println("[RS485-Master] Initialized on Serial1 @ 115200");
}

void rs485_master_poll(void) {
    unsigned long now = millis();

    /* ── Diagnostics State Machine ─────────────────────────── */
    if (m_diagState == STATE_NORMAL) {
        if (now - m_lastValidResponseMs > DIAGNOSTIC_TIMEOUT_MS) {
            Serial.println("[DIAG] Communication timeout. Starting loopback test.");
            m_diagState = STATE_DIAG_IDS_LOOPBACK;
            m_diagTimer = now;
            m_lbReceived = false;
            m_diagAttempts = 0;
            enable_loopback_gpio();
            master_reset_rx();
            send_loopback_cmd();
        }
    }

    if (m_diagState == STATE_DIAG_IDS_LOOPBACK) {
        if (now - m_diagTimer > LOOPBACK_TEST_WAIT_MS) {
            m_diagAttempts++;
            if (m_diagAttempts < 3) {
                Serial.println("[DIAG] IDS Loopback failed. Retrying...");
                m_diagTimer = now;
                send_loopback_cmd();
            } else {
                disable_loopback_gpio();
                Serial.println("[DIAG] IDS Loopback Failed 3 times!");
                Bridge.call("report_rs485_fault", "IDS TRANSCEIVER FAULT");
                Serial.println("[DIAG] Checking BMS Health anyway...");
                Bridge.call("check_bms_via_network", "IDS_FAULT");
                m_diagState = STATE_DIAG_DONE;
            }
        }
    } else if (m_diagState == STATE_DIAG_DONE) {
        /* Retry after a long delay (same as normal timeout) */
        if (now - m_diagTimer > DIAGNOSTIC_TIMEOUT_MS) {
            m_lastValidResponseMs = now;
            m_diagState = STATE_NORMAL;
        }
    }

    /* ── Periodic Command Transmission (Normal Mode Only) ──── */
    if (m_diagState == STATE_NORMAL && (now - m_lastPollMs >= BMS_POLL_INTERVAL_MS)) {
        m_lastPollMs = now;
        
        /* 1. Read IDS local voltage (A0) */
        int   adc = analogRead(IDS_VOLTAGE_PIN);
        float pinV = ((float)adc / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
        float idsVoltage = pinV * VOLTAGE_MULTIPLIER;
        
        /* 2. Dispatch local voltage to Python */
        Bridge.call("update_ids_voltage", idsVoltage);
        
        /* 3. Send bat_status poll to BMS */
        master_reset_rx();          /* Flush any stale partial response */
        send_bat_status_cmd();
    }

    /* ── Non-blocking Response Reception ───────────────────── */
    while (RS485_SERIAL.available() > 0) {
        uint8_t b = (uint8_t)RS485_SERIAL.read();

        switch (m_rxState) {

            case RX_WAIT_SOF1:
                if (b == RS485_SOF1) {
                    m_rxState = RX_WAIT_SOF2;
                }
                break;

            case RX_WAIT_SOF2:
                if (b == RS485_SOF2_EOF) {
                    m_rxState = RX_WAIT_LEN;
                } else {
                    master_reset_rx();
                }
                break;

            case RX_WAIT_LEN:
                if (b == 0 || b >= RS485_RX_BUF_SIZE) {
                    master_reset_rx(); /* Guard against buffer overrun */
                } else {
                    m_rxLen   = b;
                    m_rxIdx   = 0;
                    m_rxCrc   = 0;
                    m_rxState = RX_PAYLOAD;
                }
                break;

            case RX_PAYLOAD:
                m_rxBuf[m_rxIdx++] = b;
                m_rxCrc ^= b;                   /* Accumulate XOR CRC */
                if (m_rxIdx >= m_rxLen) {
                    m_rxState = RX_WAIT_CRC;
                }
                break;

            case RX_WAIT_CRC:
                if (b == m_rxCrc) {
                    m_rxState = RX_WAIT_EOF;    /* CRC matched */
                } else {
                    Serial.println("[RS485-Master] CRC error — response dropped");
                    master_reset_rx();
                }
                break;

            case RX_WAIT_EOF:
                if (b == RS485_SOF2_EOF) {
                    /* ── Valid complete packet received ── */
                    if (m_diagState == STATE_DIAG_IDS_LOOPBACK && m_rxLen == 3 && m_rxBuf[0] == CMD_LOOPBACK_TEST) {
                        m_lbReceived = true;
                        disable_loopback_gpio(); // Reset GPIOs immediately on success
                        Serial.println("[DIAG] IDS RS485 OK. Triggering BMS Network Check.");
                        Bridge.call("check_bms_via_network", "IDS_OK");
                        m_diagState = STATE_DIAG_DONE;
                    } else if (m_diagState == STATE_NORMAL && m_rxLen > 1) {
                        /* Parse and dispatch normal CSV */
                        dispatch_bms_data();
                    }
                } else {
                    Serial.println("[RS485-Master] EOF error — response dropped");
                }
                master_reset_rx();
                break;

            default:
                master_reset_rx();
                break;
        }
    }
}
