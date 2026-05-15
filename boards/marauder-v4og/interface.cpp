#include "powerSave.h"
#include <interface.h>

#include "idf/launcher_platform.h"
#include <CYD28_TouchscreenR.h>

CYD28_TouchR touch(320, 240);

#ifdef WAVESENTRY
#include <RotaryEncoder.h>
RotaryEncoder *encoder = nullptr;
IRAM_ATTR void checkPosition() { encoder->tick(); }
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
#ifdef WAVESENTRY
    launcherGpioInputPullup(ENCODER_KEY);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), checkPosition, CHANGE);
#endif
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    if (!touch.begin(&SPI)) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());

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
    // if (brightval > 5) launcherGpioWrite(TFT_BL, HIGH);
    // else launcherGpioWrite(TFT_BL, LOW);

    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        if (touch.touched()) {
            auto t = touch.getPointScaled();
            auto t2 = touch.getPointRaw();
            launcherConsolePrintf("\nRAW: Touch Pressed on x=%d, y=%d, rot: %d", t2.x, t2.y, rotation);
            launcherConsolePrintf("\nBEF: Touch Pressed on x=%d, y=%d, rot: %d", t.x, t.y, rotation);
            if (rotation == 3) {
                t.y = (tftHeight + 20) - t.y;
                t.x = tftWidth - t.x;
            }
            if (rotation == 0) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x;
                t.x = t.y;
                t.y = (tftHeight + 20) - tmp;
            }
            launcherConsolePrintf("\nAFT: Touch Pressed on x=%d, y=%d, rot: %d\n", t.x, t.y, rotation);
            tm = launcherMillis();
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else touchPoint.pressed = false;
    }

#ifdef WAVESENTRY
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
#endif
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    displayRedStripe("Not Available");
    launcherDelayMs(2000);
}
