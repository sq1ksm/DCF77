#include <Wire.h>
#include <U8g2lib.h>

#define DCF_PIN 7
#define ENC_A 1
#define ENC_B 2

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

volatile byte* bitsBuf;
volatile byte* frameBuf;
volatile int bitPos = 0;
volatile bool frameReady = false;
volatile bool needOledUpdate = false;
volatile bool frameSynced = false;

int curHour = 0, curMinute = 0, curSecond = 0;
int curDay = 1, curMonth = 1, curYear = 2026, curWeekday = 1;
int utcOffset = 1;

bool timeSynced = false;
unsigned long lastTick = 0;
unsigned long lastGoodSyncMs = 0;

int displayMode = 0;
int lastEncA = HIGH;

volatile byte* sigWindow;
volatile int sigIdx = 0;
volatile int sigCount = 0;
volatile unsigned long lastPulseMs = 0;
volatile int lastPulseType = -1;

char* weatherBitsStr;
bool hasWeatherBits = false;
int backupAntenna = 0;
int dstAlert = 0;

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

void dcfISR() {
  static int last = LOW;
  static unsigned long t = 0;
  int now = digitalRead(DCF_PIN);
  unsigned long nowMs = millis();

  if (now != last) {
    unsigned long len = nowMs - t;

    if (last == HIGH) {
      lastPulseMs = len;
      if (len >= 70 && len <= 130) {
        if (bitPos < 100) *(bitsBuf + bitPos++) = 0;
        *(sigWindow + sigIdx) = 1;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount++;
        lastPulseType = 0;
      } else if (len >= 140 && len <= 250) {
        if (bitPos < 100) *(bitsBuf + bitPos++) = 1;
        *(sigWindow + sigIdx) = 1;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount++;
        lastPulseType = 1;
      } else if (len >= 15) {
        *(sigWindow + sigIdx) = 0;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount++;
        lastPulseType = 2;
      }
      if (displayMode == 3 || displayMode == 4 || (!timeSynced && frameSynced)) needOledUpdate = true;
    }

    if (last == LOW && len > 1700 && len < 2200) {
      if (bitPos >= 59 && !frameReady) {
        for (int i = 0; i < 59; i++) {
          *(frameBuf + i) = *(bitsBuf + i);
        }
        frameReady = true;
      }
      bitPos = 0;
      frameSynced = true;
      if (displayMode == 4 || !timeSynced) needOledUpdate = true;
    }
    t = nowMs;
    last = now;
  }
}

void setup() {
  bitsBuf = (volatile byte*)malloc(100);
  frameBuf = (volatile byte*)malloc(64);
  sigWindow = (volatile byte*)malloc(20);
  weatherBitsStr = (char*)malloc(20);
  strcpy(weatherBitsStr, "--------------");
  for (int i = 0; i < 20; i++) *(sigWindow + i) = 0;

  Serial.begin(115200);
  delay(1000);
  Serial.println("DCF77 START (Pin D7 - IRQ v4)");

  pinMode(DCF_PIN, INPUT);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncA = digitalRead(ENC_A);

  u8g2.begin();
  u8g2.setContrast(40);

  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);

  u8g2.setFont(u8g2_font_logisoso24_tf);
  int wTitle = u8g2.getStrWidth("DCF77");
  u8g2.drawStr((128 - wTitle) / 2, 33, "DCF77");

  u8g2.setFont(u8g2_font_6x10_tf);
  int wAuthor = u8g2.getStrWidth("by SQ1KSM v4");
  u8g2.drawStr((128 - wAuthor) / 2, 52, "by SQ1KSM v4");

  u8g2.sendBuffer();
  delay(3000);

  bitPos = 0;
  attachInterrupt(digitalPinToInterrupt(DCF_PIN), dcfISR, CHANGE);
  updateOLED();
}

void loop() {
  checkEncoder();

  if (frameReady) {
    decodeDCF77();
    frameReady = false;
  }

  if (timeSynced && (millis() - lastTick >= 1000)) {
    lastTick += 1000;
    tickClock();
    needOledUpdate = true;
  }

  if (needOledUpdate) {
    needOledUpdate = false;
    updateOLED();
  }
}

void checkEncoder() {
  static unsigned long lastEncTime = 0;
  int currentA = digitalRead(ENC_A);

  if (lastEncA == HIGH && currentA == LOW && (millis() - lastEncTime > 50)) {
    lastEncTime = millis();
    if (digitalRead(ENC_B) == HIGH) {
      displayMode = (displayMode + 1) % 5;
    } else {
      displayMode = (displayMode + 4) % 5;
    }
    needOledUpdate = true;
  }
  lastEncA = currentA;
}

