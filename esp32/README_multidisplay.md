# Smart Display Mirror — 4-Display Offline Edition

`multidisplay.ino` drives four SSD1306 OLED displays via a TCA9548A I2C
multiplexer, with no Wi-Fi required.  All content (weather, news, schedule)
is configured directly in the sketch.

---

## Hardware required

| Qty | Part |
|-----|------|
| 1 | ESP32-S3 (any DevKit variant) |
| 1 | WCMCU-9548 / TCA9548A I2C multiplexer |
| 4 | SSD1306 OLED 128×64 (I2C, 3.3 V) |
| — | Dupont wires, breadboard |

---

## Wiring

### Step 1 — XIAO ESP32-S3 → TCA9548A (WCMCU-9548)

```
XIAO ESP32-S3 Pin          TCA9548A Pin
──────────────────────     ────────────
3V3                    →   VCC
GND                    →   GND
D4  (GPIO5 / SDA)      →   SDA
D5  (GPIO6 / SCL)      →   SCL
GND                    →   A0          ← sets I2C address = 0x70
GND                    →   A1
GND                    →   A2
```

> **Do not use 5V** — the XIAO ESP32-S3's 3V3 pin is the correct power rail
> for the TCA9548A and all four OLED displays.

---

### Step 2 — TCA9548A channels → OLED displays

Each OLED connects to one downstream channel of the TCA9548A.

```
TCA9548A Channel    OLED           Content
────────────────    ────────────   ──────────────────────────────
SD0 / SC0       →   Display 0      Greeting + simulated clock
SD1 / SC1       →   Display 1      Weather (temperature, humidity, wind)
SD2 / SC2       →   Display 2      News headlines (rotating)
SD3 / SC3       →   Display 3      Schedule / tasks + water reminder
```

Each OLED also needs power:

```
OLED Pin    Connect to
─────────   ──────────
VCC     →   3.3 V  (from ESP32-S3 or the TCA9548A board's 3.3 V rail)
GND     →   GND
SDA     →   SDx of the corresponding TCA9548A channel
SCL     →   SCx of the corresponding TCA9548A channel
```

---

### Full wiring diagram (ASCII)

```
                    ┌──────────────────────────────┐
                    │       XIAO ESP32-S3           │
                    │                               │
                    │  3V3  ──────────────────┐     │
                    │  GND  ──────────────────┤     │
                    │  D4 / GPIO5 (SDA) ──────┤     │
                    │  D5 / GPIO6 (SCL) ──────┤     │
                    └────────────────────────-┼─────┘
                                              │
                    ┌─────────────────────────┼─────┐
                    │       TCA9548A           │     │
                    │   VCC ◄─────────────────┘     │
                    │   GND ◄─────────────────      │
                    │   SDA ◄─── GPIO8              │
                    │   SCL ◄─── GPIO9              │
                    │   A0/A1/A2 ── GND             │
                    │                               │
                    │  SD0/SC0 ──► OLED 0 (Greeting)│
                    │  SD1/SC1 ──► OLED 1 (Weather) │
                    │  SD2/SC2 ──► OLED 2 (News)    │
                    │  SD3/SC3 ──► OLED 3 (Schedule)│
                    └───────────────────────────────┘
```

Each OLED's VCC and GND connect to the 3.3 V / GND power rail.

---

### Step 3 — Schedule buttons

Two momentary push-buttons for the Schedule display (Display 3).

```
Button          XIAO ESP32-S3 Pin    Other leg
──────────────  ─────────────────    ─────────
Button 1 (NEXT) D0 (GPIO1)       →   GND
Button 2 (DONE) D1 (GPIO2)       →   GND
```

- Uses the internal pull-up resistor — **no external resistor needed**.
- Press **Button 1** to move the cursor `>` to the next task (wraps around).
- Press **Button 2** to toggle the selected task between done `v` and not done.

---

## Libraries (Arduino IDE → Library Manager)

| Library | Author |
|---------|--------|
| **Adafruit GFX Library** | Adafruit |
| **Adafruit SSD1306** | Adafruit |

---

## Configuration (top of `multidisplay.ino`)

| Constant | Default | What it does |
|----------|---------|--------------|
| `USER_NAME` | `"Alex"` | Name shown on the greeting display |
| `START_HOUR` | `7` | Simulated hour at boot (0–23) |
| `START_MINUTE` | `30` | Simulated minute at boot (0–59) |
| `TEMP_C` | `18` | Temperature shown on weather display |
| `WEATHER_COND` | `"Partly Cloudy"` | Condition string |
| `HUMIDITY_PCT` | `65` | Relative humidity % |
| `WIND_KMH` | `12` | Wind speed in km/h |
| `WEATHER_LOC` | `"Your City"` | Location label |
| `NEWS[]` | 8 items | News headlines — add/remove lines freely |
| `SCHED[]` | 7 tasks | Task name + hour + minute |
| `SDA_PIN` / `SCL_PIN` | `8` / `9` | I2C pins — change for your board |

---

## Display layouts

### Display 0 — Greeting
```
┌────────────────────────┐
│ Good Morning           │
│                        │
│ Alex!                  │
│────────────────────────│
│       07:42            │
└────────────────────────┘
```

### Display 1 — Weather
```
┌────────────────────────┐
│ WEATHER                │
│────────────────────────│
│ 18 °C    Your City     │
│                        │
│                        │
│────────────────────────│
│ Partly Cloudy          │
│ H:65%  W:12km/h        │
└────────────────────────┘
```

### Display 2 — News
```
┌────────────────────────┐
│ NEWS              3/8  │
│────────────────────────│
│ Space telescope snaps  │
│ distant galaxy         │
│                        │
│                        │
│      ○ ○ ● ○ ○ ○ ○ ○  │
└────────────────────────┘
```

### Display 3 — Schedule (active task highlighted)
```
┌────────────────────────┐
│ SCHEDULE               │
│────────────────────────│
│ 07:30 Exercise         │
│████09:00 Study ████████│  ← current (inverted)
│ 12:00 Lunch            │
│ 14:00 Study            │
└────────────────────────┘
```

### Display 3 — Water reminder (hourly override)
```
┌────────────────────────┐
│     - REMINDER -       │
│────────────────────────│
│ DRINK                  │
│ WATER!                 │
│                        │
│   Stay hydrated :)     │
└────────────────────────┘
```

---

## Timing behaviour

| Event | Interval |
|-------|---------|
| Greeting clock tick | every 10 s |
| News headline rotation | every 5 s |
| Schedule refresh | every 15 s |
| Water reminder shown | every 60 min |
| Water reminder dismisses itself | after 6 s |

All timers use `millis()` — no blocking delays in the main loop.

---

## Troubleshooting

**All / some displays stay blank**
- Check VCC (must be 3.3 V, *not* 5 V for most SSD1306 modules).
- Confirm SDA/SCL wires are not swapped between the TCA9548A channel headers and the OLED.
- Open Serial Monitor at 115200 baud — the sketch reports each display's init status.

**I2C address conflict**
- Default TCA9548A address is `0x70` (A0=A1=A2=GND).  If you have another device at `0x70`, tie one of the A pins HIGH to shift the address.

**Wrong I2C pins**
- The sketch is already set for XIAO ESP32-S3 (`SDA_PIN=5`, `SCL_PIN=6` → D4/D5).
  If you switch to a different ESP32-S3 DevKit, change those constants accordingly.

**`display.begin()` returns false**
- The OLED may be a 128×32 variant.  Change `SCREEN_H` to `32` and recheck I2C address (some use `0x3D`).
