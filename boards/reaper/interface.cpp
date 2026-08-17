// Main I2C Bus
#define SEL_BTN 0
#define UP_BTN 41
#define DW_BTN 40
#define R_BTN 38
#define L_BTN 39
#define ESC_BTN 21
#define BTN_ACT LOW

#define GROVE_SDA 47
#define GROVE_SCL 48
#define CC1101_GDO0_PIN 46
#define CC1101_SS_PIN 9

#define NRF24_SS_PIN 13
#define LORA_CS 4

#define XPOWERS_CHIP_BQ25896
#define USE_BQ27220_VIA_I2C
#define BQ27220_I2C_ADDRESS 0x55
#ifdef BQ27220_I2C_SDA
#undef BQ27220_I2C_SDA
#endif
#ifdef BQ27220_I2C_SCL
#undef BQ27220_I2C_SCL
#endif
#define BQ27220_I2C_SDA GROVE_SDA
#define BQ27220_I2C_SCL GROVE_SCL

// IO EXPANDER
#define USE_IO_EXPANDER
#define IO_EXPANDER_AW9523
#define IO_EXP_GPS 5
#define IO_EXP_VIBRO 15
#define IO_EXP_CC_RX 9
#define IO_EXP_CC_TX 10
#define IO_EXP_LOGO 0

#include "powerSave.h"
#include <bq27220.h>
#include <globals.h>
#include <interface.h>

#include <Wire.h>
// Power handler for battery detection
#include <Wire.h>
#include <XPowersLib.h>
// Charger chip

XPowersPPM PPM;
/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/

// BATTERY GAUGE
#define BATTERY_DESIGN_CAPACITY 1000
#include <bq27220.h>
BQ27220 bq;

void _setup_gpio() {

    pinMode(UP_BTN, INPUT_PULLUP); // Sets the power btn as an INPUT
    pinMode(SEL_BTN, INPUT_PULLUP);
    pinMode(DW_BTN, INPUT_PULLUP);
    pinMode(R_BTN, INPUT_PULLUP);
    pinMode(L_BTN, INPUT_PULLUP);
    pinMode(ESC_BTN, INPUT_PULLUP);

    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);
    pinMode(SS, OUTPUT); /// NFC PIN
    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);
    digitalWrite(SS, HIGH);

    // Starts SPI instance for CC1101 and NRF24 with CS pins blocking communication at start

    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    Wire.setPins(GROVE_SDA, GROVE_SCL);
    // Wire.begin();
    // bruceConfig.rfModule = CC1101_SPI_MODULE;
    // bruceConfig.irRx = RXLED;
    // bruceConfig.irTx = LED;
    Wire.begin(GROVE_SDA, GROVE_SCL);

    bool pmu_ret = false;
    pmu_ret = PPM.init(Wire, GROVE_SDA, GROVE_SCL, BQ25896_SLAVE_ADDRESS);
    if (pmu_ret) {

        PPM.setSysPowerDownVoltage(3300);
        PPM.setInputCurrentLimit(2000);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure(PowersBQ25896::CONTINUOUS);

        PPM.disableOTG();
        // PPM.enableInputDetection();
        PPM.enableCharge();
    }
    if (bq.getDesignCap() != BATTERY_DESIGN_CAPACITY) { bq.setDesignCap(BATTERY_DESIGN_CAPACITY); }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp

** Description:   Delivers the battery value from 1-100+
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = bq.getChargePcnt();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

bool isCharging() {
    return bq.getIsCharging(); // Return the charging status from BQ27220
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (millis() - tm < 200 && !LongPress) return;
    bool _u = digitalRead(UP_BTN) == BTN_ACT;
    bool _d = digitalRead(DW_BTN) == BTN_ACT;
    bool _l = digitalRead(L_BTN) == BTN_ACT;
    bool _r = digitalRead(R_BTN) == BTN_ACT;
    bool _s = digitalRead(SEL_BTN) == BTN_ACT;
    bool _e = digitalRead(ESC_BTN) == BTN_ACT;

    if (_s || _u || _d || _r || _l || _e) {
        tm = millis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    if (_l) { PrevPress = true; }
    if (_r) { NextPress = true; }
    if (_u) { UpPress = true; }
    if (_d) { DownPress = true; }
    if (_s) { SelPress = true; }
    if (_e) { EscPress = true; }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() { PPM.shutdown(); }

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    int countDown;
    /* Long press power off */
    if (digitalRead(ESC_BTN) == BTN_ACT) {
        uint32_t time_count = millis();
        while (digitalRead(ESC_BTN) == BTN_ACT) {
            // Display poweroff bar only if holding button
            if (millis() - time_count > 500) {
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (millis() - time_count) / 1000 + 1;
                if (countDown < 3)
                    tft->drawCentreString("PWR OFF IN " + String(countDown) + "/2", tftWidth / 2, 12, 1);
                else {
                    tft->fillScreen(BGCOLOR);
                    while (digitalRead(ESC_BTN) == BTN_ACT);
                    delay(200);
                    powerOff();
                }
                delay(10);
            }
        }

        // Clear text after releasing the button
        delay(30);
        tft->fillRect(60, 12, tftWidth - 60, LH, BGCOLOR);
    }
}
