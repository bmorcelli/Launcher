#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

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
    launcherConsoleBegin(115200);
    uint8_t csPin[4] = {4, 21, 36, 41}; // NFC,SDCard, LoRa, TFT
    for (auto pin : csPin) {
        launcherGpioOutput(pin);
        launcherGpioWrite(pin, HIGH);
    }
    Wire.begin(SDA, SCL);
    bool pmu_ret = false;
    pmu_ret = PPM.init(Wire, SDA, SCL, AXP2101_SLAVE_ADDRESS);
    if (pmu_ret) {
        PPM.setSysPowerDownVoltage(3300);
        PPM.setChargeTargetVoltage(4208);
        PPM.setChargerConstantCurr(832);
        PPM.getChargerConstantCurr();
        PPM.setALDO1Voltage(3300); // SD Card
        PPM.enableALDO1();
        PPM.setALDO2Voltage(3300); // Display
        PPM.enableALDO2();
        PPM.setALDO4Voltage(3300); // Sensor
        PPM.enableALDO4();

        launcherConsolePrintf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
    }
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
            launcherDelayMs(1);
        }
    } else {
        launcherConsolePrintf("%s\n", String("Initializing expander failed").c_str());
    }
    io.digitalWrite(EXPANDS_TOUCH_RST, LOW);
    launcherDelayMs(20);
    io.digitalWrite(EXPANDS_TOUCH_RST, HIGH);
    launcherDelayMs(60);
    touch.setPins(-1, TP_INT);
    bool result = touch.begin(Wire, 0x1A, SDA, SCL);
    if (result == false) { launcherConsolePrintf("%s\n", String("touch is not online...").c_str()); }
    launcherConsolePrintf("%s", String("Model :").c_str());
    launcherConsolePrintf("%s\n", String(touch.getModelName()).c_str());

    touch.setCoverScreenCallback(
        [](void *ptr) {
            launcherConsolePrintf("%s", String(launcherMillis()).c_str());
            launcherConsolePrintf("%s\n", String(" : The screen is covered").c_str());
        },
        NULL
    );
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
void _setBrightness(uint8_t brightval) { tft->_outputDriver.setBrightness(brightval * 254 / 100); }

struct LTouchPointPro {
    int16_t x;
    int16_t y;
};
/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        if (touch.isPressed()) {
            LTouchPointPro t;
            touch.getPoint(&t.x, &t.y, 1);
            tm = launcherMillis();
            if (rotation == 1) { t.y = TFT_WIDTH - t.y; }
            if (rotation == 3) { t.x = t.x; }
            // Need to test these 2
            if (rotation == 0) {
                int tmp = t.x;
                t.x = t.y;
                t.y = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x;
                t.x = TFT_WIDTH - t.y;
                t.y = TFT_HEIGHT - tmp;
            }

            launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d", t.x, t.y, rotation);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = t.x;
            touchPoint.y = t.y;
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
void powerOff() {
    const uint8_t expands[] = {
        EXPANDS_DISP_EN,
        EXPANDS_DRV_EN,
        EXPANDS_TOUCH_RST,
        EXPANDS_SD_DET,
    };
    for (auto pin : expands) {
        io.digitalWrite(pin, LOW);
        launcherDelayMs(1);
    }
    PPM.shutdown();

    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
