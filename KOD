#include <Wire.h>
#include <U8g2lib.h>

#define DCF_PIN 7
#define ENC_A 1
#define ENC_B 2
#define SYNC_INTERVAL_MIN 5

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

byte* bitsBuf;
int bitPos = 0;

int curHour = 0, curMinute = 0, curSecond = 0;
int curDay = 1, curMonth = 1, curYear = 2026, curWeekday = 1;
int utcOffset = 1;

bool timeSynced = false;
bool isSyncing = true;
unsigned long lastTick = 0;
unsigned long lastSyncSuccess = 0;

int framesAttempted = 0;
int lastSyncFrames = 0;

int displayMode = 0;
int lastEncA = HIGH;

const char* getShortDay(int w) {
  switch(w) {
    case 1: return "Pon";
    case 2: return "Wt";
    case 3: return "Sr";
    case 4: return "Czw";
    case 5: return "Pt";
    case 6: return "Sob";
    case 7: return "Ndz";
    default: return "?";
  }
}

int getDaysInMonth(int m, int y) {
  if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28;
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

void setup() {
  bitsBuf = (byte*)malloc(100);
  Serial.begin(115200);
  delay(1000);
  Serial.println("DCF77 START (Pin D7)");

  pinMode(DCF_PIN, INPUT);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncA = digitalRead(ENC_A);

  u8g2.begin();
  u8g2.setContrast(40);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso24_tf);
  u8g2.drawStr(22, 32, "DCF77");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(34, 50, "by SQ1KSM");
  u8g2.sendBuffer();
  delay(3000);

  startSyncMode("Start - szukam DCF77");
}

void loop() {
  checkEncoder();

  if (timeSynced && (millis() - lastTick >= 1000)) {
    lastTick += 1000;
    tickClock();
    if (!isSyncing) updateOLED();
  }

  unsigned long targetMs = ((unsigned long)SYNC_INTERVAL_MIN * 60000UL) - 12000UL;
  if (displayMode == 0 && timeSynced && !isSyncing && (millis() - lastSyncSuccess >= targetMs)) {
    startSyncMode("Okresowa korekta");
  }

  if (isSyncing) readDCF77();
}

void checkEncoder() {
  static unsigned long lastEncTime = 0;
  int currentA = digitalRead(ENC_A);

  if (lastEncA == HIGH && currentA == LOW && (millis() - lastEncTime > 50)) {
    lastEncTime = millis();
    if (digitalRead(ENC_B) == HIGH) {
      displayMode = (displayMode + 1) % 3;
    } else {
      displayMode = (displayMode + 2) % 3;
    }

    if (timeSynced && isSyncing && displayMode != 0) {
      isSyncing = false;
      u8g2.setPowerSave(0);
    }

    if (displayMode == 0 && timeSynced) {
      unsigned long targetMs = ((unsigned long)SYNC_INTERVAL_MIN * 60000UL) - 12000UL;
      if (millis() - lastSyncSuccess >= targetMs) {
        lastSyncSuccess = millis() - targetMs + 5000UL;
      }
    }

    if (timeSynced && !isSyncing) updateOLED();
  }
  lastEncA = currentA;
}

void startSyncMode(const char* reason) {
  isSyncing = true;
  bitPos = 0;
  framesAttempted = 0;

  u8g2.setPowerSave(0);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 15, "Synchronizacja DCF77");
  u8g2.drawStr(0, 30, reason);
  u8g2.drawStr(0, 48, "Wygaszam ekran...");
  u8g2.sendBuffer();
  
  delay(1500);
  if (isSyncing) u8g2.setPowerSave(1);
}

void readDCF77() {
  static int last = digitalRead(DCF_PIN);
  static unsigned long t = millis();
  int now = digitalRead(DCF_PIN);

  if (now != last) {
    unsigned long len = millis() - t;

    if (last == HIGH) {
      if (len >= 70 && len <= 130 && bitPos < 100) *(bitsBuf + bitPos++) = 0;
      else if (len >= 140 && len <= 250 && bitPos < 100) *(bitsBuf + bitPos++) = 1;
    }

    if (last == LOW && len > 1700 && len < 2200) {
      if (bitPos >= 59) {
        framesAttempted++;
        Serial.print("\nRAMKA #"); Serial.print(framesAttempted);
        Serial.print(" | BITOW="); Serial.println(bitPos);
        decodeDCF77();
      } else {
        Serial.print("\n(rozruch: "); Serial.print(bitPos); Serial.println(" bitow - pomijam)");
      }
      bitPos = 0;
    }
    t = millis();
    last = now;
  }
}

