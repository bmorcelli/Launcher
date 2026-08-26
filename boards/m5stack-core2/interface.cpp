#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5Unified.h>
#include <interface.h>

void _setup_gpio() {
    M5.begin(); // Need to test if SDCard inits with the new setup
}

int getBattery() {
    int percent = 0;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) { M5.Display.setBrightness(brightval); }

void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
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
    }
}

void powerOff() { M5.Power.powerOff(); }
