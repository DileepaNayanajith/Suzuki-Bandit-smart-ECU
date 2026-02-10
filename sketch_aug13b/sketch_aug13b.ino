#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeSans7pt7b.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "animation.h"  // your anim_frames[] in PROGMEM

// ---------------- TFT setup ----------------
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    21
#define TFT_MOSI  23
#define TFT_SCLK  18

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define TFT_WIDTH    128
#define TFT_HEIGHT   128
#define DISPLAY_TIME 1000
#define NUM_FRAMES   (sizeof(anim_frames) / sizeof(anim_frames[0]))

// ---------------- Gear input pins ----------------
const int gearPins[7] = {12, 14, 25, 26, 27, 32, 33};  // N,1–6
char *gearLabels[7]   = {"N","1","2","3","4","5","6"};

// ---------------- Temp + Fan ----------------
#define ONE_WIRE_BUS 13   // DS18B20 data pin
#define FAN_PIN      15   // Fan output
#define FAN_ON_TEMP  105  // °C

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ---------------- Battery Voltage ----------------
#define BATT_PIN 34        // ADC pin
#define ADC_MAX 4095       // 12-bit ADC on ESP32
#define VREF    3.3        // ADC reference
#define R1 100000.0        // Divider top resistor
#define R2 33000.0         // Divider bottom resistor
float batteryVoltage = 0.0;

// ---------------- WiFi AP ----------------
const char* ssid = "bandit_400";   // safer than space
const char* password = "12345678"; // minimum 8 chars
bool wifiOn = false;
bool wifiBlinkState = true;
unsigned long lastBlink = 0;

WiFiServer server(80);   // Web server on port 80

// ---------------- Variables ----------------
float temperatureC = 0.0;
bool fanStatus = false;
bool fanManual = false;  // manual override
int lastGearIndex = -1;  // track last shown gear

// ---------------- Functions ----------------
void drawFrame(uint8_t index) {
  tft.setAddrWindow(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
  for (uint16_t y = 0; y < TFT_HEIGHT; y++) {
    for (uint16_t x = 0; x < TFT_WIDTH; x++) {
      uint16_t color = pgm_read_word(&(anim_frames[index][y * TFT_WIDTH + x]));
      tft.pushColor(color);
    }
  }
}

void showGear(const char *gear) {
  tft.fillRect(40, 20, 60, 60, ST77XX_BLACK);
  tft.setFont(&FreeMonoBold24pt7b);
  tft.setTextColor((gear[0] == 'N') ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(50, 70);
  tft.print(gear);
}

void updateTempFanDisplay() {
  sensors.requestTemperatures();
  temperatureC = sensors.getTempCByIndex(0);

  if (!fanManual) {
    if (temperatureC >= FAN_ON_TEMP) {
      fanStatus = true;
      digitalWrite(FAN_PIN, HIGH);
    } else {
      fanStatus = false;
      digitalWrite(FAN_PIN, LOW);
    }
  } else {
    // Manual override ON
    fanStatus = true;
    digitalWrite(FAN_PIN, HIGH);
  }

  tft.fillRect(0, 90, 128, 38, ST77XX_BLACK);

  tft.setFont(&FreeSans7pt7b);
  tft.setTextSize(1);
  tft.setTextColor((temperatureC >= FAN_ON_TEMP) ? ST77XX_RED : ST77XX_WHITE);
  tft.setCursor(5, 105);

  char tempStr[10];
  dtostrf(temperatureC, 4, 1, tempStr);
  tft.print("Temp: ");
  tft.print(tempStr);
  tft.print(" C");

  tft.setCursor(5, 122);
  tft.setTextColor(fanStatus ? ST77XX_GREEN : ST77XX_WHITE);
  tft.print(fanStatus ? "Fan: ON" : "Fan: OFF");
}

void updateBatteryDisplay() {
  int raw = analogRead(BATT_PIN);
  float vIn = (raw * VREF / ADC_MAX);
  batteryVoltage = vIn * ((R1 + R2) / R2);

  tft.fillRect(0, 0, 60, 20, ST77XX_BLACK);

  tft.setFont(&FreeSans7pt7b);
  if (batteryVoltage < 11.5) tft.setTextColor(ST77XX_RED);
  else tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(2, 15);
  char voltStr[10];
  dtostrf(batteryVoltage, 4, 1, voltStr);
  tft.print(voltStr);
  tft.print("V");
}

void updateWiFiDisplay() {
  tft.fillRect(60, 0, 68, 20, ST77XX_BLACK);

  tft.setFont(&FreeSans7pt7b);
  int clients = WiFi.softAPgetStationNum();

  if (wifiOn) {
    if (clients > 0) {
      unsigned long now = millis();
      if (now - lastBlink > 500) {
        wifiBlinkState = !wifiBlinkState;
        lastBlink = now;
      }
      if (wifiBlinkState) {
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(65, 15);
        tft.print("WiFi:ON");
      }
    } else {
      tft.setTextColor(ST77XX_GREEN);
      tft.setCursor(65, 15);
      tft.print("WiFi:ON");
    }
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(65, 15);
    tft.print("WiFi:OFF");
  }
}

void handleClient(WiFiClient client) {
  String req = client.readStringUntil('\r');
  client.flush();

  if (req.indexOf("/fan/on") != -1) {
    fanManual = true;
  } else if (req.indexOf("/fan/auto") != -1) {
    fanManual = false;
    fanStatus = false;
    digitalWrite(FAN_PIN, LOW);
  }

  // ---- Web page ----
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE HTML><html>");
  client.println("<h2>Bandit 400 Smart ECU</h2>");
  client.print("<p><b>Gear:</b> ");
  client.print((lastGearIndex >= 0) ? gearLabels[lastGearIndex] : "-");
  client.println("</p>");

  client.print("<p><b>Temp:</b> ");
  client.print(temperatureC, 1);
  client.println(" C</p>");

  client.print("<p><b>Fan:</b> ");
  client.print(fanStatus ? "ON" : "OFF");
  client.println("</p>");

  client.print("<p><b>Battery:</b> ");
  client.print(batteryVoltage, 1);
  client.println(" V</p>");

  client.println("<p><a href=\"/fan/on\"><button>Fan ON</button></a>");
  client.println("<a href=\"/fan/auto\"><button>Fan AUTO</button></a></p>");

  client.println("</html>");
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  for (uint8_t i = 0; i < NUM_FRAMES; i++) {
    drawFrame(i);
    delay(DISPLAY_TIME);
    tft.fillScreen(ST77XX_BLACK);
  }

  for (int i = 0; i < 7; i++) pinMode(gearPins[i], INPUT_PULLUP);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  sensors.begin();

  // --- WiFi AP fix ---
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (WiFi.softAP(ssid, password)) {
    wifiOn = true;
    server.begin();
    Serial.println("AP started, connect to SSID: bandit_400");
    Serial.println("Open http://192.168.4.1/");
  } else {
    wifiOn = false;
  }
}

// ---------------- Loop ----------------
void loop() {
  for (int i = 0; i < 7; i++) {
    if (digitalRead(gearPins[i]) == LOW) {
      if (lastGearIndex != i) {
        showGear(gearLabels[i]);
        lastGearIndex = i;
      }
      break;
    }
  }

  updateBatteryDisplay();
  updateWiFiDisplay();
  updateTempFanDisplay();

  WiFiClient client = server.available();
  if (client) {
    while (client.connected() && client.available()) {
      handleClient(client);
      break;
    }
    delay(1);
    client.stop();
  }

  delay(200);
}
