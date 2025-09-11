#include "powerSave.h"
#include <Button.h>
#include <SD_MMC.h>
#include <interface.h>
volatile bool nxtPress = false;
volatile bool prvPress = false;
volatile bool slPress = false;
static void onButtonSingleClickCb1(void *button_handle, void *usr_data) { nxtPress = true; }
static void onButtonDoubleClickCb1(void *button_handle, void *usr_data) { prvPress = true; }
static void onButtonHoldCb1(void *button_handle, void *usr_data) { slPress = true; }

Button *btn1;

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    //  PWM backlight setup
    //  setup buttons
    gpio_pulldown_dis(GPIO_NUM_21);
    gpio_pullup_dis(GPIO_NUM_21);
    gpio_pulldown_dis(GPIO_NUM_17);
    gpio_pullup_dis(GPIO_NUM_17);
    button_config_t bt1 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 600,
        .short_press_time = 120,
        .gpio_button_config = {
                               .gpio_num = GPIO_NUM_0,
                               .active_level = 0,
                               },
    };

    pinMode(GPIO_NUM_0, INPUT_PULLUP);

    btn1 = new Button(bt1);

    // btn->attachPressDownEventCb(&onButtonPressDownCb, NULL);
    btn1->attachSingleClickEventCb(&onButtonSingleClickCb1, NULL);
    btn1->attachDoubleClickEventCb(&onButtonDoubleClickCb1, NULL);
    btn1->attachLongPressStartEventCb(&onButtonHoldCb1, NULL);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    ledcAttach(GFX_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(GFX_BL, 0);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() { return 0; }

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    int dutyCycle;
    if (brightval == 100) dutyCycle = 0;
    else if (brightval == 75) dutyCycle = 5;
    else if (brightval == 50) dutyCycle = 20;
    else if (brightval == 25) dutyCycle = 135;
    else if (brightval == 0) dutyCycle = 250;
    else dutyCycle = 250 - ((brightval * 250) / 100);

    Serial.printf("dutyCycle for bright 0-255: %d", dutyCycle);
    if (!ledcWrite(GFX_BL, dutyCycle)) {
        Serial.println("Failed to set brightness");
        ledcDetach(GFX_BL);
        ledcAttach(GFX_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
        ledcWrite(GFX_BL, dutyCycle);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static bool btn_pressed = false;
    if (nxtPress || prvPress || slPress) btn_pressed = true;

    bool anyPressed = nxtPress || prvPress || slPress;
    if (anyPressed && wakeUpScreen()) return;

    AnyKeyPress = anyPressed;
    SelPress = slPress;
    NextPress = nxtPress;
    PrevPress = prvPress;

    if (btn_pressed) {
        btn_pressed = false;
        nxtPress = false;
        prvPress = false;
        slPress = false;
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
