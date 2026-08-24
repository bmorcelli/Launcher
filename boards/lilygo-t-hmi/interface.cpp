#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <interface.h>
#define PWR_EN_PIN 10
#define PWR_ON_PIN 14

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

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    launcherGpioWrite(TFT_BL, HIGH);
    launcherGpioOutput(CYD28_TouchR_CS);
    launcherGpioWrite(CYD28_TouchR_CS, HIGH);
    launcherGpioOutput(PWR_ON_PIN);
    launcherGpioWrite(PWR_ON_PIN, HIGH);
    launcherGpioOutput(PWR_EN_PIN);
    launcherGpioWrite(PWR_EN_PIN, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    SPI.begin(CYD28_TouchR_CLK, CYD28_TouchR_MISO, CYD28_TouchR_MOSI);
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Touchscreen initialization failed!").c_str());
    }
    // Brightness control must be initialized after tft in this case @Pirata
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
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    // log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    ledcWrite(TFT_BL, dutyCycle); // Channel 0
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
