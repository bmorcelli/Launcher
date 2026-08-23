#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

// If PMIC_BQ25896 / GAUGE_BQ27220 is set in platformio.ini:
// #include "hal/device.h"
// #include "hal/power/gauge.h"
// #include "hal/power/pmic.h"

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
void _setBrightness(uint8_t brightval) {}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
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
