#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
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
#define TOUCH_ADDR_H                                                                                         \
    0x14 // GT911_SLAVE_ADDRESS_H -- hal_touch_init() falls
         // back to 0x5D on its own if this one doesn't answer

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

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = TOUCH_RST;
    cfg.pin_irq = TOUCH_INT;
    cfg.gt911_int_sync = true;
    return cfg;
}

// Prev/Next/Sel -- Prev+Next held together also raises Esc (hal_buttons_poll_3
// standard combo; this board never had an Esc source of its own before).
static DeviceButtons buttonsCfg() { return DeviceButtons{BTN_PREV, BTN_NEXT, BTN_SEL_PWR}; }

// hal_touch_init()'s GT911 reset/probe/chip-ID sequence and register map
// (0x8140/0x8146/0x814E/0x814F) are register-identical to the hand-rolled
// driver this replaced -- confirmed against lib/SensorLib/src/touch/
// TouchDrvGT911.cpp before migrating, not just assumed. Coordinates are
// still read via hal_touch_read_raw() and rescaled by hand below rather than
// through hal_touch_read()'s cfg.SwapXY/setTargetResolution() path: the
// panel's native touch resolution isn't guaranteed to match its pixel
// dimensions on this board, and SensorLib's setSwapXY()+setTargetResolution()
// combination scales the post-swap axis using the pre-swap axis's native
// resolution (see TouchDrvInterface::updateXY()), which is wrong on a
// non-square native resolution -- the original per-rotation math here
// avoids that entirely by scaling before ever mixing axes.
static bool bringUpTouch() {
    if (!hal_touch_init(touchCfg(), TOUCH_ADDR_H)) {
        launcherConsolePrintf(
            "%s\n", String("Failed to find GT911 on either address - check your wiring!").c_str()
        );
        return false;
    }
    uint16_t w = 0, h = 0;
    if (hal_touch_get_resolution(w, h)) {
        touchNativeWidth = w;
        touchNativeHeight = h;
    }
    launcherConsolePrintf("GT911 found, sensor=%ux%u\n", touchNativeWidth, touchNativeHeight);
    return true;
}

static uint16_t scaleTouchCoordinate(uint16_t value, uint16_t sourceMax, uint16_t targetMax) {
    if (sourceMax == 0 || targetMax == 0) return 0;
    if (value > sourceMax) value = sourceMax;
    return (uint16_t)(((uint32_t)value * targetMax + sourceMax / 2U) / sourceMax);
}

