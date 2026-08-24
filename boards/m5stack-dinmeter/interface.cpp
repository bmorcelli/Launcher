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

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() { hal_encoder_init(encoderCfg()); }

void _post_setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
}

void _setBrightness(uint8_t brightval) {
    int dutyCycle = (brightval * 255) / 100;
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

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
