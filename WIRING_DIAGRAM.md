# Schemat Połączeń - Portenta H7 Stairs Controller

## Diagram ASCII

```
                                    ZASILANIE
                                    ┌──────────────┐
                                    │ Zasilacz 5V  │
                                    │    15A       │
                                    └──┬───────┬───┘
                                       │       │
                                      5V      GND
                                       │       │
    ┌──────────────────────────────────┼───────┼──────────┐
    │                                  │       │          │
    │         PORTENTA H7              │       │          │
    │                                  │       │          │
    │  ┌─────────────────┐             │       │          │
    │  │                 │             │       │          │
    │  │   M7 Core       │             │       │          │
    │  │   480 MHz       │             │       │          │
    │  │                 │             │       │          │
    │  └─────────────────┘             │       │          │
    │                                  │       │          │
    │  D6  ────────────────────────────┼───────┼──────────┼──→ WS2812 DIN
    │                                  │       │          │
    │  D7  ─────────────┐              │       │          │
    │                   │              │       │          │
    │  3.3V ────────┬───┼──────────────┼───────┤          │
    │               │   │              │       │          │
    │  GND  ────────┼───┼──────────────┼───────┼──────────┼──→ WS2812 GND
    │               │   │              │       │          │
    │  D8  ─────────│───┼──────────┐   │       │          │
    │               │   │          │   │       │          │
    └───────────────┼───┼──────────┼───┴───────┴──────────┘
                    │   │          │
                    │   │          │
              ┌─────┴───┼──────────┼────────┐
              │ 10kΩ    │          │ 4.7kΩ  │
              │         │          │        │
         ┌────┴────┐    │     ┌────┴────┐   │
         │  DHT22  │    │     │DS18B20  │   │
         │         │    │     │         │   │
         │  Data ──┴────┘     │  Data ──┴───┘
         │  VCC  ──────────────→ 3.3V
         │  GND  ──────────────→ GND
         └─────────┘            └─────────┘


                    WS2812 LED STRIP (240 LEDs)
         ┌──────────────────────────────────────────┐
         │  ●●●●●●●●●●●●●●●●●  Stopień 1  (17 LEDs) │
         │  ●●●●●●●●●●●●●●●●●  Stopień 2  (17 LEDs) │
         │  ●●●●●●●●●●●●●●●●●  Stopień 3  (17 LEDs) │
         │         ...                               │
         │  ●●●●●●●●●●●●●●●●●  Stopień 14 (17 LEDs) │
         └──────────────────────────────────────────┘
                Total: 240 LEDs
```

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
| **D7** | ───────→ | **Data** | Sygnał danych |
| **3.3V** | ───────→ | **VCC** | Zasilanie 3.3V |
| **GND** | ───────→ | **GND** | Masa |
| **3.3V** + **10kΩ** + **D7** | | | Rezystor pull-up |

📝 **DHT22 Pinout** (patrząc od przodu z otworami):
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
- Pin 2: Data (D7)
- Pin 3: NC (nie podłączony)
- Pin 4: GND

### 3. Portenta H7 → DS18B20

| Portenta Pin | Połączenie | DS18B20 Pin | Kolor przewodu | Notatki |
|--------------|-----------|-------------|----------------|---------|
| **D8** | ───────→ | **Data** | Żółty | Sygnał danych (OneWire) |
| **3.3V** | ───────→ | **VCC** | Czerwony | Zasilanie 3.3V |
| **GND** | ───────→ | **GND** | Czarny | Masa |
| **3.3V** + **4.7kΩ** + **D8** | | | | Rezystor pull-up |

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

## Lista Materiałów (BOM)

### Komponenty Główne

| Ilość | Komponent | Specyfikacja | Cena (szacunkowa) |
|-------|-----------|--------------|-------------------|
| 1 | Arduino Portenta H7 | M7+M4 core | ~90€ |
| 1 | Listwa LED WS2812 | 60 LED/m, 4m (240 LEDs) | ~40€ |
| 1 | Czujnik DHT22 | Temperatura + Wilgotność | ~5€ |
| 1 | Czujnik DS18B20 | Temperatura wodoodporna | ~3€ |
| 1 | Zasilacz 5V | 15A (75W) | ~15€ |
| 1 | Rezystor 10kΩ | ¼W | ~0.10€ |
| 1 | Rezystor 4.7kΩ | ¼W | ~0.10€ |
| 1 | Kondensator 1000µF | 6.3V elektrolit | ~0.50€ |
| 1 | Rezystor 330Ω | ¼W (opcjonalny) | ~0.10€ |

### Dodatkowe Materiały

- Przewody połączeniowe (jumper wires)
- Płytka prototypowa lub PCB
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
- **DHT22**: Max 20m
- **DS18B20**: Max 100m (na kablu ekranowanym)

### Ochrona
- Kondensator 1000µF przy wejściu zasilania listwy LED
- Rezystor 330Ω między D6 a DIN (chroni pin przed skokami prądu)
- Bezpiecznik 20A w linii zasilania 5V (opcjonalnie)

### Montaż na Schodach
```
  Stopień 14  ═══════════════ ●● (17 LEDs)
  Stopień 13  ═══════════════ ●● (17 LEDs)
  Stopień 12  ═══════════════ ●● (17 LEDs)
     ...
  Stopień 2   ═══════════════ ●● (17 LEDs)
  Stopień 1   ═══════════════ ●● (17 LEDs)
               ↑
           [Portenta H7]
         [Zasilacz 5V/15A]
```

- Listwa LED montowana pod krawędzią każdego stopnia
- Portenta montowana w dolnej części schodów (łatwy dostęp)
- Czujniki DHT22 i DS18B20 w pobliżu Portenty

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

### Krok 3: Test DHT22
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

### Krok 5: Test WiFi i MQTT
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
| DHT22 zwraca NaN | Brak pull-up lub zły pin | Sprawdź rezystor 10kΩ |
| DS18B20 zwraca -127°C | Brak pull-up lub zły pin | Sprawdź rezystor 4.7kΩ |
| Portenta się restartuje | Zbyt mały zasilacz | Użyj osobnego zasilania dla LEDów |

## Bezpieczeństwo

⚠️ **OSTRZEŻENIA**:
- Nie podłączaj 5V z zasilacza bezpośrednio do pinów Portenty (3.3V logic!)
- Zawsze łącz GND Portenty z GND zasilacza
- Nie przekraczaj 16mA na pin (dla D6 sygnał danych)
- Uważaj na prawidłową polaryzację elektrolitycznego kondensatora
- Zasilacz powinien mieć zabezpieczenie przeciążeniowe

---

**Ostatnia aktualizacja**: Styczeń 2026
