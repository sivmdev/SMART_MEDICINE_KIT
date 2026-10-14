# ESP32 DevKit V1 (30-Pin) - Hardware Pin Plan (SMB_V0.1)
## Project: Smart Medicine Box

This document provides the hardware wiring and pin connection reference for **SMB_V0.1**.

---

## 1. Master Pinout Table

| Module / Component | Module Pin | ESP32 GPIO | ESP32 Board Label | Signal Type / Direction | Wiring & Circuit Notes |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **SSD1306 0.96" OLED Display** | **SDA** | **GPIO 21** | `D21` | I2C Bidirectional Data | Hardware I2C Bus (`0x3C`). Shared with RTC. |
| | **SCL** | **GPIO 22** | `D22` | I2C Clock Output | Hardware I2C Clock. Shared with RTC. |
| | **VCC** | `3V3` | `3V3` | Power Input | Connect to ESP32 3.3V rail. |
| | **GND** | `GND` | `GND` | Power Ground | Common system ground. |
| **DS3231 Precision RTC** | **SDA** | **GPIO 21** | `D21` | I2C Bidirectional Data | Hardware I2C Bus (`0x68`). Shared with OLED. |
| | **SCL** | **GPIO 22** | `D22` | I2C Clock Output | Hardware I2C Clock. Shared with OLED. |
| | **VCC** | `3V3` | `3V3` | Power Input | Connect to ESP32 3.3V rail. |
| | **GND** | `GND` | `GND` | Power Ground | Common system ground. |
| **A4988 Stepper Driver** | **STEP** | **GPIO 14** | `D14` | Digital Output Pulse | Generates stepping pulses (`1200 µs` interval). |
| | **DIR (Primary)** | **GPIO 12** | `D12` | Digital Output | **Primary DIR pin** on HW-134A board. Active in SMB_V0.1. |
| | **DIR (Secondary)**| **GPIO 27** | `D27` | Digital Output | **Alternative DIR pin**. Driven synchronously with GPIO 12 so reverse works regardless of wiring! |
| | **ENABLE** | **GPIO 26** | `D26` | Digital Output | Driver enable line (Active LOW). Kept LOW during movement. |
| | **MS1, MS2, MS3** | - | - | Hardware Configuration | Tied to GND for Full Step, or jumpered for microstepping. |
| | **RESET & SLEEP** | - | - | Driver Jumper | **Crucial:** Jumper `RESET` directly to `SLEEP` pin on A4988! |
| | **VDD** | `3V3` | `3V3` | Logic Power Input | Connect to ESP32 3.3V rail. |
| | **GND (Logic)** | `GND` | `GND` | Logic Ground | Connect to ESP32 GND. |
| | **VMOT** | `5V - 12V Ext` | External Supply | Motor Power Input | **Separate motor power supply**. Place a `100 µF` capacitor across VMOT & GND! |
| | **GND (Motor)** | Ext GND | External Supply | Power Ground | Tie to ESP32 Ground (Common Ground). |
| **Hall Effect Sensor** | **OUT / Signal**| **GPIO 4** | `D4` | Digital Input (Pull-Up) | Detects magnet at Compartment 0 (Home). Active LOW (`INPUT_PULLUP`). |
| | **VCC** | `3V3` | `3V3` | Sensor Power | 3.3V logic level. |
| | **GND** | `GND` | `GND` | Ground | Common ground. |
| **IR Obstacle Sensor** | **OUT / Signal**| **GPIO 13** | `D13` | Digital Input (Pull-Up) | Detects patient presence / hand reaching medicine box. Active LOW (`INPUT_PULLUP`). |
| | **VCC** | `3V3` / `VIN` | `3V3` | Sensor Power | Connect to 3.3V or 5V (LM393 comparator module). |
| | **GND** | `GND` | `GND` | Ground | Common ground. |
| **Buzzer (Alert)** | **Signal (+)** | **GPIO 23** | `D23` | Digital Output | Generates rhythmic beeping alert during medicine schedule (`300 ms` ON / `200 ms` OFF). |
| | **GND (-)** | `GND` | `GND` | Ground | Common ground. |
| **Onboard Blue LED** | **Internal Anode**| **GPIO 2** | `D2` | PWM Output | Smooth breathing illumination during medicine alarm using ESP32 PWM / `analogWrite`. |

---

## 2. Key Update in SMB_V0.1: Dual DIR Pins (GPIO 12 & GPIO 27)

* **Why it was updated**: On earlier test code (`stepper_motor.ino`), DIR was wired to **GPIO 12 (`D12`)**, while in `Integrated` diagrams it was labeled as **GPIO 27 (`D27`)**. 
* **Solution in SMB_V0.1**: The firmware drives **both GPIO 12 and GPIO 27** in exact synchronization with a 30 µs setup delay. Whichever pin your wire is connected to, the driver receives the correct `LOW` reverse signal and returns smoothly to Home (Comp 0).
