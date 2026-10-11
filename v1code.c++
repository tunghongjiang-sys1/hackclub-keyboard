
/*
  nRF52840 Keyboard Firmware - starter version

  Includes:
  - 6 x 15 keyboard matrix
  - Bluetooth keyboard
  - Rotary encoder (volume up/down)
  - Encoder push (mute)
  - Previous / Play-Pause / Next media buttons
  - 128 x 32 SSD1306 OLED
  - Addressable RGB LEDs

  Macropad controls intentionally not implemented.

  IMPORTANT:
  1. Verify every GPIO against your actual schematic.
  2. Verify the matrix diode direction.
  3. Verify the board's Arduino pin mapping.
  4. Test with current-limited power before connecting a battery.
*/

#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

// =====================================================
// 1. CONFIGURATION: CHANGE THESE TO MATCH YOUR PCB
// =====================================================

// Replace these example identifiers with the correct
// Arduino pin constants for your board definition.

// Keyboard matrix: 6 rows, 15 columns
const uint8_t ROWS = 6;
const uint8_t COLS = 15;

const uint8_t rowPins[ROWS] = {
  P0_02, P0_03, P0_04, P0_05, P0_06, P0_07
};

const uint8_t colPins[COLS] = {
  P0_08, P0_11, P0_12, P0_13, P0_14,
  P0_15, P0_16, P0_17, P0_19, P0_20,
  P0_21, P0_22, P0_23, P0_26, P0_27
};

// Rotary encoder
const uint8_t ENCODER_A = P1_03;
const uint8_t ENCODER_B = P1_04;
const uint8_t ENCODER_PUSH = P1_05;

// Three music buttons
const uint8_t MUSIC_PREV = P1_08;
const uint8_t MUSIC_PLAY = P1_09;
const uint8_t MUSIC_NEXT = P1_10;

// RGB LEDs
const uint8_t LED_DATA_PIN = P1_06;
const uint16_t LED_COUNT = 12; // CHANGE to actual count

// OLED: 128 x 32, typical I2C address 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDRESS 0x3C

// Change this if your display uses another I2C address.
// I2C pins must also be configured for your actual board.

// =====================================================
// 2. DEVICES
// =====================================================

BLEDis deviceInfo;
BLEHidAdafruit blehid;

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1
);

Adafruit_NeoPixel leds(
  LED_COUNT, LED_DATA_PIN, NEO_GRB + NEO_KHZ800
);

// =====================================================
// 3. KEYMAP
// =====================================================

// This is a 6 x 15 example US-layout keymap.
// 0 means no key assigned.
// Replace entries with your real physical layout.
//
// Characters are sent as ordinary keyboard characters.
// This simple version sends one key at a time and does
// not provide full NKRO or reliable multi-key rollover.

const char keymap[ROWS][COLS] = {
  {'1','2','3','4','5','6','7','8','9','0','-','=','q','w','e'},
  {'a','s','d','f','g','h','j','k','l',';','z','x','c','v','b'},
  {'n','m',',','.','/',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '},
  {'t','y','u','i','o','p','r','e','w','q','a','s','d','f','g'},
  {'h','j','k','l','z','x','c','v','b','n','m','1','2','3','4'},
  {'5','6','7','8','9','0',' ',' ',' ',' ',' ',' ',' ',' ',' '}
};

bool previousKeyState[ROWS][COLS] = {};
bool currentKeyState[ROWS][COLS] = {};

// =====================================================
// 4. BLUETOOTH
// =====================================================

void startBluetooth() {
  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("My nRF Keyboard");

  deviceInfo.setManufacturer("DIY Keyboard");
  deviceInfo.setModel("nRF52840 Keyboard");
  deviceInfo.begin();

  blehid.begin();

  Bluefruit.Advertising.addFlags(
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE
  );
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(
    BLE_APPEARANCE_HID_KEYBOARD
  );
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);
}

// =====================================================
// 5. OLED
// =====================================================

void updateScreen(const char* message) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.println("nRF52840 Keyboard");
  display.println(Bluefruit.connected()
                  ? "Bluetooth: Connected"
                  : "Bluetooth: Waiting");
  display.println(message);

  display.display();
}

void setupScreen() {
  // Uses the board's default Wire pins.
  // Configure I2C pin routing for your PCB if required.

  if (!display.begin(
        SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found");
    return;
  }

  updateScreen("Starting...");
}

// =====================================================
// 6. RGB LEDS
// =====================================================

void setupLEDs() {
  leds.begin();
  leds.setBrightness(30); // 0-255; start low
  leds.clear();
  leds.show();
}

void setAllLEDs(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    leds.setPixelColor(i, leds.Color(r, g, b));
  }
  leds.show();
}

// =====================================================
// 7. KEYBOARD MATRIX
// =====================================================

