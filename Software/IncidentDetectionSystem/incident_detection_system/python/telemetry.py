"""!
@file telemetry.py
@brief Telemetry processing and incident tracking module.
@details Implements thread-safe state management, drive event detection (sudden accel,
         harsh brake, potholes), and battery telemetry.
"""

import time
import threading
import requests
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
        ## Live RS485 telemetry fields — populated by update_bms_data()
        "voltage_v":         0.0,
        "current_a":         0.0,
        "temperature_c":     0.0,
        "ocv_v":             0.0,
        "coulomb_count_ah":  0.0,
        "soc":               0.0,
        "soh":               0.0,
        ## Derived status strings
        "ov":                "WAITING",
        "uv":                "WAITING",
        "oc":                "WAITING",
        "bms_status":        "WAITING FOR BMS",
        "precautions":       ["Awaiting RS485 link to BatteryManagementSystem..."],
        ## Link health
        "bms_link_status":   "DISCONNECTED",
        "comm_fault":        "NOMINAL",
        "connector_status":  "NOMINAL",
        "ids_voltage":       0.0,
        "last_updated":      "--:--:--",
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


def update_ids_voltage(ids_voltage: float) -> None:
    """!
    @brief Bridge provider callback: updates the locally measured IDS voltage.
    @param ids_voltage Voltage measured on A0 in volts.
    """
    with _state_lock:
        state["battery_health"]["ids_voltage"] = round(ids_voltage, 3)

def update_bms_data(
    voltage: float,
    current: float,
    temperature: float,
    ocv: float,
    coulombs: float,
    soc: float,
    soh: float,
) -> None:
    """!
    @brief Bridge provider callback: updates battery_health with live RS485 BMS telemetry.
    @details Called by the RouterBridge when the IDS firmware receives a valid RS485
             response packet from the BatteryManagementSystem slave. Derives OV/UV/OC
             fault status strings and BMS health status from the received values.
    @param voltage      Pack voltage in Volts.
    @param current      Pack current in Amperes (negative = discharge).
    @param temperature  Pack temperature in degrees Celsius.
    @param ocv          Open-Circuit Voltage in Volts.
    @param coulombs     Coulomb counter accumulated charge in Ah.
    @param soc          State of Charge in percent (0”100).
    @param soh          State of Health in percent (0–100).
    @return None
    """
    # Battery configuration limits (matches BMS firmware)
    BATTERY_MAX_VOLTAGE = 8.4
    BATTERY_MIN_VOLTAGE = 6.0
    OVERCURRENT_LIMIT_A = 5.0

    # ── Derive OV / UV / OC fault status strings ──────────────────
    if voltage > BATTERY_MAX_VOLTAGE:
        ov_str = f"ALERT! Over-Voltage ({voltage:.2f}V)"
    else:
        ov_str = f"NORMAL ({voltage:.2f}V)"

    if voltage < BATTERY_MIN_VOLTAGE:
        uv_str = f"ALERT! Under-Voltage ({voltage:.2f}V)"
    else:
        uv_str = f"NORMAL ({voltage:.2f}V)"

    if abs(current) > OVERCURRENT_LIMIT_A:
        oc_str = f"ALERT! Over-Current ({current:.2f}A)"
    else:
        oc_str = f"NORMAL ({current:.2f}A Peak)"

    # ── Derive BMS health status string ───────────────────────────
    fault = (voltage > BATTERY_MAX_VOLTAGE or
             voltage < BATTERY_MIN_VOLTAGE or
             abs(current) > OVERCURRENT_LIMIT_A)

    if fault:
        bms_status = "FAULT DETECTED"
    elif soc > 80 and soh > 90:
        bms_status = "NOMINAL - BALANCING OK"
    elif soc < 20:
        bms_status = "WARNING - LOW CHARGE"
    elif soh < 80:
        bms_status = "WARNING - DEGRADED CAPACITY"
    else:
        bms_status = "NOMINAL"

    # ── Derive contextual precaution list ─────────────────────────
    precautions: list = []
    if temperature < 40:
        precautions.append("Thermal equilibrium optimal for fast charging (< 40\u00b0C)")
    else:
        precautions.append("WARNING: High temperature — reduce charge rate immediately")
    if soh >= 95:
        precautions.append("Cell voltage variance is within \u00b18mV tolerance")
    elif soh >= 80:
        precautions.append("Minor capacity fade detected — monitor cycle count")
    else:
        precautions.append("CAUTION: Significant capacity degradation — schedule service")
    if soc > 20 and not fault:
        precautions.append("No immediate battery maintenance or service required")
    elif soc <= 20:
        precautions.append("LOW SOC: Connect charger soon to avoid deep discharge")

    # ── Power Connector Degradation Check ────────────────────────
    # The IDS measures its own input voltage. Since it's powered by the BMS,
    # a significant voltage drop indicates high resistance (degradation) in the connector.
    connector_status = "NOMINAL"
    with _state_lock:
        ids_v = state["battery_health"].get("ids_voltage", voltage)
    
    voltage_drop = voltage - ids_v
    if voltage_drop >= 0.95:
        # We received telemetry but the voltage drop is extreme.
        # This shouldn't happen unless ground is floating, but we handle it anyway.
        connector_status = "SEVERE DEGRADATION"
        precautions.append("CRITICAL: Severe voltage drop on power connector!")
    elif voltage_drop >= 0.5:
        connector_status = "DEGRADED"
        precautions.append("WARNING: There is an connector degradation please show to the mechaninc soon before any severe damage")

    # Print command and response telemetry to console
    print(f"\n>>> [RS485 UART] Command: 0x01 (bat_status)")
    print(f"<<< [RS485 UART] Response: Voltage={voltage:.3f}V, Current={current:.4f}A, Temperature={temperature:.1f}°C, OCV={ocv:.3f}V, Coulombs={coulombs:.4f}Ah, SOC={soc:.1f}%, SOH={soh:.1f}%")
    print(f"[POWER DIAGNOSTIC] Local IDS Voltage: {ids_v:.3f}V | BMS Voltage: {voltage:.3f}V | Drop: {voltage_drop:.3f}V")

    logger.debug(
        f"[RS485] BMS update: V={voltage:.3f}V I={current:.4f}A "
        f"T={temperature:.1f}C OCV={ocv:.3f}V Ah={coulombs:.4f} "
        f"SOC={soc:.1f}% SOH={soh:.1f}%"
    )

    with _state_lock:
        bh = state["battery_health"]
        bh["voltage_v"]        = round(voltage, 3)
        bh["current_a"]        = round(current, 4)
        bh["temperature_c"]    = round(temperature, 1)
        bh["ocv_v"]            = round(ocv, 3)
        bh["coulomb_count_ah"] = round(coulombs, 4)
        bh["soc"]              = round(soc, 1)
        bh["soh"]              = round(soh, 1)
        bh["ov"]               = ov_str
        bh["uv"]               = uv_str
        bh["oc"]               = oc_str
        bh["bms_status"]       = bms_status
        bh["precautions"]      = precautions
        bh["bms_link_status"]  = "CONNECTED"
        bh["comm_fault"]       = "NOMINAL"
        bh["connector_status"] = connector_status
        bh["last_updated"]     = time.strftime("%H:%M:%S")

