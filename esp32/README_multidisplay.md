# Smart Display Mirror — 4-Display Wi-Fi Edition

`multidisplay.ino` drives four SSD1306 OLED displays via a TCA9548A I2C
multiplexer, with 3 weather-mood LEDs and an HC-SR04 proximity sensor for
auto-sleep.  All content is fetched live from the Smart Morning Assistant backend.

---

## Hardware required

| Qty | Part |
|-----|------|
| 1 | XIAO ESP32-S3 |
| 1 | WCMCU-9548 / TCA9548A I2C multiplexer |
| 4 | SSD1306 OLED 128×64 (I2C, 3.3 V) |
| 3 | LEDs — 1× yellow, 1× red, 1× blue |
| 3 | 220 Ω resistors (one per LED) |
| 1 | HC-SR04 ultrasonic distance sensor |
| 3 | Momentary push-buttons |
| — | Dupont wires, breadboard |

---

## Complete pin-to-pin wiring

### XIAO ESP32-S3 pin reference

| Arduino label | GPIO | Used for |
|---|---|---|
| D0 | GPIO1  | BTN_NEXT |
| D1 | GPIO2  | BTN_DONE |
| D2 | GPIO3  | BTN_SEL  |
| D3 | GPIO4  | LED Yellow |
| D4 | GPIO5  | SDA (I2C) |
| D5 | GPIO6  | SCL (I2C) |
| D6 | GPIO43 | *(free)*  |
| D7 | GPIO44 | HC-SR04 ECHO |
| D8 | GPIO7  | LED Blue |
| D9 | GPIO8  | LED Red |
| D10| GPIO9  | HC-SR04 TRIG |
| 3V3| —      | 3.3 V power rail |
| 5V | —      | HC-SR04 VCC |
| GND| —      | Ground |

---

### 1 — XIAO ESP32-S3 → TCA9548A (WCMCU-9548)

```
XIAO ESP32-S3         TCA9548A (WCMCU-9548)
──────────────────    ──────────────────────
3V3              →    VCC
GND              →    GND
D4  (GPIO5/SDA)  →    SDA
D5  (GPIO6/SCL)  →    SCL
GND              →    A0   ┐
GND              →    A1   ├── sets I2C address = 0x70
GND              →    A2   ┘
```

> Use **3.3 V** only — do not connect VCC to 5 V.

---

### 2 — TCA9548A → 4× SSD1306 OLED displays

Each OLED connects to one TCA9548A downstream channel.

```
TCA9548A channel    →   OLED          Content
────────────────────    ────────────  ──────────────────────
SD0 / SC0           →   Display 0     Greeting + clock
SD1 / SC1           →   Display 1     Weather (live)
SD2 / SC2           →   Display 2     News headlines
SD3 / SC3           →   Display 3     Schedule + water reminder
```

Each OLED needs 4 wires:

```
OLED pin    →   Connect to
─────────       ──────────────────────────────────────
VCC         →   3.3 V rail
GND         →   GND
SDA         →   SDx  (matching TCA9548A channel)
SCL         →   SCx  (matching TCA9548A channel)
```

---

### 3 — Buttons (3 total, press to GND)

```
Button              XIAO pin       Other leg
──────────────────  ─────────────  ─────────
BTN_SEL  (select)   D2 (GPIO3)  →  GND
BTN_NEXT (left)     D0 (GPIO1)  →  GND
BTN_DONE (right)    D1 (GPIO2)  →  GND
```

Internal pull-up is used — no external resistor needed.

---

### 4 — Weather LEDs (3 total)

Wire each LED with a **220 Ω resistor** in series between the XIAO pin and the LED anode (+).  LED cathode (−) → GND.

```
LED colour    XIAO pin       Lights up when…
────────────  ─────────────  ─────────────────────────────
Yellow        D3 (GPIO4)     Sunny / Partly cloudy / Windy
Blue          D8 (GPIO7)     Cloudy / Rainy / Snowy
Red           D9 (GPIO8)     Storm / Thunder (severe)
```

```
XIAO D3 ──[220Ω]──► Yellow LED (+)  │
XIAO D8 ──[220Ω]──► Blue   LED (+)  │  cathodes → GND
XIAO D9 ──[220Ω]──► Red    LED (+)  │
```

All three LEDs turn off automatically when the mirror sleeps.

---

### 5 — HC-SR04 ultrasonic sensor (auto-sleep)

> ⚠ The HC-SR04 runs on **5 V** and its ECHO pin outputs 5 V logic.
> To protect the ESP32 (3.3 V GPIO), add a voltage divider on the ECHO wire:
> `ECHO_OUT ──[1kΩ]── ECHO_IN ──[2kΩ]── GND`  (middle tap → ESP32 D7).
> Skip this if you have a 3.3 V-compatible SR04 variant.

```
HC-SR04 pin    →   Connect to
───────────────    ──────────────────────────────────────────────
VCC            →   5V  (XIAO 5V pin)
GND            →   GND
TRIG           →   D10 (GPIO9)
ECHO           →   D7  (GPIO44)  via 1kΩ+2kΩ voltage divider (see above)
```

**Behaviour:**
- Something within **200 cm** → presence detected → displays & LEDs stay on
- Nothing within 200 cm for **5 minutes** → displays & LEDs turn off (sleep)
- Motion returns → displays & LEDs wake up instantly
- Any button press also resets the sleep timer

