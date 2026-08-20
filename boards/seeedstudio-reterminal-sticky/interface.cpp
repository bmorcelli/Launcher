#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <TouchDrvGT911.hpp>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>

TouchDrvGT911 touch;

// Seeed reTerminal Sticky.
//
// Pins from the official hardware overview:
//   https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/
// Not tested on hardware here. The touch swap/mirror combo below is a first
// guess pending confirmation on real hardware — the doc gives pins, not
// orientation.
//
// The official page documents no software power-latch GPIO: power on/off is
// the ESP32-S3's own deep sleep, and the only physical power control besides
// the AI/Power button is a CHIP_PU reset button hole, which is not a GPIO.

// --- buttons -----------------------------------------------------------
// Plain digital, active low. GPIO4 is the "AI / Power" button in the vendor
// doc — this launcher repurposes it as Select, with a long hold to power off
// (see checkReboot()).
#define BTN_PREV 5    // vendor doc: "Up"
#define BTN_NEXT 6    // vendor doc: "Down"
#define BTN_SEL_PWR 4 // vendor doc: "AI / Power"

// --- e-paper power enable ----------------------------------------------
// Must be driven high before the panel is brought up.
#define EPD_EN 47

// --- GT911 touch (I2C0) -------------------------------------------------
#define TOUCH_SDA 3
#define TOUCH_SCL 2
#define TOUCH_EN 42 // driven high before touch.begin(); polarity unconfirmed
#define TOUCH_RST 41
#define TOUCH_INT 21
#define TOUCH_ADDR 0x5D // GT911 default I2C address

// --- BQ27220 fuel gauge (I2C1, its own bus — not the touch bus) --------
#define GAUGE_SDA 1
#define GAUGE_SCL 0
#define GAUGE_I2C_FREQ 400000
#define BQ27220_ADDR 0x55
#define BQ27220_SOC_REG 0x2C // StateOfCharge(), percent, 16-bit little endian

static bool touchReady = false;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInputPullup(BTN_PREV);
    launcherGpioInputPullup(BTN_NEXT);
    launcherGpioInputPullup(BTN_SEL_PWR);

    launcherGpioOutput(EPD_EN);
    launcherGpioWrite(EPD_EN, HIGH);

    // The e-paper panel and the SD card share one SPI bus: park both chip
    // selects high before it comes up, so neither answers while the other is
    // being addressed. GXEPD2_BEGIN_SPI is not set for this board, so the
    // DisplayDrivers backend expects SPI already running.
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);

    // The fuel gauge is on its own I2C bus (Wire1), separate from touch.
    Wire1.begin(GAUGE_SDA, GAUGE_SCL, GAUGE_I2C_FREQ);
    Wire1.setTimeOut(4);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    launcherGpioOutput(TOUCH_EN);
    launcherGpioWrite(TOUCH_EN, HIGH);
    launcherDelayMs(20);

    touch.setPins(TOUCH_RST, TOUCH_INT);
    touchReady = touch.begin(Wire, TOUCH_ADDR, TOUCH_SDA, TOUCH_SCL);
    if (!touchReady) {
        launcherConsolePrintf("%s\n", String("Failed to find GT911 - check your wiring!").c_str());
    }
    // Swap/mirror per rotation is set in InputHandler(), same as xteink-x4pro
    // (same 800x480 native panel geometry).
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    // The launcher asks on every header redraw; cache it and keep the last
    // good reading on an I2C error rather than blink to 0.
    static int cached = 0;
    static unsigned long lastPoll = 0;
    const unsigned long now = launcherMillis();
    if (lastPoll != 0 && (now - lastPoll) < 1500) return cached;
    lastPoll = now;

    Wire1.beginTransmission(BQ27220_ADDR);
    Wire1.write(BQ27220_SOC_REG);
    if (Wire1.endTransmission(false) != 0) return cached;
    if (Wire1.requestFrom(BQ27220_ADDR, 2) < 2) return cached;

    const uint8_t lo = Wire1.read();
    const uint8_t hi = Wire1.read();
    const uint16_t soc = ((uint16_t)hi << 8) | lo;
    cached = soc > 100 ? 100 : (int)soc;
    return cached;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    // No backlight and no frontlight on this panel.
    (void)brightval;
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = launcherMillis();

    // Panel is native TFT_WIDTH x TFT_HEIGHT (800x480) landscape; re-map the
    // touch axes to whichever of the four rotations is currently active, so
    // touch coordinates line up with what is drawn. Same pattern as
    // lilygo-t5-epaper-s3-pro and xteink-x4pro.
    static uint8_t lastRot = 5;
    if (touchReady && lastRot != rotation) {
        if (rotation == 1) {
            touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
            touch.setSwapXY(true);
            touch.setMirrorXY(false, true);
        } else if (rotation == 3) {
            touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
            touch.setSwapXY(true);
            touch.setMirrorXY(true, false);
        } else if (rotation == 0) {
            touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
            touch.setSwapXY(false);
            touch.setMirrorXY(false, false);
        } else if (rotation == 2) {
            touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
            touch.setSwapXY(false);
            touch.setMirrorXY(true, true);
        }
        lastRot = rotation;
    }

    int16_t tx = 0, ty = 0;
    const uint8_t touched = touchReady ? touch.getPoint(&tx, &ty, 1) : 0;

    if (launcherMillis() - tm > 200 || LongPress) {
    } else return;

    const bool prev = launcherGpioRead(BTN_PREV) == LOW;
    const bool next = launcherGpioRead(BTN_NEXT) == LOW;
    const bool sel = launcherGpioRead(BTN_SEL_PWR) == LOW;

    if (!prev && !next && !sel && !touched) return;

    tm = launcherMillis();
    if (!wakeUpScreen()) AnyKeyPress = true;
    else return;

    if (prev) PrevPress = true;
    if (next) NextPress = true;
    if (sel) SelPress = true;

    if (touched) {
        touchPoint.x = tx;
        touchPoint.y = ty;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    while (launcherGpioRead(BTN_SEL_PWR) == LOW) launcherDelayMs(50);
    launcherDelayMs(100);

    tft->fillScreen(BGCOLOR);
    tft->setTextSize(1);
    tft->setTextColor(FGCOLOR);
    tft->drawCentreString("Powered OFF", tftWidth / 2, tftHeight / 2, 1);
    tft->display();

    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_SEL_PWR, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    if (launcherGpioRead(BTN_SEL_PWR) != LOW) return;

    const uint32_t start = launcherMillis();
    int lastCountDown = -1;
    while (launcherGpioRead(BTN_SEL_PWR) == LOW) {
        if (launcherMillis() - start > 500) {
            const int countDown = (launcherMillis() - start) / 1000 + 1;
            if (countDown < 3) {
                // One refresh per second at most: this panel cannot repaint
                // per frame.
                if (countDown != lastCountDown) {
                    lastCountDown = countDown;
                    tft->setTextSize(1);
                    tft->setTextColor(FGCOLOR, BGCOLOR);
                    tft->drawCentreString("PWR OFF IN " + String(countDown) + "/2", tftWidth / 2, 12, 1);
                    tft->display();
                }
            } else {
                tft->fillScreen(BGCOLOR);
                tft->display();
                powerOff();
            }
        }
        launcherDelayMs(10);
    }

    if (lastCountDown >= 0) {
        tft->fillRect(0, 12, tftWidth, LH, BGCOLOR);
        tft->display();
    }
}
