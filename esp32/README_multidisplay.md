# Smart Display Mirror — 4-Display Wi-Fi + Fingerprint Edition

`multidisplay.ino` drives four SSD1306 OLED displays via a TCA9548A I2C
multiplexer, with fingerprint login for 3 user profiles, weather-mood LEDs,
a buzzer, and an HC-SR04 proximity sensor for auto-sleep.
An ESP8266 D1 Mini acts as a UART I/O expander for extra buttons and LEDs.
All content is fetched live from the Smart Morning Assistant backend per user.

---

## Hardware required

| Qty | Part |
|-----|------|
| 1 | XIAO ESP32-S3 (Seeed Studio) |
| 1 | ESP8266 D1 Mini |
| 1 | WCMCU-9548 / TCA9548A I2C multiplexer |
| 4 | SSD1306 OLED 128×64 (I2C, 3.3 V) |
| 1 | AS608 / R307 fingerprint sensor (3.3 V UART) |
| 1 | HC-SR04 ultrasonic distance sensor |
| 3 | LEDs — 1× red, 1× yellow, 1× blue |
| 3 | 220 Ω resistors (one per LED) |
| 7 | Momentary push-buttons |
| — | Dupont wires, breadboard |

---

## XIAO ESP32-S3 — pin allocation

| XIAO | GPIO | Function | Connected to |
|------|------|----------|--------------|
| D0 | GPIO1 | BTN_NEXT | button → GND |
| D1 | GPIO2 | BTN_DONE | button → GND |
| D2 | GPIO3 | BTN_SEL | button → GND |
| D3 | GPIO4 | UART2 TX | ESP8266 D1 (GPIO5) |
| D4 | GPIO5 | SDA (I2C) | TCA9548A SDA |
| D5 | GPIO6 | SCL (I2C) | TCA9548A SCL |
| D6 | GPIO43 | UART1 TX | Fingerprint sensor RX |
| D7 | GPIO44 | ECHO | HC-SR04 ECHO (direct) |
| D8 | GPIO7 | UART2 RX | ESP8266 D2 (GPIO4) |
| D9 | GPIO8 | UART1 RX | Fingerprint sensor TX |
| D10 | GPIO9 | TRIG | HC-SR04 TRIG |
| 3V3 | — | Power | TCA9548A, OLEDs, fingerprint sensor |
| 5V | — | Power | HC-SR04 VCC |
| GND | — | Ground | All components |

---

## ESP8266 D1 Mini — pin allocation

| D1 Mini | GPIO | Function | Connected to |
|---------|------|----------|--------------|
| D1 | GPIO5 | UART RX | ESP32-S3 D3 (GPIO4) — TX |
| D2 | GPIO4 | UART TX | ESP32-S3 D8 (GPIO7) — RX |
| D0 | GPIO16 | LED RED | 220 Ω → LED → GND |
| D3 | GPIO0 | LED YELLOW | 220 Ω → LED → GND |
| D4 | GPIO2 | LED BLUE | 220 Ω → LED → GND |
| D5 | GPIO14 | BTN_DISP0 | button → GND |
| D6 | GPIO12 | BTN_DISP1 | button → GND |
| D7 | GPIO13 | BTN_DISP2 | button → GND |
| RX | GPIO3 | BTN_DISP3 | button → GND |
| D8 | GPIO15 | — | free (boot-strapping pin — keep LOW at power-on) |
| 5V | — | Power | USB (power the board) |
| GND | — | Ground | Common GND |

> **GPIO0 (D3) and GPIO2 (D4)** are boot-sensitive — the sketch sets them LOW before `pinMode(OUTPUT)` to prevent the boot glitch.
>
> **GPIO15 (D8)** must be LOW at boot to boot from flash — leave it unconnected or tied to GND. Do not use it for buttons.
>
> **GPIO1 (TX)** is avoided for outputs — the ESP8266 bootloader sends UART data on it before `setup()` runs, which would drive connected hardware unexpectedly.

---

## UART link — ESP32-S3 ↔ ESP8266

The ESP8266 runs as a UART peripheral (9600 baud). It is **not** on the I2C bus.

```
ESP32-S3 D3 (GPIO4)  TX ──────────────► RX  D1 (GPIO5)  ESP8266
ESP32-S3 D8 (GPIO7)  RX ◄──────────────  TX  D2 (GPIO4)  ESP8266
GND ─────────────────────────────────────────────────────── GND
```

