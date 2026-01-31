
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
// TYMCZASOWO ZAKOMENTOWANE - TESTOWANIE CZUJNIKÓW
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// Definicja portu debugowania dla Portenta H7
#define DBG SerialUSB

// Portenta H7 - wbudowane LEDy RGB (aktywne LOW)
#define LED_RED   LEDR
#define LED_GREEN LEDG
#define LED_BLUE  LEDB

// Wymagane makra konfiguracyjne (przekazywane np. przez -D w arduino-cli / CI)
#ifndef WIFI_SSID_PRIMARY
#error "WIFI_SSID_PRIMARY not defined"
#endif

#ifndef WIFI_SSID_FALLBACK
#error "WIFI_SSID_FALLBACK not defined"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined"
#endif

#ifndef MQTT_SERVER
#error "MQTT_SERVER not defined"
#endif

#ifndef MQTT_USER
#error "MQTT_USER not defined"
#endif

#ifndef MQTT_PASSWORD
#error "MQTT_PASSWORD not defined"
#endif


// ===== KONFIGURACJA PINÓW =====
// Portenta H7 używa numeracji GPIO (0-14) zgodnej z Arduino API
#define LED_PIN           D6       // WS2812 Data Pin 7
#define DHT_PIN           D5       // DHT11/DHT22 Pin 22 PWM1 (auto-detekcja)
#define DS18B20_PIN       D1       // DS18B20 Pin 26  PWM2

// ===== KONFIGURACJA CZUJNIKÓW =====
// Ustaw FORCE_DHT_TYPE aby wymusić typ czujnika DHT i pominąć auto-detekcję
// Opcje: 0 = auto-detekcja (default), 11 = wymuszenie DHT11, 22 = wymuszenie DHT22
#define FORCE_DHT_TYPE 0  // 0=auto, 11=DHT11, 22=DHT22

// ===== KONFIGURACJA WS2812 =====
#define NUM_LEDS          240     // Całkowita liczba LEDów
#define NUM_STAIRS        13      // Liczba stopni
#define LEDS_PER_STAIR    18      // 240/13 ≈ 18 LEDów na stopień

// ===== KONFIGURACJA WIFI =====
const char* ssid1 = WIFI_SSID_PRIMARY;
const char* ssid2 = WIFI_SSID_FALLBACK;
const char* password = WIFI_PASSWORD;

// ===== KONFIGURACJA MQTT =====
const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = 1883;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;

const char* mqtt_topic_input = "Stairs/Light/input";
const char* mqtt_topic_output = "Stairs/Light/output";
const char* mqtt_topic_led_status = "Stairs/Light/status"; // Stan LEDów
const char* mqtt_topic_temp_dht = "Stairs/DHT/Temperature";
const char* mqtt_topic_humidity = "Stairs/DHT/Humidity";
const char* mqtt_topic_temp_ds = "Stairs/Temperature/DS18B20";
const char* mqtt_topic_time = "Stairs/Time"; // NTP time
const char* device_name = "Stairs"; // Identyfikacja w sieci/MQTT
const char* device_hostname = "Stairs"; // Nazwa hosta widoczna w DHCP/skanowaniu sieci

// ===== TRYBY OŚWIETLENIA =====
enum LightMode {
  MODE_OFF,
  MODE_AQUA_50,    // Wszystkie LEDy, Aqua 50%
  MODE_NIGHT_10    // 13 punktów (1 LED na stopień), 10%
};

// ===== OBIEKTY GLOBALNE =====
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
// DHT - dwa obiekty + wskaźnik do aktywnego (auto-detekcja w initSensors)
DHT dht11(DHT_PIN, DHT11);
DHT dht22(DHT_PIN, DHT22);
DHT* dhtPtr = &dht11; // Wskaźnik do aktywnego czujnika
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiUDP ntpUDP;
// Offset ustawiany dynamicznie (DST dla Dublina)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // start z UTC, aktualizacja co 60s
int currentTimeOffsetSeconds = 0; // bieżący offset czasu (DST)

// ===== ZMIENNE GLOBALNE =====
LightMode currentMode = MODE_OFF;
bool isDayTime = true;    // true = dzień (day=1), false = noc (day=0)
int nightStartHour = 21;  // Początek trybu nocnego: 21:00
int nightStartMinute = 0;
int nightEndHour = 5;     // Koniec trybu nocnego: 5:30
int nightEndMinute = 30;

unsigned long lastWifiPublish = 0;
const unsigned long wifiPublishInterval = 30000; // Publikacja WiFi/Time co 30s
unsigned long lastModeCheck = 0;
const unsigned long modeCheckInterval = 10000; // Sprawdzanie trybu co 10s

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 30000; // Odczyt czujników co 30s
bool dhtAvailable = false;
int dhtType = DHT11; // Wykryty typ czujnika (DHT11 lub DHT22)
bool ds18b20Available = false;

// ===== FUNKCJE POMOCNICZE =====
// Wylicza środek stopnia, rozdzielając równomiernie 240 LEDów na 13 stopni
int getStairCenterIndex(int stairIndex) {
  int base = NUM_LEDS / NUM_STAIRS;
  int remainder = NUM_LEDS % NUM_STAIRS;
  int start = stairIndex * base + (stairIndex < remainder ? stairIndex : remainder);
  int length = base + (stairIndex < remainder ? 1 : 0);
  return start + (length / 2);
}

// ===== KALKULACJA DST (Dublin) =====
int dayOfWeek(int year, int month, int day) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) year -= 1;
  return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7; // 0 = niedziela
}

int daysInMonth(int year, int month) {
  static const int baseDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int dim = baseDays[month - 1];
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (month == 2 && leap) dim = 29;
  return dim;
}

int lastSundayOfMonth(int year, int month) {
  int dim = daysInMonth(year, month);
  int dow = dayOfWeek(year, month, dim);
  return dim - dow; // cofamy się o dzień tygodnia; niedziela → dow=0 → dim
}

long long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);           // [0, 399]
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
  return era * 146097LL + (long long)doe - 719468; // dni od 1970-01-01
}

time_t makeUtc(int year, int month, int day, int hour, int minute, int second) {
  long long days = daysFromCivil(year, (unsigned)month, (unsigned)day);
  return (time_t)(days * 86400LL + hour * 3600 + minute * 60 + second);
}

