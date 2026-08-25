#include "hal/bright/bright.h"
#include "hal/device.h"
#include "hal/inputs/touch.h"
#include "idf/launcher_platform.h"
#include "powerSave.h"
#include <SD_MMC.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <interface.h>
static PowersSY6970 PMU;
#define LCD_MODULE_CMD_1
// buttons, not used here, but defined for the interface
#define SEL_BTN 0
#define UP_BTN 12
#define DW_BTN 16
#define BTN_ACT LOW

#include <esp_adc_cal.h>

#define BOARD_I2C_SDA 5
#define BOARD_I2C_SCL 6
#define BOARD_SENSOR_IRQ 21
#define BOARD_TOUCH_RST 13

static DeviceTouch touchCfg() {
    DeviceTouch cfg;
    cfg.pin_rst = BOARD_TOUCH_RST;
    cfg.pin_irq = BOARD_SENSOR_IRQ;
    // rotation:        0      1      2      3
    bool swapXY[4] = {false, true, false, true};
    bool mirrorX[4] = {false, false, true, true};
    bool mirrorY[4] = {false, true, true, false};
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
        if (launcherGpioRead(TFT_BL)) {
            launcherGpioWrite(TFT_BL, LOW);
        } else {
            launcherGpioWrite(TFT_BL, HIGH);
        }
    }
    checkMs = launcherMillis() + 200;
}

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/
void _setup_gpio() {
    gpio_hold_dis((gpio_num_t)BOARD_TOUCH_RST); // PIN_TOUCH_RES
    launcherGpioInput(SEL_BTN);
    launcherGpioInput(UP_BTN);
    launcherGpioInput(DW_BTN);

    // CS pins of SPI devices to HIGH
    launcherGpioOutput(15);
    launcherGpioWrite(15, HIGH);
    launcherGpioOutput(9);
    launcherGpioWrite(9, HIGH);
    launcherGpioOutput(6);
    launcherGpioWrite(6, HIGH);

    launcherGpioOutput(BOARD_TOUCH_RST);     // PIN_TOUCH_RES
    launcherGpioWrite(BOARD_TOUCH_RST, LOW); // PIN_TOUCH_RES
    launcherDelayMs(500);
    launcherGpioWrite(BOARD_TOUCH_RST, HIGH); // PIN_TOUCH_RES
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL); // SDA, SCL

    // Initialize capacitive touch
    hal_touch_init(touchCfg(), 0x5A /* CST226SE_SLAVE_ADDRESS */);
    // Set the screen to turn on or off after pressing the screen Home touch button
    hal_touch_set_home_button(-1, -1, touchHomeKeyCallback);

    bool hasPMU = PMU.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, SY6970_SLAVE_ADDRESS);
    if (!hasPMU) {
        launcherConsolePrintf("%s\n", String("PMU is not online...").c_str());
    } else {
        PMU.disableOTG();
        PMU.enableMeasure();
        PMU.enableCharge();
    }
}

/***************************************************************************************
** Function name: _post_setup_gpio()
** Location: main.cpp
** Description:   second stage gpio setup to make a few functions work
***************************************************************************************/
void _post_setup_gpio() {
    // PWM backlight setup
    hal_bright_attach(TFT_BL);
    hal_bright_set(TFT_BL, bright);
}

/***************************************************************************************
** Function name: getBattery()
** location: display.cpp
** Description:   Delivers the battery value from 1-100
***************************************************************************************/
int getBattery() {
    int percent = 0;
    percent = (PMU.getSystemVoltage() - 3300) * 100 / (float)(4150 - 3350);

    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) { hal_bright_set(TFT_BL, brightval); }

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static long tm = 0;
    if (launcherMillis() - tm > 200 || LongPress) {
        LTouchPoint t;
        if (hal_touch_read(touchCfg(), t)) {
            tm = launcherMillis();
            if (!hal_touch_apply(t)) return;
        }
    }
}
