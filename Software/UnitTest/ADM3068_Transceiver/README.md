# ADM3068E Full-Duplex RS-485 Transceiver Test (Arduino UNO Q)

This project contains the test code and documentation for validating full-duplex RS-485 communication capabilities on the **Analog Devices ADM3068E** transceiver using an **Arduino UNO Q** board.

---

## 📌 Project Overview

- **Target Board**: Arduino UNO Q SBC
- **Transceiver IC**: ADM3068E (50 Mbps, 3.0 V to 5.5 V, ±12 kV IEC ESD Protected, Full-Duplex RS-485)
- **Evaluation Board**: EVAL-ADM3068EEBZ (Analog Devices, Reference Doc: `UG-1540`)
- **Test Mode**: Internal/External Full-Duplex Loopback Test over Hardware Serial (`Serial1`)

---

## 🛠 Hardware Setup & Pin Mapping

### Arduino UNO Q to ADM3068E Evaluation Board Connections

| Arduino UNO Q Pin | ADM3068E Eval Board (J3 / J2) | Description |
| :--- | :--- | :--- |
| **D1 (TX)** | `DI` (Data Input) | Transmit data output from Arduino to RS-485 driver input |
| **D0 (RX)** | `RO` (Receiver Output) | Receive data input to Arduino from RS-485 receiver output |
| **5V / 3.3V** | `VCC` / `VIO` | Logic and Main Power Supply (3.0 V to 5.5 V) |
| **GND** | `GND` | Common Ground |

---

## 🔧 EVAL-ADM3068EEBZ Jumper Configurations

To enable **Full-Duplex Loopback Mode** on the EVAL-ADM3068EEBZ board (as detailed in section *Full Duplex RS-485 Transceivers Loopback Test* of `UG-1540`):

| Jumper Link | Configuration / State | Function Description |
| :--- | :--- | :--- |
| **LK1** | **Position B** | Connects `RE` (Receiver Enable) to **GND** $\rightarrow$ **Receiver Enabled** |
| **LK2** | **Position A** | Connects `DE` (Driver Enable) to **VIO** $\rightarrow$ **Driver Enabled** |
| **LK3** | **Inserted** | Connects $120\,\Omega$ termination resistor ($R_{T1}$) across receiver bus pins **A** and **B** |
| **LK4** | **Inserted** | Connects bus non-inverting driver output **Y** to non-inverting receiver input **A** |
| **LK5** | **Inserted** | Connects $120\,\Omega$ termination resistor ($R_{T3}$) across driver bus pins **Y** and **Z** |
| **LK6** | **Inserted** | Connects bus inverting driver output **Z** to inverting receiver input **B** |
| **LK7** | **Inserted** | Connects `VCC` to `VIO` (uses main power supply for digital I/O logic levels) |

> 💡 **Note on Bus Load**: With LK4 and LK6 shorted (loopback) and both termination jumpers (LK3 and LK5) inserted, the two $120\,\Omega$ resistors operate in parallel, presenting a standard $60\,\Omega$ RS-485 bus load.

---

## 💻 Software & Firmware Details

### 1. Arduino Sketch (`sketch/sketch.ino`)
- **Debug Serial**: `Serial` @ `115200 baud` (via USB Serial Monitor)
- **RS-485 UART**: `Serial1` @ `115200 baud` (via Pins D1 TX and D0 RX)
- **Operation**:
  - Transmits a test packet `ADM3068_Packet <count>` every 1000 ms via `Serial1`.
  - Continuously listens on `Serial1` for returned lines and logs received strings to the USB console.
  - Reports periodic packet transmit/receive statistics every 5 seconds.

### 2. Python App Wrapper (`python/main.py`)
- Python wrapper for Arduino App execution framework using `arduino.app_utils`.

---

## 🚀 How to Run Test

1. **Configure Hardware**: Set the jumpers on the EVAL-ADM3068EEBZ evaluation board as listed in the jumper table above.
2. **Wiring**: Connect Arduino UNO Q `D1` $\rightarrow$ `DI`, `D0` $\rightarrow$ `RO`, `5V` $\rightarrow$ `VCC`/`VIO`, and `GND` $\rightarrow$ `GND`.
3. **Upload Code**: Upload `sketch/sketch.ino` to the Arduino UNO Q.
4. **Monitor Output**: Open the Serial Monitor at `115200 baud`. You should see `TX -> ADM3068_Packet <count>` followed immediately by `RX <- ADM3068_Packet <count>`, confirming full-duplex RS-485 transmission and reception.

---

## 📚 References
- **Analog Devices EVAL-ADM3068EEBZ User Guide (UG-1540)**: Located at `Resources/EVAL-ADM3068EEBZ-UG-1540.pdf`.
