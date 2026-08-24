#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "powerSave.h"
#include <interface.h>

#include "idf/launcher_platform.h"

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // rotation:        0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {true, false, false, true};
    bool mirrorY[4] = {false, false, true, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

// --- MAX17048 fuel gauge (SDA=GPIO5, SCL=GPIO4) ---
#define MAX17048_ADDR 0x36
#define MAX17048_REG_SOC 0x04

int getBattery() {
    Wire.beginTransmission(MAX17048_ADDR);
    Wire.write(MAX17048_REG_SOC);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)MAX17048_ADDR, 2) != 2) return 0;
    uint8_t hi = Wire.read();
    Wire.read();
    if (hi > 100) hi = 100;
    return (int)hi;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    pinMode(SDCARD_CS, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    digitalWrite(TFT_CS, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    Wire.begin(5, 4, 400000U); // MAX17048 I2C
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
        delay(100);
        hal_touch_init(touchCfg());
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());

    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // if (brightval > 5) launcherGpioWrite(TFT_BL, HIGH);
    // else launcherGpioWrite(TFT_BL, LOW);

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
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), tftWidth, tftHeight, t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        } else touchPoint.pressed = false;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    displayRedStripe("Not Available");
    launcherDelayMs(2000);
}