bool isDublinDST(unsigned long utcEpoch) {
  time_t t = (time_t)utcEpoch;

  // Bezpiecznie: najpierw zrób time_t, dopiero jego adres dawaj do gmtime()
  struct tm *utc = gmtime(&t);
  if (!utc) return false;

  int year = utc->tm_year + 1900;

  int marchSunday = lastSundayOfMonth(year, 3);
  int octSunday   = lastSundayOfMonth(year, 10);

  time_t dstStart = makeUtc(year, 3,  marchSunday, 1, 0, 0); // 01:00 UTC
  time_t dstEnd   = makeUtc(year, 10, octSunday,   1, 0, 0); // 01:00 UTC

  return (t >= dstStart) && (t < dstEnd);
}

int getDublinOffsetSeconds(unsigned long utcEpoch) {
  return isDublinDST(utcEpoch) ? 3600 : 0; // Lato: UTC+1, Zima: UTC
}

// ===== POPRAWNE FUNKCJE CZASU (ZAWSZE UTC + RĘCZNY OFFSET) =====
// Zwraca czyste UTC epoch z NTPClient
unsigned long utcEpoch() {
  return timeClient.getEpochTime();
}

// Zwraca Dublin epoch (UTC + DST offset)
unsigned long dublinEpoch() {
  return utcEpoch() + currentTimeOffsetSeconds;
}

// Zwraca godziny Dublin (0-23)
int dublinHours() {
  return (dublinEpoch() % 86400UL) / 3600UL;
}

// Zwraca minuty Dublin (0-59)
int dublinMinutes() {
  return (dublinEpoch() % 3600UL) / 60UL;
}

// Zwraca sekundy Dublin (0-59)
int dublinSeconds() {
  return dublinEpoch() % 60UL;
}

// DEPRECATED - zachowane dla kompatybilności, przekierowanie do nowych funkcji
int getDublinHours() { return dublinHours(); }
int getDublinMinutes() { return dublinMinutes(); }
int getDublinSeconds() { return dublinSeconds(); }

// ===== SETUP =====
void setup() {
  // Inicjalizacja USB Serial z dłuższym czasem oczekiwania dla Portenta H7
  DBG.begin(115200);
  delay(1500);
  DBG.println("BOOT: setup() entered");
  
  // Portenta H7 wymaga czasu na inicjalizację USB Serial
  // Czekaj maksymalnie 5 sekund na połączenie USB
  unsigned long startWait = millis();
  while (!DBG && (millis() - startWait < 2000)) {
    delay(10);
  }
  
  delay(500); // Dodatkowy czas na stabilizację
  
  DBG.println("\n\n==============================================");
  DBG.println("=== Portenta H7 WiFi TEST - Simplified ===");
  DBG.println("==============================================");
  DBG.print("DBG: Timestamp: ");
  DBG.println(millis());
  
  // Inicjalizacja wbudowanych LEDów RGB (aktywne LOW)
  DBG.println("DBG: Initializing RGB LEDs...");
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);    // Wyłącz czerwoną
  digitalWrite(LED_GREEN, HIGH);  // Wyłącz zieloną
  digitalWrite(LED_BLUE, HIGH);   // Wyłącz niebieską
  DBG.println("DBG: RGB LEDs initialized (all OFF)");
  
  // Inicjalizacja WS2812
  DBG.println("\nDBG: Initializing WS2812...");
  strip.begin();
  strip.clear();
  strip.show();
  DBG.print("DBG: WS2812 Initialized - ");
  DBG.print(NUM_LEDS);
  DBG.print(" LEDs on ");
  DBG.print(NUM_STAIRS);
  DBG.println(" stairs");
  
  // Inicjalizacja czujników
  DBG.println("DBG: Initializing sensors...");
  initSensors();
  DBG.println("DBG: Sensors initialized");
  
  // Połączenie WiFi
  DBG.println("DBG: Starting WiFi connection...");
  connectWiFi();
  
  // Konfiguracja MQTT
  DBG.println("\nDBG: Configuring MQTT client...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  DBG.println("DBG: MQTT client configured");
  
  // Uruchomienie NTP
  DBG.println("\nDBG: Starting NTP client...");
  timeClient.begin();
  timeClient.setTimeOffset(0); // WYMUSZENIE UTC - ZAWSZE OFFSET = 0
  DBG.println("DBG: NTP Client Started OK");
  DBG.println("DBG: timeClient.setTimeOffset(0) - WYMUSZONY UTC, offset ZAWSZE = 0");
  DBG.println("DBG: DST obliczany TYLKO ręcznie w currentTimeOffsetSeconds");
  DBG.println("DBG: Updating time from NTP server...");
  timeClient.update();
  DBG.print("DBG: Current time (UTC): ");
  DBG.print(timeClient.getHours());
  DBG.print(":");
  DBG.println(timeClient.getMinutes());
  DBG.print("DBG: UTC Epoch: ");
  DBG.println(utcEpoch());
  
  // INICJALIZACJA DST - oblicz offset ZARAZ PO STARCIE
  DBG.println("DBG: Initializing DST offset...");
  unsigned long utc = utcEpoch();
  currentTimeOffsetSeconds = getDublinOffsetSeconds(utc);
  DBG.print("DBG: Initial DST offset: ");
  DBG.print(currentTimeOffsetSeconds);
  DBG.print("s (");
  DBG.print(currentTimeOffsetSeconds / 3600);
  DBG.println("h)");
  DBG.print("DBG: Dublin time at startup: ");
  DBG.print(dublinHours());
  DBG.print(":");
  if (dublinMinutes() < 10) DBG.print("0");
  DBG.println(dublinMinutes());
  
  DBG.println("==============================================");
  DBG.println("=== Setup Complete - Entering Main Loop ===");
  DBG.println("==============================================\n");
}

