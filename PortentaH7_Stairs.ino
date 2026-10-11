
#include <WiFi.h>
#include <mbed.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Definicja portu debugowania dla Portenta H7
#define DBG SerialUSB

// Portenta H7 - wbudowane LEDy RGB (aktywne LOW)
#define LED_RED   LEDR
#define LED_GREEN LEDG
#define LED_BLUE  LEDB

// Wymagane makra konfiguracyjne (przekazywane np. przez -D w arduino-cli / CI)
#define WIFI_SSID_PRIMARY "eir-JPP"

#define WIFI_SSID_FALLBACK "JPP2"

#define WIFI_PASSWORD_PRIMARY "**********"
#define WIFI_PASSWORD_FALLBACK "**********"

#define MQTT_SERVER "192.168.1.109"

#define MQTT_USER "sonixh"

#define MQTT_PASSWORD "*****"




// ===== KONFIGURACJA PINÓW =====
// Portenta H7 używa numeracji GPIO (0-14) zgodnej z Arduino API
#define LED_PIN           D6       // WS2812 Data Pin 7
#define DS18B20_PIN       D1       // DS18B20 Pin 26  PWM2

// ===== KONFIGURACJA WS2812 =====
#define NUM_LEDS          258     // Całkowita liczba LEDów
#define NUM_STAIRS        14      // Liczba stopni
#define LEDS_PER_STAIR    18      // 240/13 ≈ 18 LEDów na stopień

// ===== KONFIGURACJA WIFI =====
const char* ssid1 = WIFI_SSID_PRIMARY;
const char* ssid2 = WIFI_SSID_FALLBACK;
const char* password1 = WIFI_PASSWORD_PRIMARY;
const char* password2 = WIFI_PASSWORD_FALLBACK;

// ===== KONFIGURACJA MQTT =====
const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = 1883;
const char* mqtt_user = MQTT_USER;
const char* mqtt_password = MQTT_PASSWORD;

const char* mqtt_topic_input = "Stairs/Light/input";
const char* mqtt_topic_output = "Portenta2/Light/output";
const char* mqtt_topic_led_status = "Portenta2/status";//"Stairs/Light/status"; // Stan LEDów
const char* mqtt_topic_temp_ds = "HallDown/Temperature/temperature";
const char* mqtt_topic_time = "Portenta2/Time"; // NTP time
const char* mqtt_topic_watchdog_reboot = "Portenta/Watchdog/reboot";
const char* mqtt_topic_watchdog_status = "Portenta/Watchdog/status";
const char* device_name = "PortentaH7_id"; // Identyfikacja w sieci/MQTT
const char* device_hostname = "PortentaH7_host"; // Nazwa hosta widoczna w DHCP/skanowaniu sieci

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
OneWire oneWire(DS18B20_PIN);
DallasTemperature ds18b20(&oneWire);
WiFiUDP ntpUDP;
// Offset ustawiany dynamicznie (DST dla Dublina)
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // start z UTC, aktualizacja co 60s
int currentTimeOffsetSeconds = 0; // bieżący offset czasu (DST)

// ===== WYŚWIETLACZ LCD 1602 I2C =====
// Standardowy adres I2C dla LCD 1602: 0x27 lub 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2); // adres 0x27, 16 kolumn, 2 wiersze
bool lcdAvailable = false;

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
unsigned long lastLcdUpdate = 0;
const unsigned long lcdUpdateInterval = 10000; // Aktualizacja LCD co 10s
unsigned long lastLcdReinit = 0;
const unsigned long lcdReinitInterval = 60UL * 60UL * 1000UL;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 30000; // Odczyt czujników co 30s
bool ds18b20Available = false;

// ===== WATCHDOG I AUTOMATYCZNE ODZYSKIWANIE =====
mbed::Watchdog& hardwareWatchdog = mbed::Watchdog::get_instance();
bool hardwareWatchdogStarted = false;
uint32_t hardwareWatchdogTimeoutMs = 0;
const uint32_t requestedHardwareWatchdogTimeoutMs = 30000UL;

