\
#include <bluefruit.h>

// =====================================================
// MAC-STYLE BLE KEYBOARD
// Matrix: 6 rows x 16 columns
// Music controls are NOT included.
// =====================================================

BLEHidAdafruit blehid;

const int ROWS = 6;
const int COLS = 16;

// IMPORTANT:
// These are EXAMPLE Arduino pin numbers, not guaranteed
// nRF52840 GPIO assignments. Replace them with the
// actual Arduino pin numbers for your board.
//
// Every row and column must use a different GPIO.
// Do not reuse pins used by your other hardware.

const uint8_t rowPins[ROWS] = {
  2, 3, 4, 5, 6, 7
};

const uint8_t colPins[COLS] = {
  8, 9, 10, 11, 12, 13, 14, 15,
  16, 17, 18, 19, 20, 21, 22, 23
};

// Internal Fn key marker. Fn is handled by firmware,
// rather than sent as a normal key to the computer.
const uint8_t FN_KEY = 0xFF;

// Keymap: [row][column]
//
// Empty positions are 0.
// The space bar occupies ONE electrical matrix position,
// even if its physical keycap spans several columns.

const uint8_t keymap[ROWS][COLS] = {

  // ROW 0: Esc, number row, Backspace
  {
    HID_KEY_ESCAPE,
    HID_KEY_1, HID_KEY_2, HID_KEY_3, HID_KEY_4,
    HID_KEY_5, HID_KEY_6, HID_KEY_7, HID_KEY_8,
    HID_KEY_9, HID_KEY_0,
    HID_KEY_MINUS, HID_KEY_EQUAL,
    HID_KEY_BACKSPACE,
    0, 0
  },

  // ROW 1: Tab and QWERTY top row
  {
    HID_KEY_TAB,
    HID_KEY_Q, HID_KEY_W, HID_KEY_E, HID_KEY_R,
    HID_KEY_T, HID_KEY_Y, HID_KEY_U, HID_KEY_I,
    HID_KEY_O, HID_KEY_P,
    HID_KEY_BRACKET_LEFT, HID_KEY_BRACKET_RIGHT,
    HID_KEY_BACKSLASH,
    0, 0
  },

  // ROW 2: Home row
  {
    HID_KEY_CAPS_LOCK,
    HID_KEY_A, HID_KEY_S, HID_KEY_D, HID_KEY_F,
    HID_KEY_G, HID_KEY_H, HID_KEY_J, HID_KEY_K,
    HID_KEY_L,
    HID_KEY_SEMICOLON, HID_KEY_APOSTROPHE,
    HID_KEY_ENTER,
    0, 0, 0
  },

  // ROW 3: Bottom letter row and Shift keys
  {
    HID_KEY_SHIFT_LEFT,
    HID_KEY_Z, HID_KEY_X, HID_KEY_C, HID_KEY_V,
    HID_KEY_B, HID_KEY_N, HID_KEY_M,
    HID_KEY_COMMA, HID_KEY_PERIOD, HID_KEY_SLASH,
    HID_KEY_SHIFT_RIGHT,
    0, 0, 0, 0
  },

  // ROW 4: Mac modifiers and space bar
  {
    FN_KEY,                 // COL 0: Fn
    HID_KEY_CONTROL_LEFT,   // COL 1: Control
    HID_KEY_ALT_LEFT,       // COL 2: Option
    HID_KEY_GUI_LEFT,       // COL 3: Command
    HID_KEY_SPACE,          // COL 4: Space
    0, 0, 0, 0,             // COL 5-8: unused
    HID_KEY_GUI_RIGHT,      // COL 9: Command
    HID_KEY_ALT_RIGHT,      // COL 10: Option
    HID_KEY_CONTROL_RIGHT,  // COL 11: Control
    FN_KEY,                 // COL 12: Fn
    0, 0, 0                 // COL 13-15: unused
  },

  // ROW 5: Arrow keys
  {
    0, 0, 0, 0,
    HID_KEY_ARROW_UP,       // COL 4
    HID_KEY_ARROW_DOWN,     // COL 5
    HID_KEY_ARROW_LEFT,     // COL 6
    HID_KEY_ARROW_RIGHT,    // COL 7
    0, 0, 0, 0, 0, 0, 0, 0
  }
};

