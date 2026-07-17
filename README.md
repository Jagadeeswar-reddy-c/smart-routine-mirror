# Smart Morning Assistant

A university Interactive Systems prototype that delivers a personalised morning briefing — weather, schedule, and news — through a web dashboard and a XIAO ESP32-S3 smart mirror device, with per-user accounts and fingerprint-based identity on shared hardware.

---

## Project Idea

The device sits beside a mirror or on a bedside table. Each morning a family member selects their profile and places their finger on the sensor; the ESP32 identifies them, calls the backend, and displays **their** weather, schedule, news, and personal quotes — all on four shared OLED screens. A web dashboard lets each user manage their own data and register devices.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      WEB DASHBOARD                          │
│  register → login → dashboard.html  ←→  devices.html        │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTP + JWT Bearer token
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    FastAPI Backend                          │
│  auth_routes  ·  user data endpoints  ·  device_routes     │
│  SQLite (users, schedule, devices, fingerprints …)          │
│       │                                        │            │
│  Weather API                              News API          │
│  (OpenWeatherMap)                         (NewsAPI)         │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTP POST + device_secret
                           ▼
┌─────────────────────────────────────────────────────────────┐
│               XIAO ESP32-S3 Smart Mirror                    │
│                                                             │
│  ┌──────────────┐  UART1    ┌────────────────────────┐     │
│  │ Fingerprint  │◄─────────►│    XIAO ESP32-S3        │     │
│  │ (AS608/R307) │ D6/D9     │  Wi-Fi · JSON · OLED   │     │
│  └──────────────┘ GPIO43/8  └──┬────────────┬────────┘     │
│                                │ I2C D4/D5  │ UART2 D3/D8  │
│                         ┌──────▼──────┐  ┌──▼───────────┐  │
│                         │  TCA9548A   │  │ ESP8266 Mini │  │
│                         │ I2C mux     │  │ LEDs·Buttons │  │
│                         └──────┬──────┘  │ Buzzer       │  │
│           ┌──────┬──────┬──────┴─┐       └──────────────┘  │
│        OLED 0  OLED 1  OLED 2  OLED 3                       │
│       Greeting Weather  News  Schedule                       │
└─────────────────────────────────────────────────────────────┘
```

**Data flow at runtime:**

```
Profile selected on-screen (BTN_NEXT / BTN_DONE / BTN_SEL)
    → Finger placed on sensor
    → ESP32 matches fingerprint locally (slot ID 1–3)
    → POST /api/devices/{id}/login  { fingerprint_id, device_secret }
    → Backend maps slot → user_id → fetches weather + schedule + news + quotes
    → Returns personalised JSON
    → ESP32 displays briefing across 4 OLED screens
