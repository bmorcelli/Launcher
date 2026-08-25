#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>
#define SEL_BTN 37
#define UP_BTN 35
#define DW_BTN 39

static DeviceButtons buttonsCfg() {
    DeviceButtons cfg{UP_BTN, DW_BTN, SEL_BTN};
    cfg.pullup = false;
    return cfg;
}

void _setup_gpio() {
    hal_buttons_init(buttonsCfg(), 3);
    launcherGpioOutput(4);      // Keeps the Stick alive after take off the USB cable
    launcherGpioWrite(4, HIGH); // Keeps the Stick alive after take off the USB cable
    // https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick_SDCard.jpg
    // Keeps this pin high to allow working with the following pinout
    // Keeps this pin high to allow working with the following pinout
    launcherGpioOutput(32);
    launcherGpioOutput(33);
    launcherGpioWrite(32, LOW);
    launcherGpioWrite(33, HIGH);
    gpio_pulldown_dis(GPIO_NUM_36);
    gpio_pullup_dis(GPIO_NUM_36);
}

void InputHandler(void) { hal_buttons_poll_3(buttonsCfg()); }

void powerOff() {
    launcherGpioWrite(4, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)UP_BTN, LOW);
    esp_deep_sleep_start();
}

void checkReboot() {
    static unsigned long time_count = 0;
    static bool armed = false;
    int countDown;
    /* Long press power off */
    if (launcherGpioRead(UP_BTN) == LOW) {
        if (armed == false) {
            time_count = launcherMillis();
            armed = true;
            return;
        }
        if (launcherMillis() - time_count < 500) return;

        while (launcherGpioRead(UP_BTN) == LOW) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setCursor(60, 12);
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                tft->printf(" PWR OFF IN %d/3\n", countDown);
                launcherDelayMs(10);
            }
        }
        // Clear text after releasing the button
        tft->fillRect(60, 12, tftWidth - 60, 8, BGCOLOR);
    }
    armed = false;
}
