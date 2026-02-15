# SmartRace ESP32-C6 Light Controller

Control Govee network lights, onboard RGB LED, LCD display, and a relay based on SmartRace slot car racing events. Displays track conditions (weather) and race status through color-coded lighting.

## Features

- **SmartRace Integration**: Receives events via HTTP POST from SmartRace Data Interface
- **Govee Light Control**: Controls H7020 (and compatible) lights via LAN UDP protocol
- **LCD Display**: 1.47" ST7789 320x172 with flicker-free partial updates showing race state, weather, relay status, Govee connection, and IP:port
- **RGB LED**: NeoPixel LED mirrors Govee light colors
- **Relay Control**: GPIO 23 (open drain) pulses HIGH for 5 seconds on race stop/end events
- **Web Interface**: Manual testing UI at `http://<device-ip>/`
- **mDNS Support**: Access via `http://src.local`
- **Duplicate Event Filtering**: Ignores repeated SmartRace events when already in that state

## Display Layout

The LCD uses a split layout with flicker-free partial updates (no full-screen redraws):

- **Left panel** (200px): Large color-coded status indicator with word-wrapped text. IP:port shown at the bottom within the color band.
- **Right panel** (120px): Race state, weather condition, Govee connection, and relay status. Labels are drawn once at boot; only values are updated.

## Light Colors

| Condition | Color |
|-----------|-------|
| Race Running (no weather) | White |
| Track Dry | White |
| About to Rain | Purple |
| Track Wet | Blue |
| Track Drying | Green |
| Race Stopped/Ended | Red |

## Hardware

- Waveshare ESP32-C6 with 1.47" LCD
- Govee H7020 network light (or compatible)
- Relay module on GPIO 23 (open drain, requires external pull-up)

### Pin Configuration

| Pin | Function |
|-----|----------|
| 5 | LCD MISO |
| 6 | LCD MOSI |
| 7 | LCD SCLK |
| 8 | RGB LED (NeoPixel) |
| 14 | LCD CS |
| 15 | LCD DC |
| 21 | LCD RST |
| 22 | LCD Backlight |
| 23 | Relay |

## Configuration

Edit these values at the top of the `.ino` file:

```cpp
const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* MDNS_HOSTNAME = "src";
const int RELAY_PIN = 23;
String GOVEE_NETWORK_IP = "";  // Leave empty for auto-discovery
```

## SmartRace Setup

Configure SmartRace Data Interface to POST events to:
```
http://<ESP32_IP>:8080/
```
or
```
http://src.local:8080/
```

### Supported Events

| Event Type | Values |
|------------|--------|
| `event.change_status` | `starting`, `running`, `restarting`, `suspended`, `ended` |
| `events.weather_update` | `about_to_rain`, `about_to_dry_up` |
| `events.weather_change` | `wet`, `dry` |

### External Trigger

To trigger a race stop from another device (e.g. ESP8266):
```json
POST http://<ESP32_IP>:8080/
{"event_type":"event.change_status","event_data":{"new":"suspended"}}
```

## Building & Uploading

Requires Arduino CLI with ESP32 core installed.

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c6 .

# Upload (port may vary)
arduino-cli upload --fqbn esp32:esp32:esp32c6 --port /dev/cu.usbmodem14101 .

# Monitor serial output
arduino-cli monitor -p /dev/cu.usbmodem14101 -c baudrate=115200
```

## Web Interface

Access the web UI at `http://<device-ip>/` or `http://src.local/` to:

- Test race status buttons (Starting, Running, Stopped, Ended)
- Test weather condition buttons (Dry, About to Rain, Wet, Drying)
- Turn lights off
- Manually pulse the relay for 5 seconds

Settings page at `/settings` allows configuring the Govee light IP address.

## Behavior

- **Race Stopped/Ended**: Lights turn red, relay pulses HIGH for 5 seconds then returns to LOW. Weather condition is stored and displays when race resumes.
- **Race Starting**: Weather condition is cleared for a fresh start.
- **Relay**: Default LOW (open drain). Pulses HIGH for 5 seconds when race is stopped or ended. Display updates when timer expires.

## Dependencies

- WiFi.h
- WebServer.h
- ArduinoJson.h
- Preferences.h
- ESPmDNS.h
- WiFiUdp.h
- SPI.h
- Adafruit_NeoPixel.h
