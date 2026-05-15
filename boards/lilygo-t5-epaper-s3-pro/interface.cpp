#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <TouchDrvGT911.hpp>
#include <Wire.h>
#include <interface.h>

TouchDrvGT911 touch;

#include <bq27220.h>
BQ27220 bq;

#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
XPowersPPM PPM;

bool isH752_1 = false;

#define BOARD_I2C_SDA 6
#define BOARD_I2C_SCL 5
#define BOARD_SENSOR_IRQ 15
#define BOARD_TOUCH_RST 41

// Aliases matching what utilities.h (LilyGo-EPD47) used to provide
#define BOARD_SDA BOARD_I2C_SDA
#define BOARD_SCL BOARD_I2C_SCL
#define TOUCH_INT 15 // GT911 interrupt pin on T5 S3 E-Paper Pro H752

namespace {
TwoWire *activeI2c() {
    if (tft == nullptr) return nullptr;
    EPD_Painter::Config cfg = tft->getConfig();
    return cfg.i2c.wire;
}

int pmicReadReg(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    TwoWire *wire = activeI2c();
    if (wire == nullptr) return -1;

    wire->beginTransmission(devAddr);
    wire->write(regAddr);
    if (wire->endTransmission() != 0) return -1;

    const size_t read = wire->requestFrom((int)devAddr, (int)len);
    if (read != len) return -1;

    for (uint8_t i = 0; i < len; ++i) {
        if (!wire->available()) return -1;
        data[i] = wire->read();
    }
    return 0;
}

int pmicWriteReg(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len) {
    TwoWire *wire = activeI2c();
    if (wire == nullptr) return -1;

    wire->beginTransmission(devAddr);
    wire->write(regAddr);
    wire->write(data, len);
    return wire->endTransmission() == 0 ? 0 : -1;
}
} // namespace

bool startPeripherals(uint8_t touchAddress, int8_t rst, int8_t irq) {
    TwoWire *wire = activeI2c();
    if (wire == nullptr) {
        launcherConsolePrintf("%s\n", String("EPD_Painter I2C bus is not available").c_str());
        return false;
    }

    EPD_Painter::Config cfg = tft->getConfig();

    launcherGpioOutput(irq);
    launcherGpioWrite(irq, HIGH);
    touch.setPins(rst, irq);
    if (!touch.begin(*wire, touchAddress, cfg.i2c.sda, cfg.i2c.scl)) {
        while (1) {
            launcherConsolePrintf("%s\n", String("Failed to find GT911 - check your wiring!").c_str());
            launcherDelayMs(1000);
        }
    }
    touch.setMaxCoordinates(960, 540); // ED047TC2: 960x540 native resolution
    touch.setSwapXY(true);
    touch.setMirrorXY(false, true);

    launcherConsolePrintf("%s\n", String("Started Touchscreen poll...").c_str());

    // BQ25896 --- 0x6B
    wire->beginTransmission(0x6B);
    if (wire->endTransmission() == 0) {
        // Reuse the EPD_Painter I2C bus through callbacks so XPowers does not
        // call TwoWire::begin() again on an already-initialized bus.
        if (!PPM.begin(BQ25896_SLAVE_ADDRESS, pmicReadReg, pmicWriteReg)) {
            launcherConsolePrintf("%s\n", String("Failed to initialize XPowers PPM").c_str());
            return false;
        }
        // Set the minimum operating voltage. Below this voltage, the PPM will protect
        PPM.setSysPowerDownVoltage(3300);
        // Set input current limit, default is 500mA
        PPM.setInputCurrentLimit(3250);
        launcherConsolePrintf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
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
        launcherConsolePrintf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableMeasure();
        PPM.enableCharge();
        PPM.disableOTG();
    }

    return true;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(DW_BTN);

    // CS pins of SPI devices to HIGH
    launcherGpioOutput(46); // LORA module
    launcherGpioWrite(46, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000
void _post_setup_gpio() {
    uint8_t touchAddress = 0x5D; // GT911 default I2C address
    EPD_Painter::Config cfg = tft->getConfig();
    if (cfg.i2c.sda == 6) startPeripherals(touchAddress, 41, 15);
    else {
        isH752_1 = true;
        _cs = 12;
        _miso = 21;
        _mosi = 13;
        _sck = 14;
        startPeripherals(touchAddress, 9, 3);
    }

    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(isH752_1 ? 11 : 40, OUTPUT);
    ledcAttach(isH752_1 ? 11 : 40, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(isH752_1 ? 11 : 40, bright);
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

    // log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(isH752_1 ? 11 : 40, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(isH752_1 ? 11 : 40);
        ledcAttach(isH752_1 ? 11 : 40, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(isH752_1 ? 11 : 40, dutyCycle);
    }
}

struct LTouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long _tmptmp = 0;
    LTouchPointPro t;
    uint8_t touched = 0;
    uint8_t rot = 5;
    if (rot != rotation) {
        if (rotation == 1) {
            touch.setMaxCoordinates(960, 540);
            touch.setSwapXY(true);
            touch.setMirrorXY(false, true);
        }
        if (rotation == 3) {
            touch.setMaxCoordinates(960, 540);
            touch.setSwapXY(true);
            touch.setMirrorXY(true, false);
        }
        if (rotation == 0) {
            touch.setMaxCoordinates(540, 960);
            touch.setSwapXY(false);
            touch.setMirrorXY(false, false);
        }
        if (rotation == 2) {
            touch.setMaxCoordinates(540, 960);
            touch.setSwapXY(false);
            touch.setMirrorXY(true, true);
        }
        rot = rotation;
    }
    touched = touch.getPoint(&t.x, &t.y, 1);
    if ((launcherMillis() - _tmptmp) > 250 || LongPress) { // one reading each 500ms

        // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
        if (touched) {

            // launcherConsolePrintf(
            //     "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d", t.x, t.y, rotation, launcherMillis(),
            //     _tmptmp
            // );
            _tmptmp = launcherMillis();

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    tft->fillScreen(BGCOLOR);
    initDisplay(true);
    launcherDelayMs(1000);
    PPM.shutdown();
    while (1) launcherDelayMs(100);
}
