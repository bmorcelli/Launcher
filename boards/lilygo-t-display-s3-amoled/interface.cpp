#include "powerSave.h"
#include <TouchDrv.hpp>
#include <interface.h>
#include "idf/launcher_platform.h"
#define BOARD_I2C_SDA 3
#define BOARD_I2C_SCL 2
#define BOARD_SENSOR_IRQ 21
#define BOARD_TOUCH_RST 16
TouchDrvCST816 touch;
static bool touch_OK = false;

void touchHomeKeyCallback(void *user_data) {
    launcherConsolePrintf("%s\n", String("Home key pressed!").c_str());
    static uint32_t checkMs = 0;
    if (launcherMillis() > checkMs) {
        EscPress = true;
        AnyKeyPress = true;
        wakeUpScreen();
    }
    checkMs = launcherMillis() + 200;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    launcherGpioOutput(BOARD_TOUCH_RST); // PIN_TOUCH_RES
    launcherGpioOutput(38 /* PMIC_EN */);
    launcherGpioInputPullup(SEL_BTN); // SEL_BTN

    launcherGpioWrite(38 /* PMIC_EN */, HIGH);
    launcherGpioWrite(BOARD_TOUCH_RST, LOW); // PIN_TOUCH_RES
    launcherDelayMs(100);
    launcherGpioWrite(BOARD_TOUCH_RST, HIGH); // PIN_TOUCH_RES

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL); // SDA, SCL

    // Initialize capacitive touch
    touch.setPins(BOARD_TOUCH_RST, BOARD_SENSOR_IRQ);
    if (touch.begin(Wire, CST816_SLAVE_ADDRESS, BOARD_I2C_SDA, BOARD_I2C_SCL)) {
        touch_OK = true;
        // Set the screen to turn on or off after pressing the screen Home touch button
        touch.setCenterButtonCoordinate(600, 120);
        touch.setHomeButtonCallback(touchHomeKeyCallback, NULL);
    } else {
    }
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    extern Arduino_DataBus *bus;
    bus->beginWrite();
    bus->writeC8D8(0x51, (brightval * 255) / 100);
    bus->endWrite();
}

struct LTouchPointPro {
    int16_t x = 0;
    int16_t y = 0;
};
/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    constexpr unsigned long kSelectPressMs = 550;
    constexpr unsigned long kBackPressMs = 1200;
    constexpr unsigned long kDoublePressIntervalMs = 300;
    static unsigned long tm = 0;
    static bool buttonWasDown = false;
    static unsigned long buttonDownAt = 0;
    static uint8_t drawn = 2;
    // Variables for double press detection
    static unsigned long lastButtonReleaseTime = 0;
    static int clickCount = 0;
    static bool pendingNextPress = false;
    static unsigned long pendingTime = 0;
    if (touch_OK) {
        LTouchPointPro t;
        uint8_t touched = 0;
        uint8_t rot = 5;
        if (rot != rotation) {
            if (rotation == 0) {
                touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
                touch.setSwapXY(true);
                touch.setMirrorXY(true, false);
            }
            if (rotation == 2) {
                touch.setMaxCoordinates(TFT_WIDTH, TFT_HEIGHT);
                touch.setSwapXY(true);
                touch.setMirrorXY(false, true);
            }
            if (rotation == 1) {
                touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
                touch.setSwapXY(false);
                touch.setMirrorXY(false, false);
            }
            if (rotation == 3) {
                touch.setMaxCoordinates(TFT_HEIGHT, TFT_WIDTH);
                touch.setSwapXY(false);
                touch.setMirrorXY(true, true);
            }
            rot = rotation;
        }
        touched = touch.getPoint(&t.x, &t.y, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        if ((launcherMillis() - tm) > 200 || LongPress) { // one reading each 500ms

            // launcherConsolePrintf("\nPressed x=%d , y=%d, rot: %d",t.x, t.y, rotation);
            if (touched) {

                launcherConsolePrintf(
                    "\nPressed x=%d , y=%d, rot: %d, millis=%d, tmp=%d", t.x, t.y, rotation, launcherMillis(), tm
                );
                tm = launcherMillis();

                if (!wakeUpScreen()) AnyKeyPress = true;
                else return;

                // Touch point global variable
                touchPoint.x = t.x;
                touchPoint.y = t.y;
                touchPoint.pressed = true;
                touchHeatMap(touchPoint);
            }
        }
    } else {
        constexpr unsigned long kInputDebounceMs = 75;
        if (launcherMillis() - tm < kInputDebounceMs && !LongPress) return;
        checkPowerSaveTime();
        // Check for pending NextPress timeout
        if (pendingNextPress && launcherMillis() - pendingTime > kDoublePressIntervalMs) {
            NextPress = true;
            pendingNextPress = false;
        }

        bool buttonDown = (launcherGpioRead(0) == LOW);

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
}
