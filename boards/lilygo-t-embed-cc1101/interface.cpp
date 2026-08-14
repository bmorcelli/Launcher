#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <globals.h>
#include <interface.h>
// Rotary encoder
#include <RotaryEncoder.h>
RotaryEncoder *encoder = nullptr;
IRAM_ATTR void checkPosition() { encoder->tick(); }

#include <Wire.h>
// Charger chip, CC1101 board only
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
XPowersPPM PPM;

#define BATTERY_DESIGN_CAPACITY 1300
#include <bq27220.h>
BQ27220 bq;

// ---------------------------------------------------------------------------
// Which T-Embed is this?
//
// The plain T-Embed and the CC1101 are the same ESP32-S3 with the same encoder,
// the same button and the same 170x320 ST7789 -- wired differently, and with a
// charger, a fuel gauge and three radios added on the CC1101. One binary drives
// both.
//
// The probe is the BQ25896 charger: it answers on a CC1101 board and there is
// nothing at that address on a plain one. Both boards put I2C on GPIO 8/18, so
// the bus can be brought up before knowing which board this is -- and so can
// GPIO15, which is the CC1101's power enable and the plain board's backlight.
// Everything else has to wait for the answer.
//
// The seeded configuration is the CC1101; the probe only rewrites what a plain
// T-Embed needs.
// ---------------------------------------------------------------------------
enum TEmbedVariant : uint8_t { T_EMBED_CC1101 = 0, T_EMBED_PLAIN = 1 };
static TEmbedVariant tEmbed = T_EMBED_CC1101;

#define TE_I2C_SDA 8
#define TE_I2C_SCL 18
// Asserted before the probe: power enable on the CC1101, backlight on the plain
// board. Harmless as an output on either.
#define TE_SHARED_POWER_PIN 15

#define SEL_BTN 0
#define BTN_ACT LOW

// Pins that move between the two boards. Seeded with the CC1101's.
static int8_t pinPowerOn = TE_SHARED_POWER_PIN;
static int8_t pinBack = 6; // no back button on the plain board
static int8_t pinEncA = 4;
static int8_t pinEncB = 5;
static int8_t pinBacklight = TFT_BL;
static gpio_num_t pinWakeup = GPIO_NUM_6;

// Plain T-Embed
#define TE_PLAIN_POWER_ON 46
#define TE_PLAIN_ENC_A 2
#define TE_PLAIN_ENC_B 1
#define TE_PLAIN_BACKLIGHT 15
#define TE_PLAIN_BAT_PIN 4 // battery divider, 1:2
#define TE_PLAIN_TFT_CS 10
#define TE_PLAIN_TFT_DC 13
#define TE_PLAIN_TFT_RST 9
#define TE_PLAIN_TFT_SCLK 12
#define TE_PLAIN_TFT_MOSI 11
#define TE_PLAIN_SD_CS 39
#define TE_PLAIN_SD_SCK 40
#define TE_PLAIN_SD_MISO 38
#define TE_PLAIN_SD_MOSI 41

