#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <interface.h>

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

    launcherGpioInputPullup(SEL_BTN);
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    pinMode(GFX_BL, OUTPUT);
    ledcAttach(GFX_BL, TFT_BRIGHT_FREQ, TFT_BRIGHT_Bits);
    ledcWrite(GFX_BL, bright);
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
    if (!ledcWrite(GFX_BL, dutyCycle)) {
        launcherConsolePrintf("%s\n", String("Failed to set brightness").c_str());
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
    static unsigned long tm = 0;
    constexpr unsigned long kInputDebounceMs = 75;
    if (launcherMillis() - tm < kInputDebounceMs && !LongPress) return;

    checkPowerSaveTime();

    static bool buttonWasDown = false;
    static unsigned long buttonDownAt = 0;
    static uint8_t drawn = 2;
    constexpr unsigned long kSelectPressMs = 550;
    constexpr unsigned long kBackPressMs = 1200;
    constexpr unsigned long kDoublePressIntervalMs = 300;

    // Variables for double press detection
    static unsigned long lastButtonReleaseTime = 0;
    static int clickCount = 0;
    static bool pendingNextPress = false;
    static unsigned long pendingTime = 0;

    // Check for pending NextPress timeout
    if (pendingNextPress && launcherMillis() - pendingTime > kDoublePressIntervalMs) {
        NextPress = true;
        pendingNextPress = false;
    }

    bool buttonDown = (launcherGpioRead(SEL_BTN) == LOW);

    if (buttonDown && !buttonWasDown) {
        buttonWasDown = true;
        buttonDownAt = launcherMillis();
        tm = launcherMillis();
        AnyKeyPress = true;
        LongPress = false;
        if (wakeUpScreen()) return;
    }

    if (buttonDown) {
        AnyKeyPress = true;
        if (launcherMillis() - buttonDownAt >= kSelectPressMs) {
            LongPress = true;
            if (drawn > 1) {
                tft->fillRect(tftWidth - 3, 0, 3, tftHeight, GREENYELLOW);
                tft->fillRect(0, tftHeight - 3, tftWidth, 3, GREENYELLOW);
                drawn = 1;
            }
        }
        if (launcherMillis() - buttonDownAt >= kBackPressMs && drawn > 0) {
            tft->fillRect(tftWidth - 3, 0, 3, tftHeight, RED);
            tft->fillRect(0, tftHeight - 3, tftWidth, 3, RED);
            drawn = 0;
        }
        return;
    }

    if (buttonWasDown) {
        buttonWasDown = false;
        unsigned long heldMs = launcherMillis() - buttonDownAt;
        tft->fillRect(tftWidth - 3, 0, 3, tftHeight, BGCOLOR);
        tft->fillRect(0, tftHeight - 3, tftWidth, 3, BGCOLOR);
        drawn = 2;

        // Reset click count if more than 300ms has passed since last release
        if (launcherMillis() - lastButtonReleaseTime > kDoublePressIntervalMs) { clickCount = 0; }

        if (heldMs >= kBackPressMs) {
            EscPress = true;
            pendingNextPress = false; // Cancel any pending actions
        } else if (heldMs >= kSelectPressMs) {
            SelPress = true;
            pendingNextPress = false; // Cancel any pending actions
        } else {
            // Short click - handle double press detection
            clickCount++;
            lastButtonReleaseTime = launcherMillis();

            if (clickCount >= 2) {
                PrevPress = true;
                clickCount = 0;
                pendingNextPress = false; // Cancel any pending NextPress
                AnyKeyPress = true;
                LongPress = false;
                return;
            } else {
                // First click - wait for potential double click
                pendingNextPress = true;
                pendingTime = launcherMillis();
            }
        }
        AnyKeyPress = true;
        LongPress = false;
    }
}