int bcdToDec(int bcd) {
  return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void decodeDCF77() {
  bool startOk = (*(bitsBuf + 20) == 1);
  int sumMin = 0; for (int i = 21; i <= 28; i++) sumMin += *(bitsBuf + i);
  bool parMinOk = (sumMin % 2 == 0);
  int sumHour = 0; for (int i = 29; i <= 35; i++) sumHour += *(bitsBuf + i);
  bool parHourOk = (sumHour % 2 == 0);
  int sumDate = 0; for (int i = 36; i <= 58; i++) sumDate += *(bitsBuf + i);
  bool parDateOk = (sumDate % 2 == 0);

  bool isCEST = (*(bitsBuf + 17) == 1 && *(bitsBuf + 18) == 0);

  int minBCD = 0; for (int i = 21; i <= 27; i++) if (*(bitsBuf + i)) minBCD |= (1 << (i - 21));
  int minute = bcdToDec(minBCD);
  int hourBCD = 0; for (int i = 29; i <= 34; i++) if (*(bitsBuf + i)) hourBCD |= (1 << (i - 29));
  int hour = bcdToDec(hourBCD);
  int dayBCD = 0; for (int i = 36; i <= 41; i++) if (*(bitsBuf + i)) dayBCD |= (1 << (i - 36));
  int day = bcdToDec(dayBCD);
  int weekday = 0; for (int i = 42; i <= 44; i++) if (*(bitsBuf + i)) weekday |= (1 << (i - 42));
  int monthBCD = 0; for (int i = 45; i <= 49; i++) if (*(bitsBuf + i)) monthBCD |= (1 << (i - 45));
  int month = bcdToDec(monthBCD);
  int yearBCD = 0; for (int i = 50; i <= 57; i++) if (*(bitsBuf + i)) yearBCD |= (1 << (i - 50));
  int year = 2000 + bcdToDec(yearBCD);

  Serial.print("CZAS DCF77: ");
  if (hour < 10) Serial.print("0"); Serial.print(hour); Serial.print(":");
  if (minute < 10) Serial.print("0"); Serial.print(minute);
  Serial.println(isCEST ? " CEST" : " CET");

  if (startOk && parMinOk && parHourOk && parDateOk) {
    curMinute = minute;
    curHour = hour;
    curSecond = 0; 
    curDay = day;
    curWeekday = weekday;
    curMonth = month;
    curYear = year;
    utcOffset = isCEST ? 2 : 1;

    lastSyncFrames = framesAttempted;
    lastSyncSuccess = millis();
        lastTick = millis() - 700; // Kompensacja opóźnienia odbiornika i ekranu (700 ms)
    timeSynced = true;
    isSyncing = false;

    u8g2.setPowerSave(0);
    updateOLED();
  }
}

void tickClock() {
  curSecond++;
  if (curSecond >= 60) {
    curSecond = 0;
    curMinute++;
    if (curMinute >= 60) {
      curMinute = 0;
      curHour++;
      if (curHour >= 24) {
        curHour = 0;
        advanceDate();
      }
    }
  }
}

void advanceDate() {
  curDay++;
  curWeekday++;
  if (curWeekday > 7) curWeekday = 1;

  if (curDay > getDaysInMonth(curMonth, curYear)) {
    curDay = 1;
    curMonth++;
    if (curMonth > 12) {
      curMonth = 1;
      curYear++;
    }
  }
}

void drawCenteredClock(int h, int m, int s, int yPos) {
  char* hmStr = (char*)malloc(10);
  sprintf(hmStr, "%02d:%02d", h, m);
  char* secStr = (char*)malloc(8);
  sprintf(secStr, "%02d", s);

  u8g2.setFont(u8g2_font_logisoso32_tn);
  int hmW = u8g2.getStrWidth(hmStr);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  int secW = u8g2.getStrWidth(secStr);

  int startX = (128 - (hmW + 4 + secW)) / 2;

  u8g2.setFont(u8g2_font_logisoso32_tn);
  u8g2.drawStr(startX, yPos, hmStr);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(startX + hmW + 4, yPos, secStr);

  free(hmStr);
  free(secStr);
}

void updateOLED() {
  u8g2.clearBuffer();

  if (displayMode == 0) {
    u8g2.setFont(u8g2_font_logisoso32_tn);
    char* hmStr = (char*)malloc(10);
    sprintf(hmStr, "%02d:%02d", curHour, curMinute);
    u8g2.drawStr(0, 33, hmStr);
    int hmWidth = u8g2.getStrWidth(hmStr);
    free(hmStr);

    u8g2.setFont(u8g2_font_logisoso16_tn);
    char* secStr = (char*)malloc(8);
    sprintf(secStr, "%02d", curSecond);
    u8g2.drawStr(hmWidth + 4, 33, secStr);
    free(secStr);

    u8g2.setFont(u8g2_font_6x10_tf);
    char* dateStr = (char*)malloc(20);
    sprintf(dateStr, "%02d.%02d.%04d %s", curDay, curMonth, curYear, getShortDay(curWeekday));
    u8g2.drawStr(0, 49, dateStr);
    free(dateStr);

    unsigned long intervalMs = ((unsigned long)SYNC_INTERVAL_MIN * 60000UL) - 12000UL;
    unsigned long elapsedMs = millis() - lastSyncSuccess;
    unsigned long remSec = (elapsedMs < intervalMs) ? ((intervalMs - elapsedMs) / 1000) : 0;

    char* bottomStr = (char*)malloc(32);
    if (remSec >= 60) {
      sprintf(bottomStr, "R-%d | S za: %dm", lastSyncFrames, (int)((remSec + 59) / 60));
    } else {
      sprintf(bottomStr, "R-%d | S za: %lus", lastSyncFrames, remSec);
    }
    u8g2.drawStr(0, 62, bottomStr);
    free(bottomStr);
  }
  else if (displayMode == 1) {
    int utcHour = (curHour - utcOffset + 24) % 24;
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr((128 - u8g2.getStrWidth("CZAS UTC")) / 2, 14, "CZAS UTC");
    drawCenteredClock(utcHour, curMinute, curSecond, 54);
  }
  else if (displayMode == 2) {
    char* hmStr = (char*)malloc(10);
    sprintf(hmStr, "%02d:%02d", curHour, curMinute);

    u8g2.setFont(u8g2_font_logisoso38_tn); // 38px - idealnie mieści wszystkie 5 znaków!
    int hmW = u8g2.getStrWidth(hmStr);
    int startX = (128 - hmW) / 2;
    if (startX < 0) startX = 0;

    u8g2.drawStr(startX, 51, hmStr);
    free(hmStr);
  }




  u8g2.sendBuffer();
}
