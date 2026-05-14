#include "powerSave.h"
#include <interface.h>
#include "idf/launcher_platform.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInput(UP_BTN); // Sets the power btn as an INPUT
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(DW_BTN);
    launcherGpioOutput(4);    // Keeps the Stick alive after take off the USB cable
    launcherGpioWrite(4, HIGH); // Keeps the Stick alive after take off the USB cable
    // https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick_SDCard.jpg
    // Keeps this pin high to allow working with the following pinout
    // Keeps this pin high to allow working with the following pinout
    launcherGpioOutput(32);
    launcherGpioOutput(33);
    launcherGpioWrite(32, LOW);
    launcherGpioWrite(33, HIGH);
    gpio_pulldown_dis(GPIO_NUM_36);
    gpio_pullup_dis(GPIO_NUM_36);
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool upPressed = (launcherGpioRead(UP_BTN) == LOW);
    bool selPressed = (launcherGpioRead(SEL_BTN) == LOW);
    bool dwPressed = (launcherGpioRead(DW_BTN) == LOW);

    bool anyPressed = upPressed || selPressed || dwPressed;
    if (anyPressed) tm = launcherMillis();
    if (anyPressed && wakeUpScreen()) return;

    AnyKeyPress = anyPressed;
    EscPress = upPressed & dwPressed;
    if (EscPress) return;
    PrevPress = upPressed;
    NextPress = dwPressed;
    SelPress = selPressed;
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    launcherGpioWrite(4, LOW);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)UP_BTN, LOW);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to tornoff the device (name is odd btw)
**********************************************************************/
void checkReboot() {
    static unsigned long time_count = 0;
    static bool armed = false;
    int countDown;
    /* Long press power off */
    if (launcherGpioRead(UP_BTN) == LOW) {
        if (armed == false) {
            time_count = launcherMillis();
            armed = true;
            return;
        }
        if (launcherMillis() - time_count < 500) return;

        while (launcherGpioRead(UP_BTN) == LOW) {
            // Display poweroff bar only if holding button
            if (launcherMillis() - time_count > 500) {
                tft->setCursor(60, 12);
                tft->setTextSize(1);
                tft->setTextColor(FGCOLOR, BGCOLOR);
                countDown = (launcherMillis() - time_count) / 1000 + 1;
                tft->printf(" PWR OFF IN %d/3\n", countDown);
                launcherDelayMs(10);
            }
        }
        // Clear text after releasing the button
        tft->fillRect(60, 12, tftWidth - 60, 8, BGCOLOR);
    }
    armed = false;
}