```

---

## Project Structure

```
smart-routine-mirror/
├── backend/
│   ├── main.py              # FastAPI app — auth, user data, quotes, settings
│   ├── device_routes.py     # Device management + IoT fingerprint login
│   ├── database.py          # SQLAlchemy engine + session factory
│   ├── models.py            # ORM models (users, devices, fingerprints, quotes …)
│   ├── schemas.py           # Pydantic request/response schemas
│   ├── auth.py              # JWT, password hashing, dual-auth dependency
│   ├── requirements.txt
│   └── .env.example         # API keys + secret key template
├── frontend/
│   ├── login.html           # Login page (entry point)
│   ├── register.html        # Registration page
│   ├── dashboard.html       # Weather, schedule, news, quotes, device key
│   ├── devices.html         # Device management — members + fingerprint slots
│   ├── auth.js              # Login / register logic
│   ├── dashboard.js         # Dashboard data + quotes + display settings
│   ├── devices.js           # Device registration, members, fingerprint mappings
│   └── style.css            # Shared dark-theme styles
├── esp32/
│   ├── multidisplay.ino         # Main sketch — 4 OLEDs + fingerprint + Wi-Fi + ESP8266
│   ├── esp8266_io_slave.ino     # ESP8266 UART peripheral — LEDs, buzzer, display buttons
│   ├── README_multidisplay.md   # Full pin-to-pin wiring for all hardware
│   ├── fingerprint_enroll.ino   # Standalone enrollment utility (optional)
│   └── Testing/
│       └── esp32s3_wifi_testing.ino  # Wi-Fi connectivity probe
└── README.md
```

---

## Hardware

### XIAO ESP32-S3 (Seeed Studio)

The **XIAO ESP32-S3** is a thumb-sized dual-core 240 MHz microcontroller with built-in Wi-Fi (802.11 b/g/n). It is the brain of the smart mirror.

> ⚠ **Pin note:** The XIAO ESP32-S3 exposes **D0–D10** (GPIO1–9, GPIO43, GPIO44).
> GPIO17 and GPIO18 are **not available** on this board.
> Fingerprint UART uses **D6 (GPIO43)** and **D7 (GPIO44)**.

```
XIAO ESP32-S3 — accessible pins
──────────────────────────────────────────────────────────────
D0  GPIO1   D1  GPIO2   D2  GPIO3   D3  GPIO4   D4  GPIO5 (SDA)
D5  GPIO6 (SCL)         D6  GPIO43 (TX) D7  GPIO44 (RX)
D8  GPIO7   D9  GPIO8   D10 GPIO9
```

### Full hardware list

| Qty | Part | Purpose |
|-----|------|---------|
| 1 | XIAO ESP32-S3 (Seeed Studio) | Main MCU + Wi-Fi |
| 1 | ESP8266 D1 Mini | I/O expander — LEDs, buzzer, display-select buttons |
| 1 | WCMCU-9548 / TCA9548A | I2C multiplexer for 4 OLEDs |
| 4 | SSD1306 OLED 128×64 (I2C, 3.3 V) | 4 display panels |
| 1 | AS608 / R307 fingerprint sensor (3.3 V UART) | Per-user biometric ID |
| 1 | HC-SR04 ultrasonic distance sensor | Auto-sleep / presence detection |
| 3 | LEDs — 1× red, 1× yellow, 1× blue | Weather mood indicator |
| 3 | 220 Ω resistors | LED current limiting |
| 7 | Momentary push-buttons | 3 on ESP32-S3 (profile nav) + 4 on ESP8266 (display select) |
| — | Dupont wires, breadboard | Connections |

---

## Pin Allocation

### XIAO ESP32-S3

| XIAO | GPIO | Connected to |
|------|------|--------------|
| D0 | GPIO1 | BTN_NEXT — scroll up / prev |
| D1 | GPIO2 | BTN_DONE — scroll down / next |
| D2 | GPIO3 | BTN_SEL — confirm (hold 3 s = switch profile) |
| D3 | GPIO4 | UART2 TX → ESP8266 D1 (GPIO5) |
| D4 | GPIO5 | SDA — TCA9548A → 4× OLEDs |
| D5 | GPIO6 | SCL — TCA9548A → 4× OLEDs |
| D6 | GPIO43 | Fingerprint TX (ESP32 → sensor RX) |
| D7 | GPIO44 | HC-SR04 ECHO (direct, no divider) |
| D8 | GPIO7 | UART2 RX ← ESP8266 D2 (GPIO4) |
| D9 | GPIO8 | Fingerprint RX (sensor TX → ESP32) |
| D10 | GPIO9 | HC-SR04 TRIG |
| 3V3 | — | TCA9548A, OLEDs, fingerprint sensor |
| 5V | — | HC-SR04 VCC |
| GND | — | All components |

### ESP8266 D1 Mini

| D1 Mini | GPIO | Connected to |
|---------|------|--------------|
| D1 | GPIO5 | UART RX ← ESP32-S3 D3 (GPIO4) |
| D2 | GPIO4 | UART TX → ESP32-S3 D8 (GPIO7) |
| D0 | GPIO16 | LED RED — 220 Ω → GND (storm) |
| D3 | GPIO0 | LED YELLOW — 220 Ω → GND (sun/wind/storm) |
| D4 | GPIO2 | LED BLUE — 220 Ω → GND (rain/cloud/snow/storm) |
| D5 | GPIO14 | BTN_DISP0 → GND (jump to Greeting display) |
| D6 | GPIO12 | BTN_DISP1 → GND (jump to Weather display) |
| D7 | GPIO13 | BTN_DISP2 → GND (jump to News display) |
| RX | GPIO3 | BTN_DISP3 → GND (jump to Schedule display) |
| D8 | GPIO15 | — (free; boot-strapping pin) |
| 5V | — | USB power |
| GND | — | Common GND |

> Full pin-to-pin wiring with ASCII diagrams: see [esp32/README_multidisplay.md](esp32/README_multidisplay.md).

---

## Backend Setup

### Prerequisites
- Python 3.10+
- (Optional) Free API keys — the backend serves **mock data** when keys are absent.
  - Weather: [openweathermap.org/api](https://openweathermap.org/api)
  - News: [newsapi.org](https://newsapi.org)

### Install & run

```bash
cd backend
source venv/bin/activate        # or: pip install -r requirements.txt
# Windows: venv\Scripts\activate

