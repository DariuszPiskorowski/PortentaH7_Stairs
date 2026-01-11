/*
 * Portenta H7 - Smart Stairs Lighting Controller
 * 
 * Sterowanie oświetleniem schodów z listwy WS2812 (240 LEDs, 14 stopni)
 * - WiFi: JPP2 lub eir-JPP
 * - MQTT: 192.168.1.109
 * - Czujniki: DHT22 i DS18B20
 * 
 * Logika działania:
 * - Serwer wysyła przez MQTT: "day=1" (dzień) lub "day=0" (noc)
 * - Jeśli day=1: wszystkie LEDy wyłączone
 * - Jeśli day=0 + godzina 21:00-5:30: tryb nocny (14 punktów, 10%)
 * - Jeśli day=0 + inna godzina: tryb Aqua (wszystkie LEDy, 50%)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// Wrażliwe dane (SSID/hasła/MQTT) przeniesione do osobnego nagłówka
#include "secrets.h"

// ===== KONFIGURACJA PINÓW =====
#define LED_PIN           D6      // WS2812 Data Pin
#define DHT_PIN           D7      // DHT22 Pin
#define DS18B20_PIN       D8      // DS18B20 Pin

// ===== KONFIGURACJA WS2812 =====
#define NUM_LEDS          240     // Całkowita liczba LEDów
#define NUM_STAIRS        14      // Liczba stopni
#define LEDS_PER_STAIR    17      // 240/14 ≈ 17 LEDów na stopień

// ===== KONFIGURACJA WIFI =====
const char* ssid1 = WIFI_SSID_PRIMARY;
const char* ssid2 = WIFI_SSID_FALLBACK;
const char* password = WIFI_PASSWORD;

// ===== KONFIGURACJA MQTT =====
const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = MQTT_PORT;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;
const char* mqtt_topic_input = MQTT_TOPIC_INPUT;
const char* mqtt_topic_output = MQTT_TOPIC_OUTPUT;
const char* mqtt_topic_temp_dht = MQTT_TOPIC_TEMP_DHT;
const char* mqtt_topic_humidity = MQTT_TOPIC_HUMIDITY;
const char* mqtt_topic_temp_ds = MQTT_TOPIC_TEMP_DS;
const char* device_name = "Stairs"; // Identyfikacja w sieci/MQTT
const char* device_hostname = "Stairs"; // Nazwa hosta widoczna w DHCP/skanowaniu sieci

// ===== TRYBY OŚWIETLENIA =====
enum LightMode {
  MODE_OFF,
  MODE_AQUA_50,    // Wszystkie LEDy, Aqua 50%
  MODE_NIGHT_10    // 14 punktów, 10%
};

// ===== OBIEKTY GLOBALNE =====
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
DHT dht(DHT_PIN, DHT22);
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiUDP ntpUDP;
// Offset ustawiany dynamicznie (DST dla Dublina)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // start z UTC, aktualizacja co 60s

// ===== ZMIENNE GLOBALNE =====
LightMode currentMode = MODE_OFF;
bool isDayTime = true;    // true = dzień (day=1), false = noc (day=0)
int nightStartHour = 21;  // Początek trybu nocnego: 21:00
int nightStartMinute = 0;
int nightEndHour = 5;     // Koniec trybu nocnego: 5:30
int nightEndMinute = 30;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 60000; // Odczyt czujników co 60s
unsigned long lastModeCheck = 0;
const unsigned long modeCheckInterval = 10000; // Sprawdzanie trybu co 10s

bool dhtAvailable = false;
bool ds18b20Available = false;

// Wylicza środek stopnia, rozdzielając równomiernie 240 LEDów na 14 segmentów
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
  struct tm* utc = gmtime((time_t*)&utcEpoch);
  if (!utc) return false;
  int year = utc->tm_year + 1900;

  int marchSunday = lastSundayOfMonth(year, 3);
  int octSunday   = lastSundayOfMonth(year, 10);

  time_t dstStart = makeUtc(year, 3, marchSunday, 1, 0, 0); // 01:00 UTC w ostatnią niedzielę marca
  time_t dstEnd   = makeUtc(year, 10, octSunday, 1, 0, 0);   // 01:00 UTC w ostatnią niedzielę października

  return utcEpoch >= (unsigned long)dstStart && utcEpoch < (unsigned long)dstEnd;
}

int getDublinOffsetSeconds(unsigned long utcEpoch) {
  return isDublinDST(utcEpoch) ? 3600 : 0; // Lato: UTC+1, Zima: UTC
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== Portenta H7 Stairs Controller ===");
  
  // Inicjalizacja WS2812
  strip.begin();
  strip.clear();
  strip.show();
  Serial.println("WS2812: Initialized");
  
  // Inicjalizacja czujników
  initSensors();
  
  // Połączenie WiFi
  connectWiFi();
  
  // Konfiguracja MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  
  // Uruchomienie NTP
  timeClient.begin();
  Serial.println("NTP Client: Started");
  
  Serial.println("=== Setup Complete ===\n");
}

// ===== MAIN LOOP =====
void loop() {
  // Utrzymuj połączenie WiFi
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    // Jeśli nadal brak WiFi, pomiń resztę iteracji – MQTT i NTP i tak nie zadziałają
    if (WiFi.status() != WL_CONNECTED) {
      delay(500);
      return;
    }
  }
  
  // Utrzymuj połączenie MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
  
  // Aktualizuj czas z NTP i dostosuj offset DST dla Dublina
  timeClient.update();
  unsigned long utcEpoch = timeClient.getEpochTime() - timeClient.getTimeOffset();
  int targetOffset = getDublinOffsetSeconds(utcEpoch);
  if (timeClient.getTimeOffset() != targetOffset) {
    timeClient.setTimeOffset(targetOffset);
    timeClient.update(); // odśwież z poprawnym offsetem
  }
  
  // Sprawdź tryb oświetlenia
  unsigned long now = millis();
  if (now - lastModeCheck >= modeCheckInterval) {
    lastModeCheck = now;
    updateLightMode();
  }
  
  // Odczyt czujników
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readAndPublishSensors();
  }
  
  delay(100);
}

// ===== INICJALIZACJA CZUJNIKÓW =====
void initSensors() {
  // DHT22
  dht.begin();
  delay(2000);
  float testTemp = dht.readTemperature();
  if (!isnan(testTemp)) {
    dhtAvailable = true;
    Serial.println("DHT22: Available");
  } else {
    Serial.println("DHT22: Not detected (will continue without it)");
  }
  
  // DS18B20
  ds18b20.begin();
  delay(1000);
  if (ds18b20.getDeviceCount() > 0) {
    ds18b20Available = true;
    Serial.println("DS18B20: Available");
  } else {
    Serial.println("DS18B20: Not detected (will continue without it)");
  }
}

// ===== POŁĄCZENIE WiFi =====
void connectWiFi() {
  Serial.print("Connecting to WiFi");

#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  WiFi.setHostname(device_hostname);
#endif
  
  // Próba połączenia z pierwszą siecią
  WiFi.begin(ssid1, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 5) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  // Jeśli nie udało się, próbuj drugiej sieci
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("\nTrying alternative network");
    WiFi.begin(ssid2, password);
    attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    
    // Publikuj info o WiFi przez MQTT (po połączeniu z brokerem)
    delay(1000); // Krótkie opóźnienie przed publikacją
  } else {
    Serial.println("\nWiFi Connection Failed!");
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
  
  Serial.print("Connecting to MQTT...");
  
  String clientId = String("PortentaH7-") + device_name + "-" + String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
    Serial.println("Connected!");
    mqttClient.subscribe(mqtt_topic_input);
    mqttClient.publish(mqtt_topic_output, (String(device_name) + " Online").c_str());
    Serial.print("Subscribed to: ");
    Serial.println(mqtt_topic_input);
    
    // Publikuj informacje o WiFi
    publishWiFiInfo();
  } else {
    Serial.print("Failed, rc=");
    Serial.println(mqttClient.state());
  }
}

// ===== CALLBACK MQTT =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("MQTT Message [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Parsowanie wiadomości (format: "day=1" lub "day=0")
  if (message.startsWith("day=")) {
    int dayValue = message.substring(4).toInt();
    isDayTime = (dayValue == 1);
    Serial.print("Day/Night updated: ");
    Serial.println(isDayTime ? "DAY (lights off)" : "NIGHT (lights on)");
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
        Serial.println("Night hours updated");
        updateLightMode();
      }
    }
  }
  else if (message == "status") {
    // Wyślij aktualny status
    publishStatus();
  }
}

// ===== AKTUALIZACJA TRYBU OŚWIETLENIA =====
void updateLightMode() {
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentTimeMin = currentHour * 60 + currentMinute;
  
  int nightStartTimeMin = nightStartHour * 60 + nightStartMinute;
  int nightEndTimeMin = nightEndHour * 60 + nightEndMinute;
  
  LightMode newMode = MODE_OFF;
  
  // Jeśli jest dzień (day=1) - wszystko wyłączone
  if (isDayTime) {
    newMode = MODE_OFF;
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
    
    if (isNightHours) {
      newMode = MODE_NIGHT_10;  // 21:00-5:30 → 14 punktów 10%
    } else {
      newMode = MODE_AQUA_50;   // Reszta nocy → Aqua 50%
    }
  }
  
  // Zastosuj tryb jeśli się zmienił
  if (newMode != currentMode) {
    currentMode = newMode;
    applyLightMode();
    
    String modeStr = "OFF";
    if (currentMode == MODE_AQUA_50) modeStr = "AQUA_50";
    else if (currentMode == MODE_NIGHT_10) modeStr = "NIGHT_10";
    
    Serial.print("Mode changed to: ");
    Serial.println(modeStr);
    
    mqttClient.publish(mqtt_topic_output, ("Mode: " + modeStr).c_str());
  }
}

// ===== ZASTOSOWANIE TRYBU OŚWIETLENIA =====
void applyLightMode() {
  strip.clear();
  
  switch (currentMode) {
    case MODE_AQUA_50:
      // Wszystkie LEDy w kolorze Aqua z jasnością 50%
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(0, 128, 128)); // Aqua 50%
      }
      break;
      
    case MODE_NIGHT_10:
      // 14 punktów (środek każdego stopnia) z jasnością 10% rozłożone równomiernie po 240 LEDach
      for (int stair = 0; stair < NUM_STAIRS; stair++) {
        int ledIndex = getStairCenterIndex(stair);
        strip.setPixelColor(ledIndex, strip.Color(25, 25, 25)); // Biały 10%
      }
      break;
      
    case MODE_OFF:
    default:
      // Wszystkie LEDy wyłączone
      strip.clear();
      break;
  }
  
  strip.show();
}

// ===== ODCZYT I PUBLIKACJA CZUJNIKÓW =====
void readAndPublishSensors() {
  // DHT22
  if (dhtAvailable) {
    float temp = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (!isnan(temp)) {
      String tempStr = String(temp, 1);
      mqttClient.publish(mqtt_topic_temp_dht, tempStr.c_str());
      Serial.print("DHT22 Temperature: ");
      Serial.print(tempStr);
      Serial.println(" °C");
    } else {
      Serial.println("DHT22: Read error");
    }
    
    if (!isnan(humidity)) {
      String humStr = String(humidity, 1);
      mqttClient.publish(mqtt_topic_humidity, humStr.c_str());
      Serial.print("DHT22 Humidity: ");
      Serial.print(humStr);
      Serial.println(" %");
    }
  }
  
  // DS18B20
  if (ds18b20Available) {
    ds18b20.requestTemperatures();
    // DS18B20 przy maksymalnej rozdzielczości wymaga do ~750 ms — dajemy 900 ms zapasu
    delay(900);
    float temp = ds18b20.getTempCByIndex(0);
    
    if (temp != DEVICE_DISCONNECTED_C && temp > -50 && temp < 100) {
      String tempStr = String(temp, 2);
      mqttClient.publish(mqtt_topic_temp_ds, tempStr.c_str());
      Serial.print("DS18B20 Temperature: ");
      Serial.print(tempStr);
      Serial.println(" °C");
    } else {
      Serial.println("DS18B20: Read error");
    }
  }
}

// ===== PUBLIKACJA INFORMACJI WIFI =====
void publishWiFiInfo() {
  if (WiFi.status() == WL_CONNECTED) {
    String wifiInfo = "WiFi - Device: " + String(device_name) +
                      " | SSID: " + String(WiFi.SSID()) + 
                      " | IP: " + WiFi.localIP().toString() + 
                      " | Signal: " + String(WiFi.RSSI()) + " dBm";
    mqttClient.publish(mqtt_topic_output, wifiInfo.c_str());
    Serial.println("Published: " + wifiInfo);
  }
}

// ===== PUBLIKACJA STATUSU =====
void publishStatus() {
  String status = "Time: " + String(timeClient.getHours()) + ":" + 
                  String(timeClient.getMinutes()) + 
                  " | Device: " + String(device_name) +
                  " | Day: " + String(isDayTime ? "1" : "0") +
                  " | Night Hours: " + String(nightStartHour) + ":" + 
                  String(nightStartMinute) + "-" + String(nightEndHour) + ":" + 
                  String(nightEndMinute) + " | Mode: ";
  
  if (currentMode == MODE_AQUA_50) status += "AQUA_50";
  else if (currentMode == MODE_NIGHT_10) status += "NIGHT_10";
  else status += "OFF";
  
  mqttClient.publish(mqtt_topic_output, status.c_str());
  Serial.println(status);
  
  // Publikuj również info o WiFi
  publishWiFiInfo();
}
