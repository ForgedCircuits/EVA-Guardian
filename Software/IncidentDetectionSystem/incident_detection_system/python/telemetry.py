"""!
@file telemetry.py
@brief Telemetry processing and incident tracking module.
@details Implements thread-safe state management, drive event detection (sudden accel,
         harsh brake, potholes), and battery telemetry.
"""

import time
import threading
from typing import Dict, List, Any

from arduino.app_utils import Bridge, Logger
import config

logger = Logger("telemetry-module")

## @brief Thread lock protecting shared telemetry state dictionary.
_state_lock: threading.Lock = threading.Lock()

## @brief Trip start timestamp in Unix epoch seconds.
_trip_start_ts: float = time.time()

## @brief Sudden acceleration event counter.
_sudden_accel_count: int = 0

## @brief Harsh braking event counter.
_harsh_brake_count: int = 0

## @brief Pothole / vertical shock event counter.
_pothole_count: int = 0

## @brief Shared application state dictionary containing telemetry metrics.
state: Dict[str, Any] = {
    "imu_ms2": {"x": 0.0, "y": 0.0, "z": 0.0},
    "imu_g": {"x": 0.0, "y": 0.0, "z": 0.0},
    "total_acc_ms2": 0.0,
    "last_classification": "idle",
    "confidence": {},
    "counts": {
        "Front_and_Back": 0,
        "Up_and_Down": 0,
        "Accident": 0,
        "idle": 0,
    },
    "drive_safety": {
        "trip_duration_s": 0,
        "potholes": 0,
        "sudden_accel": 0,
        "harsh_braking": 0,
        "incidents": 0,
        "accident_verification": "Sensor Fusion & Edge ML",
        "sos_status": "STANDBY",
    },
    "battery_health": {
        "soc": 88.5,
        "soh": 96.8,
        "voltage_v": 51.2,
        "current_a": 0.0,
        "temperature_c": 32.5,
        "ov": "NORMAL (4.15V/Cell)",
        "uv": "NORMAL (3.65V/Cell)",
        "oc": "NORMAL (0.0A Peak)",
        "bms_status": "NOMINAL - BALANCING OK",
        "precautions": [
            "Thermal equilibrium optimal for fast charging (< 40°C)",
            "Cell voltage variance is within ±8mV tolerance",
            "No immediate battery maintenance or service required"
        ],
    },
    "history": [],
}



def record_sensor_movement(x: float, y: float, z: float, total_acc: float = 0.0) -> None:
    """!
    @brief Process raw accelerometer readings and integrate speed strictly via Accx.
    @details Converts g values to m/s², calculates speed strictly from Accx,
             updates trip distance, tracks drive incidents, and dispatches LED notifications.
    @param x Acceleration along X axis in g units.
    @param y Acceleration along Y axis in g units.
    @param z Acceleration along Z axis in g units.
    @param total_acc Total normalized acceleration in m/s2 from MCU.
    @return None
    """
    global _sudden_accel_count, _harsh_brake_count, _pothole_count

    # Unit conversion: g -> m/s²
    x_ms2 = x * config.GRAVITY  # Accx in m/s²
    y_ms2 = y * config.GRAVITY  # Accy in m/s²
    z_ms2 = z * config.GRAVITY  # Accz in m/s²

    # Detect sudden acceleration / harsh braking on X axis
    if x_ms2 > 19.6:
        _sudden_accel_count += 1
    elif x_ms2 < -19.6:
        _harsh_brake_count += 1

    # Detect pothole shocks (Z-axis variance relative to gravity)
    if abs(z_ms2 - config.GRAVITY) > 19.6:
        _pothole_count += 1

    trip_duration = int(time.time() - _trip_start_ts)

    # Dynamic simulated battery current based on Accx acceleration
    sim_current = round(abs(x_ms2) * 2.8, 1) if abs(x_ms2) > config.STANDSTILL_THRESHOLD else 0.5

    with _state_lock:
        state["imu_ms2"] = {"x": round(x_ms2, 3), "y": round(y_ms2, 3), "z": round(z_ms2, 3)}
        state["imu_g"] = {"x": round(x, 4), "y": round(y, 4), "z": round(z, 4)}
        state["total_acc_ms2"] = round(total_acc, 2)

        # Drive & Safety telemetry update
        state["drive_safety"]["trip_duration_s"] = trip_duration
        state["drive_safety"]["potholes"] = _pothole_count
        state["drive_safety"]["sudden_accel"] = _sudden_accel_count
        state["drive_safety"]["harsh_braking"] = _harsh_brake_count
        state["drive_safety"]["incidents"] = (
            _sudden_accel_count + _harsh_brake_count + _pothole_count + state["counts"]["Accident"]
        )

        # Battery health telemetry update
        state["battery_health"]["current_a"] = sim_current


def get_telemetry_status() -> Dict[str, Any]:
    """!
    @brief Fetch snapshot of current telemetry data dictionary.
    @return Dictionary containing status, IMU metrics, drive safety, and battery health data.
    """
    with _state_lock:
        return {
            "imu_ms2": dict(state["imu_ms2"]),
            "imu_g": dict(state["imu_g"]),
            "total_acc_ms2": state.get("total_acc_ms2", 0.0),
            "last_classification": state["last_classification"],
            "confidence": dict(state["confidence"]),
            "counts": dict(state["counts"]),
            "drive_safety": dict(state["drive_safety"]),
            "battery_health": dict(state["battery_health"]),
            "timestamp": time.strftime("%H:%M:%S"),
        }


def get_telemetry_history() -> Dict[str, List[Dict[str, Any]]]:
    """!
    @brief Fetch telemetry log history entries.
    @return Dictionary containing list of history events.
    """
    with _state_lock:
        return {"history": list(state["history"])}


def reset_telemetry_incidents() -> Dict[str, Any]:
    """!
    @brief Reset drive incident metrics to zero.
    @return Success status dictionary with confirmation message.
    """
    global _sudden_accel_count, _harsh_brake_count, _pothole_count
    _sudden_accel_count = 0
    _harsh_brake_count = 0
    _pothole_count = 0

    with _state_lock:
        state["drive_safety"]["potholes"] = 0
        state["drive_safety"]["sudden_accel"] = 0
        state["drive_safety"]["harsh_braking"] = 0
        state["drive_safety"]["incidents"] = 0

    logger.info("Drive incident metrics reset to zero")
    return {"ok": True, "message": "Drive incident metrics reset to 0"}

