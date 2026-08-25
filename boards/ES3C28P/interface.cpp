#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

// ES3C28P — ESP32-S3 + 2.8" ILI9341V (SPI) + FT6336G capacitive touch (I2C).
// The SD card is wired to the SDIO interface, not SPI; the pins live in
// platformio.ini and sd_functions.cpp mounts it in 4-bit mode from there.

// BOOT button (GPIO0) — the only physical key on the module, used as Esc.
#define BOOT_BTN 0

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = TOUCH_SDA;
    cfg.pin_scl = TOUCH_SCL;
    cfg.pin_rst = TOUCH_RST;
    cfg.pin_irq = TOUCH_INT;
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

void _setup_gpio() {
    // TFT CS high so the bus is quiet while everything else comes up
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    // Audio codec disabled by default (Audio_EN is active LOW)
    pinMode(AUDIO_EN_PIN, OUTPUT);
    digitalWrite(AUDIO_EN_PIN, HIGH);

    // WS2812 status LED off. neopixelWrite() ships with the Arduino core, so the
    // board needs no external LED library.
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);

    // BOOT button doubles as the Esc key
    launcherGpioInputPullup(BOOT_BTN);
}

void _post_setup_gpio() {
    // Backlight PWM — must be done after tft.init()
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);

    // Capacitive touch. Raise the touch threshold (chip default ~22) to
    // reduce phantom touches when the module sits in a case.
    hal_touch_init(touchCfg());
    hal_touch_set_threshold(40);
}

void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

void InputHandler(void) {
    static long tm = launcherMillis();
    if (launcherMillis() - tm > 250 || LongPress) {
        LTouchPoint t;
        checkPowerSaveTime();
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        } else if (launcherGpioRead(BOOT_BTN) == LOW) {
            tm = launcherMillis();
            if (!wakeUpScreen()) {
                AnyKeyPress = true;
                EscPress = true;
            }
        }
    }
}

void powerOff() {
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);
    esp_deep_sleep_start();
}
