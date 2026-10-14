/*
 * =========================================================================================
 * Project: SMART MEDICINE BOX (SMB_V0.1)
 * Board:   ESP32 DevKit V1 (30-pin)
 *
 * Updates in V0.1:
 *   - Fixes the bug where motor advanced forward to 'Position A' instead of returning Home.
 *   - Both GPIO 12 (primary) and GPIO 27 (alt) are driven synchronously as DIR signals,
 *     ensuring clean reverse rotation back to COMPARTMENT 0 (Home / boot position).
 *   - Full verification after both Alarm 1 (Comp 1 -> Home) and Alarm 2 (Comp 3 -> Home).
 *
 * Core Features:
 *   1. SSD1306 0.96" OLED:
 *      - 5s Splash Screen: "SMART MEDICINE BOX"
 *      - 10s Dabble Controller Guide on OLED (replacing Serial with full on-screen UI)
 *      - 5s Storage Details Screen (Comp 0 Home, Comp 1-5 Stocks)
 *      - 3s Next Dosage Preview
 *      - Live Clock & System Status
 *      - Complete On-Screen Dabble Refill & Alarm Setting Menus
 *
 *   2. Stepper Motor & Compartment Geometry:
 *      - Comp 0: Home position (0 deg at power on)
 *      - Comp 1 to 5: Each separated by 60 deg (60, 120, 180, 240, 300 deg)
 *      - 2048 steps per 360 deg revolution calibration (341 steps per 60 deg).
 *
 *   3. Automated Alarm & Rotation Timeline:
 *      - Alarm 1: 3 minutes after power on -> Comp 1 (60 deg), wait 30s, return to Home (Comp 0)
 *      - Alarm 2: 2 minutes after Alarm 1  -> Comp 3 (180 deg), wait 30s, return to Home (Comp 0)
 *
 *   4. Dabble BLE Inputs Integration:
 *      - Slide Switch 1: Position 0 (Mid) = Auto Run | Position 1 (Left) = Refill | Position 2 (Right) = Set Alarm
 *      - Slider 1 (Pot 1): Steers motor to Comp 0-5 in Refill Mode
 *      - Tactile 1: Moves cursor in Alarm Setting Mode
 *      - Slider 2 (Pot 2): Adjusts value of active field
 *      - Tactile 2: Saves alarm to Flash memory with confirmation
 * =========================================================================================
 */

#define CUSTOM_SETTINGS
#define INCLUDE_DABBLEINPUTS_MODULE
#define INCLUDE_TERMINAL_MODULE

#include <DabbleESP32.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include "Config.h"
#include "DisplayModule.h"
#include "StepperController.h"
#include "StorageManager.h"
#include "RTCManager.h"

// -----------------------------------------------------------------------------
// Global Objects
// -----------------------------------------------------------------------------
DisplayModule     displayMod;
StepperController stepperMod;
StorageManager    storageMod;
RTCManager        rtcMod;

// -----------------------------------------------------------------------------
// System States & Modes
// -----------------------------------------------------------------------------
enum SystemBootPhase {
  BOOT_SPLASH,          // 0 to 5s: "SMART MEDICINE BOX" centered
  BOOT_DABBLE_GUIDE_1,  // 5 to 10s: Dabble Guide Slide 1 on OLED
  BOOT_DABBLE_GUIDE_2,  // 10 to 15s: Dabble Guide Slide 2 on OLED
  BOOT_STORAGE_DETAILS, // 15 to 20s: Storage inventory counts
  BOOT_NEXT_DOSAGE,     // 20 to 23s: Next dosage notification
  BOOT_COMPLETE         // Boot finished, enter regular state machine
};

enum SystemState {
  STATE_IDLE,                 // Monitoring clock and waiting for alarms
  STATE_ALARM_WAIT_PATIENT,   // Motor at compartment, Buzzer pulsing, LED breathing, waiting for IR
  STATE_COMPARTMENT_DWELL,    // Patient taking pills, 30s countdown on OLED
  STATE_RETURNING_HOME        // Motor returning to Comp 0 (Home / 0 deg)
};

SystemBootPhase bootPhase    = BOOT_SPLASH;
SystemState     currentState = STATE_IDLE;

// App Mode controlled by Dabble Slide Switch 1
enum AppMode {
  MODE_AUTO_RUN  = 0, // Slide Switch 1 Center
  MODE_REFILL    = 1, // Slide Switch 1 Left
  MODE_SET_ALARM = 2  // Slide Switch 1 Right
};
AppMode currentMode = MODE_AUTO_RUN;

// -----------------------------------------------------------------------------
// Interactive Menu / Cursor State (Dabble Alarm Setting Mode)
// -----------------------------------------------------------------------------
enum CursorField {
  FIELD_COMPARTMENT = 0,
  FIELD_HOUR        = 1,
  FIELD_MINUTE      = 2,
  FIELD_PILLS       = 3,
  FIELD_SAVE        = 4,
  TOTAL_FIELDS      = 5
};
CursorField activeField = FIELD_COMPARTMENT;

int editComp   = 1;
int editHour   = 8;
int editMinute = 0;
int editPills  = 2;

// Input Edge-detection tracking
int  lastSlide1    = -1;
int  lastSlide2    = -1;
int  lastPot1Comp  = -1;
int  lastPot2Val   = -1;
bool lastTactile1  = false;
bool lastTactile2  = false;

