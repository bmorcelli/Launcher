// Main I2C Bus
#define SEL_BTN 0
#define UP_BTN 41
#define DW_BTN 40
#define R_BTN 38
#define L_BTN 39
#define ESC_BTN 21
#define BTN_ACT LOW

#define GROVE_SDA 47
#define GROVE_SCL 48
#define CC1101_GDO0_PIN 46
#define CC1101_SS_PIN 9

#define NRF24_SS_PIN 13
#define LORA_CS 4

#define REAPER_BQ25896_ADDRESS 0x6B

// IO EXPANDER
#define USE_IO_EXPANDER
#define IO_EXPANDER_AW9523
#define IO_EXP_GPS 5
#define IO_EXP_VIBRO 15
#define IO_EXP_CC_RX 9
#define IO_EXP_CC_TX 10
#define IO_EXP_LOGO 0
#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/power/gauge.h"
#include "hal/power/pmic.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <globals.h>
#include <interface.h>

#include <Wire.h>

#define BATTERY_DESIGN_CAPACITY 1000

static DeviceButtons buttonsCfg() { return DeviceButtons{L_BTN, R_BTN, UP_BTN, DW_BTN, SEL_BTN, ESC_BTN}; }

void _setup_gpio() {

    hal_buttons_init(buttonsCfg(), 6);

    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);
    pinMode(SS, OUTPUT); /// NFC PIN
    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);
    digitalWrite(SS, HIGH);

    // Starts SPI instance for CC1101 and NRF24 with CS pins blocking communication at start

    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    Wire.setPins(GROVE_SDA, GROVE_SCL);
    // Wire.begin();
    // bruceConfig.rfModule = CC1101_SPI_MODULE;
    // bruceConfig.irRx = RXLED;
    // bruceConfig.irTx = LED;
    Wire.begin(GROVE_SDA, GROVE_SCL);

    DevicePmic pmicCfg{GROVE_SDA, GROVE_SCL, REAPER_BQ25896_ADDRESS};
    if (!hal_pmic_init(pmicCfg)) { launcherConsolePrintln("PMIC: Failed starting BQ25896"); }

    DeviceGauge gaugeCfg{};
    gaugeCfg.design_capacity_mah = BATTERY_DESIGN_CAPACITY;
    hal_gauge_init(gaugeCfg);
}

int getBattery() { return hal_gauge_get_percent(); }

bool isCharging() { return hal_gauge_is_charging(); }

void InputHandler(void) { hal_buttons_poll_6(buttonsCfg()); }

void powerOff() { hal_pmic_shutdown(); }

void checkReboot() {
    int countDown;
    /* Long press power off */
    if (digitalRead(ESC_BTN) == BTN_ACT) {
        uint32_t time_count = millis();
        while (digitalRead(ESC_BTN) == BTN_ACT) {
            // Display poweroff bar only if holding button
            if (millis() - time_count > 500) {
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (millis() - time_count) / 1000 + 1;
                if (countDown < 3)
                    tft->drawCentreString("PWR OFF IN " + String(countDown) + "/2", tftWidth / 2, 12, 1);
                else {
                    tft->fillScreen(BGCOLOR);
                    while (digitalRead(ESC_BTN) == BTN_ACT);
                    delay(200);
                    powerOff();
                }
                delay(10);
            }
        }

        // Clear text after releasing the button
        delay(30);
        tft->fillRect(60, 12, tftWidth - 60, LH, BGCOLOR);
    }
}
