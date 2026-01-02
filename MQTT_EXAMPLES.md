# Przykłady MQTT - Portenta H7 Stairs Controller

Zbiór przykładowych komend i integracji z systemami automatyki domowej.

## 📡 Testowanie przez Mosquitto CLI

### Instalacja Mosquitto (Windows)
```bash
# Pobierz z: https://mosquitto.org/download/
# Lub zainstaluj przez Chocolatey:
choco install mosquitto
```

### Podstawowe Komendy

#### Wysłanie czasu zachodu słońca
```bash
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "sunset:18:30"
```

#### Wysłanie czasu wschodu słońca
```bash
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "sunrise:06:45"
```

#### Zmiana godzin trybu nocnego
```bash
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "night:22:00-06:00"
```

#### Sprawdzenie statusu
```bash
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "status"
```

#### Nasłuchiwanie wszystkich wiadomości
```bash
# Wszystkie topiki
mosquitto_sub -h 192.168.1.109 -t "#" -v

# Tylko topiki schodów
mosquitto_sub -h 192.168.1.109 -t "Stairs/#" -v

# Tylko czujniki
mosquitto_sub -h 192.168.1.109 -t "HallDown/#" -v
```

## 🏠 Home Assistant

### Configuration.yaml

```yaml
mqtt:
  sensor:
    # Temperatura z DHT22
    - name: "Schody - Temperatura DHT22"
      state_topic: "HallDown/Dht/Temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      icon: mdi:thermometer
      
    # Wilgotność z DHT22
    - name: "Schody - Wilgotność"
      state_topic: "HallDown/Dht/Humidity"
      unit_of_measurement: "%"
      device_class: humidity
      state_class: measurement
      icon: mdi:water-percent
      
    # Temperatura z DS18B20
    - name: "Schody - Temperatura DS18B20"
      state_topic: "HallDown/DS18B20/Temperature"
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      icon: mdi:thermometer-water
      
    # Status oświetlenia
    - name: "Schody - Status"
      state_topic: "Stairs/Light/output"
      icon: mdi:stairs

# Automatyzacje
automation:
  # Aktualizacja czasu zachodu słońca
  - id: stairs_sunset_update
    alias: "Schody - Aktualizacja Zachodu"
    trigger:
      - platform: sun
        event: sunset
        offset: "-00:30:00"  # 30 min przed zachodem
    action:
      - service: mqtt.publish
        data:
          topic: "Stairs/Light/input"
          payload: >
            sunset:{{ (as_timestamp(state_attr('sun.sun', 'next_setting')) | timestamp_custom('%H:%M')) }}
  
  # Aktualizacja czasu wschodu słońca
  - id: stairs_sunrise_update
    alias: "Schody - Aktualizacja Wschodu"
    trigger:
      - platform: sun
        event: sunrise
        offset: "-00:30:00"  # 30 min przed wschodem
    action:
      - service: mqtt.publish
        data:
          topic: "Stairs/Light/input"
          payload: >
            sunrise:{{ (as_timestamp(state_attr('sun.sun', 'next_rising')) | timestamp_custom('%H:%M')) }}
  
  # Dzienna aktualizacja (na wszelki wypadek)
  - id: stairs_daily_update
    alias: "Schody - Aktualizacja Dzienna"
    trigger:
      - platform: time
        at: "02:00:00"
    action:
      - service: mqtt.publish
        data:
          topic: "Stairs/Light/input"
          payload: >
            sunset:{{ (as_timestamp(state_attr('sun.sun', 'next_setting')) | timestamp_custom('%H:%M')) }}
      - delay: '00:00:02'
      - service: mqtt.publish
        data:
          topic: "Stairs/Light/input"
          payload: >
            sunrise:{{ (as_timestamp(state_attr('sun.sun', 'next_rising')) | timestamp_custom('%H:%M')) }}

# Karta Lovelace
lovelace:
  dashboards:
    stairs:
      mode: yaml
      title: Schody
      icon: mdi:stairs
      show_in_sidebar: true
      filename: stairs.yaml
```

