#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <IoExpanderXL9555.hpp>
#include <Wire.h>
#include <interface.h>

#define I2C_SDA 47
#define I2C_SCL 48
#define BUTTON_PIN 0 // BOOT/User
#define LORA_CS 21   // SX1262, unused here -- held high so it stays off the bus

// PCA9555/TCA9535 IO expander -- same register map as the XL9555 driver.
// Pin map from Meshtastic's variants/esp32s3/seeed_wio_tracker_L2/variant.h.
#define IO_EXPANDER_ADDR 0x21
#define EXP_WAKE_BTN 0 // active low
#define EXP_TP_INT 3
#define EXP_LCD_CS 4 // not the panel CS (that is GPIO46); Meshtastic just parks it high
#define EXP_LCD_PWR_EN 5
#define EXP_LCD_RST 6
#define EXP_TP_RST 8
#define EXP_OTG_EN 11
#define EXP_PA_PWR_EN 12
#define EXP_GNSS_PWR_EN 13
#define EXP_SD_PWR_EN 14
#define EXP_BAT_ADC_EN 15

#define TOUCH_ADDR 0x5D   // GT911, selected by holding INT low through reset
#define LP5814_ADDR 0x2C  // backlight LED driver
#define ADS1115_ADDR 0x48 // battery ADC, AIN0 through a 1:2 divider

static IoExpanderXL9555 io;
static bool expanderReady = false;

static void i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static DeviceButtons buttonsCfg() { return DeviceButtons{BUTTON_PIN}; }

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = -1; // Wire already begun in _setup_gpio()
    cfg.pin_scl = -1;
    cfg.pin_rst = -1; // RST and INT are on the IO expander, reset by hand in _setup_gpio()
    cfg.pin_irq = -1;
    // The digitizer is mounted 180 degrees from the panel's native portrait
    // (LovyanGFX touch offset_rotation=2 in Meshtastic's setup).
    //                 rotation: 0      1      2      3
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

// LP5814: 4 LED channels in parallel on the backlight, PWM per channel at 0x18..0x1B.
static void lp5814SetPwm(uint8_t duty) {
    for (uint8_t i = 0; i < 4; i++) i2cWrite(LP5814_ADDR, 0x18 + i, duty);
}

static void lp5814Init() {
    i2cWrite(LP5814_ADDR, 0x00, 0x01);                                    // chip enable
    i2cWrite(LP5814_ADDR, 0x01, 0x01);                                    // 51mA max current
    i2cWrite(LP5814_ADDR, 0x02, 0x00);                                    // outputs off while configuring
    i2cWrite(LP5814_ADDR, 0x04, 0x4E);                                    // dim mode
    i2cWrite(LP5814_ADDR, 0x05, 0xF0);                                    // engine mode
    for (uint8_t i = 0; i < 4; i++) i2cWrite(LP5814_ADDR, 0x14 + i, 200); // DC current
    i2cWrite(LP5814_ADDR, 0x02, 0x0F);                                    // all 4 channels on
    i2cWrite(LP5814_ADDR, 0x0F, 0x55);                                    // latch
    launcherDelayMs(5);
    lp5814SetPwm(0);
}

