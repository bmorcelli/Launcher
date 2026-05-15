#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <interface.h>
static PowersSY6970 PMU;
#define TOUCH_MODULES_CST_SELF
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#define LCD_MODULE_CMD_1

#include <esp_adc_cal.h>

#define BOARD_I2C_SDA 5
#define BOARD_I2C_SCL 6
#define BOARD_SENSOR_IRQ 21
#define BOARD_TOUCH_RST 13
TouchDrvCSTXXX touch;

void touchHomeKeyCallback(void *user_data) {
    launcherConsolePrintf("%s\n", String("Home key pressed!").c_str());
    static uint32_t checkMs = 0;
    if (launcherMillis() > checkMs) {
        if (launcherGpioRead(TFT_BL)) {
            launcherGpioWrite(TFT_BL, LOW);
        } else {
            launcherGpioWrite(TFT_BL, HIGH);
        }
    }
    checkMs = launcherMillis() + 200;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    gpio_hold_dis((gpio_num_t)BOARD_TOUCH_RST); // PIN_TOUCH_RES
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(UP_BTN);
    launcherGpioInput(DW_BTN);

    // CS pins of SPI devices to HIGH
    launcherGpioOutput(15);
    launcherGpioWrite(15, HIGH);
    launcherGpioOutput(9);
    launcherGpioWrite(9, HIGH);
    launcherGpioOutput(6);
    launcherGpioWrite(6, HIGH);

    launcherGpioOutput(BOARD_TOUCH_RST);     // PIN_TOUCH_RES
    launcherGpioWrite(BOARD_TOUCH_RST, LOW); // PIN_TOUCH_RES
    launcherDelayMs(500);
    launcherGpioWrite(BOARD_TOUCH_RST, HIGH); // PIN_TOUCH_RES
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL); // SDA, SCL

    // Initialize capacitive touch
    touch.setPins(BOARD_TOUCH_RST, BOARD_SENSOR_IRQ);
    touch.begin(Wire, CST226SE_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL);
    touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
    touch.setSwapXY(true);
    touch.setMirrorXY(false, false);
    // Set the screen to turn on or off after pressing the screen Home touch button
    touch.setHomeButtonCallback(touchHomeKeyCallback);

    bool hasPMU = PMU.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, SY6970_SLAVE_ADDRESS);
    if (!hasPMU) {
        launcherConsolePrintf("%s\n", String("PMU is not online...").c_str());
    } else {
        PMU.disableOTG();
        PMU.enableMeasure();
        PMU.enableCharge();
    }
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = (PMU.getSystemVoltage() - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
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
    else if (brightval == 0) dutyCycle = 5;
    else dutyCycle = ((brightval * 250) / 100);

    launcherConsolePrintf("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
        ledcDetach(TFT_BL);
        ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(TFT_BL, dutyCycle);
    }
}

struct LTouchPointPro {
    int16_t x[5];
    int16_t y[5];
};
/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    LTouchPointPro t;
    if (launcherMillis() - tm > 200 || LongPress) {
        if (touch.getPoint(t.x, t.y, 1) && touch.isPressed()) {
            tm = launcherMillis();
            if (rotation == 1) { t.y[0] = TFT_WIDTH - t.y[0]; }
            if (rotation == 3) { t.x[0] = TFT_HEIGHT - t.x[0]; }
            // Need to test these 2
            if (rotation == 0) {
                int tmp = t.x[0];
                t.x[0] = t.y[0];
                t.y[0] = tmp;
            }
            if (rotation == 2) {
                int tmp = t.x[0];
                t.x[0] = TFT_WIDTH - t.y[0];
                t.y[0] = TFT_HEIGHT - tmp;
            }

            launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d", t.x[0], t.y[0], rotation);

            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x[0];
            touchPoint.y = t.y[0];
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
        }
    }
}
