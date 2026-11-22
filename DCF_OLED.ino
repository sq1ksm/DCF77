#include <Wire.h>
#include <U8g2lib.h>
#include <string.h>

#define DCF_PIN 7          // Pin wejściowy sygnału DCF77 (przerwanie sprzętowe).
#define ENC_A 1            // Pin A enkodera obrotowego (zmiana widoku).
#define ENC_B 2            // Pin B enkodera (kierunek obrotu).

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

volatile byte* bitsBuf;               // Bufor bitów bieżącej ramki DCF77 (do 100 bitów).
volatile byte* frameBuf;              // Kopia ostatniej kompletnej ramki (59 bitów) do dekodowania.
volatile int bitPos = 0;              // Aktualna pozycja bitu w ramce (0-59).
volatile bool frameReady = false;     // Flaga: kompletna ramka gotowa do dekodowania.
volatile bool needOledUpdate = false; // Flaga: wymagane odświeżenie OLED.
volatile bool frameSynced = false;    // Flaga: wykryto znacznik końca minuty (59. sekunda) – początek ramki zsynchronizowany.

int curHour = 0, curMinute = 0, curSecond = 0;                // Aktualny czas lokalny.
int curDay = 1, curMonth = 1, curYear = 2026, curWeekday = 1; // Aktualna data.
int utcOffset = 1;                    // Offset UTC (1 = CET czas zimowy, 2 = CEST czas letni).

bool timeSynced = false;              // Flaga: czas zsynchronizowany z wzorcem DCF77.
unsigned long lastTick = 0;           // Znacznik czasu ostatniego tyknięcia zegara (ms).
unsigned long lastGoodSyncMs = 0;     // Czas ostatniej udanej synchronizacji (ms).

int displayMode = 0;                  // Aktualny tryb wyświetlania (0-4).
int lastEncA = HIGH;                  // Poprzedni stan pinu A enkodera.

volatile byte* sigWindow;             // Okno 20 ostatnich impulsów do oceny jakości sygnału.
volatile int sigIdx = 0;              // Indeks zapisu w buforze kołowym jakości sygnału.
volatile int sigCount = 0;            // Liczba zebranych próbek w oknie (maks. 20).
volatile unsigned long lastPulseMs = 0; // Czas trwania ostatniego impulsu w ms.
volatile int lastPulseType = -1;      // Typ ostatniego impulsu (-1=brak, 0=bit0, 1=bit1, 2=szum).

char* weatherBitsStr;                 // Łańcuch tekstowy bitów B1-B14 z ramki DCF77.
char* txtBuf;                         // Wspólny bufor formatowania tekstu na OLED (zapobiega fragmentacji sterty).
bool hasWeatherBits = false;          // Flaga: odebrano pierwszą poprawną ramkę z bitami B1-B16.
int backupAntenna = 0;                // Bit 15: 0 = antena główna (GLW), 1 = antena rezerwowa (REZ).
int dstAlert = 0;                     // Bit 16: 1 = zapowiedź zmiany czasu za najbliższą godzinę.

