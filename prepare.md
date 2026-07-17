# Morning Assistant — Presentation Preparation

**Project:** Morning Assistant — Smart Morning Routine Device  
**Authors:** Jagadeeswar Reddy Chennuru (7075082) · Céline Fierlings (2578147) · Fabio Rizzo (7082802)  
**Target time:** 4 minutes

---

## 4-Minute Presentation Script

> Tip: Read at a calm pace. Each section has an estimated time. Total ~560 words ≈ 4 minutes.

---

### Opening (≈ 30 seconds)

Good morning everyone. We are presenting the **Morning Assistant** — a smart device designed to make your morning routine faster, more organised, and genuinely personal.

The idea is simple: mornings are busy. You wake up, you need to know the weather, your schedule for the day, maybe a headline or two — but instead of picking up your phone and getting distracted, our device gives you exactly what you need, right at your mirror or beside your bed, in seconds.

---

### What It Does (≈ 40 seconds)

The Morning Assistant is a small, self-contained device built around the **XIAO ESP32-S3 microcontroller**. It has four OLED displays — one for a personal greeting and motivational quote, one for weather, one for today's news, and one for your task schedule.

Two LEDs give you a quick visual mood indicator for the weather — yellow for sunshine, blue for rain or cold. Three navigation buttons let you scroll through information without touching a phone or screen. And it is designed with a **small form factor** — it fits on a shelf, a bedside table, or beside a mirror without taking up space.

Importantly, it is **multi-user**. A single device can serve a whole household, and each person sees only their own data.

---

### How It Works (≈ 60 seconds)

Let me walk you through the system.

The device uses a **motion sensor** to detect when someone approaches. When it senses presence, it wakes up from sleep mode to save power when nobody is around.

The user then **selects their profile** using the navigation buttons and places their finger on the **fingerprint sensor**. This authenticates them biometrically — no passwords, no phone, just your finger.

Once authenticated, the ESP32 sends an **HTTP request to our cloud backend** — a FastAPI server with a SQLite database — and fetches that specific user's personalised data: their city's weather from OpenWeatherMap, their personal task schedule, live news headlines filtered by their chosen category, and their motivational quotes.

All of this is displayed across the four OLED screens simultaneously. The web dashboard lets each user manage their data — they can add tasks, change their news category between Technology, Business, Sports, Health and so on, and update their weather city — all from any browser.

---

### Use-Case Scenario (≈ 40 seconds)

To make this concrete: imagine Alice waking up in the morning. She approaches the device — the motion sensor wakes it. She presses the button to select her profile and places her finger. The device recognises her and within a couple of seconds her displays update: her name and a motivational quote, today's temperature in her city, the top technology headlines she prefers, and her appointments for the day. She sees a meeting at nine and a dentist appointment at two. She checks off a task from yesterday with one button press. No phone. No distractions. Done in fifteen seconds.

---

### Results and Limitations (≈ 35 seconds)

What we achieved: a convenient, distraction-free overview of the day, intuitive single-button navigation, multi-user support on one shared device, and the hardware is water and splash resistant for bathroom or kitchen use.

Our main limitations are the small display size — four small OLEDs mean limited information density — and the system currently focuses only on visual feedback. There is no audio output.

---

### Future Improvements and Closing (≈ 25 seconds)

Looking forward, we would like to replace the four small displays with one larger, higher-resolution screen. We also want to add more diverse controls and explore **text-to-speech** so the device can read out your briefing while you get dressed.

The Morning Assistant shows that an embedded, privacy-respecting device can deliver a personalised experience without a smartphone — keeping your morning focused. Thank you.

---

---

## Q&A Preparation

These are the most likely questions from professors or evaluators, grouped by topic.

---

### Technical Questions

**Q1: Why did you choose the XIAO ESP32-S3 instead of a Raspberry Pi or a larger board?**

> The XIAO ESP32-S3 is thumb-sized, runs on 3.3 V, has built-in Wi-Fi, and consumes far less power — ideal for a always-present device that spends most of its time sleeping. A Raspberry Pi would require a full Linux OS, takes longer to boot, and draws significantly more power. For a device that just fetches JSON and drives displays, the ESP32 is the right fit.

---

**Q2: How does the fingerprint authentication work? Is the biometric data stored in the cloud?**

> No — biometric data never leaves the device. The fingerprint sensor has its own internal flash memory and stores up to 127 templates locally. When a finger is placed, the sensor matches it internally and returns only a slot number — for example, slot 2. The ESP32 sends that slot number plus a device secret to our backend. The backend maps slot 2 to a user account. The actual fingerprint image or template is never transmitted or stored on the server. This is a key privacy principle we designed around.

---

**Q3: What happens if the Wi-Fi or the server goes down?**

> Currently the device requires a live connection to fetch data. If the server is unreachable, the displays show an error message. A future improvement would be to cache the last successful response on the ESP32's flash storage and display that instead, with a clear "offline" indicator.

---

**Q4: How do you handle multiple users sharing the same device?**