const unsigned long connectivityResetTimeout = 5UL * 60UL * 1000UL;
unsigned long wifiFailureSince = 0;
unsigned long mqttFailureSince = 0;
bool pendingRemoteReboot = false;

// ===== STAN ŁĄCZNOŚCI =====
bool ntpTimeValid = false;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long wifiReconnectInterval = 10000UL;
const unsigned long wifiConnectTimeout = 15000UL;

bool isZeroIP(const IPAddress& ip) {
  return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

bool wifiReady() {
  return WiFi.status() == WL_CONNECTED && !isZeroIP(WiFi.localIP());
}

bool mqttTopicConfigured(const char* topic) {
  return topic != nullptr && topic[0] != '\0';
}

bool safeMqttPublish(const char* topic, const char* payload, bool retained = false) {
  if (!mqttClient.connected()) return false;
  if (!mqttTopicConfigured(topic)) {
    DBG.println("DBG: MQTT publish skipped - empty topic");
    return false;
  }
  return mqttClient.publish(topic, payload, retained);
}

void kickHardwareWatchdog() {
  if (hardwareWatchdogStarted) {
    hardwareWatchdog.kick();
  }
}

void startHardwareWatchdog() {
  if (hardwareWatchdog.is_running()) {
    hardwareWatchdogStarted = true;
    hardwareWatchdogTimeoutMs = hardwareWatchdog.get_timeout();
    hardwareWatchdog.kick();
    DBG.print("DBG: Hardware watchdog already running, timeout=");
    DBG.print(hardwareWatchdogTimeoutMs);
    DBG.println(" ms");
    return;
  }

  const uint32_t maxTimeout = hardwareWatchdog.get_max_timeout();
  hardwareWatchdogTimeoutMs = requestedHardwareWatchdogTimeoutMs;
  if (maxTimeout > 0 && hardwareWatchdogTimeoutMs > maxTimeout) {
    hardwareWatchdogTimeoutMs = maxTimeout;
  }

  if (hardwareWatchdogTimeoutMs < 5000UL) {
    DBG.println("DBG: Hardware watchdog not started - supported timeout is too short");
    return;
  }

  hardwareWatchdogStarted = hardwareWatchdog.start(hardwareWatchdogTimeoutMs);
  DBG.print("DBG: Hardware watchdog: ");
  DBG.print(hardwareWatchdogStarted ? "STARTED, timeout=" : "FAILED, requested timeout=");
  DBG.print(hardwareWatchdogTimeoutMs);
  DBG.println(" ms");
}

void serviceDelay(unsigned long durationMs) {
  const unsigned long started = millis();
  while (millis() - started < durationMs) {
    if (mqttClient.connected()) mqttClient.loop();
    kickHardwareWatchdog();
    delay(5);
  }
}

void performSystemReset(const char* reason, bool publishRemoteStatus) {
  DBG.print("RESET: ");
  DBG.println(reason);

  if (publishRemoteStatus && mqttClient.connected()) {
    // Usuwa ewentualną retained komendę, aby nie utworzyć pętli restartów.
    mqttClient.publish(mqtt_topic_watchdog_reboot, "", true);
    mqttClient.publish(mqtt_topic_watchdog_status, "rebooting_remote", true);
    serviceDelay(500);

    // Rozłączenie kontrolowane zachowuje retained status rebooting_remote.
    mqttClient.disconnect();
    espClient.stop();
    delay(50);
  }

  NVIC_SystemReset();
  while (true) { }
}

void checkConnectivityRecovery(unsigned long now) {
  if (!wifiReady()) {
    mqttFailureSince = 0;
    if (wifiFailureSince == 0) wifiFailureSince = (now == 0 ? 1 : now);

    if (now - wifiFailureSince >= connectivityResetTimeout) {
      performSystemReset("WiFi unavailable for 5 minutes", false);
    }
    return;
  }

  wifiFailureSince = 0;

  if (!mqttClient.connected()) {
    if (mqttFailureSince == 0) mqttFailureSince = (now == 0 ? 1 : now);

    if (now - mqttFailureSince >= connectivityResetTimeout) {
      performSystemReset("MQTT unavailable for 5 minutes", false);
    }
  } else {
    mqttFailureSince = 0;
  }
}

void processPendingRemoteReboot() {
  if (!pendingRemoteReboot) return;
  pendingRemoteReboot = false;
  performSystemReset("remote MQTT command", true);
}

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

// ===== FUNKCJE LCD =====
void initLCD() {
  DBG.println("DBG: Initializing I2C (Wire)...");
  Wire.begin();

  DBG.println("DBG: Probing LCD at 0x27...");
  Wire.beginTransmission(0x27);
  byte error = Wire.endTransmission();

  if (error != 0) {
    lcdAvailable = false;
    DBG.println("DBG: LCD not found at 0x27");
    return;
  }

  DBG.println("DBG: LCD found at 0x27, init...");
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Stairs System");
  lcd.setCursor(0,1); lcd.print("Starting...");
  lcdAvailable = true;

  DBG.println("DBG: LCD init OK");
}

void updateLCD() {
  if (!lcdAvailable) return;
  
  // Pierwsza linia: czas (HH:MM)
  lcd.setCursor(0, 0);
  char timeStr[17];
  if (ntpTimeValid) {
    int h = dublinHours();
    int m = dublinMinutes();
    snprintf(timeStr, sizeof(timeStr), "Time: %02d:%02d     ", h, m);
  } else {
    snprintf(timeStr, sizeof(timeStr), "Time: --:--     ");
  }
  lcd.print(timeStr);
  
  // Druga linia: adres IP
  lcd.setCursor(0, 1);
  if (wifiReady()) {
    IPAddress ip = WiFi.localIP();
    char ipStr[17];
    snprintf(ipStr, sizeof(ipStr), "IP:%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    lcd.print(ipStr);
    // Wypełnij resztę spacjami aby wyczyścić poprzednią zawartość
    for (int i = strlen(ipStr); i < 16; i++) {
      lcd.print(" ");
    }
  } else if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi DHCP...    ");
  } else {
    lcd.print("No WiFi         ");
  }
}

// ===== SETUP =====
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

  // Sprzętowy watchdog chroni przed całkowitym zawieszeniem programu.
  startHardwareWatchdog();
  kickHardwareWatchdog();
  
  // Inicjalizacja LCD
  DBG.println("\nDBG: Initializing LCD...");
  initLCD();
  lastLcdReinit = millis();
  
  // Inicjalizacja WS2812
  DBG.println("\nDBG: Initializing WS2812...");
  strip.begin();
  strip.clear();
  strip.show();
  delay(5); // Powtorz te sama ramke po przerwie.
  strip.show();
  DBG.print("DBG: WS2812 Initialized - ");
  DBG.print(NUM_LEDS);
  DBG.print(" LEDs on ");
  DBG.print(NUM_STAIRS);
  DBG.println(" stairs");
  
  // Diagnostyka modułu WiFi
  DBG.print("DBG: WiFi firmware version: ");
  DBG.println(WiFi.firmwareVersion());

  // Połączenie WiFi
  DBG.println("DBG: Starting WiFi connection...");
  connectWiFi();
  
  // Konfiguracja MQTT
  DBG.println("\nDBG: Configuring MQTT client...");
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(4);
  DBG.println("DBG: MQTT client configured (keepalive=30s, socket timeout=4s)");
  
  // Uruchomienie NTP
  DBG.println("\nDBG: Starting NTP client...");
  timeClient.begin();
  timeClient.setTimeOffset(0); // WYMUSZENIE UTC - ZAWSZE OFFSET = 0
  DBG.println("DBG: NTP Client Started OK");
  DBG.println("DBG: timeClient.setTimeOffset(0) - WYMUSZONY UTC, offset ZAWSZE = 0");
  DBG.println("DBG: DST obliczany TYLKO ręcznie w currentTimeOffsetSeconds");
  DBG.println("DBG: Updating time from NTP server...");
  ntpTimeValid = wifiReady() && timeClient.forceUpdate();
  DBG.print("DBG: Initial NTP update: ");
  DBG.println(ntpTimeValid ? "SUCCESS" : "FAILED");
  DBG.print("DBG: Current time (UTC): ");
  DBG.print(timeClient.getHours());
  DBG.print(":");
  DBG.println(timeClient.getMinutes());
  DBG.print("DBG: UTC Epoch: ");
  DBG.println(utcEpoch());
  
  // INICJALIZACJA DST - oblicz offset ZARAZ PO STARCIE
  DBG.println("DBG: Initializing DST offset...");
  unsigned long utc = utcEpoch();
  currentTimeOffsetSeconds = ntpTimeValid ? getDublinOffsetSeconds(utc) : 0;
  DBG.print("DBG: Initial DST offset: ");
  DBG.print(currentTimeOffsetSeconds);
  DBG.print("s (");
  DBG.print(currentTimeOffsetSeconds / 3600);
  DBG.println("h)");
  DBG.print("DBG: Dublin time at startup: ");
  if (ntpTimeValid) {
    DBG.print(dublinHours());
    DBG.print(":");
    if (dublinMinutes() < 10) DBG.print("0");
    DBG.println(dublinMinutes());
  } else {
    DBG.println("not available");
  }

  // Połącz MQTT zanim opcjonalne czujniki zaczną dłuższą inicjalizację.
  if (wifiReady()) reconnectMQTT();

  // Czujniki są funkcją dodatkową; ich problemy nie mogą opóźniać startu sieci.
  DBG.println("DBG: Initializing sensors...");
  initSensors();
  DBG.println("DBG: Sensors initialized");
  
  DBG.println("==============================================");
  kickHardwareWatchdog();
  DBG.println("=== Setup Complete - Entering Main Loop ===");
  DBG.println("==============================================\n");
}