const char* getShortDay(int w) {      // Zwraca polski skrót nazwy dnia tygodnia (1=Pon .. 7=Ndz).
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

int getDaysInMonth(int m, int y) {    // Zwraca liczbę dni w miesiącu (z uwzględnieniem lat przestępnych).
  if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28;
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

void dcfISR() {                       // Przerwanie sprzętowe na zmianę stanu pinu DCF77.
  static int last = LOW;              // Poprzedni stan logiczny pinu.
  static unsigned long t = 0;         // Znacznik czasu poprzedniej zmiany stanu (ms).
  int now = digitalRead(DCF_PIN);     // Odczyt aktualnego stanu pinu.
  unsigned long nowMs = millis();     // Aktualny czas systemowy w ms.

  if (now != last) {                  // Wykryto zbocze (zmianę stanu).
    unsigned long len = nowMs - t;    // Czas trwania poprzedniego stanu (HIGH lub LOW) w ms.

    if (last == HIGH) {               // Zakończył się impuls HIGH – klasyfikacja szerokości bitu.
      lastPulseMs = len;
      if (len >= 70 && len <= 130) {         // Impuls ~100 ms = bit 0.
        if (bitPos < 100) {
          *(bitsBuf + bitPos) = 0;
          bitPos = bitPos + 1;
        }
        *(sigWindow + sigIdx) = 1;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount = sigCount + 1;
        lastPulseType = 0;
      } else if (len >= 140 && len <= 250) { // Impuls ~200 ms = bit 1.
        if (bitPos < 100) {
          *(bitsBuf + bitPos) = 1;
          bitPos = bitPos + 1;
        }
        *(sigWindow + sigIdx) = 1;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount = sigCount + 1;
        lastPulseType = 1;
      } else if (len >= 15) {                // Impuls poza widełkami (15-69 ms lub >250 ms) = zakłócenie.
        *(sigWindow + sigIdx) = 0;
        sigIdx = (sigIdx + 1) % 20;
        if (sigCount < 20) sigCount = sigCount + 1;
        lastPulseType = 2;
      }
      if (displayMode == 3 || displayMode == 4 || (!timeSynced && frameSynced)) needOledUpdate = true;
    }

    if (last == LOW && len > 1700 && len < 2200) { // Brak impulsu przez ~1,8-2,0 s = 60. sekunda (koniec ramki).
      if (bitPos == 59 && !frameReady) {     // Zebrano dokładnie 59 bitów – skopiuj ramkę do dekodowania.
        for (int i = 0; i < 59; i++) {
          *(frameBuf + i) = *(bitsBuf + i);
        }
        frameReady = true;            // Zgłoś gotowość ramki dla pętli głównej.
      }
      bitPos = 0;                     // Wyzeruj licznik bitów na nową minutę.
      frameSynced = true;             // Oznacz wykrycie początku ramki.
      if (displayMode == 4 || !timeSynced) needOledUpdate = true;
    }
    t = nowMs;                        // Zapisz czas aktualnego zbocza.
    last = now;                       // Zapisz aktualny stan pinu.
  }
}

void setup() {                        // Inicjalizacja sprzętu i zmiennych po starcie.
  Serial.begin(115200);               // Uruchomienie diagnostyki USB Serial (115200 bps).
  delay(1000);
  Serial.println("DCF77 START (Pin D7 - IRQ v5)");

  bitsBuf = (volatile byte*)malloc(100);
  frameBuf = (volatile byte*)malloc(64);
  sigWindow = (volatile byte*)malloc(20);
  weatherBitsStr = (char*)malloc(20);
  txtBuf = (char*)malloc(40);

  if (!bitsBuf || !frameBuf || !sigWindow || !weatherBitsStr || !txtBuf) {
    Serial.println("BLAD: Brak pamieci RAM (malloc)!");
    while (1) delay(1000);
  }

  strcpy(weatherBitsStr, "--------------");
  for (int i = 0; i < 20; i++) *(sigWindow + i) = 0;

  pinMode(DCF_PIN, INPUT);            // Konfiguracja wejścia odbiornika DCF77.
  pinMode(ENC_A, INPUT_PULLUP);       // Konfiguracja enkodera z wewnętrznym podciąganiem.
  pinMode(ENC_B, INPUT_PULLUP);
  lastEncA = digitalRead(ENC_A);

  u8g2.begin();                       // Inicjalizacja wyświetlacza OLED SH1106.
  u8g2.setContrast(30);               // Ustawienie umiarkowanego kontrastu (niższy pobór prądu).

  u8g2.clearBuffer();                 // Rysowanie ekranu powitalnego w ramce 128x64 px.
  u8g2.drawFrame(0, 0, 128, 64);

  u8g2.setFont(u8g2_font_logisoso24_tf);
  int wTitle = u8g2.getStrWidth("DCF77");
  u8g2.drawStr((128 - wTitle) / 2, 33, "DCF77");

  u8g2.setFont(u8g2_font_6x10_tf);
  int wAuthor = u8g2.getStrWidth("by SQ1KSM v5");
  u8g2.drawStr((128 - wAuthor) / 2, 52, "by SQ1KSM v5");

  u8g2.sendBuffer();
  delay(3000);

  bitPos = 0;
  attachInterrupt(digitalPinToInterrupt(DCF_PIN), dcfISR, CHANGE); // Aktywacja przerwania na pinie DCF77.
  updateOLED();                       // Wyświetlenie pierwszego ekranu.
}

void loop() {                         // Główna pętla programu.
  checkEncoder();                     // Sprawdzenie obrotu enkodera.

  if (frameReady) {                   // Dekodowanie odebranej 59-bitowej ramki.
    decodeDCF77();
    frameReady = false;
  }

  if (timeSynced && (millis() - lastTick >= 1000)) { // Programowy zegar sekundowy (podtrzymanie).
    lastTick += 1000;
    tickClock();
    needOledUpdate = true;
  }

  if (needOledUpdate) {               // Odświeżenie wyświetlacza OLED na żądanie.
    needOledUpdate = false;
    updateOLED();
  }
}

void checkEncoder() {                 // Obsługa enkodera obrotowego (przełączanie między 5 ekranami).
  static unsigned long lastEncTime = 0;
  int currentA = digitalRead(ENC_A);

  if (lastEncA == HIGH && currentA == LOW && (millis() - lastEncTime > 50)) { // Debouncing 50 ms.
    lastEncTime = millis();
    if (digitalRead(ENC_B) == HIGH) { // Obrót w prawo (następny ekran).
      displayMode = (displayMode + 1) % 5;
    } else {                          // Obrót w lewo (poprzedni ekran).
      displayMode = (displayMode + 4) % 5;
    }
    needOledUpdate = true;
  }
  lastEncA = currentA;
}

void drawWaitingScreen() {            // Rysuje ekran oczekiwania na pierwszą synchronizację.
  u8g2.setFont(u8g2_font_7x14B_tr);
  int w1 = u8g2.getStrWidth("OCZEKIWANIE NA");
  u8g2.drawStr((128 - w1) / 2, 18, "OCZEKIWANIE NA");

  if (!frameSynced) {                 // Przed wykryciem 60. sekundy.
    int w2 = u8g2.getStrWidth("DCF");
    u8g2.drawStr((128 - w2) / 2, 38, "DCF");
  } else {                            // Po wykryciu początku minuty – odliczanie 59s -> 00s i pasek postępu.
    int rem = 59 - (int)bitPos;
    if (rem < 0) rem = 0;

    sprintf(txtBuf, "DCF - %02ds", rem);
    int w2 = u8g2.getStrWidth(txtBuf);
    u8g2.drawStr((128 - w2) / 2, 38, txtBuf);

    u8g2.drawFrame(2, 47, 124, 12);   // Ramka paska postępu.
    int barW = ((int)bitPos * 120) / 59;
    if (barW > 120) barW = 120;
    if (barW > 0) u8g2.drawBox(4, 49, barW, 8); // Wypełnienie paska postępu.
  }
}
int bcdToDec(int bcd) {               // Konwersja liczby z kodu BCD na dziesiętny.
  return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void decodeDCF77() {                  // Weryfikacja parzystości i dekodowanie danych z 59-bitowej ramki.
  bool startOk = (*(frameBuf + 20) == 1); // Bit 20 musi zawsze wynosić 1 (start czasu).

  int sumMin = 0; for (int i = 21; i <= 28; i++) sumMin += *(frameBuf + i);
  bool parMinOk = (sumMin % 2 == 0);  // Kontrola parzystości minut (P1, bity 21-28).

  int sumHour = 0; for (int i = 29; i <= 35; i++) sumHour += *(frameBuf + i);
  bool parHourOk = (sumHour % 2 == 0); // Kontrola parzystości godzin (P2, bity 29-35).

  int sumDate = 0; for (int i = 36; i <= 58; i++) sumDate += *(frameBuf + i);
  bool parDateOk = (sumDate % 2 == 0); // Kontrola parzystości daty (P3, bity 36-58).

  bool isCEST = (*(frameBuf + 17) == 1 && *(frameBuf + 18) == 0); // Bity 17-18: strefa CEST / CET.

  int minBCD = 0; for (int i = 21; i <= 27; i++) if (*(frameBuf + i)) minBCD |= (1 << (i - 21));
  int minute = bcdToDec(minBCD);      // Dekodowanie minut (bity 21-27).

  int hourBCD = 0; for (int i = 29; i <= 34; i++) if (*(frameBuf + i)) hourBCD |= (1 << (i - 29));
  int hour = bcdToDec(hourBCD);       // Dekodowanie godzin (bity 29-34).

  int dayBCD = 0; for (int i = 36; i <= 41; i++) if (*(frameBuf + i)) dayBCD |= (1 << (i - 36));
  int day = bcdToDec(dayBCD);         // Dekodowanie dnia miesiąca (bity 36-41).

  int weekday = 0; for (int i = 42; i <= 44; i++) if (*(frameBuf + i)) weekday |= (1 << (i - 42));
                                      // Dekodowanie dnia tygodnia (bity 42-44, 1=Pon .. 7=Ndz).

  int monthBCD = 0; for (int i = 45; i <= 49; i++) if (*(frameBuf + i)) monthBCD |= (1 << (i - 45));
  int month = bcdToDec(monthBCD);     // Dekodowanie miesiąca (bity 45-49).

  int yearBCD = 0; for (int i = 50; i <= 57; i++) if (*(frameBuf + i)) yearBCD |= (1 << (i - 50));
  int year = 2000 + bcdToDec(yearBCD); // Dekodowanie roku (bity 50-57).

  Serial.print("CZAS DCF77: ");       // Wypisanie odebranego czasu na port szeregowy.
  if (hour < 10) Serial.print("0"); Serial.print(hour); Serial.print(":");
  if (minute < 10) Serial.print("0"); Serial.print(minute);
  Serial.println(isCEST ? " CEST" : " CET");

  if (startOk && parMinOk && parHourOk && parDateOk) { // Aktualizacja zegara tylko przy 100% zgodności sum kontrolnych.
    for (int i = 1; i <= 14; i++) {   // Zapis surowych bitów B1-B14 (kanał Meteotime).
      *(weatherBitsStr + (i - 1)) = (*(frameBuf + i) == 1) ? '1' : '0';
    }
    *(weatherBitsStr + 14) = '\0';
    hasWeatherBits = true;
    backupAntenna = *(frameBuf + 15); // Status anteny nadawczej (GLW / REZ).
    dstAlert = *(frameBuf + 16);      // Flaga ostrzeżenia o zmianie czasu (DST).

    curMinute = minute;
    curHour = hour;
    curSecond = 0;                    // Wyzerowanie sekund na początku nowej minuty.
    curDay = day;
    curWeekday = weekday;
    curMonth = month;
    curYear = year;
    utcOffset = isCEST ? 2 : 1;

    lastGoodSyncMs = millis();        // Zapisanie czasu udanej synchronizacji.
    lastTick = millis() - 20;         // Korekta opóźnienia detekcji końca ramki (-20 ms).
    timeSynced = true;

    needOledUpdate = true;
  }
}

void tickClock() {                    // Zwiększa programowy zegar o 1 sekundę.
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

void advanceDate() {                  // Przesuwa kalendarz o 1 dzień po północy.
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

void drawCenteredClock(int h, int m, int s, int yPos) { // Rysuje wyśrodkowany zegar ze stałą szerokością sekund.
  sprintf(txtBuf, "%02d:%02d", h, m);
  u8g2.setFont(u8g2_font_logisoso32_tn);
  int hmW = u8g2.getStrWidth(txtBuf);

  u8g2.setFont(u8g2_font_logisoso16_tn);
  int secW = u8g2.getStrWidth("00");  // Stała szerokość wzorca zapobiega drganiu cyfr.

  int startX = (128 - (hmW + 4 + secW)) / 2;

  u8g2.setFont(u8g2_font_logisoso32_tn);
  u8g2.drawStr(startX, yPos, txtBuf);

  sprintf(txtBuf, "%02d", s);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(startX + hmW + 4, yPos, txtBuf);
}

void updateOLED() {                   // Rysuje aktualnie wybrany ekran na wyświetlaczu OLED.
  u8g2.clearBuffer();

  if (displayMode == 0) {             // EKRAN 0: Główny zegar lokalny, data i status synchronizacji.
    if (!timeSynced) {
      drawWaitingScreen();
    } else {
      u8g2.setFont(u8g2_font_logisoso32_tn);
      sprintf(txtBuf, "%02d:%02d", curHour, curMinute);
      u8g2.drawStr(0, 33, txtBuf);
      int hmWidth = u8g2.getStrWidth(txtBuf);

      u8g2.setFont(u8g2_font_logisoso16_tn);
      sprintf(txtBuf, "%02d", curSecond);
      u8g2.drawStr(hmWidth + 4, 33, txtBuf);

      u8g2.setFont(u8g2_font_6x10_tf);
      sprintf(txtBuf, "%02d.%02d.%04d %s", curDay, curMonth, curYear, getShortDay(curWeekday));
      u8g2.drawStr(0, 49, txtBuf);

      unsigned long minsAgo = (millis() - lastGoodSyncMs) / 65000UL; // Zapas 5s na przejście minuty.
      if (minsAgo == 0) {
        sprintf(txtBuf, "%s (UTC+%d) | Sync:OK", (utcOffset == 2) ? "CEST" : "CET", utcOffset);
      } else if (minsAgo > 99) {
        sprintf(txtBuf, "%s (UTC+%d) | Sync:XXm", (utcOffset == 2) ? "CEST" : "CET", utcOffset);
      } else {
        sprintf(txtBuf, "%s (UTC+%d) | Sync:%02lum", (utcOffset == 2) ? "CEST" : "CET", utcOffset, minsAgo);
      }
      u8g2.drawStr(0, 62, txtBuf);
    }

  } else if (displayMode == 1) {      // EKRAN 1: Czas UTC (ZULU) z belką w inwersji i czasem lokalnym na dole.
    if (!timeSynced) {
      drawWaitingScreen();
    } else {
      u8g2.drawBox(0, 0, 128, 13);    // Biały prostokąt tytułowy.
      u8g2.setDrawColor(0);           // Czarny tekst na białym tle.
      u8g2.setFont(u8g2_font_7x13B_tr);
      int wTop = u8g2.getStrWidth("UTC");
      u8g2.drawStr((128 - wTop) / 2, 11, "UTC");
      u8g2.setDrawColor(1);           // Powrót do normalnego trybu rysowania.

      int utcHour = (curHour - utcOffset + 24) % 24;
      drawCenteredClock(utcHour, curMinute, curSecond, 49);

      u8g2.setFont(u8g2_font_5x7_tf);
      sprintf(txtBuf, "Lokalny: %02d:%02d %s", curHour, curMinute, (utcOffset == 2) ? "CEST" : "CET");
      int wBot = u8g2.getStrWidth(txtBuf);
      u8g2.drawStr((128 - wBot) / 2, 63, txtBuf);
    }

  } else if (displayMode == 2) {      // EKRAN 2: Duży zegar lokalny na cały ekran.
    if (!timeSynced) {
      drawWaitingScreen();
    } else {
      drawCenteredClock(curHour, curMinute, curSecond, 46);
    }

  } else if (displayMode == 3) {      // EKRAN 3: S-Meter DCF77 (jakość sygnału z 20 impulsów na żywo).
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 10, "SYGNAL DCF77 (LIVE)");

    int good = 0;
    int cnt = sigCount;
    for (int i = 0; i < cnt; i++) good += *(sigWindow + i);
    int quality = (cnt > 0) ? (good * 100) / cnt : 0;

    sprintf(txtBuf, "Jakosc: %d%% (%d/%d)", quality, good, cnt);
    u8g2.drawStr(0, 25, txtBuf);

    u8g2.drawFrame(0, 31, 124, 12);   // Ramka paska sygnału.
    int barW = (quality * 120) / 100;
    if (barW > 0) u8g2.drawBox(2, 33, barW, 8);

    int pType = lastPulseType;
    unsigned long pMs = lastPulseMs;
    if (pType == 0) sprintf(txtBuf, "Impuls: BIT 0 (%lums)", pMs);
    else if (pType == 1) sprintf(txtBuf, "Impuls: BIT 1 (%lums)", pMs);
    else if (pType == 2) sprintf(txtBuf, "Impuls: SZUM! (%lums)", pMs);
    else sprintf(txtBuf, "Czekam na impuls...");
    u8g2.drawStr(0, 58, txtBuf);

  } else if (displayMode == 4) {      // EKRAN 4: Analizator ramki DCF77 (licznik bitów, B1-14, Antena, DST).
    u8g2.setFont(u8g2_font_6x10_tf);
    sprintf(txtBuf, "RAMKA DCF77  %02d/59", (int)bitPos);
    u8g2.drawStr(0, 10, txtBuf);

    if (!hasWeatherBits) {
      u8g2.drawStr(0, 30, "Oczekiwanie na ramke");
      u8g2.drawStr(0, 44, "DCF77 (bity 1-16)...");
    } else {
      int blockInHour = curMinute / 3;
      int regionId = ((curHour % 3) * 20) + blockInHour;
      int part = (curMinute % 3) + 1;

      sprintf(txtBuf, "Blok: #%02d (cz. %d/3)", regionId, part);
      u8g2.drawStr(0, 25, txtBuf);

      sprintf(txtBuf, "B1-14: %s", weatherBitsStr);
      u8g2.drawStr(0, 40, txtBuf);

      sprintf(txtBuf, "Ant:%s | DST:%s", backupAntenna ? "REZ" : "GLW", dstAlert ? "TAK" : "NIE");
      u8g2.drawStr(0, 56, txtBuf);
    }
  }

  u8g2.sendBuffer();
}
