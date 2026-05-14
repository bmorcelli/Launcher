#include "powerSave.h"
#include <AXP192.h>
#include <interface.h>
#include "idf/launcher_platform.h"
AXP192 axp192;

/***************************************************************************************
** Function name: _setup_gpio()
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(DW_BTN);
    // https://github.com/pr3y/Bruce/blob/main/media/connections/cc1101_stick_SDCard.jpg
    launcherGpioOutput(33);    // Keeps this pin high to allow working with the following pinout
    launcherGpioWrite(33, HIGH); // Keeps this pin high to allow working with the following pinout
    axp192.begin();         // Start the energy management of AXP192
}

/***************************************************************************************
** Function name: getBattery()
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    float b = axp192.GetBatVoltage();
    percent = ((b - 3.0) / 1.2) * 100;

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
**  Function: setBrightness
**  set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval > 100) brightval = 100;
    axp192.ScreenBreath(brightval);
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (launcherMillis() - tm < 200 && !LongPress) return;

    bool upPressed = (axp192.GetBtnPress());
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

void powerOff() { axp192.PowerOff(); }

void checkReboot() {
    static unsigned long time_count = 0;
    static bool armed = false;
    int countDown;
    /* Long press power off */
    if (axp192.GetBtnPress()) {
        if (armed == false) {
            time_count = launcherMillis();
            armed = true;
            return;
        }
        if (launcherMillis() - time_count < 500) return;

        while (axp192.GetBtnPress()) {
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
