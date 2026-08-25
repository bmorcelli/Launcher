#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <interface.h>

#define SEL_BTN 0

void _setup_gpio() {
#ifdef USE_SD_MMC
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
#endif

    pinMode(TFT_BL, OUTPUT);
    launcherGpioWrite(TFT_BL, HIGH);

    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);

    launcherGpioOutput(TFT_DC);
    launcherGpioWrite(TFT_DC, HIGH);

    launcherGpioOutput(TFT_RST);
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(10);
    launcherGpioWrite(TFT_RST, LOW);
    launcherDelayMs(20);
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(120);

    hal_buttons_init(DeviceButtons{SEL_BTN}, 1);
}

void InputHandler(void) { hal_buttons_poll_1(DeviceButtons{SEL_BTN}); }
