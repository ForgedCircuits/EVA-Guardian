# Incident Detection & Telemetry System (EVA-Guardian)

This repository contains the software implementation of the **Incident Detection & Telematics System** (part of the **EVA-Guardian** project). The system performs real-time vehicle dynamics monitoring, detects safety incidents (such as accidents, harsh braking, sudden acceleration, and potholes), simulates battery telemetry, and dispatches instant Telegram alerts to emergency responders.

The architecture is split into two primary components communicating over a fast bidirection serial bridge:
1. **Arduino UNO Q Firmware (`sketch/`)**: Handles 42 Hz IMU sensor data acquisition, Exponential Moving Average (EMA) filtering, physical water-level tilt animation, and hardware alerts.
2. **Python Backend Service (`python/`)**: Orchestrates Edge Impulse Machine Learning motion inference, thread-safe telematics telemetry tracking, Telegram Bot event handling, and a REST API for the Web UI.

---

## Component Detailed Breakdown

### 1. Arduino Firmware (`sketch/`)
Implemented in C++ on the `arduino:zephyr` platform.

*   **[sketch.ino](file:///sketch/sketch.ino)**: The firmware entry point. Configures high-speed serial communication at 115200 bps, initializes the router bridge, and starts the telemetry acquisition and display loops.
*   **[imu_sensor.h](file:///sketch/imu_sensor.h)** & **[imu_sensor.cpp](file:///sketch/imu_sensor.cpp)**:
    *   **MPU6500 Driver**: Communicates over I2C at 400 kHz (Fast Mode) using the `FastIMU` library. Performs accelerometer calibration on startup.
    *   **42 Hz Timer**: Strictly enforces a 23.8ms sampling interval (`1,000,000 / 42` microseconds) in a non-blocking loop.
    *   **Exponential Moving Average (EMA) Filter**: Suppresses engine vibration and high-frequency chassis noise using the formula:
        $$Acc_{\text{filtered}} = \alpha \cdot Acc_{\text{raw}} + (1 - \alpha) \cdot Acc_{\text{filtered\_prev}}$$
        where $\alpha = 0.25$.
    *   **Data Dispatch**: Calls the bridge function `record_sensor_movement` to stream filtered metrics (`x`, `y`, `z`, and overall $m/s^2$ ignoring vertical gravity) to Python.
*   **[matrix_display.h](file:///sketch/matrix_display.h)** & **[matrix_display.cpp](file:///sketch/matrix_display.cpp)**:
    *   **LED Matrix Controller**: Controls the built-in 8x12 LED matrix under a Zephyr kernel mutex lock (`matrix_mtx`) to prevent write collisions during multi-threaded operation.
    *   **Tilt/Water-level Indicator**: Calculates roll tilt angle using `atan2(-filtAccY, filtAccZ)`. Renders a dynamic fluid surface on the matrix that pivots around the center column, mimicking water sloshing in a container.
    *   **7-Segment Font**: Renders custom numbers using a predefined $7 \times 5$ pixel font buffer.

---

### 2. Python Backend (`python/`)
Written in Python and packaged as application bricks.

*   **[main.py](file:///python/main.py)**: The orchestrator.
    *   Initializes the core application framework and app bricks (`MotionDetection`, `TelegramBot`, `WebUI`).
    *   Registers Bridge callbacks to ingest 42 Hz data.
    *   Accumulates sensor telemetry into groups of 126 float values (representing 42 samples $\times$ 3 axes, spanning a 1-second inference window).
    *   Executes localized Edge Impulse Machine Learning motion classification.
*   **[telemetry.py](file:///python/telemetry.py)**: The central data repository.
    *   Maintains a thread-safe `state` dictionary protected by `_state_lock`.
    *   Tracks dynamic driver metrics:
        *   **Sudden Acceleration**: Triggered when $Acc_X > 19.6 \text{ m/s}^2$ ($> 2.0g$).
        *   **Harsh Braking**: Triggered when $Acc_X < -19.6 \text{ m/s}^2$ ($< -2.0g$).
        *   **Potholes**: Triggered when vertical acceleration deviates from standard gravity by more than $19.6 \text{ m/s}^2$:
            $$|Acc_Z - g| > 19.6 \text{ m/s}^2$$
    *   Maintains historical classification memory (capped at 50 logs).
    *   Simulates real-time electric vehicle (EV) battery behavior (voltage, temperature, SoC, current, and cell balance flags).
*   **[alert_service.py](file:///python/alert_service.py)**: Telegram Alert Dispatcher.
    *   Listens for incoming Telegram commands: `/start` (registers user chat) and `/status` (queries current telemetry state).
    *   Implements **cooldown protection** (`ACCIDENT_COOLDOWN_S = 60s`) to prevent emergency alert spamming.
    *   When an `Accident` label is classified with high confidence, it pushes formatted critical notifications showing the confidence percentage and timestamp, and notifies the Arduino device to show safety indicators.
*   **[config.py](file:///python/config.py)**: Houses system configuration constants.

---

### 3. Application Metadata (`app.yaml`)
Configures the brick orchestration platform, mounting the following sub-components:
*   `arduino:motion_detection`: Sets up the Edge Impulse classification container with the model binary (`mpu6500_imu-linux-aarch64-v7-impulse-#5.eim`).
*   `arduino:telegram_bot`: Configures the Telegram server connection using `TELEGRAM_BOT_TOKEN`.
*   `arduino:web_ui`: Hosts the telematics dashboard.

---

## API Endpoints (Web UI Bridge)

The Python service exposes REST API endpoints through the `arduino:web_ui` brick:

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/status` | `GET` | Fetches the current telemetry state (IMU raw values, trip statistics, driver alerts, and battery status). |
| `/history` | `GET` | Returns the recent classifications history. |
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
    Keep the device level on startup. The MPU6500 performs an offset calibration routine for the accelerometer in `init_imu()`.
3.  **Run Application**:
    Run the application using the brick platform commands. The Python service will handle reading from the Arduino board and launching the local HTTP web UI.
