#include "powerSave.h"
#include <TouchDrvGT911.hpp>
#include <Wire.h>
#include <interface.h>

#include "epd_driver.h"
#include "utilities.h"
TouchDrvGT911 touch;

#include <bq27220.h>
BQ27220 bq;

#include <XPowersLib.h>
XPowersPPM PPM;

#define BOARD_I2C_SDA 6
#define BOARD_I2C_SCL 5
#define BOARD_SENSOR_IRQ 15
#define BOARD_TOUCH_RST 41

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    pinMode(SEL_BTN, INPUT);
    pinMode(DW_BTN, INPUT);

    // CS pins of SPI devices to HIGH
    pinMode(46, OUTPUT); // LORA module
    digitalWrite(46, HIGH);

    Wire.begin(BOARD_SDA, BOARD_SCL);

    // Assuming that the previous touch was in sleep state, wake it up
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, HIGH);

    /*
     * The touch reset pin uses hardware pull-up,
     * and the function of setting the I2C device address cannot be used.
     * Use scanning to obtain the touch device address.*/
    uint8_t touchAddress = 0;
    Wire.beginTransmission(0x14);
    if (Wire.endTransmission() == 0) { touchAddress = 0x14; }
    Wire.beginTransmission(0x5D);
    if (Wire.endTransmission() == 0) { touchAddress = 0x5D; }
    if (touchAddress == 0) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            delay(1000);
        }
    }
    touch.setPins(-1, TOUCH_INT);
    if (!touch.begin(Wire, touchAddress, BOARD_SDA, BOARD_SCL)) {
        while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
            delay(1000);
        }
    }
    touch.setMaxCoordinates(EPD_WIDTH, EPD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(false, true);

    Serial.println("Started Touchscreen poll...");

    // BQ25896 --- 0x6B
    Wire.beginTransmission(0x6B);
    if (Wire.endTransmission() == 0) {
        // battery_25896.begin();
        PPM.init(Wire, BOARD_SDA, BOARD_SCL, BQ25896_SLAVE_ADDRESS);
        // Set the minimum operating voltage. Below this voltage, the PPM will protect
        PPM.setSysPowerDownVoltage(3300);
        // Set input current limit, default is 500mA
        PPM.setInputCurrentLimit(3250);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        // Disable current limit pin
        PPM.disableCurrentLimitPin();
        // Set the charging target voltage, Range:3840 ~ 4608mV ,step:16 mV
        PPM.setChargeTargetVoltage(4208);
        // Set the precharge current , Range: 64mA ~ 1024mA ,step:64mA
        PPM.setPrechargeCurr(64);
        // The premise is that Limit Pin is disabled, or it will only follow the maximum charging current set
        // by Limi tPin. Set the charging current , Range:0~5056mA ,step:64mA
        PPM.setChargerConstantCurr(832);
        // Get the set charging current
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableADCMeasure();
        PPM.enableCharge();
        PPM.disableOTG();
    }
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000
#define TFT_BL 40
void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 125);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = bq.getChargePcnt();

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
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
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        Serial.println("Failed to set brightness");
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

struct TouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long _tmptmp;
    TouchPointPro t;
    uint8_t touched = 0;
    touched = touch.getPoint(&t.x, &t.y);
    if ((millis() - _tmptmp) > 150 || LongPress) { // one reading each 500ms

        // Serial.printf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
        if (touched) {

            Serial.printf(
                "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d", t.x, t.y, rotation, millis(), _tmptmp
            );
            _tmptmp = millis();

            // if(!wakeUpScreen()) AnyKeyPress = true;
            // else goto END;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
        }
    END:
        yield();
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
