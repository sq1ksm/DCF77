# Zegar DCF77 + S-Meter & Analizator Ramki (v4)

**Autor:** SQ1KSM  
**Platforma:** Seeeduino XIAO SAMD21 (ARM Cortex-M0+ 48 MHz)  
**Wyświetlacz:** OLED 1,3" SH1106 (128×64 px, I2C)  

Zaawansowany zegar wzorcowy synchronizowany drogą radiową z nadajnikiem długofalowym **DCF77 (77,5 kHz)** w Mainflingen pod Frankfurtem nad Menem. Projekt został zoptymalizowany pod kątem pracy ciągłej 24/7 (np. na biurku krótkofalowca) z pełną odpornością na zakłócenia elektromagnetyczne generowane przez przetwornice ekranów OLED oraz powerbanków.

---

## Główne funkcje (v4)

* **Odbiór na przerwaniach sprzętowych (`attachInterrupt`):** Zbocza sygnału DCF77 mierzone są z dokładnością do 1 ms niezależnie od odświeżania wyświetlacza OLED i szybkości kręcenia enkoderem.
* **Potrójna kontrola parzystości (P1, P2, P3) + bit startu (S):** Każda 59-bitowa ramka jest weryfikowana matematycznie przed aktualizacją czasu — zakłócenia nigdy nie przekłamią godziny ani daty.
* **Automatyczna obsługa stref czasowych:** Rozpoznawanie czasu zimowego **CET (UTC+1)** oraz letniego **CEST (UTC+2)** na podstawie bitów 17–18 ramki.
* **Ciągła synchronizacja 24/7 z podtrzymaniem kwarcowym:** Zegar koryguje czas co minutę bez wygaszania ekranu. W przypadku chwilowego zaniku sygnału wewnętrzny zegar płynnie odlicza czas dalej.
* **5 trybów wyświetlania (obsługiwanych enkoderem obrotowym):**
  1. **Zegar Lokalny (Ekran Główny):** Godzina, minuty, sekundy, pełna data z polskim dniem tygodnia, strefa czasowa (`CET/CEST`) oraz status świeżości synchronizacji (`Sync:OK` lub licznik minut od ostatniej udanej ramki `01m..XXm`).
  2. **Czas UTC (ZULU):** Wyraźny pasek tytułowy w inwersji (`UTC`), duży zegar UTC na środku oraz podgląd czasu lokalnego na dole ekranu.
  3. **Duży Zegar Lokalny:** Minimalistyczny widok samych cyfr czasu na całą szerokość ekranu.
  4. **S-Meter DCF77 (Live):** Wskaźnik jakości sygnału w procentach (`0–100%`) wyliczany z przesuwnego okna 20 ostatnich impulsów, pasek graficzny oraz pomiar szerokości ostatniego bitu w milisekundach (`~100 ms` dla `0`, `~200 ms` dla `1`, lub wykrywanie szumu).
  5. **Analizator Ramki DCF77 (`00/59`):** Licznik odebranych bitów w bieżącej minucie na żywo, podgląd surowych bitów `B1–B14` (dawny kanał pogodowy Meteotime), numer bieżącego bloku harmonogramu, status anteny nadawczej (`GLW` – główna / `REZ` – rezerwowa, bit 15) oraz zapowiedź zmiany czasu (`DST`, bit 16).
* **Inteligentny ekran oczekiwania:** Po uruchomieniu zegar wyświetla napis `OCZEKIWANIE NA DCF`, a po wykryciu 60. sekundy płynnie odlicza pozostały czas (`59s -> 00s`) wraz z graficznym paskiem postępu (w tym czasie enkoder i S-Meter są w pełni aktywne).

---

## Lista potrzebnych elementów