// Matrix debounce state
bool rawState[ROWS][COLS] = {};
bool stableState[ROWS][COLS] = {};
unsigned long changedAt[ROWS][COLS] = {};

const unsigned long DEBOUNCE_MS = 10;

uint8_t lastModifiers = 0xFF;
uint8_t lastKeys[6] = {255, 255, 255, 255, 255, 255};

// =====================================================
// INITIALISE
// =====================================================

void setup() {
  Serial.begin(115200);

  // Rows are pulled HIGH.
  for (int r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
  }

  // Columns are initially disconnected (high impedance).
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT);
  }

  // Start Bluetooth.
  Bluefruit.begin();
  Bluefruit.setName("MacStyleKeyboard");

  blehid.begin();

  Bluefruit.Advertising.addFlags(
    BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE
  );
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(blehid);
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);

  Serial.println("Mac-style keyboard started.");
}

// =====================================================
// SCAN MATRIX
//
// This scanning method assumes each diode's BAR
// (cathode) faces the COLUMN side:
//
// ROW -> switch -> diode anode -> diode cathode/bar -> COL
//
// If your physical diode orientation differs, the scanning
// method must be adjusted to match it.
// =====================================================

void scanMatrix() {
  unsigned long now = millis();

  for (int c = 0; c < COLS; c++) {

    // Disconnect every column first.
    for (int j = 0; j < COLS; j++) {
      pinMode(colPins[j], INPUT);
    }

    // Drive only the selected column LOW.
    digitalWrite(colPins[c], LOW);
    pinMode(colPins[c], OUTPUT);

    delayMicroseconds(30);

    for (int r = 0; r < ROWS; r++) {

      // A pressed key pulls its row LOW.
      bool pressed = (digitalRead(rowPins[r]) == LOW);

      if (pressed != rawState[r][c]) {
        rawState[r][c] = pressed;
        changedAt[r][c] = now;
      }

      if (rawState[r][c] != stableState[r][c] &&
          now - changedAt[r][c] >= DEBOUNCE_MS) {
        stableState[r][c] = rawState[r][c];
      }
    }
  }

  // Leave every column disconnected.
  for (int c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT);
  }
}

// =====================================================
// SEND BLUETOOTH KEYBOARD REPORT
// =====================================================

void sendKeyboardReport() {
  uint8_t modifiers = 0;
  uint8_t keys[6] = {0, 0, 0, 0, 0, 0};
  int keyCount = 0;

  bool fnPressed = false;

  // First check whether either Fn key is held.
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (stableState[r][c] && keymap[r][c] == FN_KEY) {
        fnPressed = true;
      }
    }
  }

  // Collect pressed keys.
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (!stableState[r][c]) continue;

      uint8_t key = keymap[r][c];

      if (key == 0 || key == FN_KEY) continue;

      // Fn + arrow keys:
      // Up = Page Up, Down = Page Down,
      // Left = Home, Right = End.
      if (fnPressed && r == 5) {
        if (c == 4) key = HID_KEY_PAGE_UP;
        if (c == 5) key = HID_KEY_PAGE_DOWN;
        if (c == 6) key = HID_KEY_HOME;
        if (c == 7) key = HID_KEY_END;
      }

      // HID modifier usages are E0 through E7.
      if (key >= 0xE0 && key <= 0xE7) {
        modifiers |= (1 << (key - 0xE0));
      } else if (keyCount < 6) {
        keys[keyCount++] = key;
      }
    }
  }

  // Avoid sending an unchanged report repeatedly.
  bool changed = (modifiers != lastModifiers);

  for (int i = 0; i < 6; i++) {
    if (keys[i] != lastKeys[i]) changed = true;
  }

  if (changed && Bluefruit.connected()) {
    blehid.keyboardReport(modifiers, keys);

    lastModifiers = modifiers;
    for (int i = 0; i < 6; i++) {
      lastKeys[i] = keys[i];
    }
  }
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  scanMatrix();
  sendKeyboardReport();
  delay(1);
}
