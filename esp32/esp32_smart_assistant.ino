/*
 * Smart Morning Assistant — ESP32-S3  (Multi-User Fingerprint Edition)
 *
 * One physical device supports multiple users.
 * A fingerprint scan identifies who is standing at the mirror and
 * fetches THEIR personal weather, schedule, and news from the backend.
 *
 * Flow:
 *   1. Wait for finger on sensor.
 *   2. Match against locally stored fingerprint templates.
 *   3. Send fingerprint_id + DEVICE_SECRET to the backend.
 *   4. Backend maps fingerprint_id → user_id and returns that user's data.
 *   5. Display personalised morning briefing on Serial Monitor (or display).
 *
 * Required libraries (Arduino Library Manager):
 *   - Adafruit Fingerprint Sensor Library  (by Adafruit)
 *   - ArduinoJson                          (by Benoit Blanchon, v6 or v7)
 *
 * Built-in libraries:
 *   - WiFi.h
 *   - HTTPClient.h
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * SETUP INSTRUCTIONS
 * ─────────────────────────────────────────────────────────────────────────────
 * 1. Flash fingerprint_enroll.ino first.
 *    Follow its prompts in Serial Monitor to enroll each user's finger.
 *    Note the slot ID printed for each finger.
 *
 * 2. In the web dashboard → "Devices" page:
 *    a. Register this device (creates a Device ID and Secret).
 *    b. Add all family members by email.
 *    c. Map each fingerprint slot ID to a user account.
 *
 * 3. Fill in the four config constants below and flash this sketch.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>

// ---------------------------------------------------------------------------
// Configuration — edit these before flashing
// ---------------------------------------------------------------------------

const char* WIFI_SSID     = "Junior 2GHz";  // your Wi-Fi SSID
const char* WIFI_PASSWORD = "Pavani@3534";  // your Wi-Fi password  

// Backend URL — use the LAN IP of the machine running uvicorn.
// Find it with `ip addr` (Linux) or `ipconfig` (Windows).
const char* SERVER_URL    = "http://159.69.83.245:8000";

// From the web dashboard → Devices page → select your device.
const int   DEVICE_ID     = 1;            // paste the integer ID shown on dashboard
const char* DEVICE_SECRET = "97d0f36fbeb5bcb9cff35b1cdd3f0a8257aadb55ffc2a6ad9844bf80848c6843";  // paste the secret shown on dashboard

// ---------------------------------------------------------------------------
// Fingerprint sensor wiring (ESP32-S3)
//
//  Sensor VCC  → 3.3 V
//  Sensor GND  → GND
//  Sensor TX   → GPIO 18  (ESP32-S3 UART2 RX)
//  Sensor RX   → GPIO 17  (ESP32-S3 UART2 TX)
//
// Change SENSOR_RX / SENSOR_TX if your board uses different pins.
// ---------------------------------------------------------------------------

#define SENSOR_RX 18
#define SENSOR_TX 17

HardwareSerial sensorSerial(2);                  // UART2
Adafruit_Fingerprint finger(&sensorSerial);

// ---------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------

void connectWiFi() {
  Serial.printf("\nConnecting to: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWi-Fi connected — IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWi-Fi failed. Continuing without network.");
  }
}

// ---------------------------------------------------------------------------
// Fingerprint sensor init
// ---------------------------------------------------------------------------

void initFingerprint() {
  sensorSerial.begin(57600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found.");
    Serial.printf("Enrolled templates: %d\n", finger.templateCount);
  } else {
    Serial.println("WARNING: Fingerprint sensor not detected. Check wiring.");
  }
}

// ---------------------------------------------------------------------------
// Scan one finger
//
// Returns:
//   >0   — fingerprint slot ID of the matched template
//    0   — no match found (finger not enrolled)
//   -1   — sensor error or processing failure
//   -2   — no finger on sensor yet (normal — keep waiting)
// ---------------------------------------------------------------------------

int scanFinger() {
  uint8_t result = finger.getImage();

  if (result == FINGERPRINT_NOFINGER) return -2;   // nobody touching sensor
  if (result != FINGERPRINT_OK)       return -1;   // sensor communication error

  result = finger.image2Tz();
  if (result != FINGERPRINT_OK) {
    Serial.println("Image conversion failed — try again.");
    return -1;
  }

  result = finger.fingerSearch();
  if (result == FINGERPRINT_OK) {
    Serial.printf("Match: slot #%d  confidence %d\n",
                  finger.fingerID, finger.confidence);
    return finger.fingerID;
  }
  if (result == FINGERPRINT_NOTFOUND) {
    return 0;   // valid scan but no match
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Call backend: POST /api/devices/{DEVICE_ID}/login
// Returns the raw JSON string, or "" on HTTP error.
// ---------------------------------------------------------------------------

String fetchDashboard(int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No Wi-Fi — cannot fetch data.");
    return "";
  }

  String url = String(SERVER_URL) + "/api/devices/" + DEVICE_ID + "/login";
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);

  // Build JSON body
  String body;
  {
    JsonDocument req;   // ArduinoJson v7; for v6: DynamicJsonDocument req(256);
    req["fingerprint_id"] = fingerprintId;
    req["device_secret"]  = DEVICE_SECRET;
    serializeJson(req, body);
  }

  int code = http.POST(body);

  if (code == 200) {
    String resp = http.getString();
    http.end();
    return resp;
  }

  Serial.printf("HTTP error %d from backend.\n", code);
  if (code == 401) Serial.println("  → Check DEVICE_SECRET matches the dashboard.");
  if (code == 404) Serial.println("  → Check DEVICE_ID matches the dashboard.");
  http.end();
  return "";
}

// ---------------------------------------------------------------------------
// Parse the backend response and print the morning briefing
// ---------------------------------------------------------------------------

void parseAndDisplay(const String& jsonStr) {
  JsonDocument doc;   // ArduinoJson v7; for v6: DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, jsonStr);
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return;
  }

  bool success = doc["success"] | false;
  if (!success) {
    String msg = doc["message"] | "Fingerprint not registered.";
    Serial.println("\n── Fingerprint not recognised ──────────────────────");
    Serial.println("  " + msg);
    Serial.println("  → Enroll this finger and map it in the dashboard.");
    Serial.println("────────────────────────────────────────────────────\n");
    return;
  }

  Serial.println("\n╔══════════════════════════════════════════════════╗");
  Serial.printf ("║  Good morning, %-32s║\n",
                 (String("  ") + doc["user"]["username"].as<const char*>() + "!").c_str());
  Serial.println("╚══════════════════════════════════════════════════╝");

  // ── Weather ───────────────────────────────────────────────────────────────
  Serial.println("\n[WEATHER]");
  Serial.printf("  %s — %d°C, %s\n",
    doc["weather"]["location"].as<const char*>(),
    doc["weather"]["temperature"].as<int>(),
    doc["weather"]["condition"].as<const char*>());
  Serial.printf("  Humidity %d%%   Wind %d km/h\n",
    doc["weather"]["humidity"].as<int>(),
    doc["weather"]["wind_speed"].as<int>());

  // ── Schedule ──────────────────────────────────────────────────────────────
  Serial.println("\n[TODAY'S SCHEDULE]");
  JsonArray items = doc["schedule"]["items"].as<JsonArray>();
  if (items.size() == 0) {
    Serial.println("  No events today.");
  } else {
    for (JsonObject item : items) {
      Serial.printf("  %s  %s\n",
        item["time"].as<const char*>(),
        item["task"].as<const char*>());
    }
  }

  // ── News ──────────────────────────────────────────────────────────────────
  Serial.println("\n[NEWS]");
  int n = 1;
  for (const char* h : doc["news"]["headlines"].as<JsonArray>()) {
    Serial.printf("  %d. %s\n", n++, h);
  }

  Serial.println("\n──────────────────────────────────────────────────");
  Serial.printf("Updated: %s\n", doc["timestamp"].as<const char*>());
  Serial.println("──────────────────────────────────────────────────\n");
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Smart Morning Assistant — starting…");

  connectWiFi();
  initFingerprint();

  Serial.println("\nReady.  Place your finger on the sensor…\n");
}

void loop() {
  int result = scanFinger();

  if (result > 0) {
    // ── Valid fingerprint detected ──────────────────────────────────────────
    String json = fetchDashboard(result);
    if (json.length() > 0) {
      parseAndDisplay(json);
    }
    delay(4000);   // pause before accepting next scan

  } else if (result == 0) {
    // ── Finger scanned but not recognised ──────────────────────────────────
    Serial.println("Finger not recognised.  Is it enrolled and mapped in the dashboard?");
    delay(2000);

  } else if (result == -1) {
    // ── Sensor error (logged in scanFinger) ────────────────────────────────
    delay(1000);

  }
  // result == -2: no finger yet — tight poll, minimal delay
  delay(50);
}
