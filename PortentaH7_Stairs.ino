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

// ===== KONFIGURACJA PINÓW =====
#define LED_PIN           D6      // WS2812 Data Pin
#define DHT_PIN           D7      // DHT22 Pin
#define DS18B20_PIN       D8      // DS18B20 Pin

// ===== KONFIGURACJA WS2812 =====
#define NUM_LEDS          240     // Całkowita liczba LEDów
#define NUM_STAIRS        14      // Liczba stopni
#define LEDS_PER_STAIR    17      // 240/14 ≈ 17 LEDów na stopień

// ===== KONFIGURACJA WIFI =====
const char* ssid1 = "JPP2";
const char* ssid2 = "eir-JPP";
const char* password = "faf804a3d766";

// ===== KONFIGURACJA MQTT =====
const char* mqtt_server = "192.168.1.109";
const int mqtt_port = 1883;
const char* mqtt_topic_input = "Stairs/Light/input";
const char* mqtt_topic_output = "Stairs/Light/output";
const char* mqtt_topic_temp_dht = "HallDown/Dht/Temperature";
const char* mqtt_topic_humidity = "HallDown/Dht/Humidity";
const char* mqtt_topic_temp_ds = "HallDown/DS18B20/Temperature";

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
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); // UTC+1, update co 60s

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
  }
  
  // Utrzymuj połączenie MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();
  
  // Aktualizuj czas z NTP
  timeClient.update();
  
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
  
  // Próba połączenia z pierwszą siecią
  WiFi.begin(ssid1, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  // Jeśli nie udało się, próbuj drugiej sieci
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("\nTrying alternative network");
    WiFi.begin(ssid2, password);
    attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
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
  
  String clientId = "PortentaH7-Stairs-";
  clientId += String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("Connected!");
    mqttClient.subscribe(mqtt_topic_input);
    mqttClient.publish(mqtt_topic_output, "Portenta H7 Stairs Online");
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
      // 14 punktów (środek każdego stopnia) z jasnością 10%
      for (int stair = 0; stair < NUM_STAIRS; stair++) {
        int ledIndex = stair * LEDS_PER_STAIR + LEDS_PER_STAIR / 2; // Środek stopnia
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
    delay(100);
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
    String wifiInfo = "WiFi - SSID: " + String(WiFi.SSID()) + 
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