// -----------------------------------------------------------------------------
// Automated Alarm Sequence Tracking
// -----------------------------------------------------------------------------
unsigned long bootMillis            = 0;
unsigned long phaseStartMillis      = 0;
unsigned long stateTimer            = 0;
unsigned long lastClockUpdateMs     = 0;

bool alarm1Triggered = false;
bool alarm1Finished  = false;
bool alarm2Triggered = false;
bool alarm2Finished  = false;

unsigned long alarm1TargetMillis = 0; // 3 mins from power-on
unsigned long alarm2TargetMillis = 0; // 2 mins from Alarm 1 completion

int  activeCompartment = 0;
int  activePillCount   = 0;
bool patientArrived    = false;
bool isAppConnected    = false; // BLE App connection state

enum AlarmType {
  ALARM_TYPE_NONE,
  ALARM_TYPE_DEMO_1,
  ALARM_TYPE_DEMO_2,
  ALARM_TYPE_USER
};
AlarmType activeAlarmType = ALARM_TYPE_NONE;

bool userAlarmTriggered[TOTAL_COMPARTMENTS] = {false};

struct PendingAlarmInfo {
  bool hasAlarm;
  String statusMsg;
  int comp;
  int pills;
  unsigned long remainingSec;
};

PendingAlarmInfo getNextPendingAlarm(const DateTime &now, unsigned long currentMillis) {
  PendingAlarmInfo info;
  info.hasAlarm = false;
  info.statusMsg = "All Alarms Completed";
  info.comp = 0;
  info.pills = 0;
  info.remainingSec = 0;

  // 1. Is Demo Alarm 1 still pending?
  if (!alarm1Triggered) {
    info.hasAlarm = true;
    info.statusMsg = "Alarm 1 Pending";
    info.comp = 1;
    info.pills = storageMod.alarms[1].pillCount;
    info.remainingSec = (alarm1TargetMillis > currentMillis) ? (alarm1TargetMillis - currentMillis) / 1000 : 0;
    return info;
  }

  // 2. Is Demo Alarm 2 still pending?
  if (alarm1Finished && !alarm2Triggered) {
    info.hasAlarm = true;
    info.statusMsg = "Alarm 2 Pending";
    info.comp = 3;
    info.pills = storageMod.alarms[3].pillCount;
    info.remainingSec = (alarm2TargetMillis > currentMillis) ? (alarm2TargetMillis - currentMillis) / 1000 : 0;
    return info;
  }

  // 3. Check for any enabled User-Configured Alarms
  long nowSec = now.hour() * 3600L + now.minute() * 60L + now.second();
  long minDiff = 999999L;
  int bestComp = -1;

  for (int c = 1; c < TOTAL_COMPARTMENTS; c++) {
    if (storageMod.alarms[c].enabled && !userAlarmTriggered[c]) {
      long alarmSec = storageMod.alarms[c].hour * 3600L + storageMod.alarms[c].minute * 60L;
      long diff = alarmSec - nowSec;
      if (diff < 0) {
        diff += 86400L; // Scheduled for tomorrow at that time
      }
      if (diff < minDiff) {
        minDiff = diff;
        bestComp = c;
      }
    }
  }

  if (bestComp != -1) {
    info.hasAlarm = true;
    info.statusMsg = "Comp " + String(bestComp) + " Alarm Pending";
    info.comp = bestComp;
    info.pills = storageMod.alarms[bestComp].pillCount;
    info.remainingSec = (unsigned long)minDiff;
    return info;
  }

  return info;
}

// -----------------------------------------------------------------------------
// Helper: Buzzer and LED Effects
// -----------------------------------------------------------------------------
void updateAlarmBuzzer(bool active) {
  if (!active) {
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  // Pulsing alert pattern: 300ms ON, 200ms OFF
  int cycleMs = millis() % 500;
  digitalWrite(BUZZER_PIN, (cycleMs < 300) ? HIGH : LOW);
}

void updateBreathingLED(bool active) {
  if (!active) {
    analogWrite(BUILTIN_LED_PIN, 0);
    return;
  }
  // Smooth 2-second breathing cycle (0 -> 255 -> 0)
  int cycleMs = millis() % 2000;
  int brightness = (cycleMs < 1000) 
                   ? map(cycleMs, 0, 1000, 0, 255) 
                   : map(cycleMs, 1000, 2000, 255, 0);
  analogWrite(BUILTIN_LED_PIN, brightness);
}

void beepShort(int durationMs = 30) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------
void app_connect();
void pumpDelay(unsigned long ms);
void handleBootSequence();
void handleDabbleModeSwitch();
void handleRefillMode();
void handleAlarmSettingMode();
void handleAutomatedTimeline();

// =============================================================================
// ARDUINO SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n========================================================"));
  Serial.println(F("     SMART MEDICINE BOX (SMB_V0.1) - SYSTEM BOOT       "));
  Serial.println(F("========================================================"));

  // Initialize GPIO pins
  pinMode(BUILTIN_LED_PIN, OUTPUT);
  analogWrite(BUILTIN_LED_PIN, 0);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(IR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);

  // Initialize I2C Bus (SDA=21, SCL=22)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  // 1. Initialize SSD1306 Display
  if (!displayMod.begin()) {
    Serial.println(F("[ERROR] SSD1306 OLED initialization failed!"));
  } else {
    Serial.println(F("[DISPLAY] SSD1306 OLED initialized successfully."));
  }

  // 2. Initialize Flash Storage (Preferences)
  randomSeed(micros());
  storageMod.begin();

  // 3. Initialize DS3231 RTC
  rtcMod.begin();

  // 4. Initialize Stepper Motor (Power-on marked as COMPARTMENT 0 / Home 0 deg)
  stepperMod.begin();

  // 5. Initialize Dabble BLE Stack
  Dabble.begin(BLE_DEVICE_NAME);
  Serial.print(F("[BLE] Started advertising as: "));
  Serial.println(BLE_DEVICE_NAME);

  // Configure continuous BLE advertising parameters
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  if (pAdvertising) {
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    pAdvertising->start();
    Serial.println(F("[BLE] Continuous advertising enabled."));
  }

  // Setup Timers
  bootMillis         = millis();
  phaseStartMillis   = bootMillis;
  alarm1TargetMillis = bootMillis + ALARM1_DELAY_MS; // Alarm 1 in 3 minutes

  // Immediately display Splash Screen on OLED
  displayMod.showSplashScreen();
  bootPhase = BOOT_SPLASH;
}

