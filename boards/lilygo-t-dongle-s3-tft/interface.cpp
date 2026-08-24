#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <interface.h>
#ifdef CONFIG_IDF_TARGET_ESP32C5
#define SEL_BTN 28
#else
#define SEL_BTN 0
#endif

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
#ifdef SOC_SDMMC_HOST_SUPPORTED
    /* T-DONGLE S3 */
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    gpio_pulldown_dis(GPIO_NUM_21);
    gpio_pullup_dis(GPIO_NUM_21);
    gpio_pulldown_dis(GPIO_NUM_17);
    gpio_pullup_dis(GPIO_NUM_17);
    // Turn off LED
    launcherGpioOutput(39);
    launcherGpioWrite(39, LOW);
    launcherGpioOutput(40);
    launcherGpioWrite(40, LOW);
#else
    /* T-DONGLE C5 */
    // turn off LED
    launcherGpioOutput(4);
    launcherGpioWrite(4, LOW);
    launcherGpioOutput(5);
    launcherGpioWrite(5, LOW);
#endif

    hal_buttons_init(DeviceButtons{SEL_BTN}, 1);
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

    launcherConsolePrintf("dutyCycle for bright 0-255: %d", dutyCycle);
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
void InputHandler(void) { hal_buttons_poll_1(DeviceButtons{SEL_BTN}); }