# Optional: add API keys and a custom JWT secret
cp .env.example .env
# Edit .env

# Run from inside backend/ (modules use relative imports)
uvicorn main:app --reload --host 0.0.0.0 --port 80
```

- Dashboard: `http://localhost`
- Interactive API docs: `http://localhost/docs`
- SQLite database (`smart_assistant.db`) is created automatically on first run.

### Reset the database

```bash
rm backend/smart_assistant.db
# Restart the server — tables are re-created, all users must re-register
```

---

## Frontend

Served directly by the FastAPI backend — no separate server needed.
Open `http://localhost` → redirects to `/login.html`.

### User flow

1. `/register.html` — create an account
2. `/login.html` — sign in → redirected to dashboard
3. `/dashboard.html` — weather, schedule, news, personal quotes, water reminder, ESP32 device key
4. `/devices.html` — register ESP32 devices, add family members, map fingerprint slots

---

## Database Schema

| Table | Purpose |
|-------|---------|
| `users` | Username, email, bcrypt password hash |
| `user_settings` | One row per user — weather location, water interval, news category |
| `schedule_items` | Schedule entries with `user_id` + `date` |
| `user_quotes` | Personal quotes shown on the greeting display |
| `device_tokens` | Permanent per-user ESP32 API key (`X-Device-Token`) |
| `devices` | Registered physical devices — `device_code`, `device_secret` |
| `device_users` | Many-to-many: device ↔ user, with role (`owner` / `member`) |
| `device_fingerprints` | Maps sensor slot ID (1–127) → `user_id` per device |

### JWT authentication flow

```
POST /api/auth/register  →  user row created
POST /api/auth/login     →  JWT token returned (7-day expiry)
Frontend stores token in localStorage
Every API request: Authorization: Bearer <token>
Backend decodes token → user_id → filters all queries
```

---

## Multi-User Device Setup

One XIAO ESP32-S3 serves an entire family. Each person's fingerprint is stored locally on the sensor (up to 127 slots). The backend maps each slot to a user account.

### One-time setup sequence

1. **Register the device** — go to `/devices.html` → "Register a new device" → copy **Device ID** and **Device Secret**.
2. **Add family members** — invite by email. They must each have an account.
3. **Map fingerprint slots** — on the Devices page, add slot → user mappings (slots 1, 2, 3 for up to 3 profiles).
4. **Flash `multidisplay.ino`** — fill in SSID, password, SERVER_URL, DEVICE_ID, DEVICE_SECRET.
5. **Enroll fingerprints on-device** — boot the mirror, select a profile, place an unenrolled finger → the sketch walks you through a 2-scan enrollment automatically.

### Role-based access

| Role | Permissions |
|------|-------------|
| `owner` | Add/remove members, manage fingerprint mappings, see device secret |
| `member` | View their own fingerprint mappings |

---

## API Reference

All user endpoints require `Authorization: Bearer <token>`.
The IoT login endpoint uses `device_secret` in the POST body (no JWT).