// =============================================================================
// ARDUINO LOOP
// =============================================================================
void loop() {
  // CRITICAL: processInput() must be called on every loop iteration for Dabble BLE
  Dabble.processInput();

  // App Connect Thread: Keeps BLE connection alive, handles advertising & developer link
  app_connect();

  unsigned long currentMillis = millis();

  // ---------------------------------------------------------------------------
  // 1. Handle Boot Sequence Screens (Splash -> Guide -> Storage -> Next Dosage)
  // ---------------------------------------------------------------------------
  if (bootPhase != BOOT_COMPLETE) {
    handleBootSequence();
    delay(10);
    return;
  }

  // ---------------------------------------------------------------------------
  // 2. Read Mode Selection from Dabble Slide Switch 1
  // ---------------------------------------------------------------------------
  handleDabbleModeSwitch();

  // ---------------------------------------------------------------------------
  // 3. Mode Execution
  // ---------------------------------------------------------------------------
  if (currentMode == MODE_REFILL) {
    // TABLET REFILL MODE: Slider 1 steers motor to Comp 0-5
    handleRefillMode();
  } else if (currentMode == MODE_SET_ALARM) {
    // ALARM SETTING MODE: Menu rendered directly on SSD1306
    handleAlarmSettingMode();
  } else {
    // AUTO RUN MODE: Runs automated Alarm 1 (3 min) and Alarm 2 (2 min) timeline
    handleAutomatedTimeline();
  }

  delay(10);
}

// Non-blocking delay helper that keeps Dabble BLE and app_connect running
void pumpDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    Dabble.processInput();
    app_connect();
    delay(5);
  }
}

