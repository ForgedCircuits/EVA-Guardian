# 🚕 Incident Detection & Telemetry System (EVA-Guardian)

This repository contains the software implementation of the **Incident Detection & Telematics System**, a primary subsystem of the **EVA-Guardian** project. The system performs real-time vehicle dynamics monitoring, detects safety incidents (such as accidents, harsh braking, sudden acceleration, and potholes) via Machine Learning at the edge, aggregates BMS telemetry, and dispatches instant Telegram alerts to emergency responders.

The architecture is split into two communicating components via a bidirectional serial bridge:
1. **Arduino Q Firmware (`sketch/`)**: Handles 42 Hz IMU sensor data acquisition, Exponential Moving Average (EMA) filtering, water-level tilt animation on a matrix display, and acts as the RS485 Master to collect BMS data.
2. **Python Backend Service (`python/`)**: Orchestrates Edge Impulse Machine Learning motion inference, thread-safe telemetry tracking, Telegram Bot event handling, and a REST API for the Web UI.

---

## Component Detailed Breakdown

### 1. Arduino Firmware (`sketch/`)
Implemented in C++ on the Arduino platform (Zephyr).

*   **[sketch.ino](file:///sketch/sketch.ino)**: The firmware entry point. Configures high-speed serial communication (115200 bps), initializes the RouterBridge, LED matrix, IMU, and RS485 Master.
*   **[imu_sensor.h](file:///sketch/imu_sensor.h)** & **[imu_sensor.cpp](file:///sketch/imu_sensor.cpp)**:
    *   **MPU6500 Driver**: Communicates over I2C at 400 kHz. Performs automatic accelerometer calibration on startup.
    *   **42 Hz Sampling Rate**: Strictly enforces a 23.8ms sampling interval in a non-blocking loop.
    *   **EMA Filter**: Suppresses high-frequency chassis vibration with $\alpha = 0.25$.
    *   **Bridge Dispatch**: Streams filtered XYZ acceleration data to the Python backend via `record_sensor_movement`.
*   **[matrix_display.h](file:///sketch/matrix_display.h)** & **[matrix_display.cpp](file:///sketch/matrix_display.cpp)**:
    *   **LED Matrix Controller**: Controls an 8x12 LED matrix. Uses a Zephyr kernel mutex (`matrix_mtx`) to prevent concurrent write collisions.
    *   **Tilt Indicator**: Calculates roll angle from accelerometer data and renders a dynamic fluid surface (water sloshing effect) pivoting around the center.
*   **[rs485_master.cpp](file:///sketch/rs485_master.cpp)**:
    *   Acts as the RS485 UART Master, polling the BMS slave every 2 seconds.
    *   Reads local IDS operational voltage via pin A0.
    *   Features a robust, non-blocking byte-level RX state machine to parse incoming BMS telemetry.
    *   **Diagnostics**: Implements an automated failover diagnostic state machine. If communication times out, it runs an IDS loopback test, disables/enables GPIOs, and falls back to network-based checking (`check_bms_via_network`) if RS485 fails.

---

### 2. Python Backend (`python/`)
Written in Python using the application bricks framework.

*   **[main.py](file:///python/main.py)**: The application orchestrator.
    *   Initializes `MotionDetection`, `TelegramBot`, and `WebUI` bricks.
    *   Registers Bridge callbacks to ingest 42 Hz IMU data and live RS485 telemetry.
    *   Accumulates sensor telemetry into an inference window buffer (126 features) and executes localized Edge Impulse Machine Learning classification.
*   **[telemetry.py](file:///python/telemetry.py)**: The central data repository.
    *   Maintains a thread-safe telemetry state.
    *   Tracks dynamic metrics: Sudden Acceleration ($> 2.0g$), Harsh Braking ($< -2.0g$), and Potholes ($|Acc_Z - g| > 19.6 \text{ m/s}^2$).
    *   Maintains historical classification memory and processes the real-time CSV battery telemetry (Voltage, Current, Temp, OCV, SOC, SOH) dispatched from the RS485 Master.
*   **[alert_service.py](file:///python/alert_service.py)**: Telegram Alert Dispatcher.
    *   Provides bot commands (`/start`, `/status`).
    *   Monitors ML classifications. If `Accident` is classified with high confidence across 3 consecutive windows, it triggers a critical Telegram notification to registered responders, governed by a 60-second cooldown protection.
*   **[config.py](file:///python/config.py)**: System configuration constants (thresholds, cooldowns, filter rates).

---

### 3. Application Metadata (`app.yaml`)
Configures the brick orchestration platform:
*   `arduino:motion_detection`: Loads the Edge Impulse classification container (`mpu6500_imu-linux-aarch64-v8-impulse-#9.eim`).
*   `arduino:telegram_bot`: Configures the Telegram server connection using `TELEGRAM_BOT_TOKEN`.
*   `arduino:web_ui`: Hosts the telematics dashboard REST API.

---

## API Endpoints (Web UI Bridge)

The Python service exposes REST API endpoints:

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/status` | `GET` | Fetches the current telemetry state (IMU, alerts, trip stats) and local IDS voltage. |
| `/bms`    | `GET` | Fetches the latest live BMS data collected via the RS485 Master bridge. |
| `/history`| `GET` | Returns the recent accident/incident classification history. |
| `/reset_incidents` | `POST` | Clears current trip statistics (harsh brakes, sudden acceleration, potholes). |

---

## Configuration and Setup

1.  **Configure Environment Variables**:
    Update the `TELEGRAM_BOT_TOKEN` in [app.yaml](file:///app.yaml) to connect your Telegram Bot:
    ```yaml
    - arduino:telegram_bot:
        variables:
          TELEGRAM_BOT_TOKEN: "your_bot_token_here"
    ```
2.  **Arduino Calibration**:
    Ensure the hardware is completely level on startup, as the MPU6500 performs an offset calibration routine in `init_imu()`.
3.  **Run Application**:
    Deploy the application. The system will automatically spin up the Zephyr RTOS threads, the RS485 polling loops, ML inference engine, and the web/Telegram services.
