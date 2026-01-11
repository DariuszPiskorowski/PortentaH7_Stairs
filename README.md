<<<<<<< HEAD
# PortentaH7_Stairs
=======
# Portenta H7 - Smart Stairs Lighting Controller

Inteligentny system sterowania oświetleniem schodów z użyciem listwy LED WS2812 i czujników temperatury.

## 📋 Specyfikacja

### Sprzęt
- **Mikrokontroler**: Arduino Portenta H7
- **Listwa LED**: WS2812 (240 LEDów na 14 stopni)
- **Czujniki temperatury**: 
  - DHT22 (temperatura + wilgotność)
  - DS18B20 (temperatura wodoodporna)
- **Zasilanie**: 5V dla listwy WS2812

### Sieć
- **WiFi**: JPP2 lub eir-JPP (automatyczny wybór silniejszej sieci)
- **Hasło WiFi**: `faf804a3d766`
- **MQTT Broker**: `192.168.1.109:1883`

### Topiki MQTT

#### Subskrypcja (odbieranie):
- `Stairs/Light/input` - komendy i konfiguracja

#### Publikacja (wysyłanie):
- `Stairs/Light/output` - status systemu
- `HallDown/Dht/Temperature` - temperatura z DHT22
- `HallDown/Dht/Humidity` - wilgotność z DHT22
- `HallDown/DS18B20/Temperature` - temperatura z DS18B20

## 🔌 Podłączenie

### Portenta H7 - Piny

| Komponent | Pin | Opis |
|-----------|-----|------|
| WS2812 Data | D6 | Sygnał danych do listwy LED |
| DHT22 Data | D7 | Sygnał z czujnika DHT22 |
| DS18B20 Data | D8 | Sygnał z czujnika DS18B20 (OneWire) |

### Schemat połączeń

#### WS2812 LED Strip
```
Portenta D6 ──→ WS2812 DIN
5V         ──→ WS2812 5V
GND        ──→ WS2812 GND
```
⚠️ **Uwaga**: Listwa WS2812 z 240 LEDami może pobierać do 14.4A (przy pełnej jasności, białym kolorze). Użyj odpowiedniego zasilacza 5V/15A.

#### DHT22
```
Portenta D7  ──→ DHT22 Data
3.3V         ──→ DHT22 VCC
GND          ──→ DHT22 GND
Rezystor 10kΩ między VCC a Data (pull-up)
```

#### DS18B20
```
Portenta D8  ──→ DS18B20 Data (żółty)
3.3V         ──→ DS18B20 VCC (czerwony)
GND          ──→ DS18B20 GND (czarny)
Rezystor 4.7kΩ między VCC a Data (pull-up)
```

## 💡 Tryby Oświetlenia

System automatycznie przełącza się między trybami w zależności od czasu:

### 1. Tryb Aqua 50%
- **Kiedy**: Między zachodem/wschodem słońca a godzinami 21:00/5:30
- **Efekt**: Wszystkie 240 LEDów świecą kolorem Aqua (cyjan) z 50% jasności
- **Kolor RGB**: (0, 128, 128)

### 2. Tryb Nocny 10%
- **Kiedy**: Między 21:00 a 5:30
- **Efekt**: Tylko 14 punktów (środek każdego stopnia) świeci białym światłem z 10% jasności
- **Kolor RGB**: (25, 25, 25)

### 3. Tryb Wyłączony
- **Kiedy**: Między wschodem słońca a zachodem słońca (w dzień)
- **Efekt**: Wszystkie LEDy wyłączone

### Logika przełączania
- Jeśli zachód słońca jest **po 21:00** → od razu aktywuje tryb nocny
- Jeśli zachód słońca jest **przed 21:00** → tryb Aqua 50% od zachodu do 21:00
- Jeśli wschód słońca jest **przed 5:30** → tryb nocny do 5:30, potem wyłączenie
- Jeśli wschód słońca jest **po 5:30** → od wschodu wyłączenie świateł

## 📡 Komendy MQTT

Wyślij na topik `Stairs/Light/input`:

### Ustawienie czasu zachodu słońca
```
sunset:17:30
```

### Ustawienie czasu wschodu słońca
```
sunrise:07:15
```

### Ustawienie godzin trybu nocnego (opcjonalne)
```
night:21:00-05:30
```

### Sprawdzenie statusu
```
status
```

### Przykład w Node-RED lub mosquitto:
```bash
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "sunset:18:45"
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "sunrise:06:30"
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "status"
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

6. **WiFi** (wbudowana w Portenta H7)

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

Jeśli potrzebujesz zmienić podstawowe ustawienia, edytuj następujące linie w pliku `.ino`:

```cpp
// WiFi
const char* ssid1 = "JPP2";
const char* ssid2 = "eir-JPP";
const char* password = "faf804a3d766";

// MQTT
const char* mqtt_server = "192.168.1.109";

// Piny
#define LED_PIN           D6
#define DHT_PIN           D7
#define DS18B20_PIN       D8

// LEDy
#define NUM_LEDS          240
#define NUM_STAIRS        14
```

## 🐛 Rozwiązywanie Problemów

### LEDy nie świecą
- Sprawdź zasilanie listwy (wymagane osobne 5V/15A)
- Sprawdź połączenie danych (pin D6)
- Upewnij się, że GND Portenty i zasilacza 5V są wspólne

### Brak połączenia WiFi
- Sprawdź czy obie sieci (JPP2, eir-JPP) są dostępne
- Sprawdź hasło
- Zobacz logi w Serial Monitor

### Brak połączenia MQTT
- Sprawdź czy broker MQTT działa na 192.168.1.109
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

### Czujniki (co 60 sekund):
- Temperatura DHT22 → `HallDown/Dht/Temperature`
- Wilgotność DHT22 → `HallDown/Dht/Humidity`
- Temperatura DS18B20 → `HallDown/DS18B20/Temperature`

### Status (na żądanie):
Wyślij `status` na `Stairs/Light/input` aby otrzymać pełny status.

### Przykład integracji z Home Assistant:

```yaml
mqtt:
  sensor:
    - name: "Schody - Temperatura DHT"
      state_topic: "HallDown/Dht/Temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      
    - name: "Schody - Wilgotność"
      state_topic: "HallDown/Dht/Humidity"
      unit_of_measurement: "%"
      device_class: humidity
      
    - name: "Schody - Temperatura DS18B20"
      state_topic: "HallDown/DS18B20/Temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      
    - name: "Schody - Status"
      state_topic: "Stairs/Light/output"
```

## 📝 Notatki

- System automatycznie wybiera silniejszą sieć WiFi
- Czujniki są opcjonalne - system działa bez nich
- Sprawdzanie trybu co 10 sekund
- Odczyt czujników co 60 sekund
- NTP synchronizuje czas co 60 sekund
- Wszystkie czasy w formacie 24-godzinnym

## 📄 Licencja

Ten projekt jest dostępny na licencji MIT.

## 👤 Autor

Projekt stworzony dla systemu inteligentnego domu.

---

**Wersja**: 1.0  
**Data**: Styczeń 2026  
**Status**: Produkcja
>>>>>>> b9357bb (Initial commit: Portenta H7 Stairs Controller with WiFi, MQTT, WS2812, DHT22, DS18B20)