// ===== MAIN LOOP =====
void loop() {
  unsigned long now = millis(); // Wspólna zmienna dla całej funkcji
  
  // Utrzymuj połączenie WiFi
  if (WiFi.status() != WL_CONNECTED) {
    DBG.println("\nDBG: WiFi disconnected in loop, reconnecting...");
    connectWiFi();
    // Jeśli nadal brak WiFi, pomiń resztę iteracji
    if (WiFi.status() != WL_CONNECTED) {
      delay(500);
      return;
    }
  }
  
  // Utrzymuj połączenie MQTT
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT disconnected, reconnecting...");
    reconnectMQTT();
  }
  mqttClient.loop();
  
  // Aktualizuj czas z NTP - throttling co 30s
  static unsigned long lastNtpUpdate = 0;
  if (now - lastNtpUpdate >= 30000 || lastNtpUpdate == 0) {
    lastNtpUpdate = now;
    timeClient.update();
    DBG.println("DBG: NTP time updated");
  }
  
  // Oblicz offset DST dla Dublina - TYLKO RAZ NA MINUTE (throttling)
  static unsigned long lastDSTCheck = 0;
  if (now - lastDSTCheck >= 60000 || lastDSTCheck == 0) {
    lastDSTCheck = now;
    
    unsigned long utc = utcEpoch(); // Używamy nowej funkcji utcEpoch()
    int targetOffset = getDublinOffsetSeconds(utc);
    
    if (currentTimeOffsetSeconds != targetOffset) {
      DBG.print("DBG: DST offset changed from ");
      DBG.print(currentTimeOffsetSeconds);
      DBG.print(" to ");
      DBG.println(targetOffset);
      DBG.print("DBG: UTC Epoch used: ");
      DBG.println(utc);
      currentTimeOffsetSeconds = targetOffset;
    }
  }
  
  // Sprawdź tryb oświetlenia
  if (now - lastModeCheck >= modeCheckInterval) {
    lastModeCheck = now;
    updateLightMode();
  }
  
  // Odczyt czujników
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readAndPublishSensors();
  }
  
  // Publikacja WiFi info i czasu NTP co 30s
  if (now - lastWifiPublish >= wifiPublishInterval) {
    lastWifiPublish = now;
    if (mqttClient.connected()) {
      publishWiFiInfo();
      publishNTPTime();
      publishLightStatus();
    }
  }
  
  // Informacja o działającym programie
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 10000) {
    lastHeartbeat = millis();
    DBG.println("\n=== Heartbeat ===");
    DBG.println("DBG: WiFi connected, program running...");
    DBG.print("DBG: SSID: ");
    DBG.print(WiFi.SSID());
    DBG.print(" | IP: ");
    DBG.print(WiFi.localIP());
    DBG.print(" | RSSI: ");
    DBG.print(WiFi.RSSI());
    DBG.println(" dBm");
    
    DBG.print("DBG: MQTT Status: ");
    if (mqttClient.connected()) {
      DBG.println("CONNECTED");
    } else {
      DBG.print("DISCONNECTED (state=");
      DBG.print(mqttClient.state());
      DBG.println(")");
    }
    
    DBG.print("DBG: Current Time (Dublin): ");
    DBG.print(getDublinHours());
    DBG.print(":");
    if (getDublinMinutes() < 10) DBG.print("0");
    DBG.print(getDublinMinutes());
    DBG.print(" | DST Offset: ");
    DBG.print(currentTimeOffsetSeconds / 3600);
    DBG.println(" hours");
    DBG.println("=================");
  }
  
  delay(100);
}

// ===== FUNKCJE POMOCNICZE DHT =====
// Próbuje odczytać DHT kilka razy (DHT często zwraca NaN na pierwszym odczycie)
bool tryReadDht(DHT& d, float& t, float& h) {
  for (int i = 0; i < 5; i++) {
    t = d.readTemperature();
    h = d.readHumidity();
    DBG.print("DBG:   Attempt ");
    DBG.print(i + 1);
    DBG.print("/5: temp=");
    DBG.print(t);
    DBG.print(" hum=");
    DBG.println(h);
    if (!isnan(t) && !isnan(h)) {
      DBG.println("DBG:   SUCCESS - valid reading");
      return true;
    }
    delay(300);
  }
  DBG.println("DBG:   FAILED - all attempts returned NaN");
  return false;
}

// Walidacja zakresu dla DHT11 (0-50°C, 20-90% wilg.)
bool plausibleDht11(float t, float h) {
  return !isnan(t) && !isnan(h) && t >= 0 && t <= 55 && h >= 5 && h <= 99;
}

// Walidacja zakresu dla DHT22 (-40-80°C, 0-100% wilg.)
bool plausibleDht22(float t, float h) {
  return !isnan(t) && !isnan(h) && t >= -40 && t <= 80 && h >= 0 && h <= 100;
}

