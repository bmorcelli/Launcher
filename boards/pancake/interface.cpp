#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define HAS_CAPACITIVE_TOUCH 1
#define TOUCH_SDA 9
#define TOUCH_SCL 10
#define TOUCH_RST 8
#define TOUCH_INT 25

// Pancake uses an FT6336U capacitive touch controller over I2C.
// I2C bus is shared with the MAX17048 fuel gauge.
// Pin map:
//   SDA = GPIO9   SCL = GPIO10
//   RST = GPIO8   INT = GPIO25

// ─── MAX17048 fuel gauge (I2C address 0x36, shares bus with FT6336) ──────────

#define MAX17048_ADDR 0x36
#define MAX17048_REG_SOC 0x04 // high byte = integer %, low byte = 1/256 %

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp / mykeyboard.cpp
** Description:   Returns battery percentage 0-100 from MAX17048 fuel gauge.
**                Wire must already be initialised before this is called.
***************************************************************************************/
int getBattery() {
    Wire.beginTransmission(MAX17048_ADDR);
    Wire.write(MAX17048_REG_SOC);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)MAX17048_ADDR, 2) != 2) return 0;
    uint8_t hi = Wire.read(); // integer percent
    Wire.read();              // fractional byte (discard)
    if (hi > 100) hi = 100;
    return (int)hi;
}

// --- FT6336 minimal driver ---

#define FT6336_ADDR 0x38
#define FT6336_TD_STATUS 0x02 // number of touch points
#define FT6336_T1_XH 0x03     // first touch X high byte (4-bit MSB)

static bool _ft_read(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)FT6336_ADDR, (int)len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.available() ? Wire.read() : 0;
    return true;
}

static void _ft6336_init() {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(300);

    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000U);

    // Read chip ID for diagnostics
    uint8_t chipId = 0;
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0xA3);
    if (Wire.endTransmission(false) == 0) {
        Wire.requestFrom((int)FT6336_ADDR, 1);
        if (Wire.available()) chipId = Wire.read();
    }
    launcherConsolePrintf(
        "[Pancake] FT6336 chip ID: 0x%02X%s\n", chipId, chipId == 0x64 ? " (OK)" : " (unexpected)"
    );

    // Raise touch threshold (reg 0x80 = IDTHRESHOLD). Default 22; 40 reduces
    // phantom touches when the device is in a case.
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0x80);
    Wire.write(40);
    Wire.endTransmission();
}

// Returns 1 with raw panel coordinates written, 0 if no touch.
// Panel native space: portrait 320 × 480 (X 0..319, Y 0..479).
static uint8_t _ft6336_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
    uint8_t data[7];
    if (!_ft_read(FT6336_TD_STATUS, data, 7)) return 0;
    if ((data[0] & 0x0F) == 0) return 0;
    *raw_x = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
    *raw_y = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
    return 1;
}

// Translate panel-native coordinates to launcher screen coordinates for the
// current rotation.  TFT_WIDTH=320, TFT_HEIGHT=480 in portrait.
//   rotation 0 → portrait  (launcher x = raw_x, y = raw_y)
//   rotation 1 → landscape (launcher x = raw_y, y = TFT_WIDTH - raw_x)
//   rotation 2 → portrait inverted
//   rotation 3 → landscape inverted
static bool _ft6336_get_point(LTouchPoint *out) {
    uint16_t rx, ry;
    if (!_ft6336_read_raw(&rx, &ry)) return false;
    switch (rotation % 4) {
        case 0:
            out->x = rx;
            out->y = ry;
            break;
        case 1:
            out->x = ry;
            out->y = TFT_WIDTH - rx;
            break;
        case 2:
            out->x = TFT_WIDTH - rx;
            out->y = TFT_HEIGHT - ry;
            break;
        case 3:
            out->x = TFT_HEIGHT - ry;
            out->y = rx;
            break;
    }
    return true;
}

// ─── Launcher board hooks ─────────────────────────────────────────────────────

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // De-select SD card so it doesn't fight the TFT during init
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    // TFT CS high so the bus is quiet during FT6336 I2C init
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Backlight PWM — must be done after tft.init()
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    // Capacitive touch
    _ft6336_init();
}

/*********************************************************************
** Function: _setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 250 || LongPress) {
        LTouchPoint t;
        checkPowerSaveTime();
        if (_ft6336_get_point(&t)) {
            tm = launcherMillis();

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { esp_deep_sleep_start(); }

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() { /* No dedicated reboot button on Pancake */ }
