#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "powerSave.h"
#include <interface.h>

#include "idf/launcher_platform.h"

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

#ifdef WAVESENTRY
#include "hal/inputs/encoder.h"
#define ENCODER_INA 2
#define ENCODER_INB 14
#define ENCODER_KEY 0

static DeviceEncoder encoderCfg() {
    DeviceEncoder cfg;
    cfg.pin_a = ENCODER_INA;
    cfg.pin_b = ENCODER_INB;
    cfg.pin_sel = ENCODER_KEY;
    cfg.pullup = true;
    return cfg;
}
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    pinMode(SDCARD_CS, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    digitalWrite(TFT_CS, HIGH);
#ifdef WAVESENTRY
    hal_encoder_init(encoderCfg());
#endif
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    if (!hal_touch_init(touchCfg())) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
        delay(100);
        hal_touch_init(touchCfg());
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());

    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        } else touchPoint.pressed = false;
    }

#ifdef WAVESENTRY
    hal_encoder_poll(encoderCfg());
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