static uint8_t readTouchPoint(int16_t *x, int16_t *y) {
    LTouchPoint raw;
    if (!hal_touch_read_raw(raw)) return 0;

    // The sensor is mounted portrait (480x800). Map the raw point into the
    // portrait frame (rotation 3) first, then turn it into the other three
    // rotations with the same 90-degree steps GxEPD2 uses for the framebuffer.
    // 触摸传感器按竖屏(480x800)安装：先算出竖屏(rotation 3)坐标，
    // 再按屏幕库相同的 90 度旋转步骤推出其余三个方向。
    const uint16_t rawX = (uint16_t)raw.x;
    const uint16_t rawY = (uint16_t)raw.y > touchNativeHeight ? touchNativeHeight : (uint16_t)raw.y;
    const int16_t px = scaleTouchCoordinate(rawX, touchNativeWidth, TFT_HEIGHT - 1);
    const int16_t py = scaleTouchCoordinate(touchNativeHeight - rawY, touchNativeHeight, TFT_WIDTH - 1);

    switch (rotation) {
        case 0:
            *x = py;
            *y = (TFT_HEIGHT - 1) - px;
            break;
        case 1:
            *x = (TFT_HEIGHT - 1) - px;
            *y = (TFT_WIDTH - 1) - py;
            break;
        case 2:
            *x = (TFT_WIDTH - 1) - py;
            *y = px;
            break;
        default:
            *x = px;
            *y = py;
            break;
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

void _setup_gpio() {

    powerOnHold();

    // Release any RTC GPIO hold left over from a deep sleep entered by a
    // launched app (e.g. an e-paper app that calls gpio_hold_en()/
    // gpio_deep_sleep_hold_en() on these pins to keep rails/reset lines
    // fixed while asleep, then wakes via reset back into the launcher).
    // gpio_reset_pin() below does NOT clear a hold latch by itself -- an
    // unreleased hold silently discards every write this function makes,
    // which is what "display never powers, GT911 never answers" looks
    // like after returning from such an app.
    gpio_hold_dis((gpio_num_t)TOUCH_EN);
    gpio_hold_dis((gpio_num_t)TOUCH_RST);
    gpio_hold_dis((gpio_num_t)TOUCH_INT);
    gpio_hold_dis((gpio_num_t)SD_PWR_EN);
    gpio_hold_dis((gpio_num_t)EPD_EN);
    gpio_hold_dis((gpio_num_t)BAT_CHG_EN);
    gpio_deep_sleep_hold_dis();

    // Powered up here, well ahead of the first setupSdCard() call later in
    // boot, so the confirmed 100ms settle time is already spent by then.
    gpio_reset_pin((gpio_num_t)TOUCH_INT);
    gpio_reset_pin((gpio_num_t)TOUCH_EN);
    gpio_reset_pin((gpio_num_t)SD_PWR_EN);
    gpio_reset_pin((gpio_num_t)EPD_EN);
    gpio_reset_pin((gpio_num_t)BAT_CHG_EN);
    launcherGpioOutput(TOUCH_EN);
    launcherGpioOutput(SD_PWR_EN);
    launcherGpioOutput(EPD_EN);
    launcherGpioOutput(BAT_CHG_EN);
    launcherGpioOutput(TFT_CS);
    launcherGpioOutput(SDCARD_CS);

    launcherGpioOutput(TOUCH_INT);
    launcherGpioWrite(TOUCH_INT, LOW);

    launcherGpioWrite(TOUCH_EN, LOW);
    launcherGpioWrite(SD_PWR_EN, LOW);
    launcherGpioWrite(EPD_EN, LOW);
    launcherGpioWrite(BAT_CHG_EN, HIGH);
    launcherDelayMs(100); // Wait for 3.3V rails to settle before touching the bus

    launcherGpioWrite(TOUCH_EN, HIGH);
    launcherGpioWrite(SD_PWR_EN, HIGH);
    launcherGpioWrite(EPD_EN, HIGH);
    launcherGpioWrite(BAT_CHG_EN, LOW); // Active low; left undriven the charger stays disabled.
    // Drive CS Pins High
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioWrite(SDCARD_CS, HIGH);

    // Setup Inputs
    hal_buttons_init(buttonsCfg(), 3);

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

void _post_setup_gpio() {
    touchReady = bringUpTouch();
    // Swap/mirror per rotation is set in InputHandler(), same as xteink-x4pro
    // (same 800x480 native panel geometry).
}

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

void _setBrightness(uint8_t brightval) {
    // No backlight and no frontlight on this panel.
    (void)brightval;
}

void InputHandler(void) {
    static unsigned long pool_tm = launcherMillis();
    static unsigned long ready_tm = launcherMillis();

    hal_buttons_poll_3(buttonsCfg());

    if (launcherMillis() - ready_tm > 500 && !touchReady) {
        if (!Wire.begin(TOUCH_SDA, TOUCH_SCL)) launcherConsolePrintln("Fail Starting Wire");
        touchReady = bringUpTouch();
        ready_tm = launcherMillis();
    }

    if (!touchReady || launcherMillis() - pool_tm < 100) return;
    pool_tm = launcherMillis();

    int16_t tx = 0, ty = 0;
    if (readTouchPoint(&tx, &ty)) {
        LTouchPoint t;
        t.x = tx;
        t.y = ty;
        t.pressed = true;
        hal_touch_apply(t);
    }
}

/***************************************************************************************
** Function name: reboot()
** Description:   Power-cycles the microSD rail before the CPU resets, so the
**                firmware being launched finds the card in its power-on state.
**                重启 CPU 前先给 microSD 断电，让被启动的固件拿到一张刚上电的卡。
***************************************************************************************/
void reboot() {
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioWrite(SD_PWR_EN, LOW);
    launcherDelayMs(200);
    ESP.restart();
}

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
