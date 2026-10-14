#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include "Config.h"
#include "StorageManager.h"

// =============================================================================
// RTC MANAGER - DS3231 Real-Time Clock with Software Fallback (SMB_V0.1)
// Provides resilient live timekeeping so the system never hangs if RTC is absent.
// =============================================================================

class RTCManager {
private:
  RTC_DS3231 rtc;
  bool isHwRtcAvailable;
  uint32_t softBaseUnix;
  unsigned long softBaseMillis;

public:
  RTCManager() : isHwRtcAvailable(false), softBaseUnix(0), softBaseMillis(0) {}

  bool begin() {
    if (rtc.begin()) {
      isHwRtcAvailable = true;
      if (rtc.lostPower()) {
        Serial.println(F("[RTC] DS3231 lost power, adjusting to build time..."));
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }
      Serial.println(F("[RTC] DS3231 initialized successfully."));
      return true;
    } else {
      Serial.println(F("[RTC] Warning: DS3231 not detected! Falling back to Software RTC."));
      isHwRtcAvailable = false;
      // Initialize soft RTC using compile timestamp
      DateTime buildTime(F(__DATE__), F(__TIME__));
      softBaseUnix = buildTime.unixtime();
      softBaseMillis = millis();
      return false;
    }
  }

  DateTime getNow() {
    if (isHwRtcAvailable) {
      return rtc.now();
    }
    // Calculate soft time
    unsigned long elapsedSec = (millis() - softBaseMillis) / 1000;
    return DateTime(softBaseUnix + elapsedSec);
  }

  void adjust(const DateTime &dt) {
    if (isHwRtcAvailable) {
      rtc.adjust(dt);
    }
    softBaseUnix = dt.unixtime();
    softBaseMillis = millis();
  }

  bool isHardwareAvailable() const {
    return isHwRtcAvailable;
  }
};
