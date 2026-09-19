#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <esp_sleep.h>
#include <interface.h>

// Seeed reTerminal E1001 -- ESP32-S3, 800x480 e-paper (UC8179 family), no
// touch, 3 physical nav buttons, microSD sharing the panel's SPI bus.
//
// Display pins come from Seeed's own GxEPD2 example:
//   https://github.com/Seeed-Projects/Seeed_GxEPD2/blob/1100ea37c16b910fd79152f4250c13d802b9c20b/examples/GxEPD2_reTerminal_E1001/GxEPD2_reTerminal_E1001.ino
// Button and SD pinout supplied directly by the person porting this board,
// not yet confirmed on hardware -- see connections.md.

#define BTN_SEL 3
#define BTN_NEXT 4
#define BTN_PREV 5

// microSD power enable. Polarity assumed active-HIGH (the common case for a
// dedicated "power enable" line) -- not yet confirmed on hardware.
#define SD_PWR_EN 16

// Battery divider enable -- must be HIGH for the board to run off battery at
// all (not just to gate the ADC read), per Seeed's own example:
// https://github.com/Seeed-Projects/OSHW-reTerminal-Series-E-D/blob/main/examples/base/Battery_Monitor/Battery_Monitor.ino
#define BATTERY_EN 21

static DeviceButtons buttonsCfg() { return DeviceButtons{BTN_PREV, BTN_NEXT, BTN_SEL}; }

void _setup_gpio() {
    // Release any RTC GPIO hold left over from a deep sleep entered by a
    // launched app (e.g. an e-paper app that calls gpio_hold_en()/
    // gpio_deep_sleep_hold_en() on these pins to keep rails/reset lines fixed
    // while asleep, then wakes via reset back into the launcher). Same
    // defensive pattern as the other e-paper boards in this repo
    // (seeedstudio-reterminal-sticky, xteink-x4pro).
    gpio_hold_dis((gpio_num_t)SD_PWR_EN);
    gpio_hold_dis((gpio_num_t)BATTERY_EN);
    gpio_hold_dis((gpio_num_t)BTN_SEL);
    gpio_hold_dis((gpio_num_t)BTN_NEXT);
    gpio_hold_dis((gpio_num_t)BTN_PREV);
    gpio_deep_sleep_hold_dis();

    gpio_reset_pin((gpio_num_t)SD_PWR_EN);
    gpio_reset_pin((gpio_num_t)BATTERY_EN);
    launcherGpioOutput(SD_PWR_EN);
    launcherGpioOutput(BATTERY_EN);
    launcherGpioOutput(TFT_CS);
    launcherGpioOutput(SDCARD_CS);

    // Must be HIGH for the board to run off battery at all, left on for the
    // whole session so getBattery() (ANALOG_BAT_PIN, mykeyboard.cpp) can read
    // the divider at any time.
    launcherGpioWrite(BATTERY_EN, HIGH);

    // Drive CS pins high before the shared bus comes up.
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioWrite(SD_PWR_EN, HIGH);
    launcherDelayMs(100); // Let the card's rail settle before it's touched.

    hal_buttons_init(buttonsCfg(), 3);

    // The e-paper panel is write-only (no MISO), the SD card is on the same
    // SCLK/MOSI pair with its own MISO/CS -- bring the bus up once here
    // rather than letting GxEPD2 own it (GXEPD2_BEGIN_SPI is not set).
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);
}

// getBattery() is not overridden here -- the weak default in mykeyboard.cpp
// already reads ANALOG_BAT_PIN with the right 2x divider multiplier (its
// default) once BATTERY_EN is driven HIGH above.

void _setBrightness(uint8_t brightval) {
    // No backlight/frontlight on this panel.
    (void)brightval;
}

void InputHandler(void) { hal_buttons_poll_3(buttonsCfg()); }

void reboot() {
    // Power-cycle the microSD rail before the CPU resets, so the firmware
    // being launched finds the card in its power-on state, same as the
    // other SPI-shared e-paper boards in this repo.
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioWrite(SD_PWR_EN, LOW);
    launcherDelayMs(200);
    ESP.restart();
}

void powerOff() {
    while (launcherGpioRead(BTN_SEL) == LOW) launcherDelayMs(50);
    launcherDelayMs(100);

    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    tft->setTextSize(FG);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight - 100, 1);
    tft->display();
    launcherDelayMs(1000);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_SEL, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
