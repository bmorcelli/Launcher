#include "powerSave.h"
#include <interface.h>

#include "idf/launcher_platform.h"
#include <M5Unified.h>

void _setup_gpio() {
    M5.begin(); // Need to test if SDCard inits with the new setup
}

int getBattery() {
    uint8_t percent = 0;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    uint8_t _tmp = (255 * brightval) / 100;
    M5.Lcd.setBrightness(_tmp);
}

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

void powerOff() { M5.Power.powerOff(); }