---

## Full ASCII wiring overview

```
                      ┌───────────────────────────┐
                      │      XIAO ESP32-S3         │
                      │                            │
           3V3 ───────┤─────────────────────┐      │
           GND ───────┤──────────────────┐  │      │
       D4/GPIO5 ──────┤── SDA ──────┐   │  │      │
       D5/GPIO6 ──────┤── SCL ──────┤   │  │      │
                      │             │   │  │      │
       D0/GPIO1 ───── BTN_NEXT      │   │  │      │
       D1/GPIO2 ───── BTN_DONE      │   │  │      │
       D2/GPIO3 ───── BTN_SEL       │   │  │      │
                      │             │   │  │      │
       D3/GPIO4 ──[220Ω]──► Yellow LED  │  │      │
       D8/GPIO7 ──[220Ω]──► Blue LED    │  │      │
       D9/GPIO8 ──[220Ω]──► Red LED     │  │      │
                      │             │   │  │      │
      D10/GPIO9 ────── TRIG (HC-SR04)   │  │      │
      D7/GPIO44 ────── ECHO (HC-SR04, via divider) │
           5V  ─────── VCC  (HC-SR04)   │  │      │
                      └────────────────-┼──┼──────┘
                                        │  │
               ┌────────────────────────┼──┼──────┐
               │       TCA9548A         │  │      │
               │   SDA ◄────────────────┘  │      │
               │   SCL ◄───────────────────┘      │
               │   VCC ◄──── 3V3                  │
               │   GND ◄──── GND                  │
               │   A0/A1/A2 ──── GND              │
               │                                  │
               │  SD0/SC0 ──► OLED 0 (Greeting)   │
               │  SD1/SC1 ──► OLED 1 (Weather)    │
               │  SD2/SC2 ──► OLED 2 (News)       │
               │  SD3/SC3 ──► OLED 3 (Schedule)   │
               └──────────────────────────────────┘

Each OLED: VCC→3V3, GND→GND, SDA→SDx, SCL→SCx
```

---

## Libraries (Arduino IDE → Library Manager)

| Library | Author |
|---------|--------|
| **Adafruit GFX Library** | Adafruit |
| **Adafruit SSD1306** | Adafruit |
| **ArduinoJson** | Benoit Blanchon (v6 or v7) |

---

## Wi-Fi & API setup

1. Start the backend — see main `CLAUDE.md`.
2. Register on the dashboard, go to **ESP32 Device Key** card, copy the 64-char token.
3. Edit the four constants at the top of `multidisplay.ino`:

```cpp
const char* WIFI_SSID     = "YourSSID";
const char* WIFI_PASSWORD = "YourPassword";
const char* SERVER_URL    = "http://YOUR_SERVER_IP:8000";
const char* DEVICE_TOKEN  = "paste_your_64char_token_here";
```

4. Flash. Displays show **"Connecting..."** then switch to live data once WiFi connects.

---

## Configuration constants

| Constant | Default | What it does |
|----------|---------|--------------|
| `PRESENCE_CM` | `200` | Distance (cm) below which someone is considered present |
| `SLEEP_MS` | `300000` (5 min) | Time without presence before displays sleep |
| `DATA_REFRESH_MS` | `300000` (5 min) | How often to re-fetch from the API |
| `WATER_SHOW_MS` | `6000` (6 s) | How long the water reminder stays on screen |
| `MOTION_CHECK_MS` | `500` | Ultrasonic sample interval (ms) |

---

## Button behaviour

| Active display | BTN_NEXT (D0) | BTN_DONE (D1) |
|---|---|---|
| Greeting (CH 0) | Previous quote | Next quote |
| Weather  (CH 1) | Toggle °C / °F | Toggle °C / °F |
| News     (CH 2) | Previous headline | Next headline |
| Schedule (CH 3) | Move cursor to next task | Toggle task done/undone |

**BTN_SEL (D2)** cycles which display is active (inverted header = active display).
Any button press also resets the 5-minute sleep timer.

---

## Auto-sleep behaviour

```
HC-SR04 measures distance every 500 ms
    │
    ├── distance < 200 cm  →  reset timer, wake displays if sleeping
    │
    └── distance ≥ 200 cm for 5 min  →  displays off, LEDs off

On wake: all 4 displays redraw immediately with current data.
```

---

## Troubleshooting

**API error -1 at boot**
- `SERVER_URL` is still the placeholder — update it to your server's real IP.

**Displays stay blank**
- Check VCC = 3.3 V (not 5 V).
- Open Serial Monitor at 115200 baud — each display reports `ch0 OK` etc.

**HC-SR04 ECHO always 0 / no sleep**
- Check TRIG/ECHO are not swapped.
- If using a 5 V sensor, add the 1 kΩ + 2 kΩ voltage divider on ECHO.

**LEDs never light up**
- Confirm `dataReady` is true (data has been fetched at least once).
- Check resistor polarity and LED direction (long leg = anode = to resistor).

**Wrong I2C pins**
- Sketch targets XIAO ESP32-S3 (`SDA_PIN=5 D4`, `SCL_PIN=6 D5`).  Change for other boards.
