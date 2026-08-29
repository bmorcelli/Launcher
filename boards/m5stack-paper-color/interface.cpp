#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>

#define SEL_BTN 1
#define UP_BTN 9
#define DW_BTN 10

void _setup_gpio() {
    M5.Display.setBrightness(0);
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    pinMode(SEL_BTN, INPUT); // Top button
    pinMode(UP_BTN, INPUT);  // upper button
    pinMode(DW_BTN, INPUT);  // down button

    _cs = 47;
    _sck = 15;
    _miso = 14;
    _mosi = 13;
}

void _post_setup_gpio() {
    tft->fillScreen(BGCOLOR);
    tft->setTextSize(_fg);
    tft->drawCentreString("LAUNCHER", TFT_WIDTH / 2, 200);
    tft->setTextSize(_fm);
    tft->drawCentreString("Press top Button", TFT_WIDTH / 2, 300);
    tft->drawCentreString("to start Launcher", TFT_WIDTH / 2, 350);
    tft->drawCentreString("WebUI", TFT_WIDTH / 2, 400);
    tft->display();
}

int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    // M5.Display.setBrightness(brightval);
}

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
