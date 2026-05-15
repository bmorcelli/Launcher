#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInputPullup(UP_BTN); // Sets the power btn as an INPUT
    launcherGpioInputPullup(SEL_BTN);
    launcherGpioInputPullup(DW_BTN);
    launcherGpioInputPullup(R_BTN);
    launcherGpioInputPullup(L_BTN);
}
/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    pinMode(TFT_BL, OUTPUT);
    ledcAttach(TFT_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(TFT_BL, bright);
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

    launcherConsolePrintf("dutyCycle for bright 0-255: %d\n", dutyCycle);
    if (!ledcWrite(TFT_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
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
    static unsigned long tm = launcherMillis();
    if (launcherMillis() - tm > 200 || LongPress) {
    } else return;

    bool u = launcherGpioRead(UP_BTN);
    bool d = launcherGpioRead(DW_BTN);
    bool r = launcherGpioRead(R_BTN);
    bool l = launcherGpioRead(L_BTN);
    bool s = launcherGpioRead(SEL_BTN);
    if (s == BTN_ACT || u == BTN_ACT || d == BTN_ACT || r == BTN_ACT || l == BTN_ACT) {
        tm = launcherMillis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }
    if (l == BTN_ACT && r == BTN_ACT) {
        EscPress = true;
        return;
    }
    if (l == BTN_ACT) PrevPress = true;
    if (r == BTN_ACT) NextPress = true;
    if (u == BTN_ACT) UpPress = true;
    if (d == BTN_ACT) DownPress = true;
    if (s == BTN_ACT) SelPress = true;
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_34, LOW);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}
