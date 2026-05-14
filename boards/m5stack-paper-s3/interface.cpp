#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>
#include "idf/launcher_platform.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() { M5.begin(); }

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // M5.Display.setBrightness(brightval);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed() || t.isHolding()) {
            launcherConsolePrintf("\nx1=%d, y1=%d, ", t.x, t.y);
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            launcherConsolePrintf("x2=%d, y2=%d, rot=%d\n", t.x, t.y, rotation);

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;
    }
}
/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    launcherDelayMs(1000);
    M5.Power.powerOff();
    while (1) launcherDelayMs(100);
}
