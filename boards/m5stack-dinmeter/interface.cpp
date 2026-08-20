#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define ENCODER_INA 41
#define ENCODER_INB 40
#define ENCODER_KEY 42
#define BTN_ACT LOW
#define POWER_HOLD_PIN 46

// Rotary encoder
#include <RotaryEncoder.h>
RotaryEncoder *encoder = nullptr;
IRAM_ATTR void checkPosition() { encoder->tick(); }

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInput(ENCODER_KEY);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), checkPosition, CHANGE);
}

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

void InputHandler(void) {
    static unsigned long tm = launcherMillis(); // debauce for buttons
    static int posDifference = 0;
    static int lastPos = 0;
    bool sel = !BTN_ACT;

    int newPos = encoder->getPosition();
    if (newPos != lastPos) {
        posDifference += (newPos - lastPos);
        lastPos = newPos;
    }

    if (launcherMillis() - tm < 200 && !LongPress) return;

    sel = launcherGpioRead(ENCODER_KEY);

    if (posDifference != 0 || sel == BTN_ACT) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    if (posDifference > 0) {
        PrevPress = true;
        posDifference--;
    }
    if (posDifference < 0) {
        NextPress = true;
        posDifference++;
    }

    if (sel == BTN_ACT) {
        posDifference = 0;
        SelPress = true;
        tm = launcherMillis();
    }
}

void powerOff() {
    launcherGpioOutput(POWER_HOLD_PIN);
    for (int i = 0; i < 5; i++) {
        launcherGpioWrite(POWER_HOLD_PIN, LOW);
        launcherDelayMs(50);
        launcherGpioWrite(POWER_HOLD_PIN, HIGH);
        launcherDelayMs(50);
    }
}
