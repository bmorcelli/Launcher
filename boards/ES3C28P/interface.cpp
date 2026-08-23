#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// ES3C28P — ESP32-S3 + 2.8" ILI9341V (SPI) + FT6336G capacitive touch (I2C).
// The SD card is wired to the SDIO interface, not SPI; the pins live in
// platformio.ini and sd_functions.cpp mounts it in 4-bit mode from there.

// BOOT button (GPIO0) — the only physical key on the module, used as Esc.
#define BOOT_BTN 0

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
    launcherConsolePrintf("[ES3C28P] FT6336G chip ID: 0x%02X\n", chipId);

    // Raise touch threshold (reg 0x80 = IDTHRESHOLD). Default 22; 40 reduces
    // phantom touches when the module sits in a case.
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0x80);
    Wire.write(40);
    Wire.endTransmission();
}

// Returns true with raw panel coordinates written, false if no touch.
// Panel native space: portrait 240 × 320 (X 0..239, Y 0..319).
static bool _ft6336_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
    uint8_t data[7];
    if (!_ft_read(FT6336_TD_STATUS, data, 7)) return false;
    if ((data[0] & 0x0F) == 0) return false;
    *raw_x = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
    *raw_y = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
    return true;
}

// Translate panel-native coordinates to launcher screen coordinates for the
// current rotation. TFT_WIDTH=240, TFT_HEIGHT=320 in portrait.
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
    // TFT CS high so the bus is quiet while everything else comes up
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    // Audio codec disabled by default (Audio_EN is active LOW)
    pinMode(AUDIO_EN_PIN, OUTPUT);
    digitalWrite(AUDIO_EN_PIN, HIGH);

    // WS2812 status LED off. neopixelWrite() ships with the Arduino core, so the
    // board needs no external LED library.
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);

    // BOOT button doubles as the Esc key
    launcherGpioInputPullup(BOOT_BTN);
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
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 190;
    else if (brightval == 50) dutyCycle = 130;
    else if (brightval == 25) dutyCycle = 60;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

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
        } else if (launcherGpioRead(BOOT_BTN) == LOW) {
            tm = launcherMillis();
            if (!wakeUpScreen()) {
                AnyKeyPress = true;
                EscPress = true;
            }
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    esp_deep_sleep_start();
}
