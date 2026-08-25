#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

// If PMIC_BQ25896 / GAUGE_BQ27220 is set in platformio.ini:
// #include "hal/device.h"
// #include "hal/power/gauge.h"
// #include "hal/power/pmic.h"

// For raw-GPIO buttons (HAS_1_BUTTON, HAS_3_BUTTON, HAS_5_BUTTON,
// HAS_6_BUTTON -- no build flag needed, just call the HAL directly with the
// button count):
// #include "hal/device.h"
// #include "hal/inputs/buttons.h"
// static DeviceButtons buttonsCfg() {
//     // 1 button: {Sel}. 3: {Prev, Next, Sel}. 5: {Prev, Next, Up, Down,
//     // Sel}. 6: {Prev, Next, Up, Down, Sel, Esc} -- see hal/inputs/buttons.h
//     DeviceButtons cfg{PREV_BTN, NEXT_BTN, SEL_BTN};
//     cfg.pullup = false; // only if the board has no internal/external pull-up
//     return cfg;
// }

// For HAS_2_BUTTONS with BUTTONS_IDF_COMPONENT=1 set (needs
// lib_deps = https://github.com/bmorcelli/ESP32_Button): btn1 short click ->
// Next, double click or hold -> Sel; btn2 short click -> Prev, double click
// or hold -> Esc.
// #include "hal/device.h"
// #include "hal/inputs/buttons.h"
// call hal_buttons_init_2(DeviceButtons{BTN1, BTN2}, 600) from _setup_gpio()
// and hal_buttons_poll_2() from InputHandler() -- see hal/inputs/buttons.h

// For HAS_ENCODER (needs lib_deps = mathertel/RotaryEncoder in platformio.ini):
// #include "hal/device.h"
// #include "hal/inputs/encoder.h"
// static DeviceEncoder encoderCfg() {
//     DeviceEncoder cfg;
//     cfg.pin_a = ENCODER_A;
//     cfg.pin_b = ENCODER_B;
//     cfg.pin_sel = ENCODER_SEL;
//     cfg.pin_esc = -1; // a real pin if there's a separate dedicated esc button
//     return cfg;
// }
// call hal_encoder_init(encoderCfg()) from _setup_gpio() (default LatchMode
// is TWO03 -- pass EncoderLatchMode::FOUR3 as a second arg for a board wired
// that way, e.g. lilygo-t-lora-pager), then hal_encoder_poll(encoderCfg())
// every InputHandler() cycle.

// For HAS_TOUCH with any TOUCH_CTRL_* set (XPT2046, GT911, CST8XX, FT6X36,
// GT9895, HI8561 -- see src/hal/README.md for which chip each one is):
// #include "hal/device.h"
// #include "hal/inputs/touch.h"
// static DeviceTouch touchCfg() {
//     DeviceTouch cfg;
//     // All I2C-based controllers (everything except TOUCH_CTRL_XPT2046,
//     // which is SPI and whose pins are compile-time macros from
//     // CYD28_TouchscreenR.h instead, see the Touch build_flags below):
//     cfg.pin_sda = -1; // -1 if Wire.begin() is already called elsewhere
//     cfg.pin_scl = -1;
//     cfg.pin_rst = -1;
//     cfg.pin_irq = TOUCH_INT;
//     // TOUCH_CTRL_GT9895 only -- the chip doesn't report its own native
//     // resolution over I2C, this is a fixed vendor/panel constant:
//     // cfg.raw_width = 1060; cfg.raw_height = 2400;
//     // MirrorX/MirrorY/SwapXY are indexed by the display's current
//     // rotation (0-3) -- copy the values from a similar already-migrated
//     // board (see src/hal/inputs/touch.h) rather than guessing; they only
//     // become obvious once you can touch the 4 corners on real hardware.
//     return cfg;
// }
// call hal_touch_init(touchCfg()) from _setup_gpio() (or _post_setup_gpio()
// if the panel/bus needs to come up first), then each InputHandler() cycle:
// LTouchPoint t;
// if (hal_touch_read(touchCfg(), t)) {
//     // hal_touch_apply() wakes the screen (swallowing this press as the
//     // wake-up tap, same convention as every other input source) and
//     // publishes the point to touchPoint/touchHeatMap() -- returns false
//     // if InputHandler() should return immediately instead.
//     if (!hal_touch_apply(t)) return;
// }