### stairs.yaml (Lovelace)

```yaml
title: Sterowanie Schodami
views:
  - title: Główny
    cards:
      # Status
      - type: entities
        title: Status Oświetlenia
        entities:
          - entity: sensor.schody_status
            name: Aktualny Tryb
      
      # Czujniki
      - type: horizontal-stack
        cards:
          - type: sensor
            entity: sensor.schody_temperatura_dht22
            name: Temp DHT22
            graph: line
          - type: sensor
            entity: sensor.schody_temperatura_ds18b20
            name: Temp DS18B20
            graph: line
      
      - type: sensor
        entity: sensor.schody_wilgotnosc
        name: Wilgotność
        graph: line
      
      # Kontrola
      - type: entities
        title: Kontrola
        entities:
          - type: button
            name: Sprawdź Status
            tap_action:
              action: call-service
              service: mqtt.publish
              service_data:
                topic: "Stairs/Light/input"
                payload: "status"
            icon: mdi:information
      
      # Historia
      - type: history-graph
        title: Historia Temperatury
        hours_to_show: 24
        entities:
          - entity: sensor.schody_temperatura_dht22
          - entity: sensor.schody_temperatura_ds18b20
      
      - type: history-graph
        title: Historia Wilgotności
        hours_to_show: 24
        entities:
          - entity: sensor.schody_wilgotnosc
```

## 🔵 Node-RED

### Flow 1: Automatyczna Aktualizacja Czasów Słońca