// =============================================================================
// APP CONNECT THREAD / HANDLER
// 1. Keeps BLE advertising broadcasting so the phone can connect at any time.
// 2. Detects connection state transitions (Connected / Disconnected).
// 3. Sends instant connection welcome & ACK banner to Dabble Terminal.
// 4. Provides interactive developer terminal commands (PING, STATUS, HOME, etc.)
// =============================================================================
void app_connect() {
  unsigned long currentMillis = millis();
  static unsigned long lastAdvCheckMs = 0;
  static unsigned long lastPacketTime = 0;
  static bool wasConnected = false;

  // 1. Process Developer Commands from Dabble Terminal tab
  if (Terminal.available() > 0) {
    lastPacketTime = currentMillis;

    String cmd = Terminal.readString();
    cmd.trim();

    Serial.print(F("[TERMINAL CMD] Received: \""));
    Serial.print(cmd);
    Serial.println(F("\""));

    if (cmd.equalsIgnoreCase("PING") || cmd.equalsIgnoreCase("CONNECT")) {
      Terminal.println(">> PONG! SmartMedBox_ESP32 is online.");
      Terminal.print(">> Current Slot: Comp ");
      Terminal.println(stepperMod.getCurrentCompartment());
    } 
    else if (cmd.equalsIgnoreCase("STATUS") || cmd.equalsIgnoreCase("INFO")) {
      Terminal.println("\n=== SMART MEDICINE BOX STATUS ===");
      DateTime now = rtcMod.getNow();
      char timeBuf[32];
      snprintf(timeBuf, sizeof(timeBuf), "RTC: %02d:%02d:%02d (%02d/%02d)", 
               now.hour(), now.minute(), now.second(), now.day(), now.month());
      Terminal.println(timeBuf);
      Terminal.print("Active Mode: ");
      if (currentMode == MODE_REFILL) Terminal.println("TABLET REFILL");
      else if (currentMode == MODE_SET_ALARM) Terminal.println("ALARM SETTING");
      else Terminal.println("AUTO RUN");
      Terminal.print("Motor Position: Comp ");
      Terminal.print(stepperMod.getCurrentCompartment());
      Terminal.print(" (");
      Terminal.print(stepperMod.getCompartmentAngle(stepperMod.getCurrentCompartment()));
      Terminal.println(" deg)");
      now = rtcMod.getNow();
      Terminal.print("RTC Time: ");
      if (now.hour() < 10) Terminal.print('0'); Terminal.print(now.hour()); Terminal.print(':');
      if (now.minute() < 10) Terminal.print('0'); Terminal.print(now.minute()); Terminal.print(':');
      if (now.second() < 10) Terminal.print('0'); Terminal.println(now.second());
      PendingAlarmInfo nextAlm = getNextPendingAlarm(now, millis());
      Terminal.print("Next Alarm: ");
      Terminal.println(nextAlm.statusMsg);
      if (nextAlm.hasAlarm) {
        Terminal.print("Slot: Comp "); Terminal.print(nextAlm.comp);
        Terminal.print(" | Dosage: "); Terminal.print(nextAlm.pills); Terminal.println(" pills");
        Terminal.print("Due in: "); Terminal.print(nextAlm.remainingSec / 60); Terminal.print("m ");
        Terminal.print(nextAlm.remainingSec % 60); Terminal.println("s");
      }
      Terminal.println("=================================\n");
    }
    else if (cmd.equalsIgnoreCase("HOME")) {
      Terminal.println(">> Returning to Home (Comp 0)...");
      stepperMod.returnToHome();
      Terminal.println(">> Motor confirmed at Home (0 deg).");
    }
    else if (cmd.equalsIgnoreCase("C1") || cmd.equalsIgnoreCase("COMP 1")) {
      Terminal.println(">> Moving to Comp 1 (60 deg)...");
      stepperMod.moveToCompartment(1);
      Terminal.println(">> At Comp 1.");
    }
    else if (cmd.equalsIgnoreCase("C3") || cmd.equalsIgnoreCase("COMP 3")) {
      Terminal.println(">> Moving to Comp 3 (180 deg)...");
      stepperMod.moveToCompartment(3);
      Terminal.println(">> At Comp 3.");
    }
    else if (cmd.equalsIgnoreCase("BEEP")) {
      beepShort(80);
      Terminal.println(">> Buzzer Beep OK.");
    }
    else if (cmd.equalsIgnoreCase("HELP")) {
      Terminal.println("Available Developer Commands:");
      Terminal.println(" PING   - Check connection");
      Terminal.println(" STATUS - Full diagnostics");
      Terminal.println(" HOME   - Move motor to Comp 0");
      Terminal.println(" C1     - Test Comp 1 (60 deg)");
      Terminal.println(" C3     - Test Comp 3 (180 deg)");
      Terminal.println(" BEEP   - Sound buzzer test");
    }
    else {
      Terminal.print(">> Command '");
      Terminal.print(cmd);
      Terminal.println("' received. Send 'HELP' for list.");
    }
  }

  // 2. Track activity on Inputs module
  int s1 = Inputs.getSlideSwitch1Value();
  int p1 = Inputs.getPot1Value();
  if (s1 != 0 || p1 > 10 || Inputs.getTactileSwitch1Value() || Inputs.getTactileSwitch2Value()) {
    lastPacketTime = currentMillis;
  }

  // 3. Detect connection state
  if (currentMillis - lastPacketTime < 15000UL && lastPacketTime > 0) {
    isAppConnected = true;
  } else {
    isAppConnected = false;
  }

  // 4. Connection event triggers
  if (isAppConnected && !wasConnected) {
    wasConnected = true;
    Serial.println(F("\n[BLE] >>> DABBLE APP CONNECTED! <<<"));
    Terminal.println("\n========================================");
    Terminal.println("  CONNECTED TO SMART MEDICINE BOX!      ");
    Terminal.println("========================================");
    Terminal.println("Device: SmartMedBox_ESP32");
    Terminal.println("Status: Online & Ready");
    Terminal.println("Slide 1: Mid=Auto, Left=Refill, Right=Set");
    Terminal.println("Send 'HELP' for developer commands.");
    Terminal.println("========================================\n");
    beepShort(50);
  } else if (!isAppConnected && wasConnected) {
    wasConnected = false;
    Serial.println(F("\n[BLE] >>> Dabble App Disconnected! Re-advertising... <<<"));
  }

  // 5. Periodic Advertising Keep-Alive Thread (every 3 seconds)
  // Guarantees BLE beacon stays discoverable even if connection drops or times out
  if (currentMillis - lastAdvCheckMs >= 3000UL) {
    lastAdvCheckMs = currentMillis;
    if (!isAppConnected) {
      BLEAdvertising *pAdv = BLEDevice::getAdvertising();
      if (pAdv) {
        pAdv->start();
      }
    }
  }
}

// =============================================================================
// BOOT SEQUENCE HANDLER (On SSD1306 OLED)
// =============================================================================
void handleBootSequence() {
  unsigned long currentMillis = millis();
  unsigned long elapsedPhase  = currentMillis - phaseStartMillis;

  switch (bootPhase) {
    case BOOT_SPLASH:
      // Show Splash Screen for 5 seconds
      if (elapsedPhase >= SPLASH_SCREEN_MS) {
        Serial.println(F("[BOOT] Splash complete. Showing Dabble Controller Guide (1/2)..."));
        displayMod.showDabbleGuidePage1();
        phaseStartMillis = currentMillis;
        bootPhase = BOOT_DABBLE_GUIDE_1;
      }
      break;

    case BOOT_DABBLE_GUIDE_1:
      // Show Dabble Guide Page 1 for 5 seconds
      if (elapsedPhase >= 5000UL) {
        Serial.println(F("[BOOT] Showing Dabble Controller Guide (2/2)..."));
        displayMod.showDabbleGuidePage2();
        phaseStartMillis = currentMillis;
        bootPhase = BOOT_DABBLE_GUIDE_2;
      }
      break;

    case BOOT_DABBLE_GUIDE_2:
      // Show Dabble Guide Page 2 for 5 seconds (Total Guide = 10s)
      if (elapsedPhase >= 5000UL) {
        Serial.println(F("[BOOT] Guide complete. Showing Storage Details for 5 seconds..."));
        displayMod.showStorageDetailsScreen(storageMod.tabletStockCounts, TOTAL_COMPARTMENTS);
        phaseStartMillis = currentMillis;
        bootPhase = BOOT_STORAGE_DETAILS;
      }
      break;

    case BOOT_STORAGE_DETAILS:
      // Show Storage details screen for 5 seconds
      if (elapsedPhase >= STORAGE_DETAILS_MS) {
        Serial.println(F("[BOOT] Showing Next Dosage prompt for 3 seconds..."));
        displayMod.showNextDosageScreen("First Alarm in 3 Mins", 1, storageMod.alarms[1].pillCount);
        phaseStartMillis = currentMillis;
        bootPhase = BOOT_NEXT_DOSAGE;
      }
      break;

    case BOOT_NEXT_DOSAGE:
      // Show Next Dosage screen for 3 seconds
      if (elapsedPhase >= NEXT_DOSAGE_POPUP_MS) {
        Serial.println(F("[BOOT] Boot sequence complete. Entering regular AUTO RUN mode."));
        bootPhase = BOOT_COMPLETE;
        currentState = STATE_IDLE;
        lastClockUpdateMs = 0; // Trigger immediate clock refresh
      }
      break;

    default:
      bootPhase = BOOT_COMPLETE;
      break;
  }
}

