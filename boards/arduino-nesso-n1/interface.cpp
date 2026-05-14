#include "powerSave.h"
#include <M5Unified.h>
#include <interface.h>
#include "idf/launcher_platform.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    M5.begin(); // Need to test if SDCard inits with the new setup
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { M5.Display.setBrightness(brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
        bool bntA = M5.BtnA.isPressed();
        bool bntB = M5.BtnB.isPressed();
        auto t = M5.Touch.getDetail();
        if (t.isPressed() || t.isHolding()) {
            tm = launcherMillis();

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;

        if (bntA || bntB) {
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
        }
        if (bntA) NextPress = true;
        if (bntB) SelPress = true;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }
