#include "powerSave.h"
#include <interface.h>

#include <M5Unified.h>
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
    uint8_t percent = 0;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    uint8_t _tmp = (255 * brightval) / 100;
    M5.Lcd.setBrightness(_tmp);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, check(SelPress), AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    M5.update();
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool aPressed = (M5.BtnA.isPressed());
    bool bPressed = (M5.BtnB.isPressed());
    bool cPressed = (M5.BtnC.isPressed());

    bool anyPressed = aPressed || bPressed || cPressed;
    if (anyPressed) tm = launcherMillis();
    if (anyPressed && wakeUpScreen()) return;

    AnyKeyPress = anyPressed;
    EscPress = aPressed & cPressed;
    if (EscPress) return;
    PrevPress = aPressed;
    NextPress = cPressed;
    SelPress = bPressed;
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }
