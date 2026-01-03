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