/***************************************************************************************
** Function:    _setup_gpio()
** Location:    main.cpp
** Description: initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // Power HAL example (only if PMIC_BQ25896/GAUGE_BQ27220 are set -- Wire
    // must already be begun):
    // DevicePmic pmicCfg{SDA, SCL, 0x6B};
    // hal_pmic_init(pmicCfg); // input current limit defaults to 3250mA;
    //                         // pass a second arg to override (e.g. 2000)
    //
    // If the PMIC must share an I2C bus another driver already began (no
    // second Wire.begin()), use the callback-based entry point instead:
    // hal_pmic_init_via_callbacks(0x6B, myReadReg, myWriteReg);
    //
    // DeviceGauge gaugeCfg{};
    // gaugeCfg.design_capacity_mah = 1300; // leave at 0 (default) to skip
    //                                      // setDesignCap() entirely
    // hal_gauge_init(gaugeCfg);

    // hal_buttons_init(buttonsCfg(), 3); // 1, 3, 5 or 6
}

/***************************************************************************************
** Function:    _post_setup_gpio()
** Location:    main.cpp
** Description: second stage gpio setup, run after TFT and before SD card initialization
***************************************************************************************/
void _post_setup_gpio() {}

/***************************************************************************************
** Function: _late_setup_gpio()
** Location: main.cpp
** Description: third stage gpio, run befor bootscreen animation
***************************************************************************************/
void _late_setup_gpio() {}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    // With GAUGE_BQ27220 set: return hal_gauge_get_percent();
    return 0;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
// For a plain PWM/ledc backlight pin (the common case -- see hal/bright/bright.h):
// #include "hal/bright/bright.h"
// call hal_bright_attach(TFT_BL) once from _post_setup_gpio(), then:
// void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }
// For two simultaneous backlight pins (e.g. screen + keyboard), pass an array:
// uint8_t pins[] = {TFT_BL, KEYBOARD_BL}; hal_bright_set(pins, 2, brightval);
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    // For raw-GPIO buttons, skip everything below and use the HAL instead:
    // hal_buttons_poll_3(buttonsCfg()); // or _poll_1/_poll_5/_poll_6
    // return;
    //
    // For HAS_2_BUTTONS (BUTTONS_IDF_COMPONENT=1):
    // hal_buttons_poll_2();
    // return;

    checkPowerSaveTime();
    PrevPress = false;
    NextPress = false;
    SelPress = false;
    AnyKeyPress = false;
    EscPress = false;

    if (false /*Conditions fot all inputs*/) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else goto END;
    }
    if (false /*Conditions for previous btn*/) { PrevPress = true; }
    if (false /*Conditions for Next btn*/) { NextPress = true; }
    if (false /*Conditions for Esc btn*/) { EscPress = true; }
    if (false /*Conditions for Select btn*/) { SelPress = true; }
END:
    if (AnyKeyPress) {
        long tmp = launcherMillis();
        while ((launcherMillis() - tmp) < 200 && false /*Conditions fot all inputs*/);
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    // put into deepsleep mode, or shutdown if PMIC is available
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
    // or PMIC shutdown if available (PMIC_BQ25896 set):
    // hal_pmic_shutdown();
}

/*********************************************************************
** Function: reboot
** Handles reboot process for devices
**********************************************************************/
void reboot() {
    // function to replace the ESP.restart() function, which is not working on some devices
    // some devices need specific process before ESP.restart() to enable SD mounting and other processes
    ESP.restart();
}
