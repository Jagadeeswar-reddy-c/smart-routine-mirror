/*
 * Smart Display Mirror — 4-Display Wi-Fi + Fingerprint Edition
 *
 * Flow:
 *   1. Boot → fetch all 3 usernames → Display 0 shows "Select User" with real names
 *   2. BTN_NEXT / BTN_DONE scroll, BTN_SEL confirms
 *   3. Fingerprint scan for selected profile
 *      Match    → load full data → all 4 displays live
 *      No match → enroll (2-scan), then scan again to login
 *   4. Hold BTN_SEL 3 s in dashboard → back to user select
 *
 * Each profile has its own DEVICE_TOKEN (Dashboard → ESP32 Device Key)
 * and its own fingerprint slot on the sensor.
 *
 * PIN CHANGE from single-user version:
 *   LED_RED (D9/GPIO8) removed → D9/GPIO8 now fingerprint RX
 *   D6 (GPIO43) was free       → now fingerprint TX
 *   ECHO_PIN stays at D7/GPIO44  ← NO CHANGE
 *   Storm: both Yellow + Blue LEDs on (replaces Red)
 *
 * Required libraries: Adafruit GFX | Adafruit SSD1306 | ArduinoJson
 *                     Adafruit Fingerprint Sensor Library
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>

// ═══════════════════════════════════════════════════════════════════════════
//  NETWORK CONFIG
// ═══════════════════════════════════════════════════════════════════════════

const char* WIFI_SSID     = "Junior 2GHz";
const char* WIFI_PASSWORD = "Pavani@3534";
const char* SERVER_URL    = "http://159.69.83.245";

// ═══════════════════════════════════════════════════════════════════════════
//  3 USER PROFILES
//  Each profile = one user's Device Token (Dashboard → ESP32 Device Key)
//                + their fingerprint slot number on the sensor (1–127)
// ═══════════════════════════════════════════════════════════════════════════

#define NUM_PROFILES 3

const char* PROFILE_TOKEN[NUM_PROFILES] = {
  "641837bb4ac6743d59d542bf3e20304390a363be5b1ac392797e42fa07d8ff8c",
  "9e66c82045e2df7fcfa695d1f9b0cbf2af84a4b5d44ab8fbe0eae72088cf4b82",
  "5266493de7af4276a5b63a34a64e82690e95fa9f6388efc42a27e53bb52357bc"
};

const uint8_t FP_SLOT[NUM_PROFILES] = {1, 2, 3};

char profileName[NUM_PROFILES][20] = {"Alice", "Bob", "Charlie"};
// ↑ overwritten at boot with real usernames fetched from API

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE PINS — same as before except LED_RED removed, FP pins added
// ═══════════════════════════════════════════════════════════════════════════

#define TCA_ADDR    0x70
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

#define SDA_PIN     5    // D4 / GPIO5
#define SCL_PIN     6    // D5 / GPIO6

#define BTN_SEL     3    // D2 / GPIO3 — confirm / cycle display / hold=switch user
#define BTN_NEXT    1    // D0 / GPIO1 — scroll up
#define BTN_DONE    2    // D1 / GPIO2 — scroll down
#define DEBOUNCE_MS   200UL
#define LONG_PRESS_MS 3000UL

#define LED_YELLOW  4    // D3 / GPIO4
#define LED_BLUE    7    // D8 / GPIO7
// LED_RED removed — GPIO8 now used for fingerprint RX
// Storm = both Yellow + Blue on simultaneously

#define TRIG_PIN    9    // D10 / GPIO9
#define ECHO_PIN   44    // D7  / GPIO44  ← unchanged

#define FP_TX      43    // D6 / GPIO43  (ESP32 TX → sensor RX)  was free
#define FP_RX       8    // D9 / GPIO8   (sensor TX → ESP32 RX)  was LED_RED

#define PRESENCE_CM      200
#define SLEEP_MS       60000UL
#define DATA_REFRESH_MS 60000UL
#define WATER_SHOW_MS    6000UL
#define GREET_REFRESH_MS 10000UL
#define SCHED_REFRESH_MS 15000UL
#define NEWS_ROTATE_MS    5000UL
#define MOTION_CHECK_MS   500UL
#define FP_SCAN_TIMEOUT 20000UL

// ═══════════════════════════════════════════════════════════════════════════
//  MODE
// ═══════════════════════════════════════════════════════════════════════════

enum Mode { MODE_SELECT, MODE_FP_SCAN, MODE_FP_ENROLL, MODE_NORMAL };
Mode     currentMode   = MODE_SELECT;
int8_t   selProfile    = 0;     // cursor in user list
int8_t   activeProfile = -1;   // confirmed + fingerprint-verified profile
uint32_t fpScanStart   = 0;
uint32_t btnSelDownAt  = 0;
bool     btnSelWasDown = false;

// ═══════════════════════════════════════════════════════════════════════════
//  DATA BUFFERS (filled from API for the active user)
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_QUOTES    12
#define MAX_QUOTE_LEN 64
#define MAX_NEWS      10
#define MAX_NEWS_LEN  72
#define MAX_TASKS     20
#define MAX_LABEL_LEN 24

char    dynName[32]       = "";
char    dynQuotes[MAX_QUOTES][MAX_QUOTE_LEN];
uint8_t dynQuoteCount     = 0;
char    dynNews[MAX_NEWS][MAX_NEWS_LEN];
uint8_t dynNewsCount      = 0;

struct DynTask { uint8_t h, m; char label[MAX_LABEL_LEN]; };
DynTask dynTasks[MAX_TASKS];
uint8_t dynTaskCount      = 0;

int16_t  dynTempC         = 0;
char     dynCondition[48] = "";
uint8_t  dynHumidity      = 0;
uint8_t  dynWind          = 0;
char     dynLocation[40]  = "";
uint32_t dynWaterMs       = 3600000UL;

uint32_t clockRefMs  = 0;
uint32_t clockStartS = 0;
bool     dataReady   = false;

// ═══════════════════════════════════════════════════════════════════════════
//  UI STATE
// ═══════════════════════════════════════════════════════════════════════════

Adafruit_SSD1306     oled(SCREEN_W, SCREEN_H, &Wire, -1);
HardwareSerial       fpSerial(1);
Adafruit_Fingerprint finger(&fpSerial);
bool                 fpReady = false;

uint8_t  activeDisplay  = 3;
uint8_t  quoteIdx       = 0;
uint8_t  newsIdx        = 0;
int8_t   selectedIdx    = 0;
bool     completed[MAX_TASKS] = {};
bool     showFahrenheit = false;

bool     waterAlert  = false;
uint32_t lastWaterMs = 0;
uint32_t waterOnAt   = 0;

uint32_t lastGreet     = 0;
uint32_t lastNews      = 0;
uint32_t lastSched     = 0;
uint32_t lastDataFetch = 0;

uint32_t lastBtnSel = 0;
uint32_t lastBtn1   = 0;
uint32_t lastBtn2   = 0;

bool     displaysAwake   = true;
uint32_t lastPresenceMs  = 0;
uint32_t lastMotionCheck = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

void tcaSelect(uint8_t ch) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

void simTime(uint8_t &h, uint8_t &m) {
  uint32_t elapsed = (millis() - clockRefMs) / 1000;
  uint32_t total   = (clockStartS + elapsed) % 86400;
  h = total / 3600;
  m = (total % 3600) / 60;
}

uint8_t condToIcon(const char* cond) {
  String c = String(cond); c.toLowerCase();
  if (c.indexOf("sun")     >= 0 || c.indexOf("clear")     >= 0) return 0;
  if (c.indexOf("partly")  >= 0 || c.indexOf("scattered")  >= 0) return 1;
  if (c.indexOf("cloud")   >= 0 || c.indexOf("overcast")   >= 0) return 2;
  if (c.indexOf("rain")    >= 0 || c.indexOf("drizzle")    >= 0
   || c.indexOf("shower")  >= 0)                                  return 3;
  if (c.indexOf("storm")   >= 0 || c.indexOf("thunder")    >= 0) return 4;
  if (c.indexOf("snow")    >= 0 || c.indexOf("flurr")      >= 0
   || c.indexOf("blizzard") >= 0)                                 return 5;
  if (c.indexOf("wind")    >= 0 || c.indexOf("breezy")     >= 0
   || c.indexOf("gust")    >= 0)                                  return 6;
  return 1;
}

void drawHeader(const char* left, const char* right, bool active) {
  if (active) { oled.fillRect(0,0,SCREEN_W,9,SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
  else          oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(2, 1); oled.print(left);
  if (right && right[0]) {
    oled.setCursor(SCREEN_W - (int16_t)(strlen(right)*6) - 2, 1); oled.print(right);
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
}

void drawCentred(const char* text, int16_t y, uint8_t size = 1) {
  oled.setTextSize(size);
  oled.setCursor((SCREEN_W - (int16_t)(strlen(text)*6*size)) / 2, y);
  oled.print(text);
}

void printWrapped(const char* text, int16_t x, int16_t y0, uint8_t maxLines) {
  char line[22]; int16_t y = y0; int pos = 0, len = strlen(text); uint8_t done = 0;
  while (pos < len && done < maxLines) {
    int rem = len - pos, take = (rem > 21) ? 21 : rem;
    if (rem > 21) { int b = take; while (b > 10 && text[pos+b] != ' ') b--; if (text[pos+b]==' ') take=b; }
    strncpy(line, text+pos, take); line[take] = '\0';
    oled.setCursor(x, y); oled.print(line);
    pos += take; if (pos < len && text[pos]==' ') pos++;
    y += 10; done++;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER LEDs  (storm = both Yellow + Blue)
// ═══════════════════════════════════════════════════════════════════════════

void updateWeatherLED() {
  bool on = dataReady && displaysAwake && (currentMode == MODE_NORMAL);
  uint8_t icon = on ? condToIcon(dynCondition) : 255;
  digitalWrite(LED_YELLOW, (on && (icon==0||icon==1||icon==4||icon==6)) ? HIGH : LOW);
  digitalWrite(LED_BLUE,   (on && (icon==2||icon==3||icon==4||icon==5)) ? HIGH : LOW);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY SLEEP / WAKE
// ═══════════════════════════════════════════════════════════════════════════

void drawUserSelect();   // forward decl

void displaysOff() {
  for (uint8_t ch = 0; ch < 4; ch++) { tcaSelect(ch); oled.ssd1306_command(SSD1306_DISPLAYOFF); }
  Wire.beginTransmission(TCA_ADDR); Wire.write(0); Wire.endTransmission();
  digitalWrite(LED_YELLOW, LOW); digitalWrite(LED_BLUE, LOW);
  displaysAwake = false; Serial.println(F("Sleep."));
}

void displaysOn() {
  for (uint8_t ch = 0; ch < 4; ch++) { tcaSelect(ch); oled.ssd1306_command(SSD1306_DISPLAYON); }
  displaysAwake = true; updateWeatherLED();
  if (currentMode == MODE_NORMAL)
    { drawGreeting(); drawWeather(); drawNews(); drawSchedule(); }
  else
    drawUserSelect();
  Serial.println(F("Wake."));
}

// ═══════════════════════════════════════════════════════════════════════════
//  ULTRASONIC  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

float getDistance() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000UL);
  return (dur == 0) ? 999.0f : dur * 0.0343f / 2.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WI-FI  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // keep radio active during fingerprint scan
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWi-Fi OK — %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println(F("\nWi-Fi failed."));
}

// ═══════════════════════════════════════════════════════════════════════════
//  WI-FI reconnect guard — call before any HTTP request
// ═══════════════════════════════════════════════════════════════════════════

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println(F("Wi-Fi lost, reconnecting..."));
  WiFi.reconnect();
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  bool ok = (WiFi.status() == WL_CONNECTED);
  if (ok) Serial.println(F("Wi-Fi reconnected"));
  else    Serial.println(F("Wi-Fi reconnect failed"));
  return ok;
}

// ═══════════════════════════════════════════════════════════════════════════
//  API — fetch real username for the user-select screen
// ═══════════════════════════════════════════════════════════════════════════

void fetchUserName(uint8_t profileIdx) {
  if (!ensureWiFi()) return;
  HTTPClient http;
  http.setReuse(false);
  http.begin(String(SERVER_URL) + "/api/multidisplay/data");
  http.addHeader("X-Device-Token", PROFILE_TOKEN[profileIdx]);
  http.setTimeout(8000);
  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, resp) == DeserializationError::Ok) {
      const char* uname = doc["user"]["username"] | "";
      if (strlen(uname) > 0) strlcpy(profileName[profileIdx], uname, 20);
    }
  }
  http.end();
  Serial.printf("Profile %d: %s\n", profileIdx+1, profileName[profileIdx]);
}

// ═══════════════════════════════════════════════════════════════════════════
//  API — fetch full dashboard data for active user
// ═══════════════════════════════════════════════════════════════════════════

bool fetchDisplayData(const char* token) {
  // After fingerprint scan, force WiFi reconnect and wait for DHCP to assign
  // a valid IP (WL_CONNECTED alone doesn't guarantee DHCP has completed).
  Serial.print(F("WiFi reset... "));
  WiFi.disconnect();
  delay(500);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint8_t wc = 0;
  while ((WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0)) && wc++ < 30)
    delay(500);
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0)) {
    Serial.println(F("failed")); return false;
  }
  Serial.println(WiFi.localIP());
  delay(500);   // let the lwIP routing table settle

  IPAddress serverIP(159, 69, 83, 245);

  String payload;
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) { Serial.printf("Retry %d\n", attempt); delay(2000); }

    WiFiClient client;
    Serial.print(F("TCP... "));
    if (!client.connect(serverIP, 80)) {
      Serial.println(F("connect failed"));
      client.stop(); continue;
    }
    Serial.println(F("ok"));

    // HTTP/1.0 — no keep-alive, simplest possible framing
    client.print(F("GET /api/multidisplay/data HTTP/1.0\r\nHost: 159.69.83.245\r\nX-Device-Token: "));
    client.print(token);
    client.print(F("\r\nConnection: close\r\n\r\n"));

    // Wait for first byte (up to 10 s)
    uint32_t dl = millis() + 10000UL;
    while (!client.available() && millis() < dl) delay(10);
    if (!client.available()) {
      Serial.println(F("timeout"));
      client.stop(); continue;
    }

    // Check status line
    String status = client.readStringUntil('\n');
    status.trim();
    if (status.indexOf("200") < 0) {
      Serial.println("HTTP: " + status);
      client.stop(); continue;
    }

    // Consume headers (blank line marks end)
    while (client.connected() || client.available()) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) break;
    }

    // Read body
    payload = "";
    dl = millis() + 10000UL;
    while ((client.connected() || client.available()) && millis() < dl) {
      while (client.available()) payload += (char)client.read();
    }
    client.stop();

    if (payload.length() > 0) break;  // success
    Serial.println(F("Empty body"));
  }

  if (payload.length() == 0) { Serial.println(F("fetch failed")); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) { Serial.println(F("JSON error")); return false; }

  strlcpy(dynName, doc["user"]["username"] | "", sizeof(dynName));

  dynQuoteCount = 0;
  for (JsonVariant q : doc["quotes"].as<JsonArray>()) {
    if (dynQuoteCount >= MAX_QUOTES) break;
    strlcpy(dynQuotes[dynQuoteCount++], q.as<const char*>(), MAX_QUOTE_LEN);
  }

  dynTempC    = doc["weather"]["temperature"] | (int16_t)0;
  dynHumidity = doc["weather"]["humidity"]    | (uint8_t)0;
  dynWind     = doc["weather"]["wind_speed"]  | (uint8_t)0;
  strlcpy(dynCondition, doc["weather"]["condition"] | "", sizeof(dynCondition));
  strlcpy(dynLocation,  doc["weather"]["location"]  | "", sizeof(dynLocation));

  dynNewsCount = 0;
  for (JsonVariant h : doc["news"]["headlines"].as<JsonArray>()) {
    if (dynNewsCount >= MAX_NEWS) break;
    strlcpy(dynNews[dynNewsCount++], h.as<const char*>(), MAX_NEWS_LEN);
  }

  bool resetSel = (dynTaskCount == 0);
  dynTaskCount  = 0;
  for (JsonVariant item : doc["schedule"]["items"].as<JsonArray>()) {
    if (dynTaskCount >= MAX_TASKS) break;
    const char* t = item["time"] | "00:00";
    dynTasks[dynTaskCount].h = atoi(t);
    dynTasks[dynTaskCount].m = atoi(t + 3);
    strlcpy(dynTasks[dynTaskCount].label, item["task"] | "", MAX_LABEL_LEN);
    dynTaskCount++;
  }

  uint32_t wim = doc["settings"]["water_interval_min"] | (uint32_t)60;
  dynWaterMs   = wim * 60000UL;

  const char* ts = doc["timestamp"] | "";
  if (strlen(ts) >= 16) {
    uint8_t sh = atoi(ts+11), sm = atoi(ts+14);
    clockStartS = (uint32_t)sh*3600 + (uint32_t)sm*60;
    clockRefMs  = millis();
    Serial.printf("Clock → %02d:%02d\n", sh, sm);
  }

  if (resetSel && dynTaskCount > 0) {
    uint8_t h, m; simTime(h, m); uint16_t nowMins = (uint16_t)h*60+m;
    for (int8_t i = 0; i < (int8_t)dynTaskCount; i++)
      if ((uint16_t)dynTasks[i].h*60+dynTasks[i].m <= nowMins) selectedIdx = i;
  }
  if (dynQuoteCount > 0 && quoteIdx    >= dynQuoteCount) quoteIdx    = 0;
  if (dynNewsCount  > 0 && newsIdx     >= dynNewsCount)  newsIdx     = 0;
  if (dynTaskCount  > 0 && selectedIdx >= dynTaskCount)  selectedIdx = 0;

  updateWeatherLED();
  Serial.printf("Data OK — %s | %d quotes %d news %d tasks\n",
                dynName, dynQuoteCount, dynNewsCount, dynTaskCount);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER ICONS  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

void iconCloud(int16_t cx, int16_t cy) {
  oled.fillCircle(cx-8,cy+2,6,SSD1306_WHITE); oled.fillCircle(cx,cy-2,8,SSD1306_WHITE);
  oled.fillCircle(cx+8,cy+2,6,SSD1306_WHITE); oled.fillRect(cx-14,cy+2,28,8,SSD1306_WHITE);
}

void iconSun(int16_t cx, int16_t cy, uint8_t r) {
  oled.drawCircle(cx,cy,r,SSD1306_WHITE);
  uint8_t gap=r+3,tip=r+5,d1=(uint8_t)(gap*0.7f),d2=(uint8_t)(tip*0.7f);
  oled.drawLine(cx,cy-gap,cx,cy-tip,SSD1306_WHITE); oled.drawLine(cx,cy+gap,cx,cy+tip,SSD1306_WHITE);
  oled.drawLine(cx-gap,cy,cx-tip,cy,SSD1306_WHITE); oled.drawLine(cx+gap,cy,cx+tip,cy,SSD1306_WHITE);
  oled.drawLine(cx-d1,cy-d1,cx-d2,cy-d2,SSD1306_WHITE); oled.drawLine(cx+d1,cy-d1,cx+d2,cy-d2,SSD1306_WHITE);
  oled.drawLine(cx-d1,cy+d1,cx-d2,cy+d2,SSD1306_WHITE); oled.drawLine(cx+d1,cy+d1,cx+d2,cy+d2,SSD1306_WHITE);
}

void drawWeatherIcon(uint8_t idx) {
  const int16_t cx=24,cy=29;
  switch(idx) {
    case 0: iconSun(cx,cy,8); break;
    case 1:
      oled.drawCircle(cx+8,cy-9,5,SSD1306_WHITE);
      oled.drawLine(cx+8,cy-16,cx+8,cy-18,SSD1306_WHITE);
      oled.drawLine(cx+15,cy-9,cx+17,cy-9,SSD1306_WHITE);
      oled.drawLine(cx+13,cy-14,cx+15,cy-16,SSD1306_WHITE);
      iconCloud(cx-3,cy+3); break;
    case 2: iconCloud(cx,cy); break;
    case 3:
      iconCloud(cx,cy-5);
      for(int8_t i=0;i<4;i++) oled.drawLine(cx-9+i*6,cy+8,cx-11+i*6,cy+14,SSD1306_WHITE);
      break;
    case 4:
      iconCloud(cx,cy-7);
      oled.drawLine(cx+3,cy+3,cx-2,cy+9,SSD1306_WHITE);
      oled.drawLine(cx-2,cy+9,cx+4,cy+9,SSD1306_WHITE);
      oled.drawLine(cx+4,cy+9,cx-1,cy+15,SSD1306_WHITE); break;
    case 5: {
      iconCloud(cx,cy-7);
      for(int8_t i=0;i<3;i++){
        int16_t sx=cx-8+i*8,sy=cy+10;
        oled.drawLine(sx-3,sy,sx+3,sy,SSD1306_WHITE); oled.drawLine(sx,sy-3,sx,sy+3,SSD1306_WHITE);
        oled.drawLine(sx-2,sy-2,sx+2,sy+2,SSD1306_WHITE); oled.drawLine(sx+2,sy-2,sx-2,sy+2,SSD1306_WHITE);
      } break;
    }
    case 6:
      for(int8_t i=0;i<3;i++){
        int16_t y=cy-8+i*8;
        oled.drawLine(cx-14,y,cx-5,y,SSD1306_WHITE); oled.drawLine(cx-5,y,cx,y-3,SSD1306_WHITE);
        oled.drawLine(cx,y-3,cx+5,y,SSD1306_WHITE);  oled.drawLine(cx+5,y,cx+14,y,SSD1306_WHITE);
      } break;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SPLASH  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

void drawSplash(uint8_t ch, const char* line1, const char* line2 = "") {
  tcaSelect(ch); oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
  drawCentred(line1, 22); if (line2[0]) drawCentred(line2, 38); oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  DASHBOARD DISPLAYS  (identical to original)
// ═══════════════════════════════════════════════════════════════════════════

void drawGreeting() {
  uint8_t h, m; simTime(h, m);
  tcaSelect(0); oled.clearDisplay(); oled.setTextWrap(false); oled.setTextColor(SSD1306_WHITE);
  drawHeader("GREETING", "", activeDisplay==0);
  if (!dataReady) {
    drawCentred("Connecting...", 28);
    char buf[6]; snprintf(buf,sizeof(buf),"%02d:%02d",h,m); oled.setCursor(80,56); oled.print(buf);
    oled.display(); return;
  }
  const char* period = (h<12)?"Morning":(h<17)?"Afternoon":"Evening";
  oled.setTextSize(1); oled.setCursor(0,12); oled.print("Good "); oled.print(period);
  oled.setTextSize(2); oled.setCursor(0,22); oled.print(dynName); oled.print("!");
  oled.drawLine(0,40,127,40,SSD1306_WHITE);
  oled.setTextSize(1); oled.setCursor(0,43);
  if (dynQuoteCount > 0) oled.print(dynQuotes[quoteIdx % dynQuoteCount]);
  char buf[6]; snprintf(buf,sizeof(buf),"%02d:%02d",h,m); oled.setCursor(80,56); oled.print(buf);
  if (activeDisplay==0) { oled.setCursor(0,56); oled.print("< >"); }
  oled.display();
}

void drawWeather() {
  tcaSelect(1); oled.clearDisplay(); oled.setTextWrap(false); oled.setTextColor(SSD1306_WHITE);
  if (!dataReady) { drawHeader("WEATHER","",activeDisplay==1); drawCentred("Fetching...",30); oled.display(); return; }
  drawHeader("WEATHER", showFahrenheit?"F":"C", activeDisplay==1);
  int16_t displayTemp = showFahrenheit ? (int16_t)((int32_t)dynTempC*9/5+32) : dynTempC;
  char unitCh = showFahrenheit ? 'F' : 'C';
  drawWeatherIcon(condToIcon(dynCondition));
  char tempStr[6]; snprintf(tempStr,sizeof(tempStr),"%d",displayTemp);
  oled.setTextSize(3); oled.setCursor(54,13); oled.print(tempStr);
  oled.setTextSize(1); oled.setCursor(54+(int16_t)strlen(tempStr)*18,13);
  oled.print((char)248); oled.print(unitCh);
  oled.setCursor(54,38); oled.print(dynLocation);
  oled.drawLine(0,47,127,47,SSD1306_WHITE);
  oled.setCursor(0,49); oled.print(dynCondition);
  char stats[20]; snprintf(stats,sizeof(stats),"H:%d%%  W:%dkm/h",dynHumidity,dynWind);
  oled.setCursor(0,56); oled.print(stats);
  if (activeDisplay==1) { oled.setCursor(100,56); oled.print("C/F"); }
  oled.display();
}

void drawNews() {
  tcaSelect(2); oled.clearDisplay(); oled.setTextWrap(false); oled.setTextColor(SSD1306_WHITE);
  if (!dataReady || dynNewsCount==0) { drawHeader("NEWS","",activeDisplay==2); drawCentred("Fetching...",30); oled.display(); return; }
  uint8_t cnt = dynNewsCount;
  char counter[6]; snprintf(counter,sizeof(counter),"%d/%d",(newsIdx%cnt)+1,cnt);
  drawHeader("NEWS", counter, activeDisplay==2);
  printWrapped(dynNews[newsIdx%cnt], 0, 13, 4);
  if (activeDisplay==2) { oled.setCursor(14,56); oled.print("< prev  next >"); }
  else {
    int16_t dotBaseX = (SCREEN_W - cnt*8) / 2;
    for (uint8_t i=0;i<cnt;i++) {
      int16_t dotCx = dotBaseX+i*8+3;
      if (i==newsIdx%cnt) oled.fillCircle(dotCx,60,3,SSD1306_WHITE);
      else                oled.drawCircle(dotCx,60,3,SSD1306_WHITE);
    }
  }
  oled.display();
}

void drawSchedule() {
  tcaSelect(3); oled.clearDisplay(); oled.setTextWrap(false); oled.setTextColor(SSD1306_WHITE);
  if (waterAlert) {
    oled.setTextSize(1); oled.setCursor(22,3); oled.print("- REMINDER -");
    oled.drawLine(0,13,127,13,SSD1306_WHITE);
    oled.setTextSize(2); oled.setCursor(16,18); oled.print("DRINK"); oled.setCursor(16,37); oled.print("WATER!");
    oled.setTextSize(1); oled.setCursor(18,56); oled.print("Stay hydrated :)");
    oled.display(); return;
  }
  if (!dataReady || dynTaskCount==0) {
    drawHeader("SCHEDULE","",activeDisplay==3);
    drawCentred(!dataReady?"Connecting...":"No tasks today",30); oled.display(); return;
  }
  uint8_t tc=dynTaskCount, doneCount=0;
  for (uint8_t i=0;i<tc;i++) if (completed[i]) doneCount++;
  char progress[6]; snprintf(progress,sizeof(progress),"%d/%d",doneCount,tc);
  drawHeader("SCHEDULE", progress, activeDisplay==3);
  int8_t viewStart = selectedIdx-1;
  if (viewStart > (int8_t)tc-4) viewStart=(int8_t)tc-4;
  if (viewStart < 0) viewStart=0;
  int16_t y=12;
  for (int8_t i=viewStart; i<(int8_t)tc && y<64; i++) {
    bool isSel=i==selectedIdx, isDone=completed[i];
    if (isSel) { oled.fillRect(0,y-1,SCREEN_W,11,SSD1306_WHITE); oled.setTextColor(SSD1306_BLACK); }
    else         oled.setTextColor(SSD1306_WHITE);
    char row[20];
    snprintf(row,sizeof(row),"%c %02d:%02d %-10s",isDone?'v':(isSel?'>':' '),dynTasks[i].h,dynTasks[i].m,dynTasks[i].label);
    oled.setCursor(0,y); oled.print(row);
    if (isSel) oled.setTextColor(SSD1306_WHITE);
    y+=12;
  }
  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  USER SELECT SCREEN  — Display 0: list of 3 real usernames
//                        Displays 1–3: idle
// ═══════════════════════════════════════════════════════════════════════════

void drawUserSelect() {
  // Display 0 — scrollable user list
  tcaSelect(0);
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  drawHeader("SELECT USER", "", false);

  for (int8_t i = 0; i < NUM_PROFILES; i++) {
    int16_t y = 13 + i * 14;
    bool sel = (i == selProfile);
    if (sel) {
      oled.fillRect(0, y-1, SCREEN_W, 13, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }
    oled.setCursor(4, y);
    oled.print(sel ? "> " : "  ");
    oled.print(profileName[i]);
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 56); oled.print("UP/DN=scroll  SEL=ok");
  oled.display();

  // Displays 1–3 — idle hint
  const char* hints[3] = {"WEATHER", "NEWS", "SCHEDULE"};
  for (uint8_t ch = 1; ch < 4; ch++) {
    tcaSelect(ch); oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE);
    drawCentred("Smart Mirror", 22);
    drawCentred(hints[ch-1], 36);
    drawCentred("waiting...", 48);
    oled.display();
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  FINGERPRINT SCREENS
// ═══════════════════════════════════════════════════════════════════════════

void drawFpScan(const char* status = "Place finger...") {
  tcaSelect(0); oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  drawHeader("FINGERPRINT", "", false);
  oled.setTextSize(1); oled.setCursor(0, 13); oled.print("User: ");
  oled.setTextSize(1); oled.print(profileName[selProfile]);
  oled.drawLine(0, 25, 127, 25, SSD1306_WHITE);
  drawCentred(status, 32);
  uint8_t dots = ((millis() - fpScanStart) / 500) % 4;
  oled.setCursor(55, 46); for (uint8_t d=0;d<dots;d++) oled.print(".");
  oled.setCursor(0, 56); oled.print("NEXT=back");
  oled.display();
}

void drawFpEnroll(const char* line) {
  tcaSelect(0); oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextWrap(false);
  drawHeader("ENROLL FINGER", "", false);
  printWrapped(line, 0, 13, 4);
  oled.setCursor(0, 56); oled.print("SEL=start  NEXT=back");
  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  FINGERPRINT SENSOR FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

// Returns matched slot (>0), 0 if no finger yet, -1 if no match
int16_t scanFinger() {
  if (finger.getImage() == FINGERPRINT_NOFINGER) return 0;
  if (finger.getImage() != FINGERPRINT_OK)       return -1;
  if (finger.image2Tz(1) != FINGERPRINT_OK)      return -1;
  if (finger.fingerFastSearch() != FINGERPRINT_OK) return -1;
  return (int16_t)finger.fingerID;
}

bool enrollFinger(uint8_t slot) {
  drawFpEnroll("Place finger\non sensor...");
  uint32_t t = millis();
  while (millis()-t < 15000 && finger.getImage() != FINGERPRINT_OK) delay(100);
  if (finger.getImage() != FINGERPRINT_OK) return false;
  if (finger.image2Tz(1) != FINGERPRINT_OK) return false;
  drawFpEnroll("Remove finger...");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(100);
  drawFpEnroll("Same finger\nagain...");
  t = millis();
  while (millis()-t < 15000 && finger.getImage() != FINGERPRINT_OK) delay(100);
  if (finger.getImage() != FINGERPRINT_OK) return false;
  if (finger.image2Tz(2) != FINGERPRINT_OK) return false;
  if (finger.createModel() != FINGERPRINT_OK) return false;
  if (finger.storeModel(slot) != FINGERPRINT_OK) return false;
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  MODE TRANSITIONS
// ═══════════════════════════════════════════════════════════════════════════

void enterNormalMode() {
  currentMode   = MODE_NORMAL;
  activeDisplay = 3;
  dataReady     = true;
  lastDataFetch = millis();
  lastWaterMs   = millis();
  updateWeatherLED();
  drawGreeting(); drawWeather(); drawNews(); drawSchedule();
}

void enterSelectMode() {
  currentMode   = MODE_SELECT;
  activeProfile = -1;
  dataReady     = false;
  dynName[0]    = '\0';
  dynQuoteCount = dynNewsCount = dynTaskCount = 0;
  memset(completed, 0, sizeof(completed));
  updateWeatherLED();
  drawUserSelect();
}

// ═══════════════════════════════════════════════════════════════════════════
//  OLED INIT
// ═══════════════════════════════════════════════════════════════════════════

bool initOled(uint8_t ch) {
  tcaSelect(ch);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.print(F("OLED ch")); Serial.print(ch); Serial.println(F(": not found")); return false;
  }
  oled.clearDisplay(); oled.display(); return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(200);

  pinMode(BTN_SEL,  INPUT_PULLUP);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_DONE, INPUT_PULLUP);
  pinMode(LED_YELLOW, OUTPUT); digitalWrite(LED_YELLOW, LOW);
  pinMode(LED_BLUE,   OUTPUT); digitalWrite(LED_BLUE,   LOW);
  // GPIO8 = FP_RX — do NOT set as OUTPUT
  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  lastPresenceMs = millis();
  clockRefMs     = millis();

  Serial.println(F("Smart Mirror — starting"));
  for (uint8_t ch = 0; ch < 4; ch++) {
    if (initOled(ch)) { Serial.print(F("OLED ch")); Serial.print(ch); Serial.println(F(" OK")); }
  }

  // Fingerprint sensor on UART1 — TX=D6/GPIO43, RX=D9/GPIO8
  fpSerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);
  finger.begin(57600);
  if (finger.verifyPassword()) { Serial.println(F("FP sensor OK")); fpReady = true; }
  else Serial.println(F("FP sensor not found — check TX=D6, RX=D9"));

  // Connecting splash on all 4 displays
  drawSplash(0, "Connecting...", "");
  drawSplash(1, "WEATHER",  "please wait");
  drawSplash(2, "NEWS",     "please wait");
  drawSplash(3, "SCHEDULE", "please wait");

  connectWiFi();

  // Fetch real names for all profiles from server
  // 1-second gap between calls lets lwIP sockets leave TIME_WAIT state.
  if (WiFi.status() == WL_CONNECTED) {
    for (uint8_t i = 0; i < NUM_PROFILES; i++) {
      char msg[22]; snprintf(msg, sizeof(msg), "Loading %d/%d...", i+1, NUM_PROFILES);
      drawSplash(0, msg, "");
      fetchUserName(i);
      if (i < NUM_PROFILES - 1) delay(1000);
    }
  }

  // Show user select screen
  drawUserSelect();
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  uint32_t now = millis();

  // ── Motion / presence ──────────────────────────────────────────────────
  if (now - lastMotionCheck >= MOTION_CHECK_MS) {
    lastMotionCheck = now;
    float dist = getDistance();
    if (dist < PRESENCE_CM) {
      lastPresenceMs = now;
      if (!displaysAwake) displaysOn();
    } else if (displaysAwake && (now - lastPresenceMs >= SLEEP_MS)) {
      displaysOff();
    }
  }
  if (!displaysAwake) { delay(50); return; }

  // ── Buttons ────────────────────────────────────────────────────────────
  bool selNow  = (digitalRead(BTN_SEL)  == LOW);
  bool btn1    = (digitalRead(BTN_NEXT) == LOW && (now - lastBtn1 >= DEBOUNCE_MS));
  bool btn2    = (digitalRead(BTN_DONE) == LOW && (now - lastBtn2 >= DEBOUNCE_MS));
  bool selEdge = (selNow && !btnSelWasDown && (now - lastBtnSel >= DEBOUNCE_MS));

  if (selNow && !btnSelWasDown) { btnSelDownAt = now; btnSelWasDown = true; }
  if (!selNow) btnSelWasDown = false;

  // Long-press BTN_SEL in dashboard → back to user select
  if (selNow && btnSelWasDown && (now - btnSelDownAt >= LONG_PRESS_MS) && currentMode == MODE_NORMAL) {
    enterSelectMode(); btnSelWasDown = false; delay(500); return;
  }

  // ── MODE: User select ───────────────────────────────────────────────────
  if (currentMode == MODE_SELECT) {
    if (btn1) { lastBtn1 = now; selProfile = (selProfile + NUM_PROFILES - 1) % NUM_PROFILES; drawUserSelect(); }
    if (btn2) { lastBtn2 = now; selProfile = (selProfile + 1) % NUM_PROFILES;                drawUserSelect(); }
    if (selEdge) {
      lastBtnSel = now;
      if (!fpReady) {
        // No sensor — load that user's data directly
        drawSplash(0, "Connecting...", profileName[selProfile]);
        if (fetchDisplayData(PROFILE_TOKEN[selProfile])) { activeProfile = selProfile; enterNormalMode(); }
        else { drawSplash(0, "API error", "check connection"); delay(2000); }
        return;
      }
      currentMode = MODE_FP_SCAN; fpScanStart = now; drawFpScan();
    }
    delay(50); return;
  }

  // ── MODE: Fingerprint scan ──────────────────────────────────────────────
  if (currentMode == MODE_FP_SCAN) {
    if (btn1) { lastBtn1 = now; enterSelectMode(); return; }
    if (now - fpScanStart > FP_SCAN_TIMEOUT) { drawFpScan("Timed out."); delay(1500); enterSelectMode(); return; }
    drawFpScan();
    int16_t matched = scanFinger();
    if (matched == 0) { delay(100); return; }
    if (matched == (int16_t)FP_SLOT[selProfile]) {
      // Correct finger for this user
      drawFpScan("Match! Connecting...");
      if (fetchDisplayData(PROFILE_TOKEN[selProfile])) {
        activeProfile = selProfile; enterNormalMode();
      } else { drawFpScan("API error."); delay(2000); enterSelectMode(); }
      return;
    }
    if (matched > 0) { drawFpScan("Wrong user!"); delay(2000); fpScanStart = millis(); return; }
    // No match → offer enrollment
    currentMode = MODE_FP_ENROLL;
    drawFpEnroll("Finger not enrolled.\nPress SEL to enroll\nfor this user.\nNEXT to go back.");
    delay(50); return;
  }

  // ── MODE: Enroll ────────────────────────────────────────────────────────
  if (currentMode == MODE_FP_ENROLL) {
    if (btn1) { lastBtn1 = now; enterSelectMode(); return; }
    if (selEdge) {
      lastBtnSel = now;
      uint8_t slot = FP_SLOT[selProfile];
      if (enrollFinger(slot)) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Enrolled in slot %d!\nScan again to login.", slot);
        drawFpEnroll(msg); delay(3000);
        currentMode = MODE_FP_SCAN; fpScanStart = millis(); drawFpScan("Now scan finger");
      } else {
        drawFpEnroll("Failed. Try again."); delay(2000); enterSelectMode();
      }
      return;
    }
    delay(50); return;
  }

  // ── MODE: Normal dashboard  (identical to original) ────────────────────
  if (currentMode == MODE_NORMAL) {
    lastPresenceMs = now;

    // Periodic refresh — same user's token
    if (WiFi.status() == WL_CONNECTED && (now - lastDataFetch >= DATA_REFRESH_MS)) {
      if (fetchDisplayData(PROFILE_TOKEN[activeProfile])) {
        lastDataFetch = now;
        drawGreeting(); drawWeather(); drawNews(); drawSchedule();
      }
    }

    // BTN_SEL short press → cycle active display
    if (selEdge) {
      lastBtnSel    = now;
      activeDisplay = (activeDisplay + 1) % 4;
      drawGreeting(); drawWeather(); drawNews(); drawSchedule();
    }

    // Context buttons
    if (btn1 || btn2) {
      if (btn1) lastBtn1 = now;
      if (btn2) lastBtn2 = now;
      uint8_t qc = dynQuoteCount>0?dynQuoteCount:1;
      uint8_t nc = dynNewsCount >0?dynNewsCount :1;
      uint8_t tc = dynTaskCount >0?dynTaskCount :1;
      switch (activeDisplay) {
        case 0:
          if (btn1) { quoteIdx=(quoteIdx+qc-1)%qc; drawGreeting(); }
          if (btn2) { quoteIdx=(quoteIdx+1)%qc;    drawGreeting(); }
          break;
        case 1:
          showFahrenheit = !showFahrenheit; drawWeather(); break;
        case 2:
          if (btn1) { newsIdx=(newsIdx+nc-1)%nc; drawNews(); lastNews=now; }
          if (btn2) { newsIdx=(newsIdx+1)%nc;    drawNews(); lastNews=now; }
          break;
        case 3:
          if (btn1) { selectedIdx=(selectedIdx+1)%tc; drawSchedule(); lastSched=now; }
          if (btn2) { completed[selectedIdx]=!completed[selectedIdx]; drawSchedule(); lastSched=now; }
          break;
      }
    }

    // Water reminder
    if (!waterAlert && (now - lastWaterMs >= dynWaterMs)) {
      waterAlert=true; waterOnAt=now; lastWaterMs=now; drawSchedule();
    }
    if (waterAlert && (now - waterOnAt >= WATER_SHOW_MS)) { waterAlert=false; drawSchedule(); }

    // Auto-refresh displays
    if (now - lastGreet >= GREET_REFRESH_MS) { drawGreeting(); lastGreet=now; }
    if (activeDisplay != 2 && (now - lastNews >= NEWS_ROTATE_MS)) {
      newsIdx=(newsIdx+1)%(dynNewsCount>0?dynNewsCount:1); drawNews(); lastNews=now;
    }
    if (!waterAlert && (now - lastSched >= SCHED_REFRESH_MS)) { drawSchedule(); lastSched=now; }
  }

  delay(50);
}
