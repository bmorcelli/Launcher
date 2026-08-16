#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Adafruit_NeoPixel.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <interface.h>

// ES3C28P — ESP32-S3 + 2.8" ILI9341V (SPI) + FT6336G capacitive touch (I2C).
// IMPORTANT NOTE: SD card is wired to the SDIO interface, not SPI!!!

#ifndef TFT_BRIGHT_CHANNEL
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_FREQ 5000
#define TFT_BRIGHT_Bits 8
#endif

#ifndef RGB_LED_PIN
#define RGB_LED_PIN 42
#endif

static Adafruit_NeoPixel rgbLed(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// touch i2c thingie

#define FT6336_ADDR 0x38
#define FT6336_TD_STATUS 0x02 // number of touch points
#define FT6336_T1_XH 0x03     // first touch X high byte (4-bit MSB)

static bool _ft_read(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)FT6336_ADDR, (int)len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.available() ? Wire.read() : 0;
    return true;
}

static void _ft6336_init() {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(300);

    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000U);

    // read id
    uint8_t chipId = 0;
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0xA3);
    if (Wire.endTransmission(false) == 0) {
        Wire.requestFrom((int)FT6336_ADDR, 1);
        if (Wire.available()) chipId = Wire.read();
    }
    launcherConsolePrintf("[ES3C28P] FT6336G chip ID: 0x%02X\n", chipId);

    // raise touch threshold
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(0x80);
    Wire.write(40);
    Wire.endTransmission();
}

static bool _ft6336_read_raw(uint16_t *raw_x, uint16_t *raw_y) {
    uint8_t data[7];
    if (!_ft_read(FT6336_TD_STATUS, data, 7)) return false;
    if ((data[0] & 0x0F) == 0) return false;
    *raw_x = ((uint16_t)(data[1] & 0x0F) << 8) | data[2];
    *raw_y = ((uint16_t)(data[3] & 0x0F) << 8) | data[4];
    return true;
}

static bool _ft6336_get_point(LTouchPoint *out) {
    uint16_t rx, ry;
    if (!_ft6336_read_raw(&rx, &ry)) return false;
    switch (rotation % 4) {
        case 0:
            out->x = rx;
            out->y = ry;
            break;
        case 1:
            out->x = ry;
            out->y = TFT_WIDTH - rx;
            break;
        case 2:
            out->x = TFT_WIDTH - rx;
            out->y = TFT_HEIGHT - ry;
            break;
        case 3:
            out->x = TFT_HEIGHT - ry;
            out->y = rx;
            break;
    }
    return true;
}

void _setup_gpio() {
    // TFT CS high so the bus is quiet while everything else comes up.
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);

    // Audio disabled by default (Audio_EN is active LOW).
#ifdef AUDIO_EN_PIN
    pinMode(AUDIO_EN_PIN, OUTPUT);
    digitalWrite(AUDIO_EN_PIN, HIGH);
#endif

    rgbLed.begin();
    rgbLed.clear();
    rgbLed.show();
}

void _post_setup_gpio() {
    // Backlight PWM — must be done after tft.init(). Defaults OFF at boot.
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);

    // Capacitive touch
    _ft6336_init();

    // SD card — 4-bit SDIO
#if defined(USE_SD_MMC) && defined(SD_MMC_4BIT)
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
#endif

    // BOOT button doubles as a physical Esc/select key
    launcherGpioInputPullup(SEL_BTN);
}

void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 190;
    else if (brightval == 50) dutyCycle = 130;
    else if (brightval == 25) dutyCycle = 60;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

void InputHandler(void) {
    static long tm = launcherMillis();

    // BOOT button (GPIO0)
    bool bootPressed = (launcherGpioRead(SEL_BTN) == BTN_ACT);

    if (launcherMillis() - tm > 250 || LongPress) {
        LTouchPoint t;
#ifdef DONT_USE_INPUT_TASK
        checkPowerSaveTime();
#endif
        if (_ft6336_get_point(&t)) {
            tm = launcherMillis();
#ifdef DONT_USE_INPUT_TASK
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            touchPoint.pressed = false;
#endif
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        } else if (bootPressed) {
            tm = launcherMillis();
            if (!wakeUpScreen()) {
                AnyKeyPress = true;
                EscPress = true;
            }
        }
    }
}

void powerOff() {
    rgbLed.clear();
    rgbLed.show();
    esp_deep_sleep_start();
}

void checkReboot() { /* No dedicated reboot button on ES3C28P */ }