void drawWaitingScreen(const char* title) {
  u8g2.setFont(u8g2_font_7x14B_tr);
  int w1 = u8g2.getStrWidth("OCZEKIWANIE NA");
  u8g2.drawStr((128 - w1) / 2, 18, "OCZEKIWANIE NA");

  if (!frameSynced) {
    int w2 = u8g2.getStrWidth("DCF");
    u8g2.drawStr((128 - w2) / 2, 38, "DCF");
  } else {
    int rem = 59 - (int)bitPos;
    if (rem < 0) rem = 0;

    char* line2 = (char*)malloc(20);
    sprintf(line2, "DCF - %02ds", rem);
    int w2 = u8g2.getStrWidth(line2);
    u8g2.drawStr((128 - w2) / 2, 38, line2);
    free(line2);

    u8g2.drawFrame(2, 47, 124, 12);
    int barW = ((int)bitPos * 120) / 59;
    if (barW > 120) barW = 120;
    if (barW > 0) u8g2.drawBox(4, 49, barW, 8);
  }
}

int bcdToDec(int bcd) {
  return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void decodeDCF77() {
  bool startOk = (*(frameBuf + 20) == 1);
  int sumMin = 0; for (int i = 21; i <= 28; i++) sumMin += *(frameBuf + i);
  bool parMinOk = (sumMin % 2 == 0);
  int sumHour = 0; for (int i = 29; i <= 35; i++) sumHour += *(frameBuf + i);
  bool parHourOk = (sumHour % 2 == 0);
  int sumDate = 0; for (int i = 36; i <= 58; i++) sumDate += *(frameBuf + i);
  bool parDateOk = (sumDate % 2 == 0);

  bool isCEST = (*(frameBuf + 17) == 1 && *(frameBuf + 18) == 0);

  int minBCD = 0; for (int i = 21; i <= 27; i++) if (*(frameBuf + i)) minBCD |= (1 << (i - 21));
  int minute = bcdToDec(minBCD);
  int hourBCD = 0; for (int i = 29; i <= 34; i++) if (*(frameBuf + i)) hourBCD |= (1 << (i - 29));
  int hour = bcdToDec(hourBCD);
  int dayBCD = 0; for (int i = 36; i <= 41; i++) if (*(frameBuf + i)) dayBCD |= (1 << (i - 36));
  int day = bcdToDec(dayBCD);
  int weekday = 0; for (int i = 42; i <= 44; i++) if (*(frameBuf + i)) weekday |= (1 << (i - 42));
  int monthBCD = 0; for (int i = 45; i <= 49; i++) if (*(frameBuf + i)) monthBCD |= (1 << (i - 45));
  int month = bcdToDec(monthBCD);
  int yearBCD = 0; for (int i = 50; i <= 57; i++) if (*(frameBuf + i)) yearBCD |= (1 << (i - 50));
  int year = 2000 + bcdToDec(yearBCD);

  Serial.print("CZAS DCF77: ");
  if (hour < 10) Serial.print("0"); Serial.print(hour); Serial.print(":");
  if (minute < 10) Serial.print("0"); Serial.print(minute);
  Serial.println(isCEST ? " CEST" : " CET");

  if (startOk && parMinOk && parHourOk && parDateOk) {
    for (int i = 1; i <= 14; i++) {
      *(weatherBitsStr + (i - 1)) = (*(frameBuf + i) == 1) ? '1' : '0';
    }
    *(weatherBitsStr + 14) = '\0';
    hasWeatherBits = true;
    backupAntenna = *(frameBuf + 15);
    dstAlert = *(frameBuf + 16);

    curMinute = minute;
    curHour = hour;
    curSecond = 0; 
    curDay = day;
    curWeekday = weekday;
    curMonth = month;
    curYear = year;
    utcOffset = isCEST ? 2 : 1;

    lastGoodSyncMs = millis();
    lastTick = millis() - 20;
    timeSynced = true;

    needOledUpdate = true;
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
  int secW = u8g2.getStrWidth("00");

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
    if (!timeSynced) {
      drawWaitingScreen("ZEGAR LOKALNY");
    } else {
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

      unsigned long minsAgo = (millis() - lastGoodSyncMs) / 65000UL;
      char* bottomStr = (char*)malloc(32);
      if (minsAgo == 0) {
        sprintf(bottomStr, "%s (UTC+%d) | Sync:OK", (utcOffset == 2) ? "CEST" : "CET", utcOffset);
      } else if (minsAgo > 99) {
        sprintf(bottomStr, "%s (UTC+%d) | Sync:XXm", (utcOffset == 2) ? "CEST" : "CET", utcOffset);
      } else {
        sprintf(bottomStr, "%s (UTC+%d) | Sync:%02lum", (utcOffset == 2) ? "CEST" : "CET", utcOffset, minsAgo);
      }
      u8g2.drawStr(0, 62, bottomStr);
      free(bottomStr);
    }

  } else if (displayMode == 1) {
    if (!timeSynced) {
      drawWaitingScreen("CZAS UTC");
    } else {
      u8g2.drawBox(0, 0, 128, 13);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_7x13B_tr);
      int wTop = u8g2.getStrWidth("UTC");
      u8g2.drawStr((128 - wTop) / 2, 11, "UTC");
      u8g2.setDrawColor(1);

      int utcHour = (curHour - utcOffset + 24) % 24;
      drawCenteredClock(utcHour, curMinute, curSecond, 49);

      u8g2.setFont(u8g2_font_5x7_tf);
      char* lokStr = (char*)malloc(28);
      sprintf(lokStr, "Lokalny: %02d:%02d %s", curHour, curMinute, (utcOffset == 2) ? "CEST" : "CET");
      int wBot = u8g2.getStrWidth(lokStr);
      u8g2.drawStr((128 - wBot) / 2, 63, lokStr);
      free(lokStr);
    }

  } else if (displayMode == 2) {
    if (!timeSynced) {
      drawWaitingScreen("ZEGAR DUZY");
    } else {
      drawCenteredClock(curHour, curMinute, curSecond, 46);
    }

  } else if (displayMode == 3) {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "SYGNAL DCF77 (LIVE)");

    int good = 0;
    int cnt = sigCount;
    for (int i = 0; i < cnt; i++) good += *(sigWindow + i);
    int quality = (cnt > 0) ? (good * 100) / cnt : 0;

    char* qStr = (char*)malloc(24);
    sprintf(qStr, "Jakosc: %d%% (%d/%d)", quality, good, cnt);
    u8g2.drawStr(0, 25, qStr);
    free(qStr);

    u8g2.drawFrame(0, 31, 124, 12);
    int barW = (quality * 120) / 100;
    if (barW > 0) u8g2.drawBox(2, 33, barW, 8);

    char* pStr = (char*)malloc(28);
    int pType = lastPulseType;
    unsigned long pMs = lastPulseMs;
    if (pType == 0) sprintf(pStr, "Impuls: BIT 0 (%lums)", pMs);
    else if (pType == 1) sprintf(pStr, "Impuls: BIT 1 (%lums)", pMs);
    else if (pType == 2) sprintf(pStr, "Impuls: SZUM! (%lums)", pMs);
    else sprintf(pStr, "Czekam na impuls...");
    u8g2.drawStr(0, 58, pStr);
    free(pStr);

  } else if (displayMode == 4) {
    u8g2.setFont(u8g2_font_6x10_tf);
    char* topStr = (char*)malloc(28);
    sprintf(topStr, "RAMKA DCF77  %02d/59", (int)bitPos);
    u8g2.drawStr(0, 10, topStr);
    free(topStr);

    if (!hasWeatherBits) {
      u8g2.drawStr(0, 30, "Oczekiwanie na ramke");
      u8g2.drawStr(0, 44, "DCF77 (bity 1-16)...");
    } else {
      int blockInHour = curMinute / 3;
      int regionId = ((curHour % 3) * 20) + blockInHour;
      int part = (curMinute % 3) + 1;

      char* rStr = (char*)malloc(28);
      sprintf(rStr, "Blok: #%02d (cz. %d/3)", regionId, part);
      u8g2.drawStr(0, 25, rStr);
      free(rStr);

      char* bStr = (char*)malloc(28);
      sprintf(bStr, "B1-14: %s", weatherBitsStr);
      u8g2.drawStr(0, 40, bStr);
      free(bStr);

      char* stStr = (char*)malloc(28);
      sprintf(stStr, "Ant:%s | DST:%s", backupAntenna ? "REZ" : "GLW", dstAlert ? "TAK" : "NIE");
      u8g2.drawStr(0, 56, stStr);
      free(stStr);
    }
  }

  u8g2.sendBuffer();
}
