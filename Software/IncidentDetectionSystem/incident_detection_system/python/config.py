"""!
@file config.py
@brief Configuration settings for the Incident Detection System (IDS).
@details Loads environment variables and static configuration parameters 
         for the Telegram bot and alert routing.
"""

import os

## @brief Telegram Bot Token (used by the AlertService to dispatch SOS messages).
## @details Sourced from the 'TELEGRAM_BOT_TOKEN' environment variable.
TELEGRAM_BOT_TOKEN = os.environ.get("TELEGRAM_BOT_TOKEN", "")

## @brief Default Telegram Chat ID for dispatching emergency alerts.
## @details Can be a group chat ID or a direct user ID.
DEFAULT_CHAT_ID = "-1002242136979"

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

## @brief Minimum interval in seconds between Telegram accident alert dispatches.
ACCIDENT_COOLDOWN_S: float = 60.0

## @brief Number of float features per inference window (42 samples × 3 axes).
INFERENCE_BUFFER_SIZE: int = 126

## @brief Maximum telemetry log entries retained in memory.
MAX_HISTORY: int = 50

