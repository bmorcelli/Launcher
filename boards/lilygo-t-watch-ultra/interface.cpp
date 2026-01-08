#include "powerSave.h"
#define ARDUINO_T_WATCH_S3_ULTRA
// #include <LilyGoLib.h>
#include <interface.h>

#include <Wire.h>

// GPIO expander
#include <ExtensionIOXL9555.hpp>
ExtensionIOXL9555 io;

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
XPowersAXP2101 PPM;

#include "TouchDrvCSTXXX.hpp"
TouchDrvCST92xx touch;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    Serial.begin(115200);
    uint8_t csPin[4] = {4, 21, 36, 41}; // NFC,SDCard, LoRa, TFT
    for (auto pin : csPin) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    Wire.begin(SDA, SCL);
    if (io.begin(Wire, 0x20)) {
        const uint8_t expands[] = {
            EXPANDS_DISP_EN,
            EXPANDS_DRV_EN,
            EXPANDS_TOUCH_RST,
            EXPANDS_SD_DET,
        };
        for (auto pin : expands) {
            io.pinMode(pin, OUTPUT);
            io.digitalWrite(pin, HIGH);
            delay(1);
        }
    } else {
        Serial.println("Initializing expander failed");
    }
    bool pmu_ret = false;
    pmu_ret = PPM.init(Wire, SDA, SCL, AXP2101_SLAVE_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setChargeTargetVoltage(4208);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
    }
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    delay(20);
    io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    delay(60);
    touch.setPins(-1, TP_INT);
    bool result = touch.begin(Wire, 0x1A, SDA, SCL);
    if (result == false) { Serial.println("touch is not online..."); }
    Serial.print("Model :");
    Serial.println(touch.getModelName());

    touch.setCoverScreenCallback(
        [](void *ptr) {
            Serial.print(millis());
            Serial.println(" : The screen is covered");
        },
        NULL
    );
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // brightness
    // instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = PPM.getBatteryPercent();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // brightness
    // instance.setBrightness(brightval * 254 / 100);
}

struct TouchPointPro {
    int16_t x[5];
    int16_t y[5];
};
/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (millis() - tm > 200 || LongPress) {
        if (touch.isPressed()) {
            TouchPointPro t;
            touch.getPoint(&t.x[0], &t.y[0], 1);
            tm = millis();
            if (rotation == 1) { t.y[0] = TFT_WIDTH - t.y[0]; }
            if (rotation == 3) { t.x[0] = t.x[0]; }
            // Need to test these 2
            if (rotation == 0) {
                int tmp = t.x[0];
                t.x[0] = t.y[0];
                t.y[0] = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x[0];
                t.x[0] = TFT_WIDTH - t.y[0];
                t.y[0] = TFT_HEIGHT - tmp;
            }

            Serial.printf("\nPressed x=%d , y=%d, rot: %d", t.x[0], t.y[0], rotation);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = t.x[0];
            touchPoint.y = t.y[0];
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
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
