#pragma once
#include <Arduino.h>

// =============================================================================
// SMART MEDICINE BOX (SMB_V0.1) - CONFIGURATION & HARDWARE DEFINITIONS
// Target: ESP32-DevKit V1 (30-pin) with SSD1306 OLED, DS3231 RTC, A4988 & Dabble BLE
//
// Key Update in V0.1:
// - Resolves the issue where motor advanced to Position A instead of returning Home.
// - Hardware DIR pin is driven on GPIO 12 (Primary) and GPIO 27 (Alt) simultaneously.
// - Ensures clean reverse rotation back to COMPARTMENT 0 (Home / boot position).
// =============================================================================

// -----------------------------------------------------------------------------
// 1. I2C BUS DEFINITIONS (Shared by SSD1306 OLED and DS3231 RTC)
// -----------------------------------------------------------------------------
#define I2C_SDA_PIN          21      // ESP32 default I2C SDA
#define I2C_SCL_PIN          22      // ESP32 default I2C SCL
#define OLED_I2C_ADDR        0x3C    // SSD1306 0.96" OLED I2C Address
#define DS3231_I2C_ADDR      0x68    // DS3231 RTC I2C Address

#define SCREEN_WIDTH         128     // OLED width in pixels
#define SCREEN_HEIGHT        64      // OLED height in pixels
#define OLED_RESET_PIN       -1      // Reset pin (-1 if shared with ESP32 reset)

// -----------------------------------------------------------------------------
// 2. A4988 STEPPER MOTOR DRIVER PINS & SPECIFICATIONS
// -----------------------------------------------------------------------------
#define STEPPER_STEP_PIN     14      // A4988 STEP pulse pin (D14)

// DIR PIN SETUP:
// On the physical hardware / HW-134A board, DIR is connected to GPIO 12 (D12).
// In V0.0, DIR was set to 27, which left pin 12 floating HIGH (causing motor to always step forward).
// In V0.1, BOTH GPIO 12 (Primary) and GPIO 27 (Alternative) are driven simultaneously
// to guarantee 100% reliable reverse motion back to Home regardless of wiring!
#define STEPPER_DIR_PIN      12      // Primary A4988 DIR pin (D12)
#define STEPPER_DIR_PIN_ALT  27      // Secondary / Alt DIR pin (D27)
#define STEPPER_ENABLE_PIN   26      // A4988 ENABLE pin (LOW = Active, HIGH = Disabled)

// Direction polarity: Set to true if motor direction needs inversion
#define DIR_REVERSE_POLARITY false

// Stepper Motor Steps per 360-degree revolution:
// 2048 steps for 28BYJ-48 5-wire converted to bipolar with A4988
#define STEPS_PER_REVOLUTION 2048    // 2048 steps / 360 deg
#define STEP_DELAY_US        1200    // Speed control delay in microseconds

// -----------------------------------------------------------------------------
// 3. SENSORS & ACTUATORS PINS
// -----------------------------------------------------------------------------
#define HALL_SENSOR_PIN      4       // Hall Effect Sensor (Magnet calibration at Home)
#define IR_SENSOR_PIN        13      // IR Obstacle Sensor (Patient presence detection)
#define BUZZER_PIN           23      // Active / Passive Buzzer alert pin
#define BUILTIN_LED_PIN      2       // ESP32 DevKit V1 Onboard Blue LED (GPIO 2)

#define HALL_TRIGGERED_LEVEL LOW     // Active LOW when magnet detected
#define IR_TRIGGERED_LEVEL   LOW     // Active LOW when patient hand is near

// -----------------------------------------------------------------------------
// 4. COMPARTMENT PARTITIONING & GEOMETRY
// Home is COMPARTMENT 0 (0 degrees at power on).
// Total: Compartment 0 (Home) + Compartments 1 to 5, each separated by 60 degrees.
// -----------------------------------------------------------------------------
#define TOTAL_COMPARTMENTS   6       // Compartments 0 through 5 (6 total slots)
#define COMPARTMENT_SLICE_DEG 60.0f  // 360 deg / 6 = 60 degrees per partition
#define HOME_COMPARTMENT     0       // Compartment 0 is physical Home (0 degrees at boot)

// Compartment Angles relative to Home (Comp 0 = 0°):
// Comp 0 = 0°   (Home / Power-on position)
// Comp 1 = 60°  (Alarm 1 Target)
// Comp 2 = 120°
// Comp 3 = 180° (Alarm 2 Target)
// Comp 4 = 240°
// Comp 5 = 300°

// -----------------------------------------------------------------------------
// 5. TIMINGS & SCHEDULE PARAMETERS
// -----------------------------------------------------------------------------
#define SPLASH_SCREEN_MS     5000UL  // 5 seconds Splash Screen ("SMART MEDICINE BOX")
#define DABBLE_GUIDE_MS      10000UL // 10 seconds Dabble Controller Guide on OLED
#define STORAGE_DETAILS_MS   5000UL  // 5 seconds Storage details screen
#define NEXT_DOSAGE_POPUP_MS 3000UL  // 3 seconds Next dosage screen

// Automated Alarm Schedules:
// 1. Alarm 1: 3 minutes (180 seconds) after power on -> Comp 1 (60 deg), wait 30s, return Home (Comp 0)
// 2. Alarm 2: 2 minutes (120 seconds) after Alarm 1   -> Comp 3 (180 deg), wait 30s, return Home (Comp 0)
#define ALARM1_DELAY_MS      180000UL // 3 minutes = 180,000 ms from power on
#define ALARM2_INTERVAL_MS   120000UL // 2 minutes = 120,000 ms after Alarm 1
#define ALARM_DWELL_MS       30000UL  // 30 seconds wait at compartment before returning Home

// BLE Device Name
#define BLE_DEVICE_NAME      "SmartMedBox_ESP32"