```json
[
    {
        "id": "sun_times_flow",
        "type": "tab",
        "label": "Schody - Aktualizacja Czasów",
        "disabled": false,
        "info": ""
    },
    {
        "id": "inject_daily",
        "type": "inject",
        "z": "sun_times_flow",
        "name": "Codziennie o 2:00",
        "props": [{"p": "payload"}],
        "repeat": "",
        "crontab": "00 02 * * *",
        "once": true,
        "topic": "",
        "payload": "",
        "payloadType": "date",
        "x": 150,
        "y": 100,
        "wires": [["get_sun_times"]]
    },
    {
        "id": "get_sun_times",
        "type": "function",
        "z": "sun_times_flow",
        "name": "Oblicz Wschód/Zachód",
        "func": "// API: https://sunrise-sunset.org/api\n// Współrzędne - dostosuj do swojej lokalizacji\nconst lat = 52.2297;  // Przykład: Warszawa\nconst lng = 21.0122;\n\nconst url = `https://api.sunrise-sunset.org/json?lat=${lat}&lng=${lng}&formatted=0`;\n\nmsg.url = url;\nreturn msg;",
        "outputs": 1,
        "x": 380,
        "y": 100,
        "wires": [["http_request"]]
    },
    {
        "id": "http_request",
        "type": "http request",
        "z": "sun_times_flow",
        "name": "Pobierz Dane",
        "method": "GET",
        "ret": "obj",
        "url": "",
        "x": 590,
        "y": 100,
        "wires": [["parse_times"]]
    },
    {
        "id": "parse_times",
        "type": "function",
        "z": "sun_times_flow",
        "name": "Parsuj Czasy",
        "func": "const data = msg.payload.results;\n\n// Konwersja na lokalny czas (UTC+1)\nconst sunrise = new Date(data.sunrise);\nconst sunset = new Date(data.sunset);\n\n// Format HH:MM\nconst sunriseTime = sunrise.toLocaleTimeString('pl-PL', {\n    hour: '2-digit',\n    minute: '2-digit',\n    hour12: false\n});\n\nconst sunsetTime = sunset.toLocaleTimeString('pl-PL', {\n    hour: '2-digit',\n    minute: '2-digit',\n    hour12: false\n});\n\nreturn [\n    {payload: `sunrise:${sunriseTime}`},\n    {payload: `sunset:${sunsetTime}`}\n];",
        "outputs": 2,
        "x": 790,
        "y": 100,
        "wires": [["mqtt_sunrise"], ["mqtt_sunset"]]
    },
    {
        "id": "mqtt_sunrise",
        "type": "mqtt out",
        "z": "sun_times_flow",
        "name": "MQTT Wschód",
        "topic": "Stairs/Light/input",
        "broker": "mqtt_broker",
        "x": 1000,
        "y": 80,
        "wires": []
    },
    {
        "id": "mqtt_sunset",
        "type": "mqtt out",
        "z": "sun_times_flow",
        "name": "MQTT Zachód",
        "topic": "Stairs/Light/input",
        "broker": "mqtt_broker",
        "x": 1000,
        "y": 120,
        "wires": []
    }
]
```

### Flow 2: Monitorowanie Czujników

```json
[
    {
        "id": "sensors_flow",
        "type": "tab",
        "label": "Schody - Monitorowanie",
        "disabled": false
    },
    {
        "id": "mqtt_temp_dht",
        "type": "mqtt in",
        "z": "sensors_flow",
        "name": "Temp DHT22",
        "topic": "HallDown/Dht/Temperature",
        "broker": "mqtt_broker",
        "x": 130,
        "y": 100,
        "wires": [["temp_gauge", "temp_chart"]]
    },
    {
        "id": "mqtt_humidity",
        "type": "mqtt in",
        "z": "sensors_flow",
        "name": "Wilgotność",
        "topic": "HallDown/Dht/Humidity",
        "broker": "mqtt_broker",
        "x": 130,
        "y": 160,
        "wires": [["humidity_gauge"]]
    },
    {
        "id": "mqtt_temp_ds",
        "type": "mqtt in",
        "z": "sensors_flow",
        "name": "Temp DS18B20",
        "topic": "HallDown/DS18B20/Temperature",
        "broker": "mqtt_broker",
        "x": 140,
        "y": 220,
        "wires": [["temp_gauge", "temp_chart"]]
    },
    {
        "id": "temp_gauge",
        "type": "ui_gauge",
        "z": "sensors_flow",
        "name": "Temperatura",
        "group": "sensors_group",
        "x": 390,
        "y": 160,
        "wires": []
    },
    {
        "id": "temp_chart",
        "type": "ui_chart",
        "z": "sensors_flow",
        "name": "Wykres Temp",
        "group": "sensors_group",
        "x": 390,
        "y": 200,
        "wires": []
    }
]
```

## 📱 Domoticz

### Wirtualne Urządzenia

#### 1. Dodaj wirtualny czujnik temperatury
```
Setup → Hardware → Create Virtual Sensors
Type: Temp
Name: Schody DHT22
```

#### 2. Skrypt Lua (script_device_mqtt_stairs.lua)
```lua
return {
    on = {
        ['mqtt'] = {
            'HallDown/Dht/Temperature',
            'HallDown/Dht/Humidity',
            'HallDown/DS18B20/Temperature'
        }
    },
    execute = function(domoticz, item)
        if item.topic == 'HallDown/Dht/Temperature' then
            domoticz.devices('Schody DHT22').updateTemperature(tonumber(item.payload))
        elseif item.topic == 'HallDown/Dht/Humidity' then
            domoticz.devices('Schody DHT22').updateHumidity(tonumber(item.payload))
        elseif item.topic == 'HallDown/DS18B20/Temperature' then
            domoticz.devices('Schody DS18B20').updateTemperature(tonumber(item.payload))
        end
    end
}
```

## 🐍 Python Script

### Prosty Monitor w Konsoli

```python
#!/usr/bin/env python3
"""
Monitor MQTT dla Portenta H7 Stairs Controller
"""

import paho.mqtt.client as mqtt
import json
from datetime import datetime

MQTT_BROKER = "192.168.1.109"
MQTT_PORT = 1883

def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT Broker! (code: {rc})")
    client.subscribe("HallDown/#")
    client.subscribe("Stairs/#")