// =============================================================================
// DABBLE MODE SELECTION: Slide Switch 1
// Position 0 (Center) = AUTO RUN MODE
// Position 1 (Left)   = TABLET REFILL MODE
// Position 2 (Right)  = ALARM SETTING MODE
// =============================================================================
void handleDabbleModeSwitch() {
  int slide1 = Inputs.getSlideSwitch1Value();
  if (slide1 != lastSlide1) {
    lastSlide1 = slide1;

    if (slide1 == 1) {
      currentMode = MODE_REFILL;
      Serial.println(F("\n[MODE] >>> ENTERED TABLET REFILL MODE <<<"));
      Terminal.println("\n--- TABLET REFILL MODE ---");
      Terminal.println("Use Slider 1 to rotate motor to Comp 0-5.");
      digitalWrite(BUILTIN_LED_PIN, HIGH);
      lastPot1Comp = -1; // Force immediate update
    } else if (slide1 == 2) {
      currentMode = MODE_SET_ALARM;
      Serial.println(F("\n[MODE] >>> ENTERED ALARM SETTING MODE <<<"));
      Terminal.println("\n--- ALARM SETTING MODE ---");
      Terminal.println("Tactile 1: Move Cursor | Slider 2: Value | Tactile 2: Save Alarm");
      digitalWrite(BUILTIN_LED_PIN, HIGH);
      // Pre-seed editHour & editMinute close to current time for convenient testing
      DateTime now = rtcMod.getNow();
      editHour = now.hour();
      editMinute = (now.minute() + 2) % 60;
      // Immediately display menu on SSD1306
      displayMod.showAlarmMenuScreen((int)activeField, editComp, editHour, editMinute, editPills, now);
    } else {
      currentMode = MODE_AUTO_RUN;
      Serial.println(F("\n[MODE] >>> ENTERED AUTO RUN MODE <<<"));
      Terminal.println("\n--- AUTO RUN MODE ---");
      Terminal.println("Box running schedule.");

      // Check and announce next pending alarm to Terminal
      DateTime now = rtcMod.getNow();
      PendingAlarmInfo nextAlm = getNextPendingAlarm(now, millis());
      Terminal.print("Status: ");
      Terminal.println(nextAlm.statusMsg);
      if (nextAlm.hasAlarm) {
        Terminal.print("Next: Comp "); Terminal.print(nextAlm.comp);
        Terminal.print(" | "); Terminal.print(nextAlm.pills); Terminal.println(" pills");
        Terminal.print("Due in: "); Terminal.print(nextAlm.remainingSec / 60); Terminal.print("m ");
        Terminal.print(nextAlm.remainingSec % 60); Terminal.println("s");
      }

      // Automatically return motor to COMPARTMENT 0 (Home / 0 deg)
      if (!stepperMod.isHome()) {
        displayMod.showHomingScreen(stepperMod.getCurrentCompartment());
        stepperMod.returnToHome();
      }
      digitalWrite(BUILTIN_LED_PIN, LOW);
      lastClockUpdateMs = 0; // Refresh live clock
    }
  }
}

// =============================================================================
// REFILL MODE: Potentiometer 1 (Slider 1)
// Maps 0-1023 to Compartments 0 through 5 (0, 60, 120, 180, 240, 300 deg)
// =============================================================================
void handleRefillMode() {
  int pot1 = Inputs.getPot1Value(); // 0 to 1023

  // Map 0-1023 to discrete compartments 0 to 5
  int targetComp = map(pot1, 0, 1023, 0, 5);
  if (targetComp < 0) targetComp = 0;
  if (targetComp > 5) targetComp = 5;

  float targetAngle = stepperMod.getCompartmentAngle(targetComp);

  if (targetComp != lastPot1Comp) {
    lastPot1Comp = targetComp;

    Serial.print(F("[REFILL] Slider moved -> Compartment "));
    Serial.print(targetComp);
    Serial.print(F(" (Angle: "));
    Serial.print(targetAngle);
    Serial.println(F(" deg)"));

    // Update OLED Display
    displayMod.showRefillModeScreen(targetComp, targetAngle, pot1);

    // Rotate motor to compartment opening
    stepperMod.moveToCompartment(targetComp);
  }
}

