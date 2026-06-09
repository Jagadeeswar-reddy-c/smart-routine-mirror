# Smart Morning Assistant

A university Interactive Systems prototype that delivers a personalised morning briefing — weather, schedule, and news — through a web dashboard and an ESP32-S3 smart mirror device, with per-user accounts and fingerprint-based identity on shared hardware.

---

## Project Idea

The device sits beside a mirror or on a bedside table. Each morning a family member places their finger on the sensor; the ESP32 identifies them, calls the backend, and displays **their** weather, schedule, and news — all on one shared piece of hardware. A web dashboard lets each user manage their own data and register devices.

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
│  auth_routes   ·   user data endpoints   ·  device_routes  │
│  SQLite (users, schedule, devices, fingerprints …)          │
│       │                                        │            │
│  Weather API                              News API          │
│  (OpenWeatherMap)                         (NewsAPI)         │
└──────────────────────────┬──────────────────────────────────┘
                           │ HTTP POST + device_secret
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3 Device                          │
│                                                             │
│  ┌──────────────┐   UART    ┌─────────────────────────┐    │
│  │ Fingerprint  │◄─────────►│     ESP32-S3 MCU         │    │
│  │ Sensor       │           │  Wi-Fi · JSON parser     │    │
│  │ (R307/R503)  │           │  ArduinoJson             │    │
│  └──────────────┘           └────────────┬────────────┘    │
│                                          │ I2C/SPI         │
│                                   ┌──────▼──────┐          │
│                                   │ OLED / TFT  │(optional)│
│                                   │   Display   │          │
│                                   └─────────────┘          │
└─────────────────────────────────────────────────────────────┘
```

**Data flow at runtime:**

```
Finger on sensor
    → ESP32 matches fingerprint locally (slot ID 1–127)
    → POST /api/devices/{id}/login  { fingerprint_id, device_secret }
    → Backend maps slot → user_id → fetches weather + schedule + news
    → Returns personalised JSON
    → ESP32 displays briefing
```

---

## Project Structure

```
smart-routine-mirror/
├── backend/
│   ├── main.py              # FastAPI app — all user/auth endpoints
│   ├── device_routes.py     # Device management + IoT login endpoints
│   ├── database.py          # SQLAlchemy engine + session factory
│   ├── models.py            # ORM models (users, devices, fingerprints …)
│   ├── schemas.py           # Pydantic request/response schemas
│   ├── auth.py              # JWT, password hashing, dual-auth dependency
│   ├── requirements.txt
│   └── .env.example         # API keys + secret key template
├── frontend/
│   ├── login.html           # Login page (entry point)
│   ├── register.html        # Registration page
│   ├── dashboard.html       # Authenticated dashboard
│   ├── devices.html         # Device management page
│   ├── auth.js              # Login / register logic
│   ├── dashboard.js         # Dashboard data + device token management
│   ├── devices.js           # Device registration, members, fingerprint mappings
│   └── style.css            # Shared dark-theme styles
├── esp32/
│   ├── esp32_smart_assistant.ino   # Main sketch (flash after enrolling)
│   └── fingerprint_enroll.ino      # Enrollment utility (flash first)
└── README.md
```

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
python -m venv venv
source venv/bin/activate        # Windows: venv\Scripts\activate
pip install -r requirements.txt

# Optional: configure API keys and JWT secret
cp .env.example .env
# Edit .env with your values

cd backend && uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

The backend is available at `http://localhost:8000`.  
Interactive API docs: `http://localhost:8000/docs`  
The SQLite database (`smart_assistant.db`) is created automatically on first run.

---

## Frontend

The frontend is served directly by the FastAPI backend from the `frontend/` folder.  
Open `http://localhost:8000` — it redirects to `/login.html` automatically.

### User flow

1. **`/register.html`** — create an account
2. **`/login.html`** — sign in → redirected to dashboard
3. **`/dashboard.html`** — your weather, schedule, news, and ESP32 device token
4. **`/devices.html`** — register ESP32 devices, add family members, map fingerprints

---

## User Login and SQL Database

Each user has their own weather location, schedule, and news. Data is isolated by `user_id` on every query.

### Database tables

| Table | Purpose |
|-------|---------|
| `users` | Username, email, bcrypt password hash |
| `user_settings` | One row per user — weather location |
| `schedule_items` | Schedule entries with `user_id` + `date` |
| `device_tokens` | Permanent per-user ESP32 API key (`X-Device-Token`) |
| `devices` | Registered physical devices — `device_code`, `device_name`, `device_secret` |
| `device_users` | Many-to-many: device ↔ user, with role (`owner` / `member`) |
| `device_fingerprints` | Maps sensor slot ID → `user_id` per device |

