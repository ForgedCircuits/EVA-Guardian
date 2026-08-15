"""!
@file main.py
@brief Python backend for Battery Management System.
@details Hosts a lightweight web server that exposes a network-accessible diagnostic endpoint.
         Provides a Bridge connection to the Arduino to coordinate hardware loopback tests.
"""

import time
from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI

import threading

## Cache the diagnostic status from the Arduino
diag_status = {"status": "UNKNOWN"}

## Threading event to synchronize between the HTTP request and the asynchronous Bridge callback
diag_event = threading.Event()

def report_diag_status(status: str) -> None:
    """!
    @brief Bridge provider callback: receives the hardware loopback status from Arduino.
    @param status The diagnostic result string ("BMS_OK" or "BMS_FAULT").
    @details Invoked by the C++ sketch when the hardware loopback test concludes.
             Releases the GPIO lines and unblocks the pending HTTP request.
    """
    diag_status["status"] = status
    print(f"[BMS-Network] Updated diagnostic status: {status}")
    Bridge.call("force_disable_gpio")
    diag_event.set()

def get_diag_status() -> dict:
    """!
    @brief REST endpoint handler to serve status to the IncidentDetectionSystem (IDS).
    @details Triggers the Arduino to run a hardware loopback test via Bridge and 
             blocks the HTTP response until the hardware test completes (or times out).
    @return Dictionary containing the "status" key.
    """
    diag_event.clear()
    diag_status["status"] = "TIMEOUT"
    print("[BMS-Network] Triggering loopback on Arduino...")
    Bridge.call("run_loopback_test")
    # Wait up to 5 seconds for Arduino to complete loopback (includes up to 3 retries)
    diag_event.wait(timeout=5.0)
    return diag_status

# Initialize WebUI on port 7000
ui = WebUI()
ui.expose_api("GET", "/diag", get_diag_status)

# Register the Bridge provider so the Arduino can update the status
Bridge.provide("report_diag_status", report_diag_status)

print("[BMS-Network] App started. Serving /diag endpoint on port 7000.")

def loop():
    """!
    @brief Main application loop.
    @details Keeps the Python script alive while background threads handle Bridge and WebUI traffic.
    """
    time.sleep(10)

App.run(user_loop=loop)
