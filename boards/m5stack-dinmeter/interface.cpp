#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000

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
    M5.begin();

    launcherGpioInput(ENCODER_KEY);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), checkPosition, CHANGE);
}
/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
}
/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 5;
    else dutyCycle = ((brightval * 250) / 100);

    // launcherConsolePrintf("dutyCycle for bright 0-255: %d\n", dutyCycle);

    vTaskDelay(10 / portTICK_PERIOD_MS);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        // launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int level = M5.Power.getBatteryLevel();
    return (level < 0) ? 0 : (level >= 100) ? 100 : level;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
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

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { M5.Power.powerOff(); }