// =============================================================================
// ALARM SETTING MODE: Full Interactive Menu on SSD1306 OLED
// Tactile 1 = Advance Cursor | Slider 2 = Value | Tactile 2 = Save Alarm
// =============================================================================
void handleAlarmSettingMode() {
  bool needsDisplayUpdate = false;

  // 1. Tactile Switch 1: Move Cursor through fields
  bool currentTactile1 = (Inputs.getTactileSwitch1Value() == 1);
  if (currentTactile1 && !lastTactile1) {
    beepShort(30);
    activeField = (CursorField)((activeField + 1) % TOTAL_FIELDS);
    needsDisplayUpdate = true;
    Serial.print(F("[MENU] Active field changed to: "));
    Serial.println((int)activeField);
  }
  lastTactile1 = currentTactile1;

  // 2. Potentiometer 2 (Slider 2): Dynamic Value Adjustment
  int pot2 = Inputs.getPot2Value();
  if (abs(pot2 - lastPot2Val) > 20) {
    lastPot2Val = pot2;

    switch (activeField) {
      case FIELD_COMPARTMENT: {
        int newComp = map(pot2, 0, 1023, 1, 5); // Comp 1 to 5
        if (newComp != editComp) {
          editComp = newComp;
          needsDisplayUpdate = true;
        }
        break;
      }
      case FIELD_HOUR: {
        int newHour = map(pot2, 0, 1023, 0, 23); // 00 to 23
        if (newHour != editHour) {
          editHour = newHour;
          needsDisplayUpdate = true;
        }
        break;
      }
      case FIELD_MINUTE: {
        int newMin = map(pot2, 0, 1023, 0, 59); // 00 to 59
        if (newMin != editMinute) {
          editMinute = newMin;
          needsDisplayUpdate = true;
        }
        break;
      }
      case FIELD_PILLS: {
        int newPills = map(pot2, 0, 1023, 1, 5); // 1 to 5 pills
        if (newPills != editPills) {
          editPills = newPills;
          needsDisplayUpdate = true;
        }
        break;
      }
      case FIELD_SAVE:
        break;
      default:
        break;
    }
  }

  // 3. Tactile Switch 2: Save & Confirm
  bool currentTactile2 = (Inputs.getTactileSwitch2Value() == 1);
  if (currentTactile2 && !lastTactile2) {
    // Confirmation double beep
    beepShort(70); delay(50); beepShort(70);

    // Save to Flash storage
    storageMod.saveCompartmentAlarm(editComp, editHour, editMinute, editPills);

    // Mark as pending/untriggered so it will definitely trigger and show as pending
    userAlarmTriggered[editComp] = false;

    // Retrieve current time and calculate scheduled date
    DateTime now = rtcMod.getNow();
    DateTime targetDate = now;
    if ((now.hour() * 60 + now.minute()) > (editHour * 60 + editMinute)) {
      targetDate = now + TimeSpan(1, 0, 0, 0); // Scheduled for tomorrow
    }

    // Log to Serial
    Serial.println(F("\n[ALARM] >>> USER ALARM SAVED & CONFIRMED <<<"));
    Serial.print(F("  Compartment : ")); Serial.println(editComp);
    Serial.print(F("  Date        : ")); Serial.print(targetDate.day()); Serial.print('/'); Serial.print(targetDate.month()); Serial.print('/'); Serial.println(targetDate.year());
    Serial.print(F("  Time        : ")); Serial.print(editHour); Serial.print(':'); Serial.println(editMinute);
    Serial.print(F("  Dosage      : ")); Serial.print(editPills); Serial.println(F(" Pills"));

    // Echo formatted summary to Dabble Terminal
    Terminal.println("\n========================================");
    Terminal.println("        ALARM SAVED & CONFIRMED!        ");
    Terminal.println("========================================");
    Terminal.print("Compartment : Comp "); Terminal.print(editComp);
    Terminal.print(" ("); Terminal.print(editComp * 60); Terminal.println(" deg)");
    Terminal.print("Date        : ");
    if (targetDate.day() < 10) Terminal.print('0'); Terminal.print(targetDate.day()); Terminal.print('/');
    if (targetDate.month() < 10) Terminal.print('0'); Terminal.print(targetDate.month()); Terminal.print('/');
    Terminal.println(targetDate.year());
    Terminal.print("Time        : ");
    if (editHour < 10) Terminal.print('0'); Terminal.print(editHour); Terminal.print(':');
    if (editMinute < 10) Terminal.print('0'); Terminal.print(editMinute); Terminal.println(":00");
    Terminal.print("Dosage      : "); Terminal.print(editPills); Terminal.println(" Tablets");
    Terminal.println("Status      : Pending in Auto Run Mode");
    Terminal.println("========================================\n");

    // Show Confirmation Screen on SSD1306 for 2.5 seconds (shows Slot, Date, Time, Pills)
    displayMod.showSaveConfirmationScreen(editComp, editHour, editMinute, editPills, now);
    pumpDelay(2500);

    // Reset cursor to Compartment
    activeField = FIELD_COMPARTMENT;
    needsDisplayUpdate = true;
  }
  lastTactile2 = currentTactile2;

  // Refresh OLED menu if state changed
  if (needsDisplayUpdate) {
    DateTime now = rtcMod.getNow();
    displayMod.showAlarmMenuScreen((int)activeField, editComp, editHour, editMinute, editPills, now);
  }
}

