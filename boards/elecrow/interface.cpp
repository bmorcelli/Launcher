#include "idf/launcher_platform.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "powerSave.h"
#include <CYD28_TouchscreenR.h>
#include <Wire.h>
#include <interface.h>

CYD28_TouchR touch(TFT_HEIGHT, TFT_WIDTH);

#ifndef TFT_BRIGHT_CHANNEL
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_FREQ 5000
#define TFT_BRIGHT_Bits 8
#define TFT_BL 27
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(33);
    launcherGpioWrite(33, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    if (!touch.begin(&SPI)) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
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
        if (touch.touched()) {
            auto t = touch.getPointScaled();
            auto t2 = touch.getPointRaw();
            launcherConsolePrintf("\nRAW: Touch Pressed on x=%d, y=%d, rot: %d", t2.x, t2.y, rotation);
            launcherConsolePrintf("\nBEF: Touch Pressed on x=%d, y=%d, rot: %d", t.x, t.y, rotation);
            if (rotation == 3) {
                t.y = (tftHeight + 20) - t.y;
                t.x = tftWidth - t.x;
            }
            if (rotation == 0) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x;
                t.x = t.y;
                t.y = (tftHeight + 20) - tmp;
            }
            launcherConsolePrintf("\nAFT: Touch Pressed on x=%d, y=%d, rot: %d\n", t.x, t.y, rotation);
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;
    }
}
