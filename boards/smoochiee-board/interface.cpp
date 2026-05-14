#include "powerSave.h"
#include "idf/launcher_platform.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/

// Power handler for battery detection
#include <Wire.h>
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
XPowersPPM PPM;

void _setup_gpio() {

    launcherGpioInputPullup(UP_BTN); // Sets the power btn as an INPUT
    launcherGpioInputPullup(SEL_BTN);
    launcherGpioInputPullup(DW_BTN);
    launcherGpioInputPullup(R_BTN);
    launcherGpioInputPullup(L_BTN);

    launcherGpioOutput(CC1101_SS_PIN);
    launcherGpioOutput(NRF24_SS_PIN);
    launcherGpioOutput(45);

    launcherGpioWrite(45, HIGH);
    launcherGpioWrite(CC1101_SS_PIN, HIGH);
    launcherGpioWrite(NRF24_SS_PIN, HIGH);
    // Starts SPI instance for CC1101 and NRF24 with CS pins blocking communication at start

    bool pmu_ret = false;
    Wire.begin(GROVE_SDA, GROVE_SCL);
    pmu_ret = PPM.init(Wire, GROVE_SDA, GROVE_SCL, BQ25896_SLAVE_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setInputCurrentLimit(3250);
        launcherConsolePrintf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        launcherConsolePrintf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure();
        PPM.enableCharge();
        PPM.enableOTG();
        PPM.disableOTG();
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100+
***************************************************************************************/
int getBattery() {
    uint8_t percent = 0;
    percent = (PPM.getSystemVoltage() - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int bl = MINBRIGHT + round(((255 - MINBRIGHT) * bright / 100));
    analogWrite(TFT_BL, bl);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;
    // read all inputs only once, instead of 4
    bool l = launcherGpioRead(L_BTN);
    bool r = launcherGpioRead(R_BTN);
    bool u = launcherGpioRead(UP_BTN);
    bool d = launcherGpioRead(DW_BTN);
    bool s = launcherGpioRead(SEL_BTN);

    if (s == BTN_ACT || u == BTN_ACT || d == BTN_ACT || r == BTN_ACT || l == BTN_ACT) {
        tm = launcherMillis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    } else return;
    if (l == BTN_ACT && r == BTN_ACT) {
        EscPress = true;
        return;
    }
    if (l == BTN_ACT) { PrevPress = true; }
    if (r == BTN_ACT) { NextPress = true; }
    if (u == BTN_ACT) { UpPress = true; }
    if (d == BTN_ACT) { DownPress = true; }
    if (s == BTN_ACT) { SelPress = true; }
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    int countDown;
    /* Long press power off */
    if (launcherGpioRead(L_BTN) == BTN_ACT && launcherGpioRead(R_BTN) == BTN_ACT) {
        uint32_t time_count = launcherMillis();
        while (launcherGpioRead(L_BTN) == BTN_ACT && launcherGpioRead(R_BTN) == BTN_ACT) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                if (countDown < 4)
                    tft->drawCentreString("PWR OFF IN " + String(countDown) + "/3", tftWidth / 2, 12, 1);
                else {
                    tft->fillScreen(BGCOLOR);
                    while (launcherGpioRead(L_BTN) == BTN_ACT || launcherGpioRead(R_BTN) == BTN_ACT);
                    launcherDelayMs(200);
                    powerOff();
                }
                launcherDelayMs(10);
            }
        }

        // Clear text after releasing the button
        launcherDelayMs(30);
        tft->fillRect(60, 12, tftWidth - 60, 8, BGCOLOR);
    }
}