/***************************************************************************************
** Function name: _detect_variant()
** Description:   ask I2C which board this is, and move everything that follows
***************************************************************************************/
static void _detect_variant() {
    launcherGpioOutput(TE_SHARED_POWER_PIN);
    launcherGpioWrite(TE_SHARED_POWER_PIN, HIGH);

    Wire.begin(TE_I2C_SDA, TE_I2C_SCL);
    Wire.beginTransmission(BQ25896_SLAVE_ADDRESS);
    tEmbed = (Wire.endTransmission() == 0) ? T_EMBED_CC1101 : T_EMBED_PLAIN;

    if (tEmbed == T_EMBED_PLAIN) {
        pinPowerOn = TE_PLAIN_POWER_ON;
        pinBack = -1;
        pinEncA = TE_PLAIN_ENC_A;
        pinEncB = TE_PLAIN_ENC_B;
        pinBacklight = TE_PLAIN_BACKLIGHT;
        pinWakeup = GPIO_NUM_0;

        displayConfig.cs = TE_PLAIN_TFT_CS;
        displayConfig.dc = TE_PLAIN_TFT_DC;
        displayConfig.rst = TE_PLAIN_TFT_RST;
        displayConfig.sclk = TE_PLAIN_TFT_SCLK;
        displayConfig.mosi = TE_PLAIN_TFT_MOSI;
        displayConfig.miso = -1;
        displayConfig.bl = TE_PLAIN_BACKLIGHT;

        // setupSdCard() reads these globals, not the SDCARD_* macros.
        _cs = TE_PLAIN_SD_CS;
        _sck = TE_PLAIN_SD_SCK;
        _miso = TE_PLAIN_SD_MISO;
        _mosi = TE_PLAIN_SD_MOSI;

        device_name = "Lilygo T-Embed";
        ota_tag = "t-embed";
    }

    launcherConsolePrintf("Board: %s\n", tEmbed == T_EMBED_CC1101 ? "T-Embed CC1101" : "T-Embed");
}

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    _detect_variant();

    launcherGpioOutput(pinPowerOn);
    launcherGpioWrite(pinPowerOn, HIGH);
    launcherGpioInput(SEL_BTN);

    if (tEmbed == T_EMBED_CC1101) {
        // The CC1101 board has an antenna circuit tuned per frequency band,
        // switched by SW0 and SW1.
        launcherGpioOutput(47); // antenna circuit
        launcherGpioOutput(48); // antenna circuit

        // Chip Select CC1101, SD and TFT to HIGH State to fix SD initialization
        launcherGpioOutput(12);             // CC1101
        launcherGpioWrite(12, HIGH);        // CC1101
        launcherGpioOutput(TFT_CS);         // display
        launcherGpioWrite(TFT_CS, HIGH);    // display
        launcherGpioOutput(SDCARD_CS);      // SDCard
        launcherGpioWrite(SDCARD_CS, HIGH); // SDCard
        launcherGpioOutput(44);             // nrf24
        launcherGpioWrite(44, HIGH);        // nrf24

        // Wire is already up: _detect_variant() started it to find this chip.
        if (PPM.init(Wire, TE_I2C_SDA, TE_I2C_SCL, BQ25896_SLAVE_ADDRESS)) {
            PPM.setSysPowerDownVoltage(3300);
            PPM.setInputCurrentLimit(3250);
            launcherConsolePrintf("getInputCurrentLimit: %d mA\n", (int)PPM.getInputCurrentLimit());
            PPM.disableCurrentLimitPin();
            PPM.setChargeTargetVoltage(4208);
            PPM.setPrechargeCurr(64);
            PPM.setChargerConstantCurr(832);
            PPM.getChargerConstantCurr();
            launcherConsolePrintf("getChargerConstantCurr: %d mA\n", (int)PPM.getChargerConstantCurr());
            PPM.enableMeasure(PowersBQ25896::CONTINUOUS);
            PPM.disableOTG();
            PPM.enableCharge();
        }
        if (bq.getDesignCap() != BATTERY_DESIGN_CAPACITY) { bq.setDesignCap(BATTERY_DESIGN_CAPACITY); }

        launcherGpioInput(pinBack);
    }

    encoder = new RotaryEncoder(pinEncA, pinEncB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(pinEncA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(pinEncB), checkPosition, CHANGE);
}

