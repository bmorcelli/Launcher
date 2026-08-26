#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

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
    launcherGpioOutput(33);
    launcherGpioWrite(33, HIGH);
}

void _post_setup_gpio() {
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
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
