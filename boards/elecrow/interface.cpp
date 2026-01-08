#include "nvs.h"
#include "nvs_handle.hpp"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>

namespace {
std::unique_ptr<nvs::NVSHandle> openNamespace(const char *ns, nvs_open_mode_t mode, esp_err_t &err) {
    auto handle = nvs::open_nvs_handle(ns, mode, &err);
    if (err != ESP_OK) {
        log_i("openNamespace(%s) failed: %d", ns, err);
        return nullptr;
    }
    return handle;
}
} // namespace
#ifndef TFT_BRIGHT_CHANNEL
#define TFT_BRIGHT_CHANNEL 0
#define TFT_BRIGHT_FREQ 5000
#define TFT_BRIGHT_Bits 8
#define TFT_BL 27
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    pinMode(33, OUTPUT); // touch CS
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // Brightness control must be initialized after tft in this case @Pirata
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 255);
    esp_err_t err = ESP_OK;
    uint16_t calData[5];
    auto nvsHandle = openNamespace("calData", NVS_READWRITE, err);
    if (nvsHandle) {
        err = nvsHandle->get_item("x0", calData[0]);
        err |= nvsHandle->get_item("x1", calData[1]);
        err |= nvsHandle->get_item("y0", calData[2]);
        err |= nvsHandle->get_item("y1", calData[3]);
        err |= nvsHandle->get_item("r", calData[4]);
    } else {
        err = 1;
        Serial.println("Can't access calData namespace in NVS");
    }

    if (err) {
        Serial.println("No calData available");
        tft->setRotation(1);
        tft->setTextSize(2);
        tft->drawCentreString("Touch corners as indicated", TFT_HEIGHT / 2, TFT_WIDTH / 2, 1);
        tft->calibrateTouch(calData, TFT_GREEN, TFT_BLACK, 15);
        tft->setTouch(calData);
        if (nvsHandle) {
            Serial.println("Saving values into NVS");
            err = ESP_OK;
            err = nvsHandle->set_item("x0", calData[0]);
            err |= nvsHandle->set_item("x1", calData[1]);
            err |= nvsHandle->set_item("y0", calData[2]);
            err |= nvsHandle->set_item("y1", calData[3]);
            err |= nvsHandle->set_item("r", calData[4]);
            if (err != ESP_OK) {
                Serial.printf("Failed to store settings in NVS: %d\n", err);
            } else {
                Serial.println("Settings stored in NVS successfully");
            }
            tft->setRotation(rotation);
        }
    } else {
        tft->setTouch(calData);
    }
    Serial.print("\ncalData[5] = ");
    Serial.print("{ ");
    for (uint8_t i = 0; i < 5; i++) {
        Serial.print(calData[i]);
        if (i < 4) Serial.print(", ");
    }
    Serial.println(" }");
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 250;
    else if (brightval == 75) dutyCycle = 130;
    else if (brightval == 50) dutyCycle = 70;
    else if (brightval == 25) dutyCycle = 20;
    else if (brightval == 0) dutyCycle = 0;
    else dutyCycle = ((brightval * 250) / 100);

    log_i("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        Serial.println("Failed to set brightness");
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
struct TouchPointPro {
    uint16_t x;
    uint16_t y;
};
void InputHandler(void) {
    TouchPointPro t;
    static long d_tmp = millis();
    const uint16_t w = tftWidth;
    const uint16_t h = tftHeight + 20;
    if (millis() - d_tmp > 250 || LongPress) { // I know R3CK.. I Should NOT nest if statements..
        // but it is needed to not keep SPI bus used without need, it save resources
#ifdef DONT_USE_INPUT_TASK
        checkPowerSaveTime();
#endif
        bool pressed = tft->getTouch(&t.x, &t.y);
        if (pressed) {
            d_tmp = millis();
            // need to reset the variables to avoid ghost click
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            touchPoint.pressed = false;

            Serial.printf("\nRaw Touch on   x=%d, y=%d, rot=%d\n", t.x, t.y, rotation);
            if (rotation == 1) { // Landscape
                // Do Nothing
            }
            if (rotation == 3) { // Landscape
                t.y = h - t.y;   // invert y
                t.x = w - t.x;   // invert x
            }
            if (rotation == 0) { // Portrait
                int tmp = t.x;   // swap x y
                t.x = w - t.y;   // invert x
                t.y = tmp;
            }
            if (rotation == 2) { // Portrait
                int tmp = t.x;   // swap x y
                t.x = t.y;
                t.y = h - tmp; // invert y
            }
            Serial.printf("Touch Pressed on x=%d, y=%d, rot=%d\n", t.x, t.y, rotation);
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