1. **Mikrokontroler:** Seeeduino XIAO SAMD21 (lub kompatybilny z rdzeniem Arduino SAMD).
2. **Odbiornik DCF77 (77,5 kHz):** Moduł z anteną ferrytową (np. z demontażu zegara ściennego/stacji pogodowej zasilanej z 2 baterii 3 V lub gotowy moduł 3,3 V).
3. **Wyświetlacz:** OLED 1,3" 128×64 px ze sterownikiem **SH1106** (magistrala I2C, zasilanie 3,3 V).
4. **Enkoder obrotowy:** Zwykły enkoder inkrementalny (np. EC11) do zmiany ekranów.
5. **Przewód połączeniowy do odbiornika:** Kabel 3-żyłowy w ekranie (np. LiYCY 3×0,14 mm² lub przewód mikrofonowy stereo) o długości do 1–2 m (pozwala odsunąć antenę od źródeł zakłóceń).
6. **Elementy filtrujące zakłócenia (kluczowe dla 100% jakości przy włączonym OLED):**
   * Dławiki osiowe / ferrytowe (np. 10 µH – 100 µH) lub rezystory 47–100 Ω.
   * Kondensatory elektrolityczne `47 µF – 100 µF` (na 10 V lub 16 V).
   * Kondensatory ceramiczne `100 nF` (104).
   * Opcjonalnie: zatrzaskowy rdzeń ferrytowy na przewodzie od strony odbiornika DCF77.

---

## 🔌 Schemat połączeń (Pinout Seeeduino XIAO SAMD21)

| Moduł / Element | Pin modułu | Pin Seeeduino XIAO | Uwagi |
| :--- | :--- | :--- | :--- |
| **OLED SH1106** | `VCC` | `3V3` (przez filtr LC) | Zasilanie 3,3 V |
| **OLED SH1106** | `GND` | `GND` | Masa wspólna |
| **OLED SH1106** | `SDA` | `D4` (SDA) | Magistrala I2C |
| **OLED SH1106** | `SCL` | `D5` (SCL) | Magistrala I2C |
| **Odbiornik DCF77** | `VCC` | `3V3` (przez filtr LC) | Zasilanie 3,3 V |
| **Odbiornik DCF77** | `GND` | `GND` | Masa wspólna |
| **Odbiornik DCF77** | `DATA` | `D7` | Wejście przerwania sprzętowego (`CHANGE`) |
| **Ekran kabla DCF** | Oplot | `GND` (tylko przy XIAO!) | **Nie podłączać** po stronie odbiornika (unikamy pętli masy) |
| **Enkoder obrotowy** | `CLK` (A) | `D1` | Podciągnięty wewnętrznie (`INPUT_PULLUP`) |
| **Enkoder obrotowy** | `DT` (B) | `D2` | Podciągnięty wewnętrznie (`INPUT_PULLUP`) |
| **Enkoder obrotowy** | `GND` (C) | `GND` | Środkowy pin enkodera do masy |

---

## Filtrowanie zakłóceń i ustawienie anteny

Przetwornice podbijające napięcie w ekranach OLED (charge pump) oraz w powerbankach generują silne harmoniczne w okolicach pasma 77,5 kHz. Aby uzyskać **100% jakości sygnału przy cały czas włączonym ekranie**:

1. **Filtr LC przy odbiorniku DCF77:** Wepnij dławik szeregowo w linię zasilania `3.3V` tuż przy płytce odbiornika, a za nim (między `3.3V` a `GND`) przylutuj równolegle kondensator elektrolityczny `100 µF` oraz ceramiczny `100 nF`.
2. **Filtr LC przy wyświetlaczu OLED:** Zastosuj analogiczny dławik + kondensatory `100 µF` i `100 nF` bezpośrednio przy pinach zasilania ekranu OLED, aby zatrzymać szumy przetwornicy u źródła.
3. **Ekranowanie jednostronne:** Oplot kabla ekranowanego przylutuj wyłącznie do `GND` na płytce Seeeduino. Przy module DCF77 zaizoluj oplot koszulką termokurczliwą.
4. **Orientacja pręta ferrytowego:** Antena ferrytowa odbiera sygnał najsilniej bokiem pręta. Dla zachodniej Polski (Dębno / Szczecin, azymut na Mainflingen ok. 235°) pręt należy ułożyć w osi **północny zachód – południowy wschód** (na tarczy zegara: linia **11:00 – 5:00**).

---

## Wymagane biblioteki (Arduino IDE)

* **U8g2** autorstwa Olivera Krausa (do obsługi wyświetlacza SH1106 I2C).
* Rdzeń płytki: **Seeed SAMD Boards** (`Seeeduino XIAO`).

---

## 📡 Diagnostyka po porcie szeregowym (USB Serial)

Interfejs pracuje z prędkością **115200 bps** i wypisuje status startowy oraz każdą odebraną pełną minutę:
```text
DCF77 START (Pin D7 - IRQ v4)
CZAS DCF77: 22:14 CET
```

---

##  Licencja

Projekt udostępniony do użytku własnego i edukacyjnego.  
**73 de SQ1KSM!**
