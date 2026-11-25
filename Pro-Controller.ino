#include <Wire.h>
#include <BleGamepad.h>
#include "PCF8574.h"
#include <LovyanGFX.hpp>

// =========================================================================
// 1. ILI9488 DISPLAY + XPT2046 TOUCHSCREEN SETUP (LOVYANGFX)
// =========================================================================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel_instance;
  lgfx::Bus_SPI       _bus_instance;
  lgfx::Light_PWM     _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

public:
  LGFX(void) {
    { // Display SPI Bus (SPI2)
      auto cfg = _bus_instance.config();
      cfg.spi_host   = SPI2_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = 40000000;
      cfg.pin_sclk   = 12;
      cfg.pin_mosi   = 11;
      cfg.pin_miso   = -1;
      cfg.pin_dc     = 9;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    { // ILI9488 Panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs       = 10;
      cfg.pin_rst      = 14;
      cfg.panel_width  = 320;
      cfg.panel_height = 480;
      cfg.bus_shared   = false;
      _panel_instance.config(cfg);
    }
    { // Backlight PWM
      auto cfg = _light_instance.config();
      cfg.pin_bl = 13;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    { // XPT2046 Touchscreen (SPI3)
      auto cfg = _touch_instance.config();
      cfg.x_min      = 300;
      cfg.x_max      = 3900;
      cfg.y_min      = 300;
      cfg.y_max      = 3900;
      cfg.pin_int    = -1;
      cfg.bus_shared = false;
      cfg.spi_host   = SPI3_HOST;
      cfg.freq       = 1000000;
      cfg.pin_sclk   = 39;
      cfg.pin_mosi   = 41;
      cfg.pin_miso   = 40;
      cfg.pin_cs     = 42;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
    setPanel(&_panel_instance);
  }
};

LGFX tft;
BleGamepad bleGamepad("Xbox Wireless Controller", "DIY-12th-Pro", 100);
PCF8574 pcf8574(0x20); // A0, A1, A2 connected to GND

// =========================================================================
// 2. PIN DEFINITIONS & BUTTON MAPPING
// =========================================================================
#define DPAD_UP    5
#define DPAD_DOWN  6
#define DPAD_LEFT  7
#define DPAD_RIGHT 21

// Direct GPIO Buttons: A, B, X, Y, L1, R1 (Active LOW)
const int actionPins[] = {38, 45, 46, 47, 48, 0};
const int actionBtns[] = {BUTTON_1, BUTTON_2, BUTTON_4, BUTTON_5, BUTTON_7, BUTTON_8};

// PCF8574 Pins: Start, Select, L2, R2, Extra, [P5=TTP223], L3, R3
const int pcfBtnMap[]  = {BUTTON_12, BUTTON_11, BUTTON_9, BUTTON_10, BUTTON_13, 0, BUTTON_14, BUTTON_15};

// Dual PS2 Joysticks (ADC1)
#define LX_PIN 1
#define LY_PIN 2
#define RX_PIN 3
#define RY_PIN 4
#define DEADZONE 260

// =========================================================================
// 3. STATE VARIABLES
// =========================================================================
bool turboEnabled     = false;
bool lastTouchSensor  = false;
bool turboPulse       = false;
bool lastBleState     = false;
bool lastTurboUi      = true;
uint8_t brightnessLvl = 2;
unsigned long lastScreenDraw = 0;
unsigned long lastTouchTap   = 0;
int oldLx = 115, oldLy = 165, oldRx = 365, oldRy = 165;

// =========================================================================
// 4. HELPER FUNCTIONS
// =========================================================================
int16_t mapAxis(int raw, bool invert) {
  int delta = raw - 2048;
  if (abs(delta) < DEADZONE) return 16383;
  if (invert) delta = -delta;
  return constrain(map(delta, -2048, 2048, 0, 32767), 0, 32767);
}

uint8_t readDpadHat() {
  bool u = (digitalRead(DPAD_UP) == LOW);
  bool d = (digitalRead(DPAD_DOWN) == LOW);
  bool l = (digitalRead(DPAD_LEFT) == LOW);
  bool r = (digitalRead(DPAD_RIGHT) == LOW);
  if (u && r) return DPAD_UP_RIGHT;
  if (u && l) return DPAD_UP_LEFT;
  if (d && r) return DPAD_DOWN_RIGHT;
  if (d && l) return DPAD_DOWN_LEFT;
  if (u) return DPAD_UP;
  if (d) return DPAD_DOWN;
  if (l) return DPAD_LEFT;
  if (r) return DPAD_RIGHT;
  return DPAD_CENTERED;
}

void drawTouchButtons() {
  tft.fillRoundRect(10, 265, 108, 45, 8, turboEnabled ? TFT_RED : TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 280);
  tft.print(turboEnabled ? "TURBO:ON" : "TURBO:OF");

  tft.fillRoundRect(127, 265, 108, 45, 8, TFT_BLUE);
  tft.setCursor(142, 280);
  tft.print("LIGHT:");
  tft.print(brightnessLvl);

  tft.fillRoundRect(244, 265, 108, 45, 8, TFT_DARKGREEN);
  tft.setCursor(256, 280);
  tft.print("MACRO 1");

  tft.fillRoundRect(361, 265, 108, 45, 8, TFT_PURPLE);
  tft.setCursor(373, 280);
  tft.print("Q-SCOPE");
}

void drawStaticDashboard() {
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 480, 40, TFT_NAVY);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(15, 12);
  tft.print("DIY PRO CONTROLLER (LOVYANGFX)");

  tft.drawCircle(115, 165, 58, TFT_DARKGREY);
  tft.drawCircle(365, 165, 58, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(85, 232);  tft.print("MOVE");
  tft.setCursor(330, 232); tft.print("CAMERA");

  drawTouchButtons();
}

void updateScreen(int16_t lx, int16_t ly, int16_t rx, int16_t ry, bool connected) {
  if (connected != lastBleState) {
    lastBleState = connected;
    tft.fillRect(15, 50, 230, 28, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(15, 55);
    tft.setTextColor(connected ? TFT_GREEN : TFT_YELLOW);
    tft.print(connected ? "BT: CONNECTED" : "BT: PAIRING...");
  }

  if (turboEnabled != lastTurboUi) {
    lastTurboUi = turboEnabled;
    tft.fillRect(265, 50, 205, 28, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(270, 55);
    tft.setTextColor(turboEnabled ? TFT_RED : TFT_LIGHTGREY);
    tft.print(turboEnabled ? "RAPID FIRE: ON" : "RAPID FIRE:OFF");
    drawTouchButtons();
  }

  tft.fillCircle(oldLx, oldLy, 9, TFT_BLACK);
  tft.fillCircle(oldRx, oldRy, 9, TFT_BLACK);

  oldLx = 115 + map(lx, 0, 32767, -45, 45);
  oldLy = 165 + map(ly, 0, 32767, -45, 45);
  oldRx = 365 + map(rx, 0, 32767, -45, 45);
  oldRy = 165 + map(ry, 0, 32767, -45, 45);

  tft.fillCircle(oldLx, oldLy, 9, TFT_CYAN);
  tft.fillCircle(oldRx, oldRy, 9, TFT_ORANGE);
}

// =========================================================================
// 5. SETUP & LOOP
// =========================================================================
void setup() {
  tft.init();
  tft.setRotation(1);
  tft.setBrightness(170);
  drawStaticDashboard();

  Wire.begin(8, 18);
  pcf8574.begin();

  pinMode(DPAD_UP, INPUT_PULLUP);
  pinMode(DPAD_DOWN, INPUT_PULLUP);
  pinMode(DPAD_LEFT, INPUT_PULLUP);
  pinMode(DPAD_RIGHT, INPUT_PULLUP);
  for (int i = 0; i < 6; i++) pinMode(actionPins[i], INPUT_PULLUP);
  for (int i = 0; i < 8; i++) pcf8574.pinMode(i, INPUT);

  BleGamepadConfiguration config;
  config.setAutoReport(false);
  config.setControllerType(CONTROLLER_TYPE_GAMEPAD);
  config.setButtonCount(16);
  config.setHatSwitchCount(1);
  config.setWhichAxes(true, true, true, false, false, true, false, false);
  bleGamepad.begin(&config);
}

void loop() {
  int16_t lx = mapAxis(analogRead(LX_PIN), false);
  int16_t ly = mapAxis(analogRead(LY_PIN), true);
  int16_t rx = mapAxis(analogRead(RX_PIN), false);
  int16_t ry = mapAxis(analogRead(RY_PIN), true);

  bool sensorTouched = (pcf8574.digitalRead(5) == HIGH);
  if (sensorTouched && !lastTouchSensor) turboEnabled = !turboEnabled;
  lastTouchSensor = sensorTouched;

  uint16_t tx, ty;
  bool screenTouched = tft.getTouch(&tx, &ty);
  bool macro1Active = false;
  bool quickScopeActive = false;

  if (screenTouched && ty > 260) {
    if (tx >= 10 && tx <= 118 && (millis() - lastTouchTap > 250)) {
      lastTouchTap = millis();
      turboEnabled = !turboEnabled;
    } else if (tx >= 127 && tx <= 235 && (millis() - lastTouchTap > 250)) {
      lastTouchTap = millis();
      brightnessLvl = (brightnessLvl % 3) + 1;
      tft.setBrightness(brightnessLvl * 85);
      drawTouchButtons();
    } else if (tx >= 244 && tx <= 352) {
      macro1Active = true;
    } else if (tx >= 361 && tx <= 469) {
      quickScopeActive = true;
    }
  }

  bool connected = bleGamepad.isConnected();
  if (connected) {
    turboPulse = !turboPulse;
    bleGamepad.setAxes(lx, ly, rx, 0, 0, ry, 0, 0);
    bleGamepad.setHat1(readDpadHat());

    for (int i = 0; i < 6; i++) {
      bool isPressed = (digitalRead(actionPins[i]) == LOW);
      if (actionBtns[i] == BUTTON_8 && turboEnabled && isPressed) {
        if (turboPulse) bleGamepad.press(BUTTON_8);
        else bleGamepad.release(BUTTON_8);
      } else {
        if (isPressed) bleGamepad.press(actionBtns[i]);
        else bleGamepad.release(actionBtns[i]);
      }
    }

    if (quickScopeActive) {
      bleGamepad.press(BUTTON_7);
      bleGamepad.press(BUTTON_8);
    }

    if (macro1Active) bleGamepad.press(BUTTON_16);
    else bleGamepad.release(BUTTON_16);

    for (int i = 0; i < 8; i++) {
      if (i == 5) continue;
      if (pcf8574.digitalRead(i) == LOW) bleGamepad.press(pcfBtnMap[i]);
      else bleGamepad.release(pcfBtnMap[i]);
    }

    bleGamepad.sendReport();
  }

  if (millis() - lastScreenDraw > 50) {
    lastScreenDraw = millis();
    updateScreen(lx, ly, rx, ry, connected);
  }

  delay(8);
}
