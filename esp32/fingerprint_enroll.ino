/*
 * Fingerprint Enrollment Utility — Smart Morning Assistant
 *
 * Flash this sketch ONCE to enroll each family member's fingerprint
 * into the sensor's internal storage.  After enrolling, note the slot
 * ID printed in the Serial Monitor and enter it in the web dashboard
 * under Devices → Fingerprint Mappings.
 *
 * Then re-flash the main sketch (esp32_smart_assistant.ino).
 *
 * Required library (Arduino Library Manager):
 *   - Adafruit Fingerprint Sensor Library  (by Adafruit)
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Wiring (same as main sketch):
 *
 *   Sensor VCC  → 3.3 V
 *   Sensor GND  → GND
 *   Sensor TX   → GPIO 18  (ESP32-S3 UART2 RX)
 *   Sensor RX   → GPIO 17  (ESP32-S3 UART2 TX)
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Usage:
 *   1. Flash this sketch.
 *   2. Open Serial Monitor at 115200 baud.
 *   3. Type a slot number (1–127) and press Enter.
 *   4. Follow the on-screen instructions to scan the finger twice.
 *   5. The sketch prints "Stored in slot X — map it in the dashboard."
 *   6. Repeat for each user.
 *   7. Re-flash esp32_smart_assistant.ino when finished.
 */

#include <Adafruit_Fingerprint.h>

// ── Sensor pin config ─────────────────────────────────────────────────────────
#define SENSOR_RX 18
#define SENSOR_TX 17

HardwareSerial sensorSerial(2);
Adafruit_Fingerprint finger(&sensorSerial);

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);

  sensorSerial.begin(57600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
  finger.begin(57600);

  Serial.println("════════════════════════════════════════════");
  Serial.println("  Fingerprint Enrollment — Smart Morning Assistant");
  Serial.println("════════════════════════════════════════════\n");

  if (!finger.verifyPassword()) {
    Serial.println("ERROR: Fingerprint sensor not detected.");
    Serial.println("Check wiring and restart.");
    while (true) delay(1000);
  }

  Serial.printf("Sensor ready.  Enrolled templates: %d / %d\n\n",
                finger.templateCount, finger.capacity);

  printInstructions();
}

// ── Loop: wait for slot ID typed in Serial Monitor ────────────────────────────

void loop() {
  if (Serial.available() == 0) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  int slotId = input.toInt();

  if (slotId < 1 || slotId > 127) {
    Serial.println("Invalid slot.  Enter a number between 1 and 127.");
    return;
  }

  // Check if slot already occupied
  if (finger.loadModel(slotId) == FINGERPRINT_OK) {
    Serial.printf("Warning: slot %d already has a template stored.\n", slotId);
    Serial.println("Type 'y' to overwrite, or type a different slot number: ");
    while (Serial.available() == 0) delay(10);
    String confirm = Serial.readStringUntil('\n');
    confirm.trim();
    if (confirm != "y" && confirm != "Y") {
      Serial.println("Cancelled.  Type another slot number to continue.\n");
      return;
    }
  }

  Serial.printf("\nEnrolling fingerprint in slot %d...\n\n", slotId);
  enrollFinger(slotId);
}

// ── Enrollment: two-scan process ──────────────────────────────────────────────

bool enrollFinger(int slotId) {
  // ── Scan 1 ──────────────────────────────────────────────────────────────────
  Serial.println("Step 1 of 2 — Place your finger on the sensor.");

  if (!waitForFinger()) return false;

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println("Image processing failed.  Try again.\n");
    printInstructions();
    return false;
  }
  Serial.println("First scan OK.\n");

  // ── Remove finger ────────────────────────────────────────────────────────────
  Serial.println("Remove your finger.");
  while (finger.getImage() != FINGERPRINT_NOFINGER) delay(100);
  delay(400);

  // ── Scan 2 ──────────────────────────────────────────────────────────────────
  Serial.println("Step 2 of 2 — Place the SAME finger again.");

  if (!waitForFinger()) return false;

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    Serial.println("Second image processing failed.  Try again.\n");
    printInstructions();
    return false;
  }
  Serial.println("Second scan OK.\n");

  // ── Create model ─────────────────────────────────────────────────────────────
  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println("Fingerprints did not match or model creation failed.");
    Serial.println("Make sure you scan the SAME finger both times.  Try again.\n");
    printInstructions();
    return false;
  }

  // ── Store ─────────────────────────────────────────────────────────────────────
  if (finger.storeModel(slotId) != FINGERPRINT_OK) {
    Serial.println("Failed to store the fingerprint template.  Try again.\n");
    printInstructions();
    return false;
  }

  // ── Success ───────────────────────────────────────────────────────────────────
  Serial.println("════════════════════════════════════════════");
  Serial.printf ("  ✓ Fingerprint stored in slot %d\n", slotId);
  Serial.println("════════════════════════════════════════════");
  Serial.println("  Next step:");
  Serial.println("  1. Go to the web dashboard → Devices page.");
  Serial.println("  2. Select your device.");
  Serial.println("  3. Under 'Fingerprint Mappings', add:");
  Serial.printf ("       Slot ID: %d  →  select the user's account.\n", slotId);
  Serial.println("════════════════════════════════════════════\n");

  // Show updated template count
  finger.getTemplateCount();
  Serial.printf("Total enrolled slots: %d\n\n", finger.templateCount);

  printInstructions();
  return true;
}

// ── Wait until a good image is captured ──────────────────────────────────────

bool waitForFinger() {
  uint8_t result;
  unsigned long start = millis();
  const unsigned long TIMEOUT_MS = 15000;   // 15 seconds to place finger

  while (true) {
    if (millis() - start > TIMEOUT_MS) {
      Serial.println("Timeout — no finger detected.  Type a slot number to retry.\n");
      return false;
    }
    result = finger.getImage();
    if (result == FINGERPRINT_OK) return true;
    if (result == FINGERPRINT_NOFINGER) { delay(100); continue; }
    Serial.println("Sensor error.  Try again.\n");
    return false;
  }
}

// ── Print usage hint ──────────────────────────────────────────────────────────

void printInstructions() {
  Serial.println("Type a slot ID (1–127) and press Enter to enroll a new finger:");
}
