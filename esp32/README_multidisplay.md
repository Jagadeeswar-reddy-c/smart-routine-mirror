# Smart Display Mirror — 4-Display Wi-Fi + Fingerprint Edition

`multidisplay.ino` drives four SSD1306 OLED displays via a TCA9548A I2C
multiplexer, with an Adafruit fingerprint sensor for 3-profile login,
2 weather-mood LEDs, and an HC-SR04 proximity sensor for auto-sleep.
All content is fetched live from the Smart Morning Assistant backend per user.

---

## Hardware required

| Qty | Part |
|-----|------|
| 1 | XIAO ESP32-S3 (Seeed Studio) |
| 1 | WCMCU-9548 / TCA9548A I2C multiplexer |
| 4 | SSD1306 OLED 128×64 (I2C, 3.3 V) |
| 1 | Adafruit Fingerprint Sensor (AS608 / R307, 3.3 V UART) |
| 1 | HC-SR04 ultrasonic distance sensor |
| 2 | LEDs — 1× yellow, 1× blue |
| 2 | 220 Ω resistors (one per LED) |
| 3 | Momentary push-buttons |
| — | Dupont wires, breadboard |

---

## XIAO ESP32-S3 — complete pin allocation

All 11 accessible header pins (D0–D10) are used.
GPIO17 and GPIO18 do **not** exist on this board — they are not exposed.

| XIAO Pin | GPIO   | Function     | Connected to |
|----------|--------|--------------|--------------|
| D0       | GPIO1  | Digital I/O  | BTN_NEXT (scroll up / prev) |
| D1       | GPIO2  | Digital I/O  | BTN_DONE (scroll down / next) |
| D2       | GPIO3  | Digital I/O  | BTN_SEL (confirm / cycle display) |
| D3       | GPIO4  | Digital I/O  | LED Yellow (220 Ω → anode) |
| D4       | GPIO5  | SDA (I2C)    | TCA9548A SDA |
| D5       | GPIO6  | SCL (I2C)    | TCA9548A SCL |
| D6       | GPIO43 | UART1 TX     | Fingerprint sensor RX |
| D7       | GPIO44 | UART1 RX     | Fingerprint sensor TX |
| D8       | GPIO7  | Digital I/O  | LED Blue (220 Ω → anode) |
| D9       | GPIO8  | Digital I/O  | HC-SR04 ECHO |
| D10      | GPIO9  | Digital I/O  | HC-SR04 TRIG |
| 3V3      | —      | Power output | TCA9548A VIN, all OLEDs, Fingerprint VCC |
| 5V       | —      | Power output | HC-SR04 VCC |
| GND      | —      | Ground       | All components |

---

## Wiring — section by section

### 1 — XIAO ESP32-S3 → TCA9548A (WCMCU-9548)

The WCMCU-9548 has a **VIN** pin (onboard regulator) — connect to **5V**.
If your board has a **VCC** pin (no regulator) — connect to **3V3** instead.

```
XIAO ESP32-S3           TCA9548A (WCMCU-9548)
──────────────────      ─────────────────────
5V   (or 3V3)      →    VIN  (or VCC)
GND                →    GND
D4   (GPIO5/SDA)   →    SDA
D5   (GPIO6/SCL)   →    SCL
GND                →    A0  ┐
GND                →    A1  ├─ sets I2C address = 0x70
GND                →    A2  ┘
```

---

### 2 — TCA9548A → 4× SSD1306 OLED displays

Each display connects to one downstream I2C channel of the TCA9548A.

```
TCA9548A Channel    OLED    Content shown
────────────────    ──────  ──────────────────────────────
SD0 / SC0       →   OLED 0  Greeting + clock + quotes
SD1 / SC1       →   OLED 1  Weather (live temperature + icon)
SD2 / SC2       →   OLED 2  News headlines (rotating)
SD3 / SC3       →   OLED 3  Schedule / tasks + water reminder
```

Each OLED (×4):

```
OLED Pin    Connect to
────────    ──────────────────────────────
VCC     →   3V3
GND     →   GND
SDA     →   SDx  (TCA9548A matching channel)
SCL     →   SCx  (TCA9548A matching channel)
```

---

### 3 — Buttons (3 total)

One leg to the XIAO pin, other leg to GND. Uses internal pull-up — no resistor needed.

```
Button            XIAO Pin     GPIO    Role
────────────────  ───────────  ──────  ──────────────────────────────────────
BTN_SEL           D2           GPIO3   Confirm selection / cycle display
                                       Hold 3 s in dashboard → switch profile
BTN_NEXT          D0           GPIO1   Scroll up (profile) / prev / up-task
BTN_DONE          D1           GPIO2   Scroll down (profile) / next / mark-done
```

---

### 4 — Adafruit Fingerprint Sensor (AS608 / R307)

> ⚠ **XIAO ESP32-S3 only — ignore any guide that says GPIO17 / GPIO18.**
> Those pins appear on full-size ESP32-S3 Dev Boards but are **not exposed** on the XIAO.
> The XIAO's dedicated UART pins are **D6 (GPIO43)** and **D7 (GPIO44)**.