// ===== MAIN LOOP =====
void loop() {
  kickHardwareWatchdog();
  unsigned long now = millis(); // Wspólna zmienna dla całej funkcji
  // Odzyskiwanie LCD dziala rowniez bez polaczenia WiFi.
  if (now - lastLcdReinit >= lcdReinitInterval) {
    kickHardwareWatchdog();
    initLCD();
    updateLCD();
    kickHardwareWatchdog();
    now = millis();
    lastLcdReinit = now;
    lastLcdUpdate = now;
  }
  checkConnectivityRecovery(now);
  
  // Utrzymuj połączenie WiFi. Samo WL_CONNECTED nie wystarcza:
  // po zimnym starcie DHCP może jeszcze nie przydzielić adresu.
  if (!wifiReady()) {
    if (lastWifiReconnectAttempt == 0 || now - lastWifiReconnectAttempt >= wifiReconnectInterval) {
      lastWifiReconnectAttempt = now;
      DBG.println("\nDBG: WiFi not ready, reconnecting...");
      connectWiFi();
    }
    serviceDelay(50);
    kickHardwareWatchdog();
    return;
  }
  
  // Utrzymuj połączenie MQTT
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT disconnected, reconnecting...");
    reconnectMQTT();
  }
  if (mqttClient.connected() && !mqttClient.loop()) {
    DBG.println("DBG: MQTT loop detected a lost connection");
  }
  processPendingRemoteReboot();
  checkConnectivityRecovery(millis());
  
  // Aktualizuj czas z NTP - throttling co 30s
  static unsigned long lastNtpUpdate = 0;
  if (now - lastNtpUpdate >= 30000 || lastNtpUpdate == 0) {
    lastNtpUpdate = now;
    const bool ntpUpdated = ntpTimeValid ? timeClient.update() : timeClient.forceUpdate();
    if (ntpUpdated) {
      ntpTimeValid = true;
      DBG.println("DBG: NTP time updated");
    } else {
      DBG.println("DBG: NTP update failed; keeping previous state");
    }
  }
  
  // Oblicz offset DST dla Dublina - TYLKO RAZ NA MINUTE (throttling)
  static unsigned long lastDSTCheck = 0;
  if (ntpTimeValid && (now - lastDSTCheck >= 60000 || lastDSTCheck == 0)) {
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
  
  // Aktualizacja LCD co 10s
  if (now - lastLcdUpdate >= lcdUpdateInterval) {
    lastLcdUpdate = now;
    updateLCD();
  }
  
  // Informacja o działającym programie
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 10000) {
    lastHeartbeat = millis();
    DBG.println("\n=== Heartbeat ===");
    DBG.println("DBG: Program running...");
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
  
  kickHardwareWatchdog();
  serviceDelay(100);
}