### JWT authentication flow

```
POST /api/auth/register  →  user row created
POST /api/auth/login     →  JWT token returned (7-day expiry)
Frontend stores token in localStorage
Every API request: Authorization: Bearer <token>
Backend decodes token → user_id → filters all queries
```

### Reset the database

```bash
rm backend/smart_assistant.db
# Restart the server — tables are re-created automatically
```

---

## Multi-User Device Management

One physical ESP32 can serve an entire family.  Each person's fingerprint is stored locally on the sensor (up to 127 slots). The backend maps each slot to a user account.

### Setup sequence (one-time per device)

1. **Enroll fingerprints** — flash `fingerprint_enroll.ino`, follow Serial Monitor prompts for each person. Note each slot ID.
2. **Register the device** — go to `/devices.html` → "Register a new device". Copy the generated **Device ID** and **Device Secret**.
3. **Add family members** — still on devices page, invite by email. They must have an account.
4. **Map fingerprints** — for each enrolled slot, select the user and click "Add Mapping".
5. **Flash the main sketch** — paste `DEVICE_ID` and `DEVICE_SECRET` into `esp32_smart_assistant.ino` and flash.

### Role-based access

| Role | Can do |
|------|--------|
| `owner` | Add/remove members, manage fingerprint mappings, see device secret |
| `member` | View their own fingerprint mappings |

---

## API Reference

All user endpoints require `Authorization: Bearer <token>`.  
Device IoT endpoint uses `device_secret` in the POST body instead (no JWT).

### Auth

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/auth/register` | Create account `{"username","email","password"}` |
| `POST` | `/api/auth/login` | Login → JWT token |
| `GET`  | `/api/auth/me` | Current user profile |

### User data (protected)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET`  | `/api/dashboard-data` | Combined weather + schedule + news |
| `GET`  | `/api/weather` | Weather for the user's saved location |
| `POST` | `/api/location` | Update location `{"location": "Berlin"}` |
| `GET`  | `/api/schedule` | Today's schedule |
| `POST` | `/api/schedule` | Add item `{"time": "09:00", "task": "Meeting"}` |
| `DELETE` | `/api/schedule/{id}` | Delete a schedule item |
| `GET`  | `/api/news` | Latest 5 headlines |
| `GET`  | `/api/device/token` | Get the permanent device token |
| `POST` | `/api/device/token` | Generate / regenerate device token |

### Device management (protected, owner-only where noted)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/api/devices/register` | Register a new device |
| `GET`  | `/api/devices` | List all devices you belong to |
| `GET`  | `/api/devices/{id}` | Device detail + secret |
| `GET`  | `/api/devices/{id}/users` | List members |
| `POST` | `/api/devices/{id}/users` | Add a member by email *(owner)* |
| `DELETE` | `/api/devices/{id}/users/{uid}` | Remove a member *(owner)* |
| `GET`  | `/api/devices/{id}/fingerprints` | List fingerprint mappings |
| `POST` | `/api/devices/{id}/fingerprints` | Add a mapping *(owner)* |
| `DELETE` | `/api/devices/{id}/fingerprints/{fpid}` | Remove a mapping |

### IoT device login (no JWT — used by ESP32)

```
POST /api/devices/{device_id}/login
Body: { "fingerprint_id": 3, "device_secret": "abc123..." }
```

**Response (success):**
```json
{
  "success": true,
  "user": { "id": 2, "username": "Alice" },
  "weather":  { "location": "Berlin", "temperature": 18, "condition": "Cloudy",
                "humidity": 65, "wind_speed": 12 },
  "schedule": { "date": "2026-05-24",
                "items": [{ "id": 5, "time": "09:00", "task": "Stand-up" }] },
  "news":     { "headlines": ["Headline one", "Headline two", "Headline three"] },
  "timestamp": "2026-05-24T07:30:00"
}
```

**Response (fingerprint not mapped):**
```json
{ "success": false, "message": "Fingerprint slot 3 not registered on this device." }
```

---

## ESP32-S3 Hardware Guide

### What is the ESP32-S3?

The **ESP32-S3** is a dual-core 240 MHz microcontroller made by Espressif with built-in Wi-Fi (802.11 b/g/n) and Bluetooth. It is the "brain" of the smart mirror device.

