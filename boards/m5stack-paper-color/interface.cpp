#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    M5.begin();
    pinMode(SEL_BTN, INPUT); // Top button
    pinMode(UP_BTN, INPUT);  // upper button
    pinMode(DW_BTN, INPUT);  // down button
}

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
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        bool sel = digitalRead(SEL_BTN) == LOW;
        bool nxt = digitalRead(UP_BTN) == LOW;
        bool prv = digitalRead(DW_BTN) == LOW;
        if (sel || nxt || prv) {
            tm = launcherMillis();
            AnyKeyPress = true;
        }
        if (sel) SelPress = true;
        if (nxt && prv) EscPress = true;
        if (nxt) NextPress = true;
        if (prv) PrevPress = true;
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
    tft->setTextSize(FG);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight - 100, 1);
    tft->display();
    launcherDelayMs(1000);
    M5.Power.powerOff();
    while (1) launcherDelayMs(100);
}