// ===== INICJALIZACJA CZUJNIKÓW =====
void initSensors() {
  DBG.println("DBG: ========================================");
  DBG.println("DBG: Initializing all temperature sensors...");
  DBG.println("DBG: ========================================");
  
  // =============================================
  // KROK 1: INICJALIZACJA DHT
  // =============================================
  float t, h;
  dhtAvailable = false; // Start z założeniem braku czujnika
  
#if FORCE_DHT_TYPE == 11
  // WYMUSZENIE DHT11 - bez auto-detekcji
  DBG.println("DBG: [DHT] FORCED MODE: DHT11 (skipping auto-detection)");
  dht11.begin();
  delay(2000);
  
  if (tryReadDht(dht11, t, h) && plausibleDht11(t, h)) {
    dhtPtr = &dht11;
    dhtType = DHT11;
    dhtAvailable = true;
    DBG.print("DBG: [DHT] DHT11 initialized! temp=");
    DBG.print(t);
    DBG.print("°C hum=");
    DBG.print(h);
    DBG.println("%");
  } else {
    DBG.println("DBG: [DHT] *** FORCED DHT11 FAILED - check wiring ***");
  }
  
#elif FORCE_DHT_TYPE == 22
  // WYMUSZENIE DHT22 - bez auto-detekcji
  DBG.println("DBG: [DHT] FORCED MODE: DHT22 (skipping auto-detection)");
  dht22.begin();
  delay(2000);
  
  if (tryReadDht(dht22, t, h) && plausibleDht22(t, h)) {
    dhtPtr = &dht22;
    dhtType = DHT22;
    dhtAvailable = true;
    DBG.print("DBG: [DHT] DHT22 initialized! temp=");
    DBG.print(t);
    DBG.print("°C hum=");
    DBG.print(h);
    DBG.println("%");
  } else {
    DBG.println("DBG: [DHT] *** FORCED DHT22 FAILED - check wiring ***");
  }
  
#else
  // AUTO-DETEKCJA (default)
  DBG.println("DBG: [DHT] AUTO-DETECT MODE: Testing DHT11 first, then DHT22");
  
  // Próba 1: DHT11
  DBG.println("DBG: [DHT] Trying DHT11...");
  dht11.begin();
  delay(2000);
  
  if (tryReadDht(dht11, t, h) && plausibleDht11(t, h)) {
    dhtPtr = &dht11;
    dhtType = DHT11;
    dhtAvailable = true;
    DBG.print("DBG: [DHT] DHT11 DETECTED! temp=");
    DBG.print(t);
    DBG.print("°C hum=");
    DBG.print(h);
    DBG.println("%");
  } else {
    // Próba 2: DHT22
    DBG.println("DBG: [DHT] DHT11 failed, trying DHT22...");
    dht22.begin();
    delay(2000);
    
    if (tryReadDht(dht22, t, h) && plausibleDht22(t, h)) {
      dhtPtr = &dht22;
      dhtType = DHT22;
      dhtAvailable = true;
      DBG.print("DBG: [DHT] DHT22 DETECTED! temp=");
      DBG.print(t);
      DBG.print("°C hum=");
      DBG.print(h);
      DBG.println("%");
    } else {
      DBG.println("DBG: [DHT] *** NO DHT SENSOR DETECTED - will retry after 3 failed read cycles ***");
    }
  }
#endif
  
  // =============================================
  // KROK 2: INICJALIZACJA DS18B20 (ZAWSZE!)
  // =============================================
  // USUNIĘTO RETURN - DS18B20 inicjalizowany niezależnie od DHT
  DBG.println("DBG: [DS18B20] Testing DS18B20 sensor...");
  ds18b20.begin();
  delay(1000);
  int deviceCount = ds18b20.getDeviceCount();
  if (deviceCount > 0) {
    ds18b20Available = true;
    DBG.print("DBG: [DS18B20] OK - Found ");
    DBG.print(deviceCount);
    DBG.println(" device(s)");
  } else {
    ds18b20Available = false;
    DBG.println("DBG: [DS18B20] Not detected (will continue without it)");
  }
  
  // =============================================
  // PODSUMOWANIE
  // =============================================
  DBG.println("DBG: ========================================");
  DBG.print("DBG: Sensor initialization complete: DHT=");
  DBG.print(dhtAvailable ? "YES" : "NO");
  if (dhtAvailable) {
    DBG.print(" (");
    DBG.print(dhtType == DHT11 ? "DHT11" : "DHT22");
    DBG.print(")");
  }
  DBG.print(" DS18B20=");
  DBG.println(ds18b20Available ? "YES" : "NO");
  DBG.println("DBG: ========================================");
}

// ===== POŁĄCZENIE WiFi =====
void connectWiFi() {
  DBG.println("\n======================================");
  DBG.println("DBG: Starting WiFi connection process");
  DBG.println("======================================");
  
  DBG.print("DBG: STEP 1 - Setting hostname: ");
  DBG.println(device_hostname);
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  WiFi.setHostname(device_hostname);
  DBG.println("DBG: Hostname set successfully");
#else
  DBG.println("DBG: Hostname not supported on this platform");
#endif
  
  DBG.println("\nDBG: STEP 2 - Attempting connection to primary network");
  DBG.print("DBG: SSID: ");
  DBG.println(ssid1);
  DBG.print("DBG: Password length: ");
  DBG.println(strlen(password));
  
  // Zapal niebieską LED - rozpoczęcie łączenia
  DBG.println("DBG: Turning ON BLUE LED (connecting...)");
  digitalWrite(LED_BLUE, LOW);  // LOW = świeci
  
  DBG.println("DBG: Calling WiFi.begin()...");
  WiFi.begin(ssid1, password);
  delay(100);
  DBG.println("DBG: WiFi.begin() called, waiting for connection...");
  
  // Próba połączenia z pierwszą siecią
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    // Miganie niebieską LED podczas łączenia
    digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
    delay(250);
    
    DBG.print("DBG: Attempt ");
    DBG.print(attempts + 1);
    DBG.print("/10 - WiFi status: ");
    DBG.print(WiFi.status());
    DBG.print(" (");
    switch(WiFi.status()) {
      case WL_IDLE_STATUS: DBG.print("IDLE"); break;
      case WL_NO_SSID_AVAIL: DBG.print("NO_SSID_AVAILABLE"); break;
      case WL_SCAN_COMPLETED: DBG.print("SCAN_COMPLETED"); break;
      case WL_CONNECTED: DBG.print("CONNECTED"); break;
      case WL_CONNECT_FAILED: DBG.print("CONNECT_FAILED"); break;
      case WL_CONNECTION_LOST: DBG.print("CONNECTION_LOST"); break;
      case WL_DISCONNECTED: DBG.print("DISCONNECTED"); break;
      default: DBG.print("UNKNOWN"); break;
    }
    DBG.println(")");
    
    attempts++;
  }
  
  // Jeśli nie udało się, próbuj drugiej sieci
  if (WiFi.status() != WL_CONNECTED) {
    DBG.println("\n======================================");
    DBG.println("DBG: STEP 3 - Primary network failed!");
    DBG.println("DBG: Trying alternative network...");
    DBG.println("======================================");
    DBG.print("DBG: Alternative SSID: ");
    DBG.println(ssid2);
    
    DBG.println("DBG: Calling WiFi.begin() for alternative network...");
    WiFi.begin(ssid2, password);
    delay(100);
    DBG.println("DBG: WiFi.begin() called, waiting for connection...");
    
    attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      // Kontynuuj miganie niebieską LED
      digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
      delay(250);
      
      DBG.print("DBG: Attempt ");
      DBG.print(attempts + 1);
      DBG.print("/10 - WiFi status: ");
      DBG.print(WiFi.status());
      DBG.print(" (");
      switch(WiFi.status()) {
        case WL_IDLE_STATUS: DBG.print("IDLE"); break;
        case WL_NO_SSID_AVAIL: DBG.print("NO_SSID_AVAILABLE"); break;
        case WL_SCAN_COMPLETED: DBG.print("SCAN_COMPLETED"); break;
        case WL_CONNECTED: DBG.print("CONNECTED"); break;
        case WL_CONNECT_FAILED: DBG.print("CONNECT_FAILED"); break;
        case WL_CONNECTION_LOST: DBG.print("CONNECTION_LOST"); break;
        case WL_DISCONNECTED: DBG.print("DISCONNECTED"); break;
        default: DBG.print("UNKNOWN"); break;
      }
      DBG.println(")");
      
      attempts++;
    }
  }
  
  DBG.println("\n======================================");
  DBG.println("DBG: STEP 4 - Connection Result");
  DBG.println("======================================");
  
  if (WiFi.status() == WL_CONNECTED) {
    // Zgaś niebieską LED i zapal zieloną
    digitalWrite(LED_BLUE, HIGH);   // Zgaś niebieską
    digitalWrite(LED_GREEN, LOW);   // Zapal zieloną (LOW = świeci)
    
    DBG.println("\n*** WiFi Connected Successfully! ***");
    DBG.println("DBG: GREEN LED turned ON");
    DBG.println("\nDBG: Connection Details:");
    DBG.print("DBG:   - SSID: ");
    DBG.println(WiFi.SSID());
    DBG.print("DBG:   - IP Address: ");
    DBG.println(WiFi.localIP());
    DBG.print("DBG:   - MAC Address: ");
    DBG.println(WiFi.macAddress());
    DBG.print("DBG:   - Signal Strength (RSSI): ");
    DBG.print(WiFi.RSSI());
    DBG.println(" dBm");
    DBG.print("DBG:   - Gateway: ");
    DBG.println(WiFi.gatewayIP());
    DBG.print("DBG:   - Subnet Mask: ");
    DBG.println(WiFi.subnetMask());
    DBG.print("DBG:   - DNS: ");
    DBG.println(WiFi.dnsIP());
    
    DBG.println("======================================\n");
    
    // ZAKOMENTOWANE - TESTOWANIE WIFI
    // // Publikuj info o WiFi przez MQTT (po połączeniu z brokerem)
    // delay(100); // Krótkie opóźnienie przed publikacją
  } else {
    // Zgaś niebieską LED
    digitalWrite(LED_BLUE, HIGH);
    
    DBG.println("\n*** WiFi Connection FAILED! ***");
    DBG.println("DBG: Connection failed after all attempts");
    DBG.print("DBG: Final WiFi status: ");
    DBG.print(WiFi.status());
    DBG.print(" (");
    switch(WiFi.status()) {
      case WL_IDLE_STATUS: DBG.print("IDLE"); break;
      case WL_NO_SSID_AVAIL: DBG.print("NO_SSID_AVAILABLE"); break;
      case WL_SCAN_COMPLETED: DBG.print("SCAN_COMPLETED"); break;
      case WL_CONNECTED: DBG.print("CONNECTED"); break;
      case WL_CONNECT_FAILED: DBG.print("CONNECT_FAILED"); break;
      case WL_CONNECTION_LOST: DBG.print("CONNECTION_LOST"); break;
      case WL_DISCONNECTED: DBG.print("DISCONNECTED"); break;
      default: DBG.print("UNKNOWN"); break;
    }
    DBG.println(")");
    DBG.println("DBG: Will retry in next loop iteration");
    DBG.println("======================================\n");
  }
}

// ===== PUBLIKACJA CZASU NTP =====
void publishNTPTime() {
  DBG.println("DBG: publishNTPTime() called");
  
  if (WiFi.status() != WL_CONNECTED) {
    DBG.println("DBG: WiFi not connected, skipping time publish");
    return;
  }
  
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT not connected, skipping time publish");
    return;
  }
  
  int hours = getDublinHours();
  int minutes = getDublinMinutes();
  int seconds = getDublinSeconds();
  unsigned long epochUTC = utcEpoch(); // Używamy nowej funkcji utcEpoch()
  
  String timeInfo = "Time: " + 
                    String(hours < 10 ? "0" : "") + String(hours) + ":" +
                    String(minutes < 10 ? "0" : "") + String(minutes) + ":" +
                    String(seconds < 10 ? "0" : "") + String(seconds) +
                    " (Dublin) | UTC Epoch: " + String(epochUTC) +
                    " | DST: " + String(currentTimeOffsetSeconds / 3600) + "h";
  
  DBG.print("DBG: Publishing to topic: ");
  DBG.println(mqtt_topic_time);
  DBG.print("DBG: Message: ");
  DBG.println(timeInfo);
  
  bool result = mqttClient.publish(mqtt_topic_time, timeInfo.c_str());
  
  if (result) {
    DBG.println("DBG: Time info published successfully!");
  } else {
    DBG.println("DBG: FAILED to publish time info!");
    DBG.print("DBG: MQTT state: ");
    DBG.println(mqttClient.state());
  }
}

// ===== PUBLIKACJA STANU LEDÓW =====
void publishLightStatus() {
  DBG.println("DBG: publishLightStatus() called");
  
  if (WiFi.status() != WL_CONNECTED) {
    DBG.println("DBG: WiFi not connected, skipping LED status publish");
    return;
  }
  
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT not connected, skipping LED status publish");
    return;
  }
  
  String statusStr = "Off";
  if (currentMode == MODE_AQUA_50) {
    statusStr = "On";
  } else if (currentMode == MODE_NIGHT_10) {
    statusStr = "Night Mode";
  }
  
  DBG.print("DBG: Publishing LED status to topic: ");
  DBG.println(mqtt_topic_led_status);
  DBG.print("DBG: LED Status: ");
  DBG.println(statusStr);
  
  bool result = mqttClient.publish(mqtt_topic_led_status, statusStr.c_str());
  
  if (result) {
    DBG.println("DBG: LED status published successfully!");
  } else {
    DBG.println("DBG: FAILED to publish LED status!");
    DBG.print("DBG: MQTT state: ");
    DBG.println(mqttClient.state());
  }
}

