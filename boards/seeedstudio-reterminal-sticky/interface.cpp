#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SPI.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <interface.h>

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

#define BTN_PREV 5    // vendor doc: "Up"
#define BTN_NEXT 6    // vendor doc: "Down"
#define BTN_SEL_PWR 4 // vendor doc: "AI / Power"

// --- power latch ---------------------------------------------------------
#define POWER_HOLD 45
#define POWER_LOCK 46

// --- e-paper power enable ----------------------------------------------
// Must be driven high before the panel is brought up.
#define EPD_EN 47

// --- microSD power enable -----------------------------------------------
#define SD_PWR_EN 10

// --- GT911 touch (I2C0) -------------------------------------------------
#define TOUCH_SDA 3
#define TOUCH_SCL 2
#define TOUCH_EN 42 // active HIGH, confirmed by the reference firmware
#define TOUCH_RST 41
#define TOUCH_INT 21
#define TOUCH_ADDR_L 0x5D // GT911 address selected by INT low during reset
#define TOUCH_ADDR_H 0x14 // GT911 address selected by INT high during reset
#define GT911_REG_ID 0x8140
#define GT911_REG_COORD_RES 0x8146
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINTS 0x8150
static uint8_t touchAddr = TOUCH_ADDR_H;

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
static uint16_t touchNativeWidth = TFT_WIDTH;
static uint16_t touchNativeHeight = TFT_HEIGHT;

