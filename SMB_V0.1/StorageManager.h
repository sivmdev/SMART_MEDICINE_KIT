#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

// =============================================================================
// STORAGE MANAGER - ESP32 Flash Preferences (NVS) for SMB_V0.1
// Manages stock counts and alarm configurations for Compartments 0 through 5.
// =============================================================================

struct CompartmentAlarm {
  bool enabled;
  int  hour;
  int  minute;
  int  pillCount;
  int  targetAngle;
};

class StorageManager {
private:
  Preferences prefs;

public:
  CompartmentAlarm alarms[TOTAL_COMPARTMENTS]; // Indices 0 to 5
  int tabletStockCounts[TOTAL_COMPARTMENTS];   // Indices 0 to 5

  StorageManager() {}

  void begin() {
    prefs.begin("smb_cfg", false);

    bool initialized = prefs.getBool("is_init", false);
    if (!initialized) {
      loadDefaults();
      saveAll();
      prefs.putBool("is_init", true);
    } else {
      loadFromFlash();
    }
  }

  void loadDefaults() {
    // Compartment 0 is Home (0 degrees)
    tabletStockCounts[0] = 0;
    alarms[0].enabled = false;
    alarms[0].hour = 0;
    alarms[0].minute = 0;
    alarms[0].pillCount = 0;
    alarms[0].targetAngle = 0;

    // Compartments 1 to 5 (each separated by 60 degrees)
    for (int i = 1; i < TOTAL_COMPARTMENTS; i++) {
      tabletStockCounts[i] = random(3, 10); // Random initial stock
      alarms[i].enabled = false;            // Disabled until user sets via Dabble
      alarms[i].hour = 8 + (i * 2);        // Spaced default hours: 10:00, 12:00, etc.
      alarms[i].minute = 0;
      alarms[i].pillCount = (i % 2 == 1) ? 2 : 1;
      alarms[i].targetAngle = i * (int)COMPARTMENT_SLICE_DEG;
    }

    // Specific defaults for Alarm 1 & 2 targets:
    // Comp 1 (60 deg): 2 pills
    alarms[1].pillCount = 2;
    // Comp 3 (180 deg): 3 pills
    alarms[3].pillCount = 3;
  }

  void loadFromFlash() {
    tabletStockCounts[0] = 0;
    alarms[0].enabled = false;
    alarms[0].hour = 0;
    alarms[0].minute = 0;
    alarms[0].pillCount = 0;
    alarms[0].targetAngle = 0;

    for (int i = 1; i < TOTAL_COMPARTMENTS; i++) {
      String prefix = "c" + String(i) + "_";
      tabletStockCounts[i]   = prefs.getInt((prefix + "stk").c_str(), 5);
      alarms[i].enabled     = prefs.getBool((prefix + "en").c_str(), false);
      alarms[i].hour        = prefs.getInt((prefix + "h").c_str(), 8);
      alarms[i].minute      = prefs.getInt((prefix + "m").c_str(), 0);
      alarms[i].pillCount   = prefs.getInt((prefix + "pills").c_str(), (i == 3 ? 3 : 2));
      alarms[i].targetAngle = i * (int)COMPARTMENT_SLICE_DEG;
    }
  }

  void saveCompartmentAlarm(int compId, int hour, int minute, int pills) {
    if (compId < 1 || compId >= TOTAL_COMPARTMENTS) return;

    alarms[compId].hour = hour;
    alarms[compId].minute = minute;
    alarms[compId].pillCount = pills;
    alarms[compId].enabled = true;

    String prefix = "c" + String(compId) + "_";
    prefs.putInt((prefix + "h").c_str(), hour);
    prefs.putInt((prefix + "m").c_str(), minute);
    prefs.putInt((prefix + "pills").c_str(), pills);
    prefs.putBool((prefix + "en").c_str(), true);

    Serial.print(F("[STORAGE] Saved Alarm for Comp "));
    Serial.print(compId);
    Serial.print(F(" -> "));
    Serial.print(hour);
    Serial.print(':');
    Serial.print(minute);
    Serial.print(F(" ("));
    Serial.print(pills);
    Serial.println(F(" pills)"));
  }

  void deductPills(int compId, int pills) {
    if (compId < 1 || compId >= TOTAL_COMPARTMENTS) return;
    if (tabletStockCounts[compId] >= pills) {
      tabletStockCounts[compId] -= pills;
    } else {
      tabletStockCounts[compId] = 0;
    }
    String key = "c" + String(compId) + "_stk";
    prefs.putInt(key.c_str(), tabletStockCounts[compId]);
  }

  void saveAll() {
    for (int i = 1; i < TOTAL_COMPARTMENTS; i++) {
      String prefix = "c" + String(i) + "_";
      prefs.putInt((prefix + "stk").c_str(), tabletStockCounts[i]);
      prefs.putBool((prefix + "en").c_str(), alarms[i].enabled);
      prefs.putInt((prefix + "h").c_str(), alarms[i].hour);
      prefs.putInt((prefix + "m").c_str(), alarms[i].minute);
      prefs.putInt((prefix + "pills").c_str(), alarms[i].pillCount);
    }
    Serial.println(F("[STORAGE] Settings successfully saved to NVS."));
  }
};