Uses hardware UART1 (GPIO43 = TX, GPIO44 = RX). Sensor runs at 3.3 V — no level shifting needed.

```
Fingerprint Sensor    Wire colour    XIAO ESP32-S3
──────────────────    ───────────    ─────────────────────────────────
VCC  (3.3 V)          Red        →   3V3
GND                   Black      →   GND
TX   (sensor → ESP32) Yellow     →   D7 / GPIO44  (UART1 RX)
RX   (ESP32 → sensor) White      →   D6 / GPIO43  (UART1 TX)
```

Cross the TX/RX lines — sensor TX → ESP32 RX, sensor RX → ESP32 TX.

```
XIAO ESP32-S3                       Fingerprint Sensor
─────────────                       ──────────────────
3V3  ────────────────────────────► VCC
GND  ────────────────────────────► GND
D6 / GPIO43  (UART1 TX) ─────────► RX
D7 / GPIO44  (UART1 RX) ◄─────────  TX
```

---

### 5 — Weather LEDs (2 total)

Connect each LED anode (+, long leg) through a **220 Ω resistor** to the XIAO pin.
Connect each LED cathode (−, short leg) to GND.

```
XIAO Pin      GPIO    LED Colour    Lights up when…
────────────  ──────  ────────────  ──────────────────────────────────
D3            GPIO4   Yellow        Sunny / Partly cloudy / Windy
D8            GPIO7   Blue          Cloudy / Rainy / Snowy
Both D3+D8    —       Yellow + Blue Storm / Thunder (both on = alert)
```

```
D3 (GPIO4) ──[220Ω]──► (+) Yellow LED (−) ──┐
D8 (GPIO7) ──[220Ω]──► (+) Blue   LED (−) ──┴── GND
```

Both LEDs are off during profile/fingerprint screens and when the mirror sleeps.

---

### 6 — HC-SR04 ultrasonic sensor (auto-sleep)

> ⚠ Standard HC-SR04 runs on **5 V** and outputs 5 V on ECHO.
> Protect the ESP32 with a voltage divider on the ECHO wire:
> `HC-SR04 ECHO → 1kΩ → junction → 2kΩ → GND`  and `junction → D9`.
> Skip this only if you have a 3.3 V-native SR04.

```
HC-SR04 Pin    Connect to
─────────────  ──────────────────────────────────────────
VCC        →   5V  (XIAO 5V pin)
GND        →   GND
TRIG       →   D10  (GPIO9)   — direct connection
ECHO       →   D9   (GPIO8)   — via 1kΩ + 2kΩ voltage divider
```

**Behaviour:**
- Object within **200 cm** → "present" → displays + LEDs stay on, timer resets
- Nothing within 200 cm for **1 minute** → displays + LEDs turn off (sleep)
- Motion or button press → instant wake-up, all displays redraw

---

## Full wiring diagram (ASCII)

```
  ┌──────────────────────────────────────────────────────┐
  │                  XIAO ESP32-S3                        │
  │                                                       │
  │  3V3 ─────────────────────────────────┐              │
  │  5V  ──────────────────────────────┐  │              │
  │  GND ───────────────────────────┐  │  │              │
  │                                 │  │  │              │
  │  D4 (GPIO5/SDA) ────────────────┼──┼──┼──┐          │
  │  D5 (GPIO6/SCL) ────────────────┼──┼──┼──┤          │
  │                                 │  │  │  │          │
  │  D0 (GPIO1) ─── BTN_NEXT ─── GND  │  │  │          │
  │  D1 (GPIO2) ─── BTN_DONE ─── GND  │  │  │          │
  │  D2 (GPIO3) ─── BTN_SEL  ─── GND  │  │  │          │
  │                                    │  │  │          │
  │  D3 (GPIO4) ─[220Ω]─ Yellow LED+ ─┤  │  │          │
  │  D8 (GPIO7) ─[220Ω]─ Blue   LED+ ─┼──┘  │          │
  │                       LED cathodes→GND   │          │
  │                                          │          │
  │  D6 (GPIO43/TX) ────── Fingerprint RX   │          │
  │  D7 (GPIO44/RX) ────── Fingerprint TX   │          │
  │  3V3 ───────────────── Fingerprint VCC ─┤          │
  │                                          │          │
  │  D10 (GPIO9) ─── HC-SR04 TRIG           │          │
  │  D9  (GPIO8) ─── [divider] ── ECHO      │          │
  │  5V  ────────────────── HC-SR04 VCC ────┘          │
  │                                                     │
  └─────────────────────────┬───────────────────────────┘
                            │  SDA + SCL
              ┌─────────────▼────────────────────┐
              │         TCA9548A                  │
              │   VIN ◄── 5V (or 3V3 if VCC)     │
              │   GND ◄── GND                     │
              │   A0 / A1 / A2 ── GND (addr 0x70)│
              │                                   │
              │  SD0/SC0 ──► OLED 0  (Greeting)  │
              │  SD1/SC1 ──► OLED 1  (Weather)   │
              │  SD2/SC2 ──► OLED 2  (News)      │
              │  SD3/SC3 ──► OLED 3  (Schedule)  │
              └───────────────────────────────────┘

Each OLED: VCC→3V3, GND→GND, SDA→SDx, SCL→SCx
```

