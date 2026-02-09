# Schemat Połączeń - Portenta H7 Stairs Controller



## Szczegóły Połączeń

### 1. Portenta H7 → WS2812 LED Strip

| Portenta Pin | Połączenie | WS2812 Pin | Notatki |
|--------------|-----------|------------|---------|
| **D6** | ───────→ | **DIN** | Sygnał danych (3.3V logic) |
| **GND** | ───────→ | **GND** | Masa wspólna |
| - | - | **5V** | Zasilanie z zewnętrznego zasilacza 5V/15A |

⚠️ **WAŻNE**: 
- WS2812 wymaga osobnego zasilania 5V (nie podłączać do Portenty!)
- 240 LEDów @ 100% białe = ~14.4A
- Użyj grubych przewodów (min. 18 AWG) dla zasilania
- Kondensator 1000µF 6.3V między 5V a GND przy listwie LED
- Rezystor 330Ω między D6 a DIN (opcjonalnie, dla ochrony)

### 2. Portenta H7 → DHT22

| Portenta Pin | Połączenie | DHT22 Pin | Notatki |
|--------------|-----------|-----------|---------|
| **D5** | ───────→ | **Data** | Sygnał danych |
| **3.3V** | ───────→ | **VCC** | Zasilanie 3.3V |
| **GND** | ───────→ | **GND** | Masa |
| **3.3V** + **10kΩ** + **D5** | | | Rezystor pull-up |

📝 **DHT22 Pinout** (patrząc od przodu z otworami)
```
┌─────────────┐
│  DHT22      │
│             │
│  ┌───────┐  │
│  │ ┌───┐ │  │
│  │ └───┘ │  │
│  └───────┘  │
│             │
└─┬───┬───┬───┘
  1   2   3   4
  │   │   │   │
 VCC Data NC GND
```
- Pin 1: VCC (3.3V)
- Pin 2: Data (D5)
- Pin 3: NC (nie podłączony)
- Pin 4: GND

### 3. Portenta H7 → DS18B20

| Portenta Pin | Połączenie | DS18B20 Pin | Kolor przewodu | Notatki |
|--------------|-----------|-------------|----------------|---------|
| **D1** | ───────→ | **Data** | Żółty | Sygnał danych (OneWire) |
| **3.3V** | ───────→ | **VCC** | Czerwony | Zasilanie 3.3V |
| **GND** | ───────→ | **GND** | Czarny | Masa |
| **3.3V** + **4.7kΩ** + **D1** | | | | Rezystor pull-up |

📝 **DS18B20 Pinout** (patrząc na płaską stronę):
```
     ___
    /   \
   |     |
   |DS   |
   |18B20|
   |     |
   |_____|
    | | |
    1 2 3
    │ │ │
   GND Data VCC
```
- Pin 1: GND (czarny)
- Pin 2: Data (żółty)  
- Pin 3: VCC (czerwony)

### 4. Portenta H7 → LCD 1602 I2C

| Portenta Pin | Połączenie | LCD Pin | Notatki |
|--------------|-----------|---------|---------||
| **D11 (SDA)** | ───────→ | **SDA** | Linia danych I2C |
| **D12 (SCL)** | ───────→ | **SCL** | Linia zegara I2C |
| **3.3V lub 5V** | ───────→ | **VCC** | Zasilanie (sprawdź moduł I2C) |
| **GND** | ───────→ | **GND** | Masa |

📝 **LCD 1602 I2C**:
- Adres I2C: **0x27** (domyślnie, może być 0x3F)
- Wyświetlacz: 16 znaków × 2 wiersze
- Interfejs: I2C (PCF8574 lub podobny)
- Podświetlenie: Niebieskie (sterowane przez I2C)

🔌 **Uwagi**:
- Niektóre moduły I2C mają wbudowane level shiftery i działają z 3.3V i 5V
- Sprawdź dokumentację swojego modułu przed podłączeniem
- Jeśli LCD nie działa, spróbuj adresy 0x27 lub 0x3F
- System działa bez LCD jeśli nie jest podłączone

## Lista Materiałów (BOM)

### Komponenty Główne

| Ilość | Komponent          | Specyfikacja             | 
|-------|--------------------|--------------------------|
| 1 | Arduino Portenta H7    | M7+M4 core               |
| 1 | Listwa LED WS2812      | 60 LED/m, 4m (240 LEDs)  |
| 1 | LCD 1602 I2C           | 16x2 znaków, adres 0x27 |
| 1 | Czujnik DHT11 lub DHT22| Temperatura + Wilgotność |
| 1 | Czujnik DS18B20        | Temperatura wodoodporna  |
| 1 | Zasilacz 5V            | 15A (75W)                | 
| 1 | Rezystor 10kΩ          | ¼W                       | 
| 1 | Rezystor 4.7kΩ         | ¼W                       | 
| 1 | Kondensator 1000µF     | 6.3V elektrolit          | 
| 1 | Rezystor 330Ω          | ¼W (opcjonalny)          | 

