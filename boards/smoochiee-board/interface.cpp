#include "hal/device.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"

#define SEL_BTN 0
#define UP_BTN 41
#define DW_BTN 40
#define R_BTN 38
#define L_BTN 39
#define BTN_ACT LOW
#define CC1101_SS_PIN 46
#define NRF24_SS_PIN 14
#define GROVE_SDA 47
#define GROVE_SCL 48
#define SMOOCHIEE_BQ25896_ADDRESS 0x6B

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/

// Power handler for battery detection -- no separate fuel gauge on this
// board, battery% comes from the PMIC's own system voltage reading.
#include <Wire.h>

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

    Wire.begin(GROVE_SDA, GROVE_SCL);
    DevicePmic pmicCfg{GROVE_SDA, GROVE_SCL, SMOOCHIEE_BQ25896_ADDRESS};
    if (!hal_pmic_init(pmicCfg)) { launcherConsolePrintln("PMIC: Failed starting BQ25896"); }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100+
***************************************************************************************/
int getBattery() {
    uint8_t percent = (hal_pmic_get_system_voltage_mv() - 3300) * 100 / (float)(4150 - 3350);
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
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
