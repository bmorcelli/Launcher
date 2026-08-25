#include "hal/device.h"
#include "hal/inputs/buttons.h"
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

static DeviceButtons buttonsCfg() { return DeviceButtons{SEL_BTN}; }

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

void _setup_gpio() {
    launcherGpioOutput(BOARD_TOUCH_RST); // PIN_TOUCH_RES
    launcherGpioOutput(38 /* PMIC_EN */);
    hal_buttons_init(buttonsCfg(), 1);

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

int getBattery() {
    int percent = 0;
    return (percent < 0) ? 0 : (percent >= 100) ? 100 : percent;
}

void _setBrightness(uint8_t brightval) {
    // The AMOLED has no backlight rail; brightness is the RM67162's 0x51
    // register, which the driver writes for us.
    auto *panel = static_cast<Arduino_RM67162 *>(tft->outputDriver());
    if (panel) panel->setBrightness((brightval * 255) / 100);
}

void InputHandler(void) {
    if (touch_OK) {
        static unsigned long tm = 0;
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
        hal_buttons_poll_1(buttonsCfg());
    }
}
