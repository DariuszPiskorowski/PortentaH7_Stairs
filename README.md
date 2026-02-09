
# Portenta H7 - Smart Stairs Lighting Controller

Inteligentny system sterowania oświetleniem schodów z użyciem listwy LED WS2812 i czujników temperatury. 
Projekt raczej testowy niz produkcyjny ze wzgledu na cene portentyH7 oraz cene Carriera. Ekonomicznie nieuzasadnione!


## 📋 Specyfikacja

### Sprzęt
- **Mikrokontroler**: Arduino Portenta H7
- **Listwa LED**: WS2812 (240 LEDów na 13 stopni)
- **Wyświetlacz**: LCD 1602 I2C (16x2 znaków, adres 0x27)
- **Czujniki temperatury**: 
  - DHT11/DHT22 (temperatura + wilgotność, auto-detekcja)
  - DS18B20 (temperatura wodoodporna)
- **Zasilanie**: 5V dla listwy WS2812

### Sieć
- **WiFi**: ***** lub ***** (automatyczny wybór silniejszej sieci)
- **Hasło WiFi**: `********************`
- **MQTT Broker**: `*****************`

### Topiki MQTT

#### Subskrypcja (odbieranie):
- `Stairs/Light/input` - komendy i konfiguracja

#### Publikacja (wysyłanie):
- `Stairs/Light/output` - status systemu i informacje WiFi
- `Stairs/Light/status` - czytelny stan LEDów (On/Off/Night Mode)
- `Stairs/DHT/Temperature` - temperatura z DHT11/DHT22
- `Stairs/DHT/Humidity` - wilgotność z DHT11/DHT22
- `Stairs/Temperature/DS18B20` - temperatura z DS18B20
- `Stairs/Time` - aktualny czas Dublin (UTC+0/+1 DST)

## 🔌 Podłączenie

### Portenta H7 - Piny

| Komponent | Pin | Opis |
|-----------|-----|------|
| WS2812 Data | D6 | Sygnał danych do listwy LED |
| DHT11/DHT22 Data | D5 | Sygnał z czujnika DHT (auto-detekcja) |
| DS18B20 Data | D1 | Sygnał z czujnika DS18B20 (OneWire) |
| LCD 1602 I2C | D11 (SDA), D12 (SCL) | Wyświetlacz LCD przez I2C (adres 0x27) |

### Schemat połączeń

#### WS2812 LED Strip
```
Portenta D6 ──→ WS2812 DIN
5V         ──→ WS2812 5V
GND        ──→ WS2812 GND
```
⚠️ **Uwaga**: Listwa WS2812 z 240 LEDami może pobierać do 14.4A (przy pełnej jasności, białym kolorze). Użyj odpowiedniego zasilacza 5V/15A.

#### DHT11/DHT22 (auto-detekcja)
```
Portenta D5  ──→ DHT Data
3.3V         ──→ DHT VCC
GND          ──→ DHT GND
Rezystor 10kΩ między VCC a Data (pull-up)
```

#### LCD 1602 I2C
```
Portenta D11 (SDA) ──→ LCD SDA
Portenta D12 (SCL) ──→ LCD SCL
3.3V/5V            ──→ LCD VCC (sprawdź specyfikację modułu)
GND                ──→ LCD GND
```
⚠️ **Uwaga**: Moduł LCD I2C na adresie 0x27. System działa bez LCD jeśli nie jest podłączony.

#### DS18B20
```
Portenta D1  ──→ DS18B20 Data (żółty)
3.3V         ──→ DS18B20 VCC (czerwony)
GND          ──→ DS18B20 GND (czarny)
Rezystor 4.7kΩ między VCC a Data (pull-up)
```

## 💡 Tryby Oświetlenia

System automatycznie przełącza się między trybami w zależności od czasu:

### 1. Tryb Aqua 50%
- **Kiedy**: Podczas nocy (day=0), poza godzinami 21:00-5:30
- **Efekt**: Wszystkie 240 LEDów świecą kolorem Aqua (cyjan)
- **Kolor RGB**: (0, 128, 128)

### 2. Tryb Nocny 10%
- **Kiedy**: Między 21:00 a 5:30 (podczas nocy, gdy day=0)
- **Efekt**: Tylko 13 punktów (środek każdego stopnia) świeci kolorem Aqua (cyjan)
- **Kolor RGB**: (0, 128, 128)
- **Rozmieszczenie**: 1 LED na stopień, równomiernie rozłożone po 240 LEDach

### 3. Tryb Wyłączony
- **Kiedy**: W dzień (day=1) lub ręcznie
- **Efekt**: Wszystkie LEDy wyłączone

