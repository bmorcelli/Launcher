#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#define UP_BTN 2
#define DW_BTN 13
#define L_BTN 27
#define R_BTN 35
#define SEL_BTN 34
#define ESC_BTN 12
#define BTN_ACT LOW

static DeviceButtons buttonsCfg() { return DeviceButtons{L_BTN, R_BTN, UP_BTN, DW_BTN, SEL_BTN, ESC_BTN}; }

void _setup_gpio() {
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    hal_buttons_init(buttonsCfg(), 6);
}

void _post_setup_gpio() {}

int getBattery() { return 0; }

void _setBrightness(uint8_t brightval) { (void)brightval; }

void InputHandler(void) { hal_buttons_poll_6(buttonsCfg(), /*esc_on_combo_too=*/true); }

void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
