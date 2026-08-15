# 🔋 Battery Management System (EVA-Guardian)

This repository contains the software implementation of the **Battery Management System (BMS)**, a core component of the **EVA-Guardian** project. The BMS is responsible for real-time monitoring of battery health, state estimation, local display, and serving diagnostic data to the Incident Detection System via a robust RS485 communication link and network endpoints.

The architecture is split into two primary components:
1. **Arduino Q Firmware (`sketch/`)**: Handles sensor data acquisition, State of Charge (SOC) & State of Health (SOH) calculation, persistent storage, LED matrix display, and acts as an RS485 Slave.
2. **Python Backend Service (`python/`)**: Exposes a lightweight web server that provides network-accessible diagnostic endpoints and orchestrates hardware loopback tests via a serial bridge.

---

## Component Detailed Breakdown

### 1. Arduino Firmware (`sketch/`)
Implemented in C++ on the Arduino platform.

*   **[sketch.ino](file:///sketch/sketch.ino)**: The main entry point. Orchestrates the cooperative multitasking loop:
    *   **100 ms Interval**: Reads sensors, integrates Coulombs, and calculates SOC/SOH.
    *   **1000 ms Interval**: Updates the LED Matrix display.
    *   **60000 ms Interval**: Persists BMS state to EEPROM (or EMMC).
    *   **Continuous Loop**: Services the RS485 slave non-blocking state machine.
*   **Sensors & Core Logic**:
    *   **bms_sensors**: Manages reading and filtering raw hardware sensors (Voltage, Current, Temperature).
    *   **bms_soc**: Implements Coulomb counting (Ah integration) combined with Open Circuit Voltage (OCV) boot calibration to maintain accurate SOC and SOH metrics.
    *   **bms_storage**: Handles non-volatile memory operations to persist Coulomb count and max capacity across reboots.
*   **[matrix_display.cpp](file:///sketch/matrix_display.cpp)**: Controls the local LED matrix display, showing real-time SOC percentage and other status indicators.
*   **[rs485_slave.cpp](file:///sketch/rs485_slave.cpp)**:
    *   Implements a custom byte-level, non-blocking state machine for RS485 UART communication (Serial2 @ 115200 baud).
    *   Responds to polling requests (`CMD_BAT_STATUS` `0x01`) from the Incident Detection System (IDS) Master.
    *   Builds and transmits an ASCII CSV telemetry payload: `Voltage,Current,Temperature,OCV,CoulombCount,SOC,SOH`.
    *   Features a hardware loopback diagnostic mode to isolate and verify physical layer transceiver faults.

---

### 2. Python Backend (`python/`)
Written in Python, utilizing the Arduino App Bricks framework.

*   **[main.py](file:///python/main.py)**: The Python orchestrator.
    *   Initializes a `WebUI` on port `7000`.
    *   Provides a Bridge connection to the Arduino firmware.
    *   Exposes a `/diag` REST endpoint that coordinates a hardware loopback test: it triggers the Arduino to run the test and blocks the HTTP response until the hardware confirms success or failure.

---

## API Endpoints

The Python service exposes the following diagnostic endpoint:

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/diag` | `GET` | Triggers a hardware RS485 loopback test on the Arduino. Blocks until the test completes (max 5 seconds). Returns a JSON object with a `status` key (e.g., `BMS_OK`, `BMS_FAULT`, `TIMEOUT`). |

---

## Application Metadata (`app.yaml`)
Configures the application brick orchestration platform:
*   `arduino:web_ui`: Hosts the diagnostic REST web server.