// ===== PUBLIKACJA INFORMACJI WIFI =====
void publishWiFiInfo() {
  DBG.println("DBG: publishWiFiInfo() called");
  
  if (WiFi.status() != WL_CONNECTED) {
    DBG.println("DBG: WiFi not connected, skipping publish");
    return;
  }
  
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT not connected, skipping publish");
    return;
  }
  
  String wifiInfo = "WiFi - Device: " + String(device_name) +
                    " | SSID: " + String(WiFi.SSID()) + 
                    " | IP: " + WiFi.localIP().toString() + 
                    " | Signal: " + String(WiFi.RSSI()) + " dBm";
  
  DBG.print("DBG: Publishing to topic: ");
  DBG.println(mqtt_topic_output);
  DBG.print("DBG: Message: ");
  DBG.println(wifiInfo);
  
  bool result = mqttClient.publish(mqtt_topic_output, wifiInfo.c_str());
  
  if (result) {
    DBG.println("DBG: WiFi info published successfully!");
  } else {
    DBG.println("DBG: FAILED to publish WiFi info!");
    DBG.print("DBG: MQTT state: ");
    DBG.println(mqttClient.state());
  }
}

// ===== POŁĄCZENIE MQTT =====
void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  static unsigned long lastAttempt = 0;
  unsigned long now = millis();
  
  // Próbuj połączyć co 5 sekund
  if (now - lastAttempt < 5000) return;
  lastAttempt = now;
  
  DBG.println("\n======================================");
  DBG.println("DBG: Attempting MQTT connection...");
  DBG.print("DBG: MQTT Server: ");
  DBG.print(mqtt_server);
  DBG.print(":");
  DBG.println(mqtt_port);
  
  String clientId = String("PortentaH7-") + device_name + "-" + String(random(0xffff), HEX);
  DBG.print("DBG: Client ID: ");
  DBG.println(clientId);
  
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
    DBG.println("DBG: MQTT Connected Successfully!");
    
    DBG.print("DBG: Subscribing to topic: ");
    DBG.println(mqtt_topic_input);
    bool subResult = mqttClient.subscribe(mqtt_topic_input);
    if (subResult) {
      DBG.println("DBG: Subscription successful!");
    } else {
      DBG.println("DBG: Subscription FAILED!");
    }
    
    DBG.print("DBG: Publishing 'Online' message to: ");
    DBG.println(mqtt_topic_output);
    bool pubResult = mqttClient.publish(mqtt_topic_output, (String(device_name) + " Online").c_str());
    if (pubResult) {
      DBG.println("DBG: 'Online' message published successfully!");
    } else {
      DBG.println("DBG: 'Online' message publish FAILED!");
    }
    
    // Publikuj informacje o WiFi
    DBG.println("DBG: Calling publishWiFiInfo()...");
    publishWiFiInfo();
    DBG.println("======================================");
  } else {
    DBG.print("DBG: MQTT Connection FAILED, rc=");
    DBG.println(mqttClient.state());
    DBG.println("DBG: Will retry in 5 seconds");
    DBG.println("======================================");
  }
}

// ===== CALLBACK MQTT =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  DBG.println("\n=== MQTT Message Received ===");
  DBG.print("DBG: Topic: ");
  DBG.println(topic);
  DBG.print("DBG: Message: ");
  DBG.println(message);
  DBG.println("=============================");
  
  // Parsowanie wiadomości (format: "day=1" lub "day=0")
  if (message.startsWith("day=")) {
    int dayValue = message.substring(4).toInt();
    isDayTime = (dayValue == 1);
    DBG.print("DBG: Day/Night mode updated to: ");
    DBG.println(isDayTime ? "DAY (lights off)" : "NIGHT (lights on)");
    updateLightMode(); // Natychmiastowa aktualizacja trybu
  }
  else if (message.startsWith("night:")) {
    // Opcjonalnie: aktualizacja godzin trybu nocnego "night:21:00-05:30"
    int dashPos = message.indexOf('-');
    if (dashPos > 0) {
      String startTime = message.substring(6, dashPos);
      String endTime = message.substring(dashPos + 1);
      
      int colon1 = startTime.indexOf(':');
      int colon2 = endTime.indexOf(':');
      
      if (colon1 > 0 && colon2 > 0) {
        nightStartHour = startTime.substring(0, colon1).toInt();
        nightStartMinute = startTime.substring(colon1 + 1).toInt();
        nightEndHour = endTime.substring(0, colon2).toInt();
        nightEndMinute = endTime.substring(colon2 + 1).toInt();
        DBG.print("DBG: Night hours updated to: ");
        DBG.print(nightStartHour);
        DBG.print(":");
        DBG.print(nightStartMinute);
        DBG.print(" - ");
        DBG.print(nightEndHour);
        DBG.print(":");
        DBG.println(nightEndMinute);
        updateLightMode();
      }
    }
  }
  else if (message == "status") {
    // Wyślij aktualny status
    DBG.println("DBG: Status request received, publishing...");
    publishStatus();
  }
}