/***************************************************************************************
** Function:    _setup_gpio()
** Location:    main.cpp
** Description: initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(LORA_CS);
    launcherGpioWrite(LORA_CS, HIGH);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // expander, touch, backlight, ADC, codec and USB-PD all share it

    hal_buttons_init(buttonsCfg(), 1);

    expanderReady = io.begin(Wire, IO_EXPANDER_ADDR);
    if (!expanderReady) {
        launcherConsolePrintln("IO expander NOT found @ 0x21 -- display/touch/SD stay unpowered");
        return;
    }
    io.pinMode(EXP_WAKE_BTN, INPUT);

    // Peripherals the launcher never uses stay off.
    io.pinMode(EXP_OTG_EN, OUTPUT);
    io.digitalWrite(EXP_OTG_EN, LOW);
    io.pinMode(EXP_PA_PWR_EN, OUTPUT);
    io.digitalWrite(EXP_PA_PWR_EN, LOW);
    io.pinMode(EXP_GNSS_PWR_EN, OUTPUT);
    io.digitalWrite(EXP_GNSS_PWR_EN, LOW);

    io.pinMode(EXP_SD_PWR_EN, OUTPUT);
    io.digitalWrite(EXP_SD_PWR_EN, HIGH);
    io.pinMode(EXP_BAT_ADC_EN, OUTPUT);
    io.digitalWrite(EXP_BAT_ADC_EN, HIGH);

    // LCD power and reset, before tft->begin().
    io.pinMode(EXP_LCD_PWR_EN, OUTPUT);
    io.digitalWrite(EXP_LCD_PWR_EN, HIGH);
    launcherDelayMs(50);
    io.pinMode(EXP_LCD_RST, OUTPUT);
    io.digitalWrite(EXP_LCD_RST, HIGH);
    launcherDelayMs(5);
    io.digitalWrite(EXP_LCD_RST, LOW);
    launcherDelayMs(10);
    io.digitalWrite(EXP_LCD_RST, HIGH);
    launcherDelayMs(120);
    io.pinMode(EXP_LCD_CS, OUTPUT);
    io.digitalWrite(EXP_LCD_CS, HIGH);

    // GT911 reset with INT held low selects address 0x5D; INT is released
    // back to the chip afterwards, the HAL polls instead of using it.
    io.pinMode(EXP_TP_RST, OUTPUT);
    io.digitalWrite(EXP_TP_RST, LOW);
    io.pinMode(EXP_TP_INT, OUTPUT);
    io.digitalWrite(EXP_TP_INT, LOW);
    launcherDelayMs(10);
    io.digitalWrite(EXP_TP_RST, HIGH);
    launcherDelayMs(60);
    io.pinMode(EXP_TP_INT, INPUT);

    // setBrightness() runs before _post_setup_gpio(), so the backlight driver comes up here.
    lp5814Init();
}

/***************************************************************************************
** Function:    _post_setup_gpio()
** Location:    main.cpp
** Description: second stage gpio setup, run after TFT and before SD card initialization
***************************************************************************************/
void _post_setup_gpio() {
    if (!hal_touch_init(touchCfg(), TOUCH_ADDR))
        launcherConsolePrintf("Touch IC (GT911 @ 0x%02X) NOT started\n", TOUCH_ADDR);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    static int cached = 0;
    static unsigned long lastPoll = 0;
    const unsigned long now = launcherMillis();
    if (lastPoll != 0 && (now - lastPoll) < 5000) return cached;
    lastPoll = now;

    // ADS1115 single shot: AIN0 vs GND, +-4.096V, 860SPS.
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x01);
    Wire.write(0xC3);
    Wire.write(0xE3);
    if (Wire.endTransmission() != 0) return cached;
    launcherDelayMs(3);
    Wire.beginTransmission(ADS1115_ADDR);
    Wire.write(0x00);
    if (Wire.endTransmission(false) != 0) return cached;
    if (Wire.requestFrom(ADS1115_ADDR, 2) < 2) return cached;
    int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());

    // 0.125mV per LSB, x2 for the divider
    const float mv = raw * 0.25f;
    const float MIN_VOLTAGE = 3300.0f;
    const float MAX_VOLTAGE = 4150.0f;
    float percent = (mv - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE) * 100.0f;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    cached = (int)percent;
    return cached;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { lp5814SetPwm(hal_bright_curve(brightval)); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    hal_buttons_poll_1(buttonsCfg());

    // Side WAKE button (expander P00) -> Esc.
    static unsigned long lastExp = 0;
    static bool wakeWasDown = false;
    if (expanderReady && launcherMillis() - lastExp > 50) {
        lastExp = launcherMillis();
        bool wakeDown = !io.digitalRead(EXP_WAKE_BTN);
        if (wakeDown && !wakeWasDown) {
            if (!wakeUpScreen()) {
                AnyKeyPress = true;
                EscPress = true;
            }
        }
        wakeWasDown = wakeDown;
    }

    LTouchPoint t;
    if (hal_touch_read(touchCfg(), t)) {
        if (!hal_touch_apply(t)) return;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    lp5814SetPwm(0);
    if (expanderReady) {
        io.digitalWrite(EXP_LCD_PWR_EN, LOW);
        io.digitalWrite(EXP_SD_PWR_EN, LOW);
        io.digitalWrite(EXP_BAT_ADC_EN, LOW);
    }
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