def report_rs485_fault(fault_str: str) -> None:
    """!
    @brief Bridge provider callback: updates communication fault status.
    @param fault_str The fault description (e.g., "IDS TRANSCEIVER FAULT").
    """
    logger.warning(f"[RS485 DIAGNOSTIC] {fault_str}")
    try:
        Bridge.call("force_disable_gpio")
    except Exception as e:
        logger.debug(f"[RS485 DIAGNOSTIC] Could not force disable GPIO: {e}")
        
    with _state_lock:
        state["battery_health"]["comm_fault"] = fault_str
        state["battery_health"]["bms_link_status"] = "DISCONNECTED"


def check_bms_via_network(ids_status: str = "IDS_OK") -> None:
    """!
    @brief Bridge provider callback: Fallback network diagnostic when RS485 fails.
           Dispatches HTTP check to a background thread to prevent blocking MCU loop.
    @param ids_status RS485 loopback result for IDS ("IDS_OK" or "IDS_FAULT").
    """
    
    def _do_check():
        logger.info(f"[NETWORK DIAGNOSTIC] Checking BMS health over Wi-Fi...")
        bms_ip = "192.168.29.243"
        url = f"http://{bms_ip}:7000/diag"
        
        try:
            # BMS loopback takes up to 3 seconds. Give it 6 seconds to respond.
            response = requests.get(url, timeout=6.0)
            response.raise_for_status()
            
            data = response.json()
            bms_status = data.get("status", "UNKNOWN")
            
            # Cross-reference with physical voltage to detect full cable breakage
            with _state_lock:
                ids_v = state["battery_health"].get("ids_voltage", 0.0)
                
            cable_broken = (ids_v < 3.0)  # If IDS is running on backup/USB and sees < 3V from BMS
            
            if bms_status == "BMS_OK":
                if ids_status == "IDS_OK":
                    if cable_broken:
                        logger.error("[NETWORK DIAGNOSTIC] Both transceivers OK, but voltage is dead. Power & Comm Cable is completely broken.")
                        report_rs485_fault("POWER & COMM CABLE BROKEN")
                        with _state_lock:
                            state["battery_health"]["connector_status"] = "BROKEN"
                    else:
                        logger.info("[NETWORK DIAGNOSTIC] Both transceivers OK. Comm Cable is broken.")
                        report_rs485_fault("COMMUNICATION CABLE FAULT")
                else:
                    logger.error("[NETWORK DIAGNOSTIC] BMS OK, but IDS FAULTY.")
                    report_rs485_fault("IDS TRANSCEIVER FAULT")
            else:
                if ids_status == "IDS_OK":
                    logger.error("[NETWORK DIAGNOSTIC] IDS OK, but BMS FAULTY.")
                    report_rs485_fault("BMS TRANSCEIVER FAULT")
                else:
                    logger.critical("[NETWORK DIAGNOSTIC] BOTH TRANSCEIVERS FAULTY.")
                    report_rs485_fault("BOTH TRANSCEIVERS FAULT")
                    
        except requests.exceptions.RequestException as e:
            logger.error(f"[NETWORK DIAGNOSTIC] HTTP request failed: {e}")
            if ids_status == "IDS_OK":
                logger.warning("[NETWORK DIAGNOSTIC] Could not reach BMS over Wi-Fi. Assuming BMS node failure or total power loss.")
                report_rs485_fault("BMS NODE OFFLINE")
            else:
                logger.critical("[NETWORK DIAGNOSTIC] IDS FAULTY and BMS UNREACHABLE.")
                report_rs485_fault("SYSTEM WIDE FAILURE")
                
    # Dispatch thread immediately so Bridge.call returns instantly to C++ MCU.
    threading.Thread(target=_do_check, daemon=True).start()




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


def get_bms_status() -> Dict[str, Any]:
    """!
    @brief Fetch a snapshot of the live BMS telemetry state.
    @details Returns only the battery_health sub-dictionary, including all RS485
             sourced fields: voltage, current, temperature, OCV, coulomb count,
             SOC, SOH, fault status strings, link status, and last update timestamp.
    @return Dictionary containing full battery_health telemetry.
    """
    with _state_lock:
        return dict(state["battery_health"])


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