### Logika przełączania
System sterowany jest przez zmienną `day` (dzień/noc):
- **day=1 (dzień)** → Wszystkie LEDy wyłączone
- **day=0 (noc)** → Sprawdzana jest godzina z NTP:
  - **21:00-5:30** → Tryb nocny (13 punktów)
  - **Poza 21:00-5:30** → Tryb Aqua 50% (wszystkie LEDy)

Godziny trybu nocnego można zmieniać przez MQTT (domyślnie 21:00-5:30).

## 📡 Komendy MQTT


### Sprawdzenie statusu
```
status
```

## 📚 Wymagane Biblioteki

Zainstaluj następujące biblioteki przez Arduino IDE (Sketch → Include Library → Manage Libraries):

1. **Adafruit NeoPixel** (by Adafruit)
   ```
   Wersja: 1.11.0 lub nowsza
   ```

2. **DHT sensor library** (by Adafruit)
   ```
   Wersja: 1.4.4 lub nowsza
   + wymaga: Adafruit Unified Sensor
   ```

3. **DallasTemperature** (by Miles Burton)
   ```
   Wersja: 3.9.0 lub nowsza
   + wymaga: OneWire (by Paul Stoffregen)
   ```

4. **PubSubClient** (by Nick O'Leary)
   ```
   Wersja: 2.8.0 lub nowsza
   ```

5. **NTPClient** (by Fabrice Weinberg)
   ```
   Wersja: 3.2.1 lub nowsza
   ```

6. **LiquidCrystal I2C** (by Frank de Brabander)
   ```
   Wersja: 1.1.2 lub nowsza
   ```

7. **WiFi** (wbudowana w Portenta H7)

### Instalacja przez Arduino IDE:
1. Otwórz Arduino IDE
2. Przejdź do **Tools → Board** i wybierz **Arduino Portenta H7 (M7 core)**
3. Przejdź do **Sketch → Include Library → Manage Libraries**
4. Wyszukaj i zainstaluj każdą z powyższych bibliotek

## 🚀 Instalacja i Uruchomienie

1. **Zainstaluj Arduino IDE** (wersja 2.0 lub nowsza)

2. **Dodaj wsparcie dla Portenta H7**:
   - File → Preferences → Additional Board Manager URLs:
   ```
   https://downloads.arduino.cc/packages/package_index.json
   ```
   - Tools → Board → Board Manager
   - Wyszukaj "Arduino Mbed OS Portenta Boards" i zainstaluj

3. **Zainstaluj wszystkie wymagane biblioteki** (patrz sekcja powyżej)

4. **Podłącz komponenty** zgodnie ze schematem

5. **Otwórz projekt**:
   - File → Open → `PortentaH7_Stairs.ino`

6. **Skonfiguruj Board**:
   - Tools → Board → Arduino Mbed OS Portenta Boards → Arduino Portenta H7 (M7 core)
   - Tools → Port → wybierz port COM Portenty

7. **Wgraj kod**:
   - Sketch → Upload (lub Ctrl+U)

8. **Monitoruj działanie**:
   - Tools → Serial Monitor (115200 baud)

## 🔧 Konfiguracja


## 🐛 Rozwiązywanie Problemów

### LEDy nie świecą
- Sprawdź zasilanie listwy (wymagane osobne 5V/15A)
- Sprawdź połączenie danych (pin D6)
- Upewnij się, że GND Portenty i zasilacza 5V są wspólne

### Brak połączenia WiFi
- Sprawdź czy obie sieci (****2, eir****) są dostępne
- Sprawdź hasło
- Zobacz logi w Serial Monitor

### Brak połączenia MQTT
- Sprawdź czy broker MQTT działa na 192.168.***.***
- Sprawdź czy Portenta ma połączenie z siecią
- Sprawdź firewall

### Czujniki nie działają
- System będzie działał dalej nawet bez czujników
- Sprawdź połączenia i rezystory pull-up
- Zobacz komunikaty w Serial Monitor

### Czas się nie zgadza
- System używa NTP do synchronizacji (UTC+1)
- Upewnij się że Portenta ma dostęp do internetu
- Możesz zmienić strefę czasową w linii:
  ```cpp
  NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // 3600 = UTC+1
  ```

## 📊 Monitorowanie

System publikuje dane przez MQTT:

### Czujniki (co 30 sekund):
- Temperatura DHT11/DHT22 → `Stairs/DHT/Temperature`
- Wilgotność DHT11/DHT22 (×1.5) → `Stairs/DHT/Humidity`
- Temperatura DS18B20 → `Stairs/Temperature/DS18B20`

### Inne dane (co 30 sekund):
- Status WiFi → `Stairs/Light/output`
- Czas Dublin (UTC+0/+1) → `Stairs/Time`
- Stan LEDów → `Stairs/Light/status`

### Status (na żądanie):
Wyślij `status` na `Stairs/Light/input` aby otrzymać pełny status.

### Przykład integracji z Home Assistant:

```yaml
mqtt:
  sensor:
    - name: "Schody - Temperatura DHT"
      state_topic: "Stairs/DHT/Temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      
    - name: "Schody - Wilgotność"
      state_topic: "Stairs/DHT/Humidity"
      unit_of_measurement: "%"
      device_class: humidity
      
    - name: "Schody - Temperatura DS18B20"
      state_topic: "Stairs/Temperature/DS18B20"
      unit_of_measurement: "°C"
      device_class: temperature
      
    - name: "Schody - Status"
      state_topic: "Stairs/Light/output"
      
    - name: "Schody - Stan Świateł"
      state_topic: "Stairs/Light/status"
      
    - name: "Schody - Czas"
      state_topic: "Stairs/Time"
```

## 📝 Notatki

- System automatycznie wybiera silniejszą sieć WiFi
- Czujniki są opcjonalne - system działa bez nich
- LCD jest opcjonalny - system działa bez niego (only for visual inspection)
- Auto-detekcja typu czujnika DHT (DHT11 lub DHT22)
- Re-detekcja czujników po 3 nieudanych odczytach
- Sprawdzanie trybu co 10 sekund
- Odczyt czujników co 30 sekund
- Aktualizacja NTP co 15 sekund
- Aktualizacja LCD co 15 sekund
- Obliczanie DST dla strefy Dublin (UTC+0 zimą, UTC+1 latem)
- Wszystkie czasy w formacie 24-godzinnym
- Korekcja wilgotności DHT: wartość × 1.5 ustalona doswiadczalnie porownujac z czujnikami Tuya
- LCD wyświetla: czas Dublin (linia 1) i adres IP (linia 2)

## 📄 Licencja

Ten projekt jest dostępny na licencji MIT.

## 👤 Autor

Projekt stworzony dla systemu inteligentnego domu.

---

**Wersja**: 1.0  
**Data**: Styczeń 2026  
**Status**: Produkcja
>>>>>>> b9357bb (Initial commit: Portenta H7 Stairs Controller with WiFi, MQTT, WS2812, DHT22, DS18B20)

## 🔌 Mapowanie Pinów Portenta H7

### Piny używane w projekcie

| Pin Arduino | Funkcja w projekcie | Opis |
|-------------|---------------------|------|
| **D6** | WS2812 Data | Sygnał danych do listwy LED |
| **D5** | DHT11/DHT22 Data | Czujnik temperatury i wilgotności |
| **D1** | DS18B20 Data | Czujnik temperatury (OneWire) |
| **D11** | I2C SDA | Magistrala I2C (LCD, czujniki I2C) |
| **D12** | I2C SCL | Magistrala I2C (LCD, czujniki I2C) |

### Pełne mapowanie pinów Portenta H7 → Carrier Hat (złącze 40-pin)

| Pin Arduino | Nr pinu | Funkcja | Złącze | Użycie w projekcie |
|-------------|---------|---------|--------|--------------------|
| D0 | 37 | PWM6 | J2-62 | - |
| **D1** | 36 | PWM5 | J2-60 | ✅ **DS18B20** |
| D2 | 33 | PWM4 | J2-67 | - |
| D3 | 31 | PWM3 | J2-65 | - |
| D4 | 26 | PWM2 | J2-63 | - |
| **D5** | 22 | PWM1 | J2-61 | ✅ **DHT11/DHT22** |
| **D6** | 7 | PWM0 | J2-59 | ✅ **WS2812** |
| D7 | 24 | SPI1 CE | J2-36 | - |
| D8 | 19 | SPI1 MOSI | J2-42 | - |
| D9 | 23 | SPI1 SCK | J2-38 | - |
| D10 | 21 | SPI1 MISO | J2-40 | - |
| **D11** | 27 | I2C SDA | J1-44 | ✅ **LCD I2C** |
| **D12** | 28 | I2C SCL | J1-46 | ✅ **LCD I2C** |
| D13 | 29 | RX1 | J1-35 | - |
| D14 | 32 | TX1 | J1-33 | - |

### Uwagi dotyczące pinów

- Piny D11 i D12 są domyślną magistralą I2C (Wire) w Portenta H7
- Wszystkie piny GPIO są 3.3V logic (nie 5V tolerancyjne!)
- WS2812 wymaga osobnego zasilania 5V (nie z Portenty)
- Rezystory pull-up dla DHT (10kΩ) i DS18B20 (4.7kΩ) są wymagane
- LCD I2C wymaga poprawnego adresu (domyślnie 0x27)

