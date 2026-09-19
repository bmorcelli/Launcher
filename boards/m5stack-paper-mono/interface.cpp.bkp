#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>

#define UP_BTN 2
#define DW_BTN 3

void _setup_gpio() {
    M5.Display.setBrightness(0);

    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    // M5.Power.setLed() targets a PWM/AXP LED path other boards use; this
    // board's LED is m5::LED_PaperMono_Class (M5.Led), whose red channel is
    // driven straight off the PMIC and defaults on until explicitly told
    // to display black.
    M5.Led.setAllColor(0, 0, 0);
    M5.Display.setAutoDisplay(false);
    // UP_BTN -> Prev (single click) / Esc (600ms hold)
    // DW_BTN -> Next (single click) / Sel (600ms hold)
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    hal_buttons_init_2(DeviceButtons{DW_BTN, UP_BTN}, 500);
}

void _post_setup_gpio() {}

int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) { M5.Display.setBrightness(brightval); }

void InputHandler(void) {
    static long tm = 0;
    hal_buttons_poll_2();

    if (launcherMillis() - tm > 200 || LongPress) {
        M5.update();
        auto t = M5.Touch.getDetail();
        if (t.isPressed() || t.isHolding()) {
            launcherConsolePrintf("\nx1=%d, y1=%d, ", t.x, t.y);
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            int tmp = t.y;

            if (rotation == 0) {
                t.y = map(t.x, 0, 480, 0, 800);
                t.y = 800 - t.y;
                t.x = map(tmp, 0, 800, 0, 480);
            } else if (rotation == 2) {
                t.y = map(t.x, 0, 480, 0, 800);
                t.x = map(tmp, 0, 800, 0, 480);
                t.x = 480 - t.x;
            } else if (rotation == 1) {
                t.y = map(t.x, 0, 800, 0, 480);
                t.y = 480 - t.y;
                t.x = map(tmp, 0, 480, 0, 800);
            } else if (rotation == 3) {
                t.y = map(t.x, 0, 800, 0, 480);
                t.x = map(tmp, 0, 480, 0, 800);
                t.y = 480 - t.y;
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
