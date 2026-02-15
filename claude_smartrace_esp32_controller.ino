/*
 * SmartRace ESP32-C6 Light Controller
 * Based on scottmcglynn/smartrace_events Flutter project
 *
 * ** ESP32-C6 VERSION - Network Lights + LCD + RGB LED **
 *
 * Features:
 * - SmartRace Data Interface API (receives HTTP POST events)
 * - Govee Network light control (H7020 via LAN)
 * - 1.47" ST7789 LCD display showing status
 * - RGB LED (NeoPixel) matching light colors
 * - Web interface for manual testing
 * - Relay control for 5V rail (turns off for 5 seconds on step events)
 *
 * Hardware:
 * - ESP32-C6 board with 1.47" LCD (Waveshare)
 * - RGB LED (NeoPixel) on GPIO 8
 * - Relay module on GPIO 23 (configurable below)
 * - Govee Network lights (H7020 or compatible)
 *
 * SmartRace Configuration:
 * Configure SmartRace Data Interface to POST events to:
 * http://<ESP32_IP>:8080/ or http://src.local:8080/
 *
 * Author: Based on Scott McGlynn's smartrace_events project
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>

// ==================== DISPLAY CONFIGURATION (from Waveshare examples) ====================
// Landscape mode: 320x172
#define LCD_WIDTH   320
#define LCD_HEIGHT  172
#define LCD_MISO    5
#define LCD_MOSI    6
#define LCD_SCLK    7
#define LCD_CS      14
#define LCD_DC      15
#define LCD_RST     21
#define LCD_BL      22
#define LCD_OFFSET_X 0
#define LCD_OFFSET_Y 34
#define LCD_SPI_FREQ 80000000

// RGB LED (NeoPixel) configuration
#define RGB_LED_PIN 8
#define RGB_LED_COUNT 1

// NeoPixel RGB LED
Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Color definitions (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_PURPLE  0xF81F
#define COLOR_YELLOW  0xFFE0

// ==================== CONFIGURATION - EDIT AS NEEDED ====================
const char* WIFI_SSID = "SR";
const char* WIFI_PASSWORD = "smartrace";

// mDNS hostname (access via http://src.local)
const char* MDNS_HOSTNAME = "src";

// SmartRace Data Interface API port (SmartRace POSTs events to this port)
const int SMARTRACE_API_PORT = 8080;

// GPIO Pin for Relay (controls 5V rail to CU controllers)
const int RELAY_PIN = 23;  // Change if using different GPIO

// Govee Network Light IP (leave empty for auto-discovery)
String GOVEE_NETWORK_IP = "";  // e.g., "192.168.68.50" or leave empty
// ==================== END CONFIGURATION ====================

// Preferences for persistent storage
Preferences preferences;

// Web Server for UI (port 80)
WebServer server(80);

// SmartRace Data Interface API Server (port 8080)
WebServer smartraceAPI(SMARTRACE_API_PORT);

// Network Light Status
bool networkLightFound = false;

// Track Conditions & Race Status
enum TrackCondition {
  TRACK_NONE,
  TRACK_DRY,
  TRACK_ABOUT_TO_RAIN,
  TRACK_WET,
  TRACK_DRYING
};

enum RaceState {
  RACE_NONE,
  RACE_STARTING,
  RACE_RUNNING,
  RACE_STOPPED,
  RACE_ENDED
};

TrackCondition currentCondition = TRACK_NONE;
RaceState currentRaceState = RACE_NONE;

// Relay Control (for 5V rail)
unsigned long relayOffUntil = 0;
bool relayIsOn = false;

// Function prototypes
void loadSettings();
void saveSettings();
void setupSmartRaceAPI();
void updateDisplayStatus(const char* status, uint16_t color);

// ==================== LCD Driver Functions (from Waveshare examples) ====================

void LCD_WriteCommand(uint8_t cmd) {
  SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, LOW);
  SPI.transfer(cmd);
  digitalWrite(LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteData(uint8_t data) {
  SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, HIGH);
  SPI.transfer(data);
  digitalWrite(LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteData16(uint16_t data) {
  SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, HIGH);
  SPI.transfer16(data);
  digitalWrite(LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_SetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  LCD_WriteCommand(0x2A);
  LCD_WriteData((x1 + LCD_OFFSET_X) >> 8);
  LCD_WriteData((x1 + LCD_OFFSET_X) & 0xFF);
  LCD_WriteData((x2 + LCD_OFFSET_X) >> 8);
  LCD_WriteData((x2 + LCD_OFFSET_X) & 0xFF);

  LCD_WriteCommand(0x2B);
  LCD_WriteData((y1 + LCD_OFFSET_Y) >> 8);
  LCD_WriteData((y1 + LCD_OFFSET_Y) & 0xFF);
  LCD_WriteData((y2 + LCD_OFFSET_Y) >> 8);
  LCD_WriteData((y2 + LCD_OFFSET_Y) & 0xFF);

  LCD_WriteCommand(0x2C);
}

void LCD_Init() {
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_DC, OUTPUT);
  pinMode(LCD_RST, OUTPUT);
  pinMode(LCD_BL, OUTPUT);

  SPI.begin(LCD_SCLK, LCD_MISO, LCD_MOSI);

  // Hardware reset
  digitalWrite(LCD_CS, LOW);
  delay(50);
  digitalWrite(LCD_RST, LOW);
  delay(50);
  digitalWrite(LCD_RST, HIGH);
  delay(50);

  // Backlight on
  ledcAttach(LCD_BL, 1000, 10);
  ledcWrite(LCD_BL, 1000);

  // ST7789 initialization sequence
  LCD_WriteCommand(0x11);  // Sleep out
  delay(120);

  LCD_WriteCommand(0x36);  // Memory data access control
  LCD_WriteData(0x70);     // Landscape orientation

  LCD_WriteCommand(0x3A);  // Interface pixel format
  LCD_WriteData(0x05);     // 16-bit color

  LCD_WriteCommand(0xB2);  // Porch setting
  LCD_WriteData(0x0C);
  LCD_WriteData(0x0C);
  LCD_WriteData(0x00);
  LCD_WriteData(0x33);
  LCD_WriteData(0x33);

  LCD_WriteCommand(0xB7);  // Gate control
  LCD_WriteData(0x35);

  LCD_WriteCommand(0xBB);  // VCOM setting
  LCD_WriteData(0x35);

  LCD_WriteCommand(0xC0);  // LCM control
  LCD_WriteData(0x2C);

  LCD_WriteCommand(0xC2);  // VDV and VRH command enable
  LCD_WriteData(0x01);

  LCD_WriteCommand(0xC3);  // VRH set
  LCD_WriteData(0x13);

  LCD_WriteCommand(0xC4);  // VDV set
  LCD_WriteData(0x20);

  LCD_WriteCommand(0xC6);  // Frame rate control
  LCD_WriteData(0x0F);

  LCD_WriteCommand(0xD0);  // Power control 1
  LCD_WriteData(0xA4);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xD6);
  LCD_WriteData(0xA1);

  LCD_WriteCommand(0xE0);  // Positive gamma
  LCD_WriteData(0xF0);
  LCD_WriteData(0x00);
  LCD_WriteData(0x04);
  LCD_WriteData(0x04);
  LCD_WriteData(0x04);
  LCD_WriteData(0x05);
  LCD_WriteData(0x29);
  LCD_WriteData(0x33);
  LCD_WriteData(0x3E);
  LCD_WriteData(0x38);
  LCD_WriteData(0x12);
  LCD_WriteData(0x12);
  LCD_WriteData(0x28);
  LCD_WriteData(0x30);

  LCD_WriteCommand(0xE1);  // Negative gamma
  LCD_WriteData(0xF0);
  LCD_WriteData(0x07);
  LCD_WriteData(0x0A);
  LCD_WriteData(0x0D);
  LCD_WriteData(0x0B);
  LCD_WriteData(0x07);
  LCD_WriteData(0x28);
  LCD_WriteData(0x33);
  LCD_WriteData(0x3E);
  LCD_WriteData(0x36);
  LCD_WriteData(0x14);
  LCD_WriteData(0x14);
  LCD_WriteData(0x29);
  LCD_WriteData(0x32);

  LCD_WriteCommand(0x21);  // Display inversion on
  LCD_WriteCommand(0x11);  // Sleep out
  delay(120);
  LCD_WriteCommand(0x29);  // Display on
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  LCD_SetWindow(x, y, x + w - 1, y + h - 1);

  SPI.beginTransaction(SPISettings(LCD_SPI_FREQ, MSBFIRST, SPI_MODE0));
  digitalWrite(LCD_CS, LOW);
  digitalWrite(LCD_DC, HIGH);

  for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
    SPI.transfer16(color);
  }

  digitalWrite(LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_Clear(uint16_t color) {
  LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

// Simple 8x8 font (subset of printable ASCII)
const uint8_t font8x8[][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Space
  {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // " (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // # (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // $ (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // % (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // & (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ( (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ) (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // * (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // + (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // , (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // - (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // / (placeholder)
  {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
  {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 1
  {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 2
  {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
  {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, // 4
  {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
  {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
  {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
  {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
  {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 9
  {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // :
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ; (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // < (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // = (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // > (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ? (placeholder)
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // @ (placeholder)
  {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // A
  {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
  {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
  {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
  {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, // E
  {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, // F
  {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}, // G
  {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
  {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, // I
  {0x3E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // J
  {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K
  {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L
  {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M
  {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // N
  {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
  {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
  {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // Q
  {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // R
  {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S
  {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
  {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
  {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
  {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
  {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X
  {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
  {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z
};

void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
  if (c < 32 || c > 90) c = 32;  // Only support space to Z
  int idx = c - 32;

  for (int row = 0; row < 8; row++) {
    uint8_t line = font8x8[idx][row];
    for (int col = 0; col < 8; col++) {
      uint16_t pixelColor = (line & (0x80 >> col)) ? color : bg;
      if (size == 1) {
        LCD_FillRect(x + col, y + row, 1, 1, pixelColor);
      } else {
        LCD_FillRect(x + col * size, y + row * size, size, size, pixelColor);
      }
    }
  }
}

void LCD_DrawString(uint16_t x, uint16_t y, const char* str, uint16_t color, uint16_t bg, uint8_t size) {
  while (*str) {
    LCD_DrawChar(x, y, *str, color, bg, size);
    x += 8 * size;
    str++;
  }
}

// ==================== Main Setup ====================

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println(" SmartRace ESP32-C6 Light Controller");
  Serial.println(" With LCD Display & RGB LED");
  Serial.println(" Using SmartRace Data Interface API");
  Serial.println("========================================");
  Serial.println();

  // Initialize RGB LED
  rgbLed.begin();
  rgbLed.setBrightness(50);
  rgbLed.clear();
  rgbLed.show();
  Serial.println("[RGB LED] Initialized on GPIO 8");

  // Initialize display
  LCD_Init();
  Serial.println("[Display] Initialized (ST7789 320x172 landscape)");
  LCD_Clear(COLOR_BLACK);
  LCD_DrawString(60, 50, "SMARTRACE", COLOR_WHITE, COLOR_BLACK, 3);
  LCD_DrawString(80, 100, "INITIALIZING...", COLOR_YELLOW, COLOR_BLACK, 1);

  // Load saved settings from flash
  loadSettings();

  // Initialize relay pin in open drain mode (LOW by default, HIGH when race stopped)
  pinMode(RELAY_PIN, OUTPUT_OPEN_DRAIN);
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("[RELAY] Initialized (open drain, LOW)");

  // Connect to WiFi
  connectWiFi();

  // Try to discover network light
  discoverNetworkLight();

  // Start web server for UI
  setupWebServer();

  // Start SmartRace Data Interface API server
  setupSmartRaceAPI();

  Serial.println();
  Serial.println("========================================");
  Serial.println(" Setup Complete!");
  Serial.print(" Web Interface: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
  Serial.print(" SmartRace API: http://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.print(SMARTRACE_API_PORT);
  Serial.println("/");
  Serial.print(" Or use: http://");
  Serial.print(MDNS_HOSTNAME);
  Serial.println(".local");
  Serial.println("========================================");
  Serial.println();

  // Update display with ready status - landscape layout
  LCD_Clear(COLOR_BLACK);

  // Left side - READY status with IP:port at bottom
  LCD_FillRect(0, 0, 200, LCD_HEIGHT, COLOR_GREEN);
  LCD_DrawString(30, 55, "READY", COLOR_BLACK, COLOR_GREEN, 3);
  char infoBar[32];
  sprintf(infoBar, "%s:%d", WiFi.localIP().toString().c_str(), SMARTRACE_API_PORT);
  LCD_DrawString(2, 160, infoBar, COLOR_BLACK, COLOR_GREEN, 1);

  // Right side - info
  LCD_DrawString(210, 10, "RACE:", COLOR_WHITE, COLOR_BLACK, 1);
  LCD_DrawString(210, 22, "NONE", COLOR_GREEN, COLOR_BLACK, 2);

  LCD_DrawString(210, 50, "WEATHER:", COLOR_WHITE, COLOR_BLACK, 1);
  LCD_DrawString(210, 62, "NONE", COLOR_BLUE, COLOR_BLACK, 2);

  LCD_DrawString(210, 94, networkLightFound ? "GOVEE: OK" : "GOVEE: NO",
                 networkLightFound ? COLOR_GREEN : COLOR_RED, COLOR_BLACK, 1);

  LCD_DrawString(210, 112, "RELAY: LOW", COLOR_WHITE, COLOR_BLACK, 1);

  // Set initial RGB LED to dim white (standby)
  rgbLed.setPixelColor(0, rgbLed.Color(20, 20, 20));
  rgbLed.show();
}

void loop() {
  server.handleClient();
  smartraceAPI.handleClient();

  if (relayIsOn && millis() >= relayOffUntil) {
    digitalWrite(RELAY_PIN, LOW);
    relayIsOn = false;
    Serial.println("[RELAY] Back to LOW");
    LCD_FillRect(210, 112, 110, 8, COLOR_BLACK);
    LCD_DrawString(210, 112, "RELAY: LOW", COLOR_WHITE, COLOR_BLACK, 1);
  }

  delay(10);
}

// ==================== Settings Management ====================

void loadSettings() {
  preferences.begin("smartrace", false);
  String savedGoveeIP = preferences.getString("govee_ip", "");
  if (savedGoveeIP.length() > 0) {
    GOVEE_NETWORK_IP = savedGoveeIP;
  }
  Serial.println("[Settings] Loaded from flash");
  if (GOVEE_NETWORK_IP.length() > 0) {
    Serial.print("  Govee IP: ");
    Serial.println(GOVEE_NETWORK_IP);
  }
  preferences.end();
}

void saveSettings() {
  preferences.begin("smartrace", false);
  if (GOVEE_NETWORK_IP.length() > 0) {
    preferences.putString("govee_ip", GOVEE_NETWORK_IP);
  }
  preferences.end();
  Serial.println("[Settings] Saved to flash");
}

// ==================== WiFi Functions ====================

void connectWiFi() {
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.print("[mDNS] Started! Access at: http://");
      Serial.print(MDNS_HOSTNAME);
      Serial.println(".local");
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("[mDNS] Failed to start");
    }
  } else {
    Serial.println("[WiFi] FAILED to connect!");
  }
}

// ==================== SmartRace Data Interface API ====================

void setupSmartRaceAPI() {
  smartraceAPI.on("/", HTTP_OPTIONS, handleAPICors);
  smartraceAPI.on("/", HTTP_POST, handleAPIEvent);
  smartraceAPI.on("/", HTTP_GET, handleAPIInfo);
  smartraceAPI.begin();
  Serial.print("[SmartRace API] Server started on port ");
  Serial.println(SMARTRACE_API_PORT);
}

void handleAPICors() {
  smartraceAPI.sendHeader("Access-Control-Allow-Origin", "*");
  smartraceAPI.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  smartraceAPI.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  smartraceAPI.send(200, "text/plain", "");
}

void handleAPIInfo() {
  smartraceAPI.sendHeader("Access-Control-Allow-Origin", "*");
  smartraceAPI.send(200, "application/json",
    "{\"status\":\"ok\",\"device\":\"SmartRace ESP32 Light Controller\",\"version\":\"2.0\"}");
}

void handleAPIEvent() {
  smartraceAPI.sendHeader("Access-Control-Allow-Origin", "*");

  if (!smartraceAPI.hasArg("plain")) {
    smartraceAPI.send(400, "application/json", "{\"error\":\"No data\"}");
    return;
  }

  String body = smartraceAPI.arg("plain");
  Serial.print("[SmartRace API] Received: ");
  Serial.println(body);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    smartraceAPI.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* eventType = doc["event_type"];
  if (!eventType) {
    smartraceAPI.send(400, "application/json", "{\"error\":\"Missing event_type\"}");
    return;
  }

  processSmartRaceEvent(eventType, doc["event_data"]);
  smartraceAPI.send(200, "application/json", "{\"status\":\"success\"}");
}

void processSmartRaceEvent(const char* eventType, JsonVariant eventData) {
  if (strcmp(eventType, "events.weather_update") == 0) {
    const char* condition = eventData.as<const char*>();
    if (condition) {
      TrackCondition newCondition = currentCondition;
      if (strcmp(condition, "about_to_rain") == 0) {
        newCondition = TRACK_ABOUT_TO_RAIN;
      } else if (strcmp(condition, "about_to_dry_up") == 0) {
        newCondition = TRACK_DRYING;
      }
      if (newCondition == currentCondition) return;
      currentCondition = newCondition;
      updateLights();
    }
  }
  else if (strcmp(eventType, "events.weather_change") == 0) {
    const char* condition = eventData.as<const char*>();
    if (condition) {
      TrackCondition newCondition = currentCondition;
      if (strcmp(condition, "wet") == 0) {
        newCondition = TRACK_WET;
      } else if (strcmp(condition, "dry") == 0) {
        newCondition = TRACK_DRY;
      }
      if (newCondition == currentCondition) return;
      currentCondition = newCondition;
      updateLights();
    }
  }
  else if (strcmp(eventType, "event.change_status") == 0) {
    const char* newStatus = eventData["new"];
    if (newStatus) {
      RaceState newState = currentRaceState;
      TrackCondition newCondition = currentCondition;
      if (strcmp(newStatus, "suspended") == 0) {
        newState = RACE_STOPPED;
      } else if (strcmp(newStatus, "running") == 0 || strcmp(newStatus, "restarting") == 0) {
        newState = RACE_RUNNING;
      } else if (strcmp(newStatus, "ended") == 0 || strcmp(newStatus, "finished") == 0) {
        newState = RACE_ENDED;
        newCondition = TRACK_NONE;
      } else if (strcmp(newStatus, "starting") == 0) {
        newState = RACE_STARTING;
        newCondition = TRACK_NONE;
      }
      if (newState == currentRaceState && newCondition == currentCondition) return;
      currentRaceState = newState;
      currentCondition = newCondition;
      updateLights();
    }
  }
}

void handleRaceStatus(const char* status) {
  currentCondition = TRACK_NONE;
  if (strcmp(status, "starting") == 0) currentRaceState = RACE_STARTING;
  else if (strcmp(status, "running") == 0) currentRaceState = RACE_RUNNING;
  else if (strcmp(status, "stopped") == 0) currentRaceState = RACE_STOPPED;
  else if (strcmp(status, "ended") == 0) currentRaceState = RACE_ENDED;
  updateLights();
}

void handleWeatherCondition(const char* condition) {
  if (strcmp(condition, "dry") == 0) currentCondition = TRACK_DRY;
  else if (strcmp(condition, "about_to_rain") == 0) currentCondition = TRACK_ABOUT_TO_RAIN;
  else if (strcmp(condition, "wet") == 0 || strcmp(condition, "rain") == 0) currentCondition = TRACK_WET;
  else if (strcmp(condition, "drying") == 0 || strcmp(condition, "about_to_dry") == 0) currentCondition = TRACK_DRYING;
  updateLights();
}

void updateLights() {
  uint8_t r = 0, g = 0, b = 0;
  const char* statusText = "OFF";
  uint16_t displayColor = COLOR_BLACK;

  if (currentRaceState == RACE_STOPPED || currentRaceState == RACE_ENDED) {
    r = 255; g = 0; b = 0;
    statusText = "STOPPED";
    displayColor = COLOR_RED;
  }
  else if (currentRaceState == RACE_RUNNING || currentRaceState == RACE_STARTING) {
    if (currentCondition != TRACK_NONE) {
      switch (currentCondition) {
        case TRACK_DRY:
          r = 255; g = 255; b = 255;
          statusText = "DRY";
          displayColor = COLOR_WHITE;
          break;
        case TRACK_ABOUT_TO_RAIN:
          r = 255; g = 0; b = 255;
          statusText = "RAIN SOON";
          displayColor = COLOR_PURPLE;
          break;
        case TRACK_WET:
          r = 0; g = 0; b = 255;
          statusText = "WET";
          displayColor = COLOR_BLUE;
          break;
        case TRACK_DRYING:
          r = 0; g = 255; b = 0;
          statusText = "DRYING";
          displayColor = COLOR_GREEN;
          break;
        default:
          r = 255; g = 255; b = 255;
          statusText = "RUNNING";
          displayColor = COLOR_WHITE;
          break;
      }
    } else {
      r = 255; g = 255; b = 255;
      statusText = "RUNNING";
      displayColor = COLOR_WHITE;
    }
  }
  else if (currentCondition != TRACK_NONE) {
    switch (currentCondition) {
      case TRACK_DRY: r = 255; g = 255; b = 255; statusText = "DRY"; displayColor = COLOR_WHITE; break;
      case TRACK_ABOUT_TO_RAIN: r = 128; g = 0; b = 128; statusText = "RAIN SOON"; displayColor = COLOR_PURPLE; break;
      case TRACK_WET: r = 0; g = 0; b = 255; statusText = "WET"; displayColor = COLOR_BLUE; break;
      case TRACK_DRYING: r = 0; g = 255; b = 0; statusText = "DRYING"; displayColor = COLOR_GREEN; break;
      default: break;
    }
  }

  Serial.printf("[Lights] %s - RGB(%d,%d,%d)\n", statusText, r, g, b);

  // Update relay - pulse HIGH for 5 seconds when race stopped/ended
  if (currentRaceState == RACE_STOPPED || currentRaceState == RACE_ENDED) {
    digitalWrite(RELAY_PIN, HIGH);
    relayOffUntil = millis() + 5000;
    relayIsOn = true;
    Serial.println("[RELAY] HIGH for 5s (race stopped)");
  }

  // Update Govee network light
  setNetworkLight(r, g, b);

  // Update RGB LED
  rgbLed.setPixelColor(0, rgbLed.Color(r, g, b));
  rgbLed.show();

  // Update display
  updateDisplayStatus(statusText, displayColor);
}

void updateDisplayStatus(const char* status, uint16_t color) {
  // No LCD_Clear — partial updates only to avoid flicker

  // Left side — fill directly with new color (old color → new color, no black flash)
  LCD_FillRect(0, 0, 200, LCD_HEIGHT, color);

  // Status text — word wrap if too wide for color box
  uint16_t textColor = (color == COLOR_WHITE || color == COLOR_YELLOW) ? COLOR_BLACK : COLOR_WHITE;
  int sLen = strlen(status);
  if (sLen > 7) {
    const char* space = strchr(status, ' ');
    if (space) {
      char line1[16];
      int splitAt = space - status;
      strncpy(line1, status, splitAt);
      line1[splitAt] = '\0';
      LCD_DrawString(20, 45, line1, textColor, color, 3);
      LCD_DrawString(20, 73, space + 1, textColor, color, 3);
    } else {
      LCD_DrawString(20, 55, status, textColor, color, 3);
    }
  } else {
    LCD_DrawString(20, 55, status, textColor, color, 3);
  }

  // IP:port at bottom of color box
  char infoBar[32];
  sprintf(infoBar, "%s:%d", WiFi.localIP().toString().c_str(), SMARTRACE_API_PORT);
  LCD_DrawString(2, 160, infoBar, textColor, color, 1);

  // Right panel — clear only value areas, labels stay from initial draw
  LCD_FillRect(210, 22, 110, 16, COLOR_BLACK);
  const char* raceStr = "NONE";
  switch (currentRaceState) {
    case RACE_STARTING: raceStr = "START"; break;
    case RACE_RUNNING: raceStr = "RUN"; break;
    case RACE_STOPPED: raceStr = "STOP"; break;
    case RACE_ENDED: raceStr = "END"; break;
    default: break;
  }
  LCD_DrawString(210, 22, raceStr, COLOR_GREEN, COLOR_BLACK, 2);

  LCD_FillRect(210, 62, 110, 16, COLOR_BLACK);
  const char* weatherStr = "NONE";
  switch (currentCondition) {
    case TRACK_DRY: weatherStr = "DRY"; break;
    case TRACK_ABOUT_TO_RAIN: weatherStr = "RAIN"; break;
    case TRACK_WET: weatherStr = "WET"; break;
    case TRACK_DRYING: weatherStr = "DRY"; break;
    default: break;
  }
  LCD_DrawString(210, 62, weatherStr, COLOR_BLUE, COLOR_BLACK, 2);

  LCD_FillRect(210, 94, 110, 8, COLOR_BLACK);
  LCD_DrawString(210, 94, networkLightFound ? "GOVEE: OK" : "GOVEE: NO",
                 networkLightFound ? COLOR_GREEN : COLOR_RED, COLOR_BLACK, 1);

  LCD_FillRect(210, 112, 110, 8, COLOR_BLACK);
  LCD_DrawString(210, 112, relayIsOn ? "RELAY: HIGH" : "RELAY: LOW",
                 relayIsOn ? COLOR_RED : COLOR_WHITE, COLOR_BLACK, 1);
}

// ==================== Network Light Functions ====================

void discoverNetworkLight() {
  if (GOVEE_NETWORK_IP.length() > 0) {
    Serial.print("[Network] Using configured IP: ");
    Serial.println(GOVEE_NETWORK_IP);
    networkLightFound = true;
    return;
  }

  Serial.println("[Network] Scanning for Govee lights...");

  WiFiUDP udp;
  IPAddress multicastIP(239, 255, 255, 250);

  if (udp.begin(4002)) {
    StaticJsonDocument<128> scanDoc;
    scanDoc["msg"]["cmd"] = "scan";
    scanDoc["msg"]["data"]["account_topic"] = "reserve";

    String scanMsg;
    serializeJson(scanDoc, scanMsg);

    udp.beginPacket(multicastIP, 4001);
    udp.print(scanMsg);
    udp.endPacket();

    unsigned long startTime = millis();
    while (millis() - startTime < 3000) {
      int packetSize = udp.parsePacket();
      if (packetSize > 0) {
        char incomingPacket[512];
        int len = udp.read(incomingPacket, sizeof(incomingPacket) - 1);
        if (len > 0) {
          incomingPacket[len] = 0;

          StaticJsonDocument<512> responseDoc;
          if (!deserializeJson(responseDoc, incomingPacket)) {
            const char* cmd = responseDoc["msg"]["cmd"];
            if (cmd && strcmp(cmd, "scan") == 0) {
              const char* ip = responseDoc["msg"]["data"]["ip"];
              if (ip) {
                GOVEE_NETWORK_IP = String(ip);
                networkLightFound = true;
                Serial.print("[Network] Found Govee at: ");
                Serial.println(ip);
                break;
              }
            }
          }
        }
      }
      delay(10);
    }
    udp.stop();
  }

  if (!networkLightFound) {
    Serial.println("[Network] No Govee lights found");
  }
}

void setNetworkLight(uint8_t r, uint8_t g, uint8_t b) {
  if (!networkLightFound || GOVEE_NETWORK_IP.length() == 0) return;

  StaticJsonDocument<256> doc;
  doc["msg"]["cmd"] = "colorwc";
  doc["msg"]["data"]["color"]["r"] = r;
  doc["msg"]["data"]["color"]["g"] = g;
  doc["msg"]["data"]["color"]["b"] = b;
  doc["msg"]["data"]["colorTemInKelvin"] = 0;

  String jsonStr;
  serializeJson(doc, jsonStr);

  WiFiUDP udp;
  IPAddress lightIP;
  if (lightIP.fromString(GOVEE_NETWORK_IP)) {
    udp.beginPacket(lightIP, 4001);
    udp.print(jsonStr);
    udp.endPacket();
  }
}

// ==================== Web Server ====================

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/settings", handleSettings);
  server.on("/api/trigger", HTTP_POST, handleAPITrigger);
  server.on("/api/status", handleAPIStatus);
  server.on("/api/config", HTTP_GET, handleAPIGetConfig);
  server.on("/api/config", HTTP_POST, handleAPISetConfig);
  server.begin();
  Serial.println("[Web] Server started on port 80");
}

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>SmartRace Light Controller</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);padding:20px;min-height:100vh}
    .container{max-width:900px;margin:0 auto}
    h1{text-align:center;color:#fff;margin-bottom:5px;font-size:2em}
    .subtitle{text-align:center;color:rgba(255,255,255,.9);margin-bottom:30px}
    .card{background:#fff;border-radius:15px;padding:25px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,.2)}
    h2{color:#667eea;margin-bottom:20px;border-bottom:3px solid #667eea;padding-bottom:10px}
    .button-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
    .btn{padding:18px 10px;font-size:15px;font-weight:600;border:none;border-radius:10px;cursor:pointer;color:#fff;transition:all .3s}
    .btn:hover{transform:translateY(-2px)}
    .btn-green{background:#4CAF50}.btn-red{background:#f44336}.btn-blue{background:#2196F3}
    .btn-white{background:#fff;color:#333;border:2px solid #ddd}.btn-purple{background:#800080}
    .btn-off{background:#757575}.btn-relay{background:#9C27B0}
    .response{margin-top:15px;padding:12px;border-radius:8px;display:none}
    .response.success{background:#d4edda;color:#155724}
  </style>
</head>
<body>
  <div class="container">
    <h1>SmartRace Light Controller</h1>
    <p class="subtitle">ESP32-C6 with LCD + RGB LED</p>
    <div class="card">
      <h2>Race Status</h2>
      <div class="button-grid">
        <button class="btn btn-white" onclick="trigger('race','starting')">Starting</button>
        <button class="btn btn-green" onclick="trigger('race','running')">Running</button>
        <button class="btn btn-red" onclick="trigger('race','stopped')">Stopped</button>
        <button class="btn btn-red" onclick="trigger('race','ended')">Ended</button>
      </div>
    </div>
    <div class="card">
      <h2>Track Conditions</h2>
      <div class="button-grid">
        <button class="btn btn-white" onclick="trigger('weather','dry')">Dry</button>
        <button class="btn btn-purple" onclick="trigger('weather','about_to_rain')">About to Rain</button>
        <button class="btn btn-blue" onclick="trigger('weather','wet')">Wet</button>
        <button class="btn btn-green" onclick="trigger('weather','drying')">Drying</button>
      </div>
    </div>
    <div class="card">
      <h2>Special</h2>
      <div class="button-grid">
        <button class="btn btn-off" onclick="trigger('light','off')">Lights Off</button>
        <button class="btn btn-relay" onclick="trigger('relay','pulse')">Relay 5s</button>
      </div>
    </div>
    <div class="response" id="response"></div>
  </div>
  <script>
    function trigger(c,v){
      fetch('/api/trigger',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({category:c,value:v})})
      .then(r=>r.json()).then(d=>{document.getElementById('response').textContent=d.message||'OK';
      document.getElementById('response').className='response success';document.getElementById('response').style.display='block';
      setTimeout(()=>document.getElementById('response').style.display='none',2000)});
    }
  </script>
</body>
</html>
)=====";
  server.send(200, "text/html; charset=UTF-8", html);
}

void handleAPITrigger() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const char* category = doc["category"];
  const char* value = doc["value"];

  if (strcmp(category, "race") == 0) handleRaceStatus(value);
  else if (strcmp(category, "weather") == 0) handleWeatherCondition(value);
  else if (strcmp(category, "light") == 0 && strcmp(value, "off") == 0) {
    setNetworkLight(0, 0, 0);
    rgbLed.setPixelColor(0, 0);
    rgbLed.show();
  }
  else if (strcmp(category, "relay") == 0 && strcmp(value, "pulse") == 0) {
    digitalWrite(RELAY_PIN, HIGH);
    relayOffUntil = millis() + 5000;
    relayIsOn = true;
    Serial.println("[RELAY] Manual pulse HIGH for 5s");
  }

  server.send(200, "application/json", "{\"success\":true,\"message\":\"OK\"}");
}

void handleAPIStatus() {
  StaticJsonDocument<256> doc;
  doc["api_active"] = true;
  doc["network"] = networkLightFound;
  doc["relay"] = (currentRaceState == RACE_STOPPED || currentRaceState == RACE_ENDED) ? "HIGH" : "LOW";
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleAPIGetConfig() {
  StaticJsonDocument<256> doc;
  doc["api_port"] = SMARTRACE_API_PORT;
  doc["api_url"] = "http://" + WiFi.localIP().toString() + ":" + String(SMARTRACE_API_PORT) + "/";
  doc["govee_ip"] = GOVEE_NETWORK_IP;
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleAPISetConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No data\"}");
    return;
  }
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  if (doc.containsKey("govee_ip")) {
    GOVEE_NETWORK_IP = doc["govee_ip"].as<String>();
    if (GOVEE_NETWORK_IP.length() > 0) networkLightFound = true;
  }
  saveSettings();
  server.send(200, "application/json", "{\"success\":true}");
}

void handleSettings() {
  String html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Settings</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);padding:20px;min-height:100vh}
    .container{max-width:600px;margin:0 auto}
    h1{text-align:center;color:#fff;margin-bottom:30px}
    .card{background:#fff;border-radius:15px;padding:30px;margin-bottom:20px}
    label{display:block;font-weight:600;margin-bottom:8px}
    input{width:100%;padding:12px;border:2px solid #ddd;border-radius:8px;font-size:16px;margin-bottom:20px}
    .btn{padding:15px;font-size:16px;border:none;border-radius:10px;cursor:pointer;width:48%}
    .btn-primary{background:#667eea;color:#fff}
    .btn-secondary{background:#e0e0e0}
  </style>
</head>
<body>
  <div class="container">
    <h1>Settings</h1>
    <div class="card">
      <label>Govee Light IP</label>
      <input type="text" id="govee_ip" placeholder="Leave empty for auto-discovery">
      <button class="btn btn-secondary" onclick="location.href='/'">Back</button>
      <button class="btn btn-primary" onclick="save()">Save</button>
    </div>
  </div>
  <script>
    fetch('/api/config').then(r=>r.json()).then(d=>{document.getElementById('govee_ip').value=d.govee_ip||''});
    function save(){
      fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({govee_ip:document.getElementById('govee_ip').value})})
      .then(()=>location.href='/');
    }
  </script>
</body>
</html>
)=====";
  server.send(200, "text/html; charset=UTF-8", html);
}
