#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <globals.h>
#include <interface.h>
// Rotary encoder
#include <RotaryEncoder.h>
RotaryEncoder *encoder = nullptr;
IRAM_ATTR void checkPosition() { encoder->tick(); }

// Battery libs
#if TFT_MOSI < 10
#define PIN_POWER_ON 15
#define SEL_BTN 0
#define BK_BTN 6
#define ENCODER_INA 4
#define ENCODER_INB 5
#define ENCODER_KEY 0
#define BTN_ACT LOW
// Power handler for battery detection
#include <Wire.h>
// Charger chip
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
#include <esp32-hal-dac.h>
XPowersPPM PPM;
#else
#define PIN_POWER_ON 46
#define HAS_BTN 1
#define SEL_BTN 0
#define ENCODER_INA 2
#define ENCODER_INB 1
#define ENCODER_KEY 0
#define BTN_ACT LOW
#endif

#ifdef USE_BQ27220_VIA_I2C
#define BATTERY_DESIGN_CAPACITY 1300
#include <bq27220.h>
BQ27220 bq;
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(PIN_POWER_ON);
    launcherGpioWrite(PIN_POWER_ON, HIGH);
    launcherGpioInput(SEL_BTN);
#if TFT_MOSI < 10
    // T-Embed CC1101 has a antenna circuit optimized to each frequency band, controlled by SW0 and SW1
    // Set antenna frequency settings
    launcherGpioOutput(CC1101_SW1_PIN);
    launcherGpioOutput(CC1101_SW0_PIN);

    // Chip Select CC1101, SD and TFT to HIGH State to fix SD initialization
    launcherGpioOutput(CC1101_SS_PIN);
    launcherGpioWrite(CC1101_SS_PIN, HIGH);
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);
    launcherGpioOutput(44);
    launcherGpioWrite(44, HIGH);

    // Power chip pin
    launcherGpioOutput(PIN_POWER_ON);
    launcherGpioWrite(PIN_POWER_ON, HIGH); // Power on CC1101 and LED
    bool pmu_ret = false;
    Wire.begin(GROVE_SDA, GROVE_SCL);
    pmu_ret = PPM.init(Wire, GROVE_SDA, GROVE_SCL, BQ25896_SLAVE_ADDRESS);
    if (pmu_ret) {
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

#endif

#if TFT_MOSI < 10
    launcherGpioInput(BK_BTN);
#endif
    launcherGpioInput(ENCODER_KEY);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), checkPosition, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), checkPosition, CHANGE);
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
#if defined(USE_BQ27220_VIA_I2C)
int getBattery() {
    int percent = 0;
    percent = bq.getChargePcnt();
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}
#endif
/*********************************************************************
**  Function: setBrightness
**  set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else if (brightval > 99) {
        analogWrite(TFT_BL, 254);
    } else {
        float linear = (float)brightval / 100.0;
        uint8_t value = round(pow(linear, 2.2) * 255.0);
        analogWrite(TFT_BL, value);
    }
}

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
#if TFT_MOSI < 10
    esc = launcherGpioRead(BK_BTN);
#endif

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
#if TFT_MOSI < 10
    options = {
        {"Deep Sleep",
         []() {
             launcherGpioWrite(PIN_POWER_ON, LOW);
             esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, LOW);
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

#else
    launcherGpioWrite(PIN_POWER_ON, LOW);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    esp_deep_sleep_start();
#endif
}

void checkReboot() {
#if TFT_MOSI < 10
    int countDown;
    /* Long press power off */
    if (launcherGpioRead(BK_BTN) == BTN_ACT) {
        vTaskSuspend(xHandle);
        uint32_t time_count = launcherMillis();
        while (launcherGpioRead(BK_BTN) == BTN_ACT) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                if (countDown < 4)
                    tft->drawCentreString("DeepSleep in " + String(countDown) + "/3", tftWidth / 2, 12, 1);
                else {
                    tft->fillScreen(BGCOLOR);
                    while (launcherGpioRead(BK_BTN) == BTN_ACT) launcherDelayMs(10);
                    launcherDelayMs(1000);
                    launcherGpioWrite(PIN_POWER_ON, LOW);
                    esp_sleep_enable_ext0_wakeup(GPIO_NUM_6, LOW);
                    esp_deep_sleep_start();
                }
                launcherDelayMs(10);
            }
        }
        vTaskResume(xHandle);

        // Clear text after releasing the button
        launcherDelayMs(30);
        if (launcherMillis() - time_count > 500)
            tft->fillRect(tftWidth / 2 - 9 * LW, 12, 18 * LW, LH * FP, BGCOLOR);
    }
#endif
}
/***************************************************************************************
** Function name: isCharging()
** Description:   Determines if the device is charging
***************************************************************************************/
#ifdef USE_BQ27220_VIA_I2C
bool isCharging() {
    return bq.getIsCharging(); // Return the charging status from BQ27220
}
#else
bool isCharging() { return false; }
#endif