### Auth

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/auth/register` | Create account `{"username","email","password"}` |
| `POST` | `/api/auth/login` | Login → JWT token |
| `GET`  | `/api/auth/me` | Current user profile |

### User data (protected)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET`  | `/api/dashboard-data` | Combined weather + schedule + news + quotes |
| `GET`  | `/api/weather` | Weather for the user's saved location |
| `POST` | `/api/location` | Update location `{"location": "Berlin"}` |
| `GET`  | `/api/schedule` | Today's schedule |
| `POST` | `/api/schedule` | Add item `{"time": "09:00", "task": "Meeting"}` |
| `DELETE` | `/api/schedule/{id}` | Delete a schedule item |
| `GET`  | `/api/news` | Latest 5 headlines for the user's chosen category |
| `POST` | `/api/news/category` | Set preferred category `{"category": "technology"}` |
| `GET`  | `/api/quotes` | List personal quotes |
| `POST` | `/api/quotes` | Add a quote `{"text": "...", "sort_order": 0}` |
| `PUT`  | `/api/quotes/{id}` | Update a quote |
| `DELETE` | `/api/quotes/{id}` | Delete a quote |
| `GET`  | `/api/device/token` | Get the permanent single-user device token |
| `POST` | `/api/device/token` | Generate / regenerate device token |

### Device management (protected)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/devices/register` | Register a new device |
| `GET`  | `/api/devices` | List all devices you belong to |
| `GET`  | `/api/devices/{id}` | Device detail + secret |
| `GET`  | `/api/devices/{id}/users` | List members |
| `POST` | `/api/devices/{id}/users` | Add a member by email *(owner)* |
| `DELETE` | `/api/devices/{id}/users/{uid}` | Remove a member *(owner)* |
| `GET`  | `/api/devices/{id}/fingerprints` | List fingerprint slot mappings |
| `POST` | `/api/devices/{id}/fingerprints` | Add a slot → user mapping *(owner)* |
| `DELETE` | `/api/devices/{id}/fingerprints/{fpid}` | Remove a mapping |

### IoT device login (no JWT — used by ESP32)

```
POST /api/devices/{device_id}/login
Body: { "fingerprint_id": 1, "device_secret": "abc123..." }
```

**Success response:**
```json
{
  "success": true,
  "user":     { "id": 2, "username": "Alice" },
  "weather":  { "location": "Berlin", "temperature": 18, "condition": "Cloudy",
                "humidity": 65, "wind_speed": 12 },
  "schedule": { "date": "2026-06-27",
                "items": [{ "id": 5, "time": "09:00", "task": "Stand-up" }] },
  "news":     { "headlines": ["Headline one", "Headline two", "Headline three"],
               "category": "technology" },
  "quotes":   ["Quote one", "Quote two"],
  "settings": { "water_interval_min": 60 },
  "timestamp": "2026-06-27T07:30:00"
}
```

**Fingerprint not mapped:**
```json
{ "success": false, "message": "Fingerprint slot 1 not registered on this device." }
```

---

## News Categories

Each user independently chooses their preferred news category. The setting is saved to their profile and applied on every `/api/news` and `/api/dashboard-data` call.

### Available categories

| Category value | What you get |
|----------------|-------------|
| `general` | Top headlines across all topics (default) |
| `technology` | Tech, gadgets, AI, software |
| `business` | Stocks, finance, markets, economy |
| `sports` | Football, basketball, F1, Olympics, etc. |
| `health` | Medicine, fitness, nutrition, mental health |
| `science` | Research, space, environment |
| `entertainment` | Film, music, celebrities, TV |
| `weather` | Weather-related articles (keyword search) |

### How to change category

**Web dashboard:** In the News card, click any category pill — it saves instantly and reloads headlines.

**API directly:**
```bash
curl -X POST http://YOUR_SERVER/api/news/category \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"category": "technology"}'
```

### How it works internally

- Categories `general` → `entertainment` call `GET /v2/top-headlines?category=…&language=en`
- Category `weather` calls `GET /v2/everything?q=weather&language=en&sortBy=publishedAt`
- Falls back to 5 mock headlines if `NEWS_API_KEY` is absent or the API call fails
- The response always includes a `"category"` field so the dashboard can highlight the active pill

---

## ESP32 Sketch — `multidisplay.ino`

