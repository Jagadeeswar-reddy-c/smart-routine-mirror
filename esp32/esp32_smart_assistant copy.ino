/*
 * Smart Morning Assistant — OLED + Buttons Edition
 *
 * Biometric scanning is removed for now.
 *
 * Flow:
 *   1. Connect to Wi-Fi.
 *   2. Fetch dashboard data from the backend using X-Device-Token.
 *   3. Use 4 buttons to switch OLED pages:
 *        Button 1 → Greeting
 *        Button 2 → Weather
 *        Button 3 → Schedule
 *        Button 4 → News
 *
 * Required libraries (Arduino Library Manager):
 *   - ArduinoJson
 *   - Adafruit SSD1306
 *   - Adafruit GFX Library
 *
 * Built-in libraries:
 *   - WiFi.h
 *   - HTTPClient.h
 */

#include <WiFi.h>
#include <HTTPClient.h>

#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------------------------------------------------------------
// Configuration — edit these before flashing
// ---------------------------------------------------------------------------

const char* WIFI_SSID     = "Junior 2GHz";
const char* WIFI_PASSWORD = "Pavani@3534";

// Backend URL — use the LAN IP of the machine running uvicorn.
const char* SERVER_URL    = "http://192.168.1.10:8000";

// Get this from the web dashboard → Device token section.
const char* DEVICE_TOKEN  = "97d0f36fbeb5bcb9cff35b1cdd3f0a8257aadb55ffc2a6ad9844bf80848c6843";

// OLED display settings (typical 0.96" SSD1306 display)
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_ADDRESS  0x3C
#define OLED_RESET    -1

// I2C pins for ESP32-S3. Adjust if your board uses different wiring.
#define OLED_SDA_PIN 5   // XIAO D4 = SDA
#define OLED_SCL_PIN 6   // XIAO D5 = SCL

// Button wiring: connect each button between the pin and GND.
// INPUT_PULLUP means pressed = LOW.
#define BUTTON_1_PIN 1
#define BUTTON_2_PIN 2
#define BUTTON_3_PIN 3
#define BUTTON_4_PIN 4

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

DynamicJsonDocument dashboardDoc(6144);
String lastDashboardJson;
String lastErrorMessage;

int currentPage = 1;
unsigned long lastFetchMs = 0;
const unsigned long REFRESH_INTERVAL_MS = 5UL * 60UL * 1000UL;
const unsigned long DEBOUNCE_MS = 180;

