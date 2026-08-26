#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5Unified.h>
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
** Description:   Delivers the battery value from 1-100
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
** Handles touch input through the M5Unified abstraction. PaperMono rail/reset
** sequencing remains deferred to a later controlled peripheral slice.
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed() || t.isHolding()) {
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = t.x;
            touchPoint.y = t.y;
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
** Description: retain M5Unified's PaperMono-aware shutdown abstraction
**********************************************************************/
void powerOff() { PaperMonoBsp::instance().powerOff(); }