### Arduino IDE setup

**1. Install Arduino IDE 2** from [arduino.cc/en/software](https://www.arduino.cc/en/software).

**2. Add ESP32 board support**
`File → Preferences → Additional Board Manager URLs`, add:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
`Tools → Board → Boards Manager` → search `esp32` → install **esp32 by Espressif Systems**.

**3. Select board**
`Tools → Board → esp32 → XIAO_ESP32S3`

**4. Install required libraries** (`Tools → Library Manager`):
- **Adafruit Fingerprint Sensor Library** (Adafruit)
- **Adafruit SSD1306** (Adafruit)
- **Adafruit GFX Library** (Adafruit)
- **ArduinoJson** (Benoit Blanchon — v6 or v7)

**5. For the ESP8266 sketch** — install ESP8266 board support first:
`File → Preferences → Additional Board Manager URLs`, add:
```
http://arduino.esp8266.com/stable/package_esp8266com_index.json
```
Then `Tools → Board → Boards Manager` → search `esp8266` → install **esp8266 by ESP8266 Community**.
Select board: `LOLIN(WEMOS) D1 Mini`. The `SoftwareSerial` library is built-in — no extra install needed.

**5. Connect and flash**
Plug in via USB-C. `Tools → Port` → select the port that appears.
If no port appears: hold **BOOT**, tap **RESET**, release BOOT (enters download mode).

### Sketch configuration

Edit the constants at the top of `esp32/multidisplay.ino`:

```cpp
const char* WIFI_SSID     = "YourSSID";        // 2.4 GHz only
const char* WIFI_PASSWORD = "YourPassword";
const char* SERVER_URL    = "http://YOUR_SERVER_IP";
```

And the per-profile tokens and fingerprint slots:

```cpp
const char* PROFILE_TOKEN[3] = { "token1", "token2", "token3" };
const uint8_t FP_SLOT[3]     = { 1, 2, 3 };
```

### Flashing order

Flash the **ESP8266 first**, then the **ESP32-S3**.

On ESP8266 power-up you should see the self-test: yellow → blue → red LEDs cycle. This confirms the sketch is running correctly.

### Fingerprint sensor — XIAO ESP32-S3 wiring

> ⚠ Generic guides often show **GPIO17 / GPIO18** for fingerprint UART.
> Those pins do **not exist** on the XIAO ESP32-S3.
> Use **D6 (GPIO43)** for TX and **D9 (GPIO8)** for RX.

```
XIAO ESP32-S3                       Fingerprint Sensor
─────────────                       ──────────────────
3V3  ────────────────────────────► VCC
GND  ────────────────────────────► GND
D6 / GPIO43  (UART1 TX) ─────────► RX
D9 / GPIO8   (UART1 RX) ◄─────────  TX
```

Cross the TX/RX — sensor TX → ESP32 RX, sensor RX → ESP32 TX.

### On-device fingerprint enrollment

No need to flash a separate sketch. The mirror handles enrollment automatically:

1. Select a profile with BTN_NEXT / BTN_DONE → confirm with BTN_SEL.
2. Place finger → if not enrolled, "Press SEL to enroll" appears.
3. Press BTN_SEL → "Place finger" → lift → "Place again" → stored in sensor slot.
4. Go to **Dashboard → Devices → Fingerprint Mappings**, link the slot number to the user.
5. Place finger again → logged in.

### Sketch boot flow

```
Power on
    → All 4 displays: "Connecting..."
    → Wi-Fi connects
    → Display 0: Profile selection  (other 3: "Smart Mirror")
    → Scroll BTN_NEXT / BTN_DONE,  confirm BTN_SEL
    → Display 0: "Place finger..."
    → Match found  →  POST /api/devices/{id}/login  →  dashboard loads
    → No match     →  enrollment wizard
```

### Dashboard controls

| Screen | BTN_NEXT (D0) | BTN_DONE (D1) | BTN_SEL (D2) |
|--------|---------------|---------------|---------------|
| Profile select | Scroll up | Scroll down | Confirm profile |
| FP scan / enroll | Cancel | — | Confirm enroll |
| Greeting (CH 0) | Prev quote | Next quote | Cycle active display |
| Weather  (CH 1) | Toggle °C/°F | Toggle °C/°F | Cycle active display |
| News     (CH 2) | Prev headline | Next headline | Cycle active display |
| Schedule (CH 3) | Next task | Toggle done | Cycle active display |

**Hold BTN_SEL for 3 s** in dashboard → returns to profile selection (switch user).

---

## How UART Works (for reference)

UART is a simple two-wire serial protocol. Both sides agree on speed in advance (57600 baud).

```
XIAO D6 / GPIO43 (UART1 TX) ──────────────────────► Sensor RX
                                    57600 baud
XIAO D7 / GPIO44 (UART1 RX) ◄──────────────────────  Sensor TX
```

The Adafruit Fingerprint Library handles the low-level protocol:

```
Library call               Action
────────────────────────── ──────────────────────────────────────
finger.getImage()      →   Capture image from sensor
finger.image2Tz(1)     →   Extract feature template (slot 1)
finger.fingerFastSearch()→  Compare against all stored templates
finger.storeModel(n)   →   Save matched template to slot n
```

---

## Common Problems & Fixes

| Symptom | Cause | Fix |
|---------|-------|-----|
| Fingerprint sensor not detected | TX/RX swapped or wrong pins | sensor TX→D9(GPIO8), sensor RX→D6(GPIO43) — crossed |
| `API error -1` at boot | `SERVER_URL` is a placeholder | Set your server's actual IP/domain |
| `API error 401` | Wrong device secret or token | Re-copy from Dashboard → Devices page |
| No Wi-Fi | Wrong SSID/password or 5 GHz network | ESP32 supports **2.4 GHz only** |
| OLEDs blank | Wrong I2C pins | SDA=D4(GPIO5), SCL=D5(GPIO6), VCC=3.3 V |
| TCA9548A not found | A0/A1/A2 not tied to GND | All three address pins → GND (address 0x70) |
| LEDs / buttons not working | UART wires wrong or no common GND | D3→ESP8266 D1, D8←ESP8266 D2, GND shared |
| LEDs all on at boot | Crash loop on ESP8266 | Reflash ESP8266; check D3/D4 wiring (GPIO0/GPIO2) |
| No sleep / ECHO always 0 | TRIG/ECHO swapped | TRIG=D10(GPIO9), ECHO=D7(GPIO44) |
| Port not in Arduino IDE | USB driver or charge-only cable | Install CP2102/CH340 driver; try a data cable |
| Upload fails / timeout | Board not in download mode | Hold BOOT, tap RESET, release BOOT before Upload |
| Low fingerprint confidence (<30) | Dirty sensor or dry finger | Clean glass; breathe lightly on fingertip first |

---

## Full Demo Walkthrough

```bash
# Start the backend
cd backend && source venv/bin/activate
uvicorn main:app --host 0.0.0.0 --port 80
```

1. Open `http://YOUR_SERVER_IP` → register an account → sign in.
2. Set your weather location on the dashboard.
3. Add schedule items and personal quotes.
4. In the **News** card, click a category pill (e.g. Technology or Business) — live headlines load immediately.
5. Go to **Devices** → register a device → copy Device ID and Device Secret.
6. Add family members by email (they each need an account; each picks their own news category).
7. Wire up the hardware per [esp32/README_multidisplay.md](esp32/README_multidisplay.md).
8. Fill in the 5 constants in `multidisplay.ino` → flash.
9. On the mirror: select your profile → enroll finger when prompted.
10. Back on the Devices page: link the slot number to your user account.
11. Place finger again → personalised briefing (weather, schedule, your news category, quotes) appears across all 4 screens.

---

## Future Extensions

- Google Calendar / CalDAV integration for automatic schedule sync
- Per-user news country filter (currently English only)
- E-ink display output for always-on mirror panel
- Smart mirror enclosure with a two-way mirror panel
- OTA (Over-the-Air) firmware updates for the ESP32
- Mobile push notifications
- Cloud deployment (Railway, Render, Fly.io)
- Voice response via I2S speaker
