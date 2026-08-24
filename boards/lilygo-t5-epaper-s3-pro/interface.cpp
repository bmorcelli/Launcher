#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "hal/power/gauge.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

static bool touch_OK = false;
static int8_t touchRstPin = -1;
static int8_t touchIrqPin = -1;

bool isH752_1 = false;

#define T5EPAPER_BQ25896_ADDRESS 0x6B

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

// ED047TC2: 960x540 native resolution. displayConfig.width/height (TFT_WIDTH/
// TFT_HEIGHT = 540/960) swapped per rotation by hal_touch_read() already
// reproduces the 960x540 vs 540x960 setMaxCoordinates() split the original
// code did by hand; this table replicates the per-rotation swap/mirror the
// old InputHandler() applied (see docs/etapa_7.md).
static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = touchRstPin;
    cfg.pin_irq = touchIrqPin;
    cfg.i2c_bus = activeI2c();
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

bool startPeripherals(uint8_t touchAddress, int8_t rst, int8_t irq) {
    TwoWire *wire = activeI2c();
    if (wire == nullptr) {
        launcherConsolePrintf("%s\n", String("EPD_Painter I2C bus is not available").c_str());
        return false;
    }

    launcherGpioOutput(irq);
    launcherGpioWrite(irq, HIGH);
    touchRstPin = rst;
    touchIrqPin = irq;
    touch_OK = hal_touch_init(touchCfg(), touchAddress);
    if (!touch_OK) {
        while (1) {
            launcherConsolePrintf("%s\n", String("Failed to find GT911 - check your wiring!").c_str());
            launcherDelayMs(1000);
        }
    }

    launcherConsolePrintf("%s\n", String("Started Touchscreen poll...").c_str());

    // BQ25896 --- 0x6B
    wire->beginTransmission(T5EPAPER_BQ25896_ADDRESS);
    if (wire->endTransmission() == 0) {
        // Reuse the EPD_Painter I2C bus through callbacks so XPowers does not
        // call TwoWire::begin() again on an already-initialized bus.
        if (!hal_pmic_init_via_callbacks(T5EPAPER_BQ25896_ADDRESS, pmicReadReg, pmicWriteReg)) {
            launcherConsolePrintf("%s\n", String("Failed to initialize XPowers PPM").c_str());
            return false;
        }
        hal_gauge_init(DeviceGauge{});
    }

    return true;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    // Driving this parallel EPD is bit-banged and timing sensitive, so the
    // input task has to stay out of the way while the panel is being painted.
    // A pointer, because xHandle is cleared at runtime when the task goes away.
    tft->setPaintGuard(&xHandle);

    // CS pins of SPI devices to HIGH
    launcherGpioOutput(46); // LORA module
    launcherGpioWrite(46, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
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
int getBattery() { return hal_gauge_get_percent(); }

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

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    if (!touch_OK) return; // dont have touchscreen
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;
    LTouchPoint t;
    if (hal_touch_read(touchCfg(), t)) {
        tm = launcherMillis();
        hal_touch_apply(t);
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
    tft->setTextSize(FG);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight - 100, 1);
    tft->display();
    launcherDelayMs(1000);
    hal_pmic_shutdown();
    while (1) launcherDelayMs(100);
}