// ===== AKTUALIZACJA TRYBU OŚWIETLENIA =====
void updateLightMode() {
  int currentHour = getDublinHours();
  int currentMinute = getDublinMinutes();
  int currentTimeMin = currentHour * 60 + currentMinute;
  
  DBG.println("\nDBG: updateLightMode() called");
  DBG.print("DBG: Current Dublin time: ");
  DBG.print(currentHour);
  DBG.print(":");
  DBG.println(currentMinute);
  DBG.print("DBG: isDayTime = ");
  DBG.println(isDayTime ? "true (DAY)" : "false (NIGHT)");
  
  int nightStartTimeMin = nightStartHour * 60 + nightStartMinute;
  int nightEndTimeMin = nightEndHour * 60 + nightEndMinute;
  
  LightMode newMode = MODE_OFF;
  
  // Jeśli jest dzień (day=1) - wszystko wyłączone
  if (isDayTime) {
    newMode = MODE_OFF;
    DBG.println("DBG: Day mode -> MODE_OFF");
  }
  // Jeśli jest noc (day=0) - sprawdź godzinę z NTP
  else {
    // Logika trybu nocnego (21:00 - 5:30)
    bool isNightHours = false;
    if (nightStartTimeMin > nightEndTimeMin) {
      // Przejście przez północ
      isNightHours = (currentTimeMin >= nightStartTimeMin || currentTimeMin < nightEndTimeMin);
    } else {
      isNightHours = (currentTimeMin >= nightStartTimeMin && currentTimeMin < nightEndTimeMin);
    }
    
    DBG.print("DBG: isNightHours (21:00-5:30) = ");
    DBG.println(isNightHours ? "true" : "false");
    
    if (isNightHours) {
      newMode = MODE_NIGHT_10;  // 21:00-5:30 → 13 punktów 10%
      DBG.println("DBG: Night hours -> MODE_NIGHT_10");
    } else {
      newMode = MODE_AQUA_50;   // Reszta nocy → Aqua 50%
      DBG.println("DBG: Outside night hours -> MODE_AQUA_50");
    }
  }
  
  // Zastosuj tryb jeśli się zmienił
  if (newMode != currentMode) {
    DBG.println("\n*** MODE CHANGE DETECTED ***");
    DBG.print("DBG: Old mode: ");
    DBG.println(currentMode);
    DBG.print("DBG: New mode: ");
    DBG.println(newMode);
    
    currentMode = newMode;
    applyLightMode();
    
    String modeStr = "OFF";
    if (currentMode == MODE_AQUA_50) modeStr = "AQUA_50";
    else if (currentMode == MODE_NIGHT_10) modeStr = "NIGHT_10";
    
    DBG.println("\n=== Light Mode Changed ===");
    DBG.print("DBG: New mode: ");
    DBG.println(modeStr);
    DBG.print("DBG: Current time: ");
    DBG.print(currentHour);
    DBG.print(":");
    DBG.println(currentMinute);
    DBG.println("=========================");
    
    mqttClient.publish(mqtt_topic_output, ("Mode: " + modeStr).c_str());
    publishLightStatus(); // Publikuj również czytelny stan
  } else {
    DBG.println("DBG: No mode change needed\n");
  }
}

// ===== ZASTOSOWANIE TRYBU OŚWIETLENIA ===== Brak zmiany Brightness
void applyLightMode() {
  DBG.print("DBG: Applying light mode: ");
  DBG.println(currentMode);
  strip.clear();
  
  switch (currentMode) {
    case MODE_AQUA_50:
      // Wszystkie LEDy w kolorze Aqua z jasnością 50%
      DBG.println("DBG: Setting all LEDs to Aqua 50%");
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(0, 128, 128)); // Aqua 50%
      }
      break;
      
    case MODE_NIGHT_10:
      // 13 punktów (środek każdego stopnia) z jasnością 10% rozłożone równomiernie po 240 LEDach
      DBG.println("DBG: Setting 13 LED points for night mode 10%");
      for (int stair = 0; stair < NUM_STAIRS; stair++) {
        int ledIndex = getStairCenterIndex(stair);
        strip.setPixelColor(ledIndex, strip.Color(0, 128, 128)); // Cyjan 10%
      }
      break;
      
    case MODE_OFF:
    default:
      // Wszystkie LEDy wyłączone
      strip.clear();
      break;
  }
  
  strip.show();
  DBG.println("DBG: Light mode applied and strip updated");
}

