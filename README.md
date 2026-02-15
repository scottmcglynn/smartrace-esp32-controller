# SmartRace ESP32-C6 Light Controller

Control Govee network lights, onboard RGB LED, and LCD display based on SmartRace slot car racing events. Displays track conditions (weather) and race status through color-coded lighting.

## Features

- **SmartRace Integration**: Receives events via HTTP POST from SmartRace Data Interface
- **Govee Light Control**: Controls H7020 (and compatible) lights via LAN UDP protocol
- **LCD Display**: 1.47" ST7789 320x172 showing race state and weather info
- **RGB LED**: NeoPixel LED mirrors Govee light colors
- **Relay Control**: GPIO 23 pulses HIGH for 5 seconds on race stop/end events
- **Web Interface**: Manual testing UI at `http://<device-ip>/`
- **mDNS Support**: Access via `http://src.local`

## Light Colors

| Condition | Color |
|-----------|-------|
| Race Running (no weather) | White |
| Track Dry | White |
| About to Rain | Purple |
| Track Wet | Blue |
| Track Drying | Green |
| Race Stopped/Suspended | Red |

## Hardware Requirements

- Waveshare ESP32-C6 with 1.47" LCD
- Govee H7020 network light (or compatible)
- Relay module on GPIO 23

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

## Building & Uploading

Requires Arduino CLI with ESP32 core installed.

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c6 .

# Upload
arduino-cli upload --fqbn esp32:esp32:esp32c6 --port /dev/cu.usbmodem14101 .

# Monitor serial output
arduino-cli monitor -p /dev/cu.usbmodem14101 -c baudrate=115200
```

## Web Interface

Access the web UI at `http://<device-ip>/` or `http://src.local/` to:

- View connection status
- Test race status buttons (Starting, Running, Stopped, Ended)
- Test weather condition buttons (Dry, About to Rain, Wet, Drying)
- Turn lights off

Settings page at `/settings` allows configuring the Govee light IP address.

## Behavior Notes

- **Race Stopped/Ended**: Lights turn red, relay pulses HIGH for 5 seconds then returns to LOW. Weather condition is stored and will display when race resumes.
- **Race Starting**: Weather condition is cleared for a fresh start.
- **Relay**: Default LOW. Pulses HIGH for 5 seconds when race is stopped or ended.

## License

Based on scottmcglynn/smartrace_events Flutter project.
