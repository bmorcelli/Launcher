#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <interface.h>

#define UP_BTN 2
#define DW_BTN 13
#define L_BTN 27
#define R_BTN 35
#define SEL_BTN 34
#define ESC_BTN 12
#define BTN_ACT LOW

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);
    launcherGpioOutput(SDCARD_CS);
    launcherGpioWrite(SDCARD_CS, HIGH);

    launcherGpioInputPullup(UP_BTN);
    launcherGpioInputPullup(DW_BTN);
    launcherGpioInputPullup(L_BTN);
    launcherGpioInputPullup(R_BTN);
    launcherGpioInputPullup(SEL_BTN);
    launcherGpioInputPullup(ESC_BTN);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {}

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
void _setBrightness(uint8_t brightval) { (void)brightval; }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool up = launcherGpioRead(UP_BTN);
    bool down = launcherGpioRead(DW_BTN);
    bool left = launcherGpioRead(L_BTN);
    bool right = launcherGpioRead(R_BTN);
    bool select = launcherGpioRead(SEL_BTN);
    bool escape = launcherGpioRead(ESC_BTN);

    if (up == BTN_ACT || down == BTN_ACT || left == BTN_ACT || right == BTN_ACT || select == BTN_ACT ||
        escape == BTN_ACT) {
        tm = launcherMillis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    } else return;

    if (escape == BTN_ACT || (left == BTN_ACT && right == BTN_ACT)) {
        EscPress = true;
        return;
    }
    if (left == BTN_ACT) PrevPress = true;
    if (right == BTN_ACT) NextPress = true;
    if (up == BTN_ACT) UpPress = true;
    if (down == BTN_ACT) DownPress = true;
    if (select == BTN_ACT) SelPress = true;
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