// ===== INICJALIZACJA DS18B20 =====
void initSensors() {
  DBG.println("DBG: ========================================");
  DBG.println("DBG: Initializing DS18B20 temperature sensor");
  DBG.println("DBG: DHT is disabled in this firmware");
  DBG.println("DBG: ========================================");

  ds18b20.begin();
  serviceDelay(250);

  const int deviceCount = ds18b20.getDeviceCount();
  ds18b20Available = deviceCount > 0;

  if (ds18b20Available) {
    DBG.print("DBG: [DS18B20] OK - Found ");
    DBG.print(deviceCount);
    DBG.println(" device(s)");
  } else {
    DBG.println("DBG: [DS18B20] Not detected; will retry after failed cycles");
  }
}

// ===== POŁĄCZENIE WiFi =====
bool connectToSSID(const char* ssid, const char* pass) {
  if (ssid == nullptr || ssid[0] == '\0') return false;

  DBG.print("DBG: Connecting to SSID: ");
  DBG.println(ssid);

  // Zatrzymaj stare gniazda i poprzednią próbę połączenia.
  mqttClient.disconnect();
  espClient.stop();
  WiFi.disconnect();
  serviceDelay(300);

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, LOW);

  WiFi.begin(ssid, pass);
  const unsigned long started = millis();
  unsigned long lastLog = 0;

  while (millis() - started < wifiConnectTimeout) {
    kickHardwareWatchdog();
    const int status = WiFi.status();
    const IPAddress ip = WiFi.localIP();

    // Dopiero po uzyskaniu niezerowego adresu DHCP uznajemy sieć za gotową.
    if (status == WL_CONNECTED && !isZeroIP(ip)) {
      digitalWrite(LED_BLUE, HIGH);
      digitalWrite(LED_GREEN, LOW);

      DBG.println("DBG: WiFi and DHCP ready");
      DBG.print("DBG: SSID: "); DBG.println(WiFi.SSID());
      DBG.print("DBG: IP: "); DBG.println(ip);
      DBG.print("DBG: Gateway: "); DBG.println(WiFi.gatewayIP());
      DBG.print("DBG: DNS: "); DBG.println(WiFi.dnsIP());
      DBG.print("DBG: RSSI: "); DBG.print(WiFi.RSSI()); DBG.println(" dBm");
      return true;
    }

    if (millis() - lastLog >= 1000) {
      lastLog = millis();
      DBG.print("DBG: Waiting for WiFi/DHCP, status=");
      DBG.print(status);
      DBG.print(" IP=");
      DBG.println(ip);
      digitalWrite(LED_BLUE, !digitalRead(LED_BLUE));
    }
    serviceDelay(100);
  }

  DBG.println("DBG: WiFi/DHCP timeout");
  WiFi.disconnect();
  digitalWrite(LED_BLUE, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  return false;
}