def on_message(client, userdata, msg):
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    topic = msg.topic
    payload = msg.payload.decode()
    
    print(f"[{timestamp}] {topic}: {payload}")
    
    # Parsowanie i wyświetlanie
    if "Temperature" in topic:
        print(f"  └─ 🌡️  Temperatura: {payload}°C")
    elif "Humidity" in topic:
        print(f"  └─ 💧 Wilgotność: {payload}%")
    elif "output" in topic:
        print(f"  └─ 💡 Status: {payload}")

def main():
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    print("Connecting to MQTT Broker...")
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    
    print("Listening for messages... (Ctrl+C to exit)")
    client.loop_forever()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nDisconnected!")
```

### Skrypt Aktualizacji Czasów Słońca

```python
#!/usr/bin/env python3
"""
Automatyczna aktualizacja czasów wschodu/zachodu słońca
Uruchamiaj przez cron codziennie o 2:00
"""

import requests
import paho.mqtt.client as mqtt
from datetime import datetime

# Konfiguracja
LAT = 52.2297  # Warszawa - dostosuj do swojej lokalizacji
LNG = 21.0122
MQTT_BROKER = "192.168.1.109"
MQTT_TOPIC = "Stairs/Light/input"

def get_sun_times(lat, lng):
    """Pobiera czasy wschodu i zachodu z API"""
    url = f"https://api.sunrise-sunset.org/json?lat={lat}&lng={lng}&formatted=0"
    response = requests.get(url)
    data = response.json()
    
    if data['status'] == 'OK':
        sunrise = datetime.fromisoformat(data['results']['sunrise'].replace('Z', '+00:00'))
        sunset = datetime.fromisoformat(data['results']['sunset'].replace('Z', '+00:00'))
        
        # Konwersja na lokalny czas (UTC+1)
        sunrise = sunrise.astimezone()
        sunset = sunset.astimezone()
        
        return sunrise.strftime("%H:%M"), sunset.strftime("%H:%M")
    return None, None

def publish_times(sunrise, sunset):
    """Publikuje czasy przez MQTT"""
    client = mqtt.Client()
    client.connect(MQTT_BROKER, 1883, 60)
    
    client.publish(MQTT_TOPIC, f"sunrise:{sunrise}")
    print(f"Published: sunrise:{sunrise}")
    
    client.publish(MQTT_TOPIC, f"sunset:{sunset}")
    print(f"Published: sunset:{sunset}")
    
    client.disconnect()

def main():
    print("Fetching sun times...")
    sunrise, sunset = get_sun_times(LAT, LNG)
    
    if sunrise and sunset:
        print(f"Sunrise: {sunrise}")
        print(f"Sunset: {sunset}")
        publish_times(sunrise, sunset)
        print("Successfully updated!")
    else:
        print("Failed to fetch sun times!")

if __name__ == "__main__":
    main()
```

### Instalacja Skryptu Python

```bash
# Zainstaluj wymagane biblioteki
pip install paho-mqtt requests

# Dodaj do cron (Linux)
crontab -e
# Dodaj linię:
0 2 * * * /usr/bin/python3 /path/to/update_sun_times.py

# Lub uruchom jako scheduled task (Windows)
# Task Scheduler → Create Basic Task → Daily → 2:00 AM
```

## 🧪 Testowanie Integracji

### Test 1: Ręczne Ustawienie Czasów
```bash
# Ustaw zachód na za 5 minut
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "sunset:HH:MM"

# Sprawdź czy tryb się zmienił
mosquitto_sub -h 192.168.1.109 -t "Stairs/Light/output" -v
```

### Test 2: Sprawdzenie Czujników
```bash
# Nasłuchuj czujniki przez 2 minuty
timeout 120 mosquitto_sub -h 192.168.1.109 -t "HallDown/#" -v
```

### Test 3: Symulacja Trybu Nocnego
```bash
# Ustaw tryb nocny na teraz
mosquitto_pub -h 192.168.1.109 -t "Stairs/Light/input" -m "night:HH:MM-23:59"
```

---

**Więcej przykładów**: Zobacz dokumentację MQTT na https://mqtt.org/