static bool gt911WriteReg(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(touchAddr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool gt911ReadRegs(uint16_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(touchAddr);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((int)touchAddr, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
    return true;
}

static void gt911ResetForAddress(uint8_t addr) {
    // GT911 samples INT while RST rises: INT low selects 0x5D, INT high selects
    // 0x14. Timings follow Seeed's ESP-IDF peripheral demo.
    const int intLevel = (addr == TOUCH_ADDR_L) ? LOW : HIGH;

    gpio_hold_dis((gpio_num_t)TOUCH_RST);
    gpio_hold_dis((gpio_num_t)TOUCH_INT);
    launcherGpioOutput(TOUCH_RST);
    launcherGpioOutput(TOUCH_INT);
    launcherGpioWrite(TOUCH_RST, LOW);
    launcherGpioWrite(TOUCH_INT, intLevel);
    launcherDelayMs(20);
    launcherGpioWrite(TOUCH_RST, HIGH);
    launcherDelayMs(20);
    launcherGpioInput(TOUCH_INT);
    launcherDelayMs(80);
}

static bool gt911ProbeAddress(uint8_t addr) {
    touchAddr = addr;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) return false;

    uint8_t id[4] = {};
    if (!gt911ReadRegs(GT911_REG_ID, id, sizeof(id))) return false;
    return id[0] == '9' && id[1] == '1' && id[2] == '1';
}

static void gt911ReadResolution() {
    uint8_t buf[4] = {};
    if (!gt911ReadRegs(GT911_REG_COORD_RES, buf, sizeof(buf))) return;

    const uint16_t width = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    const uint16_t height = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    if (width != 0) touchNativeWidth = width;
    if (height != 0) touchNativeHeight = height;
}

static bool bringUpTouch() {
    static const uint8_t addrs[2] = {TOUCH_ADDR_H, TOUCH_ADDR_L};

    for (uint8_t addr : addrs) {
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            gt911ResetForAddress(addr);
            if (!gt911ProbeAddress(addr)) continue;
            touchAddr = addr;
            gt911ReadResolution();
            gt911WriteReg(GT911_REG_STATUS, 0);
            launcherConsolePrintf(
                "GT911 found at addr=0x%02X sensor=%ux%u\n", addr, touchNativeWidth, touchNativeHeight
            );
            return true;
        }
    }
    launcherConsolePrintf(
        "%s\n", String("Failed to find GT911 on either address - check your wiring!").c_str()
    );
    return false;
}

static uint16_t scaleTouchCoordinate(uint16_t value, uint16_t sourceMax, uint16_t targetMax) {
    if (sourceMax == 0 || targetMax == 0) return 0;
    if (value > sourceMax) value = sourceMax;
    return (uint16_t)(((uint32_t)value * targetMax + sourceMax / 2U) / sourceMax);
}

static uint8_t readTouchPoint(int16_t *x, int16_t *y) {
    uint8_t status = 0;
    if (!gt911ReadRegs(GT911_REG_STATUS, &status, 1)) return 0;
    if ((status & 0x80) == 0) return 0;

    const uint8_t count = status & 0x0F;
    if (count == 0 || count > 5) {
        gt911WriteReg(GT911_REG_STATUS, 0);
        return 0;
    }

    uint8_t point[8] = {};
    const bool readOk = gt911ReadRegs(GT911_REG_POINTS, point, sizeof(point));
    gt911WriteReg(GT911_REG_STATUS, 0);
    if (!readOk) return 0;

    const uint16_t rawX = (uint16_t)point[0] | ((uint16_t)point[1] << 8);
    const uint16_t rawY = (uint16_t)point[2] | ((uint16_t)point[3] << 8);

    if (rotation == 3) {
        *x = scaleTouchCoordinate(rawX, touchNativeWidth, TFT_HEIGHT - 1);
        *y = scaleTouchCoordinate(
            touchNativeHeight - (rawY > touchNativeHeight ? touchNativeHeight : rawY),
            touchNativeHeight,
            TFT_WIDTH - 1
        );
    } else if (rotation == 1) {
        *x = (TFT_HEIGHT - 1) - scaleTouchCoordinate(rawX, touchNativeWidth, TFT_HEIGHT - 1);
        *y = scaleTouchCoordinate(rawY, touchNativeHeight, TFT_WIDTH - 1);
    } else if (rotation == 0) {
        *x = scaleTouchCoordinate(rawY, touchNativeHeight, TFT_WIDTH - 1);
        *y = scaleTouchCoordinate(rawX, touchNativeWidth, TFT_HEIGHT - 1);
    } else {
        *x = (TFT_WIDTH - 1) - scaleTouchCoordinate(rawY, touchNativeHeight, TFT_WIDTH - 1);
        *y = (TFT_HEIGHT - 1) - scaleTouchCoordinate(rawX, touchNativeWidth, TFT_HEIGHT - 1);
    }
    return 1;
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

    // Powered up here, well ahead of the first setupSdCard() call later in
    // boot, so the confirmed 100ms settle time is already spent by then.
    launcherGpioOutput(TOUCH_EN);
    launcherGpioWrite(TOUCH_EN, HIGH);
    launcherGpioOutput(SD_PWR_EN);
    launcherGpioWrite(SD_PWR_EN, HIGH);
    launcherGpioOutput(EPD_EN);
    launcherGpioWrite(EPD_EN, HIGH);
    launcherGpioOutput(BAT_CHG_EN);
    launcherGpioWrite(BAT_CHG_EN, LOW); // Active low; left undriven the charger stays disabled.
    // Drive CS Pins High
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    // Setup Inputs
    launcherGpioInputPullup(BTN_PREV);
    launcherGpioInputPullup(BTN_NEXT);
    launcherGpioInputPullup(BTN_SEL_PWR);

    // Start SPI interface
    SPI.begin(TFT_SCLK, SDCARD_MISO, TFT_MOSI, TFT_CS);

    // Restart Wire on the pinouts
    Wire.end();
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    pinMode(TOUCH_SCL, INPUT_PULLUP);
    if (!Wire.begin(TOUCH_SDA, TOUCH_SCL)) launcherConsolePrintln("Fail Starting Wire");
    // The fuel gauge is on its own I2C bus (Wire1), separate from touch.
    pinMode(GAUGE_SDA, INPUT_PULLUP);
    pinMode(GAUGE_SCL, INPUT_PULLUP);
    if (!Wire1.begin(GAUGE_SDA, GAUGE_SCL, GAUGE_I2C_FREQ)) launcherConsolePrintln("Fail Starting Wire1");
    Wire1.setTimeOut(4);

    // Time to raise 3.3V rails on SDCard/TFT/Touch
    launcherDelayMs(250);
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
    static unsigned long pool_tm = launcherMillis();
    static unsigned long ready_tm = launcherMillis();

    if (launcherMillis() - tm <= 200 && !LongPress) return;

    int16_t tx = 0, ty = 0;
    uint8_t touched = 0;

    if (launcherMillis() - ready_tm > 5000 && !touchReady) {
        if (!Wire.begin(TOUCH_SDA, TOUCH_SCL)) launcherConsolePrintln("Fail Starting Wire");
        touchReady = bringUpTouch();
        ready_tm = launcherMillis();
    }

    if (launcherMillis() - pool_tm > 100) {
        touched = touchReady ? readTouchPoint(&tx, &ty) : 0;
        pool_tm = launcherMillis();
    }
    const bool prev = launcherGpioRead(BTN_PREV) == LOW;
    const bool next = launcherGpioRead(BTN_NEXT) == LOW;
    const bool sel = launcherGpioRead(BTN_SEL_PWR) == LOW;

    if (!prev && !next && !sel && !touched) return;

    tm = launcherMillis();
    AnyKeyPress = true;

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

    gpio_hold_dis((gpio_num_t)POWER_HOLD);
    launcherGpioWrite(POWER_HOLD, LOW);
    powerLockPulse();

    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_SEL_PWR, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
