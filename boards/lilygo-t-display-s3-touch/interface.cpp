#include "powerSave.h"
#include <SD_MMC.h>
#include <Wire.h>
#include <interface.h>

#define TOUCH_MODULES_CST_SELF
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>
#define LCD_MODULE_CMD_1

#include <esp_adc_cal.h>
TouchDrvCSTXXX touch;
struct TouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};
bool readTouch = false;

#include <Button.h>
volatile bool nxtPress = false;
volatile bool prvPress = false;
volatile bool ecPress = false;
volatile bool slPress = false;
static void onButtonSingleClickCb1(void *button_handle, void *usr_data) { nxtPress = true; }
static void onButtonDoubleClickCb1(void *button_handle, void *usr_data) { slPress = true; }
static void onButtonHoldCb1(void *button_handle, void *usr_data) { slPress = true; }

static void onButtonSingleClickCb2(void *button_handle, void *usr_data) { prvPress = true; }
static void onButtonDoubleClickCb2(void *button_handle, void *usr_data) { ecPress = true; }
static void onButtonHoldCb2(void *button_handle, void *usr_data) { ecPress = true; }

Button *btn1;
Button *btn2;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    gpio_hold_dis((gpio_num_t)21); // PIN_TOUCH_RES
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH); // PIN_POWER_ON
    pinMode(21, OUTPUT);    // PIN_TOUCH_RES
    digitalWrite(21, LOW);  // PIN_TOUCH_RES
    delay(500);
    digitalWrite(21, HIGH); // PIN_TOUCH_RES
    Wire.begin(18, 17);     // SDA, SCL
    // PWM backlight setup
    // setup buttons
    button_config_t bt1 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 600,
        .short_press_time = 120,
        .gpio_button_config = {
                               .gpio_num = DW_BTN,
                               .active_level = 0,
                               },
    };
    button_config_t bt2 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 600,
        .short_press_time = 120,
        .gpio_button_config = {
                               .gpio_num = SEL_BTN,
                               .active_level = 0,
                               },
    };
    pinMode(SEL_BTN, INPUT_PULLUP);

    btn1 = new Button(bt1);

    // btn->attachPressDownEventCb(&onButtonPressDownCb, NULL);
    btn1->attachSingleClickEventCb(&onButtonSingleClickCb1, NULL);
    btn1->attachDoubleClickEventCb(&onButtonDoubleClickCb1, NULL);
    btn1->attachLongPressStartEventCb(&onButtonHoldCb1, NULL);

    btn2 = new Button(bt2);

    // btn->attachPressDownEventCb(&onButtonPressDownCb, NULL);
    btn2->attachSingleClickEventCb(&onButtonSingleClickCb2, NULL);
    btn2->attachDoubleClickEventCb(&onButtonDoubleClickCb2, NULL);
    btn2->attachLongPressStartEventCb(&onButtonHoldCb2, NULL);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, 250);

    Serial.println("Prepraring Touchscreen");
    touch.setPins(21, 16);
    if (!touch.begin(Wire, CST328_SLAVE_ADDRESS, 18, 17)) {
        Serial.println("Failed init CST328 Device!");
        if (!touch.begin(Wire, CST816_SLAVE_ADDRESS, 18, 17)) {
            Serial.println("Failed init CST816 Device!");
        } else readTouch = true;
    } else readTouch = true;
    if (readTouch) {
        // T-Display-S3 CST816 touch panel, touch button coordinates are is 85 , 160
        touch.setCenterButtonCoordinate(85, 360);

        // Depending on the touch panel, not all touch panels have touch buttons.
        touch.setHomeButtonCallback(
            [](void *user_data) {
                static uint32_t checkMs = 0;
                if (millis() > checkMs) {
                    if (!wakeUpScreen()) {
                        AnyKeyPress = true;
                        EscPress = true;
                    }
                }
                checkMs = millis() + 200;
            },
            NULL
        );

        // If you poll the touch, you need to turn off the automatic sleep function, otherwise there will be
        // an I2C access error. If you use the interrupt method, you don't need to turn it off, saving power
        // consumption
        touch.disableAutoSleep();
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    uint8_t percent;
    uint32_t volt = 0;
    // analogReadMilliVolts(GPIO_NUM_38);

    float mv = volt;
    percent = (mv - 3300) * 100 / (float)(4150 - 3350);

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

    Serial.printf("dutyCycle for bright 0-255: %d\n", dutyCycle);

    vTaskDelay(10 / portTICK_PERIOD_MS);
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
void InputHandler(void) {
    static long tm = millis();
    static long tm2 = millis();
    static bool btn_pressed = false;
    if (nxtPress || prvPress || ecPress || slPress) btn_pressed = true;

    if (millis() - tm > 200 || LongPress) {
        if (btn_pressed) {
            btn_pressed = false;
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;
            SelPress = slPress;
            EscPress = ecPress;
            NextPress = nxtPress;
            PrevPress = prvPress;

            nxtPress = false;
            prvPress = false;
            ecPress = false;
            slPress = false;
        }
        if (!readTouch) return; // dont have touchscreen
        TouchPointPro t;
        uint8_t touched = 0;
        touched = touch.getPoint(&t.x, &t.y);

        if (touched) {
            // Serial.printf(
            //     "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d", t.x, t.y, rotation, millis(), tm
            // );
            tm = millis();
            static uint8_t rot = 5;
            if (rot != rotation) {
                if (rotation == 1) {
                    touch.setMaxCoordinates(320, 170);
                    touch.setSwapXY(true);
                    touch.setMirrorXY(false, true);
                }
                if (rotation == 3) {
                    touch.setMaxCoordinates(320, 170);
                    touch.setSwapXY(true);
                    touch.setMirrorXY(true, false);
                }
                if (rotation == 0) {
                    touch.setMaxCoordinates(170, 320);
                    touch.setSwapXY(false);
                    touch.setMirrorXY(false, true);
                }
                if (rotation == 2) {
                    touch.setMaxCoordinates(170, 320);
                    touch.setSwapXY(false);
                    touch.setMirrorXY(true, false);
                }
                rot = rotation;
            }
            if (!wakeUpScreen()) AnyKeyPress = true;
            else return;

            // Touch point global variable
            touchPoint.x = t.x;
            touchPoint.y = t.y;
            touchPoint.pressed = true;
            touchHeatMap(touchPoint);
            touched = 0;
            return;
        }
    }
}
/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {}
