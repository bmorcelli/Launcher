#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <Wire.h>
#include <interface.h>
#define BOARD_I2C_SDA 3
#define BOARD_I2C_SCL 2
#define BOARD_SENSOR_IRQ 21
#define BOARD_TOUCH_RST 16
#define SEL_BTN 0

static bool touch_OK = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = BOARD_TOUCH_RST;
    cfg.pin_irq = BOARD_SENSOR_IRQ;
    // rotation:        0      1      2      3
    bool swapXY[4] = {true, false, true, false};
    bool mirrorX[4] = {true, false, false, true};
    bool mirrorY[4] = {false, false, true, true};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

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
    touch_OK = hal_touch_init(touchCfg(), 0x15 /* CST816_SLAVE_ADDRESS */);
    if (touch_OK) {
        // Set the screen to turn on or off after pressing the screen Home touch button
        hal_touch_set_home_button(600, 120, touchHomeKeyCallback);
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
    // The AMOLED has no backlight rail; brightness is the RM67162's 0x51
    // register, which the driver writes for us.
    auto *panel = static_cast<Arduino_RM67162 *>(tft->outputDriver());
    if (panel) panel->setBrightness((brightval * 255) / 100);
}

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
        LTouchPoint t;
        bool touched = hal_touch_read(touchCfg(), t);
        vTaskDelay(pdMS_TO_TICKS(50));
        if ((launcherMillis() - tm) > 200 || LongPress) { // one reading each 500ms
            if (touched) {
                tm = launcherMillis();
                if (!hal_touch_apply(t)) return;
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
}
