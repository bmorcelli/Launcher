#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#include "papermono_bsp.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() { PaperMonoBsp::instance().begin(); }

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {}

/***************************************************************************************
** Function name: getBattery()
** Location: display.cpp
** Description:   Delivers the battery value from 0-100; 0 is unavailable
***************************************************************************************/
int getBattery() { return PaperMonoBsp::instance().batteryLevel(); }

/*********************************************************************
** Function: setBrightness
** Location: settings.cpp
** Description: frontlight behavior is deferred until the PaperMono P3/P4 slices
**********************************************************************/
void _setBrightness(uint8_t brightval) { (void)brightval; }

/*********************************************************************
** Function: InputHandler
** Handles touch through the board-local PaperMono service.
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        PaperMonoTouchSample sample;
        if (PaperMonoBsp::instance().readTouch(sample) && sample.touched) {
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = sample.x;
            touchPoint.y = sample.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else {
            touchPoint.pressed = false;
        }
    }
}

/*********************************************************************
** Function: powerOff
** Location: mykeyboard.cpp
** Description: use the board-local PaperMono PM1 shutdown service
**********************************************************************/
void powerOff() { PaperMonoBsp::instance().powerOff(); }
