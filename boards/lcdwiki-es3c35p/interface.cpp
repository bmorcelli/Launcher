#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <interface.h>

// lcdwiki-es3c35p -- ESP32-S3 + 3.5" ST77922 (QSPI) with the capacitive touch
// block built into the same controller IC (I2C, address 0x55, 16-bit register
// map -- not FT6x36-compatible; see TOUCH_CTRL_ST77922 in touch.cpp). Sibling of
// ES3C28P, which this file mirrors closely; see that board for the same
// pattern with a standard-SPI panel and a standard-address touch chip.
// The SD card is wired to the SDIO interface, not SPI; the pins live in
// platformio.ini and sd_functions.cpp mounts it in 4-bit mode from there.

// The touch block answers at 0x55; passed to hal_touch_init() explicitly.
#define TOUCH_I2C_ADDR 0x55

// BOOT button (GPIO0) -- the only physical key on the module, used as Esc.
#define BOOT_BTN 0

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_sda = TOUCH_SDA;
    cfg.pin_scl = TOUCH_SCL;
    // The reset line is pulsed once in _setup_gpio(), before the display starts.
    // It only affects the touch half of the chip, but the touch driver must not
    // pulse it again after init anyway, so it is left to _setup_gpio().
    cfg.pin_rst = -1;
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
    // Coming back from powerOff(): release the backlight pin hold it set, or the
    // digitalWrite() below would be ignored and the screen would stay dark.
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)TFT_BL);

    // The ST77922 is one chip doing both display and touch, and the touch half's
    // reset line (GPIO 48, "TOUCH_RST") idles LOW on this board: the chip does
    // not answer on I2C (0x55) until it is pulsed. Release it once, up front.
    // This is for touch only -- the display half works without it.
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(20);
    digitalWrite(TOUCH_RST, HIGH);
    delay(200);

    // Backlight on right away (active HIGH), before the panel is initialised;
    // the brightness PWM in _post_setup_gpio() takes over the pin afterwards.
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

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
#ifndef ST77922_BRINGUP_TEST
    // Backlight PWM -- must be done after tft.init()
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
#endif

    // Capacitive touch, at this board's nonstandard I2C address.
    hal_touch_init(touchCfg(), TOUCH_I2C_ADDR);
}

void _setBrightness(uint8_t brightval) {
#ifndef ST77922_BRINGUP_TEST
    hal_bright_set(TFT_BL, brightval);
#else
    (void)brightval; // bring-up: backlight held solid on by _setup_gpio()
#endif
}

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

    // Backlight off and latched off for the duration of deep sleep; the pad would
    // otherwise float once the LEDC peripheral loses power. Released in _setup_gpio().
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
    gpio_hold_en((gpio_num_t)TFT_BL);
    gpio_deep_sleep_hold_en();

    // The BOOT button (GPIO0, an RTC pad) is the only possible wake source: the
    // touch interrupt is on GPIO47, which is not an RTC GPIO, so a touch cannot
    // wake a deep sleep. Without this the board only restarts via reset / power.
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
