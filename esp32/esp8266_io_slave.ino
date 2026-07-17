/*
 * ESP8266 UART slave — GPIO expander for Smart Mirror
 *
 * WIRING to XIAO ESP32-S3:
 *   ESP32-S3 D3/GPIO4 (TX)  ──►  ESP8266 D1/GPIO5 (RX)
 *   ESP32-S3 D8/GPIO7 (RX)  ◄──  ESP8266 D2/GPIO4 (TX)
 *   GND                     ──►  GND  (must be shared)
 *   Power ESP8266 from its own USB
 *
 * BUTTONS — wire each between pin and GND (active LOW):
 *   D5/GPIO14 = DISP0   D6/GPIO12 = DISP1
 *   D7/GPIO13 = DISP2   RX/GPIO3  = DISP3
 *
 * OUTPUTS (LEDs only — no buzzer):
 *   D0/GPIO16 = LED RED    D3/GPIO0 = LED YELLOW     D4/GPIO2 = LED BLUE
 *
 * PROTOCOL (9600 baud, 1 byte each direction):
 *   ESP32-S3 → ESP8266 : output bitmask
 *       bit0=LED_RED  bit1=LED_YELLOW  bit2=LED_BLUE
 *   ESP8266 → ESP32-S3 : button bitmask (sent every 50 ms)
 *       bit0=DISP0  bit1=DISP1  bit2=DISP2  bit3=DISP3
 */

#include <SoftwareSerial.h>

// UART to ESP32-S3  (D1=GPIO5 as RX, D2=GPIO4 as TX)
#define IO_RX_PIN  5   // D1/GPIO5 — receives from ESP32-S3 TX (D3/GPIO4)
#define IO_TX_PIN  4   // D2/GPIO4 — sends to ESP32-S3 RX (D8/GPIO7)
SoftwareSerial hostSerial(IO_RX_PIN, IO_TX_PIN);

// Buttons (active LOW, internal pull-up)
#define BTN_DISP0  14   // D5
#define BTN_DISP1  12   // D6
#define BTN_DISP2  13   // D7
#define BTN_DISP3   3   // RX / GPIO3  — no external resistor needed

// Weather LEDs
#define LED_RED_PIN 16  // D0 / GPIO16
#define LED_YLW_PIN 0   // D3 / GPIO0 (boot-sensitive: set LOW before OUTPUT)
#define LED_BLU_PIN 2   // D4 / GPIO2 (boot-sensitive: set LOW before OUTPUT)

static uint32_t lastSend = 0;

void setup() {
  // GPIO0/GPIO2 are boot-sensitive — set LOW before OUTPUT to avoid glitch
  digitalWrite(LED_RED_PIN, LOW); pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_YLW_PIN, LOW); pinMode(LED_YLW_PIN, OUTPUT);
  digitalWrite(LED_BLU_PIN, LOW); pinMode(LED_BLU_PIN, OUTPUT);

  pinMode(BTN_DISP0, INPUT_PULLUP);
  pinMode(BTN_DISP1, INPUT_PULLUP);
  pinMode(BTN_DISP2, INPUT_PULLUP);
  pinMode(BTN_DISP3, INPUT_PULLUP);

  hostSerial.begin(9600);

  // Self-test: cycle all three LEDs
  digitalWrite(LED_YLW_PIN, HIGH); delay(200);
  digitalWrite(LED_BLU_PIN, HIGH); delay(200);
  digitalWrite(LED_RED_PIN, HIGH); delay(200);
  digitalWrite(LED_YLW_PIN, LOW);
  digitalWrite(LED_BLU_PIN, LOW);
  digitalWrite(LED_RED_PIN, LOW);
}

void loop() {
  // Receive output command from ESP32-S3 (1 byte = LED bitmask)
  while (hostSerial.available()) {
    uint8_t mask = hostSerial.read();
    digitalWrite(LED_RED_PIN, (mask & 0x01) ? HIGH : LOW);
    digitalWrite(LED_YLW_PIN, (mask & 0x02) ? HIGH : LOW);
    digitalWrite(LED_BLU_PIN, (mask & 0x04) ? HIGH : LOW);
  }

  // Send button state to ESP32-S3 every 50 ms
  if (millis() - lastSend >= 50) {
    lastSend = millis();
    uint8_t state = 0;
    if (digitalRead(BTN_DISP0) == LOW) state |= 0x01;
    if (digitalRead(BTN_DISP1) == LOW) state |= 0x02;
    if (digitalRead(BTN_DISP2) == LOW) state |= 0x04;
    if (digitalRead(BTN_DISP3) == LOW) state |= 0x08;
    hostSerial.write(state);
  }

  delay(5);
}
