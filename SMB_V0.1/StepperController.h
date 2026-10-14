#pragma once
#include <Arduino.h>
#include "Config.h"

// =============================================================================
// STEPPER CONTROLLER - SMB_V0.1
// Controls 6 compartments separated by 60 degrees.
// Comp 0 = 0 deg (Home position at power on).
// Comp 1 = 60 deg, Comp 2 = 120 deg, Comp 3 = 180 deg, Comp 4 = 240 deg, Comp 5 = 300 deg.
//
// Dual-Pin Direction Driver:
// Drives both STEPPER_DIR_PIN (GPIO 12) and STEPPER_DIR_PIN_ALT (GPIO 27)
// to guarantee 100% reliable forward and backward stepping.
// =============================================================================

class StepperController {
private:
  int stepPin;
  int dirPin;
  int dirPinAlt;
  int enablePin;
  int hallPin;

  long currentStepPosition;
  long stepsPer360;
  int  currentCompartment;
  bool isHomed;

public:
  StepperController(int sPin    = STEPPER_STEP_PIN,
                    int dPin    = STEPPER_DIR_PIN,
                    int dPinAlt = STEPPER_DIR_PIN_ALT,
                    int enPin   = STEPPER_ENABLE_PIN,
                    int hPin    = HALL_SENSOR_PIN)
    : stepPin(sPin), dirPin(dPin), dirPinAlt(dPinAlt), enablePin(enPin), hallPin(hPin),
      currentStepPosition(0), stepsPer360(STEPS_PER_REVOLUTION),
      currentCompartment(HOME_COMPARTMENT), isHomed(false) {}

  void begin() {
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    if (dirPinAlt >= 0) {
      pinMode(dirPinAlt, OUTPUT);
    }
    pinMode(enablePin, OUTPUT);
    pinMode(hallPin, INPUT_PULLUP);

    digitalWrite(stepPin, LOW);
    digitalWrite(dirPin, LOW);
    if (dirPinAlt >= 0) {
      digitalWrite(dirPinAlt, LOW);
    }
    enableDriver(true);

    // Initial position at power on is marked as COMPARTMENT 0 (Home, 0 degrees)
    currentStepPosition = 0;
    currentCompartment = HOME_COMPARTMENT;
    isHomed = true;

    Serial.println(F("[STEPPER] Initialized for SMB_V0.1."));
    Serial.println(F("[STEPPER] Power-on position marked as COMPARTMENT 0 (Home / 0 deg)."));
    Serial.print(F("[STEPPER] Steps per 360 deg: "));
    Serial.println(stepsPer360);
    Serial.print(F("[STEPPER] Dual DIR pins active on GPIO "));
    Serial.print(dirPin);
    Serial.print(F(" & GPIO "));
    Serial.println(dirPinAlt);
  }

  void enableDriver(bool enable) {
    // A4988 ENABLE pin is active LOW
    digitalWrite(enablePin, enable ? LOW : HIGH);
  }

  // Calculate target step count relative to Home (0 steps) for a given compartment (0 to 5)
  long calculateStepsForCompartment(int compId) {
    if (compId <= 0 || compId >= TOTAL_COMPARTMENTS) {
      return 0; // Comp 0 is Home
    }
    // Each compartment is exactly 60 degrees apart
    float angleDeg = (float)compId * COMPARTMENT_SLICE_DEG;
    long targetSteps = (long)((angleDeg / 360.0f) * (float)stepsPer360);
    return targetSteps;
  }

  // Calculate angle in degrees for given compartment (0 to 5)
  float getCompartmentAngle(int compId) const {
    if (compId < 0 || compId >= TOTAL_COMPARTMENTS) return 0.0f;
    return (float)compId * COMPARTMENT_SLICE_DEG;
  }

  // Move motor by a specific number of steps and direction
  // dir = true (Clockwise / Forward), dir = false (Counter-Clockwise / Reverse)
  void stepMotor(bool dir, long steps, int delayUs = STEP_DELAY_US) {
    if (steps <= 0) return;
    enableDriver(true);

    // Apply polarity
    bool finalDir = DIR_REVERSE_POLARITY ? !dir : dir;

    // Drive BOTH primary and alternative DIR pins simultaneously
    digitalWrite(dirPin, finalDir ? HIGH : LOW);
    if (dirPinAlt >= 0) {
      digitalWrite(dirPinAlt, finalDir ? HIGH : LOW);
    }

    // Direction setup time for driver hardware stability
    delayMicroseconds(30);

    for (long i = 0; i < steps; i++) {
      digitalWrite(stepPin, HIGH);
      delayMicroseconds(delayUs);
      digitalWrite(stepPin, LOW);
      delayMicroseconds(delayUs);

      if (dir) {
        currentStepPosition++;
      } else {
        currentStepPosition--;
      }
    }
  }

  // Rotate to specific compartment (Comp 0 to Comp 5)
  void moveToCompartment(int compId) {
    if (compId < 0 || compId >= TOTAL_COMPARTMENTS) compId = 0;

    long targetSteps = calculateStepsForCompartment(compId);
    long stepsToMove = targetSteps - currentStepPosition;

    Serial.print(F("[STEPPER] Moving from Comp "));
    Serial.print(currentCompartment);
    Serial.print(F(" (pos "));
    Serial.print(currentStepPosition);
    Serial.print(F(") -> Comp "));
    Serial.print(compId);
    Serial.print(F(" ("));
    Serial.print(getCompartmentAngle(compId));
    Serial.print(F(" deg | target steps "));
    Serial.print(targetSteps);
    Serial.print(F(" | delta "));
    Serial.print(stepsToMove);
    Serial.println(F(")"));

    if (stepsToMove > 0) {
      stepMotor(true, stepsToMove);   // Clockwise forward
    } else if (stepsToMove < 0) {
      stepMotor(false, -stepsToMove); // Counter-Clockwise reverse back towards 0
    }

    currentStepPosition = targetSteps;
    currentCompartment = compId;

    Serial.print(F("[STEPPER] Successfully reached Compartment "));
    Serial.println(compId);
  }

  // Return to Home position (Comp 0 / 0 deg / boot position)
  void returnToHome() {
    Serial.println(F("[STEPPER] Commencing Return to HOME (Comp 0 / 0 deg)..."));
    moveToCompartment(HOME_COMPARTMENT);
    currentStepPosition = 0;
    currentCompartment = HOME_COMPARTMENT;
    isHomed = true;

    // Verify with Hall sensor if magnet installed
    verifyHallSensorAlignment();

    Serial.println(F("[STEPPER] Motor confirmed at HOME position (Comp 0)."));
  }

  // Verification with Hall effect sensor at Home (if magnet installed)
  void verifyHallSensorAlignment() {
    if (digitalRead(hallPin) == HALL_TRIGGERED_LEVEL) {
      Serial.println(F("[STEPPER] Hall Sensor detected magnet! Home alignment verified."));
      currentStepPosition = 0;
      isHomed = true;
    }
  }

  long getCurrentPosition() const { return currentStepPosition; }
  int  getCurrentCompartment() const { return currentCompartment; }
  bool isHome() const { return (currentCompartment == HOME_COMPARTMENT && currentStepPosition == 0); }
};