A **dev board** (development board) breaks all the chip's tiny pins out to rows of larger pins so you can prototype with jumper wires. Common boards: ESP32-S3-DevKitC-1, Unexpected Maker FeatherS3, LILYGO T-Display-S3.

```
┌─────────────────────────────────────────────────────┐
│                  ESP32-S3 Dev Board                 │
│                                                     │
│  USB-C ──► 3.3 V regulator ──► MCU chip            │
│                                                     │
│  Left pin row:          Right pin row:              │
│  GND  ●                           ● 3V3             │
│  GPIO ●  ← Digital I/O            ● GPIO            │
│  GPIO ●  ← UART TX/RX             ● GPIO            │
│  GPIO ●  ← I2C SDA/SCL            ● GPIO            │
│  GPIO ●  ← SPI MOSI/MISO/CLK      ● GPIO            │
│  …                                                  │
└─────────────────────────────────────────────────────┘
```

**Pin types you'll use:**
| Pin label | What it does |
|-----------|-------------|
| `3V3` | 3.3 V power output — powers the sensor |
| `GND` | Ground reference — must be shared with every component |
| `GPIO17` | UART2 TX — ESP32 sends data to sensor |
| `GPIO18` | UART2 RX — ESP32 receives data from sensor |
| `GPIO8` | I2C SCL — clock for OLED display (optional) |
| `GPIO9` | I2C SDA — data for OLED display (optional) |

> All ESP32-S3 GPIO pins operate at **3.3 V logic**. Do not connect 5 V signals directly.

---

### Fingerprint Sensor

The sketch is compatible with the **R307**, **R503**, and **AS608** optical fingerprint sensors (all use the same UART protocol and work with the Adafruit Fingerprint Sensor Library).

**How it works internally:**

```
Finger placed on glass window
       ↓
Optical image captured (LED illuminates fingertip)
       ↓
Sensor CPU extracts feature points (minutiae)
       ↓
Creates a mathematical template
       ↓
Compares template against all stored templates
       ↓
Returns: matched slot ID + confidence score (0–100)
         OR "not found"
```

Templates are stored **inside the sensor chip** (flash memory, up to 127 slots). They never leave the sensor as raw images — only the slot ID is sent to the ESP32 and then to the backend.

**Sensor connector pins (4 wires):**

| Sensor pin | Wire colour (typical) | Connect to |
|------------|-----------------------|------------|
| VCC / VIN  | Red | ESP32 `3V3` |
| GND        | Black | ESP32 `GND` |
| TX (sensor transmits) | Yellow/Green | ESP32 `GPIO 18` (UART2 RX) |
| RX (sensor receives)  | White/Blue   | ESP32 `GPIO 17` (UART2 TX) |

> **Cross the TX/RX lines.** The sensor's TX goes to the ESP32's RX, and vice versa. This is standard serial wiring.

---

### Wiring Diagram

```
ESP32-S3 Dev Board                Fingerprint Sensor
─────────────────                 ─────────────────
3V3  ───────────────────────────► VCC
GND  ───────────────────────────► GND
GPIO 17 (UART2 TX) ─────────────► RX
GPIO 18 (UART2 RX) ◄─────────────  TX
```

Full breadboard layout:

```
[ESP32-S3]              [Breadboard]           [Sensor]
 3V3 ──────────────── (+) rail ─────────────── VCC
 GND ──────────────── (-) rail ─────────────── GND
 GPIO17 ─────────────────────────────────────── RX
 GPIO18 ─────────────────────────────────────── TX
```

---

### Optional OLED Display (SSD1306)

A 0.96″ or 1.3″ OLED replaces the Serial Monitor with a physical screen on the mirror.

**Wiring (I2C, 4 wires):**

| OLED pin | Connect to |
|----------|------------|
| VCC | ESP32 `3V3` |
| GND | ESP32 `GND` |
| SCL | ESP32 `GPIO 8` |
| SDA | ESP32 `GPIO 9` |

Required library: **Adafruit SSD1306** + **Adafruit GFX** (both in Library Manager).

---

### Optional TFT Display (ILI9341 / ST7789)

A 2.4″–2.8″ colour TFT gives more space for the briefing.

**Wiring (SPI, 7 wires):**

| TFT pin | Connect to |
|---------|------------|
| VCC | ESP32 `3V3` |
| GND | ESP32 `GND` |
| CS  | GPIO 10 |
| RST | GPIO 11 |
| DC  | GPIO 12 |
| MOSI | GPIO 13 |
| CLK | GPIO 14 |

