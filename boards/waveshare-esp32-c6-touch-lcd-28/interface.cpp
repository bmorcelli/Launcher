#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

static constexpr uint8_t expanderAddress = 0x24;
static constexpr uint8_t expanderModeRegister = 0x02;
static constexpr uint8_t expanderOutputRegister = 0x03;
static constexpr uint8_t expanderPwmRegister = 0x05;
static constexpr uint8_t expanderAdcRegister = 0x06;
static uint8_t expanderOutput = 0x03;

static bool writeExpander(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(expanderAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static void setReset(uint8_t pin, bool level) {
    if (level) expanderOutput |= 1U << pin;
    else expanderOutput &= ~(1U << pin);
    if (!writeExpander(expanderOutputRegister, expanderOutput))
        launcherConsolePrintf("CH32: reset write failed\n");
}

static DeviceTouch touchConfig() {
    DeviceTouch cfg;
    cfg.pin_sda = 6;
    cfg.pin_scl = 7;
    cfg.pin_rst = -1;
    cfg.pin_irq = 18;
    cfg.cst8xx_model = 3;
    const bool swap[] = {false, true, false, true};
    const bool mirrorX[] = {false, false, true, true};
    const bool mirrorY[] = {false, true, true, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swap[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

void _setup_gpio() {
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    Wire.begin(6, 7);
    Wire.setClock(400000);
    if (!writeExpander(expanderOutputRegister, expanderOutput) || !writeExpander(expanderModeRegister, 0x0b))
        launcherConsolePrintf("CH32: initialization failed\n");
    setReset(1, false);
    delay(50);
    setReset(1, true);
    delay(120);
    setReset(0, false);
    delay(100);
    setReset(0, true);
    delay(500);
}

void _post_setup_gpio() {
    if (!hal_touch_init(touchConfig(), 0x58)) launcherConsolePrintf("CST3530: initialization failed\n");
}

void _setBrightness(uint8_t value) {
    if (!writeExpander(expanderPwmRegister, (uint8_t)((uint16_t)value * 255 / 100)))
        launcherConsolePrintf("CH32: backlight write failed\n");
}

void InputHandler() {
    static bool touchHeld = false;
    static uint32_t lastTouchTime = 0;
    checkPowerSaveTime();
    LTouchPoint point;
    const uint32_t now = launcherMillis();
    if (hal_touch_read(touchConfig(), point)) {
        lastTouchTime = now;
        if (!touchHeld) {
            touchHeld = true;
            hal_touch_apply(point);
        }
    } else if (touchHeld && now - lastTouchTime >= 150) {
        touchHeld = false;
    }
}

int getBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) {
        Wire.beginTransmission(expanderAddress);
        Wire.write(expanderAdcRegister);
        if (Wire.endTransmission(false) != 0 || Wire.requestFrom(expanderAddress, (uint8_t)2) != 2) return 0;
        uint16_t sample = Wire.read();
        sample |= (uint16_t)Wire.read() << 8;
        if (sample > 1023) return 0;
        sum += sample;
        if (i < 7) delay(5);
    }
    int millivolts = ((sum + 4) / 8) * 9900 / 1023;
    if (millivolts < 2500) return 0;
    return constrain((millivolts - 3300) * 100 / 850, 1, 100);
}
void powerOff() {
    _setBrightness(0);
    esp_deep_sleep_start();
}
void reboot() { ESP.restart(); }