void connectWiFi() {
  DBG.println("\n======================================");
  DBG.println("DBG: Starting WiFi connection process");
  DBG.println("======================================");

  DBG.print("DBG: Setting hostname: ");
  DBG.println(device_hostname);
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_PORTENTA_H7_M7) || defined(ARDUINO_PORTENTA_H7_M4)
  WiFi.setHostname(device_hostname);
#endif

  if (connectToSSID(ssid1, password1)) return;

  DBG.println("DBG: Primary network failed; trying fallback");
  if (connectToSSID(ssid2, password2)) return;

  DBG.println("DBG: No WiFi network is ready; next retry in 10 seconds");
}

// ===== PUBLIKACJA CZASU NTP =====
void publishNTPTime() {
  DBG.println("DBG: publishNTPTime() called");
  
  if (!wifiReady()) {
    DBG.println("DBG: WiFi not connected, skipping time publish");
    return;
  }
  
  if (!mqttClient.connected()) {
    DBG.println("DBG: MQTT not connected, skipping time publish");
    return;
  }

  if (!ntpTimeValid) {
    DBG.println("DBG: NTP time is not valid yet, skipping time publish");
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
  
  bool result = safeMqttPublish(mqtt_topic_time, timeInfo.c_str());
  
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
  
  if (!wifiReady()) {
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
  
  bool result = safeMqttPublish(mqtt_topic_led_status, statusStr.c_str());
  
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
  
  if (!wifiReady()) {
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
  
  bool result = safeMqttPublish(mqtt_topic_output, wifiInfo.c_str());
  
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
  if (!wifiReady()) return;

  static unsigned long lastAttempt = 0;
  const unsigned long now = millis();

  // Pierwsza próba od razu, kolejne nie częściej niż co 5 sekund.
  if (lastAttempt != 0 && now - lastAttempt < 5000UL) return;
  lastAttempt = now;

  DBG.println("\n======================================");
  DBG.println("DBG: Attempting MQTT connection...");
  DBG.print("DBG: MQTT Server: ");
  DBG.print(mqtt_server);
  DBG.print(":");
  DBG.println(mqtt_port);

  // Stabilny Client ID powoduje, że broker usuwa ewentualne stare,
  // zawieszone połączenie tego samego urządzenia.
  const char* clientId = device_name;

  const bool connected = mqttClient.connect(
    clientId,
    mqtt_user,
    mqtt_password,
    mqtt_topic_watchdog_status,
    0,
    true,
    "offline"
  );

  if (connected) {
    DBG.println("DBG: MQTT Connected Successfully!");

    safeMqttPublish(mqtt_topic_watchdog_status, "online", true);

    DBG.print("DBG: Subscribing to topic: ");
    DBG.println(mqtt_topic_input);
    const bool inputSubResult = mqttClient.subscribe(mqtt_topic_input, 1);
    DBG.println(inputSubResult ? "DBG: Light subscription request sent"
                               : "DBG: Light subscription FAILED");

    DBG.print("DBG: Subscribing to topic: ");
    DBG.println(mqtt_topic_watchdog_reboot);
    const bool rebootSubResult = mqttClient.subscribe(mqtt_topic_watchdog_reboot, 1);
    DBG.println(rebootSubResult ? "DBG: Watchdog subscription request sent"
                                : "DBG: Watchdog subscription FAILED");

    safeMqttPublish(mqtt_topic_output, (String(device_name) + " Online").c_str());
    publishWiFiInfo();
    publishLightStatus();
    DBG.println("======================================");
  } else {
    DBG.print("DBG: MQTT Connection FAILED, rc=");
    DBG.println(mqttClient.state());
    DBG.println("DBG: rc -4=timeout, -3=lost, -2=TCP failed, 4=bad credentials, 5=unauthorized");
    espClient.stop();
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
  
  message.trim();

  DBG.println("\n=== MQTT Message Received ===");
  DBG.print("DBG: Topic: ");
  DBG.println(topic);
  DBG.print("DBG: Message: ");
  DBG.println(message);
  DBG.println("=============================");

  if (strcmp(topic, mqtt_topic_watchdog_reboot) == 0) {
    if (message.equalsIgnoreCase("reboot") || message == "1" ||
        message.equalsIgnoreCase("true")) {
      DBG.println("DBG: Remote reboot command accepted");
      pendingRemoteReboot = true;
    } else if (message.length() > 0) {
      DBG.println("DBG: Remote reboot command ignored - expected reboot/1/true");
    }
    return;
  }

  if (strcmp(topic, mqtt_topic_input) != 0) {
    DBG.println("DBG: Message ignored - unsupported topic");
    return;
  }
  
  // Parsowanie wiadomości (format: "day=1" lub "day=0")
  if (message.startsWith("day=")) {
    const String dayText = message.substring(4);
    if (dayText != "0" && dayText != "1") {
      DBG.println("DBG: Invalid day command - expected day=0 or day=1");
      return;
    }
    int dayValue = dayText.toInt();
    isDayTime = (dayValue == 1);
    DBG.print("DBG: Day/Night mode updated to: ");
    DBG.println(isDayTime ? "DAY (lights off)" : "NIGHT (lights on)");
    const LightMode previousMode = currentMode;
    updateLightMode(); // Zmiana trybu sama wysyla ramki.
    if (currentMode == previousMode) {
      applyLightMode(); // Powtorzona komenda ponawia aktualny stan.
    }
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
  if (!ntpTimeValid) {
    DBG.println("DBG: Time not valid yet - keeping safe light state");
    if (isDayTime && currentMode != MODE_OFF) {
      currentMode = MODE_OFF;
      applyLightMode();
    }
    return;
  }

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
    
    safeMqttPublish(mqtt_topic_output, ("Mode: " + modeStr).c_str());
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
        strip.setPixelColor(ledIndex, strip.Color(0, 26, 26)); // Cyjan ok. 10%
      }
      break;
      
    case MODE_OFF:
    default:
      // Wszystkie LEDy wyłączone
      strip.clear();
      break;
  }
  
  strip.show();
  delay(5); // Powtorz te sama ramke po przerwie.
  strip.show();
  DBG.println("DBG: Light mode applied and strip updated");
}

// ===== ODCZYT I PUBLIKACJA DS18B20 =====
void readAndPublishSensors() {
  static float lastValidTempDS = NAN;
  static int ds18ErrorStreak = 0;

  DBG.println("DBG: [DS18B20] Read cycle start");

  if (!ds18b20Available) {
    DBG.println("DBG: [DS18B20] Sensor unavailable - retrying detection");
    initSensors();
    return;
  }

  ds18b20.requestTemperatures();
  serviceDelay(800); // 12-bit conversion; MQTT and watchdog stay serviced.

  const float temp = ds18b20.getTempCByIndex(0);
  const bool valid = temp != DEVICE_DISCONNECTED_C && temp > -50.0f && temp < 100.0f;

  if (valid) {
    lastValidTempDS = temp;
    ds18ErrorStreak = 0;

    DBG.print("DBG: [DS18B20] Temperature: ");
    DBG.print(temp);
    DBG.println(" C");

    if (mqttClient.connected()) {
      const String tempStr = String(temp, 2);
      const bool published = safeMqttPublish(mqtt_topic_temp_ds, tempStr.c_str());
      DBG.println(published ? "DBG: [DS18B20] MQTT publish OK"
                            : "DBG: [DS18B20] MQTT publish skipped/failed");
    }
    return;
  }

  ds18ErrorStreak++;
  DBG.print("DBG: [DS18B20] Invalid reading, streak=");
  DBG.println(ds18ErrorStreak);

  if (ds18ErrorStreak >= 3) {
    ds18b20Available = false;
    ds18ErrorStreak = 0;
    initSensors();
  }

  if (!isnan(lastValidTempDS) && mqttClient.connected()) {
    const String tempStr = String(lastValidTempDS, 2);
    safeMqttPublish(mqtt_topic_temp_ds, tempStr.c_str());
  }
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
  
  bool result = safeMqttPublish(mqtt_topic_output, status.c_str());
  if (result) {
    DBG.println("DBG: Status published successfully to MQTT");
  } else {
    DBG.println("DBG: FAILED to publish status to MQTT");
  }
  
  // Publikuj również info o WiFi
  publishWiFiInfo();
}
