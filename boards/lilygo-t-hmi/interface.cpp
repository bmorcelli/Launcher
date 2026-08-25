#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <interface.h>
#define PWR_EN_PIN 10
#define PWR_ON_PIN 14

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // rotation:        0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {true, false, false, true};
    bool mirrorY[4] = {false, false, true, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    launcherGpioWrite(TFT_BL, HIGH);
    launcherGpioOutput(CYD28_TouchR_CS);
    launcherGpioWrite(CYD28_TouchR_CS, HIGH);
    launcherGpioOutput(PWR_ON_PIN);
    launcherGpioWrite(PWR_ON_PIN, HIGH);
    launcherGpioOutput(PWR_EN_PIN);
    launcherGpioWrite(PWR_EN_PIN, HIGH);
}

void _post_setup_gpio() {
    SPI.begin(CYD28_TouchR_CLK, CYD28_TouchR_MISO, CYD28_TouchR_MOSI);
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Touchscreen initialization failed!").c_str());
    }
    // Brightness control must be initialized after tft in this case @Pirata
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        } else touchPoint.pressed = false;
    }
}
