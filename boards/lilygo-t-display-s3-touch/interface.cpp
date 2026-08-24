#include "hal/device.h"
#include "hal/inputs/buttons.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <Wire.h>
#include <interface.h>

#define LCD_MODULE_CMD_1

#define SEL_BTN 0
#define DW_BTN 14

#include <esp_adc_cal.h>
bool readTouch = false;

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = 21;
    cfg.pin_irq = 16;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {true, true, false, false};
    for (int i = 0; i < 4; i++) {
        cfg.SwapXY[i] = swapXY[i];
        cfg.MirrorX[i] = mirrorX[i];
        cfg.MirrorY[i] = mirrorY[i];
    }
    return cfg;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    gpio_hold_dis((gpio_num_t)21); // PIN_TOUCH_RES
    launcherGpioOutput(15);
    launcherGpioWrite(15, HIGH); // PIN_POWER_ON
    launcherGpioOutput(21);      // PIN_TOUCH_RES
    launcherGpioWrite(21, LOW);  // PIN_TOUCH_RES
    launcherDelayMs(500);
    launcherGpioWrite(21, HIGH); // PIN_TOUCH_RES
    Wire.begin(18, 17);          // SDA, SCL

    // SEL_BTN -> Next (single click) / Sel (double click or 600ms hold)
    // DW_BTN  -> Prev (single click) / Esc (double click or 600ms hold)
    hal_buttons_init_2(DeviceButtons{SEL_BTN, DW_BTN}, 600);
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

    launcherConsolePrintf("%s\n", String("Prepraring Touchscreen").c_str());
    // hal_touch_init() retries at CST816_SLAVE_ADDRESS (0x15) on its own if
    // this primary address doesn't answer -- some panels identify as either
    // depending on batch.
    readTouch = hal_touch_init(touchCfg(), 0x1A /* CST328_SLAVE_ADDRESS */);
    if (!readTouch) {
        launcherConsolePrintf("%s\n", String("Failed init touch panel!").c_str());
    } else {
        // T-Display-S3 CST816 touch panel, touch button coordinates are is 85 , 160
        // Depending on the touch panel, not all touch panels have touch buttons.
        hal_touch_set_home_button(85, 360, [](void *user_data) {
            static uint32_t checkMs = 0;
            if (launcherMillis() > checkMs) {
                if (!wakeUpScreen()) {
                    AnyKeyPress = true;
                    EscPress = true;
                }
            }
            checkMs = launcherMillis() + 200;
        });

        // If you poll the touch, you need to turn off the automatic sleep function, otherwise there will be
        // an I2C access error. If you use the interrupt method, you don't need to turn it off, saving power
        // consumption
        hal_touch_disable_auto_sleep();
    }
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
    else if (brightval == 0) dutyCycle = 5;
    else dutyCycle = ((brightval * 250) / 100);

    launcherConsolePrintf("dutyCycle for bright 0-255: %d\n", dutyCycle);

    vTaskDelay(10 / portTICK_PERIOD_MS);
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
    hal_buttons_poll_2();

    if (!readTouch) return; // dont have touchscreen
    static long tm = launcherMillis();
    if (launcherMillis() - tm < 200 && !LongPress) return;
    LTouchPoint t;
    if (hal_touch_read(touchCfg(), t)) {
        tm = launcherMillis();
        hal_touch_apply(t);
    }
}
