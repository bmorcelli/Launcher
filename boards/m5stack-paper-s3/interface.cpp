#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>

void _setup_gpio() {
    M5.Display.setBrightness(0);
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
#if defined(ARDUINO_M5STACK_PAPER)
    // Draw directly into the IT8951 panel memory and present complete frames
    // explicitly. Repeated full-screen M5Canvas pushes leave this hardware on
    // its first frame with current M5GFX.
    M5.Display.setAutoDisplay(false);
#endif
}

void _post_setup_gpio() {}

int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    // M5.Display.setBrightness(brightval);
}

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
            if (rotation == 0) {
            } else if (rotation == 2) {
                t.y = (tftHeight + (_fm * LH + 4)) - t.y;
                t.x = tftWidth - t.x;
            }
            if (rotation == 3) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = tmp;
            }
            if (rotation == 1) {
                int tmp = t.x;
                t.x = t.y;
                t.y = (tftHeight + (_fm * LH + 4)) - tmp;
            }
            launcherConsolePrintf("x2=%d, y2=%d, rot=%d\n", t.x, t.y, rotation);

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;
    }
}

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