Required library: **Adafruit ILI9341** or **TFT_eSPI** (Library Manager).

---

### Parts List

| Component | Model / notes | Approx. price |
|-----------|--------------|---------------|
| ESP32-S3 dev board | ESP32-S3-DevKitC-1 (38-pin) or similar | €8–15 |
| Fingerprint sensor | R307, R503, or AS608 (optical, UART, 4-pin JST) | €8–12 |
| OLED display *(optional)* | SSD1306, 0.96″ I2C, 128×64 px | €3–5 |
| TFT display *(optional)* | ILI9341, 2.4″ SPI, 240×320 px | €6–10 |
| Breadboard | 400-tie or 830-tie | €2–4 |
| Jumper wires | Male-to-male + male-to-female set | €2–3 |
| USB-C cable | Data-capable (not charge-only) | €2–4 |
| **Total (minimum)** | ESP32 + sensor + wires | **~€20–35** |

Where to buy: AliExpress (cheapest, 2–4 week shipping), Amazon, Mouser, LCSC, Berrybase (Germany).

---

### Arduino IDE Setup (Step by Step)

**1. Install Arduino IDE 2**  
Download from [arduino.cc/en/software](https://www.arduino.cc/en/software).

**2. Add ESP32 board support**  
`File → Preferences → Additional Board Manager URLs`, add:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then: `Tools → Board → Boards Manager` → search `esp32` → install **esp32 by Espressif Systems** (2.x or 3.x).

**3. Install required libraries**  
`Tools → Library Manager`, install all three:
- **Adafruit Fingerprint Sensor Library** (by Adafruit)
- **ArduinoJson** (by Benoit Blanchon — v6 or v7 both work)
- *(optional)* **Adafruit SSD1306** + **Adafruit GFX** for OLED

**4. Select your board**  
`Tools → Board → esp32 → ESP32S3 Dev Module`

Configure:
- USB CDC On Boot: **Enabled**
- Upload Speed: **921600**
- Flash Mode: **QIO 80MHz**

**5. Connect and find the port**  
Plug in via USB-C. `Tools → Port` → select the port that appears (e.g. `/dev/ttyUSB0` or `COM3`).

If no port appears:
- Press and hold **BOOT**, tap **RESET**, release BOOT (enters download mode).
- Install CP2102 or CH340 USB driver if the port still doesn't show.

---

### Fingerprint Enrollment (one-time per user)

> You only need to do this once. After enrollment, flash the main sketch and never touch `fingerprint_enroll.ino` again unless you want to add a new user.

**Step 1 — Flash the enrollment sketch**

Open `esp32/fingerprint_enroll.ino` in Arduino IDE and upload it.

**Step 2 — Open Serial Monitor**

`Tools → Serial Monitor`, set baud rate to **115200**.

You should see:
```
════════════════════════════════════════════
  Fingerprint Enrollment — Smart Morning Assistant
════════════════════════════════════════════

Sensor ready.  Enrolled templates: 0 / 127

Type a slot ID (1–127) and press Enter to enroll a new finger:
```

**Step 3 — Enroll each finger**

For each person:
1. Type a slot number (e.g. `1`) and press **Enter**.
2. Place the finger firmly on the sensor when asked → first scan.
3. Lift the finger when asked.
4. Place the **same finger** again → second scan.
5. The sketch creates a template and stores it. You see:

```
════════════════════════════════════════════
  ✓ Fingerprint stored in slot 1
════════════════════════════════════════════
  Next step:
  1. Go to the web dashboard → Devices page.
  2. Select your device.
  3. Under 'Fingerprint Mappings', add:
       Slot ID: 1  →  select the user's account.
════════════════════════════════════════════
```

6. Repeat for each family member using different slot numbers.

**Slot ID tips:**
- Use any number 1–127; they don't need to be consecutive.
- Write down which slot belongs to which person before closing the Serial Monitor.
- If you type a slot that's already used, the sketch asks you to confirm overwrite with `y`.

**Step 4 — Map slots in the dashboard**

Go to `/devices.html`, select your device, and under **Fingerprint Mappings** add each slot → user pairing.

**Step 5 — Flash the main sketch**

Open `esp32/esp32_smart_assistant.ino`, fill in the four constants, and upload.

---

### Main Sketch Configuration

Open `esp32/esp32_smart_assistant.ino` and edit the constants at the top before flashing:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// LAN IP of the machine running uvicorn
// Find it with: ip addr (Linux) or ipconfig (Windows)
const char* SERVER_URL    = "http://192.168.X.X:8000";

// From the web dashboard → Devices page → select your device
const int   DEVICE_ID     = 1;
const char* DEVICE_SECRET = "PASTE_DEVICE_SECRET_HERE";
```

Flash, open Serial Monitor at 115200 baud, and place a finger:

```
╔══════════════════════════════════════════════════╗
║  Good morning,   Alice!                          ║
╚══════════════════════════════════════════════════╝

[WEATHER]
  Berlin — 18°C, Cloudy
  Humidity 65%   Wind 12 km/h

[TODAY'S SCHEDULE]
  09:00  Stand-up
  14:00  Dentist

[NEWS]
  1. Headline one
  2. Headline two
  3. Headline three
```

---

### How the UART Communication Works

UART (Universal Asynchronous Receiver/Transmitter) is a simple two-wire serial protocol — one wire sends, one wire receives, both sides agree on the speed in advance (57600 baud here).

```
ESP32 GPIO17 (TX) ──────────────────────► Sensor RX
                         57600 baud
ESP32 GPIO18 (RX) ◄────────────────────── Sensor TX
```

The Adafruit Fingerprint Library handles the low-level protocol:

```
Library call               Bytes sent over UART             Sensor response
────────────────────────────────────────────────────────────────────────────
finger.getImage()      →   [header][command:0x01][…]   →   OK / NOFINGER
finger.image2Tz(1)     →   [header][command:0x02][…]   →   OK / IMAGEMESS
finger.fingerSearch()  →   [header][command:0x04][…]   →   slot_id + confidence
finger.storeModel(n)   →   [header][command:0x06][n]   →   OK / BADLOCATION
```

The `HardwareSerial sensorSerial(2)` line tells the ESP32 to use its second UART hardware block (UART2), which maps to GPIO17 and GPIO18 by default.

---

### Common Problems & Fixes

| Symptom | Most likely cause | Fix |
|---------|-------------------|-----|
| `ERROR: Fingerprint sensor not detected` | Wiring mistake | Check TX↔RX are **crossed** — sensor TX → ESP32 RX, not TX→TX |
| Port does not appear in Arduino IDE | USB driver missing or charge-only cable | Install CP2102/CH340 driver; try a different USB cable |
| Upload fails / times out | ESP32 not in download mode | Hold BOOT, tap RESET, release BOOT before clicking Upload |
| First scan succeeds, second fails | Finger placed at different angle | Use a relaxed, flat press — same orientation both scans |
| `HTTP error 401` in Serial Monitor | Wrong device secret | Re-copy `DEVICE_SECRET` from the dashboard |
| `HTTP error 404` in Serial Monitor | Wrong device ID | Check `DEVICE_ID` matches the integer shown on the dashboard |
| `No Wi-Fi` in Serial Monitor | Wrong SSID/password, or 5 GHz network | ESP32 only supports 2.4 GHz; double-check credentials |
| Finger recognised but wrong user | Slot mapped to wrong account | Check the Fingerprint Mappings table on the devices page |
| `Fingerprints did not match` during enrollment | Two different fingers, or too much movement | Use the **same finger**, flat and still, both scans |
| Sensor works but very low confidence (<30) | Dirty sensor glass or dry finger | Clean glass with a soft cloth; breathe lightly on fingertip |

---

### Full Demo Walkthrough

```bash
# Terminal 1 — backend
cd backend && uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

1. Open `http://localhost:8000` → create an account and sign in.
2. Set your weather location on the dashboard.
3. Add schedule items.
4. Go to **Devices** → register a device → copy **Device ID** and **Device Secret**.
5. Add family members by email; they must each register an account first.
6. Flash `fingerprint_enroll.ino`, enroll each finger, note slot IDs.
7. Back in the dashboard, add fingerprint mappings (slot → user).
8. Flash `esp32_smart_assistant.ino` with your Wi-Fi + Device ID + Device Secret.
9. Place a finger on the sensor → personalised briefing appears.

---

## Future Extensions

- Google Calendar / CalDAV integration for automatic schedule sync
- News preferences per user (category, country)
- E-ink display output for always-on mirror display
- Smart mirror enclosure with a two-way mirror panel
- OTA (Over-the-Air) firmware updates for the ESP32
- Mobile push notifications
- Deployment to a cloud server (Railway, Render, Fly.io)
- Voice response via I2S speaker on ESP32
