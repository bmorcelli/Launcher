#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#ifndef TFT_BRIGHT_CHANNEL
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_FREQ 5000
#define TFT_BRIGHT_Bits 8
#define TFT_BL 25
#endif

// NM-CYD-C5 uses XPT2046 resistive touch on the shared SPI bus
#include "CYD28_TouchscreenR.h"
#ifndef CYD28_DISPLAY_HOR_RES_MAX
#define CYD28_DISPLAY_HOR_RES_MAX 320
#endif
#ifndef CYD28_DISPLAY_VER_RES_MAX
#define CYD28_DISPLAY_VER_RES_MAX 240
#endif
CYD28_TouchR touch(CYD28_DISPLAY_HOR_RES_MAX, CYD28_DISPLAY_VER_RES_MAX);

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioOutput(CYD28_TouchR_CS); // Touch CS pin (XPT2046)
    launcherGpioWrite(CYD28_TouchR_CS, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    // Display and touch share the same SPI bus; pass &SPI so the driver reuses it
    if (!touch.begin(&SPI)) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
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
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 250 || LongPress) { // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
        LTouchPoint t;
#ifdef DONT_USE_INPUT_TASK
        checkPowerSaveTime();
#endif
        if (touch.touched()) {
            tm = launcherMillis();
#ifdef DONT_USE_INPUT_TASK // need to reset the variables to avoid ghost click
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            touchPoint.pressed = false;
#endif
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
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() { /* No dedicated reboot button on NM-CYD-C5 */ }
