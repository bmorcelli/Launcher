#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>
#define SEL_BTN 34
#define UP_BTN 36
#define DW_BTN 35
#define R_BTN 39
#define L_BTN 13
#define BTN_ACT LOW

static DeviceButtons buttonsCfg() { return DeviceButtons{L_BTN, R_BTN, UP_BTN, DW_BTN, SEL_BTN}; }

void _setup_gpio() { hal_buttons_init(buttonsCfg(), 5); }

void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) { hal_buttons_poll_5(buttonsCfg()); }

void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
