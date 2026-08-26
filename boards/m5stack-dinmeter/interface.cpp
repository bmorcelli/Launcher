#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/encoder.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define ENCODER_INA 41
#define ENCODER_INB 40
#define ENCODER_KEY 42
#define POWER_HOLD_PIN 46

static DeviceEncoder encoderCfg() {
    DeviceEncoder cfg;
    cfg.pin_a = ENCODER_INA;
    cfg.pin_b = ENCODER_INB;
    cfg.pin_sel = ENCODER_KEY;
    return cfg;
}

void _setup_gpio() { hal_encoder_init(encoderCfg()); }

void _post_setup_gpio() {
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) { hal_encoder_poll(encoderCfg()); }

void powerOff() {
    launcherGpioOutput(POWER_HOLD_PIN);
    for (int i = 0; i < 5; i++) {
        launcherGpioWrite(POWER_HOLD_PIN, LOW);
        launcherDelayMs(50);
        launcherGpioWrite(POWER_HOLD_PIN, HIGH);
        launcherDelayMs(50);
    }
}
