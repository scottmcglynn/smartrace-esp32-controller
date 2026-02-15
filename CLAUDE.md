# Claude Context for SmartRace ESP32 Controller

## Project Overview
Light controller for SmartRace slot car racing software. Controls Govee network lights, onboard RGB LED, and LCD display based on race events and weather conditions.

## Current Hardware (ESP32-C6 with LCD)
- **Board**: Waveshare ESP32-C6 with 1.47" LCD
- **Display**: ST7789 320x172 (landscape mode)
- **RGB LED**: NeoPixel on GPIO 8
- **Govee H7020**: Network lights via UDP port 4001
- **Relay**: GPIO 23 for 5V rail control (open drain mode)

### ESP32-C6 LCD Pin Configuration
```
LCD_MISO    = 5
LCD_MOSI    = 6
LCD_SCLK    = 7
LCD_CS      = 14
LCD_DC      = 15
LCD_RST     = 21
LCD_BL      = 22
RGB_LED_PIN = 8
```

## Build & Upload (ESP32-C6)
```bash
arduino-cli compile --fqbn esp32:esp32:esp32c6 .
arduino-cli upload --fqbn esp32:esp32:esp32c6 --port /dev/cu.usbmodem14401 .
arduino-cli monitor -p /dev/cu.usbmodem14401 -c baudrate=115200
```

## Restoring ESP32-S2 Version (No LCD)
If you need the original ESP32-S2 version without LCD/RGB LED:

1. Use git to restore: `git checkout <commit-before-lcd-changes> -- claude_smartrace_esp32_controller.ino`
2. Or remove these from the current sketch:
   - Remove `#include <SPI.h>` and `#include <Adafruit_NeoPixel.h>`
   - Remove all LCD_* defines and functions
   - Remove rgbLed code
   - Remove `updateDisplayStatus()` calls
   - Change `updateLights()` to only call `setNetworkLight(r, g, b)`

3. Build for S2:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s2 .
arduino-cli upload --fqbn esp32:esp32:esp32s2 --port /dev/cu.usbserial-14320 .
```

## Key Architecture

### Dual Web Servers
- **Port 80**: Web UI for manual testing
- **Port 8080**: SmartRace Data Interface API (receives POST events)

### State Management
- `currentRaceState`: RACE_NONE, RACE_STARTING, RACE_RUNNING, RACE_STOPPED, RACE_ENDED
- `currentCondition`: TRACK_NONE, TRACK_DRY, TRACK_ABOUT_TO_RAIN, TRACK_WET, TRACK_DRYING

### Relay Logic
- Pin mode: OUTPUT_OPEN_DRAIN
- Default state: LOW
- Race stopped/ended: pulses HIGH for 5 seconds, then returns to LOW
- Controlled via `updateLights()` on `event.change_status` events
- Manual trigger available via web UI ("Relay 5s" button)

### Light Priority Logic (in updateLights())
1. Race stopped/ended → RED + relay pulse HIGH for 5s (weather stored but not shown)
2. Race running + weather set → Show weather color
3. Race running + no weather → WHITE
4. No race state + weather set → Show weather color (web testing)

### Color Mapping
| Condition | RGB | RGB565 |
|-----------|-----|--------|
| Dry | WHITE (255, 255, 255) | 0xFFFF |
| About to Rain | PURPLE (255, 0, 255) | 0xF81F |
| Wet | BLUE (0, 0, 255) | 0x001F |
| Drying | GREEN (0, 255, 0) | 0x07E0 |
| Race Stopped | RED (255, 0, 0) | 0xF800 |

## SmartRace Event Types
- `events.weather_update`: "about_to_rain", "about_to_dry_up"
- `events.weather_change`: "wet", "dry"
- `event.change_status`: "suspended", "running", "restarting", "ended", "starting"

## Display Driver Notes
The LCD uses direct SPI control (not a library). Code is based on Waveshare examples in `arduino/examples/LVGL_Arduino/Display_ST7789.cpp`.

Key display functions:
- `LCD_Init()` - Initialize ST7789 with landscape orientation (MADCTL 0x70)
- `LCD_Clear(color)` - Fill screen with color
- `LCD_FillRect(x, y, w, h, color)` - Draw filled rectangle
- `LCD_DrawString(x, y, str, color, bg, size)` - Draw text (8x8 font)

Landscape mode settings:
- Memory access control register 0x36 = 0x70 (landscape)
- LCD_OFFSET_X = 0, LCD_OFFSET_Y = 34
- Width = 320, Height = 172

## Dependencies
- WiFi.h
- WebServer.h
- ArduinoJson.h
- Preferences.h
- ESPmDNS.h
- WiFiUdp.h
- SPI.h (for LCD)
- Adafruit_NeoPixel.h (for RGB LED)

## Session Notes (2025-02-07)
- Upgraded from ESP32-S2 to ESP32-C6 board with 1.47" LCD
- Added LCD display using direct SPI (from Waveshare examples in `arduino/` folder)
- Added NeoPixel RGB LED on GPIO 8 that mirrors Govee light colors
- Display shows: large status indicator (left), race/weather info (right)
- Landscape mode enabled for better readability
- Lesson: Use simple SPI from project examples, not complex external libraries

## Session Notes (2026-02-14)
- Fixed relay pin polarity: LOW by default, pulses HIGH for 5s on race stop/end
- Fixed relay not responding to race stop events (was looking for non-existent `event.step`/`events.step`)
- Relay now driven by race state via `updateLights()` on `event.change_status` with "suspended"/"ended"
- Reversed display orientation (MADCTL 0x70 → 0xB0)
- Removed step event handling from SmartRace API and web UI

## Session Notes (2026-02-14 #2)
- Reversed display orientation again (MADCTL 0xB0 → 0x70)
- Set relay pin to OUTPUT_OPEN_DRAIN mode
- Added duplicate event deduplication: SmartRace events ignored if controller already in that state
- Added "Relay 5s" manual trigger button back to web UI (Special section)
- USB port changed: /dev/cu.usbmodem14101 → /dev/cu.usbmodem14401

### External Integration
To trigger a race stop from another device (e.g. ESP8266), POST to port 8080:
```json
{"event_type":"event.change_status","event_data":{"new":"suspended"}}
```