/*********************************************************************
**  Function: _setBrightness
**  set brightness value -- the backlight is on a different pin per board
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(pinBacklight, brightval);
    } else {
        const uint8_t PWM_MIN = 85;
        const uint8_t PWM_MAX = 255;
        float linear = (float)brightval / 100.0;
        uint8_t value = PWM_MIN + round(pow(linear, 2.2) * (PWM_MAX - PWM_MIN));
        analogWrite(pinBacklight, value);
    }
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    if (tEmbed == T_EMBED_CC1101) {
        int percent = bq.getChargePcnt();
        return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
    }

    // Plain T-Embed: no gauge, just a 1:2 divider on an ADC.
    static bool adcInitialized = false;
    if (!adcInitialized) {
        launcherGpioInput(TE_PLAIN_BAT_PIN);
        adcInitialized = true;
    }
    float actualVoltage = (float)analogReadMilliVolts(TE_PLAIN_BAT_PIN) * 2.0f;
    const float MIN_VOLTAGE = 3300.0f;
    const float MAX_VOLTAGE = 4150.0f;
    float percent = ((actualVoltage - MIN_VOLTAGE) / (MAX_VOLTAGE - (MIN_VOLTAGE + 50.0f))) * 100.0f;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    return (int)percent;
}

/***************************************************************************************
** Function name: isCharging()
** Description:   Determines if the device is charging
***************************************************************************************/
bool isCharging() { return tEmbed == T_EMBED_CC1101 ? bq.getIsCharging() : false; }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = launcherMillis(); // debauce for buttons
    static int posDifference = 0;
    static int lastPos = 0;
    bool sel = !BTN_ACT;
    bool esc = !BTN_ACT;

    int newPos = encoder->getPosition();
    if (newPos != lastPos) {
        posDifference += (newPos - lastPos);
        lastPos = newPos;
    }

    if (launcherMillis() - tm < 200 && !LongPress) return;

    sel = launcherGpioRead(SEL_BTN);
    if (pinBack >= 0) esc = launcherGpioRead(pinBack);

    if (posDifference != 0 || sel == BTN_ACT || esc == BTN_ACT) {
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    if (posDifference > 0) {
        PrevPress = true;
        posDifference--;
    }
    if (posDifference < 0) {
        NextPress = true;
        posDifference++;
    }

    if (sel == BTN_ACT) {
        posDifference = 0;
        SelPress = true;
        tm = launcherMillis();
    }
    if (esc == BTN_ACT) {
        EscPress = true;
        tm = launcherMillis();
    }
}

void powerOff() {
    if (tEmbed == T_EMBED_PLAIN) {
        launcherGpioWrite(pinPowerOn, LOW);
        esp_sleep_enable_ext0_wakeup(pinWakeup, LOW);
        esp_deep_sleep_start();
        return;
    }

    options = {
        {"Deep Sleep",
         []() {
             launcherGpioWrite(pinPowerOn, LOW);
             esp_sleep_enable_ext0_wakeup(pinWakeup, LOW);
             esp_deep_sleep_start();
         }                                           },
        {"Power Off",
         []() {
             displayMsg("Connect to USB to pwr on");
             for (int i = 3; i > 0; i--) {
                 displayRedStripe("Shutting down in " + String(i));
                 launcherDelayMs(1000);
             }
             PPM.shutdown();
             tft->fillScreen(BLACK);
             displayRedStripe("Unplug USB to power off");
             while (true) launcherDelayMs(100);
         }                                           },
        {"Main Menu",  [=]() { returnToMenu = true; }},
    };
    loopOptions(options);
}

void checkReboot() {
    // Only the CC1101 board has a button to hold.
    if (pinBack < 0) return;

    int countDown;
    /* Long press power off */
    if (launcherGpioRead(pinBack) == BTN_ACT) {
        vTaskSuspend(xHandle);
        uint32_t time_count = launcherMillis();
        while (launcherGpioRead(pinBack) == BTN_ACT) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                if (countDown < 4)
                    tft->drawCentreString("DeepSleep in " + String(countDown) + "/3", tftWidth / 2, 12, 1);
                else {
                    tft->fillScreen(BGCOLOR);
                    while (launcherGpioRead(pinBack) == BTN_ACT) launcherDelayMs(10);
                    launcherDelayMs(1000);
                    launcherGpioWrite(pinPowerOn, LOW);
                    esp_sleep_enable_ext0_wakeup(pinWakeup, LOW);
                    esp_deep_sleep_start();
                }
                launcherDelayMs(10);
            }
        }
        vTaskResume(xHandle);

        // Clear text after releasing the button
        launcherDelayMs(30);
        if (launcherMillis() - time_count > 500)
            tft->fillRect(tftWidth / 2 - 9 * LW, 12, 18 * LW, LH * _fp, BGCOLOR);
    }
}
