#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#define BTN_ACT LOW
#define SEL_BTN 0 // Using Boot button as Select button

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    launcherConsolePrintf("%s\n", String("Setting GPIO 0 as Input, press to access the Launcher").c_str());
    launcherGpioInputPullup(SEL_BTN);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    if (launcherGpioRead(SEL_BTN) == BTN_ACT) {
        SelPress = true;
        AnyKeyPress = true;
    }
}