// ===== ODCZYT I PUBLIKACJA CZUJNIKÓW =====
void readAndPublishSensors() {
  DBG.println("\n========================================");
  DBG.println("DBG: === SENSOR READ CYCLE START ===");
  DBG.println("========================================");
  
  // =============================================
  // KROK 1: ODCZYT DHT (jeśli dostępny)
  // =============================================
  static float lastValidTemp = NAN;
  static float lastValidHum = NAN;
  static int dhtNanStreak = 0;
  
  if (dhtAvailable) {
    DBG.println("DBG: [DHT] Starting read cycle...");
    DBG.print("DBG: [DHT] Pin: D");
    DBG.println(DHT_PIN);
    DBG.print("DBG: [DHT] Type: ");
    DBG.println(dhtType == DHT11 ? "DHT11" : "DHT22");
    
    float temp = dhtPtr->readTemperature();
    float humidity = dhtPtr->readHumidity() * 1.5; // Korekcja x1.5
    
    DBG.print("DBG: [DHT] Temperature raw: ");
    DBG.print(temp);
    DBG.println(" °C");
    DBG.print("DBG: [DHT] Humidity raw (x1.5): ");
    DBG.print(humidity);
    DBG.println(" %");

    bool tempValid = !isnan(temp);
    bool humValid = !isnan(humidity);
    
    DBG.print("DBG: [DHT] Temperature valid: ");
    DBG.println(tempValid ? "YES" : "NO (NaN)");
    DBG.print("DBG: [DHT] Humidity valid: ");
    DBG.println(humValid ? "YES" : "NO (NaN)");

    // RE-DETEKCJA po 3 nieudanych odczytach
    if (!tempValid || !humValid) {
      dhtNanStreak++;
      DBG.print("DBG: [DHT] *** NaN streak: ");
      DBG.print(dhtNanStreak);
      DBG.println("/3 (will re-detect after 3)");
      
      if (dhtNanStreak >= 3) {
        DBG.println("DBG: [DHT] *** 3 FAILED READS - RE-RUNNING SENSOR DETECTION ***");
        initSensors();
        dhtNanStreak = 0;
      }
      
      DBG.println("DBG: [DHT] *** WARNING: Read returned NaN - sensor may be disconnected ***");
      // Opcja: publikuj ostatnią poprawną wartość zamiast NaN
      if (!isnan(lastValidTemp) && !isnan(lastValidHum)) {
        temp = lastValidTemp;
        humidity = lastValidHum;
        tempValid = true;
        humValid = true;
        DBG.println("DBG: [DHT] Using last valid values for publish");
      }
    } else {
      // Sukces - zapisz jako ostatnie poprawne wartości
      lastValidTemp = temp;
      lastValidHum = humidity;
      if (dhtNanStreak > 0) {
        DBG.println("DBG: [DHT] Valid read - resetting NaN streak counter");
      }
      dhtNanStreak = 0;
      DBG.println("DBG: [DHT] Read SUCCESS");
    }

    // Publikacja DHT (tylko jeśli są ważne dane do publikacji)
    if ((tempValid && humValid) && mqttClient.connected()) {
      String tempStr = String(temp, 1);
      String humStr  = String(humidity, 1);

      DBG.println("\nDBG: [DHT] Publishing data...");
      DBG.print("DBG: [DHT]   Temperature: ");
      DBG.print(tempStr);
      DBG.print(" °C -> ");
      DBG.println(mqtt_topic_temp_dht);
      bool tempPub = mqttClient.publish(mqtt_topic_temp_dht, tempStr.c_str());
      DBG.print("DBG: [DHT]   Result: ");
      DBG.println(tempPub ? "SUCCESS" : "FAILED");
      
      DBG.print("DBG: [DHT]   Humidity: ");
      DBG.print(humStr);
      DBG.print(" % -> ");
      DBG.println(mqtt_topic_humidity);
      bool humPub = mqttClient.publish(mqtt_topic_humidity, humStr.c_str());
      DBG.print("DBG: [DHT]   Result: ");
      DBG.println(humPub ? "SUCCESS" : "FAILED");
    } else if (!mqttClient.connected()) {
      DBG.println("DBG: [DHT] *** MQTT NOT CONNECTED - SKIPPING PUBLISH ***");
    } else {
      DBG.println("DBG: [DHT] *** Invalid data - SKIPPING PUBLISH ***");
    }
  } else {
    DBG.println("DBG: [DHT] Sensor not available - skipping");
  }
  
  DBG.println("DBG: [DHT] === DHT READ CYCLE END ===\n");
  
  // =============================================
  // KROK 2: ODCZYT DS18B20 (jeśli dostępny)
  // =============================================
  static float lastValidTempDS = NAN;
  static int ds18NanStreak = 0;
  
  DBG.println("DBG: [DS18B20] === DS18B20 READ CYCLE START ===");
  DBG.print("DBG: [DS18B20] ds18b20Available: ");
  DBG.println(ds18b20Available ? "TRUE" : "FALSE");
  
  if (ds18b20Available) {
    DBG.println("DBG: [DS18B20] Requesting temperature...");
    ds18b20.requestTemperatures();
    delay(900); // Czas konwersji dla maksymalnej rozdzielczości
    
    float temp = ds18b20.getTempCByIndex(0);
    DBG.print("DBG: [DS18B20] Raw temperature: ");
    DBG.print(temp);
    DBG.println(" °C");
    
    bool tempInRange = (temp > -50 && temp < 100);
    bool notDisconnected = (temp != DEVICE_DISCONNECTED_C);
    
    DBG.print("DBG: [DS18B20] In range (-50 to 100): ");
    DBG.println(tempInRange ? "YES" : "NO");
    DBG.print("DBG: [DS18B20] Connected: ");
    DBG.println(notDisconnected ? "YES" : "NO");
    
    if (notDisconnected && tempInRange) {
      // Sukces - zapisz jako ostatnią poprawną wartość
      lastValidTempDS = temp;
      if (ds18NanStreak > 0) {
        DBG.println("DBG: [DS18B20] Valid read - resetting error streak");
      }
      ds18NanStreak = 0;
      
      if (mqttClient.connected()) {
        String tempStr = String(temp, 2);
        DBG.print("DBG: [DS18B20] Publishing: ");
        DBG.print(tempStr);
        DBG.print(" °C -> ");
        DBG.println(mqtt_topic_temp_ds);
        
        bool dsPub = mqttClient.publish(mqtt_topic_temp_ds, tempStr.c_str());
        DBG.print("DBG: [DS18B20] Result: ");
        DBG.println(dsPub ? "SUCCESS" : "FAILED");
      } else {
        DBG.println("DBG: [DS18B20] *** MQTT NOT CONNECTED - SKIPPING PUBLISH ***");
      }
    } else {
      // Błąd odczytu
      ds18NanStreak++;
      DBG.print("DBG: [DS18B20] *** Read error (streak: ");
      DBG.print(ds18NanStreak);
      DBG.println("/3)");
      
      if (!notDisconnected) {
        DBG.println("DBG: [DS18B20]   Reason: Sensor DISCONNECTED");
      }
      if (!tempInRange) {
        DBG.println("DBG: [DS18B20]   Reason: Temperature OUT OF RANGE");
      }
      
      // Re-detekcja po 3 błędach
      if (ds18NanStreak >= 3) {
        DBG.println("DBG: [DS18B20] *** 3 FAILED READS - RE-RUNNING SENSOR DETECTION ***");
        initSensors();
        ds18NanStreak = 0;
      }
      
      // Opcja: publikuj ostatnią poprawną wartość
      if (!isnan(lastValidTempDS) && mqttClient.connected()) {
        String tempStr = String(lastValidTempDS, 2);
        DBG.println("DBG: [DS18B20] Using last valid value for publish");
        mqttClient.publish(mqtt_topic_temp_ds, tempStr.c_str());
      }
    }
  } else {
    DBG.println("DBG: [DS18B20] Sensor not available - skipping");
  }
  
  DBG.println("DBG: [DS18B20] === DS18B20 READ CYCLE END ===");
  DBG.println("========================================");
  DBG.println("DBG: === ALL SENSORS READ CYCLE END ===");
  DBG.println("========================================\n");
}

// ===== PUBLIKACJA STATUSU =====
void publishStatus() {
  DBG.println("DBG: publishStatus() called");
  
  String modeStr = "OFF";
  if (currentMode == MODE_AQUA_50) modeStr = "AQUA_50";
  else if (currentMode == MODE_NIGHT_10) modeStr = "NIGHT_10";
  
  String status = "Time: " + String(getDublinHours()) + ":" + 
                  String(getDublinMinutes()) + 
                  " | Device: " + String(device_name) +
                  " | Day: " + String(isDayTime ? "1" : "0") +
                  " | Night Hours: " + String(nightStartHour) + ":" + 
                  String(nightStartMinute) + "-" + String(nightEndHour) + ":" + 
                  String(nightEndMinute) + " | Mode: " + modeStr;
  
  DBG.print("DBG: Status message: ");
  DBG.println(status);
  
  bool result = mqttClient.publish(mqtt_topic_output, status.c_str());
  if (result) {
    DBG.println("DBG: Status published successfully to MQTT");
  } else {
    DBG.println("DBG: FAILED to publish status to MQTT");
  }
  
  // Publikuj również info o WiFi
  publishWiFiInfo();
}
