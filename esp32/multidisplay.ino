/*
 * Smart Display Mirror — 4-Display Wi-Fi Edition
 *
 * Hardware:
 *   XIAO ESP32-S3  +  TCA9548A (WCMCU-9548)  +  4× SSD1306 OLED 128×64
 *   3× LEDs (yellow/red/blue)  +  HC-SR04 ultrasonic sensor
 *
 * All content (name, quotes, weather, news, schedule, water interval) is
 * fetched live from the backend every 5 minutes.  Displays show
 * "Connecting..." until the first successful fetch.
 *
 * Display assignments:
 *   CH 0 — Greeting  : clock + cycling motivational quotes
 *   CH 1 — Weather   : temperature & condition icon  (°C / °F toggle)
 *   CH 2 — News      : rotating headlines
 *   CH 3 — Schedule  : today's tasks + water reminder
 *
 * 3 buttons:
 *   BTN_SEL  (D2 / GPIO3) : cycle which display the action buttons control
 *   BTN_NEXT (D0 / GPIO1) : prev / next-task
 *   BTN_DONE (D1 / GPIO2) : next / mark-done  |  weather: toggle °C/°F
 *
 * Weather LEDs:
 *   Yellow (D3/GPIO4) : sunny / partly cloudy / windy
 *   Blue   (D8/GPIO7) : cloudy / rain / snow
 *   Red    (D9/GPIO8) : storm / thunder (severe)
 *
 * Auto-sleep (HC-SR04 on D10/GPIO9 + D7/GPIO44):
 *   No presence within 200 cm for 5 min → displays + LEDs off
 *   Motion detected → displays + LEDs wake up instantly
 *
 * Required libraries (Arduino IDE → Library Manager):
 *   Adafruit GFX Library  |  Adafruit SSD1306  |  ArduinoJson
 *
 * Wiring: see README_multidisplay.md
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════════════════════
//  NETWORK CONFIG — fill in before flashing
// ═══════════════════════════════════════════════════════════════════════════


const char* WIFI_SSID     = "Junior 2GHz";
const char* WIFI_PASSWORD = "Pavani@3534";

// Backend URL — use your machine's LAN IP (find with `ip addr` / `ipconfig`)
const char* SERVER_URL    = "http://159.69.83.245:8000";

// Device token — copy from Dashboard → ESP32 Device Key
const char* DEVICE_TOKEN  = "c1663c3e521ed8862b2ce11a8fdddd7d92bdc8a92fc8277bb2d2dca4921bed15";
// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE
// ═══════════════════════════════════════════════════════════════════════════

#define TCA_ADDR    0x70
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

// XIAO ESP32-S3: D4=GPIO5=SDA, D5=GPIO6=SCL
#define SDA_PIN     5
#define SCL_PIN     6

// Buttons — one leg to pin, other to GND, INPUT_PULLUP
#define BTN_SEL     3   // D2 / GPIO3
#define BTN_NEXT    1   // D0 / GPIO1
#define BTN_DONE    2   // D1 / GPIO2
#define DEBOUNCE_MS 200UL

// Weather LEDs — anode to pin via 220Ω resistor, cathode to GND
#define LED_YELLOW  4   // D3 / GPIO4  — sunny / partly cloudy / windy
#define LED_BLUE    7   // D8 / GPIO7  — cloudy / rain / snow
#define LED_RED     8   // D9 / GPIO8  — storm / thunder

// HC-SR04 ultrasonic sensor
#define TRIG_PIN    9   // D10 / GPIO9
#define ECHO_PIN   44   // D7  / GPIO44
#define PRESENCE_CM    200   // cm — closer than this = someone present
#define SLEEP_MS     60000UL // 1 min of no presence → displays off

// ─── Timing ────────────────────────────────────────────────────────────────
#define DATA_REFRESH_MS   60000UL   // refetch from API every 1 min
#define WATER_SHOW_MS      6000UL   // water alert stays on for 6 s
#define GREET_REFRESH_MS  10000UL
#define SCHED_REFRESH_MS  15000UL
#define NEWS_ROTATE_MS     5000UL
#define MOTION_CHECK_MS    500UL    // ultrasonic sample interval

// ═══════════════════════════════════════════════════════════════════════════
//  DYNAMIC DATA BUFFERS  (all filled from API)
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_QUOTES    12
#define MAX_QUOTE_LEN 64
#define MAX_NEWS      10
#define MAX_NEWS_LEN  72
#define MAX_TASKS     20
#define MAX_LABEL_LEN 24

char    dynName[32]      = "";
char    dynQuotes[MAX_QUOTES][MAX_QUOTE_LEN];
uint8_t dynQuoteCount    = 0;
char    dynNews[MAX_NEWS][MAX_NEWS_LEN];
uint8_t dynNewsCount     = 0;

struct DynTask { uint8_t h, m; char label[MAX_LABEL_LEN]; };
DynTask dynTasks[MAX_TASKS];
uint8_t dynTaskCount     = 0;

int16_t  dynTempC        = 0;
char     dynCondition[48] = "";
uint8_t  dynHumidity     = 0;
uint8_t  dynWind         = 0;
char     dynLocation[40] = "";
uint32_t dynWaterMs      = 3600000UL;   // default 60 min until API responds

// Clock synced from server timestamp; counts from 00:00 until first sync
uint32_t clockRefMs  = 0;
uint32_t clockStartS = 0;

bool     dataReady   = false;   // true after first successful API fetch

// ═══════════════════════════════════════════════════════════════════════════
//  UI STATE
// ═══════════════════════════════════════════════════════════════════════════

Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);

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
//  HELPERS
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

// Map condition string (from API) → icon index
// 0=sunny 1=partly 2=cloudy 3=rain 4=storm 5=snow 6=wind
uint8_t condToIcon(const char* cond) {
  String c = String(cond); c.toLowerCase();
  if (c.indexOf("sun")     >= 0 || c.indexOf("clear")    >= 0) return 0;
  if (c.indexOf("partly")  >= 0 || c.indexOf("scattered") >= 0) return 1;
  if (c.indexOf("cloud")   >= 0 || c.indexOf("overcast")  >= 0) return 2;
  if (c.indexOf("rain")    >= 0 || c.indexOf("drizzle")   >= 0
   || c.indexOf("shower")  >= 0)                                 return 3;
  if (c.indexOf("storm")   >= 0 || c.indexOf("thunder")   >= 0) return 4;
  if (c.indexOf("snow")    >= 0 || c.indexOf("flurr")     >= 0
   || c.indexOf("blizzard") >= 0)                                return 5;
  if (c.indexOf("wind")    >= 0 || c.indexOf("breezy")    >= 0
   || c.indexOf("gust")    >= 0)                                 return 6;
  return 1;
}

void drawHeader(const char* left, const char* right, bool active) {
  if (active) {
    oled.fillRect(0, 0, SCREEN_W, 9, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else {
    oled.setTextColor(SSD1306_WHITE);
  }
  oled.setTextSize(1);
  oled.setCursor(2, 1);
  oled.print(left);
  if (right && right[0]) {
    oled.setCursor(SCREEN_W - (int16_t)(strlen(right) * 6) - 2, 1);
    oled.print(right);
  }
  oled.setTextColor(SSD1306_WHITE);
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);
}

// Draw centred text on a single line
void drawCentred(const char* text, int16_t y, uint8_t size = 1) {
  oled.setTextSize(size);
  int16_t w = (int16_t)(strlen(text) * 6 * size);
  oled.setCursor((SCREEN_W - w) / 2, y);
  oled.print(text);
}

void printWrapped(const char* text, int16_t x, int16_t y0, uint8_t maxLines) {
  char    line[22];
  int16_t y   = y0;
  int     pos = 0, len = strlen(text);
  uint8_t done = 0;
  while (pos < len && done < maxLines) {
    int rem  = len - pos;
    int take = (rem > 21) ? 21 : rem;
    if (rem > 21) {
      int brk = take;
      while (brk > 10 && text[pos + brk] != ' ') brk--;
      if (text[pos + brk] == ' ') take = brk;
    }
    strncpy(line, text + pos, take); line[take] = '\0';
    oled.setCursor(x, y); oled.print(line);
    pos += take;
    if (pos < len && text[pos] == ' ') pos++;
    y += 10; done++;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER LEDs
// ═══════════════════════════════════════════════════════════════════════════

// icon: 0=sunny 1=partly 2=cloudy 3=rain 4=storm 5=snow 6=wind
void updateWeatherLED() {
  bool on = dataReady && displaysAwake;
  uint8_t icon = on ? condToIcon(dynCondition) : 255;
  digitalWrite(LED_YELLOW, (on && (icon == 0 || icon == 1 || icon == 6)) ? HIGH : LOW);
  digitalWrite(LED_BLUE,   (on && (icon == 2 || icon == 3 || icon == 5)) ? HIGH : LOW);
  digitalWrite(LED_RED,    (on &&  icon == 4)                             ? HIGH : LOW);
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY SLEEP / WAKE
// ═══════════════════════════════════════════════════════════════════════════

void displaysOff() {
  for (uint8_t ch = 0; ch < 4; ch++) {
    tcaSelect(ch);
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
  }
  Wire.beginTransmission(TCA_ADDR); Wire.write(0); Wire.endTransmission();
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_BLUE,   LOW);
  digitalWrite(LED_RED,    LOW);
  displaysAwake = false;
  Serial.println(F("Sleep."));
}

void displaysOn() {
  for (uint8_t ch = 0; ch < 4; ch++) {
    tcaSelect(ch);
    oled.ssd1306_command(SSD1306_DISPLAYON);
  }
  displaysAwake = true;
  updateWeatherLED();
  drawGreeting(); drawWeather(); drawNews(); drawSchedule();
  Serial.println(F("Wake."));
}

// ═══════════════════════════════════════════════════════════════════════════
//  ULTRASONIC SENSOR
// ═══════════════════════════════════════════════════════════════════════════

// Returns distance in cm; 999 if no echo (nothing in range)
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000UL); // 30 ms timeout ≈ 5 m range
  return (dur == 0) ? 999.0f : dur * 0.0343f / 2.0f;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WI-FI & API
// ═══════════════════════════════════════════════════════════════════════════

void connectWiFi() {
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWi-Fi OK — %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\nWi-Fi failed.");
}

bool fetchDisplayData() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String url = String(SERVER_URL) + "/api/multidisplay/data";
  HTTPClient http;
  http.begin(url);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.setTimeout(10000);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("API error %d\n", code);
    http.end(); return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    Serial.println("JSON parse error");
    return false;
  }

  // ── User name ─────────────────────────────────────────────────────────
  strlcpy(dynName, doc["user"]["username"] | "", sizeof(dynName));

  // ── Quotes ────────────────────────────────────────────────────────────
  dynQuoteCount = 0;
  for (JsonVariant q : doc["quotes"].as<JsonArray>()) {
    if (dynQuoteCount >= MAX_QUOTES) break;
    strlcpy(dynQuotes[dynQuoteCount++], q.as<const char*>(), MAX_QUOTE_LEN);
  }

  // ── Weather ───────────────────────────────────────────────────────────
  dynTempC    = doc["weather"]["temperature"] | (int16_t)0;
  dynHumidity = doc["weather"]["humidity"]    | (uint8_t)0;
  dynWind     = doc["weather"]["wind_speed"]  | (uint8_t)0;
  strlcpy(dynCondition, doc["weather"]["condition"] | "", sizeof(dynCondition));
  strlcpy(dynLocation,  doc["weather"]["location"]  | "", sizeof(dynLocation));

  // ── News ──────────────────────────────────────────────────────────────
  dynNewsCount = 0;
  for (JsonVariant h : doc["news"]["headlines"].as<JsonArray>()) {
    if (dynNewsCount >= MAX_NEWS) break;
    strlcpy(dynNews[dynNewsCount++], h.as<const char*>(), MAX_NEWS_LEN);
  }

  // ── Schedule ──────────────────────────────────────────────────────────
  bool resetSel = (dynTaskCount == 0);
  dynTaskCount = 0;
  for (JsonVariant item : doc["schedule"]["items"].as<JsonArray>()) {
    if (dynTaskCount >= MAX_TASKS) break;
    const char* t = item["time"] | "00:00";
    dynTasks[dynTaskCount].h = atoi(t);
    dynTasks[dynTaskCount].m = atoi(t + 3);
    strlcpy(dynTasks[dynTaskCount].label, item["task"] | "", MAX_LABEL_LEN);
    dynTaskCount++;
  }

  // ── Water interval ────────────────────────────────────────────────────
  uint32_t wim = doc["settings"]["water_interval_min"] | (uint32_t)60;
  dynWaterMs   = wim * 60000UL;

  // ── Sync clock to server timestamp ────────────────────────────────────
  const char* ts = doc["timestamp"] | "";
  if (strlen(ts) >= 16) {
    uint8_t sh = atoi(ts + 11);
    uint8_t sm = atoi(ts + 14);
    clockStartS = (uint32_t)sh * 3600 + (uint32_t)sm * 60;
    clockRefMs  = millis();
    Serial.printf("Clock synced → %02d:%02d\n", sh, sm);
  }

  // Place cursor on the current-time task on first fetch
  if (resetSel && dynTaskCount > 0) {
    uint8_t h, m; simTime(h, m);
    uint16_t nowMins = (uint16_t)h * 60 + m;
    for (int8_t i = 0; i < (int8_t)dynTaskCount; i++)
      if ((uint16_t)dynTasks[i].h * 60 + dynTasks[i].m <= nowMins) selectedIdx = i;
  }

  // Clamp UI indices to new data sizes
  if (dynQuoteCount > 0 && quoteIdx >= dynQuoteCount) quoteIdx = 0;
  if (dynNewsCount  > 0 && newsIdx  >= dynNewsCount)  newsIdx  = 0;
  if (dynTaskCount  > 0 && selectedIdx >= dynTaskCount) selectedIdx = 0;

  updateWeatherLED();

  Serial.printf("Data OK — %d quotes, %d news, %d tasks\n",
                dynQuoteCount, dynNewsCount, dynTaskCount);
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  WEATHER ICONS  (icon area x:0-47, centre cx=24, cy=29)
// ═══════════════════════════════════════════════════════════════════════════

void iconCloud(int16_t cx, int16_t cy) {
  oled.fillCircle(cx - 8, cy + 2, 6, SSD1306_WHITE);
  oled.fillCircle(cx,     cy - 2, 8, SSD1306_WHITE);
  oled.fillCircle(cx + 8, cy + 2, 6, SSD1306_WHITE);
  oled.fillRect(cx - 14, cy + 2, 28, 8, SSD1306_WHITE);
}

void iconSun(int16_t cx, int16_t cy, uint8_t r) {
  oled.drawCircle(cx, cy, r, SSD1306_WHITE);
  uint8_t gap = r + 3, tip = r + 5;
  uint8_t d1 = (uint8_t)(gap * 0.7f), d2 = (uint8_t)(tip * 0.7f);
  oled.drawLine(cx,       cy - gap, cx,       cy - tip, SSD1306_WHITE);
  oled.drawLine(cx,       cy + gap, cx,       cy + tip, SSD1306_WHITE);
  oled.drawLine(cx - gap, cy,       cx - tip, cy,       SSD1306_WHITE);
  oled.drawLine(cx + gap, cy,       cx + tip, cy,       SSD1306_WHITE);
  oled.drawLine(cx - d1, cy - d1, cx - d2, cy - d2, SSD1306_WHITE);
  oled.drawLine(cx + d1, cy - d1, cx + d2, cy - d2, SSD1306_WHITE);
  oled.drawLine(cx - d1, cy + d1, cx - d2, cy + d2, SSD1306_WHITE);
  oled.drawLine(cx + d1, cy + d1, cx + d2, cy + d2, SSD1306_WHITE);
}

void drawWeatherIcon(uint8_t idx) {
  const int16_t cx = 24, cy = 29;
  switch (idx) {
    case 0: iconSun(cx, cy, 8); break;
    case 1:
      oled.drawCircle(cx + 8, cy - 9, 5, SSD1306_WHITE);
      oled.drawLine(cx + 8, cy - 16, cx + 8, cy - 18, SSD1306_WHITE);
      oled.drawLine(cx + 15, cy - 9,  cx + 17, cy - 9,  SSD1306_WHITE);
      oled.drawLine(cx + 13, cy - 14, cx + 15, cy - 16, SSD1306_WHITE);
      iconCloud(cx - 3, cy + 3);
      break;
    case 2: iconCloud(cx, cy); break;
    case 3:
      iconCloud(cx, cy - 5);
      for (int8_t i = 0; i < 4; i++)
        oled.drawLine(cx - 9 + i * 6, cy + 8, cx - 11 + i * 6, cy + 14, SSD1306_WHITE);
      break;
    case 4:
      iconCloud(cx, cy - 7);
      oled.drawLine(cx + 3, cy + 3,  cx - 2, cy + 9,  SSD1306_WHITE);
      oled.drawLine(cx - 2, cy + 9,  cx + 4, cy + 9,  SSD1306_WHITE);
      oled.drawLine(cx + 4, cy + 9,  cx - 1, cy + 15, SSD1306_WHITE);
      break;
    case 5: {
      iconCloud(cx, cy - 7);
      for (int8_t i = 0; i < 3; i++) {
        int16_t sx = cx - 8 + i * 8, sy = cy + 10;
        oled.drawLine(sx - 3, sy,     sx + 3, sy,     SSD1306_WHITE);
        oled.drawLine(sx,     sy - 3, sx,     sy + 3, SSD1306_WHITE);
        oled.drawLine(sx - 2, sy - 2, sx + 2, sy + 2, SSD1306_WHITE);
        oled.drawLine(sx + 2, sy - 2, sx - 2, sy + 2, SSD1306_WHITE);
      }
      break;
    }
    case 6:
      for (int8_t i = 0; i < 3; i++) {
        int16_t y = cy - 8 + i * 8;
        oled.drawLine(cx - 14, y,      cx - 5,  y,      SSD1306_WHITE);
        oled.drawLine(cx - 5,  y,      cx,      y - 3,  SSD1306_WHITE);
        oled.drawLine(cx,      y - 3,  cx + 5,  y,      SSD1306_WHITE);
        oled.drawLine(cx + 5,  y,      cx + 14, y,      SSD1306_WHITE);
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SPLASH — shown on all 4 displays while connecting
// ═══════════════════════════════════════════════════════════════════════════

void drawSplash(uint8_t ch, const char* line1, const char* line2 = "") {
  tcaSelect(ch);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  drawCentred(line1, 22);
  if (line2[0]) drawCentred(line2, 38);
  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void drawGreeting() {
  uint8_t h, m;
  simTime(h, m);

  tcaSelect(0);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  drawHeader("GREETING", "", activeDisplay == 0);

  if (!dataReady) {
    drawCentred("Connecting...", 28);
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    oled.setCursor(80, 56); oled.print(buf);
    oled.display(); return;
  }

  const char* period = (h < 12) ? "Morning" : (h < 17) ? "Afternoon" : "Evening";
  oled.setTextSize(1);
  oled.setCursor(0, 12); oled.print("Good "); oled.print(period);

  oled.setTextSize(2);
  oled.setCursor(0, 22);
  oled.print(dynName); oled.print("!");

  oled.drawLine(0, 40, 127, 40, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 43);
  if (dynQuoteCount > 0)
    oled.print(dynQuotes[quoteIdx % dynQuoteCount]);

  char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  oled.setCursor(80, 56); oled.print(buf);

  if (activeDisplay == 0) { oled.setCursor(0, 56); oled.print("< >"); }

  oled.display();
}

void drawWeather() {
  tcaSelect(1);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  if (!dataReady) {
    drawHeader("WEATHER", "", activeDisplay == 1);
    drawCentred("Fetching...", 30);
    oled.display(); return;
  }

  drawHeader("WEATHER", showFahrenheit ? "F" : "C", activeDisplay == 1);

  int16_t displayTemp = showFahrenheit
    ? (int16_t)((int32_t)dynTempC * 9 / 5 + 32)
    : dynTempC;
  char unitCh = showFahrenheit ? 'F' : 'C';

  drawWeatherIcon(condToIcon(dynCondition));

  char tempStr[6];
  snprintf(tempStr, sizeof(tempStr), "%d", displayTemp);
  oled.setTextSize(3);
  oled.setCursor(54, 13); oled.print(tempStr);
  oled.setTextSize(1);
  oled.setCursor(54 + (int16_t)strlen(tempStr) * 18, 13);
  oled.print((char)248); oled.print(unitCh);

  oled.setTextSize(1);
  oled.setCursor(54, 38); oled.print(dynLocation);

  oled.drawLine(0, 47, 127, 47, SSD1306_WHITE);
  oled.setCursor(0, 49); oled.print(dynCondition);

  char stats[20];
  snprintf(stats, sizeof(stats), "H:%d%%  W:%dkm/h", dynHumidity, dynWind);
  oled.setCursor(0, 56); oled.print(stats);

  if (activeDisplay == 1) { oled.setCursor(100, 56); oled.print("C/F"); }

  oled.display();
}

void drawNews() {
  tcaSelect(2);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  if (!dataReady || dynNewsCount == 0) {
    drawHeader("NEWS", "", activeDisplay == 2);
    drawCentred("Fetching...", 30);
    oled.display(); return;
  }

  uint8_t cnt = dynNewsCount;
  char counter[6];
  snprintf(counter, sizeof(counter), "%d/%d", (newsIdx % cnt) + 1, cnt);
  drawHeader("NEWS", counter, activeDisplay == 2);

  printWrapped(dynNews[newsIdx % cnt], 0, 13, 4);

  if (activeDisplay == 2) {
    oled.setCursor(14, 56); oled.print("< prev  next >");
  } else {
    int16_t dotBaseX = (SCREEN_W - cnt * 8) / 2;
    for (uint8_t i = 0; i < cnt; i++) {
      int16_t dotCx = dotBaseX + i * 8 + 3;
      if (i == newsIdx % cnt) oled.fillCircle(dotCx, 60, 3, SSD1306_WHITE);
      else                    oled.drawCircle(dotCx, 60, 3, SSD1306_WHITE);
    }
  }

  oled.display();
}

void drawSchedule() {
  tcaSelect(3);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  if (waterAlert) {
    oled.setTextSize(1); oled.setCursor(22, 3); oled.print("- REMINDER -");
    oled.drawLine(0, 13, 127, 13, SSD1306_WHITE);
    oled.setTextSize(2); oled.setCursor(16, 18); oled.print("DRINK");
    oled.setCursor(16, 37); oled.print("WATER!");
    oled.setTextSize(1); oled.setCursor(18, 56); oled.print("Stay hydrated :)");
    oled.display(); return;
  }

  if (!dataReady || dynTaskCount == 0) {
    drawHeader("SCHEDULE", "", activeDisplay == 3);
    drawCentred(!dataReady ? "Connecting..." : "No tasks today", 30);
    oled.display(); return;
  }

  uint8_t tc = dynTaskCount;
  uint8_t doneCount = 0;
  for (uint8_t i = 0; i < tc; i++) if (completed[i]) doneCount++;
  char progress[6];
  snprintf(progress, sizeof(progress), "%d/%d", doneCount, tc);
  drawHeader("SCHEDULE", progress, activeDisplay == 3);

  int8_t viewStart = selectedIdx - 1;
  if (viewStart > (int8_t)tc - 4) viewStart = (int8_t)tc - 4;
  if (viewStart < 0) viewStart = 0;

  int16_t y = 12;
  for (int8_t i = viewStart; i < (int8_t)tc && y < 64; i++) {
    bool isSel  = (i == selectedIdx);
    bool isDone = completed[i];

    if (isSel) {
      oled.fillRect(0, y - 1, SCREEN_W, 11, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK);
    } else {
      oled.setTextColor(SSD1306_WHITE);
    }

    char indicator = isDone ? 'v' : (isSel ? '>' : ' ');
    char row[20];
    snprintf(row, sizeof(row), "%c %02d:%02d %-10s",
             indicator, dynTasks[i].h, dynTasks[i].m, dynTasks[i].label);
    oled.setCursor(0, y); oled.print(row);

    if (isSel) oled.setTextColor(SSD1306_WHITE);
    y += 12;
  }

  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  OLED INIT
// ═══════════════════════════════════════════════════════════════════════════

bool initOled(uint8_t ch) {
  tcaSelect(ch);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.print(F("OLED ch")); Serial.print(ch); Serial.println(F(": not found"));
    return false;
  }
  oled.clearDisplay(); oled.display();
  return true;
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
  pinMode(LED_RED,    OUTPUT); digitalWrite(LED_RED,    LOW);

  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  lastPresenceMs = millis();

  Serial.println(F("Smart Display Mirror — starting"));
  for (uint8_t ch = 0; ch < 4; ch++) {
    if (initOled(ch)) { Serial.print(F("  ch")); Serial.print(ch); Serial.println(F(" OK")); }
  }

  clockRefMs = millis();

  // Show connecting splash on all 4 displays
  drawSplash(0, "GREETING",  "Connecting...");
  drawSplash(1, "WEATHER",   "Connecting...");
  drawSplash(2, "NEWS",      "Connecting...");
  drawSplash(3, "SCHEDULE",  "Connecting...");

  // Connect to Wi-Fi and fetch data
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    if (fetchDisplayData()) {
      dataReady     = true;
      lastDataFetch = millis();
    }
  }

  // Draw live data (or "Fetching..." if API failed)
  drawGreeting(); drawWeather(); drawNews(); drawSchedule();

  lastWaterMs = millis();
  Serial.println(F("Running."));
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  uint32_t now = millis();

  // ── Motion / presence check (always runs, even when sleeping) ──────────
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

  // Skip all display and button logic while sleeping
  if (!displaysAwake) { delay(50); return; }

  // ── Periodic API refresh ────────────────────────────────────────────────
  if (WiFi.status() == WL_CONNECTED && (now - lastDataFetch >= DATA_REFRESH_MS)) {
    if (fetchDisplayData()) {
      dataReady     = true;
      lastDataFetch = now;
      drawGreeting(); drawWeather(); drawNews(); drawSchedule();
    }
  }

  // ── BTN_SEL: cycle active display ───────────────────────────────────────
  if (digitalRead(BTN_SEL) == LOW && (now - lastBtnSel >= DEBOUNCE_MS)) {
    lastBtnSel    = now;
    lastPresenceMs = now;   // button press counts as presence
    activeDisplay = (activeDisplay + 1) % 4;
    drawGreeting(); drawWeather(); drawNews(); drawSchedule();
    Serial.printf("Active: %d\n", activeDisplay);
  }

  // ── Context buttons ─────────────────────────────────────────────────────
  bool btn1 = (digitalRead(BTN_NEXT) == LOW && (now - lastBtn1 >= DEBOUNCE_MS));
  bool btn2 = (digitalRead(BTN_DONE) == LOW && (now - lastBtn2 >= DEBOUNCE_MS));

  if (btn1 || btn2) lastPresenceMs = now;   // any button press counts as presence

  if ((btn1 || btn2) && dataReady) {
    uint8_t qc = dynQuoteCount > 0 ? dynQuoteCount : 1;
    uint8_t nc = dynNewsCount  > 0 ? dynNewsCount  : 1;
    uint8_t tc = dynTaskCount  > 0 ? dynTaskCount  : 1;

    switch (activeDisplay) {
      case 0:  // Greeting — cycle quotes
        if (btn1) { lastBtn1 = now; quoteIdx = (quoteIdx + qc - 1) % qc; drawGreeting(); }
        if (btn2) { lastBtn2 = now; quoteIdx = (quoteIdx + 1) % qc;       drawGreeting(); }
        break;

      case 1:  // Weather — toggle °C / °F
        if (btn1 || btn2) {
          if (btn1) lastBtn1 = now;
          if (btn2) lastBtn2 = now;
          showFahrenheit = !showFahrenheit;
          drawWeather();
        }
        break;

      case 2:  // News — manual scroll
        if (btn1) { lastBtn1 = now; newsIdx = (newsIdx + nc - 1) % nc; drawNews(); lastNews = now; }
        if (btn2) { lastBtn2 = now; newsIdx = (newsIdx + 1) % nc;      drawNews(); lastNews = now; }
        break;

      case 3:  // Schedule
        if (btn1) { lastBtn1 = now; selectedIdx = (selectedIdx + 1) % tc; drawSchedule(); lastSched = now; }
        if (btn2) {
          lastBtn2 = now;
          completed[selectedIdx] = !completed[selectedIdx];
          drawSchedule(); lastSched = now;
          Serial.printf("Task %d: %s\n", selectedIdx,
                        completed[selectedIdx] ? "DONE" : "undone");
        }
        break;
    }
  }

  // ── Water reminder ──────────────────────────────────────────────────────
  if (dataReady && !waterAlert && (now - lastWaterMs >= dynWaterMs)) {
    waterAlert = true; waterOnAt = now; lastWaterMs = now;
    drawSchedule();
  }
  if (waterAlert && (now - waterOnAt >= WATER_SHOW_MS)) {
    waterAlert = false; drawSchedule();
  }

  // ── Auto-refresh ────────────────────────────────────────────────────────
  if (now - lastGreet >= GREET_REFRESH_MS) { drawGreeting(); lastGreet = now; }

  if (activeDisplay != 2 && dataReady && (now - lastNews >= NEWS_ROTATE_MS)) {
    newsIdx = (newsIdx + 1) % (dynNewsCount > 0 ? dynNewsCount : 1);
    drawNews(); lastNews = now;
  }

  if (!waterAlert && (now - lastSched >= SCHED_REFRESH_MS)) { drawSchedule(); lastSched = now; }

  delay(50);
}