// =============================================================================
// AUTOMATED TIMELINE & ALARM SCHEDULING (SMB_V0.1)
// Alarm 1: 3 minutes from power on -> Comp 1 (60 deg), wait 30s, return Home (Comp 0)
// Alarm 2: 2 minutes after Alarm 1 -> Comp 3 (180 deg), wait 30s, return Home (Comp 0)
// =============================================================================
void handleAutomatedTimeline() {
  unsigned long currentMillis = millis();
  DateTime now = rtcMod.getNow();

  switch (currentState) {

    // -------------------------------------------------------------------------
    // STATE 1: IDLE - Live clock & Alarm monitoring
    // -------------------------------------------------------------------------
    case STATE_IDLE: {
      updateBreathingLED(false);
      updateAlarmBuzzer(false);

      // Check for Alarm 1 Trigger (3 mins from power on)
      if (!alarm1Triggered && (currentMillis >= alarm1TargetMillis)) {
        alarm1Triggered = true;
        activeCompartment = 1;
        activePillCount   = storageMod.alarms[1].pillCount;
        patientArrived    = false;
        activeAlarmType   = ALARM_TYPE_DEMO_1;

        Serial.println(F("\n=================================================="));
        Serial.println(F("  >>> ALARM 1 TRIGGERED (3 MINS AFTER BOOT) <<<   "));
        Serial.println(F("  Target: Compartment 1 (60 deg) | Dosage: 2 Pills "));
        Serial.println(F("=================================================="));

        // Display Alert on SSD1306
        displayMod.showAlarmActiveScreen(activeCompartment, activePillCount, stepperMod.getCompartmentAngle(activeCompartment), false, ALARM_DWELL_MS / 1000);

        // Rotate Stepper Motor to Compartment 1 (60 deg)
        stepperMod.moveToCompartment(activeCompartment);

        stateTimer = currentMillis;
        currentState = STATE_ALARM_WAIT_PATIENT;
        break;
      }

      // Check for Alarm 2 Trigger (2 mins from Alarm 1 completion)
      if (alarm1Finished && !alarm2Triggered && (currentMillis >= alarm2TargetMillis)) {
        alarm2Triggered = true;
        activeCompartment = 3;
        activePillCount   = storageMod.alarms[3].pillCount;
        patientArrived    = false;
        activeAlarmType   = ALARM_TYPE_DEMO_2;

        Serial.println(F("\n=================================================="));
        Serial.println(F("  >>> ALARM 2 TRIGGERED (2 MINS AFTER ALARM 1) <<<"));
        Serial.println(F("  Target: Compartment 3 (180 deg) | Dosage: 3 Pills"));
        Serial.println(F("=================================================="));

        // Display Alert on SSD1306
        displayMod.showAlarmActiveScreen(activeCompartment, activePillCount, stepperMod.getCompartmentAngle(activeCompartment), false, ALARM_DWELL_MS / 1000);

        // Rotate Stepper Motor to Compartment 3 (180 deg)
        stepperMod.moveToCompartment(activeCompartment);

        stateTimer = currentMillis;
        currentState = STATE_ALARM_WAIT_PATIENT;
        break;
      }

      // Check for User-Configured Alarms
      for (int c = 1; c < TOTAL_COMPARTMENTS; c++) {
        if (storageMod.alarms[c].enabled && !userAlarmTriggered[c]) {
          if (now.hour() == storageMod.alarms[c].hour && now.minute() == storageMod.alarms[c].minute) {
            userAlarmTriggered[c] = true;
            activeCompartment = c;
            activePillCount   = storageMod.alarms[c].pillCount;
            patientArrived    = false;
            activeAlarmType   = ALARM_TYPE_USER;

            Serial.println(F("\n=================================================="));
            Serial.print(F("  >>> USER ALARM TRIGGERED (COMPARTMENT "));
            Serial.print(c);
            Serial.println(F(") <<<"));
            Serial.print(F("  Target Angle: "));
            Serial.print(stepperMod.getCompartmentAngle(c));
            Serial.print(F(" deg | Dosage: "));
            Serial.print(activePillCount);
            Serial.println(F(" Pills"));
            Serial.println(F("=================================================="));

            if (isAppConnected) {
              Terminal.println("\n>>> ! ALARM TRIGGERED ! <<<");
              Terminal.print("Compartment "); Terminal.print(c);
              Terminal.print(" ready! Take "); Terminal.print(activePillCount);
              Terminal.println(" pills.");
            }

            // Display Alert on SSD1306
            displayMod.showAlarmActiveScreen(activeCompartment, activePillCount, stepperMod.getCompartmentAngle(activeCompartment), false, ALARM_DWELL_MS / 1000);

            // Rotate Stepper Motor to Compartment c
            stepperMod.moveToCompartment(activeCompartment);

            stateTimer = currentMillis;
            currentState = STATE_ALARM_WAIT_PATIENT;
            break;
          }
        }
      }
      if (currentState != STATE_IDLE) break;

      // Update regular live clock on SSD1306 every 1 second
      if (currentMillis - lastClockUpdateMs >= 1000) {
        lastClockUpdateMs = currentMillis;

        PendingAlarmInfo nextAlm = getNextPendingAlarm(now, currentMillis);

        displayMod.showClockAndStatus(now, nextAlm.comp, nextAlm.pills, nextAlm.statusMsg, nextAlm.remainingSec, isAppConnected);
      }
      break;
    }

    // -------------------------------------------------------------------------
    // STATE 2: ALARM ACTIVE - Waiting for patient arrival (IR Sensor)
    // -------------------------------------------------------------------------
    case STATE_ALARM_WAIT_PATIENT: {
      // Alert patient with pulsing buzzer and breathing LED
      updateBreathingLED(true);
      updateAlarmBuzzer(true);

      displayMod.showAlarmActiveScreen(activeCompartment, activePillCount, stepperMod.getCompartmentAngle(activeCompartment), false, ALARM_DWELL_MS / 1000);

      // Check IR Sensor for patient presence
      bool irDetected = (digitalRead(IR_SENSOR_PIN) == IR_TRIGGERED_LEVEL);

      // Also auto-transition after 10s if IR sensor is not connected/testing
      unsigned long elapsedWait = currentMillis - stateTimer;
      if (irDetected || (elapsedWait >= 10000UL)) {
        if (irDetected) {
          Serial.println(F("[IR] Patient arrival detected by IR sensor!"));
        } else {
          Serial.println(F("[ALARM] Dwell timer starting (Auto-advance for patient taking pills)..."));
        }

        // Silence buzzer and stop breathing LED immediately
        updateAlarmBuzzer(false);
        updateBreathingLED(false);

        patientArrived = true;
        stateTimer = currentMillis;
        currentState = STATE_COMPARTMENT_DWELL;
      }
      break;
    }

    // -------------------------------------------------------------------------
    // STATE 3: COMPARTMENT DWELL - Patient taking pills, wait 30 seconds
    // -------------------------------------------------------------------------
    case STATE_COMPARTMENT_DWELL: {
      updateBreathingLED(false);
      updateAlarmBuzzer(false);

      unsigned long elapsedMs = currentMillis - stateTimer;
      unsigned long remainingSec = (elapsedMs < ALARM_DWELL_MS) ? (ALARM_DWELL_MS - elapsedMs) / 1000 : 0;

      // Update OLED with live 30s countdown
      displayMod.showAlarmActiveScreen(activeCompartment, activePillCount, stepperMod.getCompartmentAngle(activeCompartment), true, remainingSec);

      // Check if 30 seconds wait is finished
      if (elapsedMs >= ALARM_DWELL_MS) {
        Serial.print(F("[FLOW] 30 seconds dwell at Compartment "));
        Serial.print(activeCompartment);
        Serial.println(F(" completed. Returning to Home..."));

        // Deduct pills from storage
        storageMod.deductPills(activeCompartment, activePillCount);

        currentState = STATE_RETURNING_HOME;
      }
      break;
    }

    // -------------------------------------------------------------------------
    // STATE 4: RETURNING HOME - Motor returns to Comp 0 (Home / 0 deg)
    // -------------------------------------------------------------------------
    case STATE_RETURNING_HOME: {
      displayMod.showHomingScreen(activeCompartment);

      // Return Stepper Motor to COMPARTMENT 0 (Home / 0 deg / boot position)
      stepperMod.returnToHome();

      if (activeAlarmType == ALARM_TYPE_DEMO_1) {
        alarm1Finished = true;
        // Schedule Alarm 2 for 2 minutes (120 seconds) from now!
        alarm2TargetMillis = currentMillis + ALARM2_INTERVAL_MS;
        Serial.println(F("[SCHEDULE] Alarm 1 finished. Motor confirmed at Home (Comp 0)."));
        Serial.println(F("[SCHEDULE] Alarm 2 scheduled in 2 minutes for Comp 3 (180 deg)."));

        // Show Next Dosage Preview for 3 seconds (non-blocking)
        displayMod.showNextDosageScreen("Next Alarm in 2 Mins", 3, storageMod.alarms[3].pillCount);
        pumpDelay(NEXT_DOSAGE_POPUP_MS);
      } else if (activeAlarmType == ALARM_TYPE_DEMO_2) {
        alarm2Finished = true;
        Serial.println(F("[SCHEDULE] Alarm 2 finished. Motor confirmed at Home (Comp 0)."));

        PendingAlarmInfo nextAlm = getNextPendingAlarm(now, currentMillis);
        if (nextAlm.hasAlarm) {
          Serial.println(F("[SCHEDULE] User Alarm is pending!"));
          displayMod.showNextDosageScreen(nextAlm.statusMsg, nextAlm.comp, nextAlm.pills);
        } else {
          Serial.println(F("[SCHEDULE] All alarms complete!"));
          displayMod.showNextDosageScreen("Alarms Complete!", 0, 0);
        }
        pumpDelay(NEXT_DOSAGE_POPUP_MS);
      } else {
        // User Alarm completed
        Serial.print(F("[SCHEDULE] User Alarm for Comp "));
        Serial.print(activeCompartment);
        Serial.println(F(" finished. Motor confirmed at Home (Comp 0)."));

        PendingAlarmInfo nextAlm = getNextPendingAlarm(now, currentMillis);
        if (nextAlm.hasAlarm) {
          displayMod.showNextDosageScreen(nextAlm.statusMsg, nextAlm.comp, nextAlm.pills);
        } else {
          displayMod.showNextDosageScreen("All Alarms Completed", 0, 0);
        }
        pumpDelay(NEXT_DOSAGE_POPUP_MS);
      }

      activeAlarmType = ALARM_TYPE_NONE;

      currentState = STATE_IDLE;
      lastClockUpdateMs = 0; // Immediate clock update
      break;
    }

    default:
      currentState = STATE_IDLE;
      break;
  }
}
