# Zegar DCF77 na Seeeduino XIAO (by SQ1KSM)

Precyzyjny zegar czasu atomowego oparty na mikrokontrolerze **Seeeduino XIAO SAMD21**, odbiorniku fal długich **DCF77 (77,5 kHz)** oraz wyświetlaczu **OLED 1.3" (SH1106)**. Projekt stworzony z myślą o zastosowaniach krótkofalarskich (szybki podgląd czasu lokalnego oraz UTC).

## Główne funkcje
* **Automatyczna synchronizacja DCF77:** Dekodowanie pełnej 59-bitowej ramki czasu, daty, dnia tygodnia oraz automatyczne rozpoznawanie strefy czasowej (CET / CEST).
* **Eliminacja zakłóceń EMI z ekranu OLED:** Przetwornica wyświetlacza jest programowo usypiana (`setPowerSave(1)`) na czas odbioru ramki radiowej, co zapewnia bezbłędną synchronizację za pierwszym podejściem (`R-1`).
* **3 tryby wyświetlania (przełączane enkoderem):**
  1. **Ekran główny:** Duży czas lokalny + małe sekundy, data ze skrótem dnia tygodnia oraz status synchronizacji (`R-X | S za: ...`).
  2. **Tryb UTC (`CZAS UTC`):** Automatycznie przeliczony czas uniwersalny GMT (bez wygaszania ekranu na okresową synchronizację – idealny podczas pracy na paśmie).
  3. **Tryb minimalistyczny:** Sama duża godzina i minuty na środku ekranu (bez daty i okresowego wygaszania).
* **Ochrona matrycy OLED:** Obniżony kontrast (`40/255`) wydłużający żywotność wyświetlacza oraz zmniejszający pobór prądu i szumy elektromagnetyczne.

---

## Schemat połączeń (Pinout)

| Moduł / Element | Pin modułu | Pin Seeeduino XIAO | Uwagi |
| :--- | :---: | :---: | :--- |
| **Wyświetlacz OLED** (SH1106 I2C) | `VCC` | **3V3** | Zasilanie 3.3V |
| | `GND` | **GND** | Masa |
| | `SDA` | **D4** | Sprzętowe I2C (SDA) |
| | `SCL` | **D5** | Sprzętowe I2C (SCL) |
| **Odbiornik DCF77** (77,5 kHz) | `VCC` | **3V3** | Zalecany kondensator 100nF + 47µF przy zasilaniu |
| | `GND` | **GND** | Masa |
| | `DATA` | **D7** | Wyjście sygnału impulsów |
| **Enkoder obrotowy** | `A` (CLK) | **D1** | Wyjście impulsowe A (`INPUT_PULLUP`) |
| | `B` (DT) | **D2** | Wyjście impulsowe B (`INPUT_PULLUP`) |
| | `GND` | **GND** | Wspólna masa enkodera |

*(Wolne piny do dalszej rozbudowy: `D0` – np. pasywny buzzer piezo, `D3` – przycisk PUSH enkodera, `D6` – przycisk BACK).*

---

## Wymagane biblioteki (Arduino IDE)
* `Wire` (wbudowana)
* `U8g2` (autor: *olikraus*)

## Wskazówki montażowe
* Antenę ferrytową odbiornika DCF77 należy umieścić z dala od przetwornicy wyświetlacza OLED oraz zasilaczy impulsowych (najlepiej wyprowadzić moduł na przewodzie ok. 20–50 cm).
* W ustawieniach płytki w Arduino IDE należy wybrać **Seeeduino XIAO** (pakiet *Seeed SAMD Boards*).