**Protocol — 1 byte each direction:**

| Direction | Content |
|-----------|---------|
| ESP32-S3 → ESP8266 | Output bitmask: bit0=LED RED, bit1=LED YELLOW, bit2=LED BLUE |
| ESP8266 → ESP32-S3 | Button bitmask (every 50 ms): bit0=DISP0, bit1=DISP1, bit2=DISP2, bit3=DISP3 |

---

## I2C bus — ESP32-S3 and TCA9548A only

The ESP8266 is **not** connected to the I2C bus.

```
ESP32-S3 D4 (GPIO5) SDA ──────────────► SDA  TCA9548A
ESP32-S3 D5 (GPIO6) SCL ──────────────► SCL  TCA9548A
```

---

## Wiring — section by section

### 1 — XIAO ESP32-S3 → TCA9548A

```
XIAO ESP32-S3           TCA9548A
──────────────          ────────────────────
3V3              →  VCC  (or VIN if board has regulator)
GND              →  GND
D4 (GPIO5/SDA)   →  SDA
D5 (GPIO6/SCL)   →  SCL
GND              →  A0 ┐
GND              →  A1 ├─ I2C address = 0x70
GND              →  A2 ┘
```

### 2 — TCA9548A → 4× SSD1306 OLED

```
TCA9548A ch    OLED    Content
─────────────  ──────  ──────────────────────
SD0 / SC0  →  OLED 0  Greeting + clock + quotes
SD1 / SC1  →  OLED 1  Weather
SD2 / SC2  →  OLED 2  News headlines
SD3 / SC3  →  OLED 3  Schedule + tasks
```

Each OLED: VCC → 3V3 · GND → GND · SDA → SDx · SCL → SCx

### 3 — Fingerprint sensor (AS608 / R307)

Cross TX/RX — sensor TX → ESP32 RX, sensor RX → ESP32 TX.

```
Fingerprint              XIAO ESP32-S3
───────────              ─────────────────────
VCC (3.3 V)     →   3V3
GND             →   GND
TX (sensor out) →   D9 / GPIO8  (UART1 RX)
RX (sensor in)  →   D6 / GPIO43 (UART1 TX)
```

> GPIO17/GPIO18 do **not exist** on the XIAO ESP32-S3. Never use them.

### 4 — HC-SR04 ultrasonic sensor

```
HC-SR04          XIAO ESP32-S3
────────         ─────────────────────
VCC         →   5V
GND         →   GND
TRIG        →   D10 / GPIO9
ECHO        →   D7  / GPIO44  (direct — no voltage divider needed)
```

The XIAO ESP32-S3 GPIO44 is 5V-tolerant on the ECHO signal. No divider required.

### 5 — Buttons on ESP32-S3 (3 total)

One leg to pin, other leg to GND. Uses internal pull-up.

```
BTN_SEL   →  D2 (GPIO3)  — confirm / hold 3 s to switch profile
BTN_NEXT  →  D0 (GPIO1)  — scroll up / prev
BTN_DONE  →  D1 (GPIO2)  — scroll down / next
```

### 6 — Buttons on ESP8266 (4 total)

All four use the internal pull-up — wire one leg to pin, other leg to GND. No external resistors needed.

```
BTN_DISP0  →  D5 (GPIO14)  — jump to display 0 (Greeting)
BTN_DISP1  →  D6 (GPIO12)  — jump to display 1 (Weather)
BTN_DISP2  →  D7 (GPIO13)  — jump to display 2 (News)
BTN_DISP3  →  RX (GPIO3)   — jump to display 3 (Schedule)
```

### 7 — Weather LEDs on ESP8266

```
ESP8266 D0 (GPIO16) ──[220Ω]──► (+) RED LED    (−) → GND   storm/thunder
ESP8266 D3 (GPIO0)  ──[220Ω]──► (+) YELLOW LED (−) → GND   sunny/wind/storm
ESP8266 D4 (GPIO2)  ──[220Ω]──► (+) BLUE LED   (−) → GND   rain/cloud/snow/storm
```

LED logic (controlled by ESP32-S3 via UART byte):

