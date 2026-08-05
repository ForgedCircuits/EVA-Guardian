"""!
@file main.py
@brief Incident Detection & Telematics System — Python Main Entry Point.
@details Initializes Arduino App bricks (MotionDetection, TelegramBot, WebUI),
         registers Bridge functions, API handlers, and starts the App runtime loop.
"""

import time
from arduino.app_bricks.motion_detection import MotionDetection
from arduino.app_bricks.telegram_bot import TelegramBot
from arduino.app_bricks.web_ui import WebUI
from arduino.app_utils import App, Bridge, Logger

import config
import telemetry
import alert_service

logger = Logger("incident-detection")
logger.info("Incident Detection System initializing...")

## @brief MotionDetection application brick instance.
motion_detection = MotionDetection(confidence=config.MOTION_CONFIDENCE)

## @brief TelegramBot application brick instance.
bot = TelegramBot()

## @brief WebUI application brick instance.
ui = WebUI()

# Initialize Telegram Bot Command Handlers
alert_service.setup_telegram_bot(bot)

## @brief Inference accumulative buffer list.
_infer_buffer = []


def run_inference(buffer: list) -> None:
    """!
    @brief Execute Edge Impulse ML motion classification inference.
    @param buffer List of float accelerometer features (126 elements).
    @return None
    """
    try:
        result = motion_detection.infer_from_features(buffer)
    except Exception as e:
        logger.warning(f"Inference failed: {e}")
        return

    cls = result.get("result", {}).get("classification", {})
    if not cls:
        return

    best = max(cls, key=cls.get)
    best_conf = cls.get(best, 0.0)

    logger.info(f"Classification: {best} ({best_conf:.2%})")

    with telemetry._state_lock:
        telemetry.state["last_classification"] = best
        telemetry.state["confidence"] = {k: round(v, 4) for k, v in cls.items()}

        count_key = best if best in telemetry.state["counts"] else "idle"
        telemetry.state["counts"][count_key] += 1

        event = {
            "time": time.strftime("%H:%M:%S"),
            "date": time.strftime("%Y-%m-%d"),
            "classification": best,
            "confidence": round(best_conf, 4),
        }
        telemetry.state["history"].insert(0, event)
        if len(telemetry.state["history"]) > config.MAX_HISTORY:
            telemetry.state["history"] = telemetry.state["history"][: config.MAX_HISTORY]

    # Alert updates on state classification change
    if best == "Accident":
        alert_service.dispatch_accident_alert(bot, cls)


def sensor_movement_wrapper(x: float, y: float, z: float, total_acc: float = 0.0) -> None:
    """!
    @brief Bridge provider callback wrapper for sensor readings.
    @param x Acceleration along X axis in g.
    @param y Acceleration along Y axis in g.
    @param z Acceleration along Z axis in g.
    @param total_acc Total normalized acceleration in m/s2 from MCU.
    @return None
    """
    global _infer_buffer

    # Process telemetry
    telemetry.record_sensor_movement(x, y, z, total_acc)

    # Accumulate features for ML inference window
    x_ms2 = x * config.GRAVITY
    y_ms2 = y * config.GRAVITY
    z_ms2 = z * config.GRAVITY

    _infer_buffer.extend([x_ms2, y_ms2, z_ms2])
    if len(_infer_buffer) >= config.INFERENCE_BUFFER_SIZE:
        run_inference(_infer_buffer[: config.INFERENCE_BUFFER_SIZE])
        _infer_buffer.clear()


# Movement detection callback registrations
motion_detection.on_movement_detection("Front_and_Back", lambda c: logger.info(f"Front_and_Back: {c}"))
motion_detection.on_movement_detection("Up_and_Down", lambda c: logger.info(f"Up_and_Down: {c}"))
motion_detection.on_movement_detection("Accident", lambda c: logger.info(f"Accident: {c}"))

# Register Router Bridge Providers
Bridge.provide("record_sensor_movement", sensor_movement_wrapper)

# Expose WebUI REST endpoints
ui.expose_api("GET", "/status", telemetry.get_telemetry_status)
ui.expose_api("GET", "/history", telemetry.get_telemetry_history)
ui.expose_api("POST", "/reset_incidents", telemetry.reset_telemetry_incidents)

logger.info("All Bridge providers and WebUI endpoints registered. Starting App framework...")

# Start main application runtime
App.run()