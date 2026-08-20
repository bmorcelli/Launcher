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
// Cross-checked against Lukilyy's reterminal-sticky-2048-eink-game, which is
// confirmed working on real hardware:
//   https://github.com/Lukilyy/reterminal-sticky-2048-eink-game
// That project's boot sequence (power_on_hold(), StickyTouch::init()) is what
// POWER_HOLD/POWER_LOCK and the touch EN polarity/timing below are taken
// from. The vendor doc never mentions the power-latch pins at all -- without
// them the board's power rails are not reliably held up, which plausibly
// explains touch/SD/button symptoms that looked unrelated.

// --- buttons -----------------------------------------------------------
// Plain digital, active low. GPIO4 is the "AI / Power" button in the vendor
// doc — this launcher repurposes it as Select, with a long hold to power off
// (see checkReboot()).
#define BTN_PREV 5    // vendor doc: "Up"
#define BTN_NEXT 6    // vendor doc: "Down"
#define BTN_SEL_PWR 4 // vendor doc: "AI / Power"

// --- power latch ---------------------------------------------------------
#define POWER_HOLD 45
#define POWER_LOCK 46

// --- e-paper power enable ----------------------------------------------
// Must be driven high before the panel is brought up.
#define EPD_EN 47

// --- GT911 touch (I2C0) -------------------------------------------------
#define TOUCH_SDA 3
#define TOUCH_SCL 2
#define TOUCH_EN 42 // active HIGH, confirmed by the reference firmware
#define TOUCH_RST 41
#define TOUCH_INT 21
#define TOUCH_ADDR_L 0x5D // GT911 default I2C address
#define TOUCH_ADDR_H 0x14 // GT911 alternate address (INT held high at reset)
static uint8_t touchAddr = TOUCH_ADDR_L;

// --- BQ27220 fuel gauge (I2C1, its own bus — not the touch bus) --------
#define GAUGE_SDA 1
#define GAUGE_SCL 0
#define GAUGE_I2C_FREQ 400000
#define BQ27220_ADDR 0x55
#define BQ27220_SOC_REG 0x2C // StateOfCharge(), percent, 16-bit little endian

// --- BQ25616 charger enable ---------------------------------------------
// Active low ("EN_BAT_CHGn" in the reference firmware's pin map); left
// undriven the charger stays disabled.
#define BAT_CHG_EN 39

static bool touchReady = false;

static bool bringUpTouch() {
    static const uint8_t addrs[2] = {TOUCH_ADDR_L, TOUCH_ADDR_H};

    launcherGpioOutput(TOUCH_EN);
    launcherGpioWrite(TOUCH_EN, HIGH);
    launcherDelayMs(250);

    for (uint8_t addr : addrs) {
        touch.setPins(TOUCH_RST, TOUCH_INT);
        if (touch.begin(Wire, addr, TOUCH_SDA, TOUCH_SCL)) {
            touchAddr = addr;
            launcherConsolePrintf("GT911 found at addr=0x%02X\n", addr);
            return true;
        }
    }
    launcherConsolePrintf(
        "%s\n", String("Failed to find GT911 on either address - check your wiring!").c_str()
    );
    return false;
}

/***************************************************************************************
** Function name: powerOnHold() / powerLockPulse()
***************************************************************************************/
static void powerLockPulse() {
    launcherGpioWrite(POWER_LOCK, LOW);
    delayMicroseconds(10);
    launcherGpioWrite(POWER_LOCK, HIGH);
    delayMicroseconds(10);
    launcherGpioWrite(POWER_LOCK, LOW);
}

static void powerOnHold() {
    launcherGpioOutput(POWER_HOLD);
    launcherGpioOutput(POWER_LOCK);
    gpio_hold_dis((gpio_num_t)POWER_HOLD);
    launcherGpioWrite(POWER_HOLD, HIGH);
    powerLockPulse();
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {

    powerOnHold();

    launcherGpioInputPullup(BTN_PREV);
    launcherGpioInputPullup(BTN_NEXT);
    launcherGpioInputPullup(BTN_SEL_PWR);

    launcherGpioOutput(EPD_EN);
    launcherGpioWrite(EPD_EN, HIGH);

    // Active low; left undriven the charger stays disabled.
    launcherGpioOutput(BAT_CHG_EN);
    launcherGpioWrite(BAT_CHG_EN, LOW);

    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);

    pinMode(TOUCH_SDA, INPUT_PULLUP);
    pinMode(TOUCH_SCL, INPUT_PULLUP);
    pinMode(GAUGE_SDA, INPUT_PULLUP);
    pinMode(GAUGE_SCL, INPUT_PULLUP);

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
    touchReady = bringUpTouch();
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

    // GT911 on this board reports touches pre-transformed by its own factory
    // config into a fixed 800x480 frame (confirmed by the reference firmware
    // reading back "sensor=800x480" from the chip) -- NOT the panel's raw
    // pixel grid, and NOT a simple axis swap of it. At the default rotation
    // (3, portrait 480x800) the reference firmware needs no XY swap at all,
    // only a proportional rescale (800->480 / 480->800) plus a Y-axis flip;
    // that combination is reproduced exactly below and is hardware-confirmed.
    // Rotations 0/1/2 follow the same 90-degree-step pattern used on the
    // other e-paper boards, composed on top of that confirmed anchor, but are
    // NOT hardware-tested -- revisit if touch is misaligned in a non-default
    // rotation.
    static uint8_t lastRot = 5;
    if (touchReady && lastRot != rotation) {
        // setResolution() feeds the scale-factor math, and swapXY is applied
        // *before* scaling -- so when swapXY is on, the raw resolution has to
        // be declared already-swapped too, or the scale factors get matched
        // to the wrong axis.
        if (rotation == 3) {
            touch.setResolution(TFT_WIDTH, TFT_HEIGHT);
            touch.setTargetResolution(TFT_HEIGHT, TFT_WIDTH);
            touch.setSwapXY(false);
            touch.setMirrorXY(false, true);
        } else if (rotation == 1) {
            touch.setResolution(TFT_WIDTH, TFT_HEIGHT);
            touch.setTargetResolution(TFT_HEIGHT, TFT_WIDTH);
            touch.setSwapXY(false);
            touch.setMirrorXY(true, false);
        } else if (rotation == 0) {
            touch.setResolution(TFT_HEIGHT, TFT_WIDTH);
            touch.setTargetResolution(TFT_WIDTH, TFT_HEIGHT);
            touch.setSwapXY(true);
            touch.setMirrorXY(false, false);
        } else if (rotation == 2) {
            touch.setResolution(TFT_HEIGHT, TFT_WIDTH);
            touch.setTargetResolution(TFT_WIDTH, TFT_HEIGHT);
            touch.setSwapXY(true);
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

    // Drop the latch: on this board that is what actually cuts power, per
    // the reference firmware's power_off(). The deep-sleep call below is a
    // fallback in case the hardware stays alive on USB power.
    gpio_hold_dis((gpio_num_t)POWER_HOLD);
    launcherGpioWrite(POWER_HOLD, LOW);
    powerLockPulse();

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