| Weather | Red | Yellow | Blue |
|---------|-----|--------|------|
| Sunny / clear | — | ON | — |
| Windy | — | ON | — |
| Cloudy | — | — | ON |
| Rainy / snowy | — | — | ON |
| Storm / thunder | ON | ON | ON |
| No data / sleep | — | — | — |

---

## Full wiring diagram (ASCII)

```
┌─────────────────────────────────────────────────────────────────┐
│                        XIAO ESP32-S3                            │
│                                                                 │
│  D0 (GPIO1)  ─── BTN_NEXT ──► GND                              │
│  D1 (GPIO2)  ─── BTN_DONE ──► GND                              │
│  D2 (GPIO3)  ─── BTN_SEL  ──► GND                              │
│                                                                 │
│  D3 (GPIO4)  ─── UART2 TX ──────────────────────► D1 ESP8266   │
│  D8 (GPIO7)  ─── UART2 RX ◄─────────────────────  D2 ESP8266   │
│                                                                 │
│  D4 (GPIO5/SDA) ─────────────────────────────────► TCA SDA     │
│  D5 (GPIO6/SCL) ─────────────────────────────────► TCA SCL     │
│                                                                 │
│  D6 (GPIO43/TX) ─────────────────────────────────► FP RX       │
│  D9 (GPIO8 /RX) ◄─────────────────────────────────  FP TX      │
│  3V3 ────────────────────────────────────────────► FP VCC      │
│                                                                 │
│  D10 (GPIO9)  ──── HC-SR04 TRIG                                 │
│  D7  (GPIO44) ──── HC-SR04 ECHO  (direct, no divider)           │
│  5V  ──────────── HC-SR04 VCC                                   │
│                                                                 │
└───────────────────────────┬─────────────────────────────────────┘
                            │ SDA + SCL
            ┌───────────────▼──────────────────┐
            │           TCA9548A                │
            │  VCC/VIN ◄── 3V3                 │
            │  A0/A1/A2 ── GND  (addr 0x70)    │
            │  SD0/SC0 ──► OLED 0 (Greeting)   │
            │  SD1/SC1 ──► OLED 1 (Weather)    │
            │  SD2/SC2 ──► OLED 2 (News)       │
            │  SD3/SC3 ──► OLED 3 (Schedule)   │
            └──────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                      ESP8266 D1 Mini                            │
│                                                                 │
│  D1 (GPIO5)  ── UART RX ◄── ESP32-S3 D3 (GPIO4)                │
│  D2 (GPIO4)  ── UART TX ──► ESP32-S3 D8 (GPIO7)                │
│                                                                 │
│  D0 (GPIO16) ──[220Ω]──► RED LED    (−)► GND                   │
│  D3 (GPIO0)  ──[220Ω]──► YELLOW LED (−)► GND                   │
│  D4 (GPIO2)  ──[220Ω]──► BLUE LED   (−)► GND                   │
│                                                                 │
│  D5 (GPIO14) ─── BTN_DISP0 ──► GND                             │
│  D6 (GPIO12) ─── BTN_DISP1 ──► GND                             │
│  D7 (GPIO13) ─── BTN_DISP2 ──► GND                             │
│  RX (GPIO3)  ─── BTN_DISP3 ──► GND                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Libraries (Arduino IDE → Library Manager)

**For ESP32-S3 (`multidisplay.ino`):**

| Library | Author |
|---------|--------|
| Adafruit GFX Library | Adafruit |
| Adafruit SSD1306 | Adafruit |
| Adafruit Fingerprint Sensor Library | Adafruit |
| ArduinoJson | Benoit Blanchon (v6 or v7) |

**For ESP8266 (`esp8266_io_slave.ino`):**

| Library | Author |
|---------|--------|
| SoftwareSerial | Arduino (built-in) |

---

## Flashing order

1. **Flash ESP8266 first** (`esp8266_io_slave.ino`).
   - Board: LOLIN(WEMOS) D1 Mini
   - Self-test on boot: yellow → blue → red LEDs cycle
2. **Then flash ESP32-S3** (`multidisplay.ino`).
   - Board: XIAO_ESP32S3
   - Serial Monitor (115200 baud) should show `IO expander UART ready`

---

## Wi-Fi and device setup

1. Start the backend: `cd backend && uvicorn main:app --host 0.0.0.0 --port 80`
2. Register on the web dashboard → go to **Devices** → register a device → copy **Device ID** and **Device Secret**.
3. Add up to 3 users and map fingerprint slots 1–3 to each account.
4. Edit the constants at the top of `multidisplay.ino`:

```cpp
const char* WIFI_SSID     = "YourSSID";        // 2.4 GHz only
const char* WIFI_PASSWORD = "YourPassword";
const char* SERVER_URL    = "http://YOUR_SERVER_IP";
```

And fill in the per-profile tokens and fingerprint slots:

```cpp
const char* PROFILE_TOKEN[3] = { "token1", "token2", "token3" };
const uint8_t FP_SLOT[3]     = { 1, 2, 3 };
```

---

## Fingerprint enrollment

1. Select a profile with BTN_NEXT / BTN_DONE → confirm with BTN_SEL.
2. Place finger → if not enrolled, "Press SEL to enroll" appears.
3. BTN_SEL → "Place finger" → lift → "Place again" → stored in slot.
4. Dashboard → Devices → Fingerprint Mappings → link slot → user.
5. Scan again to log in.

---

## Button reference

### Profile selection
| Button | Action |
|--------|--------|
| BTN_NEXT (D0) | Scroll up |
| BTN_DONE (D1) | Scroll down |
| BTN_SEL  (D2) | Confirm → fingerprint scan |

### Dashboard — ESP32-S3 buttons
| Button | Action |
|--------|--------|
| BTN_SEL short press | Cycle active display (inverted = active) |
| BTN_SEL hold 3 s | Return to profile selection |
| BTN_NEXT / BTN_DONE | Depend on active display (see below) |

| Active display | BTN_NEXT | BTN_DONE |
|----------------|----------|----------|
| Greeting (0) | Prev quote | Next quote |
| Weather (1) | Toggle °C/°F | Toggle °C/°F |
| News (2) | Prev headline | Next headline |
| Schedule (3) | Next task | Toggle done |

### Dashboard — ESP8266 buttons
| Button | Action |
|--------|--------|
| BTN_DISP0 (D5) | Jump to display 0 — Greeting (inverted) |
| BTN_DISP1 (D6) | Jump to display 1 — Weather (inverted) |
| BTN_DISP2 (D7) | Jump to display 2 — News (inverted) |
| BTN_DISP3 (RX) | Jump to display 3 — Schedule (inverted) |

---

## Timing constants

| Constant | Default | Description |
|----------|---------|-------------|
| `DATA_REFRESH_MS` | 60 000 ms | API re-fetch interval |
| `PRESENCE_CM` | 1 cm | HC-SR04 distance constant (sleep disabled) |
| `MOTION_CHECK_MS` | 5000 ms | Ultrasonic sample rate |
| `FP_SCAN_TIMEOUT` | 20 000 ms | Auto-cancel fingerprint scan |
| `LONG_PRESS_MS` | 3 000 ms | BTN_SEL hold to switch profile |
| `NEWS_ROTATE_MS` | 5 000 ms | Auto news rotation interval |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `IO expander UART ready` but no LEDs / buttons | Check D3→D1 and D8→D2 UART wires; check common GND between boards |
| Buzzer on continuously at boot | Normal briefly — ESP8266 GPIO16 has pull-down; clears once `setup()` runs |
| Buzzer never beeps | Module is active-LOW — wired correctly; check VCC and GND on buzzer module |
| LEDs all on at boot | Crash loop — usually old I2C slave sketch still on ESP8266; reflash `esp8266_io_slave.ino` |
| Fingerprint sensor not found | Check TX/RX crossed: sensor TX→D9(GPIO8), sensor RX→D6(GPIO43) |
| `FP sensor OK` but goes to dashboard without scanning | `fpReady=true` but `FP_SLOT` not enrolled; enroll first |
| `API error -1` | Wrong `SERVER_URL`; server not running; or firewall blocking port 80 |
| `API error 401` | Wrong device token — re-copy from Dashboard |
| OLEDs blank | VCC=3.3V; SDA=D4(GPIO5), SCL=D5(GPIO6); TCA9548A A0/A1/A2 all to GND |
| Display sleep never triggers | `PRESENCE_CM` threshold too small; check HC-SR04 TRIG=D10, ECHO=D7 |
| No Wi-Fi | 2.4 GHz only; check SSID/password |
| Upload fails | Hold BOOT, tap RESET, release BOOT before clicking Upload |