// This scan method assumes:
// - one row is driven LOW at a time
// - columns use INPUT_PULLUP
// - diode direction allows current from column
//   through the switch/diode toward the active LOW row
//
// If your diodes point the opposite way, this scan
// must be changed to match your physical circuit.

void setupMatrix() {
  for (uint8_t r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], HIGH);
    pinMode(rowPins[r], OUTPUT);
  }

  for (uint8_t c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
}

void scanMatrix() {
  for (uint8_t r = 0; r < ROWS; r++) {
    // Set every row inactive
    for (uint8_t rr = 0; rr < ROWS; rr++) {
      digitalWrite(rowPins[rr], HIGH);
    }

    // Activate this row
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(30);

    for (uint8_t c = 0; c < COLS; c++) {
      bool pressed = (digitalRead(colPins[c]) == LOW);

      currentKeyState[r][c] = pressed;

      // Send a key when it changes from released to pressed.
      if (pressed && !previousKeyState[r][c]) {
        char ch = keymap[r][c];

        if (ch != 0 && Bluefruit.connected()) {
          blehid.keyPress(ch);
          delay(8);
          blehid.keyRelease();
        }
      }

      previousKeyState[r][c] = pressed;
    }
  }

  // Return rows to inactive state
  for (uint8_t r = 0; r < ROWS; r++) {
    digitalWrite(rowPins[r], HIGH);
  }
}

// =====================================================
// 8. MUSIC BUTTONS
// =====================================================

void sendMediaKey(uint16_t usage) {
  if (!Bluefruit.connected()) return;

  blehid.consumerKeyPress(usage);
  delay(20);
  blehid.consumerKeyRelease();
}

void setupMusicButtons() {
  pinMode(MUSIC_PREV, INPUT_PULLUP);
  pinMode(MUSIC_PLAY, INPUT_PULLUP);
  pinMode(MUSIC_NEXT, INPUT_PULLUP);
}

void scanMusicButtons() {
  static bool oldPrev = HIGH;
  static bool oldPlay = HIGH;
  static bool oldNext = HIGH;

  bool prev = digitalRead(MUSIC_PREV);
  bool play = digitalRead(MUSIC_PLAY);
  bool next = digitalRead(MUSIC_NEXT);

  if (oldPrev == HIGH && prev == LOW) {
    sendMediaKey(HID_USAGE_CONSUMER_SCAN_PREVIOUS_TRACK);
  }

  if (oldPlay == HIGH && play == LOW) {
    sendMediaKey(HID_USAGE_CONSUMER_PLAY_PAUSE);
  }

  if (oldNext == HIGH && next == LOW) {
    sendMediaKey(HID_USAGE_CONSUMER_SCAN_NEXT_TRACK);
  }

  oldPrev = prev;
  oldPlay = play;
  oldNext = next;
}

// =====================================================
// 9. ROTARY ENCODER
// =====================================================

void setupEncoder() {
  pinMode(ENCODER_A, INPUT_PULLUP);
  pinMode(ENCODER_B, INPUT_PULLUP);
  pinMode(ENCODER_PUSH, INPUT_PULLUP);
}

void scanEncoder() {
  static int lastA = HIGH;
  static bool lastPush = HIGH;

  int a = digitalRead(ENCODER_A);
  int b = digitalRead(ENCODER_B);
  bool push = digitalRead(ENCODER_PUSH);

  // Detect one edge of channel A.
  // If direction is reversed, swap volume +/- below.
  if (lastA == HIGH && a == LOW) {
    if (b == HIGH) {
      if (Bluefruit.connected()) {
        blehid.consumerKeyPress(
          HID_USAGE_CONSUMER_VOLUME_INCREMENT);
        delay(10);
        blehid.consumerKeyRelease();
      }
    } else {
      if (Bluefruit.connected()) {
        blehid.consumerKeyPress(
          HID_USAGE_CONSUMER_VOLUME_DECREMENT);
        delay(10);
        blehid.consumerKeyRelease();
      }
    }
  }

  // Encoder push = mute
  if (lastPush == HIGH && push == LOW) {
    if (Bluefruit.connected()) {
      blehid.consumerKeyPress(
        HID_USAGE_CONSUMER_MUTE);
      delay(20);
      blehid.consumerKeyRelease();
    }
  }

  lastA = a;
  lastPush = push;
}

// =====================================================
// 10. SETUP AND MAIN LOOP
// =====================================================

void setup() {
  Serial.begin(115200);

  setupMatrix();
  setupMusicButtons();
  setupEncoder();
  setupLEDs();

  Wire.begin();
  setupScreen();

  startBluetooth();

  setAllLEDs(0, 10, 20);
  updateScreen("Ready to pair");
}

void loop() {
  scanMatrix();
  scanMusicButtons();
  scanEncoder();

  // Keep the OLED refreshed occasionally in a later
  // version if you want volume, layer, or battery info.
  delay(2);
}