---

## Libraries (Arduino IDE → Library Manager)

| Library | Author |
|---------|--------|
| **Adafruit GFX Library** | Adafruit |
| **Adafruit SSD1306** | Adafruit |
| **Adafruit Fingerprint Sensor Library** | Adafruit |
| **ArduinoJson** | Benoit Blanchon (v6 or v7) |

---

## Wi-Fi, API & device setup

1. Start the backend (`cd backend && uvicorn main:app --host 0.0.0.0 --port 8000`).
2. Register on the dashboard, go to **Devices** page → register device → copy **Device ID** and **Device Secret**.
3. On the Devices page add up to 3 users and map fingerprint slots 1, 2, 3 to each user account.
4. Edit the six constants at the top of `multidisplay.ino`:

```cpp
const char* WIFI_SSID     = "YourSSID";
const char* WIFI_PASSWORD = "YourPassword";
const char* SERVER_URL    = "http://YOUR_SERVER_IP:8000";
const int   DEVICE_ID     = 1;
const char* DEVICE_SECRET = "paste_device_secret_here";
const char* DEVICE_TOKEN  = "paste_64char_token_here"; // Dashboard → ESP32 Device Key
```

5. Flash. Boot sequence:
   - All 4 displays show "Connecting..."
   - Display 0 switches to **profile selection** (other 3 show "Smart Mirror")
   - Scroll and select a profile → place finger → data loads for that user

---

## Fingerprint enrollment

1. Select the profile → press **BTN_SEL**.
2. Place finger on sensor — if not enrolled, "Press SEL to enroll" appears.
3. Press **BTN_SEL** → "Place finger" → lift → "Place again" → stored in sensor slot.
4. Go to **Dashboard → Devices → Fingerprint Mappings**, link that slot number to the user's account.
5. Come back and scan again to log in.

---

## Button reference

### Profile selection

| Button | Action |
|--------|--------|
| BTN_NEXT (D0) | Scroll **up** |
| BTN_DONE (D1) | Scroll **down** |
| BTN_SEL  (D2) | **Confirm** → fingerprint scan |

### Fingerprint scan / enroll

| Button | Action |
|--------|--------|
| BTN_NEXT (D0) | Cancel → back to profile list |
| BTN_SEL  (D2) | Confirm enrollment (when offered) |

### Dashboard (normal mode)

| Active display | BTN_NEXT (D0) | BTN_DONE (D1) |
|----------------|---------------|---------------|
| Greeting (CH 0) | Previous quote | Next quote |
| Weather  (CH 1) | Toggle °C / °F | Toggle °C / °F |
| News     (CH 2) | Previous headline | Next headline |
| Schedule (CH 3) | Next task | Toggle task done / undone |

**BTN_SEL short press** → cycle active display (inverted header = active).
**BTN_SEL hold 3 s** → return to profile selection (switch user).

---

## Timing / configuration constants

| Constant | Value | Description |
|----------|-------|-------------|
| `DATA_REFRESH_MS` | 60 000 ms (1 min) | API re-fetch interval |
| `SLEEP_MS` | 60 000 ms (1 min) | No-presence time before displays sleep |
| `PRESENCE_CM` | 200 cm | Max distance to count as "someone present" |
| `MOTION_CHECK_MS` | 500 ms | Ultrasonic sample rate |
| `FP_SCAN_TIMEOUT` | 20 000 ms (20 s) | Auto-cancel fingerprint scan after this |
| `LONG_PRESS_MS` | 3 000 ms (3 s) | BTN_SEL hold duration to switch profile |
| `WATER_SHOW_MS` | 6 000 ms (6 s) | How long water reminder stays on screen |
| `NEWS_ROTATE_MS` | 5 000 ms (5 s) | Auto news rotation interval |

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `API error -1` at boot | `SERVER_URL` is still the placeholder — set your real server IP |
| `API error 401` | `DEVICE_TOKEN` or `DEVICE_SECRET` is wrong — re-copy from dashboard |
| Displays stay blank | Check VCC = 3.3 V; open Serial Monitor (115 200 baud) for `ch0 OK` messages |
| Fingerprint sensor not found | Check TX/RX are crossed; sensor needs 3.3 V, not 5 V |
| ECHO always 0 / sleep never triggers | TRIG/ECHO swapped, or missing voltage divider on ECHO for 5 V sensor |
| LEDs never light up | Data not fetched yet (`dataReady = false`); check long leg (anode) faces resistor |
| Wrong I2C — OLEDs blank | Confirm `SDA_PIN=5 (D4)`, `SCL_PIN=6 (D5)` for XIAO ESP32-S3 |
| TCA9548A not found | A0/A1/A2 must all be tied to GND for address 0x70 |
