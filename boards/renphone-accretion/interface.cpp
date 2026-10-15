#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Arduino.h>
#include <interface.h>

// Accretion Phone -- DIY ESP32-S3 + ILI9341 (SPI) + XPT2046 resistive touch.
// Touch-only device: no physical navigation buttons, the on-screen footer
// bar (HAS_TOUCH) provides Esc/Prev/Next. The SD card is wired to the SDIO
// peripheral; the pins live in platformio.ini and sd_functions.cpp mounts it
// in 1-bit mode from there. Battery percentage comes from the shared
// getBattery() (ANALOG_BAT_PIN in mykeyboard.cpp), nothing to do here.

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    // Same table as the generic 2.8" ILI9341 + XPT2046 module (CYD-2432S028),
    // which this panel/touch pair is.
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

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // TFT and touch CS high so both buses are quiet while everything else comes up
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(CYD28_TouchR_CS);
    launcherGpioWrite(CYD28_TouchR_CS, HIGH);

    // Vibration motor off (the pin floats until driven)
    launcherGpioOutput(VIB_PIN);
    launcherGpioWrite(VIB_PIN, LOW);

    // WS2812 LED off. neopixelWrite() ships with the Arduino core, so the
    // board needs no external LED library.
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup, run after TFT and before SD card initialization
***************************************************************************************/
void _post_setup_gpio() {
    // Backlight PWM -- must be done after tft.init()
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

    // The XPT2046 has its own pins, it does not share the TFT SPI bus
    if (!hal_touch_init(touchCfg(), 0x5D, false)) {
        launcherConsolePrintf("%s\n", String("Touch IC not Started").c_str());
        log_i("Touch IC not Started");
    } else launcherConsolePrintf("%s\n", String("Touch IC Started").c_str());
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
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 250 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    launcherGpioWrite(VIB_PIN, LOW);
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    esp_deep_sleep_start();
}
