#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include "Config.h"

// =============================================================================
// DISPLAY MODULE - SSD1306 OLED (128x64 I2C) for SMB_V0.1
// Full on-screen visual interface for Splash, Dabble Guide, Storage,
// Live Clock, Refill Mode, Alarm Settings Menu & Dosage Alerts.
// (Replaces Serial Monitor with complete on-screen UI)
// =============================================================================

class DisplayModule {
private:
  Adafruit_SSD1306 display;

public:
  DisplayModule() : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET_PIN) {}

  bool begin() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
      Serial.println(F("[DISPLAY] SSD1306 allocation failed!"));
      return false;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.display();
    return true;
  }

  // Helper: Draw centered text
  void drawCenteredText(const String &text, int y, int textSize) {
    display.setTextSize(textSize);
    int charWidth = 6 * textSize;
    int textWidth = text.length() * charWidth;
    int x = (SCREEN_WIDTH - textWidth) / 2;
    if (x < 0) x = 0;
    display.setCursor(x, y);
    display.print(text);
  }

  // ---------------------------------------------------------------------------
  // 1. Splash Screen: "SMART MEDICINE BOX" centered for 5 seconds
  // ---------------------------------------------------------------------------
  void showSplashScreen() {
    display.clearDisplay();

    // Decorative double border
    display.drawRoundRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 4, SSD1306_WHITE);
    display.drawRoundRect(3, 3, SCREEN_WIDTH - 6, SCREEN_HEIGHT - 6, 2, SSD1306_WHITE);

    // Decorative subtle accent lines
    display.drawFastHLine(24, 16, SCREEN_WIDTH - 48, SSD1306_WHITE);

    // Centered Title: Vertically and Horizontally balanced
    drawCenteredText("SMART", 22, 1);
    drawCenteredText("MEDICINE BOX", 34, 1);

    display.drawFastHLine(24, 47, SCREEN_WIDTH - 48, SSD1306_WHITE);

    display.display();
  }

  // ---------------------------------------------------------------------------
  // 2. Dabble Controller Guide: 10 Seconds total (2 slides x 5 seconds each)
  // Explains all physical Dabble Inputs on the SSD1306 screen!
  // ---------------------------------------------------------------------------
  void showDabbleGuidePage1() {
    display.clearDisplay();

    // Inverted Header
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    drawCenteredText("DABBLE GUIDE (1/2)", 2, 1);

    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(2, 16);
    display.print(F("Slide 1: System Mode"));
    display.setCursor(2, 27);
    display.print(F(" [MID]  : Auto Run"));
    display.setCursor(2, 38);
    display.print(F(" [LEFT] : Refill Motor"));
    display.setCursor(2, 49);
    display.print(F(" [RGHT] : Set Alarms"));

    display.drawFastHLine(0, 61, SCREEN_WIDTH, SSD1306_WHITE);
    display.display();
  }

  void showDabbleGuidePage2() {
    display.clearDisplay();

    // Inverted Header
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    drawCenteredText("DABBLE GUIDE (2/2)", 2, 1);

    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(2, 15);
    display.print(F("Pot 1 : Refill Slot"));
    display.setCursor(2, 26);
    display.print(F("Tact 1: Move Cursor >"));
    display.setCursor(2, 37);
    display.print(F("Pot 2 : Adjust Value"));
    display.setCursor(2, 48);
    display.print(F("Tact 2: SAVE ALARM"));
    display.setCursor(2, 56);
    display.print(F("BLE: SmartMedBox_ESP32"));

    display.display();
  }

  // ---------------------------------------------------------------------------
  // 3. Storage Inventory Details Screen (Comp 0 Home, Comp 1-5 Stocks)
  // ---------------------------------------------------------------------------
  void showStorageDetailsScreen(const int stockCounts[], int totalComps) {
    display.clearDisplay();

    drawCenteredText("STORAGE DETAILS", 1, 1);
    display.drawFastHLine(4, 10, SCREEN_WIDTH - 8, SSD1306_WHITE);

    display.setTextSize(1);
    // Display Comp 0 to Comp 5 in 2 tidy columns
    display.setCursor(2, 14);
    display.print(F("C0: Home (0\xF7)"));

    display.setCursor(2, 25);
    display.print(F("C1: Tab1 - ")); display.print(stockCounts[1]);

    display.setCursor(2, 36);
    display.print(F("C2: Tab2 - ")); display.print(stockCounts[2]);

    display.setCursor(66, 25);
    display.print(F("C3: Tab3 - ")); display.print(stockCounts[3]);

    display.setCursor(66, 36);
    display.print(F("C4: Tab4 - ")); display.print(stockCounts[4]);

    display.setCursor(66, 47);
    display.print(F("C5: Tab5 - ")); display.print(stockCounts[5]);

    display.drawFastHLine(4, 56, SCREEN_WIDTH - 8, SSD1306_WHITE);
    display.display();
  }

  // ---------------------------------------------------------------------------
  // 4. Next Dosage Notification screen
  // ---------------------------------------------------------------------------
  void showNextDosageScreen(const String &promptStr, int compId, int pills) {
    display.clearDisplay();
    display.drawRoundRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 3, SSD1306_WHITE);

    drawCenteredText("NEXT DOSAGE", 5, 1);
    display.drawFastHLine(10, 16, SCREEN_WIDTH - 20, SSD1306_WHITE);

    drawCenteredText(promptStr, 22, 1);

    String detail = "Slot: Comp " + String(compId) + " (" + String(compId * 60) + "\xF7)";
    drawCenteredText(detail, 36, 1);

    String pillStr = String(pills) + " Pills Prescribed";
    drawCenteredText(pillStr, 48, 1);

    display.display();
  }

  // ---------------------------------------------------------------------------
  // 5. Regular Idle Clock & System Status screen
  // ---------------------------------------------------------------------------
  void showClockAndStatus(const DateTime &now, int nextComp, int nextPills, const String &statusMsg, unsigned long nextAlarmSec, bool isAppConnected = false) {
    display.clearDisplay();

    // Top Header: Date, BLE status badge, and Day
    char dateBuffer[16];
    snprintf(dateBuffer, sizeof(dateBuffer), "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print(dateBuffer);

    // Live BLE status indicator in header
    display.setCursor(68, 2);
    if (isAppConnected) {
      display.print(F("[BLE*]"));
    } else {
      display.print(F("[BLE ]"));
    }

    const char *daysOfWeek[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    display.setCursor(108, 2);
    display.print(daysOfWeek[now.dayOfTheWeek()]);

    display.drawFastHLine(0, 11, SCREEN_WIDTH, SSD1306_WHITE);

    // Large Center Clock (HH:MM:SS)
    char timeBuffer[16];
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    drawCenteredText(String(timeBuffer), 15, 2);

    display.drawFastHLine(0, 34, SCREEN_WIDTH, SSD1306_WHITE);

    // Status / Next Alarm preview
    display.setTextSize(1);
    display.setCursor(2, 38);
    display.print(statusMsg);

    display.setCursor(2, 48);
    display.print(F("Next: Comp "));
    display.print(nextComp);
    display.print(F(" ("));
    display.print(nextPills);
    display.print(F(" pills)"));

    // Countdown timer or info
    display.setCursor(2, 56);
    if (nextAlarmSec > 0) {
      if (nextAlarmSec >= 3600) {
        int hrs = nextAlarmSec / 3600;
        int mins = (nextAlarmSec % 3600) / 60;
        int secs = nextAlarmSec % 60;
        char dueBuf[24];
        snprintf(dueBuf, sizeof(dueBuf), "Due in: %02d:%02d:%02d", hrs, mins, secs);
        display.print(dueBuf);
      } else {
        int mins = nextAlarmSec / 60;
        int secs = nextAlarmSec % 60;
        char dueBuf[24];
        snprintf(dueBuf, sizeof(dueBuf), "Due in: %02d:%02d", mins, secs);
        display.print(dueBuf);
      }
    } else {
      display.print(F("Status: Ready (Comp 0)"));
    }

    display.display();
  }

  // ---------------------------------------------------------------------------
  // 6. Tablet Refill Mode Screen (Slider 1 active)
  // Shows selected compartment, target angle, and position on OLED
  // ---------------------------------------------------------------------------
  void showRefillModeScreen(int targetComp, float targetAngle, int potRaw) {
    display.clearDisplay();

    // Inverted Title
    display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    drawCenteredText("TABLET REFILL MODE", 2, 1);

    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

    String compStr = "Target: Comp " + String(targetComp);
    if (targetComp == 0) compStr += " (Home)";
    drawCenteredText(compStr, 17, 1);

    String angleStr = "Angle: " + String((int)targetAngle) + " deg";
    drawCenteredText(angleStr, 29, 1);

    // Visual slider bar
    display.drawRect(14, 42, 100, 8, SSD1306_WHITE);
    int barWidth = map(potRaw, 0, 1023, 0, 96);
    if (barWidth > 0) {
      display.fillRect(16, 44, barWidth, 4, SSD1306_WHITE);
    }

    drawCenteredText("Slide 1 Center to Exit", 54, 1);
    display.display();
  }

  // ---------------------------------------------------------------------------
  // 7. Interactive Alarm Configuration Menu on SSD1306 OLED (NO SERIAL NEEDED!)
  // Tactile 1 moves cursor, Slider 2 edits value, Tactile 2 saves!
  // ---------------------------------------------------------------------------
  void showAlarmMenuScreen(int activeField, int editComp, int editHour, int editMinute, int editPills, const DateTime &now) {
    display.clearDisplay();

    // Inverted header with live date
    display.fillRect(0, 0, SCREEN_WIDTH, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(2, 2);
    display.print(F("SET ALARM"));

    char dateHdr[16];
    snprintf(dateHdr, sizeof(dateHdr), "%02d/%02d/%02d", now.day(), now.month(), now.year() % 100);
    display.setCursor(76, 2);
    display.print(dateHdr);

    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setTextSize(1);

    // Field 0: Compartment (1..5)
    display.setCursor(2, 14);
    display.print((activeField == 0) ? F("> Slot: Comp ") : F("  Slot: Comp "));
    display.print(editComp);
    display.print(F(" ("));
    display.print(editComp * 60);
    display.print(F("\xF7)"));

    // Field 1: Hour (00..23)
    display.setCursor(2, 24);
    display.print((activeField == 1) ? F("> Hour: ") : F("  Hour: "));
    if (editHour < 10) display.print('0');
    display.print(editHour);

    // Field 2: Minute (00..59)
    display.setCursor(2, 34);
    display.print((activeField == 2) ? F("> Min : ") : F("  Min : "));
    if (editMinute < 10) display.print('0');
    display.print(editMinute);

    // Field 3: Pill Count (1..5)
    display.setCursor(2, 44);
    display.print((activeField == 3) ? F("> Pill: ") : F("  Pill: "));
    display.print(editPills);
    display.print(F(" tablets"));

    // Field 4: Save & Confirm
    display.setCursor(2, 54);
    if (activeField == 4) {
      display.print(F("> [>> SAVE & CONFIRM <<]"));
    } else {
      display.print(F("  [ SAVE ALARM ]"));
    }

    display.display();
  }

  // Fallback overload without DateTime
  void showAlarmMenuScreen(int activeField, int editComp, int editHour, int editMinute, int editPills) {
    showAlarmMenuScreen(activeField, editComp, editHour, editMinute, editPills, DateTime(2026, 10, 12, 12, 0, 0));
  }

  // ---------------------------------------------------------------------------
  // 8. Alarm Save Confirmation Screen (Displays Slot, Target Date, Time, Dosage)
  // ---------------------------------------------------------------------------
  void showSaveConfirmationScreen(int comp, int hour, int minute, int pills, const DateTime &now) {
    display.clearDisplay();
    display.drawRoundRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 4, SSD1306_WHITE);

    drawCenteredText("ALARM CONFIRMED", 4, 1);
    display.drawFastHLine(14, 14, SCREEN_WIDTH - 28, SSD1306_WHITE);

    // Slot & Pills
    String line1 = "Comp " + String(comp) + " (" + String(comp * 60) + "\xF7) | " + String(pills) + " Pills";
    drawCenteredText(line1, 18, 1);

    // Scheduled Target Date: Today if upcoming, Tomorrow if time already passed today
    DateTime targetDate = now;
    if ((now.hour() * 60 + now.minute()) > (hour * 60 + minute)) {
      targetDate = now + TimeSpan(1, 0, 0, 0); // Tomorrow
    }
    char dateBuf[24];
    snprintf(dateBuf, sizeof(dateBuf), "Date: %02d/%02d/%04d", targetDate.day(), targetDate.month(), targetDate.year());
    drawCenteredText(String(dateBuf), 30, 1);

    // Scheduled Time
    char timeBuf[24];
    snprintf(timeBuf, sizeof(timeBuf), "Time: %02d:%02d:00", hour, minute);
    drawCenteredText(String(timeBuf), 42, 1);

    // Confirmation status
    drawCenteredText("Status: PENDING", 53, 1);

    display.display();
  }

  // Fallback overload without DateTime
  void showSaveConfirmationScreen(int comp, int hour, int minute, int pills) {
    showSaveConfirmationScreen(comp, hour, minute, pills, DateTime(2026, 10, 12, hour, minute, 0));
  }

  // ---------------------------------------------------------------------------
  // 9. Alarm Active / Dispensing Screen
  // ---------------------------------------------------------------------------
  void showAlarmActiveScreen(int compId, int pills, float angleDeg, bool patientArrived, unsigned long dwellRemainingSec) {
    display.clearDisplay();

    if (!patientArrived) {
      // Inverted flashing alert banner
      display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      drawCenteredText("! TIME FOR PILLS !", 3, 1);

      display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
      String compStr = "COMPARTMENT " + String(compId) + " (" + String((int)angleDeg) + "\xF7)";
      drawCenteredText(compStr, 20, 1);

      // Large pill count
      String countStr = String(pills) + " PILLS";
      drawCenteredText(countStr, 34, 2);

      drawCenteredText("Approach Box (IR)", 53, 1);
    } else {
      // Patient has reached the box!
      display.drawRoundRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 3, SSD1306_WHITE);
      drawCenteredText("PATIENT TAKING PILLS", 6, 1);
      display.drawFastHLine(10, 18, SCREEN_WIDTH - 20, SSD1306_WHITE);

      String countStr = "Take " + String(pills) + " Pills";
      drawCenteredText(countStr, 24, 1);

      String compStr = "From Slot #" + String(compId);
      drawCenteredText(compStr, 36, 1);

      // 30 seconds countdown display
      char timerBuf[20];
      snprintf(timerBuf, sizeof(timerBuf), "Home in: %02lu s", dwellRemainingSec);
      drawCenteredText(String(timerBuf), 49, 1);
    }

    display.display();
  }

  // ---------------------------------------------------------------------------
  // 10. Motor Returning Home Screen
  // ---------------------------------------------------------------------------
  void showHomingScreen(int fromComp) {
    display.clearDisplay();
    display.drawRoundRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, 3, SSD1306_WHITE);

    drawCenteredText("DOSAGE COMPLETE", 10, 1);
    display.drawFastHLine(12, 23, SCREEN_WIDTH - 24, SSD1306_WHITE);

    String fromStr = "From Comp " + String(fromComp) + " -> Comp 0";
    drawCenteredText(fromStr, 31, 1);

    drawCenteredText("Returning to", 42, 1);
    drawCenteredText("HOME POSITION (0\xF7)", 52, 1);

    display.display();
  }
};