struct ButtonState {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

ButtonState buttons[] = {
  { BUTTON_1_PIN, HIGH, HIGH, 0 },
  { BUTTON_2_PIN, HIGH, HIGH, 0 },
  { BUTTON_3_PIN, HIGH, HIGH, 0 },
  { BUTTON_4_PIN, HIGH, HIGH, 0 },
};

const char* wifiDisconnectReasonText(uint8_t reason) {
  switch (reason) {
    case 2:
      return "AUTH_EXPIRE (authentication timed out or was rejected)";
    case 15:
      return "4WAY_HANDSHAKE_TIMEOUT";
    case 202:
      return "AUTH_FAIL";
    case 205:
      return "BEACON_TIMEOUT";
    default:
      return "UNKNOWN";
  }
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.print("WiFi disconnect reason: ");
    Serial.println(info.wifi_sta_disconnected.reason);
    Serial.print("WiFi disconnect meaning: ");
    Serial.println(wifiDisconnectReasonText(info.wifi_sta_disconnected.reason));
  }
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void drawCenteredLine(const String& text, int16_t y, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (OLED_WIDTH - w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

String truncateText(const String& text, size_t maxChars) {
  if (text.length() <= maxChars) return text;
  if (maxChars <= 3) return text.substring(0, maxChars);
  return text.substring(0, maxChars - 3) + "...";
}

void clearDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

void showStatus(const String& title, const String& message) {
  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine(title, 0, 1);
  display.setCursor(0, 18);
  display.println(message);
  display.display();
}

void showBootScreen() {
  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine("Smart Morning Assistant", 0, 1);
  display.setCursor(0, 20);
  display.println("Button 1: Greeting");
  display.println("Button 2: Weather");
  display.println("Button 3: Schedule");
  display.println("Button 4: News");
  display.display();
}

bool isDashboardFresh() {
  return lastDashboardJson.length() > 0 && (millis() - lastFetchMs) < REFRESH_INTERVAL_MS;
}

bool parseServerHostPort(String url, String& host, uint16_t& port) {
  if (url.startsWith("http://")) {
    url = url.substring(7);
    port = 80;
  } else if (url.startsWith("https://")) {
    url = url.substring(8);
    port = 443;
  } else {
    return false;
  }

  int slashIndex = url.indexOf('/');
  String hostPort = (slashIndex >= 0) ? url.substring(0, slashIndex) : url;
  int colonIndex = hostPort.lastIndexOf(':');

  if (colonIndex >= 0) {
    host = hostPort.substring(0, colonIndex);
    port = hostPort.substring(colonIndex + 1).toInt();
  } else {
    host = hostPort;
  }

  return host.length() > 0 && port > 0;
}

bool cacheDashboardJson(const String& json) {
  dashboardDoc.clear();
  DeserializationError err = deserializeJson(dashboardDoc, json);
  if (err) {
    lastErrorMessage = String("JSON: ") + err.c_str();
    lastDashboardJson = "";
    lastFetchMs = 0;
    return false;
  }

  lastDashboardJson = json;
  lastFetchMs = millis();
  return true;
}

bool fetchDashboardData(bool force = false) {
  if (!force && isDashboardFresh()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    lastErrorMessage = "Wi-Fi disconnected";
    return false;
  }

  String url = String(SERVER_URL) + "/api/dashboard-data";
  String host;
  uint16_t port = 0;
  if (!parseServerHostPort(String(SERVER_URL), host, port)) {
    lastErrorMessage = String("Bad SERVER_URL: ") + SERVER_URL;
    return false;
  }

  WiFiClient probe;
  if (!probe.connect(host.c_str(), port)) {
    lastErrorMessage = String("Backend refused:\n") + String(SERVER_URL);
    return false;
  }
  probe.stop();

  HTTPClient http;
  if (!http.begin(url)) {
    lastErrorMessage = String("HTTP begin failed: ") + url;
    return false;
  }

  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.setTimeout(10000);

  int code = http.GET();
  String response = http.getString();

  if (code != 200) {
    String detail = http.errorToString(code);
    lastErrorMessage = String("HTTP ") + code;
    if (detail.length() > 0) {
      lastErrorMessage += String(" (") + detail + ")";
    }
    if (response.length() > 0) {
      lastErrorMessage += String(": ") + truncateText(response, 80);
    }
    http.end();
    return false;
  }

  http.end();

  if (response.length() == 0) {
    lastErrorMessage = "Empty response";
    return false;
  }

  if (!cacheDashboardJson(response)) {
    return false;
  }

  lastErrorMessage.clear();
  return true;
}

bool ensureDashboardData() {
  if (isDashboardFresh()) {
    return true;
  }
  return fetchDashboardData(true);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void renderGreeting() {
  if (!ensureDashboardData()) {
    showStatus("Greeting", String("No dashboard data.\n") + lastErrorMessage);
    return;
  }

  const char* username = dashboardDoc["user"]["username"] | "friend";
  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine("Good morning,", 10, 1);
  drawCenteredLine(String(username) + "!", 28, 1);
  display.display();
}

void renderWeather() {
  if (!ensureDashboardData()) {
    showStatus("Weather", String("No dashboard data.\n") + lastErrorMessage);
    return;
  }

  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine("WEATHER", 0, 1);
  display.setCursor(0, 16);
  display.print(dashboardDoc["weather"]["location"].as<const char*>());
  display.println();
  display.print(String(dashboardDoc["weather"]["temperature"].as<int>()) + "C, ");
  display.println(dashboardDoc["weather"]["condition"].as<const char*>());
  display.print("Humidity ");
  display.print(dashboardDoc["weather"]["humidity"].as<int>());
  display.print("%  Wind ");
  display.print(dashboardDoc["weather"]["wind_speed"].as<int>());
  display.println(" km/h");
  display.display();
}

void renderSchedule() {
  if (!ensureDashboardData()) {
    showStatus("Schedule", String("No dashboard data.\n") + lastErrorMessage);
    return;
  }

  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine("TODAY'S SCHEDULE", 0, 1);
  display.setCursor(0, 16);

  JsonArray items = dashboardDoc["schedule"]["items"].as<JsonArray>();
  if (items.isNull() || items.size() == 0) {
    display.println("No events today.");
    display.display();
    return;
  }

  int count = 0;
  for (JsonObject item : items) {
    String line = String(item["time"].as<const char*>()) + "  " + item["task"].as<const char*>();
    display.println(truncateText(line, 21));
    count++;
    if (count >= 4) break;
  }
  display.display();
}

void renderNews() {
  if (!ensureDashboardData()) {
    showStatus("News", String("No dashboard data.\n") + lastErrorMessage);
    return;
  }

  clearDisplay();
  display.setTextSize(1);
  drawCenteredLine("NEWS", 0, 1);
  display.setCursor(0, 16);

  JsonArray headlines = dashboardDoc["news"]["headlines"].as<JsonArray>();
  if (headlines.isNull() || headlines.size() == 0) {
    display.println("No headlines.");
    display.display();
    return;
  }

  int index = 1;
  for (JsonVariant headline : headlines) {
    String line = String(index) + ". " + headline.as<const char*>();
    display.println(truncateText(line, 21));
    index++;
    if (index > 3) break;
  }
  display.display();
}

void renderCurrentPage() {
  switch (currentPage) {
    case 1: renderGreeting(); break;
    case 2: renderWeather(); break;
    case 3: renderSchedule(); break;
    case 4: renderNews(); break;
    default: renderGreeting(); break;
  }
}

// ---------------------------------------------------------------------------
// Button handling
// ---------------------------------------------------------------------------

void readButtons() {
  for (int i = 0; i < 4; i++) {
    bool reading = digitalRead(buttons[i].pin);

    if (reading != buttons[i].lastReading) {
      buttons[i].lastChangeMs = millis();
    }

    if ((millis() - buttons[i].lastChangeMs) > DEBOUNCE_MS) {
      if (buttons[i].stableState != reading) {
        buttons[i].stableState = reading;

        if (buttons[i].stableState == LOW) {
          currentPage = i + 1;
          renderCurrentPage();
        }
      }
    }

    buttons[i].lastReading = reading;
  }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

bool connectWiFi() {
  Serial.printf("\nConnecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  // Clear any stale state before attempting a fresh join.
  WiFi.disconnect(true);
  delay(200);

  // Scan for diagnostics only; do not lock BSSID/channel during connect.
  Serial.println("Scanning for available WiFi networks...");
  int networkCount = WiFi.scanNetworks();
  if (networkCount == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.printf("%d networks found:\n", networkCount);
    for (int i = 0; i < networkCount; ++i) {
      String auth;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN:            auth = "OPEN";           break;
        case WIFI_AUTH_WEP:             auth = "WEP";            break;
        case WIFI_AUTH_WPA_PSK:         auth = "WPA_PSK";        break;
        case WIFI_AUTH_WPA2_PSK:        auth = "WPA2_PSK";       break;
        case WIFI_AUTH_WPA_WPA2_PSK:    auth = "WPA_WPA2_PSK";   break;
        case WIFI_AUTH_WPA2_ENTERPRISE: auth = "WPA2_ENTERPRISE"; break;
        default:                        auth = "OTHER";          break;
      }

      bool isTarget = (WiFi.SSID(i) == String(WIFI_SSID));
      Serial.printf("%s %d: %-22s  %4d dBm  %s\n",
                    isTarget ? ">>" : "  ",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    auth.c_str());
    }
  }
  WiFi.scanDelete();

  Serial.println("Connecting without scan/BSSID lock...");
  // If password is empty, call the single-arg overload to join an open network.
  if (WIFI_PASSWORD[0] == '\0') {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected successfully!");
    Serial.printf("IP Address:  %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI:        %d dBm\n", WiFi.RSSI());
    Serial.printf("Channel:     %d\n", WiFi.channel());
    Serial.printf("Gateway:     %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Backend URL: %s\n", SERVER_URL);
    return true;
  } else {
    Serial.println();
    Serial.println("WiFi connection FAILED.");
    Serial.printf("Status code: %d\n", (int)WiFi.status());
    WiFi.printDiag(Serial);
    Serial.println();
    Serial.println("Checklist:");
    Serial.println("  1. Is the SSID/password correct?");
    Serial.println("  2. Is the AP on 2.4 GHz? (ESP32 does not support 5 GHz)");
    Serial.println("  3. If a phone hotspot — did a prompt appear on the phone?");
    Serial.println("  4. Is the AP's client limit full?");
    Serial.printf("  5. Whitelist this MAC on the AP if MAC filtering is on: %s\n",
                  WiFi.macAddress().c_str());
    return false;
  }
}

void initDisplay() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED init failed. Check wiring/address.");
    while (true) {
      delay(1000);
    }
  }
  display.clearDisplay();
  display.display();
}

void initButtons() {
  for (int i = 0; i < 4; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].lastReading = digitalRead(buttons[i].pin);
    buttons[i].stableState = buttons[i].lastReading;
    buttons[i].lastChangeMs = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Smart Morning Assistant — OLED + Buttons starting...");

  WiFi.onEvent(onWiFiEvent);
  initDisplay();
  initButtons();
  connectWiFi();

  showBootScreen();
  fetchDashboardData(true);
  currentPage = 1;
  renderCurrentPage();
}

void loop() {
  readButtons();

  if ((millis() - lastFetchMs) > REFRESH_INTERVAL_MS) {
    fetchDashboardData(true);
    renderCurrentPage();
  }

  static unsigned long lastWifiStatusPrint = 0;
  if (millis() - lastWifiStatusPrint > 5000) {
    lastWifiStatusPrint = millis();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi still connected. IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi disconnected!");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 15000) {
      lastReconnectAttempt = millis();
      if (connectWiFi()) {
        lastErrorMessage.clear();
        fetchDashboardData(true);
        renderCurrentPage();
      }
    }
  }

  delay(20);
}