"""!
@file config.py
@brief System configuration parameters and constant definitions.
@details Contains threshold settings, sampling constants, physics constants,
         and buffer sizes for the Incident Detection System.
"""

## @brief Machine Learning motion classification threshold.
MOTION_CONFIDENCE: float = 0.4

## @brief Accelerometer sampling frequency in Hz (matches sketch).
SAMPLING_FREQ_HZ: int = 42

## @brief Delta time step per sample in seconds.
DT: float = 1.0 / SAMPLING_FREQ_HZ

## @brief Standard acceleration due to gravity in m/s².
GRAVITY: float = 9.80665

## @brief Standstill acceleration threshold for speed integration in m/s².
STANDSTILL_THRESHOLD: float = 0.35

## @brief Velocity decay factor applied when movement falls below standstill threshold.
SPEED_DAMPING: float = 0.85

## @brief Frequency divisor for LED Matrix speed updates.
SPEED_UPDATE_EVERY_N: int = 2

## @brief Minimum interval in seconds between Telegram accident alert dispatches.
ACCIDENT_COOLDOWN_S: float = 60.0

## @brief Number of float features per inference window (42 samples × 3 axes).
INFERENCE_BUFFER_SIZE: int = 126

## @brief Maximum telemetry log entries retained in memory.
MAX_HISTORY: int = 50