> Each physical device is registered on the web dashboard. The owner can invite family members by email — they each create their own account. Fingerprint slots are then mapped to user accounts on the Devices page. When the ESP32 detects a finger and gets slot 3, it asks the backend "who owns slot 3 on device 1?" — the backend returns that user's personal data. Each user's weather city, schedule, news category, and quotes are completely separate.

---

**Q5: The four OLEDs share the same I2C bus — how do you address them individually if they all have the same I2C address?**

> We use a TCA9548A I2C multiplexer. It sits between the ESP32 and the four OLEDs. The ESP32 writes to the multiplexer to activate channel 0, 1, 2, or 3, and only that OLED is on the bus at that moment. This lets us use four identical SSD1306 OLEDs (all address 0x3C) without any address conflict.

---

**Q6: How is the backend secured?**

> The web dashboard uses JWT token authentication with a 7-day expiry and bcrypt password hashing. The ESP32 authenticates using a device-specific secret (a 48-character random hex string generated at registration) — no user password is ever sent from the hardware. CORS is open for the prototype, but for production we would restrict it to the specific domain.

---

### Design and UX Questions

**Q7: Why four separate displays instead of one larger screen?**

> The modular approach means each piece of information has its own dedicated space — you can glance at weather without your eye scanning across a busy single display. It also maps naturally to our button layout: one button per display. That said, a single larger display is our top future improvement, which would allow richer visualisation.

---

**Q8: How did you decide which information to show?**

> We started from the question: "what does a person actually need to know in the first two minutes of their morning?" We identified four categories — who you are (greeting), what the outside is like (weather), what is happening in the world (news), and what you need to do today (schedule). Everything else is noise. That is what shaped the four-display layout.

---

**Q9: What makes this better than just picking up a smartphone?**

> Three things: focus, speed, and presence. A smartphone requires unlocking, opens a feed of notifications, and makes it too easy to get distracted by social media or messages. Our device shows exactly the four things you chose, nothing else, and is always ready — no unlock, no app to open. The fingerprint login takes about two seconds. It is designed to reduce the morning cognitive load, not add to it.

---

**Q10: How does the motion sensor improve the experience?**

> The HC-SR04 ultrasonic sensor detects when someone is within about 1.5 metres. The device wakes its displays and starts preparing only when needed, and goes back to sleep after a timeout. This avoids the displays running all night, saves power, and means the device is ready by the time you reach it — you do not press a button to wake it.

---

### Project and Process Questions

**Q11: What was the biggest technical challenge?**

> The hardest part was pin allocation on the XIAO ESP32-S3. The board only exposes 11 GPIO pins and we needed: 4 OLEDs (via I2C multiplexer), fingerprint sensor (UART), 3 buttons, 2 LEDs, HC-SR04 (2 pins). That is exactly 11 pins — no spare. We also discovered that many online tutorials for fingerprint sensors reference GPIO17 and GPIO18 — those pins simply do not exist on the XIAO. We had to remap to GPIO43 and GPIO44, which are the hardware UART1 pins on this board.

---

**Q12: How did you split the work between team members?**

> Jagadeeswar handled the backend cloud infrastructure — FastAPI server, database, REST API, and the web dashboard. The hardware assembly, ESP32 sketch, and sensor integration were shared across the team. The poster and presentation design were a joint effort.

---

**Q13: Could this scale to a commercial product?**

> The architecture is already reasonably sound for small scale. The main changes for a real product would be: replace SQLite with PostgreSQL, add HTTPS with a real certificate, implement proper device provisioning (rather than manually pasting secrets), add OTA firmware updates for the ESP32, and move from a VPS to a managed cloud service. The ESP32 hardware cost is around €20-35 per device, which is commercially viable for a niche product.

---

**Q14: What is the water/splash resistance you mentioned?**

> The current prototype is not formally rated. We designed the enclosure with the electronics set back from the front face, and the displays covered by a thin acrylic panel. This provides basic protection against accidental splashes — enough for bathroom or kitchen use. A production version would use conformal coating on the PCB and a proper IP-rated enclosure.

---

### Quick-Fire Answers (30 seconds each)

| Question | Short answer |
|----------|-------------|
| What database? | SQLite — lightweight, zero-config, sufficient for household scale |
| What news source? | NewsAPI — 8 categories, falls back to mock data if key missing |
| What weather source? | OpenWeatherMap free tier |
| How many users per device? | Up to 127 (limited by fingerprint sensor slot count) |
| Does it need the internet? | Yes, currently — local caching is a planned improvement |
| Programming languages used? | Python (FastAPI backend), C++ (Arduino/ESP32 sketch), HTML/CSS/JavaScript (dashboard) |
| How long does a login take? | ~2 seconds from finger placement to all 4 displays updating |

---

## Key Numbers to Remember

- **4** OLED displays
- **3** navigation buttons
- **127** max fingerprint slots (users per device)
- **57600** baud — fingerprint sensor UART speed
- **0x70** — TCA9548A I2C address (A0/A1/A2 all tied to GND)
- **7 days** — JWT token expiry
- **8** news categories selectable per user
- **~2 seconds** — typical login-to-display time over Wi-Fi
