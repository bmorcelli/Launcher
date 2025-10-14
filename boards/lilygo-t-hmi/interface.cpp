#include "CYD28_TouchscreenR.h"
#include "powerSave.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <interface.h>
CYD28_TouchR touch(320, 240);

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    pinMode(CYD28_TouchR_CS, OUTPUT);
    digitalWrite(CYD28_TouchR_CS, HIGH);
    pinMode(PWR_ON_PIN, OUTPUT);
    digitalWrite(PWR_ON_PIN, HIGH);
    pinMode(PWR_EN_PIN, OUTPUT);
    digitalWrite(PWR_EN_PIN, HIGH);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    SPI.begin(CYD28_TouchR_CLK, CYD28_TouchR_MISO, CYD28_TouchR_MOSI);
    if (!touch.begin(&SPI)) { Serial.println("Touchscreen initialization failed!"); }
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_Bits 8
#define TFT_BRIGHT_FREQ 5000
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 255);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 255;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 255) / 100);

    // log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    ledcWrite(TFT_BL, dutyCycle); // Channel 0
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (millis() - tm > 200 || LongPress) {
        // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
        if (touch.touched()) {
            auto t = touch.getPointScaled();
            Serial.printf("\nRAW: Touch Pressed on x=%d, y=%d, rot=%d", t.x, t.y, rotation);
            tm = millis();
            if (rotation == 3) {
                // t.y = t.y;
                t.x = tftWidth - t.x;
            }
            if (rotation == 1) {
                t.y = (tftHeight + 20) - t.y;
                // t.x = t.x;
            }
            if (rotation == 0) {
                int tmp = t.x;
                t.x = t.y;
                t.y = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x;
                t.x = tftWidth - t.y;
                t.y = (tftHeight + 20) - tmp;
            }
            Serial.printf("\nROT: Touch Pressed on x=%d, y=%d\n", t.x, t.y);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t percent;
    uint32_t volt = analogReadMilliVolts(GPIO_NUM_5);
    float mv = volt;
    percent = (mv - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}
