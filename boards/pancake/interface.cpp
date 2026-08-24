#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define HAS_CAPACITIVE_TOUCH 1
#define TOUCH_SDA 9
#define TOUCH_SCL 10
#define TOUCH_RST 8
#define TOUCH_INT 25

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = TOUCH_SDA;
    cfg.pin_scl = TOUCH_SCL;
    cfg.pin_rst = TOUCH_RST;
    cfg.pin_irq = TOUCH_INT;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

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

    // Capacitive touch. Raise the touch threshold (chip default ~22) to
    // reduce phantom touches when the device is in a case.
    hal_touch_init(touchCfg());
    hal_touch_set_threshold(40);
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
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
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
