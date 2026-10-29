#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <M5GFX.h>
#include <M5Unified.h>
#include <interface.h>

// BtnA -> Next (single click) / Sel (600ms hold or double click)
// BtnB -> Prev (single click) / Esc (600ms hold or double click)
#define BTN_A 4
#define BTN_B 3

void _setup_gpio() {
    // M5Unified detects PaperDIY through its M5PM1 (I2C 0x6E on G41/G42) and
    // powers the e-paper panel (M5PM1 GPIO2 -> EPD_PWR).
    auto cfg = M5.config();
    cfg.clear_display = false;
    M5.begin(cfg);
    hal_buttons_init_2(DeviceButtons{BTN_A, BTN_B}, 600);
}

void _post_setup_gpio() {}

int getBattery() {
    int percent;
    percent = M5.Power.getBatteryLevel();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

// M5PM1 GPIO3 is wired to the charger's CHG_STAT (low = charging).
bool isCharging() { return M5.Power.isCharging() == m5::Power_Class::is_charging; }

void _setBrightness(uint8_t brightval) {
    // E-paper, no backlight
}

void InputHandler(void) { hal_buttons_poll_2(); }

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
