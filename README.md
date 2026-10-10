# Smart Socket (ESP32) – Graduation Thesis

A 2-channel smart power socket built on an ESP32. It measures electrical power with a PZEM-004T, can be switched locally with buttons or remotely over MQTT, and keeps history and schedules in Firebase.

> My part: the whole device – firmware, circuit design and enclosure. The web/mobile app was built by a teammate and is not included in this repository.

## Features
- 2 relay outputs, each with a local push button and status LED
- Voltage, current, power and energy readings from a **PZEM-004T v3** (UART)
- **MQTT** control and telemetry
- **Firebase Realtime Database** (REST): latest values, history, schedules
- Weekly **schedules** and countdown **timers**, time from NTP
- Threshold **alerts**
- **Wi-Fi setup portal**: on first boot (or when the saved network fails) the socket opens an access point with a captive page where you enter your Wi-Fi; credentials are stored in flash (`Preferences`)

## Hardware
| Part | Pins (ESP32) |
|---|---|
| Relay 1 / Relay 2 | GPIO 25 / 26 (active LOW) |
| Button 1 / Button 2 | GPIO 33 / 35 |
| LED 1 / LED 2 | GPIO 32 / 34 |
| PZEM-004T (UART) | RX 19, TX 18 |

## Libraries (Arduino IDE)
PubSubClient, PZEM004Tv30, ArduinoJson, plus the ESP32 core (WiFi, WebServer, DNSServer, HTTPClient, Preferences).

## Build
1. Install the ESP32 board package and the libraries above.
2. In `smart_socket/`, copy `secrets.h.example` to `secrets.h` and fill in your own Firebase host/secret and setup-AP password. `secrets.h` is git-ignored.
3. Open `smart_socket/smart_socket.ino`, select your ESP32 board, upload.
4. Connect to the `SmartSocket-Setup` Wi-Fi, open `192.168.4.1` and enter your home Wi-Fi.

## Notes / known issues
- GPIO 34 and 35 are input-only on the ESP32, so an LED on GPIO 34 cannot be driven; move LED 2 to an output-capable pin.
- The Firebase database secret is used for REST calls; for a production device prefer Firebase Authentication with strict database rules.
- Mains voltage is dangerous. Build and test only with proper isolation and protection.