### Dodatkowe Materiały

- Przewody połączeniowe (jumper wires)
- Płytka prototypowa lub PCB najlepiej ze zlaczem 40pin
- Obudowa dla Portenty
- Przewody zasilające 18 AWG (dla listwy LED)
- Złączki/konektory
- Taśma montażowa dla listwy LED

## Uwagi Dotyczące Montażu

### Zasilanie
1. **Portenta H7**: Może być zasilana przez USB-C (5V) lub VIN (6-24V)
2. **WS2812**: Wymaga stabilnego 5V. Dla 240 LEDów użyj zasilacza 5V/15A
3. **Czujniki**: Zasilane z 3.3V z Portenty (pobór <5mA każdy)

### Długość Przewodów
- **Sygnał danych do WS2812**: Max 3-5m (powyżej użyj level shiftera 3.3V→5V)
- **DHT11/DHT22**: Max 20m
- **DS18B20**: Max 100m (na kablu ekranowanym)
- **I2C (LCD)**: Max 1m (bez wzmacniacza I2C)

### Ochrona
- Kondensator 1000µF przy wejściu zasilania listwy LED
- Rezystor 330Ω między D6 a DIN (chroni pin przed skokami prądu)
- Bezpiecznik 20A w linii zasilania 5V (opcjonalnie)

### Montaż na Schodach

- Listwa LED montowana wzdluż schodów
- Portenta montowana w dolnej części schodów (łatwy dostęp)
- LCD montowane w widocznym miejscu (opcjonalnie)
- Czujniki DHT i DS18B20 w pobliżu Portenty

## Testowanie

### Krok 1: Test Zasilania
```cpp
// Sprawdź czy Portenta się włącza
void setup() {
  Serial.begin(115200);
  Serial.println("Portenta OK");
}
```

### Krok 2: Test WS2812
```cpp
// Zapal wszystkie LEDy na czerwono
strip.fill(strip.Color(255, 0, 0));
strip.show();
```

### Krok 3: Test DHT11/DHT22
```cpp
// Odczytaj temperaturę
float t = dht.readTemperature();
Serial.println(t);
```

### Krok 4: Test DS18B20
```cpp
// Odczytaj temperaturę
ds18b20.requestTemperatures();
float t = ds18b20.getTempCByIndex(0);
Serial.println(t);
```

### Krok 5: Test LCD I2C
```cpp
// Wyświetl tekst
lcd.init();
lcd.backlight();
lcd.setCursor(0, 0);
lcd.print("Hello World!");
```

### Krok 6: Test WiFi i MQTT
```cpp
// Sprawdź połączenie
Serial.println(WiFi.localIP());
Serial.println(mqttClient.connected());
```

## Rozwiązywanie Problemów Sprzętowych

| Problem | Możliwa Przyczyna | Rozwiązanie |
|---------|-------------------|-------------|
| LEDy nie świecą | Brak zasilania 5V | Sprawdź zasilacz i połączenia |
| LEDy migają chaotycznie | Słabe połączenie danych | Dodaj rezystor 330Ω, skróć przewód |
| Losowe kolory | Brak wspólnego GND | Połącz GND Portenty i zasilacza |
| DHT11/DHT22 zwraca NaN | Brak pull-up lub zły pin | Sprawdź rezystor 10kΩ |
| DS18B20 zwraca -127°C | Brak pull-up lub zły pin | Sprawdź rezystor 4.7kΩ |
| LCD nie wyświetla | Zły adres I2C | Spróbuj 0x27 lub 0x3F, użyj skanera I2C |
| LCD wyświetla krzaki | Błędny kontrast | Dostosuj potencjometr na module I2C |
| Portenta się restartuje | Zbyt mały zasilacz | Użyj osobnego zasilania dla LEDów |

## Bezpieczeństwo

⚠️ **OSTRZEŻENIA**:
- Nie podłączaj 5V z zasilacza bezpośrednio do pinów Portenty (3.3V logic!)
- Zawsze łącz GND Portenty z GND zasilacza
- Nie przekraczaj 16mA na pin (dla D6 sygnał danych)
- Uważaj na prawidłową polaryzację elektrolitycznego kondensatora
- Zasilacz powinien mieć zabezpieczenie przeciążeniowe

---

**Ostatnia aktualizacja**: Luty 2026
