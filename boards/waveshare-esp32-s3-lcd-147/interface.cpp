#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <interface.h>

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
#ifdef USE_SD_MMC
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
#endif

    pinMode(TFT_BL, OUTPUT);
    launcherGpioWrite(TFT_BL, HIGH);

    launcherGpioOutput(TFT_CS);
    launcherGpioWrite(TFT_CS, HIGH);

    launcherGpioOutput(TFT_DC);
    launcherGpioWrite(TFT_DC, HIGH);

    launcherGpioOutput(TFT_RST);
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(10);
    launcherGpioWrite(TFT_RST, LOW);
    launcherDelayMs(20);
    launcherGpioWrite(TFT_RST, HIGH);
    launcherDelayMs(120);

    launcherGpioInputPullup(SEL_BTN);
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
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, 0);
        return;
    }

    int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100.0));
    analogWrite(TFT_BL, bl);
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
