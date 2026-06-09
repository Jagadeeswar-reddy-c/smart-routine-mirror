/*
 * Smart Display Mirror — 4-Display Offline Edition
 *
 * Hardware:
 *   ESP32-S3  +  TCA9548A (WCMCU-9548)  +  4× SSD1306 OLED 128×64
 *
 * No Wi-Fi — all content is pre-configured in this file.
 *
 * Display assignments (TCA9548A channel → content):
 *   CH 0 — Greeting  : time-aware welcome message + simulated clock
 *   CH 1 — Weather   : hardcoded temperature & conditions
 *   CH 2 — News      : rotating pre-built headlines (every 5 s)
 *   CH 3 — Schedule  : daily task list + hourly water reminder
 *
 * Required libraries (Arduino IDE → Library Manager):
 *   • Adafruit GFX Library
 *   • Adafruit SSD1306
 *
 * Wiring: see README_multidisplay.md in this folder.
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════════════════════
//  USER CONFIG — edit before flashing
// ═══════════════════════════════════════════════════════════════════════════

#define USER_NAME      "Alex"   // name shown on greeting display

// Simulated start time at boot (no RTC / NTP needed)
// millis() advances the clock from this point forward.
#define START_HOUR     7        // 0–23
#define START_MINUTE   30       // 0–59

// Hardcoded weather values
#define TEMP_C         18
#define WEATHER_COND   "Partly Cloudy"
#define HUMIDITY_PCT   65
#define WIND_KMH       12
#define WEATHER_LOC    "Your City"

// News headlines — add / remove lines as needed
const char* const NEWS[] = {
  "Scientists find new Amazon species",
  "Renewable energy hits record high",
  "Space telescope snaps distant galaxy",
  "AI safety tops global tech summit",
  "Local team wins national title",
  "Walking shown to boost brain health",
  "New ocean cleanup tech deployed",
  "Record harvest across midwest farms",
};
const uint8_t NEWS_COUNT = sizeof(NEWS) / sizeof(NEWS[0]);

// Daily schedule — sorted by time
struct Task { uint8_t h, m; const char* label; };
const Task SCHED[] = {
  {  7, 30, "Exercise"   },
  {  9,  0, "Study"      },
  { 12,  0, "Lunch"      },
  { 14,  0, "Study"      },
  { 16,  0, "Play"       },
  { 19,  0, "Walk"       },
  { 21,  0, "Wind Down"  },
};
const uint8_t SCHED_COUNT = sizeof(SCHED) / sizeof(SCHED[0]);

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

// TCA9548A I2C address (A0=A1=A2=GND → 0x70)
#define TCA_ADDR    0x70
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H    64

// XIAO ESP32-S3 I2C pins (D4 = GPIO5 = SDA, D5 = GPIO6 = SCL)
#define SDA_PIN     5
#define SCL_PIN     6

// Schedule buttons (connect pin to GND when pressed; uses INPUT_PULLUP)
//   D0 = GPIO1 → Button 1: move cursor to next task
//   D1 = GPIO2 → Button 2: toggle selected task complete / incomplete
#define BTN_NEXT    1   // D0
#define BTN_DONE    2   // D1
#define DEBOUNCE_MS 200UL

// ─── Timing ────────────────────────────────────────────────────────────────
#define NEWS_ROTATE_MS    5000UL   // rotate headline every 5 s
#define WATER_EVERY_MS 3600000UL   // water reminder every 60 min
#define WATER_SHOW_MS     6000UL   // show water alert for 6 s
#define GREET_REFRESH_MS 10000UL   // redraw greeting every 10 s
#define SCHED_REFRESH_MS 15000UL   // redraw schedule every 15 s

// ═══════════════════════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════════════════════

Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &Wire, -1);

uint8_t  newsIdx      = 0;
bool     waterAlert   = false;
uint32_t lastWaterMs  = 0;
uint32_t waterOnAt    = 0;
uint32_t lastGreet    = 0;
uint32_t lastNews     = 0;
uint32_t lastSched    = 0;

// Schedule button state
int8_t   selectedIdx  = 0;                    // which task the cursor is on
bool     completed[SCHED_COUNT] = {};         // true = task marked done
uint32_t lastBtn1     = 0;
uint32_t lastBtn2     = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  TCA9548A — channel select
// ═══════════════════════════════════════════════════════════════════════════

void tcaSelect(uint8_t ch) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(1 << ch);
  Wire.endTransmission();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Simulated clock (advances from START_HOUR:START_MINUTE via millis)
// ═══════════════════════════════════════════════════════════════════════════

void simTime(uint8_t &h, uint8_t &m) {
  uint32_t s = (uint32_t)START_HOUR * 3600
             + (uint32_t)START_MINUTE * 60
             + millis() / 1000;
  s %= 86400;
  h = s / 3600;
  m = (s % 3600) / 60;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CH 0 — Greeting display
// ═══════════════════════════════════════════════════════════════════════════

void drawGreeting() {
  uint8_t h, m;
  simTime(h, m);

  const char* period = (h < 12) ? "Morning"
                     : (h < 17) ? "Afternoon"
                     :            "Evening";

  tcaSelect(0);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  // "Good Morning" line
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Good ");
  oled.print(period);

  // Name in large font
  oled.setTextSize(2);
  oled.setCursor(0, 13);
  oled.print(USER_NAME);
  oled.print("!");

  oled.drawLine(0, 34, 127, 34, SSD1306_WHITE);

  // Simulated clock
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  oled.setTextSize(2);
  oled.setCursor(24, 42);
  oled.print(buf);

  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  CH 1 — Weather display
// ═══════════════════════════════════════════════════════════════════════════

void drawWeather() {
  tcaSelect(1);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  // Header
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("WEATHER");
  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Big temperature number
  oled.setTextSize(4);
  oled.setCursor(0, 14);
  oled.print(TEMP_C);

  // Unit beside it (smaller)
  oled.setTextSize(2);
  oled.setCursor(52, 14);
  // character 248 = degree sign in CP437 (Adafruit GFX default font)
  oled.print((char)248);
  oled.print("C");

  // Location
  oled.setTextSize(1);
  oled.setCursor(52, 38);
  oled.print(WEATHER_LOC);

  oled.drawLine(0, 48, 127, 48, SSD1306_WHITE);

  // Condition + stats
  oled.setCursor(0, 51);
  oled.print(WEATHER_COND);
  char stats[22];
  snprintf(stats, sizeof(stats), "H:%d%%  W:%dkm/h", HUMIDITY_PCT, WIND_KMH);
  oled.setCursor(0, 57);
  oled.print(stats);

  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  CH 2 — News display
// ═══════════════════════════════════════════════════════════════════════════

// Word-wrap text into lines of max 21 chars (text size 1 on 128-px screen)
void printWrapped(const char* text, int16_t x, int16_t startY, uint8_t maxLines) {
  char   line[22];
  int16_t y        = startY;
  int     pos      = 0;
  int     len      = strlen(text);
  uint8_t linesDone = 0;

  while (pos < len && linesDone < maxLines) {
    int remaining = len - pos;
    int take      = (remaining > 21) ? 21 : remaining;

    // Walk back to last space so we don't break mid-word
    if (remaining > 21) {
      int brk = take;
      while (brk > 10 && text[pos + brk] != ' ') brk--;
      if (text[pos + brk] == ' ') take = brk;
    }

    strncpy(line, text + pos, take);
    line[take] = '\0';

    oled.setCursor(x, y);
    oled.print(line);

    pos += take;
    if (pos < len && text[pos] == ' ') pos++; // skip leading space on next line
    y += 10;
    linesDone++;
  }
}

void drawNews() {
  tcaSelect(2);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  // Header
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("NEWS");

  // Counter  e.g. "3/8" — right-aligned
  char counter[6];
  snprintf(counter, sizeof(counter), "%d/%d", newsIdx + 1, NEWS_COUNT);
  oled.setCursor(128 - (int16_t)(strlen(counter) * 6), 0);
  oled.print(counter);

  oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Headline, word-wrapped into up to 4 lines
  oled.setTextSize(1);
  printWrapped(NEWS[newsIdx], 0, 13, 4);

  // Progress dots along the bottom
  // Each dot: diameter 6px + 2px gap = 8px each; centre at dotBaseX + i*8 + 3
  int16_t dotBaseX = (SCREEN_W - NEWS_COUNT * 8) / 2;
  for (uint8_t i = 0; i < NEWS_COUNT; i++) {
    int16_t cx = dotBaseX + i * 8 + 3;
    if (i == newsIdx)
      oled.fillCircle(cx, 60, 3, SSD1306_WHITE);
    else
      oled.drawCircle(cx, 60, 3, SSD1306_WHITE);
  }

  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  CH 3 — Schedule + water-reminder display
//
//  Row format (text size 1, 18 chars):
//    indicator  HH:MM  TaskName
//    ' '        normal task
//    '>'        cursor (selected, not done)  → inverted row
//    'v'        completed                    → 'v' prefix, normal colours
//    '*'        cursor on a completed task   → inverted row + 'v' shown
// ═══════════════════════════════════════════════════════════════════════════

void drawSchedule() {
  tcaSelect(3);
  oled.clearDisplay();
  oled.setTextWrap(false);
  oled.setTextColor(SSD1306_WHITE);

  if (waterAlert) {
    // ── Water reminder overlay ──────────────────────────────────────────
    oled.setTextSize(1);
    oled.setCursor(22, 3);
    oled.print("- REMINDER -");
    oled.drawLine(0, 13, 127, 13, SSD1306_WHITE);

    oled.setTextSize(2);
    oled.setCursor(16, 18);
    oled.print("DRINK");
    oled.setCursor(16, 37);
    oled.print("WATER!");

    oled.setTextSize(1);
    oled.setCursor(18, 56);
    oled.print("Stay hydrated :)");

  } else {
    // ── Header: "SCHEDULE" left, done count right ───────────────────────
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("SCHEDULE");

    uint8_t doneCount = 0;
    for (uint8_t i = 0; i < SCHED_COUNT; i++) if (completed[i]) doneCount++;
    char progress[6];
    snprintf(progress, sizeof(progress), "%d/%d", doneCount, SCHED_COUNT);
    oled.setCursor(128 - (int16_t)(strlen(progress) * 6), 0);
    oled.print(progress);

    oled.drawLine(0, 9, 127, 9, SSD1306_WHITE);

    // ── Scroll window: keep selectedIdx visible ─────────────────────────
    // Show 4 rows; try to keep one task above selected in view.
    int8_t viewStart = selectedIdx - 1;
    if (viewStart > (int8_t)SCHED_COUNT - 4) viewStart = (int8_t)SCHED_COUNT - 4;
    if (viewStart < 0) viewStart = 0;

    int16_t y = 12;
    for (int8_t i = viewStart; i < SCHED_COUNT && y < 64; i++) {
      bool isSel  = (i == selectedIdx);
      bool isDone = completed[i];

      // Inverted background for selected row
      if (isSel) {
        oled.fillRect(0, y - 1, SCREEN_W, 11, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
      } else {
        oled.setTextColor(SSD1306_WHITE);
      }

      // Indicator: 'v' = done, '>' = cursor (not done), ' ' = normal
      char indicator = isDone ? 'v' : (isSel ? '>' : ' ');

      char row[20];
      snprintf(row, sizeof(row), "%c %02d:%02d %-10s",
               indicator, SCHED[i].h, SCHED[i].m, SCHED[i].label);
      oled.setCursor(0, y);
      oled.print(row);

      if (isSel) oled.setTextColor(SSD1306_WHITE);
      y += 12;
    }
  }

  oled.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Init helper — select channel, init display, blank it
// ═══════════════════════════════════════════════════════════════════════════

bool initOled(uint8_t ch) {
  tcaSelect(ch);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.print(F("OLED ch")); Serial.print(ch); Serial.println(F(": not found — check wiring"));
    return false;
  }
  oled.clearDisplay();
  oled.display();
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(200);

  // Buttons: pressed = LOW (internal pull-up, other leg to GND)
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_DONE, INPUT_PULLUP);

  Serial.println(F("Smart Display Mirror — starting"));

  for (uint8_t ch = 0; ch < 4; ch++) {
    if (initOled(ch)) {
      Serial.print(F("  Display ")); Serial.print(ch); Serial.println(F(" OK"));
    }
  }

  // Start cursor on whichever task is current by simulated time
  uint8_t h, m;
  simTime(h, m);
  uint16_t nowMins = (uint16_t)h * 60 + m;
  for (int8_t i = 0; i < SCHED_COUNT; i++) {
    if ((uint16_t)SCHED[i].h * 60 + SCHED[i].m <= nowMins) selectedIdx = i;
  }

  // Initial full draw
  drawGreeting();
  drawWeather();
  drawNews();
  drawSchedule();

  lastWaterMs = millis();

  Serial.println(F("Running."));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Loop
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  uint32_t now = millis();

  // ── Button 1: move cursor to next task ─────────────────────────────────
  if (digitalRead(BTN_NEXT) == LOW && (now - lastBtn1 >= DEBOUNCE_MS)) {
    lastBtn1    = now;
    selectedIdx = (selectedIdx + 1) % SCHED_COUNT;
    drawSchedule();
    lastSched = now; // reset auto-refresh timer
  }

  // ── Button 2: toggle selected task complete / incomplete ────────────────
  if (digitalRead(BTN_DONE) == LOW && (now - lastBtn2 >= DEBOUNCE_MS)) {
    lastBtn2              = now;
    completed[selectedIdx] = !completed[selectedIdx];
    drawSchedule();
    lastSched = now;
    Serial.printf("Task %d (%s): %s\n",
                  selectedIdx, SCHED[selectedIdx].label,
                  completed[selectedIdx] ? "DONE" : "not done");
  }

  // ── Water reminder: trigger ─────────────────────────────────────────────
  if (!waterAlert && (now - lastWaterMs >= WATER_EVERY_MS)) {
    waterAlert  = true;
    waterOnAt   = now;
    lastWaterMs = now;
    drawSchedule();
  }

  // ── Water reminder: dismiss after WATER_SHOW_MS ─────────────────────────
  if (waterAlert && (now - waterOnAt >= WATER_SHOW_MS)) {
    waterAlert = false;
    drawSchedule();
  }

  // ── Greeting: refresh every 10 s (clock ticks) ──────────────────────────
  if (now - lastGreet >= GREET_REFRESH_MS) {
    drawGreeting();
    lastGreet = now;
  }

  // ── News: rotate headline every 5 s ─────────────────────────────────────
  if (now - lastNews >= NEWS_ROTATE_MS) {
    newsIdx = (newsIdx + 1) % NEWS_COUNT;
    drawNews();
    lastNews = now;
  }

  // ── Schedule: refresh every 15 s (only when water alert is not showing) ──
  if (!waterAlert && (now - lastSched >= SCHED_REFRESH_MS)) {
    drawSchedule();
    lastSched = now;
  }

  delay(50);
}
